# SOUND MAP

One of the meters that ship with Tau Alpha (id `0c82d9f07f73f25d90ca54c8d39173ce`, version 1.0.0, MIT).

This folder is a meter project for the SDK: `tau-meter build .` makes `dist/0c82d9f07f73f25d90ca54c8d39173ce.tmeter`.
The source is the player's own: `src/fw2/ui/meter_pack_sound_map.c` redirects the player's drawing calls to the host table, and `meter_sound_map.c` is the drawing. The player headers it includes are copied under `src/` in the player's folder layout. **To write a new meter, start from the template (`template/` in `tau-alpha-meter-sdk`) and `tau_meter.h`, not from these.**
