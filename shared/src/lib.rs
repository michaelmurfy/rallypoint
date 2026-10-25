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
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Packet {
    /// Client -> server. First packet; carries the client's serial.
    Hello { serial: String },
    /// Server -> client: accepted, here is your virtual LAN address.
    Welcome { vip: Vip },
    Reject(Reason),
    /// LAN discovery broadcast. Client -> server: `vip` ignored. Server -> client: sender's vip.
    Bcast { vip: Vip, payload: Vec<u8> },
    /// Game traffic. Client -> server: `vip` is the destination. Server -> client: the source.
    Data { vip: Vip, payload: Vec<u8> },
    /// Keepalive, echoed by the server.
    Ping,
}

impl Packet {
    pub fn encode(&self) -> Vec<u8> {
        match self {
            Packet::Hello { serial } => [&[1, serial.len() as u8][..], serial.as_bytes()].concat(),
            Packet::Welcome { vip } => [&[2][..], vip].concat(),
            Packet::Reject(r) => vec![3, *r as u8],
            Packet::Bcast { vip, payload } => [&[4][..], vip, payload].concat(),
            Packet::Data { vip, payload } => [&[5][..], vip, payload].concat(),
            Packet::Ping => vec![6],
        }
    }

    pub fn decode(b: &[u8]) -> Option<Packet> {
        let (&tag, rest) = b.split_first()?;
        let vip = |r: &[u8]| -> Option<(Vip, Vec<u8>)> {
            let v: Vip = r.get(..4)?.try_into().ok()?;
            let p = r[4..].to_vec();
            (p.len() <= MAX_PAYLOAD).then_some((v, p))
        };
        Some(match tag {
            1 => {
                let (&n, s) = rest.split_first()?;
                let n = n as usize;
                if n > MAX_SERIAL || s.len() != n {
                    return None;
                }
                Packet::Hello { serial: String::from_utf8(s.to_vec()).ok()? }
            }
            2 => Packet::Welcome { vip: rest.try_into().ok()? },
            3 => Packet::Reject(match *rest.first()? {
                1 => Reason::UnknownSerial,
                2 => Reason::Revoked,
                3 => Reason::InUse,
                4 => Reason::Full,
                5 => Reason::NotAuthed,
                _ => return None,
            }),
            4 => vip(rest).map(|(vip, payload)| Packet::Bcast { vip, payload })?,
            5 => vip(rest).map(|(vip, payload)| Packet::Data { vip, payload })?,
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
            Packet::Bcast { vip: [0; 4], payload: vec![1, 2, 3] },
            Packet::Data { vip: [10, 77, 0, 9], payload: vec![] },
            Packet::Ping,
        ] {
            assert_eq!(Packet::decode(&p.encode()), Some(p));
        }
    }

    #[test]
    fn rejects_garbage() {
        assert_eq!(Packet::decode(&[]), None);
        assert_eq!(Packet::decode(&[99]), None);
        assert_eq!(Packet::decode(&[1, 5, b'a']), None); // length mismatch
        assert_eq!(Packet::decode(&[3, 42]), None);
        assert_eq!(Packet::decode(&[4, 1, 2]), None); // short vip
        assert_eq!(Packet::decode(&[&[5, 0, 0, 0, 0][..], &vec![0; MAX_PAYLOAD + 1]].concat()), None);
    }
}
