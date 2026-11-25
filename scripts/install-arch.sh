#!/bin/sh
# One-command installer and updater for CachyOS, Arch Linux and other Arch-based systems.
#
#   sh install-arch.sh                       install the newest release into ~/.local
#   sh install-arch.sh FILE.tar.gz           install a tarball you already downloaded
#   sh install-arch.sh --no-deps [FILE]      skip the pacman dependency step
#   sh install-arch.sh --uninstall           remove it again
#
# Run it straight from the repository, or pipe it from curl:
#   curl -fsSL https://raw.githubusercontent.com/michaelmurfy/rallypoint/main/scripts/install-arch.sh | sh
#
# RALLYPOINT_PREFIX overrides the prefix (default: ~/.local).
# The launcher needs its engine beside it, at <prefix>/lib/Rallypoint/engine, so the
# tarball's top folder (rallypoint-<version>/) is stripped on extraction.
#
# Functions can be loaded without running the installer for testing:
#   RALLYPOINT_INSTALL_LIB=1 . ./install-arch.sh

PREFIX=${RALLYPOINT_PREFIX:-$HOME/.local}
REPO=michaelmurfy/rallypoint
DEPS_PACKAGES="webkit2gtk-4.1 gtk3 libx11 freetype2 vulkan-icd-loader alsa-lib curl tar"
TARBALL=
NO_DEPS=0
UNINSTALL=0
WORKDIR=

usage() {
    cat <<'EOF'
Install or update Rallypoint on CachyOS, Arch Linux and other Arch-based systems.

  sh install-arch.sh                    install or update to the newest release
  sh install-arch.sh FILE.tar.gz        install a release tarball you downloaded
  sh install-arch.sh --no-deps [FILE]   skip installing the system libraries
  sh install-arch.sh --uninstall        remove Rallypoint again
  sh install-arch.sh --help             show this help

Rallypoint goes into ~/.local (RALLYPOINT_PREFIX changes that), so it needs no root.
Only the system libraries are installed with sudo pacman.
EOF
}

say() {
    printf '%s\n' "$1"
}

die() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

cleanup() {
    if [ -n "$WORKDIR" ] && [ -d "$WORKDIR" ]; then
        rm -rf "$WORKDIR"
    fi
}

# Print "name<TAB>url" for every asset of a GitHub release JSON.
# No jq: each asset object is found by cutting at "}", and the name is carried
# forward to the browser_download_url that follows it in the same object.
release_assets() {
    printf '%s\n' "$1" | tr '}' '\n' | awk '
        {
            if (match($0, /"name"[ ]*:[ ]*"[^"]*"/)) {
                n = substr($0, RSTART, RLENGTH)
                sub(/^"name"[ ]*:[ ]*"/, "", n)
                sub(/"$/, "", n)
                last = n
            }
            if (match($0, /"browser_download_url"[ ]*:[ ]*"[^"]*"/)) {
                u = substr($0, RSTART, RLENGTH)
                sub(/^"browser_download_url"[ ]*:[ ]*"/, "", u)
                sub(/"$/, "", u)
                if (last != "")
                    print last "\t" u
            }
        }'
}

# Pick the client tarball and SHA256SUMS out of the release JSON.
# Prints the tarball's file name on line 1, its URL on line 2, the sums URL on line 3.
select_release_assets() {
    assets=$(release_assets "$1")
    name=
    url=
    sums=
    tab=$(printf '\t')
    while IFS=$tab read -r asset_name asset_url; do
        case "$asset_name" in
            SHA256SUMS)
                [ -n "$sums" ] || sums=$asset_url
                ;;
            rallypoint-server-*)
                ;;
            rallypoint-*-linux-x86_64.tar.gz)
                if [ -z "$name" ]; then
                    name=$asset_name
                    url=$asset_url
                fi
                ;;
        esac
    done <<EOF
$assets
EOF
    printf '%s\n%s\n%s\n' "$name" "$url" "$sums"
}

# Escape a string for use in a sed s||| replacement.
sed_escape() {
    printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'
}

# The Vulkan driver package to install, or nothing if an ICD is present already.
detect_vulkan_package() {
    if [ -n "$(find /usr/share/vulkan/icd.d -maxdepth 1 -name '*.json' -print -quit 2>/dev/null)" ]; then
        return 0
    fi
    vendor=
    if command -v lspci >/dev/null 2>&1; then
        line=$(lspci 2>/dev/null | grep -i -E 'vga|3d controller|display controller' | head -n 1)
        case "$line" in
            *AMD*|*ATI*|*Radeon*|*"Advanced Micro Devices"*) vendor=amd ;;
            *Intel*) vendor=intel ;;
            *NVIDIA*|*Nvidia*) vendor=nvidia ;;
        esac
    fi
    if [ -z "$vendor" ]; then
        for dev in /sys/class/drm/*/device/vendor; do
            [ -f "$dev" ] || continue
            case "$(cat "$dev" 2>/dev/null)" in
                0x1002) vendor=amd; break ;;
                0x8086) vendor=intel; break ;;
                0x10de) vendor=nvidia; break ;;
            esac
        done
    fi
    case "$vendor" in
        amd) printf '%s\n' vulkan-radeon ;;
        intel) printf '%s\n' vulkan-intel ;;
        nvidia) printf '%s\n' nvidia-utils ;;
    esac
}

# Can we run pacman, here and now?
can_pacman() {
    command -v pacman >/dev/null 2>&1 || return 1
    [ "$(id -u)" = 0 ] && return 0
    command -v sudo >/dev/null 2>&1 || return 1
    sudo -n true 2>/dev/null && return 0
    [ -t 0 ] && return 0
    [ -r /dev/tty ] && return 0
    return 1
}

run_pacman() {
    if [ "$(id -u)" = 0 ]; then
        pacman "$@"
    else
        sudo pacman "$@"
    fi
}

missing_libs_message() {
    say "Could not install the libraries with pacman. If Rallypoint says something is missing, install them yourself:"
    say "  sudo pacman -S --needed $DEPS_PACKAGES"
}

install_dependencies() {
    pkg=$(detect_vulkan_package)
    if ! can_pacman; then
        if command -v pacman >/dev/null 2>&1; then
            say "sudo is not available here, so the libraries are left alone."
        else
            say "No pacman on this system, so the libraries are left alone."
        fi
        say "Rallypoint needs: $DEPS_PACKAGES"
        if [ -n "$pkg" ]; then
            say "No Vulkan driver found. Install $pkg for your GPU."
        fi
        return 0
    fi

    say "Installing the libraries with pacman."
    # shellcheck disable=SC2086  # the package list is meant to split into words
    run_pacman -S --needed --noconfirm $DEPS_PACKAGES || missing_libs_message

    if [ -n "$pkg" ]; then
        say "No Vulkan driver found. Installing $pkg."
        run_pacman -S --needed --noconfirm "$pkg" || say "Could not install $pkg. Add it yourself for your GPU."
    fi
}

# Download the newest release's tarball into TMPDIR and verify it against SHA256SUMS.
download_release() {
    command -v curl >/dev/null 2>&1 || die "curl is needed to download a release"
    WORKDIR=$(mktemp -d) || die "could not create a temp folder"

    say "Looking up the newest release."
    json=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest") \
        || die "could not reach the GitHub API"

    selection=$(select_release_assets "$json")
    name=$(printf '%s\n' "$selection" | sed -n '1p')
    url=$(printf '%s\n' "$selection" | sed -n '2p')
    sums_url=$(printf '%s\n' "$selection" | sed -n '3p')

    [ -n "$name" ] || die "the newest release has no linux-x86_64.tar.gz"
    [ -n "$sums_url" ] || die "the newest release has no SHA256SUMS"

    say "Downloading $name."
    curl -fL -o "$WORKDIR/$name" "$url" || die "could not download $name"
    curl -fsSL -o "$WORKDIR/SHA256SUMS" "$sums_url" || die "could not download SHA256SUMS"

    expected=$(awk -v f="$name" '
        { n = $2; sub(/^\*/, "", n); if (n == f) { print $1; exit } }
    ' "$WORKDIR/SHA256SUMS")
    [ -n "$expected" ] || die "SHA256SUMS does not list $name"
    actual=$(sha256sum "$WORKDIR/$name" | awk '{ print $1 }')
    if [ "$expected" != "$actual" ]; then
        rm -f "$WORKDIR/$name"
        die "checksum mismatch for $name, download deleted"
    fi
    say "Checksum matches."

    TARBALL=$WORKDIR/$name
}

install_tarball() {
    mkdir -p "$PREFIX" || die "could not create $PREFIX"

    # Unpack beside the install and move the new files into place with a rename. Writing over a launcher that is
    # still running fails ("Text file busy"), replacing it does not.
    stage=$PREFIX/.rallypoint-stage
    rm -rf "${stage:?}" || die "could not clear $stage"
    mkdir -p "$stage" || die "could not create $stage"
    if ! tar -xzf "$TARBALL" --strip-components=1 -C "$stage"; then
        rm -rf "$stage"
        die "could not extract $TARBALL"
    fi

    # A tarball whose top folder holds usr/ (an older layout) is moved up so that
    # bin/, lib/ and share/ sit directly in the prefix.
    if [ -d "$stage/usr" ] && [ ! -e "$stage/bin/generals-launcher" ]; then
        cp -a "$stage/usr/." "$stage/" || die "could not move usr/ out of the tarball"
        rm -rf "${stage:?}/usr"
    fi

    if [ ! -e "$stage/bin/generals-launcher" ]; then
        rm -rf "$stage"
        die "the tarball has no bin/generals-launcher"
    fi

    say "Installing into $PREFIX."
    rm -rf "${PREFIX:?}/lib/Rallypoint" || die "could not remove the old install"
    mkdir -p "$PREFIX/bin" "$PREFIX/lib" "$PREFIX/share" || die "could not write to $PREFIX"
    if [ -d "$stage/lib/Rallypoint" ]; then
        mv "$stage/lib/Rallypoint" "$PREFIX/lib/Rallypoint" || die "could not install the engine"
    fi
    mv "$stage/bin/generals-launcher" "$PREFIX/bin/generals-launcher" || die "could not install the launcher"
    chmod 755 "$PREFIX/bin/generals-launcher" 2>/dev/null
    if [ -d "$stage/share" ]; then
        cp -a "$stage/share/." "$PREFIX/share/" || die "could not install the desktop files"
    fi
    rm -rf "${stage:?}"

    desktop=$PREFIX/share/applications/Rallypoint.desktop
    if [ -f "$desktop" ]; then
        # A menu entry cannot rely on ~/.local/bin being on PATH.
        escaped=$(sed_escape "$PREFIX/bin/generals-launcher")
        sed -i "s|^Exec=.*|Exec=$escaped|" "$desktop" || die "could not update $desktop"
    fi

    if command -v update-desktop-database >/dev/null 2>&1 && [ -d "$PREFIX/share/applications" ]; then
        update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -d "$PREFIX/share/icons/hicolor" ]; then
        gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
    fi

    say "Done. Start it with $PREFIX/bin/generals-launcher, or from your application menu."
}

uninstall() {
    say "Removing Rallypoint from $PREFIX."
    rm -f "$PREFIX/bin/generals-launcher"
    rm -rf "${PREFIX:?}/lib/Rallypoint"
    rm -rf "${PREFIX:?}/.rallypoint-stage"
    rm -f "$PREFIX/share/applications/Rallypoint.desktop"
    if [ -d "$PREFIX/share/icons" ]; then
        find "$PREFIX/share/icons" -type f -iname 'rallypoint.*' -exec rm -f {} + 2>/dev/null
        if [ -d "$PREFIX/share/icons/hicolor" ]; then
            find "$PREFIX/share/icons/hicolor" -depth -type d -empty -delete 2>/dev/null
            # The cache the install refreshed is pointless once no icon is left here.
            if [ -f "$PREFIX/share/icons/hicolor/icon-theme.cache" ] &&
                [ -z "$(find "$PREFIX/share/icons/hicolor" -type f ! -name icon-theme.cache -print -quit 2>/dev/null)" ]; then
                rm -f "$PREFIX/share/icons/hicolor/icon-theme.cache"
                rmdir "$PREFIX/share/icons/hicolor" 2>/dev/null
            fi
        fi
    fi
    say "Removed."
}

main() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --help|-h)
                usage
                return 0
                ;;
            --uninstall)
                UNINSTALL=1
                shift
                ;;
            --no-deps)
                NO_DEPS=1
                shift
                ;;
            -*)
                usage >&2
                die "unknown option: $1"
                ;;
            *)
                case "$1" in
                    *.tar.gz)
                        [ -z "$TARBALL" ] || die "give only one tarball"
                        TARBALL=$1
                        ;;
                    *)
                        usage >&2
                        die "not a .tar.gz: $1"
                        ;;
                esac
                shift
                ;;
        esac
    done

    [ "$PREFIX" = / ] && die "refusing to install into /"

    if [ "$UNINSTALL" = 1 ]; then
        uninstall
        return 0
    fi

    if [ -n "$TARBALL" ]; then
        case "$TARBALL" in
            /*) ;;
            *) TARBALL=$PWD/$TARBALL ;;
        esac
        [ -f "$TARBALL" ] || die "no such file: $TARBALL"
    else
        download_release
    fi

    if [ "$NO_DEPS" = 1 ]; then
        :
    else
        install_dependencies
    fi

    install_tarball
}

if [ "${RALLYPOINT_INSTALL_LIB:-0}" = 1 ]; then
    :
else
    trap cleanup EXIT
    trap 'exit 1' INT HUP TERM
    main "$@"
fi
