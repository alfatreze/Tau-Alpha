# WATERFALL

One of the meters that ship with Tau Alpha (id `17e607764b83b9372452eda124ae763f`, version 1.0.0, MIT).

This folder is a meter project for the SDK: `tau-meter build .` makes `dist/17e607764b83b9372452eda124ae763f.tmeter`.
The source is the player's own: `src/fw2/ui/meter_pack_waterfall_grid.c` redirects the player's drawing calls to the host table, and `meter_waterfall_grid.c` is the drawing. The player headers it includes are copied under `src/` in the player's folder layout. **To write a new meter, start from the template (`template/` in `tau-alpha-meter-sdk`) and `tau_meter.h`, not from these.**
