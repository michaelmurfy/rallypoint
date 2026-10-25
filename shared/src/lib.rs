//! Wire protocol between the launcher's tunnel agent and the relay server. One UDP datagram per packet.
//! Layout: tag byte, then per-variant fields. Hand-rolled because the payload is opaque game traffic.

pub const MAX_PAYLOAD: usize = 1400;
pub const MAX_SERIAL: usize = 64;

pub type Vip = [u8; 4];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Reason {
    UnknownSerial = 1,
    Revoked = 2,
    InUse = 3,
    Full = 4,
    NotAuthed = 5,
    /// Server only accepts admin-issued serials.
    RegistrationClosed = 6,
    /// Too many auto-registrations from this address.
    RateLimited = 7,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Packet {
    /// Client -> server. First packet; carries the client's serial.
    Hello { serial: String },
    /// Server -> client: accepted, here is your virtual LAN address.
    Welcome { vip: Vip },
    Reject(Reason),
    /// LAN discovery broadcast. Client -> server: `vip` ignored. Server -> client: sender's vip.
    /// Ports are the engine's logical UDP ports (8086 lobby, 8088 game), so one tunnel serves both sockets.
    Bcast { vip: Vip, src_port: u16, dst_port: u16, payload: Vec<u8> },
    /// Game traffic. Client -> server: `vip` is the destination. Server -> client: the source.
    Data { vip: Vip, src_port: u16, dst_port: u16, payload: Vec<u8> },
    /// Keepalive, echoed by the server.
    Ping,
    /// Client -> server: "I have no serial, issue me one" (when the server allows it).
    Register,
    /// Server -> client: the serial issued for a Register. The client must store it.
    Issued { serial: String },
    /// Engine -> local agent only: "I own this logical port, send its traffic to me".
    Bind { port: u16 },
}

fn serial(b: &[u8]) -> Option<String> {
    let (&n, s) = b.split_first()?;
    (n as usize <= MAX_SERIAL && s.len() == n as usize).then(|| String::from_utf8(s.to_vec()).ok()).flatten()
}

fn data(tag: u8, vip: &Vip, src: u16, dst: u16, payload: &[u8]) -> Vec<u8> {
    [&[tag][..], vip, &src.to_be_bytes(), &dst.to_be_bytes(), payload].concat()
}

impl Packet {
    pub fn encode(&self) -> Vec<u8> {
        match self {
            Packet::Hello { serial } => [&[1, serial.len() as u8][..], serial.as_bytes()].concat(),
            Packet::Welcome { vip } => [&[2][..], vip].concat(),
            Packet::Reject(r) => vec![3, *r as u8],
            Packet::Bcast { vip, src_port, dst_port, payload } => data(4, vip, *src_port, *dst_port, payload),
            Packet::Data { vip, src_port, dst_port, payload } => data(5, vip, *src_port, *dst_port, payload),
            Packet::Ping => vec![6],
            Packet::Bind { port } => [&[7][..], &port.to_be_bytes()].concat(),
            Packet::Register => vec![8],
            Packet::Issued { serial } => [&[9, serial.len() as u8][..], serial.as_bytes()].concat(),
        }
    }

    pub fn decode(b: &[u8]) -> Option<Packet> {
        let (&tag, rest) = b.split_first()?;
        let vip = |r: &[u8]| -> Option<(Vip, u16, u16, Vec<u8>)> {
            let v: Vip = r.get(..4)?.try_into().ok()?;
            let src = u16::from_be_bytes(r.get(4..6)?.try_into().ok()?);
            let dst = u16::from_be_bytes(r.get(6..8)?.try_into().ok()?);
            let p = r[8..].to_vec();
            (p.len() <= MAX_PAYLOAD).then_some((v, src, dst, p))
        };
        Some(match tag {
            1 => Packet::Hello { serial: serial(rest)? },
            9 => Packet::Issued { serial: serial(rest)? },
            8 if rest.is_empty() => Packet::Register,
            2 => Packet::Welcome { vip: rest.try_into().ok()? },
            3 => Packet::Reject(match *rest.first()? {
                1 => Reason::UnknownSerial,
                2 => Reason::Revoked,
                3 => Reason::InUse,
                4 => Reason::Full,
                5 => Reason::NotAuthed,
                6 => Reason::RegistrationClosed,
                7 => Reason::RateLimited,
                _ => return None,
            }),
            4 => vip(rest).map(|(vip, src_port, dst_port, payload)| Packet::Bcast { vip, src_port, dst_port, payload })?,
            5 => vip(rest).map(|(vip, src_port, dst_port, payload)| Packet::Data { vip, src_port, dst_port, payload })?,
            7 => Packet::Bind { port: u16::from_be_bytes(rest.try_into().ok()?) },
            6 if rest.is_empty() => Packet::Ping,
            _ => return None,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip() {
        for p in [
            Packet::Hello { serial: "ABCD-1234".into() },
            Packet::Welcome { vip: [10, 77, 0, 3] },
            Packet::Reject(Reason::InUse),
            Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: vec![1, 2, 3] },
            Packet::Data { vip: [10, 77, 0, 9], src_port: 8088, dst_port: 8088, payload: vec![] },
            Packet::Bind { port: 8088 },
            Packet::Register,
            Packet::Issued { serial: "AAAA-BBBB-CCCC-DDDD".into() },
            Packet::Ping,
        ] {
            assert_eq!(Packet::decode(&p.encode()), Some(p));
        }
    }

    /// Golden bytes; the engine's RelayWire.h test checks the same layout.
    #[test]
    fn wire_layout() {
        let p = Packet::Data { vip: [10, 77, 0, 1], src_port: 8088, dst_port: 8086, payload: vec![0xAA] };
        assert_eq!(p.encode(), [5, 10, 77, 0, 1, 0x1F, 0x98, 0x1F, 0x96, 0xAA]);
        assert_eq!(Packet::Bind { port: 8086 }.encode(), [7, 0x1F, 0x96]);
    }

    #[test]
    fn rejects_garbage() {
        assert_eq!(Packet::decode(&[]), None);
        assert_eq!(Packet::decode(&[99]), None);
        assert_eq!(Packet::decode(&[1, 5, b'a']), None); // length mismatch
        assert_eq!(Packet::decode(&[3, 42]), None);
        assert_eq!(Packet::decode(&[4, 1, 2]), None); // short header
        assert_eq!(Packet::decode(&[&[5, 0, 0, 0, 0, 0, 0, 0, 0][..], &vec![0; MAX_PAYLOAD + 1]].concat()), None);
    }
}
