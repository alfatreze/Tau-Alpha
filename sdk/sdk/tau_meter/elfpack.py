"""ELF -> TMPK pack, in plain Python (no objcopy, nm or readelf needed). The 48-byte header and the load image are exactly what tools/player2/pack_meter.py writes for the firmware's own packs
(fw2/cores/meter_pack_core.h; docs/features/meters/EXTERNAL_METERS_SPEC.md section 3): origin = the window, abi = the minor the meter NEEDS, meter id = the low 16 bits of the id hash."""
import struct, zlib
from pathlib import Path

WINDOW, WINDOW_SIZE = 0x24970000, 0x10000
SCRATCH_ORG, SCRATCH_SIZE = 0x00026C00, 0x1000
DATA_ALIAS = 0x80000000                   # rodata, descriptor and pstate are linked at the data alias of the window: the instruction alias can be fetched from, not loaded from
TMPK_MAGIC = 0x4B504D54
SYMBOLS = ("_pack_load_end", "_pack_data_lma", "_pack_data_start", "_pack_data_end", "_pack_bss_start", "_pack_bss_end", "_pack_pstate_start", "_pack_pstate_end", "mtr_pack_entry")


class ElfError(ValueError):
    pass


def read_elf(path):
    """-> (segments [(paddr, bytes)] with data, symbols {name: (value, shndx)}, undefined [names])"""
    d = Path(path).read_bytes()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1: raise ElfError("not a 32-bit little-endian ELF file")
    if struct.unpack_from("<H", d, 18)[0] != 243: raise ElfError("not a RISC-V ELF file")
    phoff, shoff = struct.unpack_from("<II", d, 28)
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", d, 42)
    segs = []
    for i in range(phnum):
        p_type, p_off, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, _ = struct.unpack_from("<IIIIIIII", d, phoff + i * phentsize)
        if p_type == 1 and p_filesz: segs.append((p_paddr, d[p_off:p_off + p_filesz]))
    secs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    syms, undef = {}, []
    for sh in secs:
        if sh[1] == 2:                                       # SHT_SYMTAB
            strtab = secs[sh[6]]; stro = strtab[4]
            for k in range(sh[5] // 16):
                n_off, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, sh[4] + 16 * k)
                if not n_off: continue
                name = d[stro + n_off:d.index(b"\0", stro + n_off)].decode("ascii", "replace")
                if shndx == 0: undef.append(name)
                elif (info >> 4) != 0 or True: syms[name] = (value, shndx)
    return segs, syms, undef


def make_pack(elf, abi_minor, meter_id16, abi_major=1, window=WINDOW, scratch=SCRATCH_ORG):
    """-> (pack bytes, info dict); raises ElfError in plain words"""
    segs, sy, undef = read_elf(elf)
    undef = [u for u in undef if u]
    if undef: raise ElfError("the meter uses names it does not define: %s. A meter may call only the host table (tau_meter.h) and the helpers in the SDK's runtime." % ", ".join(sorted(set(undef))))
    for s in SYMBOLS:
        if s not in sy: raise ElfError("the link did not define %s: was the SDK's linker script used?" % s)
    load_end = sy["_pack_load_end"][0] - window
    segs = sorted((p, b) for p, b in segs if window <= p < window + WINDOW_SIZE)
    body = bytearray(load_end)
    for p, b in segs:
        o = p - window
        if o + len(b) > load_end: raise ElfError("a loadable section ends after the image end: unexpected link layout")
        body[o:o + len(b)] = b
    body = bytes(body)
    data_lma, data_start, data_end = sy["_pack_data_lma"][0] - window, sy["_pack_data_start"][0], sy["_pack_data_end"][0]
    bss_start, bss_end = sy["_pack_bss_start"][0], sy["_pack_bss_end"][0]
    pst_off, pst_end = sy["_pack_pstate_start"][0] - DATA_ALIAS - window, sy["_pack_pstate_end"][0] - DATA_ALIAS - window
    if bss_start != data_end: raise ElfError(".bss does not follow .data in the scratch area")
    entry = sy["mtr_pack_entry"][0] - window
    hdr = struct.pack("<IHHIIIIIIIIII", TMPK_MAGIC, (abi_major << 8) | abi_minor, meter_id16 & 0xFFFF, len(body), window, entry, zlib.crc32(body) & 0xFFFFFFFF, data_lma, data_end - data_start,
                      data_start, bss_end - bss_start, pst_off, pst_end - pst_off)
    info = {"image": len(body), "data": data_end - data_start, "bss": bss_end - bss_start, "pstate": pst_end - pst_off, "entry": entry, "scratch": (data_end - data_start) + (bss_end - bss_start)}
    return hdr + body, info
