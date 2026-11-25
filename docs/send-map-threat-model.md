# Sending maps to a room: threat model

A player can send a map they have to the other players of their relay room, for Generals / Zero Hour (a map folder) and Red Alert 2 (one map file). Code: `shared` (`MapMsg`, packet tag 28), `server` (`map_msg`), `launcher/core/src/mapshare.rs`, `launcher/ui/mapshare.js`.

## What travels

One transfer is: `Offer` (sender to one player) → `Accept` (that player) → `Data` chunks answered by cumulative `Ack`s → `End` (the receiver says how it went). The sender can `Cancel`; the receiver can end it with `End` at any time. Presence (`Hi`) goes to the room every few seconds and tells each launcher who can receive and which game they have; the relay answers the first `Hi` with `Caps`, which is how a launcher knows the server shares maps.

The payload is a bundle: zlib of `RPM1`, game, name, a table of (file name, size), then the files. Generals bundles hold the map folder's `.map`, `.tga`, `.ini`, `.str`, `.txt`; Red Alert 2 bundles hold one `.map`, `.mpr`, `.yrm` or `.yro`.

## Who is trusted

- **The relay** is trusted to enforce room membership and quotas, and to be honest about who is the room master. It is not trusted with content: it stores nothing, and never sees a map it could not already see (the session is encrypted to it, not end to end).
- **Everyone else in the room is hostile.** A room is a password, so anyone who learns it is in. Every byte from another player is checked as if an attacker sent it.
- Nothing in a message is believed about its sender. The relay replaces `vip` with the address of the session the packet came in on, and fills in `master` itself.

## Threats and what stops them

| Threat | Defence |
|---|---|
| A map pushed at someone who did not agree | The relay forwards `Data` only on a transfer whose receiver sent `Accept`. The receiver's launcher writes nothing before the player clicks Accept (or the player turned on auto-accept for the room master, and the map does not take the name of one they have). |
| Maps reaching another room | The relay finds the receiver by address **inside the sender's room only**. Rooms reuse the same `10.77.0.x` addresses, so a transfer is keyed by room as well. Isolation is tested on the relay and with real agents. |
| Spoofing the sender | The sender is the relay's session, not a field in the payload. A transfer is also bound to both players' serials: if an address changes hands, the transfer ends. |
| A third player feeding, ending or cancelling a transfer | The transfer key includes both addresses. `Accept`, `Ack` and `End` only work from the receiver, `Data` and `Cancel` only from the sender. |
| Flooding the relay | Per session per second: 20 control messages and 450 data messages, on top of the existing 600 packets and 400 kB. Presence is passed on at most every 2 s. At most 4 open transfers out of one player, 4 into one player, and 1 per pair. Map bytes offered: 64 MiB per player per hour, 192 MiB per room per hour. A server-wide bucket of 4 MiB/s (a data chunk that finds it empty is dropped; the sender repeats it). Transfers die after 30 s of silence, 180 s unanswered, 15 min in all. |
| Oversize | An `Offer` over 8 MiB, or empty, does not decode. A `Data` chunk must be exactly the length the offer's size implies for its place, `seq` must be inside the transfer, and a transfer may relay at most twice its size plus 64 KiB (repeats after loss). |
| Replay and old traffic | Each datagram is inside the Noise session (64-packet replay window). Messages for a closed or unknown transfer are dropped. A launcher that already answered an offer answers a repeat with the same answer and never shows a second prompt. |
| Relay as storage | None: chunks are forwarded as they arrive, nothing is kept after a transfer ends, nothing is buffered beyond one datagram. Back-pressure is the sender's window (24 chunks) and the receiver's acks. |
| Prompt spam | The receiver allows one open offer per sender and four in all; others are answered "busy". "Block this player's maps" ignores a player for as long as the launcher stays in the room. |
| Fake content | The offer carries a SHA-256 of the map's content (names and bytes, in name order, any case). The receiver recomputes it after unpacking and refuses a mismatch, a different game, or a different name than offered. |
| Path tricks in file names | Names are flat: no `/`, `\`, `:`, wildcards, control characters or NUL, no leading dot or space, no trailing dot or space, not `.`/`..`, none of the Windows device names (`CON`, `NUL.txt`, `COM1`...), at most 100 bytes, no duplicates in any case. Extensions are whitelisted per game. Names are checked before anything is written, files are created with `create_new`, and links in a sender's folder are never read. |
| Decompression bombs | The inflater stops at 32 MiB. Sizes in the bundle header are compared with what is left, never used to allocate. At most 16 files, 16 MiB for a map file, 4 MiB for a picture, 1 MiB for text. The packed file is never read past 8 MiB. |
| Malformed maps | The map must parse with the same readers the launcher uses to show it (`mapview::parse_map`, `ra2maps::parse`), pictures must decode, text files must have no NUL. |
| Overwriting or half-installed maps | The map is written in `.rallypoint-incoming/` next to the maps folder (same disk, never inside a folder the game reads) and moved into place with one rename. A map whose name is taken by a **different** map is never overwritten without the player's say: the page asks (keep both, saved as `Name (2)`, or replace). Replacing moves the old map to `.rallypoint-incoming/replaced/`, and puts it back if the new one cannot take its place. The same map (same content hash, even if switched off) is not asked about: the sender is told "already has it". |
| Executing anything | Nothing is run. Only the file kinds above are ever written. |
| Hostile text in the page | A player's name and a map's name are cleaned (control and direction-changing characters out, one line, length-limited) and only ever put into the page as text. |
| Old servers and launchers | An old relay ignores tag 28, so no `Caps` comes back and the launcher says "this server does not support sending maps yet". An old launcher never sends presence, so it is not listed as someone to send to. Neither breaks. |

## What is not protected

- **The relay operator and anyone who can read the relay's memory** see maps in transit, as they see everything else in the room. There is no end-to-end encryption between players.
- **A hostile room member can still send junk a player has to decline**, once at a time, until blocked. A block lasts as long as the launcher stays in the room: it is by address, which the relay hands out per room, so someone who rejoins gets a fresh start. It is not a persistent ban list.
- **A hostile room master** can use auto-accept (if a player turned it on) to put maps into the maps folder without a click. They are still checked as above and cannot replace a map the player has. The default is off.
- **Maps are game content that a game may read**: `.ini`, `.str` and `.map` files are parsed by the engine, and a malicious map could trigger a bug in that parser. The launcher's own checks reduce this; they cannot rule it out.
- The replaced-map backups are not cleaned up automatically.
- Block and preferences are per launcher; a relay that is not updated simply does not carry maps.

## Deployment

The relay must be redeployed (the release workflow's image) before the feature works. Until then launchers show that the server does not support sending maps.
