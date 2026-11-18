# CHLADNI

One of the meters that ship with Tau Alpha (id `7471cc214987407c0b03917f27923179`, version 1.0.0, MIT).

This folder is a meter project for the SDK: `tau-meter build .` makes `dist/7471cc214987407c0b03917f27923179.tmeter`.
The source is the player's own: `src/fw2/ui/meter_pack_chladni.c` redirects the player's drawing calls to the host table, and `meter_chladni.c` is the drawing. The player headers it includes are copied under `src/` in the player's folder layout. **To write a new meter, start from the template (`tau-alpha-meter-template`) and `tau_meter.h`, not from these.**
