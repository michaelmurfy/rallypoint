# CMake generated Testfile for 
# Source directory: /home/mmurphy/projects/rallypoint/ra2/engine
# Build directory: /home/mmurphy/projects/rallypoint/ra2/build-mingw
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[ra2assets_tests]=] "/home/mmurphy/projects/rallypoint/ra2/build-mingw/ra2assets_tests.exe")
set_tests_properties([=[ra2assets_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/mmurphy/projects/rallypoint/ra2/engine/CMakeLists.txt;368;add_test;/home/mmurphy/projects/rallypoint/ra2/engine/CMakeLists.txt;0;")
subdirs("_deps/sdl2-build")
