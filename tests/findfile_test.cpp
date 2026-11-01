// FindFirstFile (macOS shim) must treat [ and ] in a name literally: Win32 wildcards are only * and ?.
// Community maps are named like "[RANK] Desert Vortex ZH v1"; with fnmatch's character classes the engine never found them.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <windows.h>

static int check(const char *spec, const char *want) {
  WIN32_FIND_DATA fd;
  HANDLE h = FindFirstFile(spec, &fd);
  const bool ok = want ? (h != INVALID_HANDLE_VALUE && std::string(fd.cFileName) == want) : (h == INVALID_HANDLE_VALUE);
  if (!ok) printf("FAIL %s -> %s\n", spec, h == INVALID_HANDLE_VALUE ? "not found" : fd.cFileName);
  if (h != INVALID_HANDLE_VALUE) FindClose(h);
  return ok ? 0 : 1;
}

int main() {
  if (system("rm -rf /tmp/findfile_test && mkdir -p '/tmp/findfile_test/[RANK] Foo v1' && touch '/tmp/findfile_test/[RANK] Foo v1/[RANK] Foo v1.map' '/tmp/findfile_test/[RANK] Foo v1/R Foo v1.map'") != 0) return 2;
  int bad = 0;
  bad += check("/tmp/findfile_test/[RANK] Foo v1/[RANK] Foo v1.map", "[RANK] Foo v1.map");
  bad += check("/tmp/findfile_test/[rank] foo v1/[rank] foo v1.map", "[RANK] Foo v1.map");  // case-insensitive, as on Windows
  bad += check("/tmp/findfile_test/[RANK] Foo v1/[RANK] Foo v2.map", nullptr);
  {
    WIN32_FIND_DATA fd; int n = 0;
    HANDLE h = FindFirstFile("/tmp/findfile_test/[RANK] Foo v1/*.map", &fd);   // wildcards still work
    if (h != INVALID_HANDLE_VALUE) { do ++n; while (FindNextFile(h, &fd)); FindClose(h); }
    if (n != 2) { printf("FAIL *.map found %d, want 2\n", n); ++bad; }
  }
  system("rm -rf /tmp/findfile_test");
  if (!bad) printf("findfile_test ok\n");
  return bad ? 1 : 0;
}
