//! Encryption between the agent and the relay: Noise `NK` (client anonymous, server known by its static key)
//! over X25519, ChaCha20-Poly1305 and SHA-256, using the `snow` crate. Nothing here is hand-rolled crypto.
//!
//! * The first handshake message carries the client's `Hello` (serial, room, password proof) or `Register`,
//!   encrypted to the server's key, so an eavesdropper sees none of it. Only the real server can answer, which
//!   is what makes the server's identity checkable (clients pin its key, see `fingerprint`).
//! * The reply carries `Welcome` / `Issued` / `Reject` under keys that include a fresh server secret.
//! * After that every datagram is `counter || ChaCha20-Poly1305(packet)` with a per-direction key, a replay
//!   window, and no way to forge or read it without the session keys.
use crate::{sha256, Packet};
use snow::{Builder, HandshakeState, StatelessTransportState};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

pub const PARAMS: &str = "Noise_NK_25519_ChaChaPoly_SHA256";
pub type Key = [u8; 32];

const MAX_MSG: usize = 2048;
/// Counter (8) in front of every transport datagram.
const COUNTER: usize = 8;

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    /// Failed to decrypt or verify: wrong key, damaged or forged.
    Crypto,
    /// Decrypted, but not a packet we understand.
    Format,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Error::Crypto => "could not decrypt or verify the message",
            Error::Format => "unexpected message",
        })
    }
}

impl std::error::Error for Error {}

fn builder() -> Builder<'static> {
    Builder::new(PARAMS.parse().expect("valid noise parameters"))
}

/// A new (private, public) X25519 pair for a server.
pub fn generate_keypair() -> (Key, Key) {
    let kp = builder().generate_keypair().expect("system randomness");
    (kp.private.try_into().expect("32 bytes"), kp.public.try_into().expect("32 bytes"))
}

/// What a person compares to check a server is the one they meant: 16 hex digits, grouped.
pub fn fingerprint(public: &Key) -> String {
    let h = sha256(&[b"rallypoint/key/v1\0", public]);
    let hex: String = h[..8].iter().map(|b| format!("{b:02X}")).collect();
    format!("{}-{}-{}-{}", &hex[0..4], &hex[4..8], &hex[8..12], &hex[12..16])
}

pub fn to_hex(k: &Key) -> String {
    k.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn from_hex(s: &str) -> Option<Key> {
    let s = s.trim();
    if s.len() != 64 || !s.is_ascii() {
        return None;
    }
    let mut k = [0u8; 32];
    for (i, b) in k.iter_mut().enumerate() {
        *b = u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).ok()?;
    }
    Some(k)
}

// ---- handshake -------------------------------------------------------------------------------------------

/// Client side, waiting for the server's reply.
pub struct ClientPending(HandshakeState);

/// First message: `first` (a `Hello` or `Register`) encrypted to `server_pub`. Send it, and keep the pending state
/// to read the reply; resend the same bytes if the reply is lost.
pub fn client_start(server_pub: &Key, first: &Packet) -> Result<(Vec<u8>, ClientPending), Error> {
    let mut hs = builder().remote_public_key(server_pub).build_initiator().map_err(|_| Error::Crypto)?;
    let mut out = vec![0u8; MAX_MSG];
    let n = hs.write_message(&first.encode(), &mut out).map_err(|_| Error::Crypto)?;
    out.truncate(n);
    Ok((out, ClientPending(hs)))
}

impl ClientPending {
    /// The server's reply: its packet, and the session if the handshake is complete.
    pub fn finish(mut self, msg2: &[u8]) -> Result<(Packet, Session), Error> {
        let mut payload = vec![0u8; MAX_MSG];
        let n = self.0.read_message(msg2, &mut payload).map_err(|_| Error::Crypto)?;
        let packet = Packet::decode(&payload[..n]).ok_or(Error::Format)?;
        Ok((packet, Session::new(self.0)?))
    }
}

/// Server side, between reading the client's first message and answering it.
pub struct ServerPending(HandshakeState);

/// Decrypt a first message. Fails (and so is dropped silently) for anything not encrypted to this server's key.
pub fn server_accept(private: &Key, msg1: &[u8]) -> Result<(Packet, ServerPending), Error> {
    let mut hs = builder().local_private_key(private).build_responder().map_err(|_| Error::Crypto)?;
    let mut payload = vec![0u8; MAX_MSG];
    let n = hs.read_message(msg1, &mut payload).map_err(|_| Error::Crypto)?;
    Ok((Packet::decode(&payload[..n]).ok_or(Error::Format)?, ServerPending(hs)))
}

impl ServerPending {
    /// Answer with `reply`. The session is only worth keeping when the reply is a `Welcome`.
    pub fn reply(mut self, reply: &Packet) -> Result<(Vec<u8>, Session), Error> {
        let mut out = vec![0u8; MAX_MSG];
        let n = self.0.write_message(&reply.encode(), &mut out).map_err(|_| Error::Crypto)?;
        out.truncate(n);
        Ok((out, Session::new(self.0)?))
    }
}

// ---- transport -------------------------------------------------------------------------------------------

/// Which counters have been seen: the highest, and the 64 below it.
#[derive(Default)]
struct Replay {
    top: u64,
    seen: u64,
    any: bool,
}

impl Replay {
    fn fresh(&self, c: u64) -> bool {
        if !self.any || c > self.top {
            return true;
        }
        let back = self.top - c;
        back < 64 && self.seen & (1 << back) == 0
    }

    fn mark(&mut self, c: u64) {
        if !self.any || c > self.top {
            let shift = if self.any { c - self.top } else { 64 };
            self.seen = if shift >= 64 { 0 } else { self.seen << shift };
            self.top = c;
            self.any = true;
        }
        self.seen |= 1 << (self.top - c);
    }
}

/// Keys for one connection, usable from several threads.
pub struct Session {
    state: StatelessTransportState,
    next: AtomicU64,
    replay: Mutex<Replay>,
}

impl Session {
    fn new(hs: HandshakeState) -> Result<Session, Error> {
        Ok(Session { state: hs.into_stateless_transport_mode().map_err(|_| Error::Crypto)?, next: AtomicU64::new(0), replay: Mutex::default() })
    }

    /// `counter || ciphertext` for `p`.
    pub fn seal(&self, p: &Packet) -> Vec<u8> {
        let counter = self.next.fetch_add(1, Ordering::Relaxed);
        let plain = p.encode();
        let mut out = vec![0u8; COUNTER + plain.len() + 16];
        out[..COUNTER].copy_from_slice(&counter.to_be_bytes());
        let n = self.state.write_message(counter, &plain, &mut out[COUNTER..]).expect("sized for the tag");
        out.truncate(COUNTER + n);
        out
    }

    /// The packet in `data`, if it is authentic, new and well formed. Anything else is `None`.
    pub fn open(&self, data: &[u8]) -> Option<Packet> {
        let counter = u64::from_be_bytes(data.get(..COUNTER)?.try_into().ok()?);
        if !self.replay.lock().ok()?.fresh(counter) {
            return None;
        }
        let mut plain = vec![0u8; data.len()];
        let n = self.state.read_message(counter, &data[COUNTER..], &mut plain).ok()?;
        // Only a verified packet may move the replay window, or a forger could use it to lock real traffic out.
        self.replay.lock().ok()?.mark(counter);
        Packet::decode(&plain[..n])
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Reason, Vip};

    fn hello() -> Packet {
        Packet::Hello { serial: "AAAA-BBBB".into(), room: "lan".into(), verifier: [7; 32] }
    }

    fn connect() -> (Session, Session, Key) {
        let (private, public) = generate_keypair();
        let (msg1, pending) = client_start(&public, &hello()).unwrap();
        let (got, server) = server_accept(&private, &msg1).unwrap();
        assert_eq!(got, hello());
        let (msg2, server_session) = server.reply(&Packet::Welcome { vip: [10, 77, 0, 1] as Vip }).unwrap();
        let (reply, client_session) = pending.finish(&msg2).unwrap();
        assert_eq!(reply, Packet::Welcome { vip: [10, 77, 0, 1] });
        (client_session, server_session, public)
    }

    #[test]
    fn handshake_then_two_way_traffic() {
        let (c, s, _) = connect();
        let p = Packet::Data { vip: [10, 77, 0, 2], src_port: 8088, dst_port: 8088, payload: vec![1, 2, 3] };
        assert_eq!(s.open(&c.seal(&p)), Some(p.clone()));
        assert_eq!(c.open(&s.seal(&Packet::Ping)), Some(Packet::Ping));
        for i in 0..200u16 {
            let q = Packet::Bind { port: i };
            assert_eq!(s.open(&c.seal(&q)), Some(q));
        }
    }

    #[test]
    fn nothing_readable_on_the_wire() {
        let (private, public) = generate_keypair();
        let (msg1, _) = client_start(&public, &hello()).unwrap();
        let hay = |h: &[u8], n: &[u8]| h.windows(n.len()).any(|w| w == n);
        assert!(!hay(&msg1, b"AAAA-BBBB") && !hay(&msg1, b"lan") && !hay(&msg1, &[7; 32]), "serial, room and password proof are encrypted");
        let (c, s, _) = connect();
        let wire = c.seal(&Packet::Data { vip: [10, 77, 0, 2], src_port: 8088, dst_port: 8088, payload: b"SECRET-ORDERS".to_vec() });
        assert!(!hay(&wire, b"SECRET-ORDERS"));
        drop((private, s));
    }

    #[test]
    fn wrong_server_key_cannot_read_or_answer() {
        let (_, public) = generate_keypair();
        let (other_private, _) = generate_keypair();
        let (msg1, _) = client_start(&public, &hello()).unwrap();
        assert!(server_accept(&other_private, &msg1).is_err(), "an impostor cannot even read the hello");
    }

    #[test]
    fn forged_replayed_and_damaged_packets_are_refused() {
        let (c, s, _) = connect();
        let wire = c.seal(&Packet::Ping);
        assert!(s.open(&wire).is_some());
        assert!(s.open(&wire).is_none(), "replay");
        for i in 0..wire.len() {
            let mut bad = wire.clone();
            bad[i] ^= 1;
            assert!(s.open(&bad).is_none(), "byte {i}");
        }
        assert!(s.open(&[]).is_none() && s.open(&[0; 8]).is_none() && s.open(&[1; 40]).is_none());
        // a forgery must not poison the replay window: the next genuine packet still gets through
        let mut forged = c.seal(&Packet::Ping);
        forged[..8].copy_from_slice(&u64::MAX.to_be_bytes());
        assert!(s.open(&forged).is_none());
        assert!(s.open(&c.seal(&Packet::Ping)).is_some());
        // keys do not work across directions or sessions
        let (c2, _, _) = connect();
        assert!(s.open(&c2.seal(&Packet::Ping)).is_none());
        assert!(c.open(&c.seal(&Packet::Ping)).is_none(), "a client cannot open its own packets (separate direction keys)");
    }

    #[test]
    fn reordering_within_the_window_is_fine_and_old_packets_are_not() {
        let (c, s, _) = connect();
        let wires: Vec<Vec<u8>> = (0..100u16).map(|i| c.seal(&Packet::Bind { port: i })).collect();
        for i in [5usize, 3, 4, 2, 1, 0] {
            assert!(s.open(&wires[i]).is_some(), "out of order {i}");
        }
        assert!(s.open(&wires[3]).is_none(), "duplicate");
        assert!(s.open(&wires[90]).is_some());
        assert!(s.open(&wires[20]).is_none(), "more than 64 behind the newest");
        assert!(s.open(&wires[91]).is_some() && s.open(&wires[89]).is_some());
    }

    #[test]
    fn rejects_travel_the_same_way() {
        let (private, public) = generate_keypair();
        let (msg1, pending) = client_start(&public, &Packet::Register).unwrap();
        let (_, server) = server_accept(&private, &msg1).unwrap();
        let (msg2, _) = server.reply(&Packet::Reject(Reason::RateLimited)).unwrap();
        assert_eq!(pending.finish(&msg2).unwrap().0, Packet::Reject(Reason::RateLimited));
        assert!(pending_garbage(&public));
    }

    fn pending_garbage(public: &Key) -> bool {
        let (_, p) = client_start(public, &Packet::Register).unwrap();
        p.finish(&[0u8; 80]).is_err() && server_accept(&[1; 32], &[0u8; 5]).is_err()
    }

    #[test]
    fn fingerprints_and_hex() {
        let (_, public) = generate_keypair();
        assert_eq!(from_hex(&to_hex(&public)), Some(public));
        assert_eq!(from_hex("zz"), None);
        assert_eq!(from_hex(&"g".repeat(64)), None);
        let f = fingerprint(&public);
        assert_eq!((f.len(), f.matches('-').count()), (19, 3));
        assert_ne!(f, fingerprint(&[9; 32]));
    }
}
