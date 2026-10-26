# Complete Tau Alpha source/module inventory

Audited revision: `ae11a8fee5a7cf3e7873a4a654bccf5a7df6b908`. This inventory has **482 source/build-script files**. All 933 tracked paths, including configuration, media, licences and binaries, are in `tracked-files.csv`.

SLOC excludes blank lines and comments; Python docstrings count as source. HarpMudd columns describe copy-aware Git origin, not exclusive copyright authorship. Vendor/framework/generated buckets must not be interpreted as HarpMudd-authored. “Port” is surviving code last recorded by the explicitly documented upstream-port commit. A tiny inherited count in a new file can be copied boilerplate rather than a derived implementation.

Base comparison is against v1.4.0-era fork `7ef8f0f`; upstream comparison is against `f4b8f01` (v1.6.0 main). `absent` means no same-path file at that revision; it does not prove no copied code. All relative source links point into this checkout; line numbers refer to the audited revision.

## archive

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [archive/qr_encoder/gen_qr_tables.py](../../../archive/qr_encoder/gen_qr_tables.py) | 29 | 0 | 0.0% | 0 | absent / absent |
| [archive/qr_encoder/qr_harness.c](../../../archive/qr_encoder/qr_harness.c) | 23 | 0 | 0.0% | 0 | absent / absent |
| [archive/qr_encoder/qrcode.h](../../../archive/qr_encoder/qrcode.h) | 254 | 0 | 0.0% | 0 | absent / absent |
| [archive/qr_encoder/test_qr.py](../../../archive/qr_encoder/test_qr.py) | 87 | 3 | 3.4% | 0 | absent / absent |

## firmware

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [fw/alloc.c](../../../fw/alloc.c) | 31 | 31 | 100.0% | 0 | identical / identical |
| [fw/art.inc](../../../fw/art.inc) | 340 | 282 | 82.9% | 0 | modified / modified |
| [fw/assets.inc](../../../fw/assets.inc) | 72 | 0 | 0.0% | 0 | absent / absent |
| [fw/assets_core.h](../../../fw/assets_core.h) | 157 | 0 | 0.0% | 0 | absent / absent |
| [fw/blit_probe.inc](../../../fw/blit_probe.inc) | 164 | 0 | 0.0% | 0 | absent / absent |
| [fw/build.sh](../../../fw/build.sh) | 216 | 54 | 25.0% | 0 | modified / modified |
| [fw/chladni.inc](../../../fw/chladni.inc) | 170 | 0 | 0.0% | 0 | absent / absent |
| [fw/chladni_core.h](../../../fw/chladni_core.h) | 263 | 0 | 0.0% | 0 | absent / absent |
| [fw/cold.inc](../../../fw/cold.inc) | 76 | 0 | 0.0% | 0 | absent / absent |
| [fw/cold_core.h](../../../fw/cold_core.h) | 35 | 0 | 0.0% | 0 | absent / absent |
| [fw/flac.c](../../../fw/flac.c) | 694 | 508 | 73.2% | 0 | modified / modified |
| [fw/flac.h](../../../fw/flac.h) | 62 | 52 | 83.9% | 0 | modified / modified |
| [fw/flac_lpc_hw.h](../../../fw/flac_lpc_hw.h) | 8 | 0 | 0.0% | 0 | absent / absent |
| [fw/flac_lpc_hw.inc](../../../fw/flac_lpc_hw.inc) | 30 | 0 | 0.0% | 0 | absent / absent |
| [fw/fullscreen.inc](../../../fw/fullscreen.inc) | 164 | 0 | 0.0% | 0 | absent / absent |
| [fw/gain_handover.h](../../../fw/gain_handover.h) | 31 | 0 | 0.0% | 0 | absent / absent |
| [fw/halcyon.inc](../../../fw/halcyon.inc) | 70 | 0 | 0.0% | 0 | absent / absent |
| [fw/halcyon_core.h](../../../fw/halcyon_core.h) | 44 | 0 | 0.0% | 0 | absent / absent |
| [fw/halcyon_curve.h](../../../fw/halcyon_curve.h) | 22 | 0 | 0.0% | 0 | absent / absent |
| [fw/halcyon_hw.h](../../../fw/halcyon_hw.h) | 45 | 0 | 0.0% | 0 | absent / absent |
| [fw/halcyon_page.inc](../../../fw/halcyon_page.inc) | 181 | 0 | 0.0% | 0 | absent / absent |
| [fw/halcyon_prst.h](../../../fw/halcyon_prst.h) | 70 | 0 | 0.0% | 0 | absent / absent |
| [fw/headroom.h](../../../fw/headroom.h) | 31 | 0 | 0.0% | 0 | absent / absent |
| [fw/helios.inc](../../../fw/helios.inc) | 205 | 0 | 0.0% | 0 | absent / absent |
| [fw/helios_rect.h](../../../fw/helios_rect.h) | 36 | 0 | 0.0% | 0 | absent / absent |
| [fw/info_export.inc](../../../fw/info_export.inc) | 38 | 0 | 0.0% | 0 | absent / absent |
| [fw/key_repeat.h](../../../fw/key_repeat.h) | 17 | 0 | 0.0% | 0 | absent / absent |
| [fw/lame_tag.h](../../../fw/lame_tag.h) | 29 | 0 | 0.0% | 0 | absent / absent |
| [fw/layered_wave.inc](../../../fw/layered_wave.inc) | 534 | 0 | 0.0% | 0 | absent / absent |
| [fw/library.inc](../../../fw/library.inc) | 491 | 10 | 2.0% | 0 | absent / absent |
| [fw/library_core.h](../../../fw/library_core.h) | 257 | 0 | 0.0% | 0 | absent / absent |
| [fw/link.ld](../../../fw/link.ld) | 57 | 24 | 42.1% | 0 | modified / modified |
| [fw/meter.h](../../../fw/meter.h) | 50 | 0 | 0.0% | 0 | absent / absent |
| [fw/meter_core.h](../../../fw/meter_core.h) | 65 | 0 | 0.0% | 0 | absent / absent |
| [fw/meter_module.h](../../../fw/meter_module.h) | 59 | 0 | 0.0% | 0 | absent / absent |
| [fw/meter_policy.h](../../../fw/meter_policy.h) | 20 | 0 | 0.0% | 0 | absent / absent |
| [fw/mp3_poly_hw.h](../../../fw/mp3_poly_hw.h) | 11 | 0 | 0.0% | 0 | absent / absent |
| [fw/mp3_poly_hw.inc](../../../fw/mp3_poly_hw.inc) | 45 | 0 | 0.0% | 0 | absent / absent |
| [fw/pcm_push.h](../../../fw/pcm_push.h) | 85 | 0 | 0.0% | 0 | absent / absent |
| [fw/pixgrid.h](../../../fw/pixgrid.h) | 20 | 0 | 0.0% | 0 | absent / absent |
| [fw/player.c](../../../fw/player.c) | 5,397 | 3,172 | 58.8% | 43 | modified / modified |
| [fw/playlist.inc](../../../fw/playlist.inc) | 179 | 141 | 78.8% | 0 | modified / modified |
| [fw/psram_diag.c](../../../fw/psram_diag.c) | 557 | 20 | 3.6% | 0 | absent / absent |
| [fw/rc_lut.h](../../../fw/rc_lut.h) | 20 | 0 | 0.0% | 0 | absent / absent |
| [fw/replaygain.h](../../../fw/replaygain.h) | 120 | 0 | 0.0% | 0 | absent / absent |
| [fw/report.inc](../../../fw/report.inc) | 95 | 0 | 0.0% | 0 | absent / absent |
| [fw/settings.inc](../../../fw/settings.inc) | 140 | 59 | 42.1% | 0 | modified / modified |
| [fw/settings_diag.inc](../../../fw/settings_diag.inc) | 103 | 0 | 0.0% | 0 | absent / absent |
| [fw/settingsui.inc](../../../fw/settingsui.inc) | 1,373 | 0 | 0.0% | 0 | absent / absent |
| [fw/start.S](../../../fw/start.S) | 57 | 47 | 82.5% | 0 | modified / modified |
| [fw/start_gesture.h](../../../fw/start_gesture.h) | 22 | 0 | 0.0% | 0 | absent / absent |
| [fw/stress.inc](../../../fw/stress.inc) | 189 | 0 | 0.0% | 0 | absent / absent |
| [fw/stress_defs.inc](../../../fw/stress_defs.inc) | 25 | 0 | 0.0% | 0 | absent / absent |
| [fw/suite.inc](../../../fw/suite.inc) | 1,256 | 0 | 0.0% | 0 | absent / absent |
| [fw/suite_core.h](../../../fw/suite_core.h) | 116 | 0 | 0.0% | 0 | absent / absent |
| [fw/sysio.c](../../../fw/sysio.c) | 25 | 25 | 100.0% | 0 | identical / identical |
| [fw/tempo_core.h](../../../fw/tempo_core.h) | 105 | 0 | 0.0% | 0 | absent / absent |
| [fw/theme.h](../../../fw/theme.h) | 72 | 0 | 0.0% | 0 | absent / absent |
| [fw/timg.inc](../../../fw/timg.inc) | 137 | 0 | 0.0% | 0 | absent / absent |
| [fw/timg_core.h](../../../fw/timg_core.h) | 24 | 0 | 0.0% | 0 | absent / absent |
| [fw/tpg.h](../../../fw/tpg.h) | 73 | 0 | 0.0% | 0 | absent / absent |
| [fw/vu_master.inc](../../../fw/vu_master.inc) | 179 | 0 | 0.0% | 0 | absent / absent |
| [fw/vu_master_core.h](../../../fw/vu_master_core.h) | 35 | 0 | 0.0% | 0 | absent / absent |
| [fw/wsola_core.h](../../../fw/wsola_core.h) | 379 | 0 | 0.0% | 0 | absent / absent |
| [fw/wvcfg_export.inc](../../../fw/wvcfg_export.inc) | 25 | 0 | 0.0% | 0 | absent / absent |

## framework

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [src/fpga/apf/apf_constraints.sdc](../../../src/fpga/apf/apf_constraints.sdc) | 5 | 5 | 100.0% | 0 | identical / identical |
| [src/fpga/apf/apf_top.v](../../../src/fpga/apf/apf_top.v) | 294 | 294 | 100.0% | 0 | identical / identical |
| [src/fpga/apf/build_id_gen.tcl](../../../src/fpga/apf/build_id_gen.tcl) | 61 | 61 | 100.0% | 0 | identical / identical |
| [src/fpga/apf/common.v](../../../src/fpga/apf/common.v) | 93 | 93 | 100.0% | 0 | identical / identical |
| [src/fpga/apf/io_bridge_peripheral.v](../../../src/fpga/apf/io_bridge_peripheral.v) | 217 | 217 | 100.0% | 0 | identical / identical |
| [src/fpga/apf/io_pad_controller.v](../../../src/fpga/apf/io_pad_controller.v) | 214 | 214 | 100.0% | 0 | identical / identical |
| [src/fpga/apf/mf_datatable.v](../../../src/fpga/apf/mf_datatable.v) | 85 | 85 | 100.0% | 0 | identical / identical |
| [src/fpga/apf/mf_ddio_bidir_12.v](../../../src/fpga/apf/mf_ddio_bidir_12.v) | 51 | 51 | 100.0% | 0 | identical / identical |
| [src/fpga/core/core_bridge_cmd.v](../../../src/fpga/core/core_bridge_cmd.v) | 452 | 442 | 97.8% | 0 | modified / modified |

## generated

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [archive/qr_encoder/qr_tables.h](../../../archive/qr_encoder/qr_tables.h) | 89 | 0 | 0.0% | 0 | absent / absent |
| [fw/diag_features.h](../../../fw/diag_features.h) | 57 | 0 | 0.0% | 0 | absent / absent |
| [fw/font_metrics.h](../../../fw/font_metrics.h) | 15 | 15 | 100.0% | 0 | identical / identical |
| [fw/halcyon_tab.h](../../../fw/halcyon_tab.h) | 563 | 0 | 0.0% | 0 | absent / absent |
| [fw/layered_wave_tables.h](../../../fw/layered_wave_tables.h) | 51 | 0 | 0.0% | 0 | absent / absent |
| [fw/meter_gen_enum.h](../../../fw/meter_gen_enum.h) | 1 | 0 | 0.0% | 0 | absent / absent |
| [fw/meter_gen_names.h](../../../fw/meter_gen_names.h) | 3 | 0 | 0.0% | 0 | absent / absent |
| [fw/meter_gen_order.h](../../../fw/meter_gen_order.h) | 2 | 0 | 0.0% | 0 | absent / absent |
| [fw/meter_thumbs.h](../../../fw/meter_thumbs.h) | 211 | 0 | 0.0% | 0 | absent / absent |
| [fw/meters_gen.h](../../../fw/meters_gen.h) | 224 | 0 | 0.0% | 0 | absent / absent |
| [fw/theme_data.h](../../../fw/theme_data.h) | 16 | 0 | 0.0% | 0 | absent / absent |
| [fw/vu_segment_table.h](../../../fw/vu_segment_table.h) | 19 | 0 | 0.0% | 0 | absent / absent |
| [fw/wsola_tables.h](../../../fw/wsola_tables.h) | 41 | 0 | 0.0% | 0 | absent / absent |
| [sim/common/test_mp3.h](../../../sim/common/test_mp3.h) | 604 | 604 | 100.0% | 0 | identical / identical |
| [sim/common/test_mp3_128.h](../../../sim/common/test_mp3_128.h) | 2,051 | 2,051 | 100.0% | 0 | identical / identical |
| [sim/cymo_resamp_rom.h](../../../sim/cymo_resamp_rom.h) | 5 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/font_rom.v](../../../src/fpga/core/font_rom.v) | 6,102 | 3,052 | 50.0% | 0 | modified / modified |
| [src/fpga/core/tau_cymo_resamp_rom.svh](../../../src/fpga/core/tau_cymo_resamp_rom.svh) | 5,125 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_mp3_poly_rom.svh](../../../src/fpga/core/tau_mp3_poly_rom.svh) | 538 | 0 | 0.0% | 0 | absent / absent |

## rtl

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [src/fpga/core/core_constraints.sdc](../../../src/fpga/core/core_constraints.sdc) | 9 | 8 | 88.9% | 0 | modified / modified |
| [src/fpga/core/core_game.vh](../../../src/fpga/core/core_game.vh) | 714 | 217 | 30.4% | 0 | modified / modified |
| [src/fpga/core/core_top.v](../../../src/fpga/core/core_top.v) | 243 | 238 | 97.9% | 0 | modified / modified |
| [src/fpga/core/mf_pllbase.v](../../../src/fpga/core/mf_pllbase.v) | 20 | 19 | 95.0% | 0 | modified / modified |
| [src/fpga/core/mp3_fb.sv](../../../src/fpga/core/mp3_fb.sv) | 948 | 418 | 44.1% | 0 | modified / modified |
| [src/fpga/core/mp3_soc.v](../../../src/fpga/core/mp3_soc.v) | 1,082 | 287 | 26.5% | 0 | modified / modified |
| [src/fpga/core/pcm_fifo.v](../../../src/fpga/core/pcm_fifo.v) | 76 | 64 | 84.2% | 7 | modified / modified |
| [src/fpga/core/tau_cdc_gray_bus.sv](../../../src/fpga/core/tau_cdc_gray_bus.sv) | 27 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_cdc_gray_ctr.sv](../../../src/fpga/core/tau_cdc_gray_ctr.sv) | 44 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_cdc_sync1.sv](../../../src/fpga/core/tau_cdc_sync1.sv) | 11 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_clut_wr.sv](../../../src/fpga/core/tau_clut_wr.sv) | 26 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_cymo_feed.sv](../../../src/fpga/core/tau_cymo_feed.sv) | 60 | 2 | 3.3% | 0 | absent / absent |
| [src/fpga/core/tau_cymo_resamp.sv](../../../src/fpga/core/tau_cymo_resamp.sv) | 126 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_flac_lpc.sv](../../../src/fpga/core/tau_flac_lpc.sv) | 108 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_gain_stage.sv](../../../src/fpga/core/tau_gain_stage.sv) | 86 | 4 | 4.7% | 0 | absent / absent |
| [src/fpga/core/tau_halcyon.sv](../../../src/fpga/core/tau_halcyon.sv) | 235 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_main_ram.sv](../../../src/fpga/core/tau_main_ram.sv) | 60 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_mp3_poly.sv](../../../src/fpga/core/tau_mp3_poly.sv) | 149 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_psram_async.sv](../../../src/fpga/core/tau_psram_async.sv) | 171 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_psram_bus.sv](../../../src/fpga/core/tau_psram_bus.sv) | 70 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_psram_probe.sv](../../../src/fpga/core/tau_psram_probe.sv) | 155 | 11 | 7.1% | 0 | absent / absent |
| [src/fpga/core/tau_ram_probe_a.sv](../../../src/fpga/core/tau_ram_probe_a.sv) | 23 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_sdram_addr_decode.sv](../../../src/fpga/core/tau_sdram_addr_decode.sv) | 30 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_sdram_arbiter.sv](../../../src/fpga/core/tau_sdram_arbiter.sv) | 86 | 3 | 3.5% | 0 | absent / absent |
| [src/fpga/core/tau_sdram_bridge_mux.sv](../../../src/fpga/core/tau_sdram_bridge_mux.sv) | 45 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_sdram_cpu_bridge.sv](../../../src/fpga/core/tau_sdram_cpu_bridge.sv) | 209 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_sdram_cpu_window_probe.sv](../../../src/fpga/core/tau_sdram_cpu_window_probe.sv) | 170 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_sdram_wb_adapter.sv](../../../src/fpga/core/tau_sdram_wb_adapter.sv) | 63 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_signaltap_tap.sv](../../../src/fpga/core/tau_signaltap_tap.sv) | 9 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_spec_bank.sv](../../../src/fpga/core/tau_spec_bank.sv) | 72 | 2 | 2.8% | 0 | absent / absent |
| [src/fpga/core/tau_vs_counter.sv](../../../src/fpga/core/tau_vs_counter.sv) | 18 | 0 | 0.0% | 0 | absent / absent |
| [src/fpga/core/tau_wave_meter.sv](../../../src/fpga/core/tau_wave_meter.sv) | 88 | 2 | 2.3% | 0 | absent / absent |
| [src/fpga/core/tgt_cmd.v](../../../src/fpga/core/tgt_cmd.v) | 83 | 76 | 91.6% | 0 | modified / modified |

## test

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [sim/assets_load_harness.c](../../../sim/assets_load_harness.c) | 32 | 0 | 0.0% | 0 | absent / absent |
| [sim/build_fw.sh](../../../sim/build_fw.sh) | 52 | 51 | 98.1% | 0 | modified / modified |
| [sim/check_psram_fw_record.py](../../../sim/check_psram_fw_record.py) | 82 | 0 | 0.0% | 0 | absent / absent |
| [sim/chladni_module_harness.c](../../../sim/chladni_module_harness.c) | 127 | 0 | 0.0% | 0 | absent / absent |
| [sim/common/link.ld](../../../sim/common/link.ld) | 27 | 27 | 100.0% | 0 | identical / identical |
| [sim/common/start.S](../../../sim/common/start.S) | 47 | 47 | 100.0% | 0 | identical / identical |
| [sim/common/sysio.c](../../../sim/common/sysio.c) | 52 | 52 | 100.0% | 0 | identical / identical |
| [sim/cymo_resamp_model.c](../../../sim/cymo_resamp_model.c) | 76 | 0 | 0.0% | 0 | absent / absent |
| [sim/dcfifo_model.v](../../../sim/dcfifo_model.v) | 27 | 0 | 0.0% | 0 | absent / absent |
| [sim/flac_lpc_fw_harness.c](../../../sim/flac_lpc_fw_harness.c) | 107 | 0 | 0.0% | 0 | absent / absent |
| [sim/flac_lpc_model.c](../../../sim/flac_lpc_model.c) | 93 | 0 | 0.0% | 0 | absent / absent |
| [sim/flac_rg_harness.c](../../../sim/flac_rg_harness.c) | 18 | 0 | 0.0% | 0 | absent / absent |
| [sim/flac_rice_diff_harness.c](../../../sim/flac_rice_diff_harness.c) | 58 | 0 | 0.0% | 0 | absent / absent |
| [sim/flac_to16_harness.c](../../../sim/flac_to16_harness.c) | 10 | 0 | 0.0% | 0 | absent / absent |
| [sim/fw_audiopair/main.c](../../../sim/fw_audiopair/main.c) | 44 | 2 | 4.5% | 0 | absent / absent |
| [sim/fw_halcyon/main.c](../../../sim/fw_halcyon/main.c) | 44 | 2 | 4.5% | 0 | absent / absent |
| [sim/fw_helix/main.c](../../../sim/fw_helix/main.c) | 76 | 76 | 100.0% | 0 | identical / identical |
| [sim/fw_hello/main.c](../../../sim/fw_hello/main.c) | 36 | 36 | 100.0% | 0 | identical / identical |
| [sim/fw_ifetch/link.ld](../../../sim/fw_ifetch/link.ld) | 16 | 0 | 0.0% | 0 | absent / absent |
| [sim/fw_ifetch/main.c](../../../sim/fw_ifetch/main.c) | 97 | 0 | 0.0% | 0 | absent / absent |
| [sim/fw_ifetch/start.S](../../../sim/fw_ifetch/start.S) | 12 | 9 | 75.0% | 0 | absent / absent |
| [sim/fw_minimp3/main.c](../../../sim/fw_minimp3/main.c) | 52 | 52 | 100.0% | 0 | identical / identical |
| [sim/gain_handover_harness.c](../../../sim/gain_handover_harness.c) | 21 | 0 | 0.0% | 0 | absent / absent |
| [sim/gen_art_fixtures.py](../../../sim/gen_art_fixtures.py) | 42 | 0 | 0.0% | 0 | absent / absent |
| [sim/halcyon_core_harness.c](../../../sim/halcyon_core_harness.c) | 16 | 0 | 0.0% | 0 | absent / absent |
| [sim/halcyon_curve_harness.c](../../../sim/halcyon_curve_harness.c) | 15 | 0 | 0.0% | 0 | absent / absent |
| [sim/halcyon_hw_harness.c](../../../sim/halcyon_hw_harness.c) | 21 | 0 | 0.0% | 0 | absent / absent |
| [sim/halcyon_page_harness.c](../../../sim/halcyon_page_harness.c) | 81 | 8 | 9.9% | 0 | absent / absent |
| [sim/halcyon_prst_harness.c](../../../sim/halcyon_prst_harness.c) | 24 | 0 | 0.0% | 0 | absent / absent |
| [sim/helix_host/assembly.h](../../../sim/helix_host/assembly.h) | 11 | 0 | 0.0% | 0 | absent / absent |
| [sim/lame_tag_harness.c](../../../sim/lame_tag_harness.c) | 25 | 0 | 0.0% | 0 | absent / absent |
| [sim/make_soc_sim.py](../../../sim/make_soc_sim.py) | 19 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_fw_capture.c](../../../sim/mp3_poly_fw_capture.c) | 10 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_fw_check_common.h](../../../sim/mp3_poly_fw_check_common.h) | 21 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_map.c](../../../sim/mp3_poly_map.c) | 54 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_model.c](../../../sim/mp3_poly_model.c) | 54 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_model_core.h](../../../sim/mp3_poly_model_core.h) | 51 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_probe.c](../../../sim/mp3_poly_probe.c) | 94 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_scratch_capture.c](../../../sim/mp3_poly_scratch_capture.c) | 9 | 0 | 0.0% | 0 | absent / absent |
| [sim/mp3_poly_subband_harness.c](../../../sim/mp3_poly_subband_harness.c) | 95 | 0 | 0.0% | 0 | absent / absent |
| [sim/overlay_proto/harness.c](../../../sim/overlay_proto/harness.c) | 46 | 0 | 0.0% | 0 | absent / absent |
| [sim/overlay_proto/overlay.h](../../../sim/overlay_proto/overlay.h) | 36 | 0 | 0.0% | 0 | absent / absent |
| [sim/psram_chip_model.v](../../../sim/psram_chip_model.v) | 141 | 0 | 0.0% | 0 | absent / absent |
| [sim/splash_harness.c](../../../sim/splash_harness.c) | 35 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_audio_pair.v](../../../sim/tb_audio_pair.v) | 105 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_blit_scene.v](../../../sim/tb_blit_scene.v) | 214 | 76 | 35.5% | 0 | absent / absent |
| [sim/tb_cymo_i2s_rate.v](../../../sim/tb_cymo_i2s_rate.v) | 51 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_halcyon_soc.v](../../../sim/tb_halcyon_soc.v) | 83 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_halcyon_tick.v](../../../sim/tb_halcyon_tick.v) | 45 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_helios_dbuf.v](../../../sim/tb_helios_dbuf.v) | 195 | 86 | 44.1% | 0 | absent / absent |
| [sim/tb_mp3_fb.v](../../../sim/tb_mp3_fb.v) | 366 | 197 | 53.8% | 0 | modified / modified |
| [sim/tb_pcm_decay.v](../../../sim/tb_pcm_decay.v) | 75 | 68 | 90.7% | 7 | modified / modified |
| [sim/tb_pcm_fifo.v](../../../sim/tb_pcm_fifo.v) | 76 | 11 | 14.5% | 60 | absent / identical |
| [sim/tb_picorv32.v](../../../sim/tb_picorv32.v) | 125 | 125 | 100.0% | 0 | identical / identical |
| [sim/tb_psram_fw.v](../../../sim/tb_psram_fw.v) | 129 | 3 | 2.3% | 0 | absent / absent |
| [sim/tb_psram_ifetch.v](../../../sim/tb_psram_ifetch.v) | 108 | 3 | 2.8% | 0 | absent / absent |
| [sim/tb_reference.v](../../../sim/tb_reference.v) | 413 | 413 | 100.0% | 0 | identical / identical |
| [sim/tb_sdram_fb_controller_probe.v](../../../sim/tb_sdram_fb_controller_probe.v) | 106 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_sound_i2s_full16.v](../../../sim/tb_sound_i2s_full16.v) | 45 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_cdc_gray_bus.v](../../../sim/tb_tau_cdc_gray_bus.v) | 38 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_cdc_gray_ctr.v](../../../sim/tb_tau_cdc_gray_ctr.v) | 62 | 10 | 16.1% | 0 | absent / absent |
| [sim/tb_tau_cdc_sync1.v](../../../sim/tb_tau_cdc_sync1.v) | 45 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_clut_wr.v](../../../sim/tb_tau_clut_wr.v) | 53 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_cymo_feed.v](../../../sim/tb_tau_cymo_feed.v) | 53 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_cymo_resamp.v](../../../sim/tb_tau_cymo_resamp.v) | 79 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_flac_lpc.v](../../../sim/tb_tau_flac_lpc.v) | 95 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_gain_stage.v](../../../sim/tb_tau_gain_stage.v) | 46 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_halcyon.v](../../../sim/tb_tau_halcyon.v) | 74 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_main_ram.v](../../../sim/tb_tau_main_ram.v) | 97 | 10 | 10.3% | 0 | absent / absent |
| [sim/tb_tau_mp3_poly.v](../../../sim/tb_tau_mp3_poly.v) | 44 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_psram_async.v](../../../sim/tb_tau_psram_async.v) | 201 | 3 | 1.5% | 0 | absent / absent |
| [sim/tb_tau_psram_probe.v](../../../sim/tb_tau_psram_probe.v) | 176 | 3 | 1.7% | 0 | absent / absent |
| [sim/tb_tau_psram_wb_return_regression.v](../../../sim/tb_tau_psram_wb_return_regression.v) | 122 | 3 | 2.5% | 0 | absent / absent |
| [sim/tb_tau_sdram_adapter_return_probe.v](../../../sim/tb_tau_sdram_adapter_return_probe.v) | 48 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_sdram_addr_decode.v](../../../sim/tb_tau_sdram_addr_decode.v) | 87 | 8 | 9.2% | 0 | absent / absent |
| [sim/tb_tau_sdram_arbiter.v](../../../sim/tb_tau_sdram_arbiter.v) | 118 | 10 | 8.5% | 0 | absent / absent |
| [sim/tb_tau_sdram_bridge_mux.v](../../../sim/tb_tau_sdram_bridge_mux.v) | 17 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_sdram_composed_path.v](../../../sim/tb_tau_sdram_composed_path.v) | 119 | 3 | 2.5% | 0 | absent / absent |
| [sim/tb_tau_sdram_cpu_bridge.v](../../../sim/tb_tau_sdram_cpu_bridge.v) | 177 | 8 | 4.5% | 0 | absent / absent |
| [sim/tb_tau_sdram_cpu_return_probe.v](../../../sim/tb_tau_sdram_cpu_return_probe.v) | 53 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_sdram_cpu_window_probe.v](../../../sim/tb_tau_sdram_cpu_window_probe.v) | 118 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_sdram_mux_return_probe.v](../../../sim/tb_tau_sdram_mux_return_probe.v) | 48 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_sdram_phase2_path.v](../../../sim/tb_tau_sdram_phase2_path.v) | 123 | 4 | 3.3% | 0 | absent / absent |
| [sim/tb_tau_sdram_wb_adapter.v](../../../sim/tb_tau_sdram_wb_adapter.v) | 110 | 8 | 7.3% | 0 | absent / absent |
| [sim/tb_tau_sdram_wb_return_regression.v](../../../sim/tb_tau_sdram_wb_return_regression.v) | 164 | 3 | 1.8% | 0 | absent / absent |
| [sim/tb_tau_spec_bank.v](../../../sim/tb_tau_spec_bank.v) | 70 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_vs_counter.v](../../../sim/tb_tau_vs_counter.v) | 24 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tau_wave_meter.v](../../../sim/tb_tau_wave_meter.v) | 78 | 0 | 0.0% | 0 | absent / absent |
| [sim/tb_tgt_cmd.v](../../../sim/tb_tgt_cmd.v) | 84 | 67 | 79.8% | 0 | modified / modified |
| [sim/tb_vexriscv.v](../../../sim/tb_vexriscv.v) | 93 | 93 | 100.0% | 0 | identical / identical |
| [sim/tempo_funnel_harness.c](../../../sim/tempo_funnel_harness.c) | 40 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_art_decode.py](../../../sim/test_art_decode.py) | 93 | 4 | 4.3% | 0 | absent / absent |
| [sim/test_art_source.py](../../../sim/test_art_source.py) | 45 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_assets_load.py](../../../sim/test_assets_load.py) | 60 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_blit_reference.py](../../../sim/test_blit_reference.py) | 113 | 4 | 3.5% | 0 | absent / absent |
| [sim/test_chladni_core.py](../../../sim/test_chladni_core.py) | 153 | 4 | 2.6% | 0 | absent / absent |
| [sim/test_chladni_module.py](../../../sim/test_chladni_module.py) | 23 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_chladni_params.py](../../../sim/test_chladni_params.py) | 92 | 2 | 2.2% | 0 | absent / absent |
| [sim/test_clk66.py](../../../sim/test_clk66.py) | 107 | 2 | 1.9% | 0 | absent / absent |
| [sim/test_clut_contract.py](../../../sim/test_clut_contract.py) | 43 | 4 | 9.3% | 0 | absent / absent |
| [sim/test_cold_fw.py](../../../sim/test_cold_fw.py) | 68 | 6 | 8.8% | 0 | absent / absent |
| [sim/test_cymo_crossfeed.py](../../../sim/test_cymo_crossfeed.py) | 6 | 1 | 16.7% | 0 | absent / absent |
| [sim/test_cymo_gain_model.py](../../../sim/test_cymo_gain_model.py) | 100 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_cymo_i2s_rate.py](../../../sim/test_cymo_i2s_rate.py) | 36 | 1 | 2.8% | 0 | absent / absent |
| [sim/test_cymo_isp.py](../../../sim/test_cymo_isp.py) | 6 | 1 | 16.7% | 0 | absent / absent |
| [sim/test_cymo_loopback.py](../../../sim/test_cymo_loopback.py) | 25 | 1 | 4.0% | 0 | absent / absent |
| [sim/test_cymo_out_model.py](../../../sim/test_cymo_out_model.py) | 59 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_cymo_resamp_model.py](../../../sim/test_cymo_resamp_model.py) | 123 | 2 | 1.6% | 0 | absent / absent |
| [sim/test_cymo_tplimit.py](../../../sim/test_cymo_tplimit.py) | 6 | 1 | 16.7% | 0 | absent / absent |
| [sim/test_diag_features.py](../../../sim/test_diag_features.py) | 54 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_eq_coeff_precision.py](../../../sim/test_eq_coeff_precision.py) | 24 | 1 | 4.2% | 0 | absent / absent |
| [sim/test_fit_manifest.py](../../../sim/test_fit_manifest.py) | 49 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_flac_lpc_fw_redirect.py](../../../sim/test_flac_lpc_fw_redirect.py) | 116 | 2 | 1.7% | 0 | absent / absent |
| [sim/test_flac_lpc_model.py](../../../sim/test_flac_lpc_model.py) | 33 | 3 | 9.1% | 0 | absent / absent |
| [sim/test_flac_lpc_symmetry.py](../../../sim/test_flac_lpc_symmetry.py) | 142 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_flac_rice_fast.py](../../../sim/test_flac_rice_fast.py) | 157 | 3 | 1.9% | 0 | absent / absent |
| [sim/test_flac_to16.py](../../../sim/test_flac_to16.py) | 43 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_fw_pair_check.py](../../../sim/test_fw_pair_check.py) | 30 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_gain_handover.py](../../../sim/test_gain_handover.py) | 126 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_halcyon_assets.py](../../../sim/test_halcyon_assets.py) | 11 | 1 | 9.1% | 0 | absent / absent |
| [sim/test_halcyon_core.py](../../../sim/test_halcyon_core.py) | 53 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_halcyon_curve.py](../../../sim/test_halcyon_curve.py) | 48 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_halcyon_generator.py](../../../sim/test_halcyon_generator.py) | 65 | 1 | 1.5% | 0 | absent / absent |
| [sim/test_halcyon_hw.py](../../../sim/test_halcyon_hw.py) | 101 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_halcyon_lab.py](../../../sim/test_halcyon_lab.py) | 45 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_halcyon_model.py](../../../sim/test_halcyon_model.py) | 65 | 1 | 1.5% | 0 | absent / absent |
| [sim/test_halcyon_page.py](../../../sim/test_halcyon_page.py) | 126 | 2 | 1.6% | 0 | absent / absent |
| [sim/test_halcyon_prst.py](../../../sim/test_halcyon_prst.py) | 107 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_headroom.py](../../../sim/test_headroom.py) | 58 | 2 | 3.4% | 0 | absent / absent |
| [sim/test_helios_beam.py](../../../sim/test_helios_beam.py) | 87 | 3 | 3.4% | 0 | absent / absent |
| [sim/test_helios_clip.py](../../../sim/test_helios_clip.py) | 114 | 2 | 1.8% | 0 | absent / absent |
| [sim/test_helios_rect.py](../../../sim/test_helios_rect.py) | 96 | 4 | 4.2% | 0 | absent / absent |
| [sim/test_install_dev_core.py](../../../sim/test_install_dev_core.py) | 208 | 2 | 1.0% | 0 | absent / absent |
| [sim/test_key_repeat.py](../../../sim/test_key_repeat.py) | 54 | 2 | 3.7% | 0 | absent / absent |
| [sim/test_lame_tag.py](../../../sim/test_lame_tag.py) | 64 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_layered_wave_golden.py](../../../sim/test_layered_wave_golden.py) | 111 | 2 | 1.8% | 0 | absent / absent |
| [sim/test_library_fw.py](../../../sim/test_library_fw.py) | 194 | 6 | 3.1% | 0 | absent / absent |
| [sim/test_library_index.py](../../../sim/test_library_index.py) | 167 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_lw_ring.py](../../../sim/test_lw_ring.py) | 94 | 2 | 2.1% | 0 | absent / absent |
| [sim/test_meter_core.py](../../../sim/test_meter_core.py) | 181 | 2 | 1.1% | 0 | absent / absent |
| [sim/test_meter_golden.py](../../../sim/test_meter_golden.py) | 126 | 2 | 1.6% | 0 | absent / absent |
| [sim/test_meter_module.py](../../../sim/test_meter_module.py) | 108 | 2 | 1.9% | 0 | absent / absent |
| [sim/test_meter_policy.py](../../../sim/test_meter_policy.py) | 70 | 2 | 2.9% | 0 | absent / absent |
| [sim/test_meter_trace.py](../../../sim/test_meter_trace.py) | 49 | 2 | 4.1% | 0 | absent / absent |
| [sim/test_mp3_poly_fw.py](../../../sim/test_mp3_poly_fw.py) | 60 | 2 | 3.3% | 0 | absent / absent |
| [sim/test_mp3_poly_model.py](../../../sim/test_mp3_poly_model.py) | 35 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_mp3_poly_probe.py](../../../sim/test_mp3_poly_probe.py) | 31 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_mp3_poly_subband.py](../../../sim/test_mp3_poly_subband.py) | 67 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_overlay_proto.py](../../../sim/test_overlay_proto.py) | 29 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_pcm_burst.py](../../../sim/test_pcm_burst.py) | 70 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_pcm_push.py](../../../sim/test_pcm_push.py) | 110 | 2 | 1.8% | 0 | absent / absent |
| [sim/test_persist_ranges.py](../../../sim/test_persist_ranges.py) | 19 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_pixgrid.py](../../../sim/test_pixgrid.py) | 17 | 1 | 5.9% | 0 | absent / absent |
| [sim/test_psram_decode.py](../../../sim/test_psram_decode.py) | 98 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_ram192k.py](../../../sim/test_ram192k.py) | 68 | 2 | 2.9% | 0 | absent / absent |
| [sim/test_ram_report.py](../../../sim/test_ram_report.py) | 49 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_rc_lut.py](../../../sim/test_rc_lut.py) | 67 | 4 | 6.0% | 0 | absent / absent |
| [sim/test_replaygain.py](../../../sim/test_replaygain.py) | 120 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_sdram_map_overlap.py](../../../sim/test_sdram_map_overlap.py) | 29 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_settings_menu_counts.py](../../../sim/test_settings_menu_counts.py) | 11 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_sound_i2s_full16.py](../../../sim/test_sound_i2s_full16.py) | 24 | 1 | 4.2% | 0 | absent / absent |
| [sim/test_splash_asset.py](../../../sim/test_splash_asset.py) | 109 | 2 | 1.8% | 0 | absent / absent |
| [sim/test_start_gesture.py](../../../sim/test_start_gesture.py) | 53 | 2 | 3.8% | 0 | absent / absent |
| [sim/test_suite.py](../../../sim/test_suite.py) | 129 | 3 | 2.3% | 0 | absent / absent |
| [sim/test_tau_assets.py](../../../sim/test_tau_assets.py) | 202 | 2 | 1.0% | 0 | absent / absent |
| [sim/test_tau_compat.py](../../../sim/test_tau_compat.py) | 353 | 2 | 0.6% | 0 | absent / absent |
| [sim/test_tau_image.py](../../../sim/test_tau_image.py) | 99 | 2 | 2.0% | 0 | absent / absent |
| [sim/test_tau_timg.py](../../../sim/test_tau_timg.py) | 106 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_tau_version.py](../../../sim/test_tau_version.py) | 47 | 2 | 4.3% | 0 | absent / absent |
| [sim/test_tempo_funnel.py](../../../sim/test_tempo_funnel.py) | 96 | 2 | 2.1% | 0 | absent / absent |
| [sim/test_tempo_slice.py](../../../sim/test_tempo_slice.py) | 71 | 0 | 0.0% | 0 | absent / absent |
| [sim/test_theme.py](../../../sim/test_theme.py) | 192 | 7 | 3.6% | 0 | absent / absent |
| [sim/test_thumb_dbuf.py](../../../sim/test_thumb_dbuf.py) | 62 | 3 | 4.8% | 0 | absent / absent |
| [sim/test_tpg.py](../../../sim/test_tpg.py) | 100 | 1 | 1.0% | 0 | absent / absent |
| [sim/test_tpg_fw.py](../../../sim/test_tpg_fw.py) | 28 | 1 | 3.6% | 0 | absent / absent |
| [sim/test_vu_master.py](../../../sim/test_vu_master.py) | 119 | 1 | 0.8% | 0 | absent / absent |
| [sim/test_wsola.py](../../../sim/test_wsola.py) | 182 | 2 | 1.1% | 0 | absent / absent |
| [sim/thumb_dbuf_harness.c](../../../sim/thumb_dbuf_harness.c) | 164 | 3 | 1.8% | 0 | absent / absent |
| [sim/timg_harness.c](../../../sim/timg_harness.c) | 119 | 0 | 0.0% | 0 | absent / absent |
| [sim/tpg_harness.c](../../../sim/tpg_harness.c) | 20 | 0 | 0.0% | 0 | absent / absent |
| [sim/wsola2_harness.c](../../../sim/wsola2_harness.c) | 46 | 0 | 0.0% | 0 | absent / absent |
| [sim/wsola_harness.c](../../../sim/wsola_harness.c) | 29 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/art_decode_harness.c](../../../tools/host/art_decode_harness.c) | 71 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/blit_reference.py](../../../tools/host/blit_reference.py) | 236 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/chladni_harness.c](../../../tools/host/chladni_harness.c) | 125 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/cold_harness.c](../../../tools/host/cold_harness.c) | 28 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/helios_rect_harness.c](../../../tools/host/helios_rect_harness.c) | 15 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/hostio.h](../../../tools/host/hostio.h) | 57 | 57 | 100.0% | 0 | identical / modified |
| [tools/host/level_harness.c](../../../tools/host/level_harness.c) | 70 | 70 | 100.0% | 0 | identical / identical |
| [tools/host/library_harness.c](../../../tools/host/library_harness.c) | 132 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/link.ld](../../../tools/host/link.ld) | 19 | 19 | 100.0% | 0 | identical / identical |
| [tools/host/rc_lut_harness.c](../../../tools/host/rc_lut_harness.c) | 24 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/seek_harness.c](../../../tools/host/seek_harness.c) | 173 | 173 | 100.0% | 0 | identical / identical |
| [tools/host/selftest.c](../../../tools/host/selftest.c) | 41 | 41 | 100.0% | 0 | identical / identical |
| [tools/host/skip_harness.c](../../../tools/host/skip_harness.c) | 99 | 99 | 100.0% | 0 | identical / identical |
| [tools/host/spectrum_harness.c](../../../tools/host/spectrum_harness.c) | 84 | 84 | 100.0% | 0 | identical / identical |
| [tools/host/start.S](../../../tools/host/start.S) | 15 | 15 | 100.0% | 0 | identical / identical |
| [tools/host/suite_harness.c](../../../tools/host/suite_harness.c) | 31 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/vu_master_harness.c](../../../tools/host/vu_master_harness.c) | 28 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/wsola2_harness.c](../../../tools/host/wsola2_harness.c) | 43 | 0 | 0.0% | 0 | absent / absent |
| [tools/host/wsola_harness.c](../../../tools/host/wsola_harness.c) | 33 | 0 | 0.0% | 0 | absent / absent |
| [tools/triad/test_director.py](../../../tools/triad/test_director.py) | 127 | 0 | 0.0% | 0 | absent / absent |

## tooling

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [Makefile](../../../Makefile) | 413 | 0 | 0.0% | 4 | absent / absent |
| [package.py](../../../package.py) | 124 | 77 | 62.1% | 0 | modified / modified |
| [tools/art_scale_model.py](../../../tools/art_scale_model.py) | 101 | 101 | 100.0% | 0 | identical / identical |
| [tools/capture_splash_frame.py](../../../tools/capture_splash_frame.py) | 87 | 0 | 0.0% | 0 | absent / absent |
| [tools/card_snapshot.py](../../../tools/card_snapshot.py) | 219 | 215 | 98.2% | 0 | modified / modified |
| [tools/check_art_load_order.py](../../../tools/check_art_load_order.py) | 118 | 2 | 1.7% | 0 | absent / absent |
| [tools/check_audit_trail.py](../../../tools/check_audit_trail.py) | 18 | 0 | 0.0% | 0 | absent / absent |
| [tools/check_build_env.sh](../../../tools/check_build_env.sh) | 58 | 0 | 0.0% | 0 | absent / absent |
| [tools/check_cold_calls.py](../../../tools/check_cold_calls.py) | 65 | 3 | 4.6% | 0 | absent / absent |
| [tools/check_fw_bitstream_pair.py](../../../tools/check_fw_bitstream_pair.py) | 78 | 2 | 2.6% | 0 | absent / absent |
| [tools/check_heap_gap.py](../../../tools/check_heap_gap.py) | 103 | 0 | 0.0% | 0 | absent / absent |
| [tools/check_meter_deps.py](../../../tools/check_meter_deps.py) | 40 | 2 | 5.0% | 0 | absent / absent |
| [tools/check_psram_idle.py](../../../tools/check_psram_idle.py) | 92 | 0 | 0.0% | 0 | absent / absent |
| [tools/check_splash_asset.py](../../../tools/check_splash_asset.py) | 25 | 0 | 0.0% | 0 | absent / absent |
| [tools/check_targets_link.py](../../../tools/check_targets_link.py) | 18 | 0 | 0.0% | 0 | absent / absent |
| [tools/check_tau_package.py](../../../tools/check_tau_package.py) | 51 | 0 | 0.0% | 0 | absent / absent |
| [tools/check_ui_snapshot_renderer.py](../../../tools/check_ui_snapshot_renderer.py) | 63 | 2 | 3.2% | 0 | absent / absent |
| [tools/convert_pocket_art.py](../../../tools/convert_pocket_art.py) | 117 | 0 | 0.0% | 0 | absent / absent |
| [tools/decode_tau_diag_log.py](../../../tools/decode_tau_diag_log.py) | 259 | 0 | 0.0% | 0 | absent / absent |
| [tools/decode_tau_suite.py](../../../tools/decode_tau_suite.py) | 330 | 0 | 0.0% | 0 | absent / absent |
| [tools/diag_cost.py](../../../tools/diag_cost.py) | 49 | 0 | 0.0% | 0 | absent / absent |
| [tools/fit_manifest.py](../../../tools/fit_manifest.py) | 57 | 2 | 3.5% | 0 | absent / absent |
| [tools/flac_bitreader_check.py](../../../tools/flac_bitreader_check.py) | 120 | 120 | 100.0% | 0 | identical / identical |
| [tools/flac_framepos_check.py](../../../tools/flac_framepos_check.py) | 150 | 150 | 100.0% | 0 | identical / identical |
| [tools/flac_hdrcrc_check.py](../../../tools/flac_hdrcrc_check.py) | 119 | 119 | 100.0% | 0 | identical / identical |
| [tools/flac_lpc_capture.py](../../../tools/flac_lpc_capture.py) | 154 | 47 | 30.5% | 0 | absent / absent |
| [tools/flac_make_test.py](../../../tools/flac_make_test.py) | 199 | 199 | 100.0% | 0 | identical / identical |
| [tools/flac_probe.py](../../../tools/flac_probe.py) | 64 | 64 | 100.0% | 0 | identical / identical |
| [tools/flac_ref.py](../../../tools/flac_ref.py) | 233 | 233 | 100.0% | 0 | identical / identical |
| [tools/flac_resync_check.py](../../../tools/flac_resync_check.py) | 76 | 76 | 100.0% | 0 | identical / identical |
| [tools/flac_verify.py](../../../tools/flac_verify.py) | 269 | 269 | 100.0% | 0 | identical / identical |
| [tools/font_preview.py](../../../tools/font_preview.py) | 175 | 175 | 100.0% | 0 | identical / identical |
| [tools/gen_cymo_resamp_rom.py](../../../tools/gen_cymo_resamp_rom.py) | 105 | 3 | 2.9% | 0 | absent / absent |
| [tools/gen_diag_features.py](../../../tools/gen_diag_features.py) | 90 | 0 | 0.0% | 0 | absent / absent |
| [tools/gen_eq_coeffs.py](../../../tools/gen_eq_coeffs.py) | 281 | 198 | 70.5% | 0 | modified / modified |
| [tools/gen_font_rom.py](../../../tools/gen_font_rom.py) | 159 | 120 | 75.5% | 0 | modified / modified |
| [tools/gen_halcyon_tab.py](../../../tools/gen_halcyon_tab.py) | 78 | 0 | 0.0% | 0 | absent / absent |
| [tools/gen_layered_wave_tables.py](../../../tools/gen_layered_wave_tables.py) | 54 | 2 | 3.7% | 0 | absent / absent |
| [tools/gen_meter_thumbs.py](../../../tools/gen_meter_thumbs.py) | 165 | 2 | 1.2% | 0 | absent / absent |
| [tools/gen_meters.py](../../../tools/gen_meters.py) | 411 | 1 | 0.2% | 0 | absent / absent |
| [tools/gen_mp3_poly_rom.py](../../../tools/gen_mp3_poly_rom.py) | 71 | 0 | 0.0% | 0 | absent / absent |
| [tools/gen_signaltap_stp.py](../../../tools/gen_signaltap_stp.py) | 120 | 0 | 0.0% | 0 | absent / absent |
| [tools/gen_splash_asset.py](../../../tools/gen_splash_asset.py) | 77 | 0 | 0.0% | 0 | absent / absent |
| [tools/gen_text_gamma.py](../../../tools/gen_text_gamma.py) | 179 | 166 | 92.7% | 0 | modified / modified |
| [tools/gen_themes.py](../../../tools/gen_themes.py) | 210 | 2 | 1.0% | 0 | absent / absent |
| [tools/gen_vu_segment_table.py](../../../tools/gen_vu_segment_table.py) | 82 | 2 | 2.4% | 0 | absent / absent |
| [tools/gradient_preview.py](../../../tools/gradient_preview.py) | 138 | 138 | 100.0% | 0 | identical / identical |
| [tools/halcyon_assets.py](../../../tools/halcyon_assets.py) | 234 | 0 | 0.0% | 0 | absent / absent |
| [tools/idle_preview.py](../../../tools/idle_preview.py) | 92 | 91 | 98.9% | 0 | modified / modified |
| [tools/install_dev_core.py](../../../tools/install_dev_core.py) | 451 | 2 | 0.4% | 0 | absent / absent |
| [tools/lab/barcode_lab.py](../../../tools/lab/barcode_lab.py) | 160 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_ab_listen.py](../../../tools/lab/cymo_ab_listen.py) | 148 | 11 | 7.4% | 0 | absent / absent |
| [tools/lab/cymo_crossfeed_model.py](../../../tools/lab/cymo_crossfeed_model.py) | 196 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_gain_model.py](../../../tools/lab/cymo_gain_model.py) | 45 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_isp.py](../../../tools/lab/cymo_isp.py) | 423 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_loopback.py](../../../tools/lab/cymo_loopback.py) | 656 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_out_model.py](../../../tools/lab/cymo_out_model.py) | 54 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_recording_script.py](../../../tools/lab/cymo_recording_script.py) | 209 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_resamp_model.py](../../../tools/lab/cymo_resamp_model.py) | 268 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_script_files.py](../../../tools/lab/cymo_script_files.py) | 110 | 4 | 3.6% | 0 | absent / absent |
| [tools/lab/cymo_tempo_model.py](../../../tools/lab/cymo_tempo_model.py) | 280 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/cymo_tplimit_model.py](../../../tools/lab/cymo_tplimit_model.py) | 124 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/eq_coeff_precision.py](../../../tools/lab/eq_coeff_precision.py) | 40 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/eq_cramping.py](../../../tools/lab/eq_cramping.py) | 32 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/eq_parallel_study.py](../../../tools/lab/eq_parallel_study.py) | 55 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/eq_structure_noise.py](../../../tools/lab/eq_structure_noise.py) | 53 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/eq_svf_precision.py](../../../tools/lab/eq_svf_precision.py) | 60 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/gen_halcyon_lab.py](../../../tools/lab/gen_halcyon_lab.py) | 86 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/halcyon_engine_model.py](../../../tools/lab/halcyon_engine_model.py) | 157 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/halcyon_model.py](../../../tools/lab/halcyon_model.py) | 101 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/img_format_lab.py](../../../tools/lab/img_format_lab.py) | 53 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/tpg_preview.py](../../../tools/lab/tpg_preview.py) | 45 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/wsola_fixed_ref.py](../../../tools/lab/wsola_fixed_ref.py) | 297 | 0 | 0.0% | 0 | absent / absent |
| [tools/lab/wsola_search_lab.py](../../../tools/lab/wsola_search_lab.py) | 101 | 0 | 0.0% | 0 | absent / absent |
| [tools/library_check.py](../../../tools/library_check.py) | 181 | 180 | 99.4% | 0 | modified / modified |
| [tools/make_compat_fixtures.py](../../../tools/make_compat_fixtures.py) | 32 | 2 | 6.2% | 0 | absent / absent |
| [tools/make_halcyon_test_assets.py](../../../tools/make_halcyon_test_assets.py) | 39 | 0 | 0.0% | 0 | absent / absent |
| [tools/make_release.py](../../../tools/make_release.py) | 169 | 6 | 3.6% | 0 | absent / absent |
| [tools/meter_cost_estimate.py](../../../tools/meter_cost_estimate.py) | 161 | 2 | 1.2% | 0 | absent / absent |
| [tools/meter_preview.py](../../../tools/meter_preview.py) | 76 | 76 | 100.0% | 0 | identical / identical |
| [tools/meters/preview/build.py](../../../tools/meters/preview/build.py) | 84 | 0 | 0.0% | 0 | absent / absent |
| [tools/pack_cold.py](../../../tools/pack_cold.py) | 98 | 2 | 2.0% | 0 | absent / absent |
| [tools/package_dev_build.py](../../../tools/package_dev_build.py) | 203 | 0 | 0.0% | 0 | absent / absent |
| [tools/package_psram_diagnostic.py](../../../tools/package_psram_diagnostic.py) | 167 | 0 | 0.0% | 0 | absent / absent |
| [tools/pixgrid_check.py](../../../tools/pixgrid_check.py) | 42 | 0 | 0.0% | 0 | absent / absent |
| [tools/push_card.ps1](../../../tools/push_card.ps1) | 72 | 69 | 95.8% | 0 | modified / modified |
| [tools/quartus_fit_summary.py](../../../tools/quartus_fit_summary.py) | 99 | 0 | 0.0% | 0 | absent / absent |
| [tools/ram_report.py](../../../tools/ram_report.py) | 157 | 0 | 0.0% | 0 | absent / absent |
| [tools/rv32sim.py](../../../tools/rv32sim.py) | 286 | 286 | 100.0% | 0 | identical / modified |
| [tools/splash_preview.py](../../../tools/splash_preview.py) | 185 | 183 | 98.9% | 0 | modified / modified |
| [tools/strip_xing.py](../../../tools/strip_xing.py) | 70 | 70 | 100.0% | 0 | identical / identical |
| [tools/sync_media.py](../../../tools/sync_media.py) | 516 | 7 | 1.4% | 0 | absent / absent |
| [tools/tau_assets.py](../../../tools/tau_assets.py) | 229 | 2 | 0.9% | 0 | absent / absent |
| [tools/tau_compat.py](../../../tools/tau_compat.py) | 737 | 2 | 0.3% | 0 | absent / absent |
| [tools/tau_data_slots.py](../../../tools/tau_data_slots.py) | 75 | 0 | 0.0% | 0 | absent / absent |
| [tools/tau_image.py](../../../tools/tau_image.py) | 306 | 0 | 0.0% | 0 | absent / absent |
| [tools/tau_layout.py](../../../tools/tau_layout.py) | 83 | 0 | 0.0% | 0 | absent / absent |
| [tools/tau_library.py](../../../tools/tau_library.py) | 631 | 0 | 0.0% | 0 | absent / absent |
| [tools/tau_version.py](../../../tools/tau_version.py) | 89 | 3 | 3.4% | 0 | absent / absent |
| [tools/theme_preview.py](../../../tools/theme_preview.py) | 150 | 0 | 0.0% | 0 | absent / absent |
| [tools/tpg.py](../../../tools/tpg.py) | 186 | 0 | 0.0% | 0 | absent / absent |
| [tools/triad/director.py](../../../tools/triad/director.py) | 460 | 0 | 0.0% | 0 | absent / absent |
| [tools/triad/qwen_developer.sh](../../../tools/triad/qwen_developer.sh) | 8 | 0 | 0.0% | 0 | absent / absent |
| [tools/triad/start_local_stack.sh](../../../tools/triad/start_local_stack.sh) | 30 | 0 | 0.0% | 0 | absent / absent |
| [tools/ui_snapshot_renderer.py](../../../tools/ui_snapshot_renderer.py) | 799 | 0 | 0.0% | 0 | absent / absent |
| [tools/visual_review.py](../../../tools/visual_review.py) | 169 | 0 | 0.0% | 0 | absent / absent |
| [tools/vm_fit.py](../../../tools/vm_fit.py) | 136 | 0 | 0.0% | 0 | absent / absent |
| [tools/xing_check.py](../../../tools/xing_check.py) | 122 | 120 | 98.4% | 0 | modified / modified |

## vendor

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [third_party/libhelix-mp3/Umakefil](../../../third_party/libhelix-mp3/Umakefil) | 31 | 31 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/mp3dec.c](../../../third_party/libhelix-mp3/mp3dec.c) | 264 | 241 | 91.3% | 0 | modified / modified |
| [third_party/libhelix-mp3/mp3tabs.c](../../../third_party/libhelix-mp3/mp3tabs.c) | 95 | 95 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/pub/mp3_profile.h](../../../third_party/libhelix-mp3/pub/mp3_profile.h) | 36 | 0 | 0.0% | 0 | absent / absent |
| [third_party/libhelix-mp3/pub/mp3common.h](../../../third_party/libhelix-mp3/pub/mp3common.h) | 54 | 54 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/pub/mp3dec.h](../../../third_party/libhelix-mp3/pub/mp3dec.h) | 68 | 68 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/pub/mpadecobjfixpt.h](../../../third_party/libhelix-mp3/pub/mpadecobjfixpt.h) | 35 | 35 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/pub/statname.h](../../../third_party/libhelix-mp3/pub/statname.h) | 28 | 28 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/arm/asmmisc.s](../../../third_party/libhelix-mp3/real/arm/asmmisc.s) | 41 | 41 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/arm/asmpoly.s](../../../third_party/libhelix-mp3/real/arm/asmpoly.s) | 324 | 324 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/arm/asmpoly_gcc.S](../../../third_party/libhelix-mp3/real/arm/asmpoly_gcc.S) | 600 | 600 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/assembly.h](../../../third_party/libhelix-mp3/real/assembly.h) | 263 | 263 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/bitstream.c](../../../third_party/libhelix-mp3/real/bitstream.c) | 179 | 179 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/buffers.c](../../../third_party/libhelix-mp3/real/buffers.c) | 66 | 66 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/coder.h](../../../third_party/libhelix-mp3/real/coder.h) | 212 | 206 | 97.2% | 0 | modified / modified |
| [third_party/libhelix-mp3/real/dct32.c](../../../third_party/libhelix-mp3/real/dct32.c) | 172 | 124 | 72.1% | 0 | modified / modified |
| [third_party/libhelix-mp3/real/dequant.c](../../../third_party/libhelix-mp3/real/dequant.c) | 65 | 65 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/dqchan.c](../../../third_party/libhelix-mp3/real/dqchan.c) | 194 | 194 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/huffman.c](../../../third_party/libhelix-mp3/real/huffman.c) | 271 | 271 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/hufftabs.c](../../../third_party/libhelix-mp3/real/hufftabs.c) | 639 | 639 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/imdct.c](../../../third_party/libhelix-mp3/real/imdct.c) | 441 | 436 | 98.9% | 0 | modified / modified |
| [third_party/libhelix-mp3/real/polyphase.c](../../../third_party/libhelix-mp3/real/polyphase.c) | 159 | 159 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/scalfact.c](../../../third_party/libhelix-mp3/real/scalfact.c) | 215 | 215 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/stproc.c](../../../third_party/libhelix-mp3/real/stproc.c) | 150 | 150 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/real/subband.c](../../../third_party/libhelix-mp3/real/subband.c) | 106 | 35 | 33.0% | 0 | modified / modified |
| [third_party/libhelix-mp3/real/trigtabs.c](../../../third_party/libhelix-mp3/real/trigtabs.c) | 113 | 113 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/testwrap/Umakefil](../../../third_party/libhelix-mp3/testwrap/Umakefil) | 6 | 6 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/testwrap/debug.c](../../../third_party/libhelix-mp3/testwrap/debug.c) | 78 | 78 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/testwrap/debug.h](../../../third_party/libhelix-mp3/testwrap/debug.h) | 13 | 13 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/testwrap/main.c](../../../third_party/libhelix-mp3/testwrap/main.c) | 123 | 123 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/testwrap/timing.c](../../../third_party/libhelix-mp3/testwrap/timing.c) | 98 | 98 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/testwrap/timing.h](../../../third_party/libhelix-mp3/testwrap/timing.h) | 16 | 16 | 100.0% | 0 | identical / identical |
| [third_party/libhelix-mp3/testwrap/winmain.cpp](../../../third_party/libhelix-mp3/testwrap/winmain.cpp) | 123 | 123 | 100.0% | 0 | identical / identical |
| [third_party/picojpeg/picojpeg.c](../../../third_party/picojpeg/picojpeg.c) | 1,795 | 1,795 | 100.0% | 0 | identical / identical |
| [third_party/picojpeg/picojpeg.h](../../../third_party/picojpeg/picojpeg.h) | 74 | 74 | 100.0% | 0 | identical / identical |

## vendor_generated

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [src/fpga/core/mf_pllbase/mf_pllbase_0002.v](../../../src/fpga/core/mf_pllbase/mf_pllbase_0002.v) | 84 | 76 | 90.5% | 0 | modified / modified |
| [src/fpga/core/mf_pllbase_mclk.v](../../../src/fpga/core/mf_pllbase_mclk.v) | 77 | 57 | 74.0% | 0 | absent / absent |
| [src/fpga/rtl/VexRiscv_Full.v](../../../src/fpga/rtl/VexRiscv_Full.v) | 6,441 | 6,441 | 100.0% | 0 | identical / identical |

## vendor_mixed

| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |
|---|---:|---:|---:|---:|---|
| [src/fpga/core/data_loader.sv](../../../src/fpga/core/data_loader.sv) | 117 | 117 | 100.0% | 0 | identical / identical |
| [src/fpga/core/sound_i2s.v](../../../src/fpga/core/sound_i2s.v) | 73 | 52 | 71.2% | 0 | modified / modified |
| [src/fpga/core/sync_fifo.v](../../../src/fpga/core/sync_fifo.v) | 64 | 59 | 92.2% | 0 | modified / modified |
| [src/fpga/rtl/mem/sdram_fb.sv](../../../src/fpga/rtl/mem/sdram_fb.sv) | 470 | 402 | 85.5% | 0 | modified / modified |

## HDL module declarations

Every lexical HDL module declaration, including generated CPU/IP internals. This is a declaration inventory, not the elaborated release hierarchy.

| Source | Declared modules |
|---|---|
| `sim/dcfifo_model.v` | `dcfifo` |
| `sim/psram_chip_model.v` | `psram_chip_model` |
| `sim/tb_audio_pair.v` | `tb_audio_pair` |
| `sim/tb_blit_scene.v` | `tb_blit_scene` |
| `sim/tb_cymo_i2s_rate.v` | `tb_cymo_i2s_rate` |
| `sim/tb_halcyon_soc.v` | `tb_halcyon_soc` |
| `sim/tb_halcyon_tick.v` | `tb_halcyon_tick` |
| `sim/tb_helios_dbuf.v` | `tb_helios_dbuf` |
| `sim/tb_mp3_fb.v` | `tb_mp3_fb` |
| `sim/tb_pcm_decay.v` | `tb_pcm_decay` |
| `sim/tb_pcm_fifo.v` | `tb_pcm_fifo` |
| `sim/tb_picorv32.v` | `tb_picorv32` |
| `sim/tb_psram_fw.v` | `tb_psram_fw` |
| `sim/tb_psram_ifetch.v` | `tb_psram_ifetch` |
| `sim/tb_reference.v` | `testbench`, `picorv32_wrapper`, `axi4_memory` |
| `sim/tb_sdram_fb_controller_probe.v` | `tb_sdram_fb_controller_probe` |
| `sim/tb_sound_i2s_full16.v` | `dcfifo` |
| `sim/tb_tau_cdc_gray_bus.v` | `tb_tau_cdc_gray_bus` |
| `sim/tb_tau_cdc_gray_ctr.v` | `tb_tau_cdc_gray_ctr` |
| `sim/tb_tau_cdc_sync1.v` | `tb_tau_cdc_sync1` |
| `sim/tb_tau_clut_wr.v` | `tb_tau_clut_wr` |
| `sim/tb_tau_cymo_feed.v` | `tb_tau_cymo_feed` |
| `sim/tb_tau_cymo_resamp.v` | `tb_tau_cymo_resamp` |
| `sim/tb_tau_flac_lpc.v` | `tb_tau_flac_lpc` |
| `sim/tb_tau_gain_stage.v` | `tb_tau_gain_stage` |
| `sim/tb_tau_halcyon.v` | `tb_tau_halcyon` |
| `sim/tb_tau_main_ram.v` | `tb_tau_main_ram` |
| `sim/tb_tau_mp3_poly.v` | `tb_tau_mp3_poly` |
| `sim/tb_tau_psram_async.v` | `tb_tau_psram_async` |
| `sim/tb_tau_psram_probe.v` | `tb_tau_psram_probe` |
| `sim/tb_tau_psram_wb_return_regression.v` | `tb_tau_psram_wb_return_regression` |
| `sim/tb_tau_sdram_adapter_return_probe.v` | `tb_tau_sdram_adapter_return_probe` |
| `sim/tb_tau_sdram_addr_decode.v` | `tb_tau_sdram_addr_decode` |
| `sim/tb_tau_sdram_arbiter.v` | `tb_tau_sdram_arbiter` |
| `sim/tb_tau_sdram_bridge_mux.v` | `tb_tau_sdram_bridge_mux` |
| `sim/tb_tau_sdram_composed_path.v` | `tb_tau_sdram_composed_path` |
| `sim/tb_tau_sdram_cpu_bridge.v` | `tb_tau_sdram_cpu_bridge` |
| `sim/tb_tau_sdram_cpu_return_probe.v` | `tb_tau_sdram_cpu_return_probe` |
| `sim/tb_tau_sdram_cpu_window_probe.v` | `tb_tau_sdram_cpu_window_probe` |
| `sim/tb_tau_sdram_mux_return_probe.v` | `tb_tau_sdram_mux_return_probe` |
| `sim/tb_tau_sdram_phase2_path.v` | `tb_tau_sdram_phase2_path` |
| `sim/tb_tau_sdram_wb_adapter.v` | `tb_tau_sdram_wb_adapter` |
| `sim/tb_tau_sdram_wb_return_regression.v` | `tb_tau_sdram_wb_return_regression` |
| `sim/tb_tau_spec_bank.v` | `tb_tau_spec_bank` |
| `sim/tb_tau_vs_counter.v` | `tb_tau_vs_counter` |
| `sim/tb_tau_wave_meter.v` | `tb_tau_wave_meter` |
| `sim/tb_tgt_cmd.v` | `tb_tgt_cmd` |
| `sim/tb_vexriscv.v` | `tb_vexriscv` |
| `src/fpga/apf/apf_top.v` | `apf_top` |
| `src/fpga/apf/common.v` | `synch_2`, `synch_3`, `bram_block_dp`, `bram_block_dp_nonstd` |
| `src/fpga/apf/io_bridge_peripheral.v` | `io_bridge_peripheral` |
| `src/fpga/apf/io_pad_controller.v` | `io_pad_controller` |
| `src/fpga/apf/mf_datatable.v` | `mf_datatable` |
| `src/fpga/apf/mf_ddio_bidir_12.v` | `mf_ddio_bidir_12` |
| `src/fpga/core/core_bridge_cmd.v` | `core_bridge_cmd` |
| `src/fpga/core/core_top.v` | `core_top` |
| `src/fpga/core/data_loader.sv` | `data_loader` |
| `src/fpga/core/font_rom.v` | `font_rom` |
| `src/fpga/core/mf_pllbase.v` | `mf_pllbase` |
| `src/fpga/core/mf_pllbase/mf_pllbase_0002.v` | `mf_pllbase_0002` |
| `src/fpga/core/mf_pllbase_mclk.v` | `mf_pllbase_mclk` |
| `src/fpga/core/mp3_fb.sv` | `mp3_fb` |
| `src/fpga/core/mp3_soc.v` | `mp3_soc` |
| `src/fpga/core/pcm_fifo.v` | `pcm_fifo` |
| `src/fpga/core/sound_i2s.v` | `sound_i2s` |
| `src/fpga/core/sync_fifo.v` | `sync_fifo` |
| `src/fpga/core/tau_cdc_gray_bus.sv` | `tau_cdc_gray_bus` |
| `src/fpga/core/tau_cdc_gray_ctr.sv` | `tau_cdc_gray_ctr` |
| `src/fpga/core/tau_cdc_sync1.sv` | `tau_cdc_sync1` |
| `src/fpga/core/tau_clut_wr.sv` | `tau_clut_wr` |
| `src/fpga/core/tau_cymo_feed.sv` | `tau_cymo_feed` |
| `src/fpga/core/tau_cymo_resamp.sv` | `tau_cymo_resamp` |
| `src/fpga/core/tau_flac_lpc.sv` | `tau_flac_lpc` |
| `src/fpga/core/tau_gain_stage.sv` | `tau_gain_stage` |
| `src/fpga/core/tau_halcyon.sv` | `tau_halcyon` |
| `src/fpga/core/tau_main_ram.sv` | `tau_main_ram` |
| `src/fpga/core/tau_mp3_poly.sv` | `tau_mp3_poly` |
| `src/fpga/core/tau_psram_async.sv` | `tau_psram_async` |
| `src/fpga/core/tau_psram_bus.sv` | `tau_psram_bus` |
| `src/fpga/core/tau_psram_probe.sv` | `tau_psram_probe` |
| `src/fpga/core/tau_ram_probe_a.sv` | `tau_ram_probe_a` |
| `src/fpga/core/tau_sdram_addr_decode.sv` | `tau_sdram_addr_decode` |
| `src/fpga/core/tau_sdram_arbiter.sv` | `tau_sdram_arbiter` |
| `src/fpga/core/tau_sdram_bridge_mux.sv` | `tau_sdram_bridge_mux` |
| `src/fpga/core/tau_sdram_cpu_bridge.sv` | `tau_sdram_cpu_bridge` |
| `src/fpga/core/tau_sdram_cpu_window_probe.sv` | `tau_sdram_cpu_window_probe` |
| `src/fpga/core/tau_sdram_wb_adapter.sv` | `tau_sdram_wb_adapter` |
| `src/fpga/core/tau_signaltap_tap.sv` | `tau_signaltap_tap` |
| `src/fpga/core/tau_spec_bank.sv` | `tau_spec_bank` |
| `src/fpga/core/tau_vs_counter.sv` | `tau_vs_counter` |
| `src/fpga/core/tau_wave_meter.sv` | `tau_wave_meter` |
| `src/fpga/core/tgt_cmd.v` | `tgt_cmd` |
| `src/fpga/rtl/VexRiscv_Full.v` | `VexRiscv`, `InstructionCache` |
| `src/fpga/rtl/mem/sdram_fb.sv` | `sdram_fb` |

## Removed fork sources

- `fw/eq_curve.h`
- `fw/main.c`
- `sim/tb_eq_biquad.v`
- `sim/tb_eq_cycles.v`
- `sim/test_m3u_parse.py`
- `src/fpga/core/eq_biquad.v`
- `src/fpga/core/eq_coefs.vh`
- `tools/eq_model.py`
- `tools/eye_preview.py`
- `tools/gen_eq_vectors.py`
- `tools/host/hash_check.py`
- `tools/host/hash_gen.c`
- `tools/make_album_playlists.py`
- `tools/make_playlist_index.py`
- `tools/overlay_preview.py`

Removed means absent at the old path, not proof that its implementation was eliminated everywhere. The archive holds `archive/cassette_meter/cassette_meter_code.c.txt`, preserved C code with a `.txt` suffix; it is in the tracked-file ledger and outside the source-extension percentage denominator.

## Firmware function definitions

`firmware-functions.csv` maps **767 lexical function definitions** across firmware C/header/include files to current line ranges and Git-origin counts. It includes conditional alternative definitions and host-testable header functions. It is not a compiler call graph; include-expanded reachability and complete global ownership are Phase 0 implementation gates.
