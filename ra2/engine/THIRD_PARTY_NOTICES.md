# Third-party notices

RA2 Engine itself is licensed under the Zero-Clause BSD licence (0BSD, see
`LICENSE`). The engine
binaries (desktop `ra2engine` and the Android APK) also include or link
the following third-party software.

## SDL 2.30.9 (Simple DirectMedia Layer)

Linked as `SDL2.dll` (desktop) / `libSDL2.so` (Android); the Android Java glue
(`android/app/src/main/java/org/libsdl/app/*`) is SDL's.
Source: https://github.com/libsdl-org/SDL (tag `release-2.30.9`; the build
downloads the release tarball pinned by SHA-256 in `cmake/sdl2_pin.cmake`).

```
Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## stb_image_write v1.16 (Sean Barrett)

`third_party/stb_image_write.h` (PNG output for screenshots / captures).
Dual-licensed public domain (Unlicense) or MIT; used here under the MIT
alternative:

```
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## doctest (Viktor Kirilov) — tests only

`third_party/doctest.h`, compiled into the `ra2assets_tests` test binary only
(not shipped). MIT License, Copyright (c) 2016-2023 Viktor Kirilov.

## MinGW-w64 runtime (Windows build)

The Windows `ra2engine.exe` / `ra2game.exe` link the GCC runtime statically:
libstdc++ and libgcc (GPL-3.0 with the GCC Runtime Library Exception, which
allows this without imposing terms on the program) and the MinGW-w64
winpthreads library:

```
Copyright (c) 2011 mingw-w64 project

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

## Android toolchain runtime

The APK ships `libc++_shared.so` from the Android NDK (LLVM libc++,
Apache License 2.0 with LLVM Exceptions).
