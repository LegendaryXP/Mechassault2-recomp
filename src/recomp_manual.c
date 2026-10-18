/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>
#include <string.h>

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

/* Thread-local in the runtime: a plain extern would bind to the TLS template
 * rather than the calling thread's copy (see main.c). */
extern __declspec(thread) uint32_t g_eax;
extern ptrdiff_t g_xbox_mem_offset;

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
/* ── MechAssault 2: D3D8LTCG fence wait (D3D_BlockOnTime) ──── */

/*
 * sub_0040F510(fence, flags), stdcall, ret 8. Device pointer at
 * MEM32(0x0041BC68); it spins until the GPU semaphore *(device + 0x30)
 * catches up with the fence. With RECOMP_PB_EXEC the executor writes
 * semaphore releases (method 0x1D70) as it finishes work, but only to the
 * address it was told about, and nothing told it: the first display-mode
 * switch after the loading screen (sub_0040ECF0) waited here forever.
 *
 * Re-armed on every call because D3D rewrites the field on device reset
 * (docs/technical/d3d8ltcg-device-context.md). The original body still does
 * the kickoff and the wait.
 */
#define MA2_D3D_DEVICE_PTR 0x0041BC68u
#define MA2_MEM32(va) (*(volatile uint32_t *)((uint8_t *)g_xbox_mem_offset + (va)))

extern void sub_0040F510_gen(void);
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);

extern volatile uint32_t *xbox_Nv2aRegPtr(uint32_t offset);
extern __declspec(thread) uint32_t g_esp;
#include <windows.h>   /* Sleep */

/* Has the GPU semaphore passed this fence? D3D's own test (0x0040F51C):
 * seq - fence >= seq - sem, all unsigned. */
static int ma2_fence_done(uint32_t dev, uint32_t sem_va, uint32_t fence)
{
    uint32_t seq = MA2_MEM32(dev + 0x2C);
    return (uint32_t)(seq - fence) >= (uint32_t)(seq - MA2_MEM32(sem_va));
}

void sub_0040F510(void)
{
    static uint32_t armed_sem;
    static int forced;
    uint32_t dev = MA2_MEM32(MA2_D3D_DEVICE_PTR);

    if (dev) {
        uint32_t sem = MA2_MEM32(dev + 0x30);
        if (sem && sem != armed_sem) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X (device 0x%08X)\n",
                    sem, dev);
            armed_sem = sem;
        }

        /* An idle "GPU" that has not reached the fence will never reach it.
         *
         * D3D writes a fence's semaphore release into the pushbuffer but only
         * kicks it off later; a wait on such a fence -- the GUI movie locking
         * its texture after the title screen -- finds DMA_PUT == DMA_GET,
         * nothing in flight, and the semaphore two behind for ever. Give the
         * executor a moment, then report what an idle GPU means: everything
         * up to this fence is done. The latch at PGRAPH 0x400B10 must carry
         * the same fence bits or the next wait (sub_0040F4A0) spins instead. */
        if (sem && !getenv("MA2_NO_IDLE_FENCE")) {
            uint32_t fence = MA2_MEM32(g_esp + 4);   /* [esp] is the return address */
            volatile uint32_t *put = xbox_Nv2aRegPtr(0x800040u);
            volatile uint32_t *get = xbox_Nv2aRegPtr(0x800044u);
            volatile uint32_t *latch = xbox_Nv2aRegPtr(0x400B10u);
            int ms, idle_ms = 0;

            /* Wait here rather than in the lifted loop, which never looks
             * again once the executor has gone idle: up to 2 s, forcing the
             * fence once the GPU has had nothing to do for 20 ms. */
            /* Most waits end within microseconds once the executor reaches
             * the release, so spin briefly before paying for a sleep. */
            {
                int spin;
                for (spin = 0; spin < 2000 && !ma2_fence_done(dev, sem, fence); spin++)
                    YieldProcessor();
                for (spin = 0; spin < 200 && !ma2_fence_done(dev, sem, fence); spin++)
                    SwitchToThread();
            }
            for (ms = 0; ms < 2000 && !ma2_fence_done(dev, sem, fence); ms++) {
                idle_ms = (put && get && *put == *get) ? idle_ms + 1 : 0;
                if (idle_ms >= 20)
                    break;
                Sleep(1);
            }
            if (!ma2_fence_done(dev, sem, fence)) {
                if (forced++ < 16)
                    fprintf(stderr, "[MA2] %s GPU behind fence 0x%08X "
                            "(sem 0x%08X, seq 0x%08X): marking it reached\n",
                            idle_ms >= 20 ? "idle" : "stuck", fence,
                            MA2_MEM32(sem), MA2_MEM32(dev + 0x2C));
                MA2_MEM32(sem) = fence;
                if (latch)
                    *latch = (*latch & ~0x7Cu) | ((fence & 0x1Fu) << 2);
            }
        }
    }
    sub_0040F510_gen();
}

/* ── MechAssault 2: D3D write-combine flush (sub_0040F980) ──── */

/*
 * XDK D3D before every kick: sfence, set bit 16 of PFB register 0x100410,
 * spin until the hardware clears it -- a flush of the CPU's write-combining
 * buffers, which the GPU acknowledges at once. Here guest memory is already
 * coherent, but the bit is only cleared by the emulated GPU between
 * pushbuffer commands, so every kick waited for the executor to catch up and
 * the title and the GPU never overlapped (22% of the main thread spun here).
 * Do what the original leaves behind -- eax the register base, ecx the
 * register with the flush bit -- without the wait. Takes no arguments.
 * MA2_SLOW_WC_FLUSH=1 runs the original.
 */
extern void sub_0040F980_gen(void);
extern __declspec(thread) uint32_t g_ecx;

void sub_0040F980(void)
{
    static int slow = -1;
    uint32_t dev, base;

    if (slow < 0)
        slow = getenv("MA2_SLOW_WC_FLUSH") != NULL;
    if (slow) {
        sub_0040F980_gen();
        return;
    }
    dev = MA2_MEM32(MA2_D3D_DEVICE_PTR);
    base = dev ? MA2_MEM32(dev + 0x1C28) : 0;
    g_eax = base;
    g_ecx = base ? (MA2_MEM32(base + 0x100410) | 0x10000u) : 0x10000u;
    g_esp += 4;                         /* ret */
}

/* ── MechAssault 2: XOnline "go to the Xbox Live dashboard" ─── */

/*
 * sub_00366DBC: XOnline export thunk (ecx = XOnline context, jmp
 * sub_0036BFD0), stdcall with one argument, ret 4. The game's leave helper
 * sub_0023EABB calls it with mode 0 to reboot into xonlinedash.xbe
 * (\Device\Harddisk0\partition2\XODash or the disc's XODASH), queueing a
 * relaunch of itself with launch data {1,0,1} first. There is no online
 * dashboard to run, and right after the intro the game took this path and
 * rebooted forever. Answer the way XOnline does when the launch cannot
 * happen, and let the title carry on offline.
 *
 * MA2_ALLOW_XODASH=1 restores the original behaviour.
 */
extern void sub_00366DBC_gen(void);
extern __declspec(thread) uint32_t g_esp;

void sub_00366DBC(void)
{
    static int hits;
    if (getenv("MA2_ALLOW_XODASH")) {
        sub_00366DBC_gen();
        return;
    }
    if (hits++ < 8)
        fprintf(stderr, "[MA2] XOnline dashboard launch refused (#%d)\n", hits);
    g_esp += 8;               /* return address + the one stdcall argument */
    g_eax = 0x80150005u;      /* the code sub_0036BFD0 itself returns on failure */
}

/* ── Bring-up switch: MA2_SKIP_ICALL ───────────────────────── */

/*
 * MA2_SKIP_ICALL=0x00420BDA,0x00310954 makes indirect calls to those VAs
 * behave exactly like an unresolved ICALL (pop the return address, eax = 0),
 * i.e. like the build before they were seeded. Bisects which newly seeded
 * function changes behaviour without a 30-minute regenerate per step.
 */
#include <stdlib.h>
#include <string.h>

extern __declspec(thread) uint32_t g_esp;

static void ma2_skipped_icall(void)
{
    g_esp += 4;
    g_eax = 0;
}

static int ma2_is_skipped(uint32_t va)
{
    static uint32_t list[32];
    static int count = -1;
    if (count < 0) {
        const char *spec = getenv("MA2_SKIP_ICALL");
        count = 0;
        if (spec) {
            char buf[512], *tok, *ctx = NULL;
            strncpy(buf, spec, sizeof buf - 1);
            buf[sizeof buf - 1] = 0;
            for (tok = strtok_s(buf, ",", &ctx); tok && count < 32;
                 tok = strtok_s(NULL, ",", &ctx))
                list[count++] = (uint32_t)strtoul(tok, NULL, 0);
            fprintf(stderr, "[MA2] skipping %d indirect-call target(s)\n", count);
        }
    }
    for (int i = 0; i < count; i++)
        if (list[i] == va)
            return 1;
    return 0;
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    if (ma2_is_skipped(xbox_va))
        return ma2_skipped_icall;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    /* MA2 bring-up: on the first sighting of a non-null wild target, show
     * what led here -- the recent targets name the methods that ran, ecx is
     * usually the object whose vtable held the garbage. */
    if (hits[i] == 1) {
        extern __declspec(thread) uint32_t g_ecx, g_esi, g_edi, g_ebx;
        uint32_t k, obj = g_ecx;
        fprintf(stderr, "  recent targets:");
        for (k = 0; k < 16; k++)
            fprintf(stderr, " %08X", g_icall_trace[(g_icall_trace_idx + k) & 15]);
        fprintf(stderr, "\n  ecx=%08X esi=%08X edi=%08X ebx=%08X esp=%08X\n",
                g_ecx, g_esi, g_edi, g_ebx, g_esp);
        if (obj >= 0x10000 && obj < 0x04000000) {
            const uint8_t *p = (const uint8_t *)g_xbox_mem_offset + obj;
            fprintf(stderr, "  [ecx] bytes:");
            for (k = 0; k < 48; k++)
                fprintf(stderr, "%s%02X", (k % 16) ? " " : "\n    ", p[k]);
            fprintf(stderr, "\n  [ecx] text: \"");
            for (k = 0; k < 48; k++)
                fputc(p[k] >= 0x20 && p[k] < 0x7F ? p[k] : '.', stderr);
            fprintf(stderr, "\"\n");
        }
        {
            const uint8_t *s = (const uint8_t *)g_xbox_mem_offset;
            fprintf(stderr, "  guest stack:");
            for (k = 0; k < 24; k++)
                fprintf(stderr, " %08X", *(const uint32_t *)(s + g_esp + k * 4));
            fprintf(stderr, "\n");
        }
    }
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
