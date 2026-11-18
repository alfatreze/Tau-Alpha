# MASTER VU

One of the meters that ship with Tau Alpha (id `0d8c03d3a2770c1aac2dc6abbc222afd`, version 1.0.0, MIT).

This folder is a meter project for the SDK: `tau-meter build .` makes `dist/0d8c03d3a2770c1aac2dc6abbc222afd.tmeter`.
The source is the player's own: `src/fw2/ui/meter_pack_vu_master.c` redirects the player's drawing calls to the host table, and `meter_vu_master.c` is the drawing. The player headers it includes are copied under `src/` in the player's folder layout. **To write a new meter, start from the template (`template/` in `tau-alpha-meter-sdk`) and `tau_meter.h`, not from these.**
