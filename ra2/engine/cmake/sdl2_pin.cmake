# W6-06-14: the ONE pinned SDL2 source for both builds (desktop FetchContent
# in CMakeLists.txt and the Android jni build). A release tarball + SHA-256
# instead of a git clone: reproducible, verified, and ~8 MB instead of the
# full history (the old GIT_REPOSITORY clone took ~460 s to configure).
# Offline: pass -DFETCHCONTENT_SOURCE_DIR_SDL2=<SDL 2.30.9 source> (desktop)
# or -DSDL2_SRC_DIR=<same> / env RA2_SDL2_SRC_DIR (Android).
set(RA2_SDL2_VERSION "2.30.9")
set(RA2_SDL2_URL "https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz")
set(RA2_SDL2_SHA256 "24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4")
