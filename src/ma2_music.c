/*
 * MechAssault 2: the mission music, watched (bring-up).
 *
 * A mission's music action (sub_0014D9CC, wrapped in recomp_manual.c) plays
 * a world cue from Music.xsb through the game's sound wrapper (vtable
 * 0x00464840). The cue is interactive: an intensity value picks the
 * variation. Under MA2_MUSIC_TRACE=1 the XACT cue functions it runs on are
 * logged here, so the moment the intro ends shows what the cue does next
 * (or fails to).
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern ptrdiff_t g_xbox_mem_offset;

#define MUS_MEM32(va) (*(volatile uint32_t *)((uint8_t *)g_xbox_mem_offset + (va)))
#define MUS_PTR(a) ((a) >= 0x10000u && (a) < 0x04000000u)

/* The XACT cue state machine (wrappers in recomp_manual.c). which: 0 the
 * per-update sub_003DFCDF, 1 sub_003E0587 (start a variation's sound), 2
 * sub_003E0C44 (pick a new variation). The update runs for every cue every
 * frame, so it prints only when a cue's state changed: the flags (+0x38),
 * its sound slots (+0x28 +0x2C +0x30), the variation (+0x3A) and the entry
 * (+0x3C). */
void ma2_xact_cue(int which, int leave, uint32_t cue, uint32_t a, uint32_t b, uint32_t ret)
{
    static int on = -1;
    static struct { uint32_t cue, s[5]; } seen[128];
    uint32_t s[5];
    int k, slot = -1;

    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (!on || !MUS_PTR(cue))
        return;
    s[0] = MUS_MEM32(cue + 0x38) & 0xFFFF;
    s[1] = MUS_MEM32(cue + 0x28);
    s[2] = MUS_MEM32(cue + 0x2C);
    s[3] = MUS_MEM32(cue + 0x30);
    s[4] = MUS_MEM32(cue + 0x3A);
    if (which == 0) {
        for (k = 0; k < 128; k++)
            if (seen[k].cue == cue || !seen[k].cue) { slot = k; break; }
        if (slot < 0)
            return;
        if (seen[slot].cue == cue && !memcmp(seen[slot].s, s, sizeof s))
            return;
        seen[slot].cue = cue;
        memcpy(seen[slot].s, s, sizeof s);
    }
    fprintf(stderr, "[XACT] %lu %s %s cue %08X bank %08X idx %u flags %04X snd %08X %08X %08X"
            " var %04X entry %04X%s",
            (unsigned long)GetTickCount(),
            which == 0 ? "update" : which == 1 ? "start " : "pick  ",
            leave ? "<" : ">", cue, MUS_MEM32(cue + 0x14), MUS_MEM32(cue + 0x18) & 0xFFFF,
            s[0], s[1], s[2], s[3], s[4] & 0xFFFF, s[4] >> 16, "");
    if (which == 1)
        fprintf(stderr, " arg entry %08X flag %u", a, b);
    if (leave && which)
        fprintf(stderr, " -> %08X", ret);
    fprintf(stderr, "\n");
}

/* XACT's "has this wave finished?" (sub_003E27F2, wrapper in
 * recomp_manual.c), for streamed waves: the wave's flags (+0), its stream
 * (+0x74) and the two packet slots at +0x40 (status +0x10, then +0x18 and
 * +0x1C). Printed when any of that or the answer changes. */
void ma2_xact_wave(int leave, uint32_t wave, uint32_t sound, uint32_t ret)
{
    static int on = -1;
    static struct { uint32_t wave, s[9]; } seen[32];
    uint32_t s[9], pk;
    int k, slot = -1;

    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (!on || !leave || !MUS_PTR(wave) || !MUS_MEM32(wave + 0x74))
        return;
    pk = MUS_MEM32(wave + 0x40);
    if (!MUS_PTR(pk))
        return;
    s[0] = MUS_MEM32(wave) & 0xFFFF;
    s[1] = MUS_MEM32(pk + 0x10);
    s[2] = MUS_MEM32(pk + 0x18);
    s[3] = MUS_MEM32(pk + 0x1C);
    s[4] = MUS_MEM32(pk + 0x30);
    s[5] = MUS_MEM32(pk + 0x38);
    s[6] = MUS_MEM32(pk + 0x3C);
    s[7] = MUS_MEM32(sound + 0x38);
    s[8] = ret;
    for (k = 0; k < 32; k++)
        if (seen[k].wave == wave || !seen[k].wave) { slot = k; break; }
    if (slot < 0 || (seen[slot].wave == wave && !memcmp(seen[slot].s, s, sizeof s)))
        return;
    seen[slot].wave = wave;
    memcpy(seen[slot].s, s, sizeof s);
    fprintf(stderr, "[XACT] %lu wave %08X sound %08X flags %04X stream %08X"
            " pk0 %08X %08X %08X pk1 %08X %08X %08X soundflags %08X -> %u\n",
            (unsigned long)GetTickCount(), wave, sound, s[0], MUS_MEM32(wave + 0x74),
            s[1], s[2], s[3], s[4], s[5], s[6], s[7], ret);
}

/* One step of a transition (wrappers in recomp_manual.c): the function,
 * its arguments and, leaving, its result. At most 400 lines. */
void ma2_xact_step(const char *fn, int leave, uint32_t a, uint32_t b, uint32_t c, uint32_t ret)
{
    static int on = -1, left = 400;
    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (!on || left <= 0)
        return;
    left--;
    if (leave)
        fprintf(stderr, "[XACT] %lu %s < %08X %08X %08X -> %08X\n",
                (unsigned long)GetTickCount(), fn, a, b, c, ret);
    else
        fprintf(stderr, "[XACT] %lu %s > %08X %08X %08X\n",
                (unsigned long)GetTickCount(), fn, a, b, c);
}

/* XACT's audio clock (sub_003E73D1, wrapper in recomp_manual.c): which
 * object owns it, the XGSCNT it last saw (+0x144), its sample count
 * (+0x148) and the time it handed out. The first 40 calls, every 2000th,
 * and any value of a day or more. */
void ma2_xact_clock(uint32_t owner, uint32_t out)
{
    static int on = -1;
    static unsigned n;
    unsigned long long v;

    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (!on || !MUS_PTR(out))
        return;
    v = (unsigned long long)MUS_MEM32(out) | ((unsigned long long)MUS_MEM32(out + 4) << 32);
    n++;
    if (n <= 40 || n % 2000 == 0 || v >= 864000000000ull)
        fprintf(stderr, "[XACT] %lu clock #%u owner %08X xgscnt %08X samples %08X%08X -> %llu\n",
                (unsigned long)GetTickCount(), n, owner, MUS_PTR(owner) ? MUS_MEM32(owner + 0x144) : 0,
                MUS_PTR(owner) ? MUS_MEM32(owner + 0x14C) : 0, MUS_PTR(owner) ? MUS_MEM32(owner + 0x148) : 0, v);
}

static unsigned long long mus_mem64(uint32_t va)
{
    return (unsigned long long)MUS_MEM32(va) | ((unsigned long long)MUS_MEM32(va + 4) << 32);
}

/* An XACT event's due time (sub_003E7479, wrapper in recomp_manual.c) =
 * offset + the sound's start (sound+0x30) + the engine's clock offset
 * (engine+0x158). Every event of type 0xE (a transition at a position), and
 * any due time past 2e17 (twice the system clock), with all three parts. */
void ma2_xact_due(uint32_t ev, uint32_t engine, uint32_t sound, uint32_t flag, uint32_t elapsed)
{
    static int on = -1, left = 300;
    uint32_t desc, d0 = 0;
    unsigned long long due;

    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (!on || left <= 0 || !MUS_PTR(ev))
        return;
    due = mus_mem64(ev + 0x18);
    desc = MUS_MEM32(ev + 0x14);
    if (MUS_PTR(desc))
        d0 = MUS_MEM32(desc);
    if ((d0 & 0xFF) != 0xE && due < 200000000000000000ull)
        return;
    left--;
    fprintf(stderr, "[XACT] %lu due ev %08X type %02X desc0 %08X desc+C %02X flag %u elapsed %u"
            " sound %08X start %llu engine %08X offset %llu -> due %llu\n",
            (unsigned long)GetTickCount(), ev, d0 & 0xFF, d0,
            MUS_PTR(desc) ? MUS_MEM32(desc + 0xC) & 0xFF : 0, flag, elapsed,
            sound, MUS_PTR(sound) ? mus_mem64(sound + 0x30) : 0ull,
            engine, MUS_PTR(engine) ? mus_mem64(engine + 0x158) : 0ull, due);
}

/* XACT pause/resume of a sound (sub_003E133C, wrapper in recomp_manual.c):
 * the sound, which way, who asked, its flags (+0x38) and pause time
 * (+0x50) before and after. At most 300 lines. */
void ma2_xact_pause(int leave, uint32_t sound, uint32_t pause, uint32_t caller)
{
    static int on = -1, left = 300;
    if (on < 0)
        on = getenv("MA2_MUSIC_TRACE") != NULL;
    if (!on || left <= 0 || !MUS_PTR(sound))
        return;
    left--;
    fprintf(stderr, "[XACT] %lu %s %s sound %08X from %08X flags %08X state %u paused-at %llu\n",
            (unsigned long)GetTickCount(), pause ? "pause " : "resume", leave ? "<" : ">",
            sound, caller, MUS_MEM32(sound + 0x38), MUS_MEM32(sound + 0x30) & 0xFFFF,
            mus_mem64(sound + 0x50));
}
