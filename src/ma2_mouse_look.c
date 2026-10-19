/*
 * MechAssault 2: direct mouse look.
 *
 * A walker turns by its physics body's yaw rate: the body's physics step
 * adds rate * dt to the heading, body+0x64, in degrees and unwrapped
 * (body+0x28 is last frame's copy). The rate is set through the body's
 * setter sub_0006C790 (this = body+0x40, writes body+0x74). The human
 * pilot's update, sub_00172E96 -- reached from the player's input-action
 * dispatch (sub_001AFE3B) -- sets it from the Look stick in two places:
 * standing, through sub_001002A3 (the setter call returns to 0x001002FB),
 * and walking, inline (returns to 0x001739C5); both as stick * the walker's
 * top turn rate, 180 deg/s. The stick it uses is the game's smoothed copy of
 * the pad, about 0.3 s to settle, behind a quarter-throw deadzone: a mouse
 * driving the stick turned late, by nothing for a small move, and never
 * faster than 180 deg/s.
 *
 * So after those two calls the mouse turns the heading itself:
 * mkb_mouse_look_take() hands over the degrees the mouse moved sideways
 * since the last frame (look_deg per count, times sens, from mkb.ini), and
 * while this keeps asking, sideways mouse movement no longer moves the Look
 * stick -- the rate the game sets comes from the pad alone.
 *
 * Up/down is the walker's look pitch, walker+0x144, in degrees (0 level,
 * positive looking down), integrated by sub_00120EBD: pitch += input *
 * walker+0x140 (80 deg/s) * dt, clamped to [walker+0x13C, walker+0x138]
 * (-54, 50). The pilot's sub_00175CA4 calls it with the smoothed Look y
 * (sub_00175AD1: the same deadzone and smoothing, sign from the game's
 * Invert option, pilot+0x6E8); the call returns to 0x00175CEF. The camera
 * reads the pitch back as a fraction of its range (sub_001205CD) and tilts
 * with it. After that call the mouse adds its own vertical degrees to the
 * pitch, in the same clamp. The game's Invert option is the stick's and
 * defaults to Yes; the mouse goes by mkb.ini's look_invert_y instead (0:
 * mouse up looks up). The view follows the pitch through the chase
 * camera's geometry, not exactly 1:1: about 1.2 degrees of view per degree
 * near level, 0.9 looking far down (50 degrees of pitch tilt the view 50,
 * -54 tilt it 60).
 *
 * Outside the walker (menus, vehicles, on foot) nothing asks, and after half
 * a second the mouse drives the stick on both axes again.
 *
 * MA2_MOUSE_LOOK=0 restores the stick path. MA2_LOOK_DEBUG=1 logs turns
 * and tilts.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>

extern ptrdiff_t g_xbox_mem_offset;
int mkb_mouse_look_take(float *yaw_deg, float *pitch_deg);

#define LOOK_MEMF(va)  (*(volatile float *)((uint8_t *)g_xbox_mem_offset + (va)))

/* MA2_LOOK_TRACE=1 (bring-up): every hook call, with the time, the axis and
 * what it applied -- the cadence the game asks at. The time is the
 * performance counter in ms, the clock RECOMP_PRESENT_TRACE prints. */
#include <windows.h>
static void look_trace(char axis, float d)
{
    static int on = -1, n;
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (on < 0) {
        on = getenv("MA2_LOOK_TRACE") != NULL;
        QueryPerformanceFrequency(&f);
    }
    if (!on || n >= 20000)
        return;
    n++;
    QueryPerformanceCounter(&t);
    fprintf(stderr, "[LOOKT] %c %.3f %+.4f\n", axis,
            (double)(t.QuadPart % (f.QuadPart * 100000)) * 1000.0 / (double)f.QuadPart, d);
}

/* The active camera: the camera manager at [0x00524A5C] keeps a vector of
 * cameras at +4..+8; the first with byte +0x95 set is the one in use
 * (sub_0016B554 / sub_0016B56A). 0 when there is none. */
#define LOOK_MEM32(va) (*(volatile uint32_t *)((uint8_t *)g_xbox_mem_offset + (va)))
static uint32_t ma2_active_camera(void)
{
    uint32_t mgr = LOOK_MEM32(0x00524A5Cu), p, e;
    if (mgr < 0x10000u || mgr >= 0x04000000u)
        return 0;
    p = LOOK_MEM32(mgr + 4);
    e = LOOK_MEM32(mgr + 8);
    if (p < 0x10000u || e >= 0x04000000u || e < p || e - p > 64 * 4)
        return 0;
    for (; p < e; p += 4) {
        uint32_t c = LOOK_MEM32(p);
        if (c >= 0x10000u && c < 0x04000000u
                && (*((volatile uint8_t *)g_xbox_mem_offset + c + 0x95)))
            return c;
    }
    return 0;
}

/* MA2_LOOK_CAMDUMP=1 (bring-up): the active camera's vtable and fields,
 * every frame for two seconds from the first mouse turn, to find where it
 * keeps its yaw. */
static void look_cam_dump(int moving)
{
    static int on = -1, shown;
    static DWORD start;
    uint32_t cam, k;
    if (on < 0)
        on = getenv("MA2_LOOK_CAMDUMP") != NULL;
    if (!on)
        return;
    if (!start) {
        if (!moving)
            return;
        start = GetTickCount() - 100;
    }
    if (GetTickCount() - start > 2000 || shown > 1000)
        return;
    cam = ma2_active_camera();
    if (!cam)
        return;
    if (!shown++) {
        uint32_t vt = LOOK_MEM32(cam);
        fprintf(stderr, "[CAM] camera %08X vtable %08X:", cam, vt);
        for (k = 0; k < 0x80 && vt >= 0x10000u && vt < 0x04000000u; k += 4)
            fprintf(stderr, " %08X", LOOK_MEM32(vt + k));
        fprintf(stderr, "\n");
    }
    {
        /* One write a line: stderr is unbuffered and other threads log too. */
        static char line[0x900 / 4 * 16 + 64];
        size_t at = (size_t)snprintf(line, sizeof line, "[CAMF] %lu", (unsigned long)GetTickCount());
        uint32_t node = LOOK_MEM32(cam + 0x78);
        int nodeok = node >= 0x10000u && node < 0x04000000u;
        for (k = 0; k < 0x900 && at < sizeof line - 20; k += 4) {
            float v = k < 0x800 ? LOOK_MEMF(cam + k)
                                : nodeok ? LOOK_MEMF(node + k - 0x800) : 0.0f;
            at += (size_t)snprintf(line + at, sizeof line - at, " %g", v);
        }
        snprintf(line + at, sizeof line - at, "\n");
        fputs(line, stderr);
    }
}

#define MA2_TURN_STANDING   0x001002FBu   /* in sub_001002A3 */
#define MA2_TURN_WALKING    0x001739C5u   /* in sub_00172E96 */
#define MA2_BODY_RATE_SET   0x40u         /* the setter's this, from the body */
#define MA2_BODY_HEADING    0x64u         /* degrees, unwrapped */
#define MA2_WALKER_CAM_TARGET 0x12Cu      /* heading the camera aims at */
#define MA2_WALKER_CAM_YAW    0x130u      /* the camera's yaw, degrees */

void ma2_mouse_look_rate_set(uint32_t ret, uint32_t comp, float rate)
{
    static int mode = -1, dbg, shown;
    uint32_t body;
    float yaw, before;

    if (ret != MA2_TURN_STANDING && ret != MA2_TURN_WALKING)
        return;
    if (mode < 0) {
        const char *e = getenv("MA2_MOUSE_LOOK");
        mode = !(e && *e == '0');
        dbg = getenv("MA2_LOOK_DEBUG") != NULL;
    }
    body = comp - MA2_BODY_RATE_SET;
    if (!mode || body < 0x10000u || body >= 0x04000000u)
        return;
    if (!mkb_mouse_look_take(&yaw, NULL))
        return;
    look_trace('y', yaw);
    look_cam_dump(yaw != 0.0f);
    if (yaw == 0.0f)
        return;
    before = LOOK_MEMF(body + MA2_BODY_HEADING);
    LOOK_MEMF(body + MA2_BODY_HEADING) = before + yaw;
    /* The camera turns with it. The walker keeps the heading it is aiming
     * the camera at (walker+0x12C) and the camera's own yaw (walker+0x130,
     * degrees), and moves the second toward the first at about 400 deg/s:
     * a flick turned the mech at once and the view a quarter of a second
     * later. The same turn on both leaves no gap to chase. The walker is the
     * player entity the camera follows (the camera list's active entry);
     * it is this body's when the heading it aims at is this body's heading
     * as it stood before the turn. */
    {
        uint32_t walker = ma2_active_camera();
        if (walker && fabsf(LOOK_MEMF(walker + MA2_WALKER_CAM_TARGET) - before) < 0.01f) {
            LOOK_MEMF(walker + MA2_WALKER_CAM_TARGET) += yaw;
            LOOK_MEMF(walker + MA2_WALKER_CAM_YAW) += yaw;
        } else if (dbg && shown < 2000) {
            fprintf(stderr, "[LOOK] camera not locked: walker %08X target %g heading %g\n",
                    walker, walker ? LOOK_MEMF(walker + MA2_WALKER_CAM_TARGET) : 0.0f, before);
        }
    }
    if (dbg && shown < 2000) {
        shown++;
        fprintf(stderr, "[LOOK] %s body %08X rate %g yaw %+.3f heading %.3f\n",
                ret == MA2_TURN_WALKING ? "walking " : "standing", body, rate, yaw,
                LOOK_MEMF(body + MA2_BODY_HEADING));
    }
}

#define MA2_PITCH_PILOT     0x00175CEFu   /* in sub_00175CA4 */
#define MA2_PITCH_MAX       0x138u
#define MA2_PITCH_MIN       0x13Cu
#define MA2_PITCH_RATE      0x140u
#define MA2_PITCH           0x144u

void ma2_mouse_look_pitch(uint32_t ret, uint32_t walker, float input)
{
    static int mode = -1, dbg, shown;
    float up, p, lo, hi;

    if (ret != MA2_PITCH_PILOT)
        return;
    if (mode < 0) {
        const char *e = getenv("MA2_MOUSE_LOOK");
        mode = !(e && *e == '0');
        dbg = getenv("MA2_LOOK_DEBUG") != NULL;
    }
    if (!mode || walker < 0x10000u || walker >= 0x04000000u)
        return;
    if (!mkb_mouse_look_take(NULL, &up))
        return;
    look_trace('p', up);
    lo = LOOK_MEMF(walker + MA2_PITCH_MIN);
    hi = LOOK_MEMF(walker + MA2_PITCH_MAX);
    p = LOOK_MEMF(walker + MA2_PITCH);
    if (dbg && shown < 4000 && (up != 0.0f || input != 0.0f)) {
        shown++;
        fprintf(stderr, "[LOOK] pitch walker %08X input %g up %+.3f pitch %.4f [%g, %g] rate %g\n",
                walker, input, up, p, lo, hi, LOOK_MEMF(walker + MA2_PITCH_RATE));
    }
    if (up == 0.0f)
        return;
    p -= up;
    if (p < lo) p = lo;
    if (p > hi) p = hi;
    LOOK_MEMF(walker + MA2_PITCH) = p;
}
