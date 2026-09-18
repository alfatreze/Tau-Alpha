# Issue 017 — A-064 payload probe observes the zero-pattern store

**Status:** open; diagnostic instrumentation defect, not yet a functional
SDRAM conclusion.

## Observation

The A-064 full-width Pocket bar was visible and decoded as:

```text
GGGGGGGGRRRRGGGGRGGRGGRRGGRRGGGRGGG
```

The stable red cells are 8–11, 16, 19, 22–23, 26–27, and 31. The first
request is the A-061 CPU preflight read. The second request is the first
fixed-pattern store in `run_tests()`, which writes `0x00000000`; therefore the
red payload predicates are expected for that transaction. The existing probe
comments and the earlier diagnostic guide incorrectly treated the captured
store as an all-ones write.

The player-facing CPU-window diagnostic still reports the first failing
readback as `A0200000`, expected `FFFFFFFF`, actual `00000000` (181 failures on
this run). That is evidence that a later all-ones write/readback fails, but the
A-064 bar does not observe that later transaction.

## Impact and boundary

This issue prevents the A-064 payload bar from locating the failing boundary
for the all-ones write. It does **not** prove that the adapter, owner mux,
bridge, or SDRAM controller corrupted the zero-pattern store. No release/player
path changed, and no SDRAM migration is authorised.

## Next gate

A-065 is Quartus-fitted and host-packaged: it captures the fourth CPU request
(preflight read, zero store, zero read, then the first all-ones store) and the
bridge retains the first mapped write whose payload is `0xFFFFFFFF`. Its
focused probe and bridge simulations pass. Its raw RBF SHA-256 is
`dcdc78107dbb0dc729a71f9559f55dda3e6fd7e950c46468255f4c90e0878bdb`; its
side-by-side package identity is `alfatreze.TAU_SDRAM_PRB65` /
`tau_sdram_prb65`. Obtain one Pocket bar and failure-screen photograph before
making any functional RTL claim.
