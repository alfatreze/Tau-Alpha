# flacperf: cycle profile of the FLAC decoder under the RV32IM simulator (S8-0)

Runs the real `fw/flac.c` (and, as a yardstick only, dr_flac, never part of the product) over slices of real FLAC files on `prof_sim.py`
(instruction counts are exact; cycles are an ESTIMATE from a VexRiscv-style penalty model: calibrate against the Pocket's `MP DEC AVG`).
The sink models Player 2's `aout_put` (a packed word per pair stored to the PSRAM ring window, +24 cycles a store, one pointer write per block).
Audio hashes make variants comparable (identical hash = identical PCM; 24-bit differs between decoders that round or truncate).

    mkdir out; ./build.sh $PWD/../../../fw/flac.c 1 base          # TAU_LPC_FW=1 (unit); 0 = software LPC; extra cflags as arg 4 (e.g. -DFLAC_RICE_FAST=0)
    python3 mkblob.py "<file>.flac" <skip frames> <n frames> w.bin  # frame slice for fw/flac.c
    python3 run.py out/base.elf w.bin                                # per-function cycles per channel-sample
    python3 mkblob2.py ... d_w.bin; DRFLAC_DIR=<dir> ./bdr.sh dr_o2 -O2; python3 run.py out/dr_o2.elf d_w.bin   # dr_flac yardstick

Results and interpretation: `docs/features/FLAC_DECODER_V2_REVIEW.md` section 11. Mono is not supported by the Python slicer (`flac_verify.py`).
