.PHONY: check check-firmware check-fpga firmware firmware-advanced firmware-sdram-stress firmware-sdram-cpu-diag firmware-sdram-cpu-readback fpga package test test-host test-rtl rtl-vectors rtl-lint test-rtl-fb test-rtl-tgt test-rtl-eq test-rtl-sdram-arbiter test-rtl-sdram-bridge test-rtl-sdram-decode test-rtl-sdram-wb-adapter test-rtl-sdram-bridge-mux test-rtl-sdram-phase2-path test-rtl-sdram-composed-path test-rtl-sdram-cpu-window-probe test-rtl-sdram-cpu-return-probe test-rtl-sdram-controller-probe card-check visual-review

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
	bash fw/build.sh player

# Capability build for the future in-app Advanced section. No advanced option
# becomes visible merely by compiling this profile; the user must opt in at
# runtime once the settings screen exists.
firmware-advanced:
	EXTRA_CFLAGS="-DTAU_ADVANCED_BUILD=1" bash fw/build.sh player

# Separate developer artifact: the normal TAU ROM remains untouched.
firmware-sdram-stress:
	EXTRA_CFLAGS="-Os" bash fw/build.sh player-stress

# Separate Phase 2 diagnostic ROM. It performs real CPU loads/stores through
# the opt-in uncached SDRAM mapping, rather than the Phase 1 mailbox path.
firmware-sdram-cpu-diag:
	bash fw/build.sh sdram-cpu-diag

firmware-sdram-cpu-readback:
	bash fw/build.sh sdram-cpu-readback

fpga:
	cd src/fpga && $(QUARTUS_SH) --flow compile ap_core.qpf

package:
	$(PYTHON) package.py

test: test-host test-rtl

test-host:
	$(PYTHON) tools/triad/test_director.py
	$(PYTHON) sim/test_m3u_parse.py
	$(PYTHON) tools/check_splash_asset.py
	$(PYTHON) tools/check_tau_package.py
	$(PYTHON) tools/check_ui_snapshot_renderer.py
	$(PYTHON) tools/check_audit_trail.py

test-rtl: test-rtl-fb test-rtl-tgt test-rtl-eq test-rtl-pcm test-rtl-eq-cycles test-rtl-sdram-arbiter test-rtl-sdram-bridge test-rtl-sdram-decode test-rtl-sdram-wb-adapter test-rtl-sdram-bridge-mux test-rtl-sdram-phase2-path test-rtl-sdram-composed-path test-rtl-sdram-cpu-window-probe test-rtl-sdram-cpu-return-probe test-rtl-sdram-controller-probe

rtl-vectors:
	$(PYTHON) tools/gen_eq_vectors.py

$(RTL_BUILD_DIR):
	mkdir -p $@

$(RTL_BUILD_DIR)/tb_mp3_fb.vvp: sim/tb_mp3_fb.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-fb: $(RTL_BUILD_DIR)/tb_mp3_fb.vvp
	$(VVP) $<

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

$(RTL_BUILD_DIR)/tb_eq_cycles.vvp: sim/tb_eq_cycles.v src/fpga/core/eq_biquad.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -I src/fpga/core -o $@ $^

test-rtl-eq-cycles: rtl-vectors $(RTL_BUILD_DIR)/tb_eq_cycles.vvp
	$(VVP) $(RTL_BUILD_DIR)/tb_eq_cycles.vvp

$(RTL_BUILD_DIR)/tb_tau_sdram_arbiter.vvp: sim/tb_tau_sdram_arbiter.v src/fpga/core/tau_sdram_arbiter.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-sdram-arbiter: $(RTL_BUILD_DIR)/tb_tau_sdram_arbiter.vvp
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

# Verilator's generated GNUmakefiles cannot run beneath this repository's path
# because it contains spaces. Keep this tool-only artefact outside the tree.
test-rtl-sdram-controller-probe:
	$(VERILATOR) --binary --timing -DSIM -DTAU_PHASE2_WINDOW --top-module tb_sdram_fb_controller_probe -Wno-fatal -Wno-TIMESCALEMOD -Wno-REALCVT -Mdir /tmp/tau_sdram_fb_controller_probe sim/tb_sdram_fb_controller_probe.v src/fpga/rtl/mem/sdram_fb.sv
	/tmp/tau_sdram_fb_controller_probe/Vtb_sdram_fb_controller_probe

rtl-lint:
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module mp3_fb src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tgt_cmd src/fpga/core/tgt_cmd.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module eq_biquad -Isrc/fpga/core src/fpga/core/eq_biquad.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module pcm_fifo src/fpga/core/pcm_fifo.v
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_arbiter src/fpga/core/tau_sdram_arbiter.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_cpu_bridge src/fpga/core/tau_sdram_cpu_bridge.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_addr_decode src/fpga/core/tau_sdram_addr_decode.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_wb_adapter src/fpga/core/tau_sdram_wb_adapter.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_bridge_mux src/fpga/core/tau_sdram_bridge_mux.sv

card-check:
	$(PYTHON) tools/library_check.py

# Decode the actual packaged RGB565/RLE loading asset, rather than previewing
# the source file.  This is a design-review aid and never alters release files.
visual-review:
	$(PYTHON) tools/visual_review.py
