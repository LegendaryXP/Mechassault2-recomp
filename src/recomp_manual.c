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
extern void sub_0040F220(void);        /* D3D InsertFence(flags) */
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

/* RECOMP_WAIT_PROF=1: time spent in D3D's fence wait, per two seconds,
 * beside the kernel's own [WAITS] lines (this wait spins and sleeps in host
 * code, not through a kernel call). */
static void ma2_fence_prof(LONGLONG t0)
{
    static int on = -1;
    static LARGE_INTEGER f;
    static LONGLONG acc;
    static uint32_t n;
    static DWORD start;
    LARGE_INTEGER t1;
    DWORD now;

    if (on < 0) {
        on = getenv("RECOMP_WAIT_PROF") != NULL;
        QueryPerformanceFrequency(&f);
    }
    if (!on)
        return;
    QueryPerformanceCounter(&t1);
    acc += t1.QuadPart - t0;
    n++;
    now = GetTickCount();
    if (!start)
        start = now;
    if (now - start >= 2000) {
        fprintf(stderr, "  [WAITS] D3D fence waits (sub_0040F510): %u in %.1f s, %.1f ms\n",
                n, (now - start) / 1000.0, (double)acc * 1000.0 / (double)f.QuadPart);
        fflush(stderr);
        acc = 0;
        n = 0;
        start = now;
    }
}

void sub_0040F510(void)
{
    static uint32_t armed_sem;
    static int forced;
    uint32_t dev = MA2_MEM32(MA2_D3D_DEVICE_PTR);
    LARGE_INTEGER fence_t0;

    QueryPerformanceCounter(&fence_t0);

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

            /* fence == seq: a fence D3D has not inserted yet. The original
             * inserts and kicks it at 0x0040F538 (InsertFence(0): the
             * semaphore release with value seq, seq += 2, KickOff); waiting
             * before that could only end by the idle timeout below, ~20 ms at
             * every load. Insert it the same way first, so the wait is for a
             * release that is in flight. stdcall, ret 4; eax/ecx/edx are
             * clobbered, as in the original. */
            if (fence == MA2_MEM32(dev + 0x2C) && !ma2_fence_done(dev, sem, fence)) {
                g_esp -= 4; MA2_MEM32(g_esp) = 0;
                g_esp -= 4; MA2_MEM32(g_esp) = 0x0040F53Fu;
                sub_0040F220();
            }

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
    ma2_fence_prof(fence_t0.QuadPart);
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

/* ── MechAssault 2: the mouse turning the walker (sub_0006C790) ──────── */

/*
 * sub_0006C790(rate), thiscall, ret 4: a physics body's yaw-rate setter
 * (this+0x34; the body object is this - 0x40). Every body in the game calls
 * it; the return address says who called, and the pilot's two stick turns
 * are the mouse's to steer: afterwards it turns the heading itself
 * (src/ma2_mouse_look.c).
 */
#include <string.h>   /* memcpy */
extern void sub_0006C790_gen(void);
void ma2_mouse_look_rate_set(uint32_t ret, uint32_t comp, float rate);

void sub_0006C790(void)
{
    uint32_t comp = g_ecx;
    uint32_t ret = MA2_MEM32(g_esp);        /* [esp] is the return address */
    uint32_t rv = MA2_MEM32(g_esp + 4);
    float rate;
    memcpy(&rate, &rv, sizeof rate);
    sub_0006C790_gen();
    ma2_mouse_look_rate_set(ret, comp, rate);
}

/* ── MechAssault 2: the mouse tilting the camera (sub_00120EBD) ──────── */

/*
 * sub_00120EBD(input), thiscall, ret 4: a walker's look pitch, integrated:
 * this+0x144 += input * this+0x140 * dt, then clamped to [this+0x13C,
 * this+0x138]. The human pilot's call (returns to 0x00175CEF) feeds it the
 * smoothed Look y; afterwards the mouse tilts the pitch itself
 * (src/ma2_mouse_look.c).
 */
extern void sub_00120EBD_gen(void);
void ma2_mouse_look_pitch(uint32_t ret, uint32_t walker, float input);

void sub_00120EBD(void)
{
    uint32_t walker = g_ecx;
    uint32_t ret = MA2_MEM32(g_esp);
    uint32_t iv = MA2_MEM32(g_esp + 4);
    float input;
    memcpy(&input, &iv, sizeof input);
    sub_00120EBD_gen();
    ma2_mouse_look_pitch(ret, walker, input);
}

/* ── MechAssault 2: the walker's ground probe (sub_001760E1) ─────────── */

/*
 * sub_001760E1(pos, reset, out), thiscall, ret 0xC: a walker's ground height
 * under pos, max(terrain, a downward ray against the level's objects), on the
 * FPU stack. The ray is sub_0008DF50(grid, query, result), stdcall, al = hit.
 * Bring-up: MA2_GROUND_TRACE logs the player's (src/ma2_ground.c).
 */
extern __declspec(thread) double g_fp_stack[8];
extern __declspec(thread) int g_fp_top;
extern void sub_001760E1_gen(void);
extern void sub_0008DF50_gen(void);
void ma2_ground_probe_enter(uint32_t body, uint32_t pos);
void ma2_ground_probe_leave(uint32_t body, double h);
void ma2_ground_ray_leave(uint32_t grid, uint32_t query, uint32_t result, int hit);

void sub_001760E1(void)
{
    uint32_t body = g_ecx;
    ma2_ground_probe_enter(body, MA2_MEM32(g_esp + 4));
    sub_001760E1_gen();
    ma2_ground_probe_leave(body, g_fp_stack[g_fp_top & 7]);
}

/* The ray's walk down a candidate's scene graph: sub_00083070 (vt+0x68,
 * thiscall node, query, result) recurses into the children; sub_000831E0
 * (ecx node, ebx query, [esp+4] -> "descend" byte) tests the node itself. */
extern __declspec(thread) uint32_t g_ebx;
extern void sub_00083070_gen(void);
extern void sub_000831E0_gen(void);
void ma2_ground_node(int leave, int test, uint32_t node, uint32_t query, uint32_t result,
                     int hit, int descend);

void sub_00083070(void)
{
    uint32_t node = g_ecx, query = MA2_MEM32(g_esp + 4), result = MA2_MEM32(g_esp + 8);
    ma2_ground_node(0, 0, node, query, result, 0, 0);
    sub_00083070_gen();
    ma2_ground_node(1, 0, node, query, result, g_eax & 0xFF, 0);
}

void sub_000831E0(void)
{
    uint32_t node = g_ecx, query = g_ebx, out = MA2_MEM32(g_esp + 4);
    ma2_ground_node(0, 1, node, query, g_eax, 0, 0);
    sub_000831E0_gen();
    ma2_ground_node(1, 1, node, query, 0, g_eax & 0xFF, MA2_MEM32(out) & 0xFF);
}

void sub_0008DF50(void)
{
    uint32_t grid = MA2_MEM32(g_esp + 4);
    uint32_t query = MA2_MEM32(g_esp + 8);
    uint32_t result = MA2_MEM32(g_esp + 0xC);
    sub_0008DF50_gen();
    ma2_ground_ray_leave(grid, query, result, g_eax & 0xFF);
}

/* ── MechAssault 2: mission music actions (sub_0014D9CC) ──────────────── */

/*
 * sub_0014D9CC, thiscall: a mission script's "Action - Music"
 * (MAActionMusic) firing. this+0x68 is its MusicEvent, this+0x60 its
 * MusicCue, this+0x6C its IntensityLevel; the six dwords at 0x0050C91C are
 * the music state every music action shares (current and fallback track).
 * Bring-up: MA2_MUSIC_TRACE=1 logs each one, with that state before and
 * after.
 */
extern void sub_0014D9CC_gen(void);

static void ma2_music_state(const char *tag, uint32_t self)
{
    uint32_t k;
    fprintf(stderr, "[MUSIC] %lu %s action %08X event %u cue %08X level %u sound %08X state",
            (unsigned long)GetTickCount(), tag, self, MA2_MEM32(self + 0x68),
            MA2_MEM32(self + 0x60), MA2_MEM32(self + 0x6C), MA2_MEM32(self + 0x74));
    for (k = 0; k < 6; k++)
        fprintf(stderr, " %08X", MA2_MEM32(0x0050C91Cu + 4 * k));
    fprintf(stderr, "\n");
    {
        /* The playing sound (this+0x74), raw: its vtable and fields. */
        uint32_t snd = MA2_MEM32(self + 0x74);
        if (snd >= 0x10000u && snd < 0x04000000u) {
            uint32_t ci = MA2_MEM32(snd + 0x18), pl;
            fprintf(stderr, "[MUSIC]   sound %08X:", snd);
            for (k = 0; k < 0x40; k += 4)
                fprintf(stderr, " %08X", MA2_MEM32(snd + k));
            fprintf(stderr, "\n");
            /* Its cue info (sound+0x18) and the player that runs it (cue
             * info+0x38). */
            if (ci >= 0x10000u && ci < 0x04000000u) {
                fprintf(stderr, "[MUSIC]   cueinfo %08X:", ci);
                for (k = 0; k < 0x60; k += 4)
                    fprintf(stderr, " %08X", MA2_MEM32(ci + k));
                fprintf(stderr, "\n");
                pl = MA2_MEM32(ci + 0x38);
                if (pl >= 0x10000u && pl < 0x04000000u) {
                    fprintf(stderr, "[MUSIC]   player %08X:", pl);
                    for (k = 0; k < 0x60; k += 4)
                        fprintf(stderr, " %08X", MA2_MEM32(pl + k));
                    fprintf(stderr, "\n");
                }
            }
        }
    }
}

void sub_0014D9CC(void)
{
    static int on = -1;
    uint32_t self = g_ecx;
    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (on)
        ma2_music_state("fire ", self);
    sub_0014D9CC_gen();
    if (on)
        ma2_music_state("after", self);
}

/* ── MechAssault 2: the XACT cue state machine (bring-up) ─────────────── */

/*
 * Three XACT engine functions an interactive cue runs on: sub_003DFCDF (eax
 * = cue) is its per-update state machine, driven by the flags at cue+0x38;
 * sub_003E0C44 (edi = cue) picks and starts a new variation;
 * sub_003E0587(cue, entry, flag), stdcall, starts the sound of a variation
 * table entry. Under MA2_MUSIC_TRACE=1 src/ma2_music.c logs them.
 */
extern void sub_003DFCDF_gen(void);
extern void sub_003E0587_gen(void);
extern void sub_003E0C44_gen(void);
void ma2_xact_cue(int which, int leave, uint32_t cue, uint32_t a, uint32_t b, uint32_t ret);

void sub_003DFCDF(void)
{
    uint32_t cue = g_eax;
    ma2_xact_cue(0, 0, cue, 0, 0, 0);
    sub_003DFCDF_gen();
    ma2_xact_cue(0, 1, cue, 0, 0, g_eax);
}

void sub_003E0587(void)
{
    uint32_t cue = MA2_MEM32(g_esp + 4), entry = MA2_MEM32(g_esp + 8);
    uint32_t flag = MA2_MEM32(g_esp + 12);
    ma2_xact_cue(1, 0, cue, entry, flag, 0);
    sub_003E0587_gen();
    ma2_xact_cue(1, 1, cue, entry, flag, g_eax);
}

/* sub_003E27F2 (eax = wave instance, ecx = its sound, [esp+4] flag): XACT
 * asking whether a playing wave has finished. A streamed one is finished
 * once DirectSound has completed one of its two packets (status no longer
 * 0x8000000A, XMEDIAPACKET pending; sub_003E28DB). */
extern void sub_003E27F2_gen(void);
void ma2_xact_wave(int leave, uint32_t wave, uint32_t sound, uint32_t ret);

void sub_003E27F2(void)
{
    uint32_t wave = g_eax, sound = g_ecx;
    ma2_xact_wave(0, wave, sound, 0);
    sub_003E27F2_gen();
    ma2_xact_wave(1, wave, sound, g_eax);
}

/* The hand-off from a transition sound to the variation after it:
 * sub_003E1288 (ecx = sound), sub_003E007B (eax = sound, esi = cue),
 * sub_003E3628 (eax = sound, edi -> position out; HRESULT) and
 * sub_003E0A13 (ecx = cue, edx = slot, eax = position, [esp+4]). */
extern void sub_003E1288_gen(void);
extern void sub_003E007B_gen(void);
extern void sub_003E0A13_gen(void);
extern void sub_003E3628_gen(void);
void ma2_xact_step(const char *fn, int leave, uint32_t a, uint32_t b, uint32_t c, uint32_t ret);

void sub_003E1288(void)
{
    uint32_t snd = g_ecx;
    ma2_xact_step("1288 sound-step", 0, snd, 0, 0, 0);
    sub_003E1288_gen();
    ma2_xact_step("1288 sound-step", 1, snd, 0, 0, g_eax);
}

void sub_003E007B(void)
{
    extern __declspec(thread) uint32_t g_esi;
    uint32_t snd = g_eax, cue = g_esi;
    ma2_xact_step("007B handoff", 0, snd, cue, 0, 0);
    sub_003E007B_gen();
    ma2_xact_step("007B handoff", 1, snd, cue, 0, g_eax);
}

void sub_003E3628(void)
{
    extern __declspec(thread) uint32_t g_edi;
    uint32_t snd = g_eax, out = g_edi;
    ma2_xact_step("3628 position", 0, snd, out, 0, 0);
    sub_003E3628_gen();
    ma2_xact_step("3628 position", 1, snd, out, out ? MA2_MEM32(out) : 0, g_eax);
}

void sub_003E0A13(void)
{
    extern __declspec(thread) uint32_t g_edx;
    uint32_t cue = g_ecx, slot = g_edx, pos = g_eax;
    ma2_xact_step("0A13 schedule", 0, cue, slot, pos, 0);
    sub_003E0A13_gen();
    ma2_xact_step("0A13 schedule", 1, cue, slot, pos, g_eax);
}

/* sub_003E73D1 (esi = clock owner, [esp+4] -> 64-bit out): XACT's audio
 * clock, in 100 ns, accumulated from XGSCNT reads. */
extern void sub_003E73D1_gen(void);
void ma2_xact_clock(uint32_t owner, uint32_t out);

void sub_003E73D1(void)
{
    extern __declspec(thread) uint32_t g_esi;
    uint32_t owner = g_esi, out = MA2_MEM32(g_esp + 4);
    sub_003E73D1_gen();
    ma2_xact_clock(owner, out);
}

/* sub_003E7479 (eax = event; engine, sound, flag, elapsed; ret 0x10): an
 * XACT event's due time, into event+0x18. */
extern void sub_003E7479_gen(void);
void ma2_xact_due(uint32_t ev, uint32_t engine, uint32_t sound, uint32_t flag, uint32_t elapsed);

void sub_003E7479(void)
{
    uint32_t ev = g_eax, engine = MA2_MEM32(g_esp + 4), sound = MA2_MEM32(g_esp + 8);
    uint32_t flag = MA2_MEM32(g_esp + 12), elapsed = MA2_MEM32(g_esp + 16);
    sub_003E7479_gen();
    ma2_xact_due(ev, engine, sound, flag, elapsed);
}

/* sub_003E133C (edi = sound, [esp+4] = 1 pause / 0 resume): XACT pausing
 * or resuming a sound; a resume moves its queued events on by the time it
 * spent paused (now - sound+0x50). */
extern void sub_003E133C_gen(void);
void ma2_xact_pause(int leave, uint32_t sound, uint32_t pause, uint32_t caller);

void sub_003E133C(void)
{
    extern __declspec(thread) uint32_t g_edi;
    static int fix = -1;
    uint32_t sound = g_edi, pause = MA2_MEM32(g_esp + 4), caller = MA2_MEM32(g_esp);

    /* A resume of a sound that was never paused (pause time 0) has nothing
     * to undo, but XACT does it anyway: its guard lets a playing sound
     * through, and it moves every queued event on by now - 0, a whole
     * system time. MA2 resumes everything when a front-end screen closes
     * and again when loading ends; a mission's music cue started between
     * the two had its intro-to-track transition pushed four centuries out,
     * and the music stopped when the intro did. MA2_XACT_RESUME_FIX=0
     * lets the resume through as before. */
    if (fix < 0) {
        const char *e = getenv("MA2_XACT_RESUME_FIX");
        fix = !(e && *e == '0');
    }
    if (fix && !pause && MA2_MEM32(sound + 0x50) == 0 && MA2_MEM32(sound + 0x54) == 0) {
        g_esp += 8;           /* ret 4: the return address and the flag */
        return;
    }
    ma2_xact_pause(0, sound, pause, caller);
    sub_003E133C_gen();
    ma2_xact_pause(1, sound, pause, caller);
}

/* sub_000BEE80 (edi = the game's audio system; category, pause; ret 8):
 * the game asking XACT to pause or resume a category, 0xFF for all. */
extern void sub_000BEE80_gen(void);

void sub_000BEE80(void)
{
    static int on = -1;
    uint32_t cat = MA2_MEM32(g_esp + 4), pause = MA2_MEM32(g_esp + 8);
    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (on) {
        uint32_t k;
        fprintf(stderr, "[XACT] %lu game %s category %02X, stack:", (unsigned long)GetTickCount(),
                pause ? "pause " : "resume", cat & 0xFF);
        for (k = 0; k < 0x60; k += 4) {
            uint32_t v = MA2_MEM32(g_esp + k);
            if (v >= 0x00012000u && v < 0x003279C8u)
                fprintf(stderr, " %08X", v);
        }
        fprintf(stderr, "\n");
    }
    sub_000BEE80_gen();
}

void sub_003E0C44(void)
{
    extern __declspec(thread) uint32_t g_edi;
    uint32_t cue = g_edi;
    ma2_xact_cue(2, 0, cue, 0, 0, 0);
    sub_003E0C44_gen();
    ma2_xact_cue(2, 1, cue, 0, 0, g_eax);
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

/* ── MechAssault 2: vtable methods the disassembler never found ── */

/*
 * Nothing calls these directly, so the function finder missed them, they got
 * no dispatch entry, and every call failed: return address popped, eax = 0.
 * In combat that was ~1000 failed calls a second, each logged as 18 lines --
 * the main thread spent two thirds of its time in WriteFile and the frame
 * rate fell from 60-100 to 30-40 (MODLOG, "COMBAT FRAME DROPS").
 *
 *   0x0006B4E0  lea eax, [ecx+0x28]; ret -- the getter beside SetMatrix
 *               (0x0006B4F0 copies 16 dwords to this+0x28). Failing, it gave
 *               its callers a NULL matrix, read from guest page 0.
 *   0x001C4EC6, 0x001C4F66, 0x001C4FAE, 0x001C5275  forward to the object at
 *               this+0x38 through its vtable (+0x3C, +0x48, +0x24, +0x40):
 *                   cmp [ecx+0x38], 0 / je none
 *                   mov ecx, [ecx+0x38] / mov eax, [ecx] / jmp [eax+slot]
 *                   none: (xor al, al) ret
 *
 * A few instructions each, so written here rather than seeded and the whole
 * title regenerated. MA2_SKIP_ICALL=<va> makes one fail again, as before.
 */
extern __declspec(thread) uint32_t g_ecx;
extern recomp_func_t recomp_lookup(uint32_t xbox_va);
extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
void recomp_icall_fail_log(uint32_t va);

static void ma2_get_matrix_0006B4E0(void)
{
    g_eax = g_ecx + 0x28;
    g_esp += 4;                         /* ret */
}

/* The jmp is a tail call: the method returns straight to our caller, so the
 * return address stays on the guest stack for it to pop. */
static void ma2_forward(uint32_t slot, int clear_al)
{
    uint32_t sub = MA2_MEM32(g_ecx + 0x38), va;
    recomp_func_t fn;

    if (!sub) {
        if (clear_al)
            g_eax &= ~0xFFu;
        g_esp += 4;                     /* ret */
        return;
    }
    g_ecx = sub;
    g_eax = MA2_MEM32(sub);
    va = MA2_MEM32(g_eax + slot);
    fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) fn = recomp_lookup_kernel(va);
    if (fn) {
        fn();
    } else {
        recomp_icall_fail_log(va);
        g_esp += 4;
        g_eax = 0;
    }
}

static void ma2_forward_001C4EC6(void) { ma2_forward(0x3C, 0); }
static void ma2_forward_001C4F66(void) { ma2_forward(0x48, 1); }
static void ma2_forward_001C4FAE(void) { ma2_forward(0x24, 0); }
static void ma2_forward_001C5275(void) { ma2_forward(0x40, 1); }

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    if (ma2_is_skipped(xbox_va))
        return ma2_skipped_icall;
    switch (xbox_va) {
    case 0x0006B4E0u: return ma2_get_matrix_0006B4E0;
    case 0x001C4EC6u: return ma2_forward_001C4EC6;
    case 0x001C4F66u: return ma2_forward_001C4F66;
    case 0x001C4FAEu: return ma2_forward_001C4FAE;
    case 0x001C5275u: return ma2_forward_001C5275;
    default: break;
    }
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
 *
 * Rate-limited per address, as recomp_icall_not_code_log is: reported at
 * 1, 10, 100, 1000 ... calls, with the recent targets on the first. This
 * printed 18 lines for every failure, and a vtable method missing from the
 * dispatch table fails every frame: in MA2's combat that was 18,000 lines a
 * second, one WriteFile each, on the thread that runs the game.
 */
void recomp_icall_fail_log(uint32_t va)
{
    enum { SLOTS = 64 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS + 1];    /* [SLOTS]: addresses past the table */
    static int count;
    static SRWLOCK lock = SRWLOCK_INIT;
    uint64_t n;
    int i;

    AcquireSRWLockExclusive(&lock);
    for (i = 0; i < count && seen[i] != va; i++)
        ;
    if (i == count && count < SLOTS)
        seen[count++] = va;
    n = ++hits[i];
    ReleaseSRWLockExclusive(&lock);
    while (n >= 10 && n % 10 == 0)
        n /= 10;
    if (n != 1)
        return;

    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X -- %llu time(s)%s (total calls: %llu)\n",
            va, (unsigned long long)hits[i], i == SLOTS ? " (all addresses past the first 64)" : "",
            (unsigned long long)g_icall_count);

    if (hits[i] == 1) {
        /* Dump last 16 call targets from the ring buffer */
        fprintf(stderr, "  Recent ICALL targets:\n");
        for (int k = 0; k < 16; k++) {
            int idx = (g_icall_trace_idx - 16 + k) & 15;
            if (g_icall_trace[idx])
                fprintf(stderr, "    [%2d] 0x%08X\n", k, g_icall_trace[idx]);
        }
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
