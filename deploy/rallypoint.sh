#!/bin/sh
# rallypoint: the one command you need on a Rallypoint server. The Ansible playbook installs it as /usr/local/bin/rallypoint.
# It runs the Makefile in the server's folder, so `rallypoint update` and `make update` there do the same thing.
DIR="${RALLYPOINT_DIR:-/opt/rallypoint}"

[ -d "$DIR" ] || { echo "No Rallypoint server in $DIR."; exit 1; }
# The server's files belong to root, so a regular user is sent through sudo.
if [ "$(id -u)" != 0 ]; then
  exec sudo RALLYPOINT_DIR="$DIR" "$0" "$@"
fi
cd "$DIR" || exit 1

port() { sed -n 's/^PORT=//p' .env 2>/dev/null | tail -1 | grep . || echo 7777; }

case "${1:-help}" in
  start)   make up ;;
  stop)    make down ;;
  restart) make restart ;;
  update)  make update ;;
  logs)    make logs ;;
  key)     make key ;;
  status)
    make status
    echo
    echo "Players connect to this server on UDP port $(port)."
    if command -v ufw >/dev/null 2>&1; then ufw status | sed -n '1p;/'"$(port)"'/p'; fi
    ;;
  countries)
    # rallypoint countries NZ,AU   limits who can connect; rallypoint countries none   lets everyone in
    [ -n "$2" ] || { echo "usage: rallypoint countries NZ,AU   (or: none)"; exit 1; }
    list="$2"; [ "$list" = none ] && list=""
    [ -f .env ] || make config
    if [ -n "$list" ]; then
      echo "$list" | grep -Eq '^[A-Za-z]{2}(,[A-Za-z]{2})*$' || { echo "Use two-letter country codes, like NZ or NZ,AU"; exit 1; }
    fi
    sed -i "s/^ALLOWED_COUNTRIES=.*/ALLOWED_COUNTRIES=$list/" .env
    make restart
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
  countries   rallypoint countries NZ,AU  (only those countries), or: none
  version     what is installed
EOF
    ;;
esac
