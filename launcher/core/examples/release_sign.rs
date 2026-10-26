//! Release signing for the updater (see `generals_core::update`).
//!
//!   cargo run -p generals-core --example release_sign -- keygen
//!       prints a new key pair as hex: keep the secret in a password manager and in the GitHub Actions secret
//!       RELEASE_SIGNING_KEY, and put the public half in `update::UPDATE_PUBLIC_KEY`.
//!   RELEASE_SIGNING_KEY=<secret hex> cargo run -p generals-core --example release_sign -- sign dist/SHA256SUMS
//!       writes dist/SHA256SUMS.sig (hex of the 64-byte Ed25519 signature) and checks it against the public key built
//!       into the app, so a release can never be published signed with the wrong key.
use ed25519_dalek::{Signer, SigningKey};

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("keygen") => {
            let mut seed = [0u8; 32];
            seed[..16].copy_from_slice(&generals_proto::secure::random_token());
            seed[16..].copy_from_slice(&generals_proto::secure::random_token());
            let key = SigningKey::from_bytes(&seed);
            println!("secret {}", hex(&seed));
            println!("public {}", hex(key.verifying_key().as_bytes()));
        }
        Some("sign") if args.len() == 2 => {
            let secret = std::env::var("RELEASE_SIGNING_KEY").unwrap_or_default();
            let seed: Vec<u8> = (0..secret.trim().len() / 2).filter_map(|i| u8::from_str_radix(&secret.trim()[i * 2..i * 2 + 2], 16).ok()).collect();
            let seed: [u8; 32] = seed.try_into().unwrap_or_else(|_| {
                eprintln!("RELEASE_SIGNING_KEY must be 64 hex characters");
                std::process::exit(2);
            });
            let message = std::fs::read(&args[1]).unwrap_or_else(|e| {
                eprintln!("{}: {e}", args[1]);
                std::process::exit(2);
            });
            let sig = hex(&SigningKey::from_bytes(&seed).sign(&message).to_bytes());
            if let Err(e) = generals_core::update::verify_signature(generals_core::update::UPDATE_PUBLIC_KEY, &message, &sig) {
                eprintln!("the signing key does not match the key built into the app: {e}");
                std::process::exit(3);
            }
            std::fs::write(format!("{}.sig", args[1]), format!("{sig}\n")).expect("write signature");
            println!("signed {}", args[1]);
        }
        _ => {
            eprintln!("usage: release_sign keygen | release_sign sign <file>");
            std::process::exit(1);
        }
    }
}
