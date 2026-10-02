/* ================================================================== FILE OPEN (was PLAYLIST)
 * Opens a named file into an APF data slot using APF's 0192 "open file into data slot"
 * command. This used to be the legacy .m3u-playlist-file loading path (reading a playlist
 * off the card, parsing it, walking it) as well as the library's own file-open primitive;
 * the legacy playlist-file mode itself has been removed (the media library is now the only
 * way to play more than one file), but this file-open mechanism is still load-bearing: the
 * media library (fw/library.inc) opens every track BY THIS SAME ROUTE, one absolute path at
 * a time (pl_open_name() -> pl_open_into() -> pl_open_try()).
 *
 * 0192 was already a valid selector in tgt_cmd.v and the datatable already had a CPU write
 * port -- both were built for the reload work and left unused, so nothing in the RTL changes
 * and the bitstream is untouched.
 *
 * There is no directory-listing command in APF. A core fundamentally cannot enumerate a
 * folder, which is why a track has to be named explicitly (by the library's own index, now)
 * rather than found by scanning the card.
 */

/* The template slot for every open-by-name (0192): the media library index, data slot 5 (LIB_SLOT_ID in library.inc, which is
 * included after this file). It always has a descriptor APF built for a real file whenever a library is loaded, and a library is
 * the only reason anything is opened by name. This used to be data slot 3, the legacy playlist slot, kept declared only for its
 * default file playlist.m3u; that slot is gone (B-517), and with it the need for a root playlist.m3u on the card. */
#define PL_TEMPLATE_SLOT_ID   5u

/* ---------------------------------------------------------------------------
 * The 0192 parameter struct, WITHOUT guessing its layout.
 *
 * Analogue documents 0192 as taking "filename and flag/size" in the parameter
 * struct, but not the field order, widths or padding -- and guessing a struct
 * layout is exactly the class of mistake that cost several hardware rounds on
 * the reload bug. So this never constructs one.
 *
 * Instead: 0190 (getfile) asks APF to write the CURRENT file's descriptor into
 * the response struct. That is a real, correctly-formed struct in whatever
 * layout this firmware version uses. Copy it verbatim into the parameter
 * struct, overwrite just the filename, and hand it back. Every field that is
 * not the name keeps the value APF itself produced.
 *
 * The name is located the same way slot_filename() locates it -- the longest
 * run of printable ASCII -- so even the name's OFFSET is observed rather than
 * assumed.
 */
/* Word addresses inside mf_datatable, which is 256 WORDS deep. These MUST
 * match target_buffer_resp_struct / target_buffer_param_struct in
 * core_game.vh, and must not overlap: the parameter struct is built from the
 * response struct, so an overlap is a read-after-write on itself. The param
 * struct was originally at word 256 -- past the end of a 256-word RAM, so the
 * address wrapped and landed straight back on the response struct. */
/* MUST stay above APF's dataslot ID/size table, which occupies the START of
 * this BRAM as {slot_id, size} pairs -- measured, see core_game.vh. The
 * response struct used to sit at word 0 and APF overwrote the table with it on
 * every 0190. */
/* DT_RESP_W / DT_PARAM_W / DT_WORDS are defined in player.c, above
 * slot_file_id() -- which needs them and is compiled before this file. */

static inline void dt_write(uint32_t word, uint32_t val)
{
    REG(R_DT_ADDR) = word;
    REG(R_DT_DATA) = val;
}

/* Set when the last pl_cmd() gave up on the deadline rather than being answered.
 *
 * Both outcomes return 0, but they cost wildly different amounts of time and
 * mean different things. An answered command with a nonzero result is instant
 * and describes THIS file; a timeout burns the whole timeout and says the
 * command path itself is not responding, which the next file will not fix. */
static uint8_t pl_cmd_timedout;

/* Run one target command and wait for the sequence counter to move. Same
 * discipline as target_read_poll(): never trust the `done` LEVEL, which stays
 * asserted from the previous command until the next one is picked up. */
static int pl_cmd(uint32_t sel, uint32_t slot, uint32_t timeout_cycles)
{
    refill_drain();                       /* one command in flight, ever */
    pl_cmd_timedout = 0;
    uint32_t seq0 = (REG(R_TGT_GO) >> 8) & 0xFFu;
    REG(R_TGT_ID) = slot;
    REG(R_TGT_GO) = sel;

    uint32_t deadline = cycles() + timeout_cycles;
    for (;;) {
        uint32_t st = REG(R_TGT_GO);
        if (((st >> 8) & 0xFFu) != seq0) return (((st >> 2) & 7u) == 0u);
        if ((int32_t)(cycles() - deadline) >= 0) { pl_cmd_timedout = 1; return 0; }
    }
}

/* Ask APF to put `name` in the MP3 slot. Returns 0 if the command failed --
 * the caller stays on the current track rather than leaving the player in a
 * state where nothing is loaded. */
/* Why the last open failed, surfaced in the toast. Guessing between these is
 * exactly what cost several rounds on the reload bug -- each has a different
 * cause and a different fix, and they are indistinguishable from outside. */
enum { PLO_OK = 0, PLO_GETFILE, PLO_NONAME, PLO_TOOLONG, PLO_OPEN };
static uint8_t pl_open_err;
static uint8_t pl_open_errcode;      /* target_dataslot_err from 0192 */
static uint8_t pl_open_timeout;      /* 0192 never answered; errcode is stale  */

/* The result code 0192 returns for a file that is not on the card. Established
 * by observation, not from the docs: pointing the open at a bare filename that
 * did not exist at the card root is what produced it, repeatably, during the
 * playlist bring-up (see the path-handling note in pl_open_name). It is the one
 * code that justifies giving up on an entry for good. */
#define PL_ERRCODE_NOFILE 4u

static int pl_open_name(const char *name);   /* -> pl_open_into(MP3 slot) */

/* Fetch a template descriptor into `buf`. Returns the length of the printable
 * path it contains, or 0.
 *
 * Either slot will do. The MP3 slot is preferred because its path is the folder
 * the current track came from, but at launch it may hold nothing at all -- the
 * core no longer forces a file to be picked before it starts. The
 * PL_TEMPLATE_SLOT_ID slot (the library index) is filled by APF at boot (data.json
 * declares its file) and sits in the core's folder on the card, so it is the
 * template of record. */
COLD_FN2 static uint32_t pl_template(uint32_t slot, uint8_t *buf, uint32_t *name_at)
{
    if (!pl_cmd(TGT_GETFILE, slot, CLK_HZ)) return 0;

    for (uint32_t w = 0; w < DT_WORDS; w++) {
        uint32_t v = dt_read(DT_RESP_W + w);
        /* BIG-endian: the bridge byte-swaps. */
        buf[w*4 + 0] = (uint8_t)(v >> 24);
        buf[w*4 + 1] = (uint8_t)(v >> 16);
        buf[w*4 + 2] = (uint8_t)(v >> 8);
        buf[w*4 + 3] = (uint8_t)v;
    }

    uint32_t best = 0, best_len = 0, i = 0;
    while (i < DT_WORDS * 4u) {
        uint32_t start = i;
        while (i < DT_WORDS * 4u && buf[i] >= 0x20u && buf[i] < 0x7Fu) i++;
        if (i - start > best_len) { best_len = i - start; best = start; }
        i++;
    }
    *name_at = best;
    return best_len;
}

/* Open `name` into ANY slot (the MP3 slot for tracks, the cover slot for TIM1 images). The target is
 * the slot argument; the descriptor template comes from tmpl_slot. */
COLD_FN2 static int pl_open_try(uint32_t slot, const char *name, uint32_t tmpl_slot)
{
    pl_open_err = PLO_OK; pl_open_errcode = 0; pl_open_timeout = 0;

    /* 1. Template: a struct APF built itself, for a file it already has. */
    uint8_t buf[DT_WORDS * 4u];
    uint32_t best = 0, best_len = pl_template(tmpl_slot, buf, &best);
    if (!best_len) { pl_open_err = PLO_NONAME; return 0; }


    /* 3. If the word immediately before the name holds the OLD name's length,
     *    it is a length field and has to move with the name. Checked rather
     *    than assumed, and only accepted in that one adjacent position -- a
     *    scan of the whole struct for "any word equal to the length" would
     *    eventually match something unrelated and corrupt it. */
    /* The slot's current entry is a full PATH ("/Assets/tau/common/x.mp3"
     * -- the 0190 work saw exactly that), while the name passed in here is
     * either a bare filename or, from the library, an absolute path.
     * Replacing the whole path with a bare name asks APF to open a file that
     * does not exist at the root, which is what result code 4 was
     * reporting. So KEEP the directory the slot is already using and swap only
     * the last component. An entry that starts with '/' is taken as absolute
     * and replaces the path outright -- which is how the library, which
     * always names an absolute path, uses this.
     *
     * The directory is read off the observed response rather than hardcoded --
     * a track can live anywhere, and this way it follows the slot. */
    uint32_t dir_len = 0;
    if (name[0] != '/') {
        for (uint32_t k = 0; k < best_len; k++)
            if (buf[best + k] == '/') dir_len = k + 1u;
    }

    uint32_t nm_len = 0;
    while (name[nm_len]) nm_len++;
    uint32_t new_len = dir_len + nm_len;
    if (new_len >= sizeof(buf) - best) { pl_open_err = PLO_TOOLONG; return 0; }

    uint32_t len_w = (best >= 4u) ? ((best - 4u) / 4u) : 0xFFFFFFFFu;
    int patch_len = 0;
    if (len_w != 0xFFFFFFFFu) {
        uint32_t v = ((uint32_t)buf[len_w*4+0] << 24) | ((uint32_t)buf[len_w*4+1] << 16) |
                     ((uint32_t)buf[len_w*4+2] << 8)  |  (uint32_t)buf[len_w*4+3];
        if (v == best_len) patch_len = 1;
    }

    /* 4. Overwrite the name, and clear the tail of the old one so a shorter
     *    name cannot leave the previous file's characters trailing behind it. */
    for (uint32_t k = dir_len; k < best_len || k < new_len; k++) {
        if (best + k >= sizeof(buf)) break;
        buf[best + k] = (k < new_len) ? (uint8_t)name[k - dir_len] : 0u;
    }
    if (best + new_len < sizeof(buf)) buf[best + new_len] = 0u;

    if (patch_len) {
        buf[len_w*4 + 0] = (uint8_t)(new_len >> 24);
        buf[len_w*4 + 1] = (uint8_t)(new_len >> 16);
        buf[len_w*4 + 2] = (uint8_t)(new_len >> 8);
        buf[len_w*4 + 3] = (uint8_t)new_len;
    }

    /* 5. Hand it back and fire 0192. */
    for (uint32_t w = 0; w < DT_WORDS; w++) {
        uint32_t v = ((uint32_t)buf[w*4+0] << 24) | ((uint32_t)buf[w*4+1] << 16) |
                     ((uint32_t)buf[w*4+2] << 8)  |  (uint32_t)buf[w*4+3];
        dt_write(DT_PARAM_W + w, v);
    }

    /* This switches the slot but raises NO 008A -- that notification is for a
     * file the USER picked from the menu, not one the core opened. The caller
     * therefore has to drive the load itself; see pl_arm_load(). */
    if (!pl_cmd(TGT_OPENFILE, slot, CLK_HZ * 3u)) {
        pl_open_err     = PLO_OPEN;
        pl_open_timeout = pl_cmd_timedout;
        pl_open_errcode = (uint8_t)((REG(R_TGT_GO) >> 2) & 7u);
        return 0;
    }
    return 1;
}

/* The PL_TEMPLATE_SLOT_ID slot (the library index) is the template of record, and
 * the MP3 slot is the fallback.
 *
 * Only the directory of the template matters here: the last component is
 * replaced either way (or the whole path, for an absolute name such as the
 * library always passes). Both orders are still tried. If the preferred
 * template produces a path APF will not open, the other is worth a go before
 * giving up -- except on PL_ERRCODE_NOFILE, which means the entry genuinely
 * is not there and no directory will change that. */
COLD_FN2 static int pl_open_into(uint32_t slot, const char *name)
{
    if (pl_open_try(slot, name, PL_TEMPLATE_SLOT_ID)) return 1;
    if (pl_open_errcode == PL_ERRCODE_NOFILE) return 0;
    return pl_open_try(slot, name, MP3_SLOT_ID);
}

/* --------------------------------------------------------------- bad entries
 *
 * What a failed open means for the REST of a queue. Getting this wrong in
 * either direction loses music: treat a transient failure as permanent and a
 * track that would play is dropped for the session; treat a permanent one as
 * transient and every pass re-attempts a file that is not there.
 */
enum {
    PLF_DEAD = 0,       /* this entry is not on the card: never try it again  */
    PLF_SKIP,           /* this entry failed, but might not next time         */
    PLF_FATAL           /* not about this entry at all: stop trying entries   */
};

COLD_FN2 static uint8_t pl_classify_failure(void)
{
    switch (pl_open_err) {
    /* No template from EITHER slot. That is the 0190 path failing, which has
     * nothing to do with which file was asked for -- the next entry produces
     * exactly the same result, so walking a queue is pure waste. */
    case PLO_GETFILE:
    case PLO_NONAME:
        return PLF_FATAL;

    /* The name cannot be expressed in the parameter struct at all. Its length
     * will not change, so this is as permanent as a missing file. */
    case PLO_TOOLONG:
        return PLF_DEAD;

    case PLO_OPEN:
        /* Answered, and answered "no such file". The only permanent verdict
         * APF actually gives us. */
        if (!pl_open_timeout && pl_open_errcode == PL_ERRCODE_NOFILE)
            return PLF_DEAD;
        /* Never answered. Cheap to misjudge one way, ruinous the other: each
         * timeout costs the full 3 s, so a long queue would freeze the core
         * for minutes before reaching the idle screen. Stop. */
        if (pl_open_timeout)
            return PLF_FATAL;
        /* Answered with something else -- busy, or a state we have not seen.
         * Move past it for now, but do not condemn it. */
        return PLF_SKIP;
    }
    return PLF_SKIP;
}

/* Why the LAST attempt failed. Only reached when a caller has given up
 * on the whole request -- a single bad entry it steps over says nothing. */
COLD_FN2 static void pl_toast_open_fail(void)
{
    switch (pl_open_err) {
    case PLO_GETFILE: ui_toast_msg("OPEN FAIL: NO 0190");   break;
    case PLO_NONAME:  ui_toast_msg("OPEN FAIL: NO NAME");   break;
    case PLO_TOOLONG: ui_toast_msg("OPEN FAIL: NAME LONG"); break;
    default:          ui_toast_set("OPEN FAIL: 0192 ERR",
                                   pl_open_errcode, 0);     break;
    }
}

/* Everything that has to follow a SUCCESSFUL open, and nothing that survives a
 * failed one -- which is what lets a caller attempt an entry without
 * committing to it.
 *
 * A successful 0192 switches the slot but raises NO 008A -- that notification
 * is for a file the USER picked from the menu. So nothing here can wait for the
 * reload path to notice: without driving it, file_pos, the decoder and the ring
 * all carry over and the new track starts at the previous one's position. That
 * is the same fault the original "Load MP3 resumed mid-song" bug had, arriving
 * by a different route.
 *
 * Rather than re-implement loading, arm the very same gate the menu reload
 * uses, with the OUTGOING file's identity as the reference so 0190 can confirm
 * the switch actually happened before the track is loaded. */
COLD_FN2 static void pl_arm_load(uint32_t leaving_id)
{
    for (uint32_t i = 0; i < sizeof(stale_ref_title); i++)
        stale_ref_title[i] = last_title[i];
    stale_ref_size    = slot_size;
    stale_ref_file_id = leaving_id;

    /* R_SLOT_SZ is only latched by an 008A edge, so it still describes the
     * track being left. Measure the new one instead of inheriting that. */
    force_size_probe = 1;
    slot_size        = 0;

    reload_pending  = 0;
    reload_armed    = 1;
    reload_probe_at = cycles();
    /* Shorter settle than a menu pick: 0192 has already reported success, so
     * the slot is open rather than merely chosen. */
    reload_settle   = cycles() + CLK_HZ / 4u;
    reload_at       = cycles() + CLK_HZ * 3u;
}

static int pl_open_name(const char *name)
{
    return pl_open_into(MP3_SLOT_ID, name);
}

/* -------------------------------------------------------- SDRAM CPU window
 * A general-purpose proven-safe scratch region for cold-code helpers, at
 * 0xA0100000 (linker section .sdram). On a bitstream without the CPU window
 * that alias decodes to MMIO, so no store may be issued until the window is
 * proven: write a pattern through the Phase 1 mailbox (physical byte 1 MiB =
 * mailbox word 0x80000) and read it back through the window (a read, harmless
 * on a wrong bitstream), then exercise the first and last word of the region
 * with two patterns. Runs once; the verdict is kept.
 *
 * This used to exist only for the (now removed) legacy .m3u text buffer; it
 * is kept because it is now shared scratch: fw/suite.inc's Blit Test crumb
 * trail (BT_CRUMB_BASE) and the general SDRAM-window Check tests (CT_SDW/
 * CT_SDC) all use this same proven window. */
#define PL_SDRAM_BASE   0xA0100000u
#define PL_SDRAM_BYTES  0x3400u             /* the whole .sdram linker region */
static uint8_t pl_sdram_state;                  /* 0 untested, 1 proven, 2 failed */
static uint32_t pl_win_rd;                      /* cycles for one window read (Info page) */

static int pl_sdram_prove(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    volatile uint32_t *w0 = (volatile uint32_t *)(uintptr_t)PL_SDRAM_BASE;
    volatile uint32_t *w1 = (volatile uint32_t *)(uintptr_t)(PL_SDRAM_BASE + PL_SDRAM_BYTES - 4u);
    uint32_t t = cycles();

    REG(R_SDR_ADDR) = 0x00080000u; REG(R_SDR_DATA) = 0x504C5342u;   /* "PLSB" */
    REG(R_SDR_CTRL) = 0x3Fu;
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return 0;
    uint32_t t1 = cycles();
    uint32_t got = *w0;
    pl_win_rd = cycles() - t1;
    if (got != 0x504C5342u) return 0;
    for (uint32_t k = 0; k < 2u; k++) {
        *w0 = pat[k];  *w1 = ~pat[k];
        if (*w0 != pat[k] || *w1 != ~pat[k]) return 0;
    }
    return 1;
}

COLD_FN2 static int pl_sdram_ready(void)
{
    if (!pl_sdram_state) pl_sdram_state = pl_sdram_prove() ? 1u : 2u;
    return pl_sdram_state == 1u;
}
