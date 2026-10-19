/*
 * MechAssault 2: the walker's ground probe, traced (bring-up).
 *
 * A walker keeps a ground height (state +0x20, body +0x60) and counts itself
 * airborne while its feet are more than 0.05 above it: gravity on, ground
 * friction off. sub_001760E1 refreshes it as max(terrain height, a downward
 * ray against the level's objects), and the ray is sub_0008DF50 (grid,
 * query, result): a broadphase gather into grid+8..+0xC, then each
 * candidate's vt+0x68 narrowphase.
 *
 * MA2_GROUND_TRACE=<ms>:<count> logs the player's next <count> probes, that
 * long after the player's body is first known: the position asked about, the
 * height answered, and for each ray the candidates the broadphase found and
 * whether any was hit.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <windows.h>   /* GetTickCount */

extern ptrdiff_t g_xbox_mem_offset;
uint32_t ma2_player_body(void);

#define GND_MEM32(va) (*(volatile uint32_t *)((uint8_t *)g_xbox_mem_offset + (va)))
#define GND_MEMF(va)  (*(volatile float *)((uint8_t *)g_xbox_mem_offset + (va)))

static int s_in;            /* inside the player's probe */
static int s_left = -1;     /* probes still to log; -1 = not configured yet */
static DWORD s_from, s_t0;

static int gnd_tracing(uint32_t body)
{
    if (s_left < 0) {
        const char *v = getenv("MA2_GROUND_TRACE");
        char *end;
        s_left = 0;
        if (v) {
            s_from = strtoul(v, &end, 10);
            s_left = *end == ':' ? atoi(end + 1) : 200;
        }
    }
    if (!s_left || !body || body != ma2_player_body())
        return 0;
    if (!s_t0)
        s_t0 = GetTickCount();
    return GetTickCount() - s_t0 >= s_from;
}

void ma2_ground_probe_enter(uint32_t body, uint32_t pos)
{
    s_in = gnd_tracing(body);
    if (s_in)
        fprintf(stderr, "[GND] probe %lu pos %.3f %.3f %.3f  ground %.3f\n",
                (unsigned long)(GetTickCount() - s_t0), GND_MEMF(pos),
                GND_MEMF(pos + 4), GND_MEMF(pos + 8), GND_MEMF(body + 0x60));
}

void ma2_ground_probe_leave(uint32_t body, double h)
{
    (void)body;
    if (!s_in)
        return;
    fprintf(stderr, "[GND]   -> %.3f\n", h);
    s_in = 0;
    s_left--;
}

void ma2_ground_ray_leave(uint32_t grid, uint32_t query, uint32_t result, int hit)
{
    char line[1024];
    int at, k, n;
    uint32_t b, e;

    if (!s_in)
        return;
    b = GND_MEM32(grid + 8);
    e = GND_MEM32(grid + 0xC);
    n = e >= b ? (int)((e - b) / 4) : -1;
    at = snprintf(line, sizeof line, "[GND]   ray hit=%d t=%.4f obj=%08X cand=%d q:",
                  hit, GND_MEMF(result + 0xC), GND_MEM32(result + 4), n);
    for (k = 0; k < 0x30; k += 4) {
        float f = GND_MEMF(query + k);
        uint32_t u = GND_MEM32(query + k);
        at += (u >= 0x00010000u && u < 0x04000000u) || (f != f)
            ? snprintf(line + at, sizeof line - at, " %08X", u)
            : snprintf(line + at, sizeof line - at, " %g", f);
    }
    for (k = 0; k < n && k < 12 && at < (int)sizeof line - 40; k++) {
        uint32_t o = GND_MEM32(b + 4 * k);
        uint32_t vt = o ? GND_MEM32(o) : 0;
        at += snprintf(line + at, sizeof line - at, " [%08X vt %06X n %06X]",
                       o, vt, vt ? GND_MEM32(vt + 0x68) : 0);
    }
    snprintf(line + at, sizeof line - at, "\n");
    fputs(line, stderr);
}

/* One node of the walk: enter (leave = 0) and leave, indented by depth.
 * test = the node's own test (sub_000831E0) rather than the walk into its
 * children (sub_00083070). */
void ma2_ground_node(int leave, int test, uint32_t node, uint32_t query, uint32_t result,
                     int hit, int descend)
{
    static int depth;
    uint32_t vt;

    if (!s_in)
        return;
    if (leave) {
        depth--;
        if (test)
            fprintf(stderr, "[GND]   %*s< test %08X hit=%d%s\n", 2 * depth, "",
                    node, hit, descend ? " descend" : " stop");
        else
            fprintf(stderr, "[GND]   %*s< walk %08X hit=%d t %.4f obj %08X n %.3f %.3f %.3f"
                    " r: %08X %08X %g %g %g %g %g %g\n", 2 * depth, "",
                    node, hit, GND_MEMF(result + 0xC), GND_MEM32(result + 4),
                    GND_MEMF(result + 0x34), GND_MEMF(result + 0x38), GND_MEMF(result + 0x3C),
                    GND_MEM32(result), GND_MEM32(result + 8), GND_MEMF(result + 0x10),
                    GND_MEMF(result + 0x14), GND_MEMF(result + 0x18), GND_MEMF(result + 0x1C),
                    GND_MEMF(result + 0x20), GND_MEMF(result + 0x30));
        return;
    }
    vt = GND_MEM32(node);
    {
        /* the node's collision shape (node+0xAC): its class and ray test */
        uint32_t sh = GND_MEM32(node + 0xAC);
        uint32_t svt = sh >= 0x10000u && sh < 0x04000000u ? GND_MEM32(sh) : 0;
        fprintf(stderr, "[GND]   %*s> %s %08X vt %06X flags %08X kids %d qtype %u%s %06X"
                " shape %08X vt %06X ray %06X\n",
                2 * depth, "", test ? "test" : "walk", node, vt, GND_MEM32(node + 0x34),
                (int)((GND_MEM32(node + 0xC) - GND_MEM32(node + 8)) / 4), GND_MEM32(query),
                test ? " vt88" : " vt68", GND_MEM32(vt + (test ? 0x88 : 0x68)),
                sh, svt, svt ? GND_MEM32(svt + 0x14) : 0);
    }
    depth++;
}
