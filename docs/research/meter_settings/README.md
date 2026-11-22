# Golden vectors for the meter-settings save file `tau-meter-settings.bin` v1 (for Tau Omega: O026/3)

Made by `tools/meter_settings_vectors.py` (deterministic: no clock, no toolchain) with the Python reference `tools/meter_settings.py`; `sim/test_p2_mset_vectors.py` proves that the firmware's own C code (`fw2/cores/mset_file_core.h`) answers every file the same way, regenerates this folder byte for byte, and that a reader which compares `seq` as a plain number fails on the wrap files.

Format and rules: `docs/features/meters/METER_SAVE_FILE_SPEC.md` section 4. In short: two generations of 4,096 bytes; a generation is a 20-byte header (`TMSF`, version 1, count, seq, used, CRC-32 of header bytes 0..15 and the records) and `used` bytes of records (`id_hash u32, params_rev u16, preset u8, n u8, n x (index u8, value u16)`, little-endian, oldest first). A generation is valid when magic, version 1, `used <= 4076`, the records exactly filling `used` and the CRC all hold; the valid generation with the larger `seq` in **serial arithmetic** (`(a - b)` read as a signed 32-bit number is positive) is the active one; none valid = defaults; a version above 1 in EITHER generation = `locked`: the whole file is ignored (no records are used, not even those of the other generation) and nothing is written.

## What `expected.json` says for each file
`status` (`ok` = a valid generation exists, `none` = defaults, `locked` = a newer version is present, `size` = not an 8,192-byte file), `active_bank` (0 or 1, null when none), `seq`, `count`, `records` (in file order), `generations` (valid and seq of bank A and bank B), and `lookups`: for each (`id_hash`, `params_rev`) whether the **active** generation holds a record usable for it. A record of another `params_rev` is **not** found (it is ignored on read and dropped at that meter's next write). A file that is not 8,192 bytes is never loaded by the Pocket (`size_exact`), so for the two `wrong_size` files the answer is `size` and no lookups.

## Files
| file | bytes | SHA-256 | status | bank | seq | records | what |
|---|---|---|---|---|---|---|---|
| `zero_seed.bin` | 8192 | `9f1dcbc35c350d6027f98be0f5c8b43b42ca52b7604459c0c42be3aa88913d47` | none | - | - | 0 | the zero-filled seed Omega or the installer creates: no generation, defaults |
| `all_ff.bin` | 8192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` | none | - | - | 0 | 8,192 bytes of 0xFF (the Pocket's own fill when a file is missing, bit 5): no generation |
| `one_record.bin` | 8192 | `4a23dd641814894871ffc2545c9491a3b107f66b89ac3b84c6ca752de418e2a1` | ok | A | 1 | 1 | a fresh file after one edit: bank A, seq 1, one record |
| `two_generations_b_wins.bin` | 8192 | `13584a28e1a052a520d53f334738bf807ceaadcc52f781a590a9a48240aab9bc` | ok | B | 2 | 2 | two edits: bank A holds seq 1 (one record), bank B seq 2 (two records): the larger seq wins, so bank B |
| `three_records.bin` | 8192 | `82086c06ce0ba19b543e75380a71418ae0ccd9ee2706507b278952d0af20d28e` | ok | A | 3 | 3 | three edits: bank A seq 3 wins over bank B seq 2; three records, oldest first; the third is params_rev 2 with preset 3 |
| `record_replaced.bin` | 8192 | `4faef2bd98aa27c2d73eeebefddce9a9913c90b60a8a806437f14236a6b5c588` | ok | A | 3 | 2 | the first meter edited again: its record moves to the END (recency order) and replaces the old one |
| `record_removed.bin` | 8192 | `6d66f13a36ab144ffdd1e990c8bfb49a5cc47cada68669f2caf148221dbb9ea6` | ok | B | 4 | 2 | an edit with no entries removes the meter's record (B): two records left |
| `full_with_eviction.bin` | 8192 | `3cd70f32ae18eaf7fe05c8af466c1a7e9547804197b49f9675c104aca5104987` | ok | B | 40 | 31 | 40 meters with 40 changed parameters each (5,120 bytes of records, more than a generation holds): the oldest meters were evicted, the newest are intact; used <= 4,076 |
| `damaged_newest.bin` | 8192 | `136cc7103f9e9cd823c2d33533f958bea2241597385c9421e712cb9cdd434f32` | ok | B | 2 | 2 | three_records.bin with one bit flipped inside a record of the newest generation (bank A): its CRC fails, so the older generation (bank B, seq 2, two records) is used |
| `damaged_header_crc.bin` | 8192 | `7873f6cba1ca90b858210f3c4704db8955d15913b26f13c52ecf77e031043aea` | ok | B | 2 | 2 | three_records.bin with a bit of the newest generation's stored CRC flipped: same outcome, the older generation is used |
| `damaged_both.bin` | 8192 | `734825d00362de10a5912a523d1034390486aa438dc5797a56eda638d8fbe152` | none | - | - | 0 | one bit flipped inside a record of each generation: no valid generation, defaults |
| `newer_version.bin` | 8192 | `fe45bff3db05260b326df358b091d390e7ad8c9d6025799420ba4dc7fe7cb9b8` | locked | - | - | 0 | three_records.bin with version 2 in the newest generation's header (a file from a future Tau): the whole file is ignored (status locked): no record of either generation is used and nothing is written |
| `wrong_size_8191.bin` | 8191 | `ff35516a2d6e777877f4f6731a3fdb73ce41bcdfb855070761f6c2154d0fa895` | size | - | - | 0 | three_records.bin cut by one byte: not an 8,192-byte file, so the Pocket does not load it (size_exact) and a reader says size |
| `wrong_size_8193.bin` | 8193 | `73ad46ba436a647f3c16dc33aea630ca7d137a05d6c1bf908735d8dbb5ce823f` | size | - | - | 0 | three_records.bin with one byte appended: size |
| `seq_wrap_b_wins.bin` | 8192 | `0ad1f3c523ce61caba6a8e66f59f72f1719157c9b28dcb26e6126f8dbd26d8c9` | ok | B | 0 | 1 | bank A seq 0xFFFFFFFF, bank B seq 0: serial arithmetic says 0 is NEWER, so bank B wins (a plain 'larger number wins' would pick bank A) |
| `seq_wrap_a_wins.bin` | 8192 | `85a440a3c9a61cdf5ba45421be8ce4728c51f8ab55147e237f45c8ef78013d47` | ok | A | 0 | 1 | the same two generations the other way round: bank A seq 0, bank B seq 0xFFFFFFFF: bank A wins |
| `seq_half_b_wins.bin` | 8192 | `09096d6ef21e8af50e0d7ff5ce41b885e8434bb2c21fc82d539d6152dda23d3c` | ok | B | 2147483648 | 1 | bank A seq 0x7FFFFFFF, bank B seq 0x80000000: the difference is +1, so bank B wins (a signed 32-bit compare of the seqs themselves would pick A) |
| `seq_half_a_wins.bin` | 8192 | `a6c6798cf275691a5f25c89243ac092a1ce69d78a65062da4a98be493fdf1914` | ok | A | 2147483648 | 1 | bank A seq 0x80000000, bank B seq 0x7FFFFFFF: bank A wins |

`locked` files: the firmware does not write them and neither should a tool (the one-writer rule, spec 4.4). Writes by Omega are limited to creating the zero seed.

## Regenerating
`python3 tools/meter_settings_vectors.py` rewrites everything, including `expected.json` and this README.
