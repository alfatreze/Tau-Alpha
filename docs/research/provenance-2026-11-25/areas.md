| Area | 2026-10-18 lines | HarpMudd | % | 2026-11-25 lines | HarpMudd | % | delta HarpMudd |
|---|---:|---:|---:|---:|---:|---:|---:|
| fw/ headers read by fw2 | 2,817 | 0 | 0.0% | 2,340 | 0 | 0.0% | +0 |
| fw/ rest (old player) | 13,791 | 3,758 | 27.2% | 13,316 | 3,758 | 28.2% | +0 |
| fw2 (Player 2 firmware) | 12,752 | 20 | 0.2% | 19,936 | 13 | 0.1% | -7 |
| other (root, packaging, assets) | 609 | 77 | 12.6% | 4,219 | 86 | 2.0% | +9 |
| sdk | 0 | 0 | - | 11,659 | 38 | 0.3% | +38 |
| sim | 22,452 | 4,411 | 19.6% | 42,453 | 5,284 | 12.4% | +873 |
| src/fpga/apf (Analogue framework) | 1,020 | 1,020 | 100.0% | 1,020 | 1,020 | 100.0% | +0 |
| src/fpga/core | 24,281 | 6,411 | 26.4% | 27,643 | 6,387 | 23.1% | -24 |
| src/fpga/rtl (vendored CPU, sdram) | 6,911 | 6,843 | 99.0% | 6,441 | 6,441 | 100.0% | -402 |
| third_party | 7,173 | 6,963 | 97.1% | 7,173 | 6,963 | 97.1% | +0 |
| tools | 20,199 | 4,382 | 21.7% | 25,826 | 4,254 | 16.5% | -128 |

First-party (firmware, rtl, test, tooling) 2026-10-18: 77,261 lines, 13,996 HarpMudd-origin (18.1%)

First-party (firmware, rtl, test, tooling) 2026-11-25: 125,414 lines, 14,757 HarpMudd-origin (11.8%)

src/fpga/core by module (new vs old HarpMudd lines, sloc):
  src/fpga/core/core_bridge_cmd.v: old 442/452  new 442/452
  src/fpga/core/core_constraints.sdc: old 8/9  new 8/24
  src/fpga/core/core_game.vh: old 217/741  new 211/759
  src/fpga/core/core_top.v: old 238/243  new 238/249
  src/fpga/core/data_loader.sv: old 117/117  new 117/117
  src/fpga/core/font_ram.v: old 1211/6128  new 1211/6128
  src/fpga/core/font_rom.v: old 3052/6102  new 3052/6102
  src/fpga/core/mf_pllbase.v: old 19/20  new 19/20
  src/fpga/core/mf_pllbase/mf_pllbase_0002.v: old 76/84  new 76/84
  src/fpga/core/mf_pllbase_mclk.v: old 57/77  new 57/77
  src/fpga/core/mp3_fb.sv: old 415/971  new absent/-
  src/fpga/core/mp3_soc.v: old 285/1145  new absent/-
  src/fpga/core/pcm_fifo.v: old 63/89  new 63/89
  src/fpga/core/sound_i2s.v: old 52/73  new 52/73
  src/fpga/core/sync_fifo.v: old 59/64  new 59/64
  src/fpga/core/tau_audio_stats.sv: old absent/-  new 2/81
  src/fpga/core/tau_cymo_feed.sv: old 2/60  new 2/60
  src/fpga/core/tau_gain_stage.sv: old 4/90  new 4/90
  src/fpga/core/tau_gfx.sv: old absent/-  new 19/184
  src/fpga/core/tau_psram_probe.sv: old 11/155  new 11/155
  src/fpga/core/tau_save_ram.sv: old absent/-  new 2/72
  src/fpga/core/tau_scanout.sv: old absent/-  new 1/132
  src/fpga/core/tau_sdram_arbiter.sv: old 3/86  new absent/-
  src/fpga/core/tau_sdram_phy.sv: old absent/-  new 8/125
  src/fpga/core/tau_sdram_sched_a.sv: old absent/-  new 11/184
  src/fpga/core/tau_soc.v: old absent/-  new 282/1238
  src/fpga/core/tau_spec_bank.sv: old 2/72  new 2/72
  src/fpga/core/tau_talos.sv: old absent/-  new 360/985
  src/fpga/core/tau_wave_meter.sv: old 2/88  new 2/88
  src/fpga/core/tgt_cmd.v: old 76/83  new 76/83

fw/ files read by fw2 (new): 21
