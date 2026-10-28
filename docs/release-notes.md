## Rallypoint 0.5.7

Skirmishes get a speed dial, setting up a server no longer needs a computer science degree, and Settings has a Changelog button.

- **Game speed for skirmishes.** The map skirmish screen has a speed menu beside Play skirmish, from 50% to 200%. Normal until you say otherwise, and skirmishes only: online games stay the same speed for everyone.
- **A proper way to set up a server.** There's now an Ansible playbook that turns a blank Debian, Ubuntu or Raspberry Pi OS machine into a Rallypoint server (updates, Docker, firewall, SSH protection, automatic security updates), plus a `rallypoint` command to start, stop, update and check it. The README walks through it, with a few hosting suggestions.
- **Servers say when they're ready.** `make up` pulls the release and waits until the container reports healthy. `make build` builds from source.
- **Changelog button.** *Settings → Updates* shows the notes for this version and the two before it, whether or not an update is waiting.
- **Known.** The game speed menu hasn't had a long play test yet. If 50% or 200% feels wrong, tell us.

## Rallypoint 0.5.6

Fixed units and flags that drew black on Mac (the GLA worker, team-colour parts and base emblems). Only the Mac engine had the problem; Windows was fine.

## Rallypoint 0.5.5

Multiplayer opens the LAN lobby directly, the window and Mac menu name the game (Cmd+Q quits), the unused Custom Mission button is hidden, and the Play page fits without a scrollbar.
