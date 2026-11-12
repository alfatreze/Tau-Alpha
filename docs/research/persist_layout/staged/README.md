# Staged captures for the persist_layout 2 migration (P2.3, host-only)

Real persist files from the owner's card and what the migration to **layout 2** must make of them. Made by `python3 tools/persist_layout.py --stage .` (from the repository root); `sim/test_persist_layout.py` checks that every file equals a fresh run and that this README lists its SHA-256. Format: APF's `Settings/<core id>/Interact/_core/interact_persist.json` (16 variables, ids 10..33), the same as `../captures/`.

* `input_dev181_layout0_*`: **TAU DEV 181**, a real **layout-0** card (word 0 is the volume 73; meter index 6; colour 7). Copied byte for byte from `../captures/dev181_interact_persist.json`.
* `input_dev185_layout1_*`: **TAU DEV 185**, the real **layout-1** file the Pocket wrote after the firmware migrated DEV 181's settings (meter index 6, preset position 1 of meter 6). Copied byte for byte from `../captures/dev185_after_migration_interact_persist.json`.
* `expected_layout2_from_*`: what the migration to layout 2 must write (both inputs hold the same settings, so both give the same file): version 2 in word 0, word 5 = the id hash of shipped meter 6 (`fw2/cores/shipped_meter_ids.h`: **a PLACEHOLDER until P2.0**, so these two files change then), word 6 = preset position 1 + the old index 6 as scratch (bits 16..11) + the converted mark (bit 29), word 15 empty; volume, colour, theme, last played unchanged.
* `interrupted_*_after_NN_writes_*`: the input as a migration leaves it after its first NN writes (`WRITE_ORDER` in `tools/persist_layout.py`: words 1, 2, 6, 5, 15, 16, 17, 18, 21, 8, 9, 10, 11, 19, 20, then word 0 last). NN = 3: words 1, 2, 6 written (word 6 carries the mark, word 5 not yet), 4: word 5 too, 10: everything but the version. Booting any of them must end at the `expected_layout2_*` file (the resumed migration).

**No tuning is staged**: neither real capture holds one, and re-keying a layout-1 tuning needs the shipped meters' parameter tables (see `../golden_vectors.json`, `cases_v1`, which uses a labelled fixture). The real hardware test of the migration with a tuning is P2.7.

| file | SHA-256 |
|---|---|
| `input_dev181_layout0_interact_persist.json` | `897677c733d61a0b875d02c8f39f8cf84938a0f4a2d9b44e84d1fc527bd79331` |
| `expected_layout2_from_dev181_layout0_interact_persist.json` | `f787e08d06704b9756f1219b6539f48bd8888f32afdd17c0352ea9511e494d1a` |
| `interrupted_dev181_layout0_after_03_writes_interact_persist.json` | `124f97096d260d1f424ebb6832383c4b78f19002a49005aaa06ca1b617670460` |
| `interrupted_dev181_layout0_after_04_writes_interact_persist.json` | `700daacc26bbb3cf07d478186e6a25ee09d53d6b1eaf170445658336179ceef7` |
| `interrupted_dev181_layout0_after_10_writes_interact_persist.json` | `a0a0824b25326a9530d48dea09ba478300c1befc60cc5aadc1970eb01750e996` |
| `input_dev185_layout1_interact_persist.json` | `49f503a3a19afad6114e5d7187127ffb8c3c887b3a4b10cf66d0d728b3f50085` |
| `expected_layout2_from_dev185_layout1_interact_persist.json` | `f787e08d06704b9756f1219b6539f48bd8888f32afdd17c0352ea9511e494d1a` |
| `interrupted_dev185_layout1_after_03_writes_interact_persist.json` | `6f589e10a7a102f4fcb761d626ad256db82189977b91b3193f70a6e3a6d3a3c6` |
| `interrupted_dev185_layout1_after_04_writes_interact_persist.json` | `5fb529db931f540674ed55da84a77e23cfd9c2dbb51f8cae92377106945b3af9` |
| `interrupted_dev185_layout1_after_10_writes_interact_persist.json` | `aa8f6e8d53e8b7477c1b05262eca082401d557ebfdeb7405c289c3c543f476d8` |
