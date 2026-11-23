// R-171: a tiny sampling profiler for the desktop (Windows x64) build.
// Dev-only: RA2_PROFILE=<file> starts it in main(); every ~1 ms a helper
// thread suspends the main thread, walks its stack with the x64 unwind
// tables (RtlLookupFunctionEntry / RtlVirtualUnwind -- MinGW-w64 emits
// .pdata for every function) and counts the return addresses. At exit the
// counts are written as module-relative addresses:
//   self <rva-hex> <samples>      (the leaf: where the CPU was)
//   incl <rva-hex> <samples>      (anywhere on the stack, once per sample)
// tools/profile_report.py maps them to function names with `nm`.
// No-op on other platforms. Never used by a gate or --play hash.
#include "os_profiler.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <thread>

namespace ra2engine {
namespace {
struct Prof {
    std::string path;
    HANDLE target = nullptr;
    std::thread th;
    std::atomic<bool> stop{false};
    std::map<DWORD64, unsigned> self, incl;
    unsigned samples = 0;
    DWORD64 base = 0;
};
Prof* g_prof = nullptr;

// While the main thread is suspended NOTHING here may allocate or take a
// user-mode lock (it may hold the heap / loader lock): only the registers
// and a raw copy of the top of its stack are taken. The unwind then runs on
// the copy (Rsp / Rbp rebased into it) after the thread was resumed.
constexpr std::size_t kStackCopy = 64 * 1024;
alignas(16) unsigned char g_stack[kStackCopy];

void sample_once(Prof& P) {
    if (SuspendThread(P.target) == static_cast<DWORD>(-1)) return;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    std::size_t n = 0;
    DWORD64 lo = 0;
    const bool ok = GetThreadContext(P.target, &ctx) != 0;
    if (ok) {
        MEMORY_BASIC_INFORMATION mbi{};
        lo = ctx.Rsp;
        if (VirtualQuery(reinterpret_cast<void*>(lo), &mbi, sizeof mbi)) {
            const DWORD64 hi = reinterpret_cast<DWORD64>(mbi.BaseAddress) + mbi.RegionSize;
            n = static_cast<std::size_t>(std::min<DWORD64>(hi - lo, kStackCopy));
            std::memcpy(g_stack, reinterpret_cast<void*>(lo), n);
        }
    }
    ResumeThread(P.target);
    if (!ok) return;
    ++P.samples;
    const DWORD64 off = reinterpret_cast<DWORD64>(g_stack) - lo;
    auto in_copy = [&](DWORD64 a) { return a >= lo && a + 8 <= lo + n; };
    ctx.Rsp += off;
    if (in_copy(ctx.Rbp)) ctx.Rbp += off;
    const DWORD64 clo = lo + off, chi = lo + off + n;
    std::set<DWORD64> seen;
    for (int depth = 0; depth < 48 && ctx.Rip; ++depth) {
        if (ctx.Rsp < clo || ctx.Rsp + 8 > chi) break;   // walked off the copy
        const DWORD64 pc = ctx.Rip;
        if (depth == 0) ++P.self[pc];
        if (seen.insert(pc).second) ++P.incl[pc];
        DWORD64 image = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(pc, &image, nullptr);
        if (!fn) {   // a leaf with no unwind data: return address at [rsp]
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
            continue;
        }
        void* handler_data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, pc, fn, &ctx, &handler_data, &frame, nullptr);
        // a frame register restored from the copy still points at the real stack
        if (ctx.Rbp >= lo && ctx.Rbp + 8 <= lo + n) ctx.Rbp += off;
    }
}
}  // namespace

void os_profiler_start(const char* path) {
    if (g_prof || !path || !*path) return;
    auto* P = new Prof;
    P->path = path;
    P->base = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &P->target,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    timeBeginPeriod(1);
    P->th = std::thread([P] {
        while (!P->stop.load()) {
            sample_once(*P);
            Sleep(1);
        }
    });
    g_prof = P;
    std::fprintf(stderr, "[profile] sampling the main thread -> %s\n", path);
}

void os_profiler_stop() {
    Prof* P = g_prof;
    if (!P) return;
    g_prof = nullptr;
    P->stop = true;
    P->th.join();
    timeEndPeriod(1);
    if (FILE* f = std::fopen(P->path.c_str(), "w")) {
        std::fprintf(f, "# samples %u base %llx\n", P->samples, static_cast<unsigned long long>(P->base));
        for (const auto& [a, n] : P->self)
            std::fprintf(f, "self %llx %u\n", static_cast<unsigned long long>(a - P->base), n);
        for (const auto& [a, n] : P->incl)
            std::fprintf(f, "incl %llx %u\n", static_cast<unsigned long long>(a - P->base), n);
        std::fclose(f);
    }
    std::fprintf(stderr, "[profile] %u samples written to %s\n", P->samples, P->path.c_str());
    CloseHandle(P->target);
    delete P;
}
}  // namespace ra2engine
#else
namespace ra2engine {
void os_profiler_start(const char*) {}
void os_profiler_stop() {}
}  // namespace ra2engine
#endif
