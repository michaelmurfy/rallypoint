#!/usr/bin/env bash
# Back up everything Rallypoint downloads or builds on, in case a source goes offline.
#
#   scripts/backup-sources.sh [DEST]            DEST defaults to ~/RallypointBackup
#   scripts/backup-sources.sh --no-git [DEST]   skip the git mirrors (they are the big part)
#   scripts/backup-sources.sh --verify [DEST]   only re-check the files already saved
#
# What it saves (read from the repo, so it follows version bumps):
#   downloads/   DepotDownloader (all 5 platforms), Control Bar HD, Control Bar Pro (all 5 sizes),
#                DXVK, the GenTool map packs. Pinned files are checked against the SHA-256 in the
#                source, and a mismatch is thrown away. Unpinned ones get their hash recorded.
#   git/         bare mirrors of this repo, the engine forks, the upstream engines, the graphics fix,
#                Control Bar Pro's source and DepotDownloader. `git clone <dir>` restores any of them.
#   MANUAL.txt   what a script cannot fetch (ModDB blocks automated downloads): the mod pages to save by hand.
#   SHA256SUMS   every file in downloads/, so --verify (or `shasum -c`) can prove nothing rotted.
set -euo pipefail

cd "$(dirname "$0")/.."
REPO=$PWD
GIT=1
VERIFY=0
DEST=""
for a in "$@"; do
  case $a in
    --no-git) GIT=0 ;;
    --verify) VERIFY=1 ;;
    -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option $a" >&2; exit 2 ;;
    *) DEST=$a ;;
  esac
done
DEST=${DEST:-$HOME/RallypointBackup}
mkdir -p "$DEST/downloads" "$DEST/git"

if command -v sha256sum >/dev/null; then sha() { sha256sum "$1" | cut -d' ' -f1; }; else sha() { shasum -a 256 "$1" | cut -d' ' -f1; }; fi

ok=0; bad=0; unpinned=0
note() { printf '%s\n' "$*"; }

if [ $VERIFY = 1 ]; then
  cd "$DEST/downloads"
  [ -f ../SHA256SUMS ] || { echo "no SHA256SUMS in $DEST: run the backup first" >&2; exit 1; }
  while read -r want file; do
    if [ -f "$file" ] && [ "$(sha "$file")" = "$want" ]; then ok=$((ok+1)); else note "BAD     $file"; bad=$((bad+1)); fi
  done < ../SHA256SUMS
  note "$ok files match, $bad do not"
  [ $bad = 0 ]
  exit
fi

# fetch URL FILE [EXPECTED_SHA256]: skip a good copy, check a pinned hash, never keep a file that fails it.
fetch() {
  local url=$1 file=$DEST/downloads/$2 want=${3:-} got
  if [ -f "$file" ] && { [ -z "$want" ] || [ "$(sha "$file")" = "$want" ]; }; then
    note "have    $2"
  else
    note "get     $2"
    if ! curl -fsSL --proto '=https' --proto-redir '=https' --retry 3 --max-time 1800 -o "$file.part" "$url"; then
      rm -f "$file.part"; note "FAILED  $2 ($url)"; bad=$((bad+1)); return 0
    fi
    mv "$file.part" "$file"
  fi
  got=$(sha "$file")
  if [ -n "$want" ] && [ "$got" != "$want" ]; then
    rm -f "$file"; note "MISMATCH $2 (expected $want, got $got): removed"; bad=$((bad+1)); return 0
  fi
  [ -z "$want" ] && { unpinned=$((unpinned+1)); note "        unpinned, recorded $got"; }
  ok=$((ok+1))
}

# --- DepotDownloader: version from assets.rs, one pinned zip per platform
dd=$(sed -n 's/^const DD_VERSION: &str = "\(.*\)";/\1/p' launcher/core/src/assets.rs)
while read -r slug want; do
  fetch "https://github.com/SteamRE/DepotDownloader/releases/download/$dd/DepotDownloader-$slug.zip" "$dd-$slug.zip" "$want"
done < <(sed -n 's/.*b("\([a-z0-9-]*\)", "\([0-9a-f]\{64\}\)".*/\1 \2/p' launcher/core/src/assets.rs)

# --- Control Bar HD and Pro: every url/sha256 pair in icons.rs
url_hd=$(sed -n 's/^pub const URL: &str = "\(.*\)";/\1/p' launcher/core/src/icons.rs)
sha_hd=$(sed -n 's/^pub const SHA256: &str = "\(.*\)";/\1/p' launcher/core/src/icons.rs)
fetch "$url_hd" "$(basename "$url_hd")" "$sha_hd"
while read -r url want; do
  fetch "$url" "$(basename "$url")" "$want"
done < <(sed -n 's/.*url: "\(https[^"]*\)", sha256: "\([0-9a-f]\{64\}\)".*/\1 \2/p' launcher/core/src/icons.rs)

# --- DXVK (the Windows engine's d3d8.dll): version and hash from release.yml
dxvk=$(sed -n "s/^ *DXVK_VERSION: '\(.*\)'/\1/p" .github/workflows/release.yml | head -1)
dxvk_sha=$(sed -n "s/^ *DXVK_SHA256: '\([0-9a-f]\{64\}\)'.*/\1/p" .github/workflows/release.yml | head -1)
if [ -n "$dxvk" ]; then
  fetch "https://github.com/doitsujin/dxvk/releases/download/v$dxvk/dxvk-$dxvk.tar.gz" "dxvk-$dxvk.tar.gz" "$dxvk_sha"
fi

# --- GenTool map packs (no hash is pinned: it is recorded). Packs without a zip_url are manual.
python3 -I - "$REPO/mods/mappacks.json" <<'PY' > "$DEST/.mappacks.txt"
import json, sys
for p in json.load(open(sys.argv[1])):
    print(p.get("zip_url") or "-", p["id"], p.get("homepage", ""), sep="\t")
PY
while IFS=$'\t' read -r url id home; do
  if [ "$url" = "-" ]; then continue; fi
  fetch "$url" "$(basename "$url")"
done < "$DEST/.mappacks.txt"

# --- what has to be saved by hand
{
  echo "Save these by hand (ModDB and similar sites block scripts). Keep the file you download."
  echo
  python3 -I - "$REPO/mods/mods.json" "$REPO/mods/mappacks.json" <<'PY'
import json, sys
for p in json.load(open(sys.argv[1])):
    print(f"- {p['name']}: {p.get('download_page') or p.get('homepage')}")
for p in json.load(open(sys.argv[2])):
    if not p.get("zip_url"):
        print(f"- {p['name']} (map pack): {p.get('homepage')}")
PY
  echo "- Your own game files (Steam: Generals 2229870, Zero Hour 2732960): copy the install folders."
  echo "- GenTool: https://www.gentool.net/ (the Control Bar and map downloads above are already saved)."
} > "$DEST/MANUAL.txt"
rm -f "$DEST/.mappacks.txt"

# --- checksums of everything saved
( cd "$DEST/downloads" && for f in *; do [ -f "$f" ] && printf '%s  %s\n' "$(sha "$f")" "$f"; done ) > "$DEST/SHA256SUMS"

# --- git mirrors
if [ $GIT = 1 ]; then
  mirror() {
    local url=$1 name=$2 dir=$DEST/git/$2.git
    if [ -d "$dir" ]; then
      note "update  git/$name.git"; git -C "$dir" remote update --prune >/dev/null 2>&1 || { note "FAILED  $url"; bad=$((bad+1)); }
    else
      note "mirror  git/$name.git"; git clone --mirror --quiet "$url" "$dir" 2>/dev/null || { rm -rf "$dir"; note "FAILED  $url"; bad=$((bad+1)); }
    fi
  }
  mirror https://github.com/michaelmurfy/rallypoint rallypoint
  mirror https://github.com/dvcdsys/GeneralsGameCode-macOS dvcdsys-GeneralsGameCode-macOS
  mirror https://github.com/TheSuperHackers/GeneralsGameCode GeneralsGameCode
  mirror https://github.com/TheSuperHackers/GeneralsControlBar GeneralsControlBar
  mirror https://github.com/electronicarts/CnC_Generals_Zero_Hour CnC_Generals_Zero_Hour
  mirror https://github.com/olcayseygan/CnCGeneralsZH-Reforged CnCGeneralsZH-Reforged
  mirror https://github.com/trailblazingfive/cnc-generals-graphics-fix cnc-generals-graphics-fix
  mirror https://github.com/SteamRE/DepotDownloader DepotDownloader
fi

note
note "saved to $DEST: $ok files ok ($unpinned unpinned, hash recorded), $bad problems"
[ $bad = 0 ]
