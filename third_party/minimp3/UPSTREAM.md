# minimp3: pristine upstream, host float oracle only

Every file in this directory except `UPSTREAM.md` and `UPSTREAM.sha256` is **byte-identical to upstream** (checked by `sim/test_mp3_v2_oracle.py`). Nothing here is linked into firmware. It is the licence-clean float reference for the MP3 decoder v2 (decision D-C01) and the source the derived front end is converted from (the derived tree is separate, see `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`, section 7).

| | |
|---|---|
| Upstream | <https://github.com/lieff/minimp3> |
| Pinned commit | `48e3a085f0e1a99446b34614e1e7f17f09dc2e82` ("initialise free_format_bytes in mp3dec_detect_cb (issue #161)", committed 2026-11-22T20:11:58+03:00 by the upstream author) |
| Retrieved | 2026-11-25, `git clone` into an empty directory, `git checkout` of the pinned commit |
| Licence | CC0 1.0 Universal (the upstream `LICENSE` file, read in full; `minimp3.h` and `minimp3_ex.h` also carry the dedication "To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide") |
| Not vendored | the upstream `vectors/` directory (see below), `minimp3_test.c`, `player/`, `fuzz*/`, `huffopt/`, `scripts/`, `README.md` |

| File | Bytes | SHA-256 |
|---|---|---|
| `minimp3.h` | 80,975 | `eb4b44812cfe77a358aa1820462cc877473a8d97206009ebecd18b60a123875a` |
| `minimp3_ex.h` | 54,088 | `ec1240ad4f29961ed83a9da160fcb190ce2253b618aaae36dfbd7bab4d5eedcf` |
| `LICENSE` | 6,556 | `6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01` |

`minimp3.h` has the same SHA-256 as the one recorded by B-1168 for the window-table extraction. The `LICENSE` hash printed in the plan (`6a1ee543...d01c9`) is a transcription slip; the real file ends `...e94d01`.

## Test vectors

The upstream `vectors/` directory (26 MB: ISO-style `.bit`/`.pcm` pairs and a `performance/` set of MP3 files) is **not vendored**. It is git-ignored (`third_party/minimp3/vectors/`) so a local download works without becoming part of the repository. Upstream states no licence for the vectors apart from the repository-wide CC0 `LICENSE`; several file names (`l3-compl`, `l3-he_*`, `l3-hecommon`, `l3-si*`, `l3-sin1k0db`) match the names of the ISO/IEC 11172-4 conformance streams, so they may be third-party material that the repository's CC0 file cannot relicense. Unresolved, see decision D-C02 D4 and the WP-01 report.

## Updating the pin

Replace the files byte for byte, regenerate `UPSTREAM.sha256` and the table above, change the commit, re-run `sim/test_mp3_v2_oracle.py` and every test that uses the oracle.
