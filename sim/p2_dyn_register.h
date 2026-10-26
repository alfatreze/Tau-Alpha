/* For the host harnesses that include fw2/ui/meters.c with P2_PACKS and host slot buffers (slot_mem): registers pack-only rows from descriptor files (what tools/player2/meter_desc.py builds), each in the slot its
 * own header names, as if its pack had been installed: the image is a dummy followed by the descriptor, status OK. Include AFTER meters.c. Returns how many rows came up. */
static uint32_t dyn_load_files(int n, char **files)
{
    uint32_t up = 0;
    for (int a = 0; a < n; a++) {
        FILE *f = fopen(files[a], "rb"); static uint8_t desc[4096]; const uint32_t len = f ? (uint32_t)fread(desc, 1, sizeof desc, f) : 0u; if (f) fclose(f);
        if (len < 40u) continue;
        const uint32_t slot = (uint32_t)desc[34] | ((uint32_t)desc[35] << 8);
        if (slot < METER_N || slot >= METER_SLOTS) continue;
        const uint32_t size = 64u + len;
        for (uint32_t i = 0; i < len; i++) slot_mem[slot][size - len + i] = desc[i];
        pack_slot[slot].info.size = size; pack_slot[slot].info.entry = MTR_PACK_SLOT_BASE + slot * MTR_PACK_SLOT_SIZE; pack_slot[slot].status = MPK_OK; dyn_register(slot);
        up += (uint32_t)meters_present(slot);
    }
    return up;
}
