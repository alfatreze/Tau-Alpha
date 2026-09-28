/* ================================================================== SETTINGS
 * Volume, accent colour, repeat, shuffle, album art, meter and EQ preset,
 * remembered between sessions.
 *
 * The Pocket stores them, not this core. interact.json declares one `persist`
 * variable per setting at a word of a register file at 0x20000000; APF reads
 * those words back from the core every frame, lets the user adjust them in
 * Core Settings, writes them back, and saves them itself. Nothing here touches
 * the SD card.
 *
 * ---------------------------------------------------------------------------
 * DO NOT REPLACE THIS WITH A FILE WRITE. Two earlier mechanisms did, and both
 * damaged the user's music:
 *
 *   0184 target write   -- destroyed the library three times. Its bridge
 *       address is parsed as a struct whose second word APF reads as a SIZE,
 *       applied to whatever file occupies the MP3 slot.
 *   nonvolatile slot    -- hung the Pocket, at two different addresses.
 *       Isolated to the flag itself: the identical slot without it loaded fine.
 *
 * The full forensics, including the root cause -- this core was writing its
 * 0190 response struct over APF's dataslot ID/size table at word 0 -- are in
 * ROADMAP.md under Fixed. Read that before touching persistence again.
 * ---------------------------------------------------------------------------
 */

/* Set when something changes, cleared when it has been published. `set_poll_at`
 * throttles the check for changes made in the Core Settings menu -- APF
 * rewrites these words every frame and there is no reason to compare all seven
 * at the main loop's rate. */
static uint32_t set_dirty_at;
static uint32_t set_poll_at;

static void settings_mark_dirty(void) { set_dirty_at = cycles() | 1u; }


/* ---------------------------------------------------------------------------
 * PERSISTENCE, third mechanism and the first that cannot touch a data slot.
 *
 * interact.json declares one `persist` variable per setting, each at a word of
 * the register file at 0x20000000. Per Analogue: "Each frame, all of the UI
 * elements are read back from the core at a specified 32-bit BRIDGE address,
 * updated with new user-adjusted values, and finally written back", and
 * `persist` "retains the value set by the user after the core is shut down",
 * saved to /Settings/<core>/Interact/interact_persist.json.
 *
 * So APF does the storing, into its OWN file. Nothing here writes the card, and
 * no data slot is involved at any point. That is the entire reason for this
 * route: 0184 and the nonvolatile slot both reached the user's .mp3 files, three
 * libraries between them. This mechanism has no path to them.
 *
 * Because APF reads back from the core rather than only writing to it, a change
 * made with the BUTTONS is picked up and persisted too -- it is not merely a
 * menu of defaults.
 *
 * Direction is resolved by comparing against what we last published: if the
 * word APF hands back differs from that, the user moved it in the Core Settings
 * menu and we adopt it. Otherwise ours stands.
 *
 * ---------------------------------------------------------------------------
 * RULES FOR CHANGING interact.json IN A RELEASE. Read these before editing it.
 *
 * The persist file keys on `id`. Nothing else -- not the name, not the order,
 * not the address. A released core's saved settings survive an update only for
 * as long as those numbers hold still, and there is no versioning or migration
 * in APF to fall back on. Stable ids ARE the mechanism.
 *
 *   1. NEVER renumber an existing variable. Every updating user's stored value
 *      would land on whichever setting inherited its id -- their volume
 *      arriving as an accent colour, silently, on first launch.
 *
 *   2. RETIRING a setting: delete it and leave the id dead. The orphaned entry
 *      in the user's file is ignored. Do not close the gap.
 *
 *   3. ADDING one: take the next unused id, even when reusing a retired
 *      variable's ADDRESS. That is why resume took ids 18 and 19 while sitting
 *      at the addresses album art and screen blank had vacated -- reusing 14
 *      and 17 would have applied a stored "album art = 1" to Resume point, and
 *      every updating user would have resumed at a garbage position exactly
 *      once, on the launch right after updating.
 *
 *   4. Changing a `type` CANNOT be made safe. The list experiment wrote its
 *      type into the persist file and reverting interact.json was not enough
 *      to recover -- the file had to be deleted. Treat any type change as a
 *      settings reset and say so in the release notes.
 *
 *   5. Keep the ADDRESS stable too, separately from the id: it selects the
 *      register-file slot via bridge_addr[4:2], so moving it changes which
 *      firmware variable the word feeds.
 *
 *   6. ARRAY ORDER IS FREE, and it sets the Core Settings menu order.
 *      MEASURED 2026-08-12: moving "Saved position" to the end of the
 *      variables array moved it to the bottom of the menu and changed
 *      nothing else -- every stored value survived, with only a colour the
 *      user had pressed and a resume point that had advanced while playing.
 *      Neither had returned to a default, which is what a reset looks like.
 *
 *      This matters more than tidiness. Storage words that must exist but
 *      mean nothing to a user -- the saved position, and a playlist name if
 *      one is ever stored -- can sit AFTER the real settings instead of
 *      between Volume and EQ preset. There is no way to hide them (see the
 *      ROADMAP entry on `enabled`), so ordering is the only control there is
 *      over how much they intrude.
 *
 * The persist file is plain JSON on the card at
 * /Settings/<core>/Interact/_core/interact_persist.json. It is readable, it is
 * authoritative, and it answers persistence questions with no hardware round
 * trip -- reading it is what found the signed-int bug that made resume store
 * -1. Check it FIRST.
 * ---------------------------------------------------------------------------
 */
/* Slot 4 was the album-art panel and slot 7 the screen-blank timeout. Both are
 * now button-only and unpersisted, freeing the two slots resume needs -- the
 * index is 3 bits wide (`set_idx <= dDAT_MOSI[2:0]`), so eight is all there is
 * and a ninth would silently alias onto Volume.
 *
 * The ADDRESSES of the six survivors are unchanged, deliberately: renumbering
 * would have scrambled every already-saved setting on every card. */
/* SW_RETIRED_*: legacy playlist mode (Shuffle, Resume, the remembered playlist name/hash) and the
 * library on/off switch were all removed together (the library is now mandatory, so there is no
 * fallback left to switch to). Per this file's own rule 2 above, a retired id is deleted and left
 * DEAD, never renumbered and never reused for something else -- these placeholders keep every
 * later id's numeric value exactly where it was, so nothing after them shifts. Nothing reads or
 * writes these slots any more; an old persist file's values there are simply ignored. */
enum { SW_VOL = 0, SW_PAL, SW_REP, SW_RETIRED_SHUF, SW_RETIRED_RESUME, SW_VIZ, SW_EQ,
       SW_RETIRED_RESUMEON,
       /* rev 21 widened the index to 4 bits; these three were the remembered playlist's name stem,
        * and the one after them its full-name hash (playlists.m3u lookup). */
       SW_RETIRED_PLN0, SW_RETIRED_PLN1, SW_RETIRED_PLN2,
       SW_RETIRED_PLHASH,
       /* rev 22 library: what was playing, the index it refers to, the Shuffle All seed, and (retired) the on/off switch. */
       SW_LIBH, SW_LIBB, SW_LIBS, SW_RETIRED_LIBOFF,
       /* B-346: the register file widened 16 -> 32 words (see core_game.vh's set_reg). These five are
        * the first of the sixteen new slots -- theme index and polarity (session-only until now, per
        * docs/THEME_SPEC.md section 5's own open item), and the current preset of each Configurable
        * meter (docs/METER_CONFIG_SPEC.md's own "no persistence yet" gap). Not range-checked against
        * PERSIST32_READY() here -- every read/write of these five goes through set_rd32()/set_wr32()
        * below instead of the plain set_rd()/set_wr(), which do that gating once, not five times. A
        * hand-tuned CUSTOM meter (every value, not just which preset) still has no persistence path;
        * that stays the QR-export workaround B-216 already settled on. */
       SW_THEME, SW_POL, SW_MPRE_BARS, SW_MPRE_SCOPE, SW_MPRE_CHLADNI,
       SW_N };

/* What we last published. Initialised to a value APF can never write for
 * these ranges, so the FIRST poll always counts as a change and adopts
 * whatever APF has -- we must not publish before adopting, or our defaults
 * would overwrite the persisted values on the way past. */
static uint32_t set_pub[SW_N] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                  0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                  0xFFFFFFFFu, 0xFFFFFFFFu,
                                  0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                  0xFFFFFFFFu
                                  , 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
                                  , 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
                                  };

static uint32_t set_rd(uint32_t w) { REG(R_SET_IDX) = w; return REG(R_SET_DAT); }
static void     set_wr(uint32_t w, uint32_t v)
{
    REG(R_SET_IDX) = w;
    REG(R_SET_DAT) = v;
    set_pub[w] = v;
}

/* B-346: whether slots 16-31 are their own, independent words -- true on any bitstream carrying the
 * 32-word register file, false on an older 16-word one, where writing index 16 aliases (via the old
 * 4-bit set_idx truncating the top bit) onto slot 0 (Volume) and corrupts it. Cannot be answered by
 * "write 16, read 16 back, check it matches": on the OLD hardware BOTH the write and the read alias
 * onto the SAME slot 0, so that check would falsely pass. The real test: slot 0's value must survive a
 * write to slot 16 untouched. Probed once, at boot, before settings_load() ever reads/writes the new
 * slots -- never later, since nothing about this changes at runtime. */
static uint8_t persist32_ok;
static uint8_t persist32_probed;

static void persist32_probe(void)
{
    uint32_t save0 = set_rd(0u);
    uint32_t probe0 = save0 ^ 0x5A5A5A5Au;         /* a value slot 0 does not already hold */
    REG(R_SET_IDX) = 0u; REG(R_SET_DAT) = probe0;  /* raw write: set_wr() would also update set_pub[0], not wanted for a probe */
    REG(R_SET_IDX) = 16u; REG(R_SET_DAT) = ~probe0;
    uint32_t back0 = set_rd(0u);
    persist32_ok = (back0 == probe0) ? 1u : 0u;    /* unchanged by the slot-16 write == the two are independent */
    REG(R_SET_IDX) = 0u; REG(R_SET_DAT) = save0;   /* restore -- this probe must leave no trace either way */
}
#define PERSIST32_READY() (persist32_ok != 0u)

static void persist32_probe_ensure(void)
{
    if (!persist32_probed) { persist32_probe(); persist32_probed = 1u; }
}

/* Gated read/write for the five new slots: falls back to a fixed default (0, "theme 0 / Dark / no
 * saved preset") on a bitstream that does not have them, instead of risking the slot-0 aliasing above. */
static uint32_t set_rd32(uint32_t w) { persist32_probe_ensure(); return PERSIST32_READY() ? set_rd(w) : 0u; }
static void     set_wr32(uint32_t w, uint32_t v) { persist32_probe_ensure(); if (PERSIST32_READY()) set_wr(w, v); }

/* Publish every current value. Called at boot after adopting, and whenever a
 * setting changes. Two MMIO writes per word -- nothing to debounce. */
#if TAU_DIAGNOSTIC
/* fw/suite.inc keeps its four-word summary in the words the (now-retired) legacy playlist
 * name/hash used (SW_RETIRED_PLN0..SW_RETIRED_PLHASH). While a summary is held it is what gets
 * published there instead; a summary is recognised by (word & 15) == 1 with that character below
 * 0x20. Those four ids stay dedicated to this Diagnostic Build use, not reclaimed as ordinary
 * settings, since a release build now never writes them at all. */
static uint8_t  chk_rec_on;
static uint32_t chk_w[4];
#endif
static int settings_store(void)
{
    set_wr(SW_VOL,  volume);
    set_wr(SW_PAL,  ui_pal_idx);
    set_wr(SW_REP,  rep_mode);
    set_wr(SW_VIZ,  (viz_mode == VIZ_BARS && bars_layout) ? VIZ_RETIRED_MIRROR : viz_mode);
    set_wr(SW_EQ,   eq_idx);
#if TAU_DIAGNOSTIC
    if (chk_rec_on) { for (uint32_t k = 0; k < 4u; k++) set_wr(SW_RETIRED_PLN0 + k, chk_w[k] & 0x7FFFFFFFu); }
#endif
    set_wr(SW_LIBH, lib_h & 0x7FFFFFFFu);
    set_wr(SW_LIBB, lib_hb & 0x7FFFFFFFu);
    set_wr(SW_LIBS, lib_hs & 0x7FFFFFFFu);
    set_wr32(SW_THEME, th_theme);
    set_wr32(SW_POL,   th_pol ? 1u : 0u);
    set_wr32(SW_MPRE_BARS,    mtr_pi_winamp_bars);
    set_wr32(SW_MPRE_SCOPE,   mtr_pi_winamp_scope);
    set_wr32(SW_MPRE_CHLADNI, mtr_pi_chladni);
    return 1;
}

/* Take whatever APF has for us -- the persisted values at boot, or a menu
 * change at runtime. Every field is range-checked: a value from a file on the
 * card must not be able to silence the player or index off a palette. */
static int settings_load(void)
{
    uint32_t v;

    /* ALL ZERO means APF has not written yet -- the words power up at zero and
     * the core can reach this before APF's first pass. Adopting that would set
     * the volume to 0 and hand the user a silent player that looks broken.
     * An all-default state is indistinguishable from an unwritten one, so the
     * firmware defaults stand and the next poll picks up the real values. */
    uint32_t any = 0;
    for (uint32_t w = 0; w < SW_N; w++) any |= set_rd(w);
    if (!any) return 0;

    v = set_rd(SW_VOL);   if (v <= VOL_MAX)      { volume = v; vol_apply(); }
    v = set_rd(SW_PAL);   if (v < UI_PALETTE_N)  { ui_pal_idx = v;
                                                   ui_accent = th_accent_of(v); }
    v = set_rd(SW_REP);   if (v <= REP_ONE)        rep_mode = (uint8_t)v;
    /* Only modes in viz_order[] are restored, not just a range check: a settings word saved with a RETIRED meter (cassette, magic eye,
     * L/R levels) must not select a mode that no longer has a drawing block (see viz_sel_to_mode(), fw/player.c). */
    v = set_rd(SW_VIZ);
    if (v == VIZ_RETIRED_MIRROR) { viz_mode = VIZ_BARS; bars_layout = 1u; }          /* the saved form of BARS in its mirrored layout */
    else if (v < VIZ_COUNT && viz_selectable(v))  viz_mode = (uint8_t)v;
    v = set_rd(SW_EQ);    if (v < EQ_COUNT)      { eq_idx = (uint8_t)v;
                                                   REG(R_EQ) = eq_idx; }
    /* Retired ids (Shuffle, Resume, Resume point): nothing here reads them into any firmware
     * variable any more, but set_pub[] must still be kept in sync with whatever APF/the persist
     * file holds for them -- otherwise settings_poll_menu()'s comparison sees them permanently
     * "changed" against the 0xFFFFFFFF init sentinel and re-triggers a reload/store every single
     * poll, forever (the exact spurious-poll trap this file's own rev-21 comment warns about for
     * a different case). This is the read-only echo that keeps them dead without that side effect. */
    set_pub[SW_RETIRED_SHUF]     = set_rd(SW_RETIRED_SHUF);
    set_pub[SW_RETIRED_RESUME]   = set_rd(SW_RETIRED_RESUME);
    set_pub[SW_RETIRED_RESUMEON] = set_rd(SW_RETIRED_RESUMEON);
#if TAU_DIAGNOSTIC
    chk_rec_on = 0u;
    { uint32_t w0 = set_rd(SW_RETIRED_PLN0);
      if ((w0 & 15u) == 1u && (w0 & 0x7Fu) < 0x20u) { chk_rec_on = 1u; for (uint32_t k = 0; k < 4u; k++) chk_w[k] = set_rd(SW_RETIRED_PLN0 + k); } }
    if (!chk_rec_on) {
        for (uint32_t k = 0; k < 4u; k++) set_pub[SW_RETIRED_PLN0 + k] = set_rd(SW_RETIRED_PLN0 + k);
    }
#else
    for (uint32_t k = 0; k < 4u; k++) set_pub[SW_RETIRED_PLN0 + k] = set_rd(SW_RETIRED_PLN0 + k);
#endif
    lib_h = set_rd(SW_LIBH) & 0x7FFFFFFFu;      /* validated where used: against the index and its build id */
    lib_hb = set_rd(SW_LIBB) & 0x7FFFFFFFu;
    lib_hs = set_rd(SW_LIBS) & 0x7FFFFFFFu;
    set_pub[SW_RETIRED_LIBOFF] = set_rd(SW_RETIRED_LIBOFF);
    /* B-346: on a bitstream without the 32-word register file, set_rd32() returns 0 for all five --
     * theme 0 (TAU), Dark, no saved preset for any of the three meters -- exactly today's defaults, so
     * this degrades to "nothing persisted" rather than to a wrong or corrupted value. */
    v = set_rd32(SW_THEME); if (v < TH_COUNT()) th_theme = (uint8_t)v;
    th_pol = set_rd32(SW_POL) ? 1u : 0u;
    v = set_rd32(SW_MPRE_BARS);    if (v < mtr_d_winamp_bars.npre)  mtr_apply_preset(&mtr_d_winamp_bars,  v);
    v = set_rd32(SW_MPRE_SCOPE);   if (v < mtr_d_winamp_scope.npre) mtr_apply_preset(&mtr_d_winamp_scope, v);
    v = set_rd32(SW_MPRE_CHLADNI); if (v < mtr_d_chladni.npre)      mtr_apply_preset(&mtr_d_chladni,      v);
    th_apply();
    settings_adopted = 1u;
    return 1;
}

/* Watch for changes made in the Core Settings menu while running. APF rewrites
 * every word each frame, so a word differing from what we published is the
 * user having moved it. */
static int settings_poll_menu(void)
{
    int changed = 0;
    /* B-346: SW_THEME.. (16+) are gated -- on a bitstream without the wide register file, set_rd(w)
     * there aliases onto an early slot (e.g. Volume) and would never match set_pub[w]'s 0xFFFFFFFF
     * sentinel, forcing a spurious reload/store EVERY poll. persist32_probe_ensure() is cheap (a boot-
     * time probe, this call is just the flag check after the first). */
    uint32_t lim = SW_RETIRED_LIBOFF + 1u;   /* the original 16; SW_THEME is the first of the new ones */
    for (uint32_t w = 0; w < lim; w++)
        if (set_rd(w) != set_pub[w]) { changed = 1; break; }
    if (!changed && PERSIST32_READY())
        for (uint32_t w = lim; w < SW_N; w++)
            if (set_rd(w) != set_pub[w]) { changed = 1; break; }
    if (!changed) return 0;
    settings_load();
    settings_store();          /* re-publish so pub[] tracks again */
    return 1;
}

/* Called from the main loop. Does nothing until the settings have been stable
 * for SET_QUIET, so a held volume button costs one write rather than a dozen.
 * Deliberately not called while a track is loading -- see the call site. */
/* Writes are DEFERRED until playback is not running.
 *
 * The tic on every settings change is not the length of the write. Touching a
 * different data slot makes APF drop its fragment cache for the MP3 slot, so
 * the NEXT refill has to re-walk the FAT cluster chain -- the cost lands after
 * the write has finished, which is why banking audio in the FIFO first did not
 * help. It is the same behaviour target_flush_slot_cache() exists to exploit,
 * and the reason that function is marked load-time-only.
 *
 * So the write waits for a moment when there is no stream to disturb: paused,
 * stopped, or the OS menu opening -- which is also the moment before the core
 * is left, so a change made while playing is still saved on the way out.
 */

static void settings_pump(void)
{
    /* A change of ours: publish it. Immediate -- the quiet window and the
     * wait-for-paused both existed to keep an SD round trip away from the
     * decoder, and there is no SD access here at all now, just MMIO writes. */
    if (set_dirty_at) {
        set_flush_now = 0;
        set_dirty_at  = 0;
        settings_store();
        return;
    }

    /* Otherwise watch for a change made in the Core Settings menu. Throttled:
     * APF rewrites these words every frame and there is no reason to compare
     * seven of them at the main loop's rate. */
    if ((int32_t)(cycles() - set_poll_at) < 0) return;
    set_poll_at = cycles() + CLK_HZ / 10u;
    if (settings_poll_menu()) {
        ui_accent_changed = 1u;
        ui_mode_dirty     = 1u;
        ui_wave_force     = 1u;
    }
}
