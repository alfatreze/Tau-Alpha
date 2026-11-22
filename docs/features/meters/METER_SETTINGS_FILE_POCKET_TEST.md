# TAU DEV 230: every meter keeps its settings (Pocket test sheet)

DEV 230 is a normal build (DIAG=0) with the save-file backend of `meter_settings_get/put` (`fw2/ui/meters_settings.c`, file format `docs/features/meters/METER_SAVE_FILE_SPEC.md` section 4) on the bitstream of DEV 229 (no new fit). The file is `Saves/tau_dev/alfatreze.TAU DEV 230/tau-meter-settings.bin` (8,192 bytes, zeroed at install).

| Step | Do | Passes when |
|---|---|---|
| 1 | Boot DEV 230. METERS page: open Configure (X) of **four different meters**, one of them not the set meter, change at least one parameter of each (a colour or a switch is enough). Write down what you set. | the Configure pages work as in DEV 226 |
| 2 | Quit from the root menu. Mount the card and tell me. | |
| 3 | (Mac) I run `python3 tools/meter_settings.py` on the file. | `status ok`, one record per meter you changed, `seq` 4 or more |
| 4 | Boot DEV 230 again. Open each of the four meters. | all four show the values you set (the third and fourth are the new thing: the old pool kept only two) |
| 5 | Change one of them back to its default, Quit, boot. | it is back to the default, the others still hold |
| 6 | Power the Pocket off with a meter just edited (no menu Quit), boot. | the edit is kept, or the previous value is: never a broken file or a default for every meter |

**Migration:** if the card still holds tunings made with DEV 226 or earlier (the old two-tuning pool), those two meters should keep their values after the first look at them and then live in the file.

**Not covered here:** sleep, a damaged file, a deleted file (boot must still work, settings fall back to the old pool for the session), `--replace` over the file. Host-proven: `sim/test_p2_mset_file.py`, `sim/test_p2_mset_backend.py`.
