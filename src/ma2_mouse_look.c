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
 * stick -- the rate the game sets comes from the pad alone. Vertical
 * movement still drives the stick's y: the camera integrates its pitch
 * elsewhere, from the smoothed look y (heap 0x0240F4E0), and that has not
 * been found yet. Outside the walker (menus, vehicles, on foot) nothing
 * asks, and after half a second the mouse drives the stick on both axes.
 *
 * MA2_MOUSE_LOOK=0 restores the stick path. MA2_LOOK_DEBUG=1 logs turns.
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
