/* The inherited FLAC decoder (fw/flac.c) as its own unit, compiled for size: at -O2 its text is 10.6 KB, at -Os 6.2 KB, and the old player shipped it at -Os with the FLAC idle figures measured on hardware (B-363..B-562).
 * Replaced last by the new decoder (spec 8.2 / D-P2-03); until then it is used unchanged. */
#pragma GCC optimize ("Os")
#include "../../fw/flac.c"
