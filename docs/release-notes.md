## Rallypoint 0.5.10

Skirmishes can start with the money you choose, and setting up a server is easier.

- **Starting money.** A new menu beside Speed in the skirmish setup: $5,000 to $250,000, or the game's own setting. It lights up when you change it and only applies to skirmishes.
- **Server setup as a regular user.** The Ansible playbook now works from a user that can sudo: it asks for the sudo password, adds you to the docker group, and replaces a hand-made Docker apt source instead of tripping over it.
- **SSH is restarted, not reloaded.** On Debian 13 a reload could stop SSH and cut the server off. Tried on a Debian 13 test host.

## Rallypoint 0.5.9

Every game's official maps joined one list, each tagged **Official** and with its game, and setting up a skirmish on one switches "Play with" to the game that has it.

## Rallypoint 0.5.8

The game's own maps joined the map list, marked **Official**. Only skirmish maps are listed, and a map of yours with the same name wins.
