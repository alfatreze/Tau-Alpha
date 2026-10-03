.PHONY: test-qr test-rtl-psram-ifetch check check-firmware check-fpga firmware fpga package test test-host test-rtl rtl-vectors rtl-lint test-rtl-fb test-rtl-tgt test-rtl-eq test-rtl-sdram-arbiter test-rtl-sdram-bridge test-rtl-sdram-decode test-rtl-sdram-wb-adapter test-rtl-sdram-bridge-mux test-rtl-sdram-phase2-path test-rtl-sdram-composed-path test-rtl-sdram-cpu-window-probe test-rtl-sdram-cpu-return-probe test-rtl-sdram-adapter-return-probe test-rtl-sdram-mux-return-probe test-rtl-sdram-wb-return test-rtl-sdram-controller-probe test-rtl-cdc-gray-ctr test-rtl-cdc-sync1 test-rtl-vs-counter test-rtl-spec-bank test-rtl-wave-meter test-rtl-mp3-poly test-rtl-mp3-poly-mutation test-rtl-cymo-resamp test-rtl-cymo-resamp-mutation test-rtl-cymo-feed test-rtl-cymo-feed-mutation test-rtl-gray-bus test-rtl-fb-mutation test-rtl-pcm-prime card-check visual-review

PYTHON ?= python3
QUARTUS_SH ?= quartus_sh
IVERILOG ?= iverilog
VVP ?= vvp
VERILATOR ?= verilator
RTL_BUILD_DIR ?= build/rtl
VERILATOR_LINT_FLAGS ?= --lint-only --timing -Wall -Wno-fatal

check:
	bash tools/check_build_env.sh all

check-firmware:
	bash tools/check_build_env.sh firmware

check-fpga:
	bash tools/check_build_env.sh fpga

firmware:
	bash fw/build.sh release

fpga:
	cd src/fpga && $(QUARTUS_SH) --flow compile ap_core.qpf

package:
	$(PYTHON) package.py

test: test-host test-rtl

test-host:
	$(PYTHON) tools/triad/test_director.py
	$(PYTHON) tools/check_splash_asset.py
	$(PYTHON) tools/check_tau_package.py
	$(PYTHON) tools/check_ui_snapshot_renderer.py
	$(PYTHON) tools/check_audit_trail.py
	$(PYTHON) tools/gen_meters.py --check
	$(PYTHON) tools/gen_layered_wave_tables.py --check
	$(PYTHON) tools/check_meter_deps.py
	$(PYTHON) sim/test_meter_core.py --check
	$(PYTHON) sim/test_meter_module.py
	$(PYTHON) sim/test_chladni_params.py
	$(PYTHON) sim/test_cymo_loopback.py
	@if command -v node >/dev/null 2>&1; then $(PYTHON) tools/meters/preview/build.py --check && node tools/meters/preview/test.js && $(PYTHON) sim/test_meter_golden.py && $(PYTHON) sim/test_layered_wave_golden.py && $(PYTHON) sim/test_meter_trace.py; else echo "node not found: meter preview and golden-frame tests skipped"; fi
	$(PYTHON) sim/test_lw_ring.py
	$(PYTHON) tools/gen_themes.py --check
	$(PYTHON) tools/meter_cost_estimate.py
	$(PYTHON) sim/test_psram_decode.py
	$(PYTHON) sim/test_library_index.py
	$(PYTHON) sim/test_library_fw.py
	$(PYTHON) sim/test_cold_fw.py
	$(PYTHON) sim/test_suite.py
	$(PYTHON) sim/test_install_dev_core.py
	$(PYTHON) sim/test_tau_image.py
	$(PYTHON) sim/test_art_source.py
	$(PYTHON) sim/test_ram192k.py
	$(PYTHON) sim/test_clk66.py
	$(PYTHON) sim/test_helios_beam.py
	$(PYTHON) sim/test_chladni_module.py
	$(PYTHON) sim/test_tau_timg.py
	$(PYTHON) sim/test_mp3_poly_probe.py
	$(PYTHON) sim/test_mp3_poly_fw.py
	$(PYTHON) sim/test_mp3_poly_subband.py
	$(PYTHON) sim/test_chladni_core.py
	$(PYTHON) sim/test_vu_master.py
	$(PYTHON) sim/test_helios_rect.py
	$(PYTHON) sim/test_helios_clip.py
	$(PYTHON) sim/test_start_gesture.py
	$(PYTHON) sim/test_meter_policy.py
	$(PYTHON) sim/test_pcm_push.py
	$(PYTHON) sim/test_key_repeat.py
	$(PYTHON) sim/test_headroom.py
	$(PYTHON) sim/test_wsola.py
	$(PYTHON) sim/test_tempo_funnel.py
	$(PYTHON) sim/test_theme.py
	$(PYTHON) sim/test_tau_assets.py
	$(PYTHON) sim/test_flac_lpc_symmetry.py
	$(PYTHON) sim/test_flac_lpc_fw_redirect.py
	$(PYTHON) sim/test_flac_rice_fast.py
	$(PYTHON) tools/check_art_load_order.py --check

test-rtl: test-rtl-fb test-rtl-fb-mutation test-rtl-helios-dbuf test-rtl-blit-reference test-rtl-tgt test-rtl-eq test-rtl-pcm test-rtl-pcm-prime test-rtl-eq-cycles test-rtl-sdram-arbiter test-rtl-sdram-bridge test-rtl-sdram-decode test-rtl-sdram-wb-adapter test-rtl-sdram-bridge-mux test-rtl-sdram-phase2-path test-rtl-sdram-composed-path test-rtl-sdram-cpu-window-probe test-rtl-sdram-cpu-return-probe test-rtl-sdram-adapter-return-probe test-rtl-sdram-wb-return test-rtl-sdram-controller-probe test-rtl-cdc-gray-ctr test-rtl-cdc-sync1 test-rtl-vs-counter test-rtl-spec-bank test-rtl-wave-meter test-rtl-mp3-poly test-rtl-mp3-poly-mutation test-rtl-flac-lpc test-rtl-flac-lpc-mutation test-rtl-cymo-resamp test-rtl-cymo-resamp-mutation test-rtl-cymo-feed test-rtl-cymo-feed-mutation test-rtl-gray-bus test-rtl-main-ram test-rtl-psram-idle test-rtl-psram-async test-rtl-psram-wb-return test-rtl-psram-mutation test-rtl-psram-probe test-rtl-psram-fw test-rtl-psram-ifetch

rtl-vectors:
	$(PYTHON) tools/gen_eq_vectors.py

$(RTL_BUILD_DIR):
	mkdir -p $@

$(RTL_BUILD_DIR)/tb_mp3_fb.vvp: sim/tb_mp3_fb.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-fb: $(RTL_BUILD_DIR)/tb_mp3_fb.vvp
	$(VVP) $<

# Helios H2 (B-340): the buffer-select mux and the vblank-gated flip, both with DBUF_ENABLE=1 (the real
# behaviour) and =0 (must reproduce today's addressing exactly, no matter what the flip-request/cpu_buf
# ports are driven with -- DBUF_ENABLE gates the RTL itself, not just what a well-behaved caller wires up).
test-rtl-helios-dbuf: | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -Isrc/fpga/core -o $(RTL_BUILD_DIR)/tb_helios_dbuf.vvp sim/tb_helios_dbuf.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v
	$(VVP) $(RTL_BUILD_DIR)/tb_helios_dbuf.vvp | grep -q "^PASSED"
	$(IVERILOG) -g2012 -Isrc/fpga/core -Ptb_helios_dbuf.DBUF_ENABLE=0 -o $(RTL_BUILD_DIR)/tb_helios_dbuf0.vvp sim/tb_helios_dbuf.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v
	$(VVP) $(RTL_BUILD_DIR)/tb_helios_dbuf0.vvp | grep -q "^PASSED"

# Phase F B1/B2: each mutant MUST fail this bench.
test-rtl-fb-mutation: | $(RTL_BUILD_DIR)
	@set -e; \
	run() { $(IVERILOG) -g2012 $$1 -o $(RTL_BUILD_DIR)/tb_mp3_fb_mut.vvp sim/tb_mp3_fb.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v; \
	  if $(VVP) $(RTL_BUILD_DIR)/tb_mp3_fb_mut.vvp | grep -q "^FAILED"; then echo "mutant killed: $$1"; \
	  else echo "MUTANT SURVIVED: $$1"; exit 1; fi; }; \
	run -Ptb_mp3_fb.BUG_IGNORE_BLIT_STRIDE=1; \
	run -Ptb_mp3_fb.BUG_IGNORE_KEY=1; \
	run -Ptb_mp3_fb.BUG_SBLIT_NO_SCALE=1; \
	run -Ptb_mp3_fb.BUG_BLEND_ALWAYS_SRC=1; \
	run -Ptb_mp3_fb.BUG_IGNORE_RC_CUT=1; \
	run -Ptb_mp3_fb.BUG_IGNORE_BAR_HI=1

# PHASE_F_SPEC.md section 12: software reference renderer + pixel-diff
# fixtures, run the whole scene through the RTL sim and through
# tools/host/blit_reference.py, diff exactly, and confirm every existing
# mutation hook is caught by the diff (the "injected-fault case" section 12
# asks for). Needs its own recipe, not a .vvp rule, since it invokes iverilog
# itself (once clean, once per mutation).
test-rtl-blit-reference: | $(RTL_BUILD_DIR)
	$(PYTHON) sim/test_blit_reference.py

$(RTL_BUILD_DIR)/tb_tgt_cmd.vvp: sim/tb_tgt_cmd.v src/fpga/core/tgt_cmd.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-tgt: $(RTL_BUILD_DIR)/tb_tgt_cmd.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_eq_biquad.vvp: sim/tb_eq_biquad.v src/fpga/core/eq_biquad.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2005-sv -I src/fpga/core -o $@ $^

test-rtl-eq: rtl-vectors $(RTL_BUILD_DIR)/tb_eq_biquad.vvp
	$(VVP) $(RTL_BUILD_DIR)/tb_eq_biquad.vvp

$(RTL_BUILD_DIR)/tb_pcm_decay.vvp: sim/tb_pcm_decay.v src/fpga/core/pcm_fifo.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-pcm: $(RTL_BUILD_DIR)/tb_pcm_decay.vvp
	$(VVP) $<

# Ported from HarpMudd upstream v1.5.0 (4d396bf): start-of-track priming,
# checked separately from the decay bench above.
$(RTL_BUILD_DIR)/tb_pcm_fifo.vvp: sim/tb_pcm_fifo.v src/fpga/core/pcm_fifo.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-pcm-prime: $(RTL_BUILD_DIR)/tb_pcm_fifo.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_eq_cycles.vvp: sim/tb_eq_cycles.v src/fpga/core/eq_biquad.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -I src/fpga/core -o $@ $^

test-rtl-eq-cycles: rtl-vectors $(RTL_BUILD_DIR)/tb_eq_cycles.vvp
	$(VVP) $(RTL_BUILD_DIR)/tb_eq_cycles.vvp

$(RTL_BUILD_DIR)/tb_tau_sdram_arbiter.vvp: sim/tb_tau_sdram_arbiter.v src/fpga/core/tau_sdram_arbiter.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-arbiter: $(RTL_BUILD_DIR)/tb_tau_sdram_arbiter.vvp
	$(VVP) $<

# ---- Phase F B7: SDRAM busy-cycle counter's clock-domain crossing ------------
$(RTL_BUILD_DIR)/tb_tau_cdc_gray_ctr.vvp: sim/tb_tau_cdc_gray_ctr.v src/fpga/core/tau_cdc_gray_ctr.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-cdc-gray-ctr: $(RTL_BUILD_DIR)/tb_tau_cdc_gray_ctr.vvp
	$(VVP) $<

# ---- Helios/Talos H0: vblank status's single-bit clock-domain crossing (docs/HELIOS_SPEC.md section 9) ----
$(RTL_BUILD_DIR)/tb_tau_cdc_sync1.vvp: sim/tb_tau_cdc_sync1.v src/fpga/core/tau_cdc_sync1.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-gray-bus: $(RTL_BUILD_DIR)/tb_tau_cdc_gray_bus.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_cdc_gray_bus.vvp: sim/tb_tau_cdc_gray_bus.v src/fpga/core/tau_cdc_gray_bus.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-spec-bank: $(RTL_BUILD_DIR)/tb_tau_spec_bank.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_spec_bank.vvp: sim/tb_tau_spec_bank.v src/fpga/core/tau_spec_bank.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

# MP3 window unit (B-292): the vectors come from the golden model, itself checked against Helix's real PolyphaseStereo.
MP3_POLY_SRC = sim/tb_tau_mp3_poly.v src/fpga/core/tau_mp3_poly.sv src/fpga/core/tau_mp3_poly_rom.svh
$(RTL_BUILD_DIR)/mp3_poly_vectors.txt: sim/test_mp3_poly_model.py sim/mp3_poly_model.c sim/mp3_poly_map.c tools/gen_mp3_poly_rom.py | $(RTL_BUILD_DIR)
	$(PYTHON) sim/test_mp3_poly_model.py

# FLAC LPC reconstruction unit (docs/research/FLAC_LPC_KERNEL_DESIGN.md, B-364..B-366): unlike the MP3 window
# unit, coefficients are per-frame/streamed, not a fixed ROM -- the model IS the reference (no real decoder
# to link against), checked against an unbounded int64_t computation of fw/flac.c's own arithmetic.
# No RTL testbench consumes these vectors yet (design doc build order item 3, not started).
$(RTL_BUILD_DIR)/flac_lpc_vectors.txt: sim/test_flac_lpc_model.py sim/flac_lpc_model.c | $(RTL_BUILD_DIR)
	$(PYTHON) sim/test_flac_lpc_model.py

$(RTL_BUILD_DIR)/tb_mp3_poly.vvp: $(MP3_POLY_SRC) | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -I src/fpga/core -o $@ sim/tb_tau_mp3_poly.v src/fpga/core/tau_mp3_poly.sv

test-rtl-mp3-poly: $(RTL_BUILD_DIR)/tb_mp3_poly.vvp $(RTL_BUILD_DIR)/mp3_poly_vectors.txt
	$(VVP) $<

# each mutant MUST fail the bench (no rounding constant, +c2 for -c2, history age off by one, no clip)
test-rtl-mp3-poly-mutation: $(RTL_BUILD_DIR)/mp3_poly_vectors.txt
	@set -e; for b in 1 2 3 4; do \
	  $(IVERILOG) -g2012 -I src/fpga/core -Ptb_tau_mp3_poly.BUG=$$b -o $(RTL_BUILD_DIR)/mp3_poly_mut.vvp sim/tb_tau_mp3_poly.v src/fpga/core/tau_mp3_poly.sv; \
	  if $(VVP) $(RTL_BUILD_DIR)/mp3_poly_mut.vvp | grep -q "^FAILED"; then echo "mutant killed: BUG=$$b"; else echo "MUTANT SURVIVED: BUG=$$b"; exit 1; fi; done

$(RTL_BUILD_DIR)/tb_flac_lpc.vvp: sim/tb_tau_flac_lpc.v src/fpga/core/tau_flac_lpc.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -I src/fpga/core -o $@ sim/tb_tau_flac_lpc.v src/fpga/core/tau_flac_lpc.sv

test-rtl-flac-lpc: $(RTL_BUILD_DIR)/tb_flac_lpc.vvp $(RTL_BUILD_DIR)/flac_lpc_vectors.txt
	$(VVP) $<

# each mutant MUST fail the bench (history index reversed, no shift, sign-extension dropped, one tap short, no history push)
test-rtl-flac-lpc-mutation: $(RTL_BUILD_DIR)/flac_lpc_vectors.txt
	@set -e; for b in 1 2 3 4 5; do \
	  $(IVERILOG) -g2012 -I src/fpga/core -Ptb_tau_flac_lpc.BUG=$$b -o $(RTL_BUILD_DIR)/flac_lpc_mut.vvp sim/tb_tau_flac_lpc.v src/fpga/core/tau_flac_lpc.sv; \
	  if $(VVP) $(RTL_BUILD_DIR)/flac_lpc_mut.vvp | grep -q "^FAILED"; then echo "mutant killed: BUG=$$b"; else echo "MUTANT SURVIVED: BUG=$$b"; exit 1; fi; done

# Cymo polyphase FIR resampler, 44100:48000 only (docs/features/CYMO_AUDIO_ENGINE.md section 15, B-467+):
# a new design, not a port -- sim/cymo_resamp_model.c IS the reference, independently cross-checked in
# Python by sim/test_cymo_resamp_model.py (this project's own "prove it twice, differently" discipline).
# Standalone unit, NOT yet wired into pcm_fifo.v/mp3_soc.v.
CYMO_RESAMP_SRC = sim/tb_tau_cymo_resamp.v src/fpga/core/tau_cymo_resamp.sv src/fpga/core/tau_cymo_resamp_rom.svh
$(RTL_BUILD_DIR)/cymo_resamp_vectors.txt: sim/test_cymo_resamp_model.py sim/cymo_resamp_model.c tools/gen_cymo_resamp_rom.py | $(RTL_BUILD_DIR)
	$(PYTHON) sim/test_cymo_resamp_model.py

$(RTL_BUILD_DIR)/tb_cymo_resamp.vvp: $(CYMO_RESAMP_SRC) | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -I src/fpga/core -o $@ sim/tb_tau_cymo_resamp.v src/fpga/core/tau_cymo_resamp.sv

test-rtl-cymo-resamp: $(RTL_BUILD_DIR)/tb_cymo_resamp.vvp $(RTL_BUILD_DIR)/cymo_resamp_vectors.txt
	$(VVP) $<

# each mutant MUST fail the bench (tap/history index reversed, no shift, sign-extension dropped, wrong phase step, pop_req never asserted)
test-rtl-cymo-resamp-mutation: $(RTL_BUILD_DIR)/cymo_resamp_vectors.txt
	@set -e; for b in 1 2 3 4 5; do \
	  $(IVERILOG) -g2012 -I src/fpga/core -Ptb_tau_cymo_resamp.BUG=$$b -o $(RTL_BUILD_DIR)/cymo_resamp_mut.vvp sim/tb_tau_cymo_resamp.v src/fpga/core/tau_cymo_resamp.sv; \
	  if $(VVP) $(RTL_BUILD_DIR)/cymo_resamp_mut.vvp | grep -q "^FAILED"; then echo "mutant killed: BUG=$$b"; else echo "MUTANT SURVIVED: BUG=$$b"; exit 1; fi; done

# B-527: the hand-off between the track-rate tick and the resampler, against the REAL resampler at the real clock ratio (a counting ramp, so a dropped or
# repeated input sample is a break in the pushed sequence). The mutant is the old tick-gated hand-off, which must fail.
CYMO_FEED_SRC = sim/tb_tau_cymo_feed.v src/fpga/core/tau_cymo_feed.sv src/fpga/core/tau_cymo_resamp.sv src/fpga/core/tau_cymo_resamp_rom.svh
$(RTL_BUILD_DIR)/tb_cymo_feed.vvp: $(CYMO_FEED_SRC) | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -I src/fpga/core -o $@ sim/tb_tau_cymo_feed.v src/fpga/core/tau_cymo_feed.sv src/fpga/core/tau_cymo_resamp.sv

test-rtl-cymo-feed: $(RTL_BUILD_DIR)/tb_cymo_feed.vvp
	$(VVP) $<

test-rtl-cymo-feed-mutation: $(CYMO_FEED_SRC) | $(RTL_BUILD_DIR)
	@set -e; $(IVERILOG) -g2012 -I src/fpga/core -Ptb_tau_cymo_feed.BUG=1 -o $(RTL_BUILD_DIR)/cymo_feed_mut.vvp sim/tb_tau_cymo_feed.v src/fpga/core/tau_cymo_feed.sv src/fpga/core/tau_cymo_resamp.sv; \
	  if $(VVP) $(RTL_BUILD_DIR)/cymo_feed_mut.vvp | grep -q "^FAILED"; then echo "mutant killed: BUG=1 (the old tick-gated hand-off)"; else echo "MUTANT SURVIVED: BUG=1"; exit 1; fi

test-rtl-wave-meter: $(RTL_BUILD_DIR)/tb_tau_wave_meter.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_wave_meter.vvp: sim/tb_tau_wave_meter.v src/fpga/core/tau_wave_meter.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-vs-counter: $(RTL_BUILD_DIR)/tb_tau_vs_counter.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_vs_counter.vvp: sim/tb_tau_vs_counter.v src/fpga/core/tau_vs_counter.sv src/fpga/core/tau_cdc_sync1.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-cdc-sync1: $(RTL_BUILD_DIR)/tb_tau_cdc_sync1.vvp
	$(VVP) $<

# ---- Phase G RAM-shrink: main RAM's two-region split (docs/PHASE_F_SPEC.md section 4/4.1) ----
$(RTL_BUILD_DIR)/tb_tau_main_ram.vvp: sim/tb_tau_main_ram.v src/fpga/core/tau_main_ram.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-main-ram: $(RTL_BUILD_DIR)/tb_tau_main_ram.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_cpu_bridge.vvp: sim/tb_tau_sdram_cpu_bridge.v src/fpga/core/tau_sdram_cpu_bridge.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-bridge: $(RTL_BUILD_DIR)/tb_tau_sdram_cpu_bridge.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_addr_decode.vvp: sim/tb_tau_sdram_addr_decode.v src/fpga/core/tau_sdram_addr_decode.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-decode: $(RTL_BUILD_DIR)/tb_tau_sdram_addr_decode.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_wb_adapter.vvp: sim/tb_tau_sdram_wb_adapter.v src/fpga/core/tau_sdram_wb_adapter.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-wb-adapter: $(RTL_BUILD_DIR)/tb_tau_sdram_wb_adapter.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_bridge_mux.vvp: sim/tb_tau_sdram_bridge_mux.v src/fpga/core/tau_sdram_bridge_mux.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-bridge-mux: $(RTL_BUILD_DIR)/tb_tau_sdram_bridge_mux.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_phase2_path.vvp: sim/tb_tau_sdram_phase2_path.v src/fpga/core/tau_sdram_addr_decode.sv src/fpga/core/tau_sdram_wb_adapter.sv src/fpga/core/tau_sdram_bridge_mux.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-phase2-path: $(RTL_BUILD_DIR)/tb_tau_sdram_phase2_path.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_composed_path.vvp: sim/tb_tau_sdram_composed_path.v src/fpga/core/tau_sdram_wb_adapter.sv src/fpga/core/tau_sdram_bridge_mux.sv src/fpga/core/tau_sdram_cpu_bridge.sv src/fpga/core/tau_sdram_arbiter.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-composed-path: $(RTL_BUILD_DIR)/tb_tau_sdram_composed_path.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_cpu_window_probe.vvp: sim/tb_tau_sdram_cpu_window_probe.v src/fpga/core/tau_sdram_cpu_window_probe.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -s tb_tau_sdram_cpu_window_probe -o $@ $^

test-rtl-sdram-cpu-window-probe: $(RTL_BUILD_DIR)/tb_tau_sdram_cpu_window_probe.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_cpu_return_probe.vvp: sim/tb_tau_sdram_cpu_return_probe.v src/fpga/core/tau_sdram_cpu_window_probe.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -s tb_tau_sdram_cpu_return_probe -o $@ $^

test-rtl-sdram-cpu-return-probe: $(RTL_BUILD_DIR)/tb_tau_sdram_cpu_return_probe.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_adapter_return_probe.vvp: sim/tb_tau_sdram_adapter_return_probe.v src/fpga/core/tau_sdram_cpu_window_probe.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -s tb_tau_sdram_adapter_return_probe -o $@ $^

test-rtl-sdram-adapter-return-probe: $(RTL_BUILD_DIR)/tb_tau_sdram_adapter_return_probe.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_mux_return_probe.vvp: sim/tb_tau_sdram_mux_return_probe.v src/fpga/core/tau_sdram_cpu_window_probe.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -s tb_tau_sdram_mux_return_probe -o $@ $^

test-rtl-sdram-mux-return-probe: $(RTL_BUILD_DIR)/tb_tau_sdram_mux_return_probe.vvp
	$(VVP) $<

$(RTL_BUILD_DIR)/tb_tau_sdram_wb_return_regression.vvp: sim/tb_tau_sdram_wb_return_regression.v src/fpga/core/tau_sdram_wb_adapter.sv src/fpga/core/tau_sdram_bridge_mux.sv src/fpga/core/tau_sdram_cpu_bridge.sv src/fpga/core/tau_sdram_arbiter.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -s tb_tau_sdram_wb_return_regression -o $@ $^

test-rtl-sdram-wb-return: $(RTL_BUILD_DIR)/tb_tau_sdram_wb_return_regression.vvp
	$(VVP) $<

# ---- PSRAM (P0/P1): idle-pin static check, controller, bus regression, mutations
PSRAM_SRC = src/fpga/core/tau_psram_async.sv src/fpga/core/tau_psram_bus.sv sim/psram_chip_model.v

test-rtl-psram-idle:
	$(PYTHON) tools/check_psram_idle.py

$(RTL_BUILD_DIR)/tb_tau_psram_async.vvp: sim/tb_tau_psram_async.v $(PSRAM_SRC) | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ sim/tb_tau_psram_async.v $(PSRAM_SRC)

test-rtl-psram-async: $(RTL_BUILD_DIR)/tb_tau_psram_async.vvp
	$(VVP) $< | tail -4 | tee $(RTL_BUILD_DIR)/psram_async.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_async.log
	$(IVERILOG) -g2012 -Ptb_tau_psram_async.IO_NS=15.0 -o $(RTL_BUILD_DIR)/tb_tau_psram_async_io.vvp sim/tb_tau_psram_async.v $(PSRAM_SRC)
	$(VVP) $(RTL_BUILD_DIR)/tb_tau_psram_async_io.vvp | tail -4 | tee $(RTL_BUILD_DIR)/psram_async_io.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_async_io.log

$(RTL_BUILD_DIR)/tb_tau_psram_wb_return_regression.vvp: sim/tb_tau_psram_wb_return_regression.v $(PSRAM_SRC) | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ sim/tb_tau_psram_wb_return_regression.v $(PSRAM_SRC)

test-rtl-psram-wb-return: $(RTL_BUILD_DIR)/tb_tau_psram_wb_return_regression.vvp
	$(VVP) $< | tail -4 | tee $(RTL_BUILD_DIR)/psram_wb.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_wb.log
	$(IVERILOG) -g2012 -Ptb_tau_psram_wb_return_regression.GUARD_ERR=0 -o $(RTL_BUILD_DIR)/tb_tau_psram_wb_guard_ack.vvp sim/tb_tau_psram_wb_return_regression.v $(PSRAM_SRC)
	$(VVP) $(RTL_BUILD_DIR)/tb_tau_psram_wb_guard_ack.vvp | tail -4 | tee $(RTL_BUILD_DIR)/psram_wb_ack.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_wb_ack.log

# Each mutant MUST fail; a mutant that passes means the tests are toothless.
test-rtl-psram-mutation: | $(RTL_BUILD_DIR)
	@set -e; \
	run() { $(IVERILOG) -g2012 $$1 -o $(RTL_BUILD_DIR)/psram_mut.vvp $$2 $(PSRAM_SRC); \
	  if $(VVP) $(RTL_BUILD_DIR)/psram_mut.vvp | grep -q "^FAILED"; then echo "mutant killed: $$1"; \
	  else echo "MUTANT SURVIVED: $$1"; exit 1; fi; }; \
	run -Ptb_tau_psram_async.T_ACC=4 sim/tb_tau_psram_async.v; \
	run -Ptb_tau_psram_async.T_ACC=6 sim/tb_tau_psram_async.v; \
	run -Ptb_tau_psram_async.T_ACC=7 sim/tb_tau_psram_async.v; \
	run "-Ptb_tau_psram_async.T_ACC=8 -Ptb_tau_psram_async.IO_NS=15.0" sim/tb_tau_psram_async.v; \
	run -Ptb_tau_psram_wb_return_regression.REL_CYC=1 sim/tb_tau_psram_wb_return_regression.v; \
	run -Ptb_tau_psram_wb_return_regression.MUT_EARLY_ACK=1 sim/tb_tau_psram_wb_return_regression.v

# ---- PSRAM P2: mailbox test, and the real firmware on the real CPU -----------
$(RTL_BUILD_DIR)/tb_tau_psram_probe.vvp: sim/tb_tau_psram_probe.v src/fpga/core/tau_psram_probe.sv $(PSRAM_SRC) | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ sim/tb_tau_psram_probe.v src/fpga/core/tau_psram_probe.sv $(PSRAM_SRC)

test-rtl-psram-probe: $(RTL_BUILD_DIR)/tb_tau_psram_probe.vvp
	$(VVP) $< | tail -3 | tee $(RTL_BUILD_DIR)/psram_probe.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_probe.log
	$(IVERILOG) -g2012 -Ptb_tau_psram_probe.WD=8 -o $(RTL_BUILD_DIR)/tb_tau_psram_probe_wd8.vvp sim/tb_tau_psram_probe.v src/fpga/core/tau_psram_probe.sv $(PSRAM_SRC)
	$(VVP) $(RTL_BUILD_DIR)/tb_tau_psram_probe_wd8.vvp | tail -3 | tee $(RTL_BUILD_DIR)/psram_probe_wd8.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_probe_wd8.log

PSRAM_FW_SRC = sim/tb_psram_fw.v $(RTL_BUILD_DIR)/mp3_soc_sim.v src/fpga/rtl/VexRiscv_Full.v src/fpga/core/pcm_fifo.v src/fpga/core/eq_biquad.v src/fpga/core/tau_sdram_addr_decode.sv src/fpga/core/tau_sdram_wb_adapter.sv src/fpga/core/tau_psram_probe.sv src/fpga/core/tau_cdc_sync1.sv $(PSRAM_SRC)

$(RTL_BUILD_DIR)/mp3_soc_sim.v: src/fpga/core/mp3_soc.v sim/make_soc_sim.py | $(RTL_BUILD_DIR)
	$(PYTHON) sim/make_soc_sim.py $< $@

# Builds the short-fill ROM, runs it on the real VexRiscv against the RTL and
# the strict chip model (good run, then one injected bit flip that MUST be
# reported), and verifies both published records independently in Python.
test-rtl-psram-fw: $(RTL_BUILD_DIR)/mp3_soc_sim.v
	bash fw/build.sh psram-diag-sim > $(RTL_BUILD_DIR)/psram_fw_build.log 2>&1 || { cat $(RTL_BUILD_DIR)/psram_fw_build.log; exit 1; }
	$(IVERILOG) -g2012 -Isrc/fpga/core -o $(RTL_BUILD_DIR)/tb_psram_fw.vvp $(PSRAM_FW_SRC)
	$(VVP) $(RTL_BUILD_DIR)/tb_psram_fw.vvp +OUT=$(RTL_BUILD_DIR)/psram_fw_record.txt | tail -4 | tee $(RTL_BUILD_DIR)/psram_fw.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_fw.log
	$(PYTHON) sim/check_psram_fw_record.py $(RTL_BUILD_DIR)/psram_fw_record.txt
	$(IVERILOG) -g2012 -Isrc/fpga/core -Ptb_psram_fw.FAULT=1 -o $(RTL_BUILD_DIR)/tb_psram_fw_fault.vvp $(PSRAM_FW_SRC)
	$(VVP) $(RTL_BUILD_DIR)/tb_psram_fw_fault.vvp +OUT=$(RTL_BUILD_DIR)/psram_fw_record_fault.txt > /dev/null
	$(PYTHON) sim/check_psram_fw_record.py $(RTL_BUILD_DIR)/psram_fw_record_fault.txt --expect-fault

# Phase G2: real CPU executing code from PSRAM through the instruction alias, sharing the controller with the data window.
# Also builds without the feature (the firmware must then report NOFEATURE, proving the netlist is inert).
PSRAM_IFETCH_SRC = sim/tb_psram_ifetch.v $(RTL_BUILD_DIR)/mp3_soc_sim.v src/fpga/rtl/VexRiscv_Full.v src/fpga/core/pcm_fifo.v src/fpga/core/eq_biquad.v src/fpga/core/tau_sdram_addr_decode.sv src/fpga/core/tau_sdram_wb_adapter.sv src/fpga/core/tau_psram_probe.sv src/fpga/core/tau_cdc_sync1.sv $(PSRAM_SRC)
test-rtl-psram-ifetch: $(RTL_BUILD_DIR)/mp3_soc_sim.v
	toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc -march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments -T sim/fw_ifetch/link.ld sim/fw_ifetch/start.S sim/fw_ifetch/main.c -o $(RTL_BUILD_DIR)/fw_ifetch.elf
	toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-objcopy -O binary $(RTL_BUILD_DIR)/fw_ifetch.elf $(RTL_BUILD_DIR)/fw_ifetch.bin
	$(IVERILOG) -g2012 -Isrc/fpga/core -o $(RTL_BUILD_DIR)/tb_psram_ifetch.vvp $(PSRAM_IFETCH_SRC)
	$(VVP) $(RTL_BUILD_DIR)/tb_psram_ifetch.vvp +ROM=$(RTL_BUILD_DIR)/fw_ifetch.bin | tee $(RTL_BUILD_DIR)/psram_ifetch.log | tail -30; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_ifetch.log
	$(IVERILOG) -g2012 -Isrc/fpga/core -Ptb_psram_ifetch.IFETCH=0 -Ptb_psram_ifetch.EXPECT_NOFEATURE=1 -o $(RTL_BUILD_DIR)/tb_psram_ifetch_off.vvp $(PSRAM_IFETCH_SRC)
	$(VVP) $(RTL_BUILD_DIR)/tb_psram_ifetch_off.vvp +ROM=$(RTL_BUILD_DIR)/fw_ifetch.bin | tail -4 | tee $(RTL_BUILD_DIR)/psram_ifetch_off.log; grep -q "^PASSED" $(RTL_BUILD_DIR)/psram_ifetch_off.log

# Verilator's generated GNUmakefiles cannot run beneath this repository's path
# because it contains spaces. Keep this tool-only artefact outside the tree.
test-rtl-sdram-controller-probe:
	$(VERILATOR) --binary --timing -DSIM -DTAU_PHASE2_WINDOW -DTAU_PHASE2_PROBE --top-module tb_sdram_fb_controller_probe -Wno-fatal -Wno-TIMESCALEMOD -Wno-REALCVT -Mdir /tmp/tau_sdram_fb_controller_probe sim/tb_sdram_fb_controller_probe.v src/fpga/rtl/mem/sdram_fb.sv
	/tmp/tau_sdram_fb_controller_probe/Vtb_sdram_fb_controller_probe

rtl-lint:
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module mp3_fb src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tgt_cmd src/fpga/core/tgt_cmd.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module eq_biquad -Isrc/fpga/core src/fpga/core/eq_biquad.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module pcm_fifo src/fpga/core/pcm_fifo.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_arbiter src/fpga/core/tau_sdram_arbiter.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_flac_lpc src/fpga/core/tau_flac_lpc.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_cymo_resamp -Isrc/fpga/core src/fpga/core/tau_cymo_resamp.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_cymo_feed src/fpga/core/tau_cymo_feed.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_cpu_bridge src/fpga/core/tau_sdram_cpu_bridge.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_addr_decode src/fpga/core/tau_sdram_addr_decode.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_wb_adapter src/fpga/core/tau_sdram_wb_adapter.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_bridge_mux src/fpga/core/tau_sdram_bridge_mux.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_cdc_gray_ctr src/fpga/core/tau_cdc_gray_ctr.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_cdc_sync1 src/fpga/core/tau_cdc_sync1.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_cdc_gray_bus src/fpga/core/tau_cdc_gray_bus.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_spec_bank src/fpga/core/tau_spec_bank.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_wave_meter src/fpga/core/tau_wave_meter.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_mp3_poly -Isrc/fpga/core src/fpga/core/tau_mp3_poly.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_vs_counter src/fpga/core/tau_cdc_sync1.sv src/fpga/core/tau_vs_counter.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_main_ram src/fpga/core/tau_main_ram.sv

card-check:
	$(PYTHON) tools/library_check.py

# Decode the actual packaged RGB565/RLE loading asset, rather than previewing
# the source file.  This is a design-review aid and never alters release files.
visual-review:
	$(PYTHON) tools/visual_review.py

# QR encoder vs segno (needs work/venv-qr; about 3 min, so not part of test-host)
test-qr:
	work/venv-qr/bin/python sim/test_qr.py

# B-333: the 192 KB firmware links (release + Diagnostic Build) and the normal release is unchanged. Slow (three firmware builds).
test-ram192k:
	$(PYTHON) sim/test_ram192k.py --build

# B-338: the CLK66=1 firmware link and the RAM_192K/CLK66 mutual-exclusion refusal. Slow (a firmware build).
test-clk66:
	$(PYTHON) sim/test_clk66.py --build
