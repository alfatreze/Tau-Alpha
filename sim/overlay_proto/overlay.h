/* PROTOTYPE of the mode-overlay region (docs/features/MODE_OVERLAY_PROPOSAL.md). Host only: nothing in fw/ includes this file. It models the safety rules the
 * firmware version would need, so they can be tested (and the tests proven to catch violations) before any firmware is touched.
 *
 * One region, several OWNERS that are never live at the same time. Rules:
 *   R1  only the current owner may touch the region (ovl_ptr() checks the owner),
 *   R2  switching owner runs the new owner's init, never the old owner's state: the region is POISONED first (a stale read shows up as 0xA5A5, not as plausible data),
 *   R3  a switch is refused while the old owner is BUSY (an in-flight job: a blit reading the scratch, a decode using the staging area),
 *   R4  every owner declares its size at compile time and the region is the largest: a bigger owner is a compile error, not an overlap. */
#ifndef OVL_PROTO_H
#define OVL_PROTO_H
#include <stdint.h>
#include <string.h>
#ifndef OVL_BYTES
#define OVL_BYTES 2816u
#endif
typedef enum { OVL_NONE = 0, OVL_CHLADNI, OVL_SCOPE, OVL_LAYERED, OVL_N } ovl_owner_t;
typedef void (*ovl_init_t)(void *region);
typedef struct { uint8_t region[OVL_BYTES] __attribute__((aligned(8))); ovl_owner_t owner; uint8_t busy; uint32_t switches, violations; } ovl_t;
static ovl_t OVL;
#ifdef OVL_BUG_NO_POISON
#define OVL_POISON(r) ((void)0)
#else
#define OVL_POISON(r) memset((r), 0xA5, OVL_BYTES)
#endif
/* Claim the region for `who`: poison, then run its init. Returns 0 and counts a violation if the old owner is busy. */
static int ovl_claim(ovl_owner_t who, ovl_init_t init)
{
    if (OVL.owner == who) return 1;
#ifndef OVL_BUG_IGNORE_BUSY
    if (OVL.busy) { OVL.violations++; return 0; }
#endif
    OVL_POISON(OVL.region);
    OVL.owner = who; OVL.switches++;
    if (init) init(OVL.region);
    return 1;
}
static void *ovl_ptr(ovl_owner_t who)
{
#ifndef OVL_BUG_NO_OWNER_CHECK
    if (OVL.owner != who) { OVL.violations++; return 0; }
#endif
    return OVL.region;
}
#define OVL_SIZE_CHECK(T) _Static_assert(sizeof(T) <= OVL_BYTES, "overlay owner larger than the region")
#endif
