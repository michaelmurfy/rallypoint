# Security

Report a vulnerability privately through GitHub's "Report a vulnerability" button on the Security tab of this repository, not in a public issue. Include what you did, what happened, and which version.

How the project protects its users:
- **Updates** are signed (Ed25519) and only installed from inside the app when the signature on the release's `SHA256SUMS` matches the key built into the app. The public key is `UPDATE_PUBLIC_KEY` in `launcher/core/src/update.rs`.
- **The relay** encrypts everything with Noise (`NK`), accepts only the game's own packets and limits abuse per address and per session. See "Server design" in `agent.md`.
- **Passwords** are never put on a command line or in a log. A room password is only stored if you ask for it.
- **Dependencies** are pinned by `Cargo.lock`, audited weekly (`audit.yml`) and updated through Dependabot pull requests.
