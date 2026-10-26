# 025: `tau_details.pack` crashes on a ReplayGain peak of 2.0 or more

**Status:** FIXED 2026-10-26 (B-854): `pack()` clamps both peaks to 65,535, test in `sim/test_track_details.py`. Originally OPEN, logged 2026-10-14 (reported by the Tau Omega session; Omega clamps the value on its side, nothing was changed here). Found by reading, reproduced on the host.

**What happens.** `tools/tau_details.py` `pack()` writes the track and album peaks as unsigned 16-bit Q15 (`struct.pack("<BBBBIIHHhhHHHHI", ...)`: 1.0 = 32768). `parse_peak_q15()` accepts a whole part up to 7 (like `fw/replaygain.h rg_parse_peak_q15`), so a PEAK tag of 2.0 or more gives 65,536 or more (`parse_peak_q15("2.0")` = 65,536; "3.5" = 114,688; even "1.99997" rounds up to 65,536), and `struct.pack` raises `ushort format requires 0 <= number <= 65535`. The exception stops the whole index build, so one such track (a clipped master with a sample peak above 2.0 is rare but real) makes `tau_library.py build` / `sync_media.py --library` fail for the entire library.

**Reproduce.** `python3 - <<'E'` with `import tau_details as t; d = {... "tp": t.parse_peak_q15("2.0") ...}; t.pack(d, {})` raises the error above.

**Scope.** Index writer only. The firmware is not affected by the parse itself (`rg_t` keeps peaks as `uint32_t`); the details page reads the u16 field, so a clamped value shows as 1.9999.

**Suggested fix (not applied).** Clamp the stored peak to 65,535 in `pack()` (and say so in the format note, `docs/MEDIA_LIBRARY_0.4_SPEC.md` section 16), keep `parse_peak_q15` unchanged so the firmware rule and the Python rule stay identical. Safe for ReplayGain: the boost cap is `0.944 / peak`, so for any peak of about 1.0 or more there is no boost at all and the exact value above 2.0 cannot change the result. Add a host test (a peak of 2.0, 3.5 and 7.9999 must build an index and read back as the clamp) next to `sim/test_track_details.py`. Tau Omega's writer already clamps, so the two tools would then agree.
