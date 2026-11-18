# WINAMP SCOPE

One of the meters that ship with Tau Alpha (id `143acb80cbcd1b08b84c2661eeab3414`, version 1.0.0, MIT).

This folder is a meter project for the SDK: `tau-meter build .` makes `dist/143acb80cbcd1b08b84c2661eeab3414.tmeter`.
The source is the player's own: `src/fw2/ui/meter_pack_winamp_scope.c` redirects the player's drawing calls to the host table, and `meter_winamp_scope.c` is the drawing. The player headers it includes are copied under `src/` in the player's folder layout. **To write a new meter, start from the template (`tau-alpha-meter-template`) and `tau_meter.h`, not from these.**
