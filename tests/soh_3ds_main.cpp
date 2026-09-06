// 3DS runtime configuration for Ship of Harkinian.
//
// main() itself comes from the decomp (soh/src/code/main.c). This translation
// unit only overrides libctru's weak configuration symbols, because the defaults
// are sized for small homebrew and SoH is not that.
//
// Kept separate from the decomp so the upstream diff stays minimal.

#include <3ds.h>

#include <unistd.h>
#include <sys/iosupport.h>
#include <cerrno>
#include <fcntl.h>
#include <cstdlib>

#include <atomic>
#include <exception>
#include <new>
#include <unwind.h>
#include <malloc.h>
#include <cstdio>

extern "C" {

// Default is 32 KB, which SoH overflows before main() is even reached: the
// enhancement layer's static initialisers (CosmeticsEditor.cpp builds large
// std::map/std::vector tables at load time) run on this stack, and the crash
// lands at 0x07FFDxxx, just under the 0x08000000 stack top.
//
// 1 MB also has to cover the decomp's own frames, which are generous - OoT was
// written against a 64 KB thread stack per N64 thread and several functions
// declare multi-kilobyte locals.
u32 __stacksize__ = 1 * 1024 * 1024;

// The linear heap backs every GPU-visible allocation: vertex buffers, texture
// staging, framebuffer readback. citro3d's command buffers live here too.
// Measured 2026-09-03 (Azahar, Kokiri shop then pause menu): fixed costs
// (2.25 MB vertex buffer, command buffers, gfx framebuffers, audio) leave about
// 4 MB of an 8 MB linear heap for textures, and the texture cache reached
// 3.99 MB with the pause menu open - free linear fell to 1 KiB and every later
// texture upload failed (287 dropped frames, invisible text boxes, flat room
// backgrounds). 16 MB doubles the texture headroom; the interpreter also evicts
// under linear pressure now, so this is margin, not the only guard.
u32 __ctru_linear_heap_size = 16 * 1024 * 1024;

// Everything else: the resource manager's 39k-entry index, the loaded scene
// graph, SoH's own N64 heaps (Heaps_Alloc), and the C++ runtime. New 3DS gives
// an APPLICATION region of 96 MB, and the binary itself is ~35 MB of that.
// Measured, not guessed: with a 36 MB heap the randomizer's hint tables ran out
// of memory. mallinfo at that point read used=30489 KiB, free=4846 KiB - so
// ~30 MB is already committed by the 39k-entry archive index, the resource
// manager and SoH's own tables before the hint tables are built at all.
//
// APPLICATION region on New 3DS is 96 MB and the binary is ~35 MB resident, so
// 48 heap + 12 linear + 1 stack = 61 MB is the space actually available.
// Building with -Os cut the resident binary from 34.7 MB to 21.2 MB, so there is
// now room to give the heap what SoH actually wants. 96 MB region - 21.2 MB
// binary leaves ~75 MB; 56 heap + 12 linear + 1 stack = 69 MB keeps a margin for
// the loader and thread stacks.
u32 __ctru_heap_size = 66 * 1024 * 1024;

// The fixed 66 MB request is sized to New 3DS's 96 MB APPLICATION region. On
// Old 3DS (64 MB region, most of it eaten by the ~21 MB binary) libctru's
// stock __system_allocateHeaps svcBreak-panics before main() - an
// undiagnosable black screen, the same wall MK64-3DS's O3DS users hit.
// Override it: same allocation mechanics, but clamp to what the kernel
// actually granted and record the outcome so the early-init constructor can
// show a legible "New 3DS required" screen instead.
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;
static u32 sGrantedHeapSize = 0;

void __system_allocateHeaps(void) {
    Handle reslimit = 0;
    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) {
        svcBreak(USERBREAK_PANIC);
    }
    s64 maxCommit = 0, currentCommit = 0;
    ResourceLimitType reslimitType = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&maxCommit, reslimit, &reslimitType, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &reslimitType, 1);
    svcCloseHandle(reslimit);

    u32 remaining = (u32)(maxCommit - currentCommit) & ~0xFFF;

    // Take what the console ACTUALLY grants, in both directions.
    //
    // This used to only ever clamp DOWN from the hardcoded 66 MB, so on a
    // New 3DS - whose APPLICATION region is far larger - the surplus was
    // simply left unused. Hardware evidence for why that matters: at the
    // ceiling a 135.2 KiB operator-new threw while the allocator reported
    // arena=64.98 MB against the 66 MB request, uordblks=56.31 MB,
    // fordblks=8.67 MB, keepcost=24 KB.
    //
    // What that proves: malloc failed with 8.67 MB free and keepcost showing
    // the top chunk had only 24 KB to give, so the arena could not extend and
    // no single free chunk satisfied the request. Headroom lets malloc extend
    // instead of failing while free space exists.
    //
    // What it does NOT prove, and an earlier note here got this wrong:
    // ordblks=276142 with an 8.67 MB total is an *average* free chunk of
    // ~33 bytes, and an average bounds nothing about the maximum. glibc-style
    // mallinfo exposes no largest-free-block field - keepcost is top-chunk
    // trim space, not max chunk. The contiguity claim rests on the failure
    // itself, not on the chunk census.
    //
    // The margin is slightly larger when growing: taking every last page of
    // commit leaves nothing for kernel bookkeeping or service shared
    // memory.
    const u32 margin = 2 * 1024 * 1024;
    u32 usable = remaining > (__ctru_linear_heap_size + margin) ? remaining - __ctru_linear_heap_size - margin : 0;
    usable &= ~0xFFF;
    if (usable != 0) {
        __ctru_heap_size = usable;
    }
    sGrantedHeapSize = __ctru_heap_size;

    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC,
                                  (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) {
        svcBreak(USERBREAK_PANIC);
    }
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR,
                                  (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) {
        svcBreak(USERBREAK_PANIC);
    }
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}

// std::thread (and everything built on it: SaveManager's BS::thread_pool, the
// LUS resource pool, the randomizer thread) reaches libctru through
// __syscall_thread_create, which substitutes a 32 KiB heap stack when the
// pthread attr carries none - std::thread always carries none. SaveManager's
// worker overflowed that during save creation (std::filesystem's _Dir deque +
// nlohmann; three identical data aborts at a prologue push, FAR just under sp).
// Linked with --wrap so every default-stack thread gets a real floor instead.
// Explicit pthread_attr_setstacksize values still pass through untouched.
int __real___syscall_thread_create(struct __pthread_t** thread, void* (*func)(void*), void* arg, void* stack_addr,
                                   size_t stack_size);
int __wrap___syscall_thread_create(struct __pthread_t** thread, void* (*func)(void*), void* arg, void* stack_addr,
                                   size_t stack_size) {
    // 128 KiB matches what the port already gives the audio thread; covers
    // filesystem recursion + json serialization with margin. ~4 such threads
    // exist at once, so the cost is ~512 KiB of the 66 MB heap.
    constexpr size_t kMinThreadStack = 128 * 1024;
    if (stack_addr == nullptr && stack_size < kMinThreadStack) {
        stack_size = kMinThreadStack;
    }
    return __real___syscall_thread_create(thread, func, arg, stack_addr, stack_size);
}

// Direct threadCreate callers bypass the pthread wrap above. The thinnest is
// libctru's own NDSP worker: 4 KiB stack, and its Thread_tag + stack + TLS
// share one ~4.5 KiB guard-free heap block - the whole thread state fits in a
// page, allocated amid boot churn. Two hardware dumps (11/13) fault on that
// thread with a prologue push just below sp. Floor small requests at 16 KiB;
// explicit larger sizes (audio 128 KiB) pass through.
static std::atomic<u32> sThreadBlocks[16] = {};
static std::atomic<unsigned> sThreadBlockCount{0};
typedef struct Thread_tag* Soh3dsWrapThread;
Soh3dsWrapThread __real_threadCreate(void (*entry)(void*), void* arg, size_t stack_size, int prio, int core_id,
                                     bool detached);
Soh3dsWrapThread __wrap_threadCreate(void (*entry)(void*), void* arg, size_t stack_size, int prio, int core_id,
                                     bool detached) {
    constexpr size_t kMinDirectStack = 16 * 1024;
    if (stack_size < kMinDirectStack) {
        stack_size = kMinDirectStack;
    }
    Soh3dsWrapThread t = __real_threadCreate(entry, arg, stack_size, prio, core_id, detached);
    if (t != NULL) {
        const unsigned slot = sThreadBlockCount.fetch_add(1, std::memory_order_relaxed);
        if (slot < 16) sThreadBlocks[slot].store((u32)t, std::memory_order_relaxed);
        // The Thread handle IS the block base ([Thread_tag | stack | TLS]).
        // Persisting block extents lets a crash dump's sp be matched to its
        // owning thread after the fact.
        fprintf(stderr, "soh-3ds thread: entry=%p block=%p size=%u prio=%d core=%d\n", (void*)entry, (void*)t,
                (unsigned)stack_size, prio, core_id);
        FILE* f = fopen("bootinfo.txt", "a");
        if (f != NULL) {
            fprintf(f, "thread entry=%p block=%p size=%u prio=%d core=%d\n", (void*)entry, (void*)t,
                    (unsigned)stack_size, prio, core_id);
            fclose(f);
        }
    }
    return t;
}

// Cross-archive C++ unwinding has to be PROVEN, not assumed: the exception
// containment strategy (LoadResource catch-all etc.) depends on a throw in
// libultraship.a unwinding into a catch in another object. Log the verdict
// once at boot.
void Soh3dsUnwindThrowTest(void);
static void SohCtrUnwindSelfTest() {
    try {
        Soh3dsUnwindThrowTest();
        fprintf(stderr, "soh-3ds unwind: test function returned?!\n");
    } catch (...) {
        fprintf(stderr, "soh-3ds unwind: cross-archive catch OK\n");
        return;
    }
}

} // extern "C"

// Crash-classification marker. Written from the FATAL path during heap
// exhaustion, so it MUST NOT allocate: the fd is opened once at boot and
// only write() is used here (no FILE buffer, no fprintf). Reading the file
// afterwards:
//   missing / empty      -> marker system failed; proves nothing
//   "armed" only         -> died WITHOUT reaching the FATAL handler (live)
//   + "fatal"            -> FATAL reached; died in the 5 s message window
//   + "teardown-entered" -> died at or after teardown entry (debris)
static int sFatalFd = -1;

static void SohCtrFatalMark(const char* stage, const char* detail = "") {
    if (sFatalFd < 0) {
        return;
    }
    char buf[192];
    int n = snprintf(buf, sizeof(buf), "%s tick=%llu %s\n", stage, (unsigned long long)svcGetSystemTick(), detail);
    if (n > 0) {
        // snprintf returns the length it WOULD have written; a long detail
        // string would otherwise make write() read past buf.
        size_t len = (size_t)n < sizeof(buf) - 1 ? (size_t)n : sizeof(buf) - 1;
        (void)write(sFatalFd, buf, len);
        (void)fsync(sFatalFd);
    }
}

extern "C" void Soh3dsLifecycleMark(const char* stage) {
    SohCtrFatalMark(stage);
    for (const auto& recorded : sThreadBlocks) {
        const u32 block = recorded.load(std::memory_order_relaxed);
        if (block == 0) continue;
        MemInfo memory = {};
        PageInfo page = {};
        const Result result = svcQueryMemory(&memory, &page, block);
        char detail[128];
        snprintf(detail, sizeof(detail), "block=%08lx rc=%08lx base=%08lx size=%08lx state=%lu perm=%lu",
                 (unsigned long)block, (unsigned long)result,
                 (unsigned long)memory.base_addr, (unsigned long)memory.size,
                 (unsigned long)memory.state, (unsigned long)memory.perm);
        SohCtrFatalMark("thread-map", detail);
    }
}

// Ordinary process exit previously left only "armed" in lastfatal.txt.
__attribute__((constructor(103))) static void SohCtrExitMarkInit() {
    std::atexit([] { SohCtrFatalMark("normal-exit"); });
}

// Backtrace sink that writes into the SD marker instead of stderr (stderr is
// invisible on hardware). Used by the new_handler, where the stack is still
// intact, so these frames name the real allocation site.
static _Unwind_Reason_Code SohCtrTraceFrameToMarker(struct _Unwind_Context* ctx, void* arg) {
    int* count = (int*)arg;
    if (*count >= 12) {
        return _URC_FAILURE;
    }
    char frame[32];
    snprintf(frame, sizeof(frame), "pc=%08lx", (unsigned long)_Unwind_GetIP(ctx));
    SohCtrFatalMark("alloc-frame", frame);
    ++*count;
    return _URC_NO_REASON;
}

// Route stderr through svcOutputDebugString before anything logs.
//
// SoH logs through spdlog, whose default sink is stdout - invisible to an
// emulator. consoleDebugInit(debugDevice_SVC) redirects stderr to
// svcOutputDebugString, which Azahar records in its log, so the game's own
// startup diagnostics become readable.
//
// constructor(101) runs before the normal static initialisers (which start at
// 65535/default), so this is in place before the enhancement layer's
// load-time tables are built and before spdlog's first message.
__attribute__((constructor(101))) static void SohCtrEarlyLogInit() {
    consoleDebugInit(debugDevice_SVC);
    // Unbuffered: a crash must not swallow the lines leading up to it.
    setvbuf(stderr, nullptr, _IONBF, 0);
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Pin the working directory. Every LUS path is cwd-relative on this port
    // (NON_PORTABLE=OFF resolves the app directory to "."): under hbmenu the
    // cwd happens to be the .3dsx's directory, but a CIA launch has none, so
    // oot.o2r was never found and boot null-dereferenced in OTRMessage_Init
    // (FAULTS.md would call this the F3 that never got a number). One chdir
    // makes both launch paths identical - and puts the spdlog file sink at
    // sdmc:/3ds/soh/logs/SoH-3DS.log where FTP can reach it.
    // First launch must work on a blank SD. Create the data directory before
    // pinning cwd; the native setup screen will install its archives there.
    if ((mkdir("sdmc:/3ds", 0777) != 0 && errno != EEXIST) ||
        (mkdir("sdmc:/3ds/soh", 0777) != 0 && errno != EEXIST) ||
        chdir("sdmc:/3ds/soh") != 0) {
        fprintf(stderr, "soh-3ds: FATAL: could not create/use sdmc:/3ds/soh (errno=%d). Exiting.\n", errno);
        gfxInitDefault();
        consoleInit(GFX_TOP, nullptr);
        printf("\n\n  Ship of Harkinian 3DS\n  ---------------------\n\n"
               "  The app could not create or open:\n\n"
               "       sd:/3ds/soh\n\n"
               "  Check that the SD card is writable and\n"
               "  that no file is using that name.\n\n"
               "  Press START to exit.\n");
        gfxFlushBuffers();
        gfxSwapBuffers();
        while (aptMainLoop()) {
            hidScanInput();
            if ((hidKeysDown() & KEY_START) != 0) break;
            gspWaitForVBlank();
        }
        gfxExit();
        exit(EXIT_FAILURE);
    }

    // Old 3DS (or a stingy exheader): __system_allocateHeaps clamped the heap.
    // Below ~40 MB SoH cannot even finish boot (30 MB is committed by the
    // archive index and tables alone, measured), so dying later in a random
    // allocation would be misleading. Say why, on-screen, and leave.
    constexpr u32 kViableHeapFloor = 40 * 1024 * 1024;
    if (sGrantedHeapSize < kViableHeapFloor) {
        fprintf(stderr, "soh-3ds: FATAL: only %u MiB heap granted (need >= %u). New 3DS required.\n",
                (unsigned)(sGrantedHeapSize / (1024 * 1024)), (unsigned)(kViableHeapFloor / (1024 * 1024)));
        gfxInitDefault();
        consoleInit(GFX_TOP, nullptr);
        printf("\n\n  Ship of Harkinian 3DS\n  ---------------------\n\n"
               "  This system granted only %u MiB of memory.\n"
               "  A New 3DS / New 2DS (with its extra RAM)\n  is required.\n\n"
               "  If this IS a New 3DS, launch via a title\n  with a New-3DS exheader (install the CIA).\n",
               (unsigned)(sGrantedHeapSize / (1024 * 1024)));
        gfxFlushBuffers();
        gfxSwapBuffers();
        svcSleepThread(8'000'000'000LL);
        gfxExit();
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "soh-3ds: early log init, stack %u KiB, linear %u MiB, heap %u MiB (granted %u MiB)\n",
            (unsigned)(__stacksize__ / 1024), (unsigned)(__ctru_linear_heap_size / (1024 * 1024)),
            (unsigned)(__ctru_heap_size / (1024 * 1024)), (unsigned)(sGrantedHeapSize / (1024 * 1024)));

    // Memory-map ground truth: repeated hardware dumps fault at addresses the
    // emulator map says are mapped. Walk the address space once and PERSIST it
    // to SD (stderr is invisible on hardware): after one boot,
    // sdmc:/3ds/soh/bootinfo.txt holds the console's real map, FTP-readable.
    {
        FILE* info = fopen("bootinfo.txt", "w");
        // SoH-3DS: crash-classification marker, ARMED HERE at boot. It is
        // written from the FATAL path while the heap is exhausted, so it
        // must never allocate: the fd is opened now and held open for the
        // process lifetime, and the fatal path only calls write(). The
        // "armed" line is the capability check - if it is missing, the
        // marker system itself failed and the file proves NOTHING about the
        // crash. See SohCtrFatalMark().
        sFatalFd = open("lastfatal.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        SohCtrFatalMark("armed");
        u32 addr = 0x00100000;
        int regions = 0;
        if (info != NULL) {
            fprintf(info, "granted heap=%u MiB (requested %u), linear=%u MiB, main stack=%u KiB\n",
                    (unsigned)(sGrantedHeapSize / (1024 * 1024)), 66u,
                    (unsigned)(__ctru_linear_heap_size / (1024 * 1024)), (unsigned)(__stacksize__ / 1024));
        }
        while (addr < 0x40000000 && regions < 40) {
            MemInfo mem;
            PageInfo page;
            if (R_FAILED(svcQueryMemory(&mem, &page, addr))) {
                break;
            }
            if (mem.state != MEMSTATE_FREE) {
                fprintf(stderr, "soh-3ds map: %08lx-%08lx state=%u perm=%u\n", (unsigned long)mem.base_addr,
                        (unsigned long)(mem.base_addr + mem.size), (unsigned)mem.state, (unsigned)mem.perm);
                if (info != NULL) {
                    fprintf(info, "map %08lx-%08lx state=%u perm=%u\n", (unsigned long)mem.base_addr,
                            (unsigned long)(mem.base_addr + mem.size), (unsigned)mem.state, (unsigned)mem.perm);
                }
                ++regions;
            }
            u32 next = mem.base_addr + mem.size;
            if (next <= addr) {
                break;
            }
            addr = next;
        }
        if (info != NULL) {
            fclose(info);
        }
    }

    // Name the FAILING ALLOCATION on hardware, with no debugger. A
    // std::bad_alloc backtrace taken from the terminate handler is useless -
    // the stack has already unwound, and libstdc++'s cantunwind barrier caps
    // it at two frames. new_handler runs INSIDE the failing operator new
    // with the stack fully intact, so this is the one place the real
    // allocation site is recoverable. It fires once (then clears itself, so
    // the next failure throws normally and cannot loop), and writes through
    // the allocation-free marker.
    std::set_new_handler([] {
        std::set_new_handler(nullptr); // next failure throws instead of looping
        // Allocator state AT THE FAILURE, not from a heartbeat sampled
        // earlier. ordblks (free-chunk count) is the fragmentation witness:
        // a large fordblks split across many ordblks, with a modest request
        // failing, is fragmentation. A small fordblks instead means a real
        // arena limit. mallinfo() walks existing structures and allocates
        // nothing, so it is safe on this path.
        struct mallinfo mi = mallinfo();
        char detail[176];
        snprintf(detail, sizeof(detail), "uordblks=%u fordblks=%u ordblks=%u keepcost=%u arena=%u",
                 (unsigned)mi.uordblks, (unsigned)mi.fordblks, (unsigned)mi.ordblks, (unsigned)mi.keepcost,
                 (unsigned)mi.arena);
        SohCtrFatalMark("alloc-fail", detail);
        int frames = 0;
        _Unwind_Backtrace(SohCtrTraceFrameToMarker, &frames);
    });

    // Gated: an intentional throw at boot would abort EVERY boot if the
    // unwind hypothesis fails. Runs only when the operator plants the flag
    // file (emulator sdmc or SD card); absent on normal installs.
    if (FILE* f = fopen("unwindtest.flag", "r")) {
        fclose(f);
        SohCtrUnwindSelfTest();
    }
}

// Uncaught exceptions otherwise die as an opaque abort() with no Luma frame
// worth reading. Name the exception on stderr (SVC log) and the bottom-screen
// console, then give the user a beat to photograph it. Registered from the
// same early constructor path as the log init.
#include <exception>
#include <unwind.h>

// Partial telemetry only: measured on a real bad_alloc, this walk captures
// exactly two frames (the lambda and __cxxabiv1::__terminate) and then stops
// at libstdc++'s cantunwind barrier - it CANNOT see the throw site. Kept
// because other terminate classes may unwind further and two frames still
// beat none. Locating a real throw site requires hardware GDB (abort trap or
// break __cxa_throw): it reads the raw stack and needs no unwind tables.
static _Unwind_Reason_Code SohCtrTraceFrame(struct _Unwind_Context* ctx, void* arg) {
    int* count = (int*)arg;
    if (*count >= 24) {
        return _URC_FAILURE;
    }
    fprintf(stderr, "soh-3ds terminate bt[%02d]: %08lx\n", *count, (unsigned long)_Unwind_GetIP(ctx));
    ++*count;
    return _URC_NO_REASON;
}

// Weak: links only when the NDSP player is in the binary. Must be declared
// at file scope with C linkage to match the extern "C" definition.
extern "C" void Soh3dsAudioQuiesce(void) __attribute__((weak));

__attribute__((constructor(102))) static void SohCtrTerminateInit() {
    std::set_terminate([] {
        const char* what = "(unknown)";
        if (auto e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& ex) {
                what = ex.what();
            } catch (...) {
            }
        }
        fprintf(stderr, "soh-3ds: FATAL uncaught exception: %s\n", what);
        // Allocation-free: fopen needs the heap we just ran out of.
        SohCtrFatalMark("fatal", what);
        int frames = 0;
        _Unwind_Backtrace(SohCtrTraceFrame, &frames);
        // Join libctru's NDSP worker before teardown. Proven necessary in
        // Azahar: FATAL at t=170.69 s, then unmapped writes to the NDSP
        // worker's stack at t=175.71 s - exactly this handler's 5 s sleep.
        if (Soh3dsAudioQuiesce != nullptr) {
            Soh3dsAudioQuiesce();
        }
        svcSleepThread(5'000'000'000LL); // 5 s to read the bottom screen
        // Second mark: distinguishes "died during the sleep" from "died in
        // teardown". Present => the dump was taken at or after teardown entry.
        SohCtrFatalMark("teardown-entered");
        abort();
    });
}

// Boot-status console. Boot spends minutes indexing the o2r and precaching
// audio with both screens dark, which is indistinguishable from a hang on
// hardware. The window backend calls Soh3dsBootConsoleReady() once gfxInit has
// run; from then on every SOH3DS_INIT_TRACE breadcrumb is also printed on the
// bottom screen. The renderer never draws the bottom screen on this port, so
// the text simply stays.
extern "C" {

static bool sBootConsoleUp = false;
void Soh3dsRestoreConsoleOutput(void);

void Soh3dsBootConsoleReady(void) {
    if (!sBootConsoleUp) {
        consoleInit(GFX_BOTTOM, nullptr);
        // consoleInit installs the console devoptab only on its first process
        // call. Native setup consumed that call and later routed stdout back
        // to SVC, so explicitly restore the saved console output here.
        Soh3dsRestoreConsoleOutput();
        // consoleInit redirects BOTH stdout and stderr to the screen; put
        // stderr back on svcOutputDebugString or every later diagnostic
        // (init traces, gfx heartbeat, crash breadcrumbs) vanishes from the
        // emulator log. stdout stays on the console for the boot text.
        consoleDebugInit(debugDevice_SVC);
        setvbuf(stderr, nullptr, _IONBF, 0);
        sBootConsoleUp = true;
        printf("Ship of Harkinian 3DS\n---------------------\n");
    }
}

void Soh3dsBootStatus(const char* msg) {
    fprintf(stderr, "soh-3ds init: %s\n", msg);
    if (sBootConsoleUp) {
        printf("%s\n", msg);
        gfxFlushBuffers();
    }
}

// Dual screen: the renderer takes the bottom LCD on its first bottom-screen
// draw. Stop printing there (the console renders glyphs straight into the
// GSP framebuffer the GPU now transfers into) and send stdout where stderr
// already goes, so stray printf output cannot draw over the UI.
void Soh3dsBootConsoleRelease(void) {
    if (sBootConsoleUp) {
        sBootConsoleUp = false;
        devoptab_list[STD_OUT] = devoptab_list[STD_ERR];
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
}

// SoH-3DS DIAGNOSTIC: manual ISG state tracer. Prints every 0<->nonzero
// transition of Player.meleeWeaponState on the bottom screen, because on
// hardware there is no log to read. Reading it: a normal sword swing prints
// "set" then "clear" as a pair. A successful ISG prints "set" with no
// following "clear" - the state is stuck on, which IS the glitch. An attempt
// that never prints "set" at all means the crouch stab never armed the
// collider, i.e. the input never landed, which is a different problem from
// an interrupt that fails to persist.
// Gated on sdmc:/3ds/soh/isg.flag (cwd is that directory); absent = inert.
static int Soh3dsProbeArmed(void) {
    static int armed = -1;
    if (armed < 0) {
        FILE* flag = fopen("isg.flag", "r");
        armed = (flag != NULL) ? 1 : 0;
        if (flag != NULL) {
            fclose(flag);
        }
    }
    return armed;
}

// Companion probe: the N64 button mask the game ACTUALLY receives on this
// console. Needed because the button mapping lives in shipofharkinian.json on
// the console's own SD, which cannot be assumed to match any other profile -
// if R and B never arrive together here, no amount of player timing can arm a
// crouch stab and the ISG question never gets as far as the state machine.
void Soh3dsPadProbe(unsigned int button, int stickX, int stickY) {
    static unsigned int sLastMask = 0xFFFFFFFFu;

    if (Soh3dsProbeArmed() == 0) {
        return;
    }
    // Emit on BUTTON change only. The caller's change check also trips on
    // stick_x/stick_y, which move every frame while walking - printing (and
    // flushing) per frame would both flood the console and perturb the very
    // frame timing this probe exists to observe. The stick value is still
    // shown, just sampled at the moment a button changed.
    if (button == sLastMask) {
        return;
    }
    sLastMask = button;

    char line[96];
    // Decode only the buttons a crouch stab needs, so the line stays readable.
    std::snprintf(line, sizeof(line), "pad %04X %s%s%s%s%s (%d,%d)", button, (button & 0x8000) ? "A" : "",
                  (button & 0x4000) ? "B" : "", (button & 0x2000) ? "Z" : "", (button & 0x0020) ? "L" : "",
                  (button & 0x0010) ? "R" : "", stickX, stickY);
    Soh3dsBootStatus(line);
}

void Soh3dsIsgProbe(int newState, int oldState, unsigned long pc) {
    static unsigned sets = 0;
    static unsigned clears = 0;

    if (Soh3dsProbeArmed() == 0) {
        return;
    }

    char line[96];
    if (newState != 0) {
        ++sets;
        std::snprintf(line, sizeof(line), "isg: SET   mws=%d  sets=%u clears=%u", newState, sets, clears);
    } else {
        ++clears;
        // pc is the return address inside z_player.c that cleared it - one of
        // the 10 func_80832318 call sites, which names the interrupting action.
        std::snprintf(line, sizeof(line), "isg: clear from=%08lx (was %d) c=%u", pc, oldState, clears);
    }
    Soh3dsBootStatus(line);
}

} // extern "C"
