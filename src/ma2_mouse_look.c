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
 * The heading is not the whole of it. The player's walker (the camera list's
 * active entity) keeps the aim, walker+0x12C -- where the mech looks; a copy
 * of the heading when standing or walking ahead, its own when strafing or on
 * a diagonal, where the legs turn toward the way the mech moves -- and the
 * camera's yaw, walker+0x130, which chases the aim at about 400 deg/s. And
 * the two stick turns above are skipped when strafing or on a diagonal.
 *
 * So the mouse turns all three at once, from the pitch hook below, which the
 * pilot runs every frame however it moves: mkb_mouse_look_take() hands over
 * the degrees the mouse moved since the last frame (look_deg per count, times
 * sens, from mkb.ini); aim, camera yaw and the headings of the bodies the
 * stick turns (the mech's [mech+0x8C], and its second body at mech+0x2B8 on
 * mechs that have one -- look_bodies) each get them, so nothing chases and
 * the legs keep their angle to the aim. While this keeps asking,
 * mouse movement no longer moves the Look stick -- the rates the game sets
 * come from the pad alone.
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
 * The pitch call is gated by the game: the pilot's enable for Look y and a
 * third-person camera. When it is silent the mech pilot's update stands in
 * (ma2_mouse_look_update, below) and logs why, once per change.
 *
 * Vehicles go through the same pitch integrator: tanks, flyers and movers
 * through the pilot's sub_00175CA4 (the call above), the VTOL from its
 * pilot's update, sub_00174A48 (returns to 0x00174C18). The hook names the
 * pilot ([pilot+8] the camera, [pilot+0xC] the vehicle) and the mouse turns
 * what that pilot's Look X turns -- a tank's turret, a VTOL's or flyer's
 * body (look_vehicle) -- with the aim and the camera yaw, and tilts the
 * same walker+0x144. A pilot class whose Look X turns nothing it knows (the
 * corporeal one) gets the tilt only. A VTOL also climbs or dives while the
 * stick pushes past either end of that range (sub_00174A48 reads the raw
 * Look y there); the mouse does not do that -- Jump (pilot+0x70) climbs and
 * pilot+0x214 dives, as with the pad.
 *
 * Elsewhere (menus, pilots that never make the call) nothing asks, and
 * after half a second the mouse drives the stick on both axes again.
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
#define MA2_BODY_POS        0x44u         /* x, y, z */
#define MA2_WALKER_NODE     0x78u         /* scene node; its +0x38 is x, y, z */
#define MA2_WALKER_CAM_TARGET 0x12Cu      /* the aim: heading the camera aims at */
#define MA2_WALKER_CAM_YAW    0x130u      /* the camera's yaw, degrees */

/* The pilot's body, as its stick turns last saw it (0 = not yet). */
static uint32_t s_body;

uint32_t ma2_player_body(void) { return s_body; }

/* sub_0006C790 wrapper: the pilot's two stick turns name the body. */
static void look_poslog(uint32_t walker);

void ma2_mouse_look_rate_set(uint32_t ret, uint32_t comp, float rate)
{
    uint32_t body;
    (void)rate;
    /* Every body's rate setter runs every frame, whatever the player drives:
     * the place to log where the player is (MA2_POSLOG, F8). */
    {
        uint32_t w = ma2_active_camera();
        if (w)
            look_poslog(w);
    }
    if (ret != MA2_TURN_STANDING && ret != MA2_TURN_WALKING)
        return;
    body = comp - MA2_BODY_RATE_SET;
    if (body >= 0x10000u && body < 0x04000000u)
        s_body = body;
}

/* Is this body the walker's? Its scene node sits at the body, offset by the
 * camera's orbit (about 10 units): within 40 is the same mech. */
static int body_is_walkers(uint32_t body, uint32_t walker)
{
    uint32_t node = LOOK_MEM32(walker + MA2_WALKER_NODE);
    float dx, dz;
    if (!body || node < 0x10000u || node >= 0x04000000u)
        return 0;
    dx = LOOK_MEMF(body + MA2_BODY_POS) - LOOK_MEMF(node + 0x38);
    dz = LOOK_MEMF(body + MA2_BODY_POS + 8) - LOOK_MEMF(node + 0x40);
    return dx * dx + dz * dz < 40.0f * 40.0f;
}

/* The sideways mouse turn, once a frame from the pitch hook below (which the
 * pilot runs in every way of moving; the stick turns above are skipped when
 * strafing or moving diagonally). Three angles move together:
 *  - walker+0x12C, the aim: where the mech looks and the camera points.
 *    Standing or walking ahead it is a copy of the body's heading; strafing
 *    or on a diagonal the body's legs turn toward the way it moves (the
 *    heading is the aim plus that angle, 90 strafing) and the aim is its own.
 *  - walker+0x130, the camera's yaw, which chases the aim at ~400 deg/s.
 *  - body+0x64, the heading (legs), so the legs keep their angle to the aim.
 * Turned together there is no gap for either to chase. */
/* The player's mech, as the pilot's update (ma2_mouse_look_update) last saw
 * it: the camera entity [pilot+8] and the vehicle [pilot+0xC]. Read every
 * frame, so the bodies to turn are known whichever way the mech moves. */
static uint32_t s_pilot_cam, s_pilot_mech;

#define MA2_MECH_BODY       0x8Cu     /* [mech+0x8C]: the body the legs stand on */
#define MA2_MECH_FLAGS_HI   0x26Cu    /* bit 30: stick turns the second body */
#define MA2_MECH_BODY2_ON   0x400u    /* nonzero: the mech has the second body */
#define MA2_MECH_BODY2      0x2B8u    /* mech+0x2B8: second body, same layout */

static int look_va_ok(uint32_t va) { return va >= 0x10000u && va < 0x04000000u; }

/* The player's vehicle, as the pilot's pitch call (ma2_mouse_look_pitch)
 * last saw it: the camera entity [pilot+8], the vehicle [pilot+0xC] and the
 * pilot's behaviour class, told by its vtable (sub_000C9084 makes one per
 * entity type). What the mouse turns is what the stick's Look X turns in
 * that class:
 *  - walker (MAMech): the bodies in look_bodies below.
 *  - tank (MATank): only the turret, a body embedded at tank+0x488 whose
 *    heading is the turret's angle from the hull. Look X sets its yaw rate
 *    (sub_001753E6 -> sub_00109FF4 -> sub_002ADC10); the hull drives toward
 *    the Move stick, taken relative to the camera, by itself.
 *  - VTOL, flyer, mover (MAFlyer and the rest on MAMover): the body
 *    [vehicle+0x8C]; Look X sets its yaw rate (VTOL sub_001745B9 -> vt+0x260,
 *    the others sub_0017537D -> vt+0x234).
 *  - anything else (the corporeal pilot ignores Look X): nothing; the
 *    sideways mouse stays on the stick there.
 * The third-person camera aims at the vehicle's heading, plus the turret's
 * angle on a tank (sub_0011F506: +0x12C from sub_000DD905 and, for MAMech
 * and MATank, the entity's vt+0x19C), so turning these turns the aim the
 * camera works out each frame. */
enum { VEH_OTHER, VEH_WALKER, VEH_TANK, VEH_FLYER, VEH_VTOL, VEH_MOVER };
static const char *const s_veh_names[] = { "other", "walker", "tank", "flyer", "VTOL", "mover" };
static uint32_t s_veh_cam, s_veh;
static int s_veh_kind;

#define MA2_TANK_TURRET     0x488u    /* tank+0x488: the turret's body */
#define MA2_BODY_LAST_HEADING 0x28u   /* last frame's heading */
#define MA2_BODY_RATE       0x74u     /* yaw rate, deg/s */
#define MA2_BODY_CMD_HEADING 0xA0u    /* the controller's heading (+0x7C block) */
#define MA2_BODY_CMD_RATE   0xB0u     /* the controller's yaw rate */

/* The headings the Look stick can turn. sub_00172E96 turns the body at
 * [mech+0x8C] (through sub_001002A3 standing, inline walking), and through
 * the mech's vtable +0x270 (sub_00100228) a second body embedded at
 * mech+0x2B8, when mech+0x400 is set; a per-mech flag (mech+0x26C bit 30)
 * sends the stick to the second one only. Both get the mouse's turn, so
 * neither lags behind the aim, whichever the flag picks. In play the second
 * body's heading read as the torso's twist from the legs (body + second =
 * aim, held at +-45), which the game keeps up itself. Before the pilot's
 * update has named the mech, the body the stick-turn wrapper saw last is
 * used, while it stands at this camera. In a vehicle *a is what it turns
 * (s_veh_kind above): the turret's body on a tank, else its body. */
static int look_bodies(uint32_t walker, uint32_t *a, uint32_t *b, int *flag)
{
    uint32_t mech = walker == s_pilot_cam ? s_pilot_mech : 0;
    *a = *b = 0;
    *flag = 0;
    if (walker == s_veh_cam && s_veh_kind != VEH_WALKER) {
        if (s_veh_kind == VEH_TANK) {
            *a = s_veh + MA2_TANK_TURRET;
        } else if (s_veh_kind != VEH_OTHER) {
            uint32_t body = LOOK_MEM32(s_veh + MA2_MECH_BODY);
            if (look_va_ok(body))
                *a = body;
        }
        return *a != 0;
    }
    if (look_va_ok(mech)) {
        uint32_t body = LOOK_MEM32(mech + MA2_MECH_BODY);
        if (look_va_ok(body))
            *a = body;
        if (LOOK_MEM32(mech + MA2_MECH_BODY2_ON))
            *b = mech + MA2_MECH_BODY2;
        if (*b == *a)
            *b = 0;
        *flag = (int)((LOOK_MEM32(mech + MA2_MECH_FLAGS_HI) >> 30) & 1u);
    } else if (body_is_walkers(s_body, walker)) {
        *a = s_body;
    }
    return *a || *b;
}

/* The sideways mouse turn, once a frame from the pitch hook below (which the
 * pilot runs in every way of moving; the stick turns above are skipped when
 * strafing or moving diagonally). These move together:
 *  - walker+0x12C, the aim: where the mech looks and the camera points.
 *    Standing or walking ahead it is a copy of the body's heading; strafing
 *    or on a diagonal the body's legs turn toward the way it moves (the
 *    heading is the aim plus that angle, 90 strafing) and the aim is its own.
 *  - walker+0x130, the camera's yaw, which chases the aim at ~400 deg/s.
 *  - the headings, body+0x64, of the bodies the stick turns (look_bodies),
 *    so the legs keep their angle to the aim.
 * Turned together there is no gap for either to chase.
 *
 * Each new mech is logged once, and while turning, twice a second for the
 * first minute of each mech, its headings before the turn next to where the
 * last turn left them: a heading the game puts back shows there. */
static void look_turn(uint32_t walker, float yaw, int dbg)
{
    static int shown, lines;
    static uint32_t last_mech_cam, last_a;
    static float left_a, left_b;
    static DWORD next, first;
    float aim = LOOK_MEMF(walker + MA2_WALKER_CAM_TARGET);
    uint32_t a, b;
    int flag;
    int known = look_bodies(walker, &a, &b, &flag);
    int kind = walker == s_veh_cam ? s_veh_kind : VEH_WALKER;
    DWORD now = GetTickCount();

    if (walker != last_mech_cam || a != last_a) {
        last_mech_cam = walker;
        last_a = a;
        first = now;
        if (lines++ < 200)
            fprintf(stderr, "[LOOK] turning camera %08X: %s %08X flag %d, %s %08X, second body %08X%s\n",
                    walker, s_veh_names[kind],
                    kind != VEH_WALKER ? s_veh : walker == s_pilot_cam ? s_pilot_mech : 0, flag,
                    kind == VEH_TANK ? "turret" : "body", a, b,
                    known ? "" : " (none known: aim and camera only)");
    } else if (now - first < 60000 && (int)(now - next) >= 0 && lines < 200) {
        lines++;
        next = now + 500;
        if (kind == VEH_WALKER) {
            fprintf(stderr, "[LOOK] turn %+.2f: aim %.2f cam %.2f; body %.2f (left %.2f); second %.2f (left %.2f)\n",
                    yaw, aim, LOOK_MEMF(walker + MA2_WALKER_CAM_YAW),
                    a ? LOOK_MEMF(a + MA2_BODY_HEADING) : 0.0f, left_a,
                    b ? LOOK_MEMF(b + MA2_BODY_HEADING) : 0.0f, left_b);
        } else if (a) {
            /* A heading the game puts back shows as "left" against the
             * heading; the other copies say which one it keeps. */
            uint32_t hull = kind == VEH_TANK ? LOOK_MEM32(s_veh + MA2_MECH_BODY) : 0;
            fprintf(stderr, "[LOOK] turn %+.2f: aim %.2f cam %.2f; %s %.2f (left %.2f; last frame %.2f,"
                            " controller %.2f, rate %.2f / %.2f); hull %.2f\n",
                    yaw, aim, LOOK_MEMF(walker + MA2_WALKER_CAM_YAW), s_veh_names[kind],
                    LOOK_MEMF(a + MA2_BODY_HEADING), left_a, LOOK_MEMF(a + MA2_BODY_LAST_HEADING),
                    LOOK_MEMF(a + MA2_BODY_CMD_HEADING), LOOK_MEMF(a + MA2_BODY_RATE),
                    LOOK_MEMF(a + MA2_BODY_CMD_RATE),
                    look_va_ok(hull) ? LOOK_MEMF(hull + MA2_BODY_HEADING) : 0.0f);
        }
    }
    LOOK_MEMF(walker + MA2_WALKER_CAM_TARGET) = aim + yaw;
    LOOK_MEMF(walker + MA2_WALKER_CAM_YAW) += yaw;
    if (a)
        left_a = LOOK_MEMF(a + MA2_BODY_HEADING) += yaw;
    if (b)
        left_b = LOOK_MEMF(b + MA2_BODY_HEADING) += yaw;
    if (dbg && shown < 4000) {
        shown++;
        fprintf(stderr, "[LOOK] turn walker %08X yaw %+.3f aim %.3f heading %.3f / %.3f%s\n",
                walker, yaw, aim + yaw, a ? LOOK_MEMF(a + MA2_BODY_HEADING) : 0.0f,
                b ? LOOK_MEMF(b + MA2_BODY_HEADING) : 0.0f,
                known ? "" : " (body not known: aim only)");
    }
}

/* Bug hunting: the player's position.
 *   MA2_POSLOG=<ms>: a [POS] line every <ms> while the pilot is in a walker.
 *   F8 (any time): a [MARK] line from the input layer, and here a [POS]
 *   line at once, so a player can point at a spot ("I sink here").
 * Position: the body's (x, y, z) and heading when the stick-turn wrapper has
 * named a body at this walker, and the walker's scene node (the camera's
 * pivot) always. */
int mkb_take_mark(void);
/* MA2_TELEPORT=<ms>:<x>:<y>:<z>,... (bring-up): put the player's body there
 * (position and last frame's, so it arrives at rest), <ms> after the first
 * frame its body is known -- the mission start, whatever the boot took --
 * and hold it there for 150 ms. For standing a test where a bug was
 * reported; the heap moves with the frame rate, so fixed-address pokes
 * cannot do it. */
static void look_teleport(uint32_t walker)
{
    static int init, n;
    static struct { DWORD at; float x, y, z; } tp[32];
    static DWORD t0;
    DWORD now = GetTickCount();
    int k;
    if (!init) {
        const char *e = getenv("MA2_TELEPORT");
        char *end;
        init = 1;
        while (e && *e && n < 32) {
            tp[n].at = strtoul(e, &end, 10);
            if (*end != ':') break;
            tp[n].x = (float)strtod(end + 1, &end);
            if (*end != ':') break;
            tp[n].y = (float)strtod(end + 1, &end);
            if (*end != ':') break;
            tp[n].z = (float)strtod(end + 1, &end);
            n++;
            e = *end == ',' ? end + 1 : NULL;
        }
    }
    if (!n || !body_is_walkers(s_body, walker))
        return;
    if (!t0)
        t0 = now;
    /* MA2_BODY_DUMP=<ms>:<frames>: the body's first 0x200 bytes as floats,
     * one line a call, from <ms> after the mission start. */
    {
        static int left = -1;
        static DWORD from;
        if (left < 0) {
            const char *bd = getenv("MA2_BODY_DUMP");
            char *end;
            left = 0;
            if (bd && *bd) {
                from = strtoul(bd, &end, 10);
                left = *end == ':' ? atoi(end + 1) : 60;
            }
        }
        if (left > 0 && now - t0 >= from) {
            char line[0x200 / 4 * 14 + 48];
            int at = snprintf(line, sizeof line, "[BODY] %lu", (unsigned long)(now - t0));
            uint32_t k2;
            for (k2 = 0; k2 < 0x200 && at < (int)sizeof line - 16; k2 += 4)
                at += snprintf(line + at, sizeof line - at, " %g", LOOK_MEMF(s_body + k2));
            snprintf(line + at, sizeof line - at, "\n");
            fputs(line, stderr);
            left--;
        }
    }
    /* MA2_WATCH_BODY=<ms>:<offset>: arm the write watch on that field of the
     * player's body, that long after the mission start (the body's address
     * changes from run to run). */
    {
        static int armed;
        const char *wb = getenv("MA2_WATCH_BODY");
        if (wb && !armed) {
            char *end;
            DWORD at = strtoul(wb, &end, 10);
            uint32_t off = *end == ':' ? (uint32_t)strtoul(end + 1, NULL, 16) : 0x48;
            if (now - t0 >= at) {
                extern int xbox_WatchArmAt(uint32_t va);
                armed = 1;
                fprintf(stderr, "[LOOK] watching body %08X+0x%X\n", s_body, off);
                xbox_WatchArmAt(s_body + off);
            }
        }
    }
    for (k = 0; k < n; k++) {
        DWORD rel = now - t0;
        if (rel < tp[k].at || rel > tp[k].at + 150)
            continue;
        LOOK_MEMF(s_body + MA2_BODY_POS) = tp[k].x;
        LOOK_MEMF(s_body + MA2_BODY_POS + 4) = tp[k].y;
        LOOK_MEMF(s_body + MA2_BODY_POS + 8) = tp[k].z;
        LOOK_MEMF(s_body + 0x08) = tp[k].x;      /* last frame's position */
        LOOK_MEMF(s_body + 0x0C) = tp[k].y;
        LOOK_MEMF(s_body + 0x10) = tp[k].z;
    }
}

static void look_poslog(uint32_t walker)
{
    look_teleport(walker);
    if (walker < 0x10000u || walker >= 0x04000000u)
        return;
    static int every = -1;
    static DWORD next;
    uint32_t node;
    int marked;
    DWORD now = GetTickCount();
    if (every < 0) {
        const char *e = getenv("MA2_POSLOG");
        every = e && *e ? atoi(e) : 0;
    }
    marked = mkb_take_mark();
    if (!marked && (!every || (int)(now - next) < 0))
        return;
    node = LOOK_MEM32(walker + MA2_WALKER_NODE);
    next = now + (DWORD)every;
    {
        char line[256];
        int legs = body_is_walkers(s_body, walker);
        int at = snprintf(line, sizeof line, "[POS]%s %lu node %.2f %.2f %.2f aim %.1f",
                          marked ? " MARK" : "", (unsigned long)now,
                          node >= 0x10000u && node < 0x04000000u ? LOOK_MEMF(node + 0x38) : 0.0f,
                          node >= 0x10000u && node < 0x04000000u ? LOOK_MEMF(node + 0x3C) : 0.0f,
                          node >= 0x10000u && node < 0x04000000u ? LOOK_MEMF(node + 0x40) : 0.0f,
                          LOOK_MEMF(walker + MA2_WALKER_CAM_TARGET));
        if (legs)
            at += snprintf(line + at, sizeof line - at, " body %08X %.2f %.2f %.2f heading %.1f",
                           s_body, LOOK_MEMF(s_body + MA2_BODY_POS), LOOK_MEMF(s_body + MA2_BODY_POS + 4),
                           LOOK_MEMF(s_body + MA2_BODY_POS + 8), LOOK_MEMF(s_body + MA2_BODY_HEADING));
        snprintf(line + at, sizeof line - at, " walker %08X\n", walker);
        fputs(line, stderr);
    }
}

#define MA2_PITCH_PILOT     0x00175CEFu   /* in sub_00175CA4 */
#define MA2_PITCH_MAX       0x138u
#define MA2_PITCH_MIN       0x13Cu
#define MA2_PITCH_RATE      0x140u
#define MA2_PITCH           0x144u

static int s_mode = -1, s_dbg;

static int look_on(void)
{
    if (s_mode < 0) {
        const char *e = getenv("MA2_MOUSE_LOOK");
        s_mode = !(e && *e == '0');
        s_dbg = getenv("MA2_LOOK_DEBUG") != NULL;
    }
    return s_mode;
}

/* Hand the mouse to the camera entity: when yaw_on, the sideways turn
 * (look_turn), and when pitch_on, the tilt -- walker+0x144 in the game's
 * own clamp. An axis not asked for stays on the stick. */
static void look_apply(uint32_t walker, float input, int pitch_on, int yaw_on)
{
    static int shown;
    float yaw = 0.0f, up = 0.0f, p, lo, hi;

    if (!mkb_mouse_look_take(yaw_on ? &yaw : NULL, pitch_on ? &up : NULL))
        return;
    look_trace('y', yaw);
    look_trace('p', up);
    look_cam_dump(yaw != 0.0f);
    if (yaw != 0.0f)
        look_turn(walker, yaw, s_dbg);
    if (!pitch_on)
        return;
    lo = LOOK_MEMF(walker + MA2_PITCH_MIN);
    hi = LOOK_MEMF(walker + MA2_PITCH_MAX);
    p = LOOK_MEMF(walker + MA2_PITCH);
    if (s_dbg && shown < 4000 && (up != 0.0f || input != 0.0f)) {
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

/* When the pilot's pitch call last ran (GetTickCount), and the camera it
 * ran on: the update below stands in while it is silent. */
static DWORD s_pitch_at;
static uint32_t s_pitch_walker;

#define MA2_PITCH_VTOL      0x00174C18u   /* in sub_00174A48 */
#define MA2_PILOT_CAMERA    0x08u
#define MA2_ENTITY_TYPE     0x28u     /* the entity's type id */
#define MA2_PILOT_VEHICLE   0x0Cu
#define MA2_PILOT_VT_WALKER 0x00477410u
#define MA2_PILOT_VT_VTOL   0x00477570u   /* "Default VTOL Behavior" */
#define MA2_PILOT_VT_FLYER  0x004776E0u   /* "Default Flyer Behavior" */
#define MA2_PILOT_VT_TANK   0x00477850u   /* "Tank Behavior" */
#define MA2_PILOT_VT_MOVER  0x004779B8u   /* "Default Mover Behavior" */
#define MA2_ENTITY_HEADING_SRC 0x90u      /* nonzero: sub_000DD905 reads its +0x28 */
#define MA2_CAM_AIM_OFFSET  0x11Cu        /* added to the heading for the aim */
#define MA2_CAM_FLAGS       0x114u

/* Name the vehicle the pitch call's pilot drives (s_veh_*, and the mech for
 * look_bodies), with a line for each new one: what the mouse turns and the
 * angles the camera's aim is made of. -1: not the pilot of this camera. */
static int look_vehicle(uint32_t pilot, uint32_t walker)
{
    static int shown;
    uint32_t vt, veh;
    int kind;

    if (!look_va_ok(pilot) || LOOK_MEM32(pilot + MA2_PILOT_CAMERA) != walker)
        return -1;
    vt = LOOK_MEM32(pilot);
    veh = LOOK_MEM32(pilot + MA2_PILOT_VEHICLE);
    kind = vt == MA2_PILOT_VT_WALKER ? VEH_WALKER :
           vt == MA2_PILOT_VT_TANK ? VEH_TANK :
           vt == MA2_PILOT_VT_VTOL ? VEH_VTOL :
           vt == MA2_PILOT_VT_FLYER ? VEH_FLYER :
           vt == MA2_PILOT_VT_MOVER ? VEH_MOVER : VEH_OTHER;
    if (!look_va_ok(veh))
        kind = VEH_OTHER;
    if (kind == VEH_WALKER) {
        s_pilot_cam = walker;
        s_pilot_mech = veh;
    }
    if (walker != s_veh_cam || veh != s_veh || kind != s_veh_kind) {
        s_veh_cam = walker;
        s_veh = veh;
        s_veh_kind = kind;
        if (shown++ < 50 && look_va_ok(veh)) {
            char line[512];
            uint32_t body = LOOK_MEM32(veh + MA2_MECH_BODY), src = LOOK_MEM32(veh + MA2_ENTITY_HEADING_SRC);
            int at = snprintf(line, sizeof line,
                              "[LOOK] camera %08X: %s, pilot %08X (vtable %08X), vehicle %08X type %08X;"
                              " body %08X heading %.2f",
                              walker, s_veh_names[kind], pilot, vt, veh, LOOK_MEM32(veh + MA2_ENTITY_TYPE),
                              body, look_va_ok(body) ? LOOK_MEMF(body + MA2_BODY_HEADING) : 0.0f);
            if (kind == VEH_TANK)
                at += snprintf(line + at, sizeof line - at, ", turret %.2f",
                               LOOK_MEMF(veh + MA2_TANK_TURRET + MA2_BODY_HEADING));
            if (src)
                at += snprintf(line + at, sizeof line - at, ", heading source %08X", src);
            snprintf(line + at, sizeof line - at, "; aim %.2f cam %.2f offset %.2f flags %08X; mouse %s\n",
                     LOOK_MEMF(walker + MA2_WALKER_CAM_TARGET), LOOK_MEMF(walker + MA2_WALKER_CAM_YAW),
                     LOOK_MEMF(walker + MA2_CAM_AIM_OFFSET), LOOK_MEM32(walker + MA2_CAM_FLAGS),
                     kind == VEH_OTHER ? "tilts; the stick turns" : "turns and tilts");
            fputs(line, stderr);
        }
    }
    return kind;
}

void ma2_mouse_look_pitch(uint32_t ret, uint32_t walker, float input, uint32_t pilot)
{
    int kind;

    if (ret != MA2_PITCH_PILOT && ret != MA2_PITCH_VTOL)
        return;
    if (!look_on() || walker < 0x10000u || walker >= 0x04000000u)
        return;
    s_pitch_at = GetTickCount();
    kind = look_vehicle(pilot, walker);
    if (walker != s_pitch_walker) {
        /* One line a camera, in every log: which path drives the mouse. */
        static int shown;
        s_pitch_walker = walker;
        if (shown++ < 50)
            fprintf(stderr, "[LOOK] mouse look on camera %08X (%s pitch call); body %s\n",
                    walker, ret == MA2_PITCH_VTOL ? "VTOL" : "pilot",
                    kind > VEH_WALKER ? "the vehicle's" :
                    body_is_walkers(s_body, walker) ? "known" : "not known yet");
    }
    look_apply(walker, input, 1, kind != VEH_OTHER);
}

/* The mech pilot's update, sub_00172E96 (thiscall, this = pilot), runs every
 * frame the player drives a mech. The pitch call above sits behind more:
 * the pilot's per-action enable for Look y (pilot+0x6A8 + 5, which the game's
 * scripts switch off and on) and a third-person camera, [pilot+8] being an
 * MACameraThird. When that call has been silent for 0.2 s while the pilot
 * still updates, the mouse goes in from here instead -- the turn always, the
 * tilt only while the game has Look y enabled -- on the same camera entity,
 * when it is a third-person one. Each change of the reason is logged once
 * ([LOOK] pilot update ...), so a play log says which way the mouse went. */
#define MA2_PILOT_OFF       0x6E4u    /* nonzero: the pilot is not driving */
#define MA2_PILOT_ACTIONS   0x6A8u    /* + action id: the game's enables */
#define MA2_ACTION_LOOK_Y   5u
#define MA2_TYPE_IDS        0x004EAFC0u
static const char *const s_cam_types[] = {
    "MACamera", "MAFreeCamera", "MACameraFirst", "MACameraThird",
    "MACameraSpiral", "MACameraFree", "MACameraPan",
};

void ma2_mouse_look_update(uint32_t pilot)
{
    static uint32_t last_pilot, last_cam, last_type;
    static int last_why = -1, shown;
    uint32_t cam, type;
    const char *tname = "not a camera";
    int third, look_y, why, k;

    if (!look_on() || pilot < 0x10000u || pilot >= 0x04000000u)
        return;
    s_pilot_cam = LOOK_MEM32(pilot + MA2_PILOT_CAMERA);
    s_pilot_mech = LOOK_MEM32(pilot + 0x0Cu);
    if (GetTickCount() - s_pitch_at < 200)
        return;
    if (*((volatile uint8_t *)g_xbox_mem_offset + pilot + MA2_PILOT_OFF))
        return;
    cam = LOOK_MEM32(pilot + MA2_PILOT_CAMERA);
    if (cam < 0x10000u || cam >= 0x04000000u)
        return;
    type = LOOK_MEM32(cam + MA2_ENTITY_TYPE);
    for (k = 0; k < (int)(sizeof s_cam_types / sizeof s_cam_types[0]); k++)
        if (type == LOOK_MEM32(MA2_TYPE_IDS + 4u * (uint32_t)k))
            tname = s_cam_types[k];
    third = type == LOOK_MEM32(MA2_TYPE_IDS + 4u * 3u);
    look_y = *((volatile uint8_t *)g_xbox_mem_offset + pilot + MA2_PILOT_ACTIONS
               + MA2_ACTION_LOOK_Y) != 0;
    why = !third ? 0 : look_y ? 1 : 2;
    if ((why != last_why || pilot != last_pilot || cam != last_cam || type != last_type)
            && shown++ < 50) {
        fprintf(stderr, "[LOOK] pilot update %08X (vtable %08X): pitch call silent; camera %08X is %s"
                        " (type %08X); look-y %s -> %s\n",
                pilot, LOOK_MEM32(pilot), cam, tname, type, look_y ? "on" : "off",
                why == 0 ? "mouse left to the stick" :
                why == 1 ? "mouse turns and tilts from here" : "mouse turns from here, no tilt");
    }
    last_why = why; last_pilot = pilot; last_cam = cam; last_type = type;
    if (third)
        look_apply(cam, 0.0f, look_y, 1);
}
