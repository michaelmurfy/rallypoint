use super::*;
use crate::mapview::tests::tiny_map;

const VA: Vip = [10, 77, 0, 1];
const VB: Vip = [10, 77, 0, 2];
const VC: Vip = [10, 77, 0, 3];

fn scratch(tag: &str) -> PathBuf {
    static N: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let d = std::env::temp_dir().join(format!("rp-mapshare-{}-{tag}-{}", std::process::id(), N.fetch_add(1, std::sync::atomic::Ordering::Relaxed)));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

fn lib_in(root: &Path) -> DirLibrary {
    DirLibrary { maps_dir: root.join("Maps"), disabled_dir: root.join("lib").join("disabled"), ra2_dir: root.join("RA2Maps") }
}

/// Bytes that do not compress, so a transfer takes many chunks.
fn noise(n: usize, seed: u32) -> Vec<u8> {
    let mut x = seed | 1;
    (0..n)
        .map(|_| {
            x = x.wrapping_mul(1664525).wrapping_add(1013904223);
            ((x >> 24) as u8).max(1)
        })
        .collect()
}

/// A 2 x 2 uncompressed 24-bit TGA.
fn tga() -> Vec<u8> {
    let mut t = vec![0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0];
    t.extend([7u8; 12]);
    t
}

const RA2_MAP: &str = "[Basic]\nName=Dune Wars\n[Map]\nSize=0,0,50,50\nLocalSize=2,4,46,42\n[Waypoints]\n0=30040\n1=20010\n";

fn make_map(lib: &DirLibrary, name: &str, extra: &[(&str, Vec<u8>)]) {
    let d = lib.maps_dir.join(name);
    std::fs::create_dir_all(&d).unwrap();
    std::fs::write(d.join(format!("{name}.map")), tiny_map()).unwrap();
    std::fs::write(d.join(format!("{name}.tga")), tga()).unwrap();
    for (n, b) in extra {
        std::fs::write(d.join(n), b).unwrap();
    }
}

fn generals_bundle(name: &str, extra: &[(&str, Vec<u8>)]) -> Bundle {
    let mut files = vec![(format!("{name}.map"), tiny_map()), (format!("{name}.tga"), tga())];
    files.extend(extra.iter().map(|(n, b)| (n.to_string(), b.clone())));
    Bundle { game: MAP_GENERALS, name: name.to_string(), files }
}

/// Two launchers in one room and a relay that carries what they say, with the loss the test asks for.
struct Room {
    a: MapShare,
    b: MapShare,
    la: DirLibrary,
    lb: DirLibrary,
    now: u64,
    lose: Box<dyn FnMut(&MapMsg) -> bool>,
    /// Every message that went from a to b, in order.
    seen_by_b: Vec<MapMsg>,
}

impl Room {
    fn new(tag: &str) -> Room {
        let (ra, rb) = (scratch(&format!("{tag}-a")), scratch(&format!("{tag}-b")));
        let mut r = Room { a: MapShare::new(100), b: MapShare::new(200), la: lib_in(&ra), lb: lib_in(&rb), now: 1, lose: Box::new(|_| false), seen_by_b: vec![] };
        r.a.set_self(0, Some(VA), "Dana", MAP_GENERALS, false);
        r.b.set_self(0, Some(VB), "Sam", MAP_GENERALS, false);
        let caps = MapMsg::Caps { proto: generals_proto::MAP_PROTO, max_bytes: MAX_MAP_BYTES };
        r.a.receive(0, [0; 4], caps.clone(), &r.la);
        r.b.receive(0, [0; 4], caps, &r.lb);
        r.step(20); // they say hi
        r
    }
    /// Carry messages until none are left.
    fn settle(&mut self) {
        for _ in 0..1000 {
            let (qa, qb) = (self.a.drain(), self.b.drain());
            if qa.is_empty() && qb.is_empty() {
                return;
            }
            for (to, m) in qa {
                if (self.lose)(&m) {
                    continue;
                }
                if to == VB || to == [0; 4] {
                    self.seen_by_b.push(m.clone());
                    self.b.receive(self.now, VA, m, &self.lb);
                }
            }
            for (to, m) in qb {
                if (self.lose)(&m) {
                    continue;
                }
                if to == VA || to == [0; 4] {
                    self.a.receive(self.now, VB, m, &self.la);
                }
            }
        }
        panic!("messages never stopped");
    }
    fn step(&mut self, ms: u64) {
        self.now += ms;
        self.a.tick(self.now);
        self.b.tick(self.now);
        self.settle();
    }
    fn run(&mut self, ms: u64) {
        for _ in 0..ms / 20 {
            self.step(20);
        }
    }
    fn until(&mut self, what: &str, f: impl Fn(&Room) -> bool) {
        for _ in 0..30_000 {
            if f(self) {
                return;
            }
            self.step(20);
        }
        panic!("never: {what}\nout: {:?}\nin: {:?}", self.a.view(self.now).outgoing, self.b.view(self.now).incoming);
    }
    fn offer_generals(&mut self, name: &str, extra: &[(&str, Vec<u8>)]) -> Prepared {
        make_map(&self.la, name, extra);
        let p = prepare_generals(&self.la.maps_dir.join(name)).unwrap();
        self.a.send(self.now, &[VB], &p).unwrap();
        self.settle();
        p
    }
    fn out_state(&self) -> &'static str {
        self.a.view(self.now).outgoing.last().map_or("none", |o| o.state)
    }
    fn in_state(&self) -> &'static str {
        self.b.view(self.now).incoming.last().map_or("none", |i| i.state)
    }
    fn answer(&mut self, r: Reply) {
        let i = self.b.view(self.now).incoming.into_iter().find(|i| i.state == "prompt").expect("a prompt");
        self.b.respond(self.now, MapShare::parse_vip(&i.from).unwrap(), i.id, r);
        self.settle();
    }
}

// ---------------------------------------------------------------------------------------------- bundles

#[test]
fn a_bundle_packs_and_unpacks_and_its_hash_does_not_depend_on_who_packed_it() {
    let b = generals_bundle("Tournament Desert", &[("map.ini", b"Weather = Snowy".to_vec())]);
    assert_eq!(validate(&b), Ok(2));
    let back = unpack(&pack(&b).unwrap()).unwrap();
    assert_eq!(back, b);
    let mut shuffled = b.clone();
    shuffled.files.reverse();
    shuffled.files[0].0 = shuffled.files[0].0.to_uppercase();
    let upper = Bundle { name: b.name.to_uppercase(), ..shuffled.clone() };
    assert_eq!(content_hash(MAP_GENERALS, &b.name, &b.files), content_hash(MAP_GENERALS, &upper.name, &upper.files), "order and case of names do not matter");
    let mut other = b.clone();
    other.files[1].1[0] ^= 1;
    assert_ne!(content_hash(MAP_GENERALS, &b.name, &b.files), content_hash(MAP_GENERALS, &b.name, &other.files));
    assert_ne!(content_hash(MAP_GENERALS, "a", &b.files), content_hash(MAP_GENERALS, "b", &b.files), "the name is part of what a map is");
    // a Red Alert 2 map is one file
    let ra2 = Bundle { game: MAP_RA2, name: "dune.map".into(), files: vec![("dune.map".into(), RA2_MAP.as_bytes().to_vec())] };
    assert_eq!(validate(&ra2), Ok(2));
    assert_eq!(unpack(&pack(&ra2).unwrap()).unwrap(), ra2);
}

#[test]
fn file_names_that_could_leave_the_folder_or_hurt_windows_are_refused() {
    for bad in ["../evil.map", "a/b.map", "a\\b.map", "C:evil.map", "\\\\server\\share\\x.map", "..", ".", "", ".hidden.map", "x.map.", "x.map ", "nul\0.map", "con.map", "NUL.txt", "Com1.map", "lpt9.ini", "a:b.map", "a*.map", "q?.map", "a|b.map", "a\u{7}.map", "x\n.map"] {
        assert!(!flat_name_ok(bad), "{bad:?}");
    }
    assert!(!flat_name_ok(&"x".repeat(MAX_FILE_NAME + 1)));
    for good in ["Tournament Desert.map", "map.ini", "Ünïcode.str", "[RANK] Desert.map", "a (2).tga", "comet.map", "com10.map"] {
        assert!(flat_name_ok(good), "{good:?}");
    }
}

#[test]
fn a_hostile_bundle_is_refused_whatever_it_claims() {
    let ok = |name: &str| generals_bundle(name, &[]);
    let with = |extra: (&str, &[u8])| generals_bundle("Fine", &[(extra.0, extra.1.to_vec())]);
    for b in [
        with(("../escape.map", b"x")),
        with(("sub/dir.ini", b"x")),
        with(("C:\\Windows\\x.ini", b"x")),
        with(("CON.ini", b"x")),
        with(("run.exe", b"MZ")),
        with(("lib.dll", b"MZ")),
        with(("go.bat", b"x")),
        with(("go.sh", b"x")),
        with(("link.lnk", b"x")),
        with(("data.ini:stream", b"x")),
        with(("FINE.MAP", b"another one")), // the same name twice, in another case
        with(("bin.ini", b"text\0binary")),
        ok(".."),
        ok("a/b"),
        ok("a\\b"),
        ok(""),
        Bundle { files: vec![("Fine.tga".into(), tga())], ..ok("Fine") }, // no map file of its own name
        Bundle { files: vec![("Fine.map".into(), b"CkMp junk".to_vec())], ..ok("Fine") },
        Bundle { files: vec![("Fine.map".into(), tiny_map()), ("Fine.tga".into(), b"not a picture at all".to_vec())], ..ok("Fine") },
        Bundle { files: vec![], ..ok("Fine") },
        Bundle { game: 7, ..ok("Fine") },
        Bundle { files: (0..MAX_FILES + 1).map(|i| (format!("f{i}.ini"), vec![b'x'])).chain([("Fine.map".into(), tiny_map())]).collect(), ..ok("Fine") },
        Bundle { files: vec![("Fine.map".into(), tiny_map()), ("big.ini".into(), vec![b'x'; MAX_TEXT + 1])], ..ok("Fine") },
        Bundle { game: MAP_RA2, name: "x.map".into(), files: vec![("x.map".into(), RA2_MAP.as_bytes().to_vec()), ("y.map".into(), RA2_MAP.as_bytes().to_vec())] },
        Bundle { game: MAP_RA2, name: "x.map".into(), files: vec![("x.exe".into(), RA2_MAP.as_bytes().to_vec())] },
        Bundle { game: MAP_RA2, name: "x.map".into(), files: vec![("y.map".into(), RA2_MAP.as_bytes().to_vec())] },
        Bundle { game: MAP_RA2, name: "x.map".into(), files: vec![("x.map".into(), b"MZ\x90\0 not a map".to_vec())] },
    ] {
        assert!(validate(&b).is_err(), "{:?}", b.files.iter().map(|f| &f.0).collect::<Vec<_>>());
        // even a bundle that someone packed by hand, skipping `pack`'s own checks, is refused on arrival
        if let Ok(packed) = compress(&encode_raw(&b)) {
            if let Ok(got) = unpack(&packed) {
                assert!(validate(&got).is_err());
            }
        }
    }
}

#[test]
fn damaged_truncated_and_inflated_bundles_are_refused_without_allocating_what_they_claim() {
    let packed = pack(&generals_bundle("Fine", &[])).unwrap();
    assert!(unpack(&packed).is_ok());
    // truncated and flipped
    assert!(unpack(&packed[..packed.len() / 2]).is_err());
    assert!(unpack(&[]).is_err());
    assert!(unpack(b"not zlib at all").is_err());
    let mut flipped = packed.clone();
    let mid = flipped.len() / 2;
    flipped[mid] ^= 0xFF;
    assert!(unpack(&flipped).is_err() || unpack(&flipped).and_then(|b| validate(&b).map(|_| ())).is_err());
    // wrong magic, a table that claims more than there is, bytes left over, too many files
    let raw = encode_raw(&generals_bundle("Fine", &[]));
    let mut magic = raw.clone();
    magic[0] = b'X';
    assert!(unpack(&compress(&magic).unwrap()).is_err());
    assert!(unpack(&compress(&raw[..raw.len() - 5]).unwrap()).is_err(), "a file shorter than the table says");
    assert!(unpack(&compress(&[raw.clone(), vec![0]].concat()).unwrap()).is_err(), "bytes nobody asked for");
    let mut huge = MAGIC.to_vec();
    huge.extend([0, 1, b'x', 1, 1, b'a']);
    huge.extend(u32::MAX.to_be_bytes());
    assert!(unpack(&compress(&huge).unwrap()).is_err(), "a size from the header is compared with what is there, never allocated");
    let mut many = MAGIC.to_vec();
    many.extend([0, 1, b'x', (MAX_FILES + 1) as u8]);
    assert!(unpack(&compress(&many).unwrap()).is_err());
    // a decompression bomb: 100 MB of zeros packs to about 100 kB and is refused at the cap
    let bomb = compress(&vec![0u8; 100 << 20]).unwrap();
    assert!(bomb.len() < 200_000);
    let e = unpack(&bomb).unwrap_err();
    assert!(e.contains("far more"), "{e}");
    // and a packed file over the transfer cap is not even opened
    assert!(unpack(&vec![0u8; MAX_MAP_BYTES as usize + 1]).unwrap_err().contains("bigger"));
}

#[test]
fn a_map_too_big_to_send_says_so_before_anything_is_offered() {
    let b = generals_bundle("Huge", &[("a.str", noise(MAX_TEXT, 1)), ("b.ini", noise(MAX_TEXT, 2)), ("c.txt", noise(MAX_TEXT, 3))]);
    assert!(pack(&b).is_ok());
    let mut b = b;
    for i in 0..10 {
        b.files.push((format!("m{i}.ini"), noise(MAX_TEXT, 10 + i)));
    }
    let e = pack(&b).unwrap_err();
    assert!(e.contains("MB"), "{e}");
}

#[test]
fn display_text_from_other_players_is_cleaned_and_limited() {
    assert_eq!(display("  Sam\u{202E}evil\u{0}\n\t name ", 24), "Samevil name");
    assert_eq!(display(&"x".repeat(100), 24).chars().count(), 24);
    assert_eq!(display("\u{200B}\u{FEFF}", 24), "");
    assert_eq!(display("<b>x</b>", 24), "<b>x</b>", "markup stays text: the page only ever uses textContent");
}

// ---------------------------------------------------------------------------------------------- the library

#[test]
fn installing_goes_through_staging_and_never_overwrites_a_different_map() {
    let root = scratch("lib");
    let lib = lib_in(&root);
    let b = generals_bundle("Desert", &[("map.ini", b"one".to_vec())]);
    assert_eq!(lib.install(&b, false).unwrap(), Installed { name: "Desert".into(), renamed: false, replaced: false });
    assert!(lib.maps_dir.join("Desert").join("Desert.map").is_file());
    assert!(std::fs::read_dir(staging_root(&lib.maps_dir)).unwrap().flatten().all(|e| !e.file_name().to_string_lossy().starts_with("in-")), "no half-built map is left behind");
    assert!(lib.has(MAP_GENERALS, "desert", &content_hash(MAP_GENERALS, "Desert", &b.files)), "the same map, found by name in any case");
    // a different map of the same name: keep both
    let other = generals_bundle("Desert", &[("map.ini", b"two".to_vec())]);
    assert!(lib.exists(MAP_GENERALS, "Desert") && !lib.has(MAP_GENERALS, "Desert", &content_hash(MAP_GENERALS, "Desert", &other.files)));
    let got = lib.install(&other, false).unwrap();
    assert_eq!(got, Installed { name: "Desert (2)".into(), renamed: true, replaced: false });
    assert_eq!(std::fs::read(lib.maps_dir.join("Desert").join("map.ini")).unwrap(), b"one", "the first one was not touched");
    assert!(lib.maps_dir.join("Desert (2)").join("Desert (2).map").is_file(), "the files named after the folder follow it");
    assert!(lib.maps_dir.join("Desert (2)").join("Desert (2).tga").is_file());
    assert_eq!(lib.install(&other, false).unwrap().name, "Desert (3)");
    // replace: the old one goes to the backup folder, the new one takes its place
    let got = lib.install(&other, true).unwrap();
    assert_eq!(got, Installed { name: "Desert".into(), renamed: false, replaced: true });
    assert_eq!(std::fs::read(lib.maps_dir.join("Desert").join("map.ini")).unwrap(), b"two");
    let kept: Vec<_> = std::fs::read_dir(staging_root(&lib.maps_dir).join("replaced")).unwrap().flatten().collect();
    assert_eq!(kept.len(), 1);
    assert_eq!(std::fs::read(kept[0].path().join("map.ini")).unwrap(), b"one", "the old map is recoverable");
    // a map that is switched off counts as there, and is replaced where it is
    std::fs::create_dir_all(&lib.disabled_dir).unwrap();
    std::fs::rename(lib.maps_dir.join("Desert (2)"), lib.disabled_dir.join("Desert (2)")).unwrap();
    assert!(lib.exists(MAP_GENERALS, "Desert (2)"));
    assert!(lib.has(MAP_GENERALS, "Desert (2)", &content_hash(MAP_GENERALS, "Desert (2)", &read_folder(&lib.disabled_dir.join("Desert (2)")).unwrap())));
    // red alert 2
    let ra2 = Bundle { game: MAP_RA2, name: "dune.map".into(), files: vec![("dune.map".into(), RA2_MAP.as_bytes().to_vec())] };
    assert_eq!(lib.install(&ra2, false).unwrap().name, "dune.map");
    let ra2b = Bundle { files: vec![("dune.map".into(), format!("{RA2_MAP}; changed").into_bytes())], ..ra2.clone() };
    assert_eq!(lib.install(&ra2b, false).unwrap(), Installed { name: "dune (2).map".into(), renamed: true, replaced: false });
    assert_eq!(std::fs::read(lib.ra2_dir.join("dune.map")).unwrap(), RA2_MAP.as_bytes());
    assert!(lib.has(MAP_RA2, "DUNE.MAP", &content_hash(MAP_RA2, "dune.map", &ra2.files)));
    lib.install(&ra2b, true).unwrap();
    assert_eq!(std::fs::read(lib.ra2_dir.join("dune.map")).unwrap(), format!("{RA2_MAP}; changed").as_bytes());
}

#[test]
fn a_folder_with_links_or_other_files_sends_only_what_belongs_to_a_map() {
    let root = scratch("read");
    let lib = lib_in(&root);
    make_map(&lib, "Plain", &[("map.ini", b"x".to_vec()), ("notes.docx", b"no".to_vec()), ("run.exe", b"MZ".to_vec())]);
    std::fs::create_dir(lib.maps_dir.join("Plain").join("sub")).unwrap();
    std::fs::write(lib.maps_dir.join("Plain").join("sub").join("deep.ini"), b"x").unwrap();
    #[cfg(unix)]
    {
        std::fs::write(root.join("secret.ini"), b"top secret").unwrap();
        std::os::unix::fs::symlink(root.join("secret.ini"), lib.maps_dir.join("Plain").join("link.ini")).unwrap();
    }
    let names: Vec<String> = read_folder(&lib.maps_dir.join("Plain")).unwrap().into_iter().map(|f| f.0).collect();
    assert_eq!(names, ["map.ini", "Plain.map", "Plain.tga"]);
    // a file bigger than a map's own is not read at all
    std::fs::write(lib.maps_dir.join("Plain").join("big.str"), vec![b'x'; MAX_MAP_FILE + 1]).unwrap();
    assert!(prepare_generals(&lib.maps_dir.join("Plain")).unwrap_err().contains("too big"));
}

// ---------------------------------------------------------------------------------------------- presence

#[test]
fn players_see_each_other_and_only_those_with_the_same_game_can_be_sent_to() {
    let mut r = Room::new("peers");
    r.run(3000);
    let v = r.a.view(r.now);
    assert_eq!(v.supported, Some(true));
    assert_eq!(v.peers.len(), 1);
    assert_eq!((v.peers[0].name.as_str(), v.peers[0].vip.as_str(), v.peers[0].can_receive), ("Sam", "10.77.0.2", true));
    // Sam switches to Red Alert 2
    r.b.set_self(r.now, Some(VB), "Sam", MAP_RA2, false);
    r.run(6000);
    let v = r.a.view(r.now);
    assert_eq!((v.peers[0].game, v.peers[0].can_receive), ("ra2", false));
    make_map(&r.la, "Desert", &[]);
    let p = prepare_generals(&r.la.maps_dir.join("Desert")).unwrap();
    assert!(r.a.send(r.now, &[VB], &p).unwrap_err().contains("other game"));
    // someone who is gone for twenty seconds drops out
    r.b.set_self(r.now, None, "Sam", MAP_GENERALS, false);
    r.run(30_000);
    assert!(r.a.view(r.now).peers.is_empty());
    assert!(r.a.send(r.now, &[VB], &p).unwrap_err().contains("no longer in the room"));
}

#[test]
fn an_old_server_that_never_answers_is_reported_not_waited_for() {
    let mut a = MapShare::new(1);
    a.set_self(0, Some(VA), "Dana", MAP_GENERALS, false);
    assert_eq!(a.supported(1000), None, "still finding out");
    let root = scratch("old");
    let lib = lib_in(&root);
    make_map(&lib, "Desert", &[]);
    let p = prepare_generals(&lib.maps_dir.join("Desert")).unwrap();
    assert!(a.send(1000, &[VB], &p).unwrap_err().contains("Still checking"));
    assert_eq!(a.supported(10_000), Some(false));
    assert!(a.send(10_000, &[VB], &p).unwrap_err().contains("does not support sending maps"));
    // a fake answer from another player is not the server's
    a.receive(10_000, VB, MapMsg::Caps { proto: 1, max_bytes: 1 }, &lib);
    assert_eq!(a.supported(10_000), Some(false));
    // not in a room at all
    a.set_self(11_000, None, "Dana", MAP_GENERALS, false);
    assert_eq!(a.supported(20_000), None);
    assert!(!a.view(20_000).connected);
}

// ---------------------------------------------------------------------------------------------- transfers

#[test]
fn a_map_goes_from_one_player_to_another_and_lands_in_the_maps_folder() {
    let mut r = Room::new("happy");
    let p = r.offer_generals("Tournament Desert", &[("Tournament Desert.str", noise(60_000, 5)), ("map.ini", b"Weather = Snowy".to_vec())]);
    assert!(p.packed.len() > 50_000, "{}", p.packed.len());
    assert_eq!((r.out_state(), r.in_state()), ("waiting", "prompt"));
    let v = r.b.view(r.now);
    let i = &v.incoming[0];
    assert_eq!((i.from_name.as_str(), i.name.as_str(), i.game, i.players, i.conflict, i.master), ("Dana", "Tournament Desert", "generals", 2, false, false));
    assert_eq!(i.size as usize, p.packed.len());
    assert!(!r.lb.maps_dir.join("Tournament Desert").exists(), "nothing is written before the player says yes");
    r.run(2000);
    assert_eq!(r.b.view(r.now).incoming[0].percent, 0, "and nothing is sent");
    r.answer(Reply::Accept);
    r.until("delivered", |r| r.out_state() == "done");
    assert_eq!(r.in_state(), "done");
    let v = r.b.view(r.now);
    assert_eq!(v.incoming[0].installed.as_deref(), Some("Tournament Desert"));
    assert!(v.incoming[0].note.contains("Saved as Tournament Desert"));
    let dir = r.lb.maps_dir.join("Tournament Desert");
    assert_eq!(std::fs::read(dir.join("Tournament Desert.map")).unwrap(), tiny_map());
    assert_eq!(std::fs::read(dir.join("map.ini")).unwrap(), b"Weather = Snowy");
    assert!(std::fs::read_dir(staging_root(&r.lb.maps_dir)).map(|d| d.flatten().all(|e| e.file_name() == "replaced")).unwrap_or(true), "no half-built maps left behind");
    // and now Sam has it: a second offer of the same map is not even shown
    r.a.dismiss_out(VB, r.a.view(r.now).outgoing[0].id);
    r.b.dismiss_in(VA, r.b.view(r.now).incoming[0].id);
    r.a.send(r.now, &[VB], &p).unwrap();
    r.settle();
    assert_eq!(r.out_state(), "have", "the sender is told");
    assert_eq!(r.in_state(), "have", "the receiver is told, without a prompt");
}

#[test]
fn a_transfer_survives_lost_datagrams_in_both_directions() {
    let mut r = Room::new("loss");
    let mut n = 0u32;
    r.lose = Box::new(move |m| {
        n += 1;
        matches!(m, MapMsg::Data { .. } | MapMsg::Ack { .. }) && n % 4 == 0
    });
    let p = r.offer_generals("Lossy", &[("Lossy.str", noise(300_000, 9))]);
    assert!(map_chunks(p.packed.len() as u32) > 300);
    r.answer(Reply::Accept);
    r.until("delivered over a lossy link", |r| r.out_state() == "done");
    assert_eq!(r.in_state(), "done");
    assert_eq!(std::fs::read(r.lb.maps_dir.join("Lossy").join("Lossy.str")).unwrap(), noise(300_000, 9));
}

#[test]
fn a_lost_offer_is_repeated_and_a_repeated_offer_is_not_a_second_prompt() {
    let mut r = Room::new("offer-loss");
    let mut first = true;
    r.lose = Box::new(move |m| matches!(m, MapMsg::Offer { .. }) && std::mem::replace(&mut first, false));
    r.offer_generals("Retry", &[]);
    assert_eq!(r.in_state(), "none");
    r.run(5000);
    assert_eq!(r.in_state(), "prompt");
    r.run(15_000); // the sender keeps repeating while waiting
    assert_eq!(r.b.view(r.now).incoming.len(), 1);
    // a repeat after the answer gets the answer again, never a new prompt
    r.answer(Reply::Decline);
    assert_eq!(r.out_state(), "declined");
    r.a.receive(r.now, VB, MapMsg::Cancel { id: 1 }, &r.la); // noise for an id nobody has: ignored
    r.b.receive(r.now, VA, r.a.out[0].offer(), &r.lb);
    let again = r.b.drain();
    assert_eq!(again, vec![(VA, MapMsg::End { id: r.a.out[0].id, code: map_end::DECLINED })]);
    assert_eq!(r.b.view(r.now).incoming.len(), 1);
}

#[test]
fn declining_blocking_busy_and_the_other_game_say_so_to_the_sender() {
    let mut r = Room::new("answers");
    r.offer_generals("One", &[]);
    r.answer(Reply::Decline);
    assert_eq!((r.out_state(), r.in_state()), ("declined", "failed"));
    assert!(!r.lb.maps_dir.join("One").exists());
    // a second offer while the first is still waiting: busy (one at a time from a sender)
    let mut r = Room::new("busy");
    r.offer_generals("One", &[]);
    make_map(&r.la, "Two", &[]);
    let p2 = prepare_generals(&r.la.maps_dir.join("Two")).unwrap();
    r.a.dismiss_out(VB, 0); // nothing to dismiss: it is still open, the list keeps it
    r.a.out[0].state = OutState::Over(map_end::CANCELLED, "x".into()); // pretend the first was dropped on the sender's side only
    r.a.send(r.now, &[VB], &p2).unwrap();
    r.settle();
    assert_eq!(r.a.view(r.now).outgoing[1].note, "Busy with another map. Try again in a moment");
    assert_eq!(r.b.view(r.now).incoming.iter().filter(|i| i.state == "prompt").count(), 1, "only one prompt from a sender");
    // blocking: the pending one is declined and later ones are not even shown, until the room is left
    r.answer(Reply::Block);
    r.a.send(r.now, &[VB], &p2).unwrap();
    r.settle();
    assert_eq!(r.b.view(r.now).incoming.iter().filter(|i| i.state == "prompt").count(), 0);
    assert_eq!(r.a.view(r.now).outgoing.last().unwrap().state, "declined", "a blocked sender just sees 'declined'");
    r.b.set_self(r.now, None, "Sam", MAP_GENERALS, false);
    r.b.set_self(r.now, Some(VB), "Sam", MAP_GENERALS, false);
    assert!(r.b.blocked.is_empty(), "blocks last as long as the room");
    // another game on the other side
    let mut r = Room::new("othergame");
    r.b.set_self(r.now, Some(VB), "Sam", MAP_RA2, false);
    make_map(&r.la, "Desert", &[]);
    let p = prepare_generals(&r.la.maps_dir.join("Desert")).unwrap();
    r.a.out.push(Outgoing { id: 9, to: VB, name: "Desert".into(), game: MAP_GENERALS, players: 2, hash: p.hash, data: p.packed.clone(), state: OutState::Waiting, next_ack: 0, next_send: 0, started: r.now, last_offer: r.now, last_ack: r.now, last_resend: r.now, over_at: 0 });
    let offer = r.a.out[0].offer();
    r.a.queue.push((VB, offer));
    r.settle();
    assert_eq!(r.out_state(), "failed");
    assert_eq!(r.a.view(r.now).outgoing[0].note, "Has the other game selected");
    assert_eq!(r.b.view(r.now).incoming.len(), 0, "no prompt for a map of the other game");
}

#[test]
fn a_player_can_have_at_most_four_prompts_at_once() {
    let root = scratch("four");
    let lib = lib_in(&root);
    let mut b = MapShare::new(1);
    b.set_self(0, Some(VB), "Sam", MAP_GENERALS, false);
    let offer = |id: u32, name: &str| MapMsg::Offer { id, game: MAP_GENERALS, master: false, size: 1000, players: 2, hash: [id as u8; 32], name: name.into() };
    for (i, from) in [[10, 77, 0, 1], [10, 77, 0, 3], [10, 77, 0, 4], [10, 77, 0, 5], [10, 77, 0, 6]].into_iter().enumerate() {
        b.receive(10, from, offer(i as u32 + 1, "M"), &lib);
    }
    assert_eq!(b.view(10).incoming.len(), 4);
    assert_eq!(b.drain(), vec![([10, 77, 0, 6], MapMsg::End { id: 5, code: map_end::BUSY })]);
    // nonsense is refused, not shown
    b.receive(10, VC, offer(40, "   "), &lib);
    b.receive(10, VC, MapMsg::Offer { id: 41, game: MAP_GENERALS, master: false, size: MAX_MAP_BYTES + 1, players: 0, hash: [0; 32], name: "Big".into() }, &lib);
    assert_eq!(b.view(10).incoming.len(), 4);
    assert_eq!(b.drain().iter().filter(|(_, m)| matches!(m, MapMsg::End { code, .. } if *code == map_end::REFUSED)).count(), 2);
    // unanswered prompts end by themselves
    b.tick(10 + PROMPT_WAIT + 1);
    assert!(b.view(10 + PROMPT_WAIT + 1).incoming.iter().all(|i| i.state == "failed"));
    assert_eq!(b.drain().iter().filter(|(_, m)| matches!(m, MapMsg::End { code, .. } if *code == map_end::DECLINED)).count(), 4);
}

#[test]
fn a_map_with_the_name_of_one_you_have_asks_first_and_keeps_both_or_replaces() {
    // keep both
    let mut r = Room::new("conflict");
    make_map(&r.lb, "Desert", &[("map.ini", b"mine".to_vec())]);
    r.offer_generals("Desert", &[("map.ini", b"theirs".to_vec())]);
    let i = r.b.view(r.now).incoming.remove(0);
    assert_eq!((i.state, i.conflict), ("prompt", true), "the page is told to ask which");
    r.answer(Reply::Accept);
    r.until("delivered", |r| r.out_state() == "done");
    let v = r.b.view(r.now);
    assert_eq!(v.incoming[0].installed.as_deref(), Some("Desert (2)"));
    assert!(v.incoming[0].note.contains("different map with that name"));
    assert_eq!(std::fs::read(r.lb.maps_dir.join("Desert").join("map.ini")).unwrap(), b"mine");
    assert_eq!(std::fs::read(r.lb.maps_dir.join("Desert (2)").join("map.ini")).unwrap(), b"theirs");
    // replace
    let mut r = Room::new("replace");
    make_map(&r.lb, "Desert", &[("map.ini", b"mine".to_vec())]);
    r.offer_generals("Desert", &[("map.ini", b"theirs".to_vec())]);
    r.answer(Reply::Replace);
    r.until("delivered", |r| r.out_state() == "done");
    assert_eq!(std::fs::read(r.lb.maps_dir.join("Desert").join("map.ini")).unwrap(), b"theirs");
    assert!(!r.lb.maps_dir.join("Desert (2)").exists());
    let kept = std::fs::read_dir(staging_root(&r.lb.maps_dir).join("replaced")).unwrap().flatten().next().unwrap().path();
    assert_eq!(std::fs::read(kept.join("map.ini")).unwrap(), b"mine");
    assert!(r.b.view(r.now).incoming[0].note.contains("backup"));
    // replace means nothing when there is nothing to replace
    let mut r = Room::new("noreplace");
    r.offer_generals("Fresh", &[]);
    r.answer(Reply::Replace);
    r.until("delivered", |r| r.out_state() == "done");
    assert_eq!(r.b.view(r.now).incoming[0].installed.as_deref(), Some("Fresh"));
    // exactly the same map is "have", whatever its files' order or the case of its folder
    let mut r = Room::new("same");
    make_map(&r.lb, "DESERT", &[("map.ini", b"same".to_vec())]);
    r.offer_generals("Desert", &[("map.ini", b"same".to_vec())]);
    assert_eq!((r.out_state(), r.in_state()), ("have", "have"));
    assert!(r.b.view(r.now).incoming[0].note.contains("already have"));
    // a switched-off copy counts too
    let mut r = Room::new("off");
    make_map(&r.lb, "Desert", &[("map.ini", b"same".to_vec())]);
    std::fs::create_dir_all(&r.lb.disabled_dir).unwrap();
    std::fs::rename(r.lb.maps_dir.join("Desert"), r.lb.disabled_dir.join("Desert")).unwrap();
    r.offer_generals("Desert", &[("map.ini", b"same".to_vec())]);
    assert_eq!(r.in_state(), "have");
}

#[test]
fn the_rooms_master_can_be_trusted_to_send_without_asking_only_if_the_player_said_so_and_never_over_a_map_they_have() {
    let offer = |master: bool| MapMsg::Offer { id: 1, game: MAP_GENERALS, master, size: 1000, players: 2, hash: [1; 32], name: "Desert".into() };
    let root = scratch("auto");
    let lib = lib_in(&root);
    for (auto, master, expect) in [(false, true, "prompt"), (true, false, "prompt"), (true, true, "receiving"), (false, false, "prompt")] {
        let mut b = MapShare::new(1);
        b.set_self(0, Some(VB), "Sam", MAP_GENERALS, auto);
        b.receive(5, VA, offer(master), &lib);
        assert_eq!(b.view(5).incoming[0].state, expect, "auto {auto}, master {master}");
        assert_eq!(b.drain().iter().any(|(_, m)| matches!(m, MapMsg::Accept { .. })), expect == "receiving");
    }
    // a name they already have is always asked about
    make_map(&lib, "Desert", &[("map.ini", b"mine".to_vec())]);
    let mut b = MapShare::new(1);
    b.set_self(0, Some(VB), "Sam", MAP_GENERALS, true);
    b.receive(5, VA, offer(true), &lib);
    let i = &b.view(5).incoming[0];
    assert_eq!((i.state, i.conflict), ("prompt", true));
    assert!(b.drain().is_empty());
}

#[test]
fn a_map_that_is_not_what_was_offered_is_thrown_away() {
    // the hash differs from the one in the offer
    let mut r = Room::new("hash");
    let p = r.offer_generals("Tricky", &[]);
    for o in r.a.out.iter_mut() {
        o.hash = [9; 32]; // from now on the offer says another map
    }
    r.b.inc[0].hash = [9; 32];
    r.answer(Reply::Accept);
    r.until("ended", |r| r.out_state() != "sending" && r.out_state() != "waiting");
    assert_eq!(r.out_state(), "failed");
    assert_eq!(r.a.view(r.now).outgoing[0].note, "Could not use the map (it failed their checks)");
    assert!(r.b.view(r.now).incoming[0].note.contains("not the map that was offered"));
    assert!(!r.lb.maps_dir.join("Tricky").exists(), "nothing was installed");
    let _ = p;
    // the offer promises one name and the bundle holds another
    let mut r = Room::new("name");
    let other = Bundle { name: "Other".into(), files: generals_bundle("Other", &[]).files, game: MAP_GENERALS };
    let packed = Arc::new(pack(&other).unwrap());
    let p = Prepared { game: MAP_GENERALS, name: "Promised".into(), players: 2, hash: content_hash(MAP_GENERALS, "Other", &other.files), packed };
    r.a.send(r.now, &[VB], &p).unwrap();
    r.settle();
    r.answer(Reply::Accept);
    r.until("ended", |r| r.out_state() == "failed");
    assert!(!r.lb.maps_dir.join("Other").exists() && !r.lb.maps_dir.join("Promised").exists());
    // a bundle whose map is rubbish, with a hash that matches itself (so the sender is the one lying)
    for (tag, files) in [("garbage", vec![("Bad.map".to_string(), b"CkMp not a real map".to_vec())]), ("traversal", vec![("Bad.map".to_string(), tiny_map()), ("../../escape.ini".to_string(), b"x".to_vec())]), ("exe", vec![("Bad.map".to_string(), tiny_map()), ("evil.exe".to_string(), b"MZ".to_vec())])] {
        let mut r = Room::new(tag);
        let b = Bundle { game: MAP_GENERALS, name: "Bad".into(), files };
        let packed = Arc::new(compress(&encode_raw(&b)).unwrap()); // `pack` would refuse; a hostile sender does not use it
        let p = Prepared { game: MAP_GENERALS, name: "Bad".into(), players: 2, hash: content_hash(MAP_GENERALS, "Bad", &b.files), packed };
        r.a.send(r.now, &[VB], &p).unwrap();
        r.settle();
        r.answer(Reply::Accept);
        r.until("ended", |r| r.out_state() == "failed");
        assert!(!r.lb.maps_dir.join("Bad").exists(), "{tag}: nothing was installed");
        assert!(!r.lb.maps_dir.parent().unwrap().join("escape.ini").exists());
        assert!(!r.lb.maps_dir.parent().unwrap().parent().unwrap().join("escape.ini").exists());
    }
}

#[test]
fn chunks_that_do_not_fit_the_offer_or_the_order_are_ignored() {
    let root = scratch("forged");
    let lib = lib_in(&root);
    let mut b = MapShare::new(1);
    b.set_self(0, Some(VB), "Sam", MAP_GENERALS, false);
    let size = 2000u32;
    b.receive(1, VA, MapMsg::Offer { id: 4, game: MAP_GENERALS, master: false, size, players: 2, hash: [1; 32], name: "Desert".into() }, &lib);
    let chunk = |id: u32, seq: u32, n: usize| MapMsg::Data { id, seq, bytes: vec![1; n] };
    b.receive(2, VA, chunk(4, 0, 900), &lib); // before accepting
    assert_eq!(b.inc[0].next, 0);
    b.respond(3, VA, 4, Reply::Accept);
    b.drain();
    b.receive(4, VC, chunk(4, 0, 900), &lib); // another player
    b.receive(4, VA, chunk(5, 0, 900), &lib); // another transfer
    b.receive(4, VA, chunk(4, 1, 900), &lib); // out of order
    b.receive(4, VA, chunk(4, 0, 899), &lib); // short
    b.receive(4, VA, chunk(4, 0, 901), &lib); // can't even be built: too long for a chunk of this size
    b.receive(4, VA, chunk(4, 7, 900), &lib); // past the end
    assert_eq!(b.inc[0].next, 0, "nothing of that was taken");
    b.receive(300, VA, chunk(4, 0, 900), &lib);
    b.receive(300, VA, chunk(4, 0, 900), &lib); // a repeat is not taken twice
    assert_eq!(b.inc[0].next, 1);
    assert_eq!(b.inc[0].buf.len(), 900);
    // silence ends it, and the buffer is freed
    b.tick(300 + RECV_IDLE + 1);
    assert_eq!(b.view(0).incoming[0].state, "failed");
    assert!(b.inc[0].buf.is_empty());
    assert!(b.drain().iter().any(|(_, m)| matches!(m, MapMsg::End { code, .. } if *code == map_end::CANCELLED)));
}

#[test]
fn nothing_replays_after_a_transfer_is_over() {
    let mut r = Room::new("replay");
    r.offer_generals("Once", &[]);
    r.answer(Reply::Accept);
    r.until("delivered", |r| r.out_state() == "done");
    let before = std::fs::read_dir(&r.lb.maps_dir).unwrap().count();
    // everything the sender ever sent, played again
    for m in r.seen_by_b.clone() {
        r.b.receive(r.now, VA, m, &r.lb);
    }
    r.settle();
    assert_eq!(std::fs::read_dir(&r.lb.maps_dir).unwrap().count(), before, "no second copy, no overwrite");
    assert_eq!(r.b.view(r.now).incoming.len(), 1);
    assert_eq!(r.in_state(), "done");
}

#[test]
fn the_sender_can_cancel_and_gives_up_on_silence_and_dropped_links() {
    let mut r = Room::new("cancel");
    r.offer_generals("Desert", &[("a.str", noise(100_000, 3))]);
    r.answer(Reply::Accept);
    r.run(100);
    let id = r.a.view(r.now).outgoing[0].id;
    r.a.cancel_out(r.now, VB, id);
    r.settle();
    assert_eq!((r.out_state(), r.in_state()), ("cancelled", "failed"));
    assert!(r.b.view(r.now).incoming[0].note.contains("sender cancelled"));
    assert!(!r.lb.maps_dir.join("Desert").exists());
    // nobody answers the offer
    let mut r = Room::new("nobody");
    r.lose = Box::new(|_| true);
    make_map(&r.la, "Desert", &[]);
    let p = prepare_generals(&r.la.maps_dir.join("Desert")).unwrap();
    r.a.send(r.now, &[VB], &p).unwrap();
    r.run(OFFER_WAIT + 1000);
    assert_eq!(r.a.view(r.now).outgoing[0].note, "No answer");
    // the link dies mid-way
    let mut r = Room::new("dead");
    r.offer_generals("Desert", &[("a.str", noise(100_000, 3))]);
    r.answer(Reply::Accept);
    r.run(200);
    r.lose = Box::new(|m| matches!(m, MapMsg::Ack { .. } | MapMsg::End { .. }));
    r.run(STALL + 2000);
    assert_eq!(r.out_state(), "failed");
    assert_eq!(r.a.view(r.now).outgoing[0].note, "The connection to them dropped");
    // the sender may have four open transfers, one per player
    let mut a = MapShare::new(1);
    a.set_self(0, Some(VA), "Dana", MAP_GENERALS, false);
    a.receive(0, [0; 4], MapMsg::Caps { proto: 1, max_bytes: 1 }, &r.la);
    let peers: Vec<Vip> = (2..8).map(|n| [10, 77, 0, n]).collect();
    for v in &peers {
        a.receive(0, *v, MapMsg::Hi { game: MAP_GENERALS, master: false, name: format!("P{}", v[3]) }, &r.la);
    }
    assert_eq!(a.send(1, &peers, &p).unwrap(), 4);
    assert!(a.send(1, &peers[4..], &p).unwrap_err().contains("Four maps"));
    assert!(a.send(1, &peers[..1], &p).unwrap_err().contains("already sending"));
}

#[test]
fn a_red_alert_2_map_travels_the_same_way() {
    let mut r = Room::new("ra2");
    r.a.set_self(r.now, Some(VA), "Dana", MAP_RA2, false);
    r.b.set_self(r.now, Some(VB), "Sam", MAP_RA2, false);
    r.run(6000);
    std::fs::create_dir_all(&r.la.ra2_dir).unwrap();
    std::fs::write(r.la.ra2_dir.join("dune.map"), RA2_MAP).unwrap();
    let p = prepare_ra2(&r.la.ra2_dir.join("dune.map")).unwrap();
    assert_eq!((p.game, p.players), (MAP_RA2, 2));
    r.a.send(r.now, &[VB], &p).unwrap();
    r.settle();
    assert_eq!(r.b.view(r.now).incoming[0].game, "ra2");
    r.answer(Reply::Accept);
    r.until("delivered", |r| r.out_state() == "done");
    assert_eq!(std::fs::read(r.lb.ra2_dir.join("dune.map")).unwrap(), RA2_MAP.as_bytes());
    // a Generals-only launcher refuses the same offer
    let mut g = MapShare::new(1);
    g.set_self(0, Some(VC), "Kim", MAP_GENERALS, false);
    g.receive(1, VA, MapMsg::Offer { id: 3, game: MAP_RA2, master: false, size: 100, players: 2, hash: [0; 32], name: "dune.map".into() }, &r.lb);
    assert_eq!(g.drain(), vec![(VA, MapMsg::End { id: 3, code: map_end::OTHER_GAME })]);
}

#[test]
fn a_map_cannot_be_sent_to_yourself_or_by_a_stranger_who_is_not_in_a_room() {
    let root = scratch("self");
    let lib = lib_in(&root);
    let mut a = MapShare::new(1);
    // not in a room: everything is ignored
    a.receive(1, VB, MapMsg::Offer { id: 1, game: MAP_GENERALS, master: false, size: 100, players: 2, hash: [0; 32], name: "X".into() }, &lib);
    assert!(a.view(1).incoming.is_empty() && a.drain().is_empty());
    a.set_self(0, Some(VA), "Dana", MAP_GENERALS, false);
    // an offer that claims to come from ourselves (a reflection) is ignored
    a.receive(1, VA, MapMsg::Offer { id: 1, game: MAP_GENERALS, master: false, size: 100, players: 2, hash: [0; 32], name: "X".into() }, &lib);
    assert!(a.view(1).incoming.is_empty());
    // the page cannot name an address that is not an address
    assert_eq!(MapShare::parse_vip("10.77.0.2"), Some(VB));
    for bad in ["", "10.77.0", "10.77.0.2.1", "10.77.0.256", "a.b.c.d", "10.77.0.-1"] {
        assert_eq!(MapShare::parse_vip(bad), None, "{bad:?}");
    }
}
