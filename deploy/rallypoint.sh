#!/bin/sh
# rallypoint: the one command you need on a Rallypoint server. The Ansible playbook installs it as /usr/local/bin/rallypoint.
# It runs the Makefile in the server's folder, so `rallypoint update` and `make update` there do the same thing.
# A fixed folder, not an environment variable: this script runs as root and runs the Makefile it finds there.
DIR=/opt/rallypoint

[ -d "$DIR" ] || { echo "No Rallypoint server in $DIR."; exit 1; }
# The server's files belong to root, so a regular user is sent through sudo.
if [ "$(id -u)" != 0 ]; then
  exec sudo "$0" "$@"
fi
cd "$DIR" || exit 1

port() { sed -n 's/^PORT=//p' .env 2>/dev/null | tail -1 | grep . || echo 7777; }

case "${1:-help}" in
  start)   make -s up ;;
  stop)    make -s down ;;
  restart) make -s restart ;;
  update)  make -s update ;;
  logs)    make -s logs ;;
  key)     make -s key ;;
  status)
    make -s status
    echo
    echo "Players connect to this server on UDP port $(port)."
    echo "Countries let in: $(sed -n 's/^ALLOWED_COUNTRIES=//p' .env | tail -1 | grep . || echo everyone)"
    if command -v ufw >/dev/null 2>&1; then ufw status | sed -n '1p;/'"$(port)"'/p'; fi
    ;;
  countries)
    # rallypoint countries            a menu
    # rallypoint countries NZ,AU      set it directly (or: none)
    [ -f .env ] || make -s config
    current=$(sed -n 's/^ALLOWED_COUNTRIES=//p' .env | tail -1)
    list="$2"
    if [ -z "$list" ]; then
      echo
      echo "Who can connect to this server?"
      echo "Right now: ${current:-everyone}"
      echo
      echo "  1) New Zealand only        (NZ)"
      echo "  2) New Zealand and Australia (NZ,AU)"
      echo "  3) Australia only          (AU)"
      echo "  4) The US and Canada       (US,CA)"
      echo "  5) The UK and Europe       (GB,IE,DE,FR,NL)"
      echo "  6) Choose my own countries"
      echo "  7) Let everyone in"
      echo "  q) Leave it as it is"
      echo
      printf "Pick one: "
      read -r pick
      case "$pick" in
        1) list=NZ ;;
        2) list=NZ,AU ;;
        3) list=AU ;;
        4) list=US,CA ;;
        5) list=GB,IE,DE,FR,NL ;;
        6) printf "Two-letter country codes, separated by commas (for example NZ,AU,GB): "; read -r list ;;
        7) list=none ;;
        *) echo "Nothing changed."; exit 0 ;;
      esac
    fi
    [ "$list" = none ] && list=""
    if [ -n "$list" ]; then
      list=$(echo "$list" | tr 'a-z' 'A-Z' | tr -d ' ')
      echo "$list" | grep -Eq '^[A-Z]{2}(,[A-Z]{2})*$' || { echo "Use two-letter country codes, like NZ or NZ,AU"; exit 1; }
    fi
    sed -i "s/^ALLOWED_COUNTRIES=.*/ALLOWED_COUNTRIES=$list/" .env
    echo "Now letting in: ${list:-everyone}"
    make -s restart
    ;;
  version) git log -1 --format='server files from %h (%cd)' --date=short; docker compose images 2>/dev/null ;;
  help|-h|--help|*)
    cat <<'EOF'
rallypoint <command>

  status      is it running, and which port do players use
  start       start the server (pulls the latest release first)
  stop        stop it
  restart     apply changes made to .env
  update      get the newest release and restart
  logs        watch what the server is doing (Ctrl+C to leave)
  key         show the server's identity fingerprint, for players to check
  countries   choose who can connect, from a menu (or: rallypoint countries NZ,AU, or none)
  version     what is installed
EOF
    ;;
esac
