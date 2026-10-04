/* The SDRAM mailbox helpers (two adjacent 16-bit words per 32-bit transaction), shared by the built-in Chladni (fw/chladni.inc) and the host side of the Chladni pack
 * (fw/meter_packs.inc, which hands them to the pack through the host table; in a PACKS_ONLY build the built-in meter is not compiled at all). */
#ifndef TAU_CHL_MAILBOX_INC
#define TAU_CHL_MAILBOX_INC
/* One 32-bit mailbox transaction = two adjacent 16-bit SDRAM words. Returns 0 on success, -1 on timeout. */
static int chl_mb_write(uint32_t word_addr, uint32_t data)
{
    REG(R_SDR_ADDR) = word_addr; REG(R_SDR_DATA) = data;
    REG(R_SDR_CTRL) = 0x3Fu;                                   /* start, write, all four lanes: the pattern blit_probe() proved */
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    return 0;
}

static int chl_mb_read(uint32_t word_addr, uint32_t *out)
{
    REG(R_SDR_ADDR) = word_addr; REG(R_SDR_CTRL) = 0x3Du;      /* start, read, all lanes */
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    *out = REG(R_SDR_RDATA);
    return 0;
}
#endif
