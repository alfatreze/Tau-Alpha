/* Host run of fw2/cores/about_core.h: prints every visible row for a scenario (argv[1]): "kind|label|value". See sim/test_p2_about.py. */
#include <stdio.h>
#include <string.h>
#include "../fw2/cores/about_core.h"
int main(int argc, char **argv)
{
    about_in_t in; memset(&in, 0, sizeof in);
    const char *s = argc > 1 ? argv[1] : "a";
    in.core_version = 0x4D50331Bu; in.theme = "TAU"; in.colour = "ORANGE"; in.font = "GROTESK BOLD"; in.meter = "WINAMP BARS"; in.eq = "WARM"; in.volume = 73; in.tempo_q8 = 256;
    if (!strcmp(s, "a")) {                       /* a stable release, a loaded library, an MP3 playing at normal speed */
        strcpy(in.version, "0.7.0"); in.lib_state = 1; in.tracks = 143; in.albums = 12; in.artists = 9; in.np_valid = 1; strcpy(in.quality, "MP3 128K 44.1K STEREO"); in.rg_mode = 1;
        in.themes = 2; in.eq_user = 3; in.packs_seen = 7; in.packs_run = 7; in.uptime_s = 30; in.art_on = 1;
    } else if (!strcmp(s, "b")) {                /* a dev build, no library, no track, settings carried over, problems, a long run, a stretch at 1.25x */
        strcpy(in.version, "0.6.1-dev.191+f2c7350c"); in.lib_state = 0; in.migrated = 1; in.dropouts = 3; in.read_errors = 1; in.uptime_s = 3725; in.packs_seen = 7; in.packs_run = 5; in.light = 1;
    } else if (!strcmp(s, "c")) {                /* a failed library, a FLAC at 1.25x stretch, one dropout, one error, 12 minutes */
        strcpy(in.version, "0.7.1-preview.2"); in.lib_state = 2; in.lib_err = 5; in.np_valid = 1; strcpy(in.quality, "FLAC 96K STEREO"); in.rg_mode = 2; in.tempo_q8 = 320; in.varispeed = 0; in.dropouts = 1; in.read_errors = 1; in.uptime_s = 720; in.packs_seen = 1; in.packs_run = 1;
    } else if (!strcmp(s, "d")) { strcpy(in.version, "0.7.0"); in.lib_state = 1; in.tracks = 1; in.albums = 1; in.artists = 1; in.np_valid = 1; in.tempo_q8 = 512; in.varispeed = 1; in.rg_mode = 0; in.uptime_s = 59; in.eq = 0; }
    const uint32_t n = about_count(&in);
    printf("count %u\n", n);
    about_row_t r;
    for (uint32_t i = 0; i < n; i++) { about_get(&in, i, &r); printf("%u|%s|%s\n", r.kind, r.label, r.value); }
    return 0;
}
