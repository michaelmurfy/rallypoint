#!/bin/sh
# Linux release packaging, run from the repository root.
#   package-linux.sh engine OUT            collect the built engine into OUT/engine (stripped, with its font and assets)
#                                          and its debug symbols into OUT/symbols
#   package-linux.sh tarball VERSION DEB OUT   turn the launcher .deb into OUT/rallypoint-VERSION-linux-x86_64.tar.gz
# The tarball is the deb's /usr tree: extract it with `--strip-components=1 -C ~/.local` (or /usr/local) and
# the launcher finds the engine in ../lib/Rallypoint/engine, the same place as in the deb and rpm.
set -e
case "$1" in
engine)
  out=$2
  bin=engine/build/linux-x64/GeneralsMD/generalszh
  fonts=/usr/share/fonts/truetype/dejavu
  mkdir -p "$out/engine/fonts" "$out/symbols"
  objcopy --only-keep-debug "$bin" "$out/symbols/generalszh.debug"
  strip --strip-all -o "$out/engine/generalszh" "$bin"
  chmod 755 "$out/engine/generalszh"
  # Menu text is drawn with this font (patch 0108), so no distribution needs a font package.
  cp "$fonts/DejaVuSans.ttf" "$fonts/DejaVuSans-Bold.ttf" "$out/engine/fonts/"
  cp /usr/share/doc/fonts-dejavu-core/copyright "$out/engine/fonts/LICENSE"
  # Textures of the Enhanced renderer (generated scorch atlas and height map, no game data), read through GEN_ASSETS.
  cp -R patches/upstream/moderngeneralszh/assets "$out/engine/assets"
  strings -a "$out/engine/generalszh" | grep -q 'DebugFrame_' || { echo "sync log writer missing from the engine"; exit 1; }
  ls -lR "$out/engine" | head -40
  ;;
tarball)
  ver=$2; deb=$3; out=$4
  tmp=$(mktemp -d)
  dpkg-deb -x "$deb" "$tmp/x"
  mkdir "$tmp/rallypoint-$ver"
  mv "$tmp/x/usr/"* "$tmp/rallypoint-$ver/"
  mkdir -p "$out"
  tar --owner=0 --group=0 -C "$tmp" -czf "$out/rallypoint-$ver-linux-x86_64.tar.gz" "rallypoint-$ver"
  rm -rf "$tmp"
  ls -l "$out"
  ;;
*)
  echo "usage: package-linux.sh engine OUT | tarball VERSION DEB OUT" >&2
  exit 2
  ;;
esac
