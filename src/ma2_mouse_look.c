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

extern ptrdiff_t g_xbox_mem_offset;
int mkb_mouse_look_take(float *yaw_deg, float *pitch_deg);

#define LOOK_MEMF(va)  (*(volatile float *)((uint8_t *)g_xbox_mem_offset + (va)))

#define MA2_TURN_STANDING   0x001002FBu   /* in sub_001002A3 */
#define MA2_TURN_WALKING    0x001739C5u   /* in sub_00172E96 */
#define MA2_BODY_RATE_SET   0x40u         /* the setter's this, from the body */
#define MA2_BODY_HEADING    0x64u         /* degrees, unwrapped */

void ma2_mouse_look_rate_set(uint32_t ret, uint32_t comp, float rate)
{
    static int mode = -1, dbg, shown;
    uint32_t body;
    float yaw;

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
    if (!mkb_mouse_look_take(&yaw, NULL) || yaw == 0.0f)
        return;
    LOOK_MEMF(body + MA2_BODY_HEADING) += yaw;
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
