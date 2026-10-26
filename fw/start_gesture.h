/* Start-button gesture resolution (B-522): Start opens Settings on RELEASE, and only if nothing else claimed the press.
 *
 * Pure logic, no hardware: fw/player.c's poll_input() calls sg_step() once per poll with the rising edges, falling edges and held keys, and gets back the
 * edges to act on and whether the Start+Y chord fired. Host-tested by sim/test_start_gesture.py. Tagged as a structural input item for the Helios GUI
 * framework review (docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md section 8): press/release/chord resolution belongs in one input layer, not in
 * per-button special cases inside poll_input().
 *
 * Rules:
 *   - A Start press ARMS the gesture only when the player screen is showing (`closed`: no Settings, no library) and the menu can open (`menu_ok`) and Select
 *     is not held. While armed, the Start edge is withheld from the rest of the input code.
 *   - Any other button going down while Start is held CLAIMS the gesture: the release then does nothing. One physical gesture never produces two actions.
 *   - Start+Y (either order) fires `SG_CHORD`: both edges are consumed, the gesture is claimed.
 *   - Releasing Start while still armed re-presents it as an ordinary Start edge, which opens Settings.
 *   - Closing is not affected: with Settings or the library open a Start press is not armed, so its release can never reopen what the press just closed.
 */
#ifndef TAU_START_GESTURE_H
#define TAU_START_GESTURE_H
#include <stdint.h>

enum { SG_CHORD = 1u };

typedef struct { uint8_t pend; } sg_t;

/* `edge`/`fall`/`keys`: this poll's rising edges, falling edges and held keys. `start`, `y`, `select`: the bit masks for those buttons.
 * Returns SG_CHORD when Start+Y fired; *edge is rewritten to what the rest of the input code should see. */
static uint32_t sg_step(sg_t *g, uint32_t *edge, uint32_t fall, uint32_t keys, uint32_t start, uint32_t y, uint32_t select, int closed, int menu_ok)
{
    uint32_t out = 0u;
    if (*edge & start) g->pend = (uint8_t)(closed && menu_ok && !(keys & select));
    else if (*edge) g->pend = 0u;                                   /* another button went down while Start is held: it owns the gesture */
    if (closed && menu_ok && (((*edge & start) && (keys & y)) || ((*edge & y) && (keys & start)))) {
        *edge &= ~(start | y);
        g->pend = 0u;
        out |= SG_CHORD;
    }
    if (g->pend) {
        if (*edge & start) *edge &= ~start;                         /* armed: withhold the press */
        if (fall & start) { *edge |= start; g->pend = 0u; }         /* released unclaimed: present it as the opening edge */
    }
    return out;
}
#endif
