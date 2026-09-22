.PHONY: test-qr test-rtl-psram-ifetch check check-firmware check-fpga firmware firmware-advanced firmware-sdram-stress firmware-sdram-cpu-diag firmware-sdram-cpu-readback fpga package test test-host test-rtl rtl-vectors rtl-lint test-rtl-fb test-rtl-tgt test-rtl-eq test-rtl-sdram-arbiter test-rtl-sdram-bridge test-rtl-sdram-decode test-rtl-sdram-wb-adapter test-rtl-sdram-bridge-mux test-rtl-sdram-phase2-path test-rtl-sdram-composed-path test-rtl-sdram-cpu-window-probe test-rtl-sdram-cpu-return-probe test-rtl-sdram-adapter-return-probe test-rtl-sdram-mux-return-probe test-rtl-sdram-wb-return test-rtl-sdram-controller-probe test-rtl-cdc-gray-ctr test-rtl-fb-mutation card-check visual-review

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
	$(PYTHON) sim/test_psram_decode.py
	$(PYTHON) sim/test_library_index.py
	$(PYTHON) sim/test_library_fw.py
	$(PYTHON) sim/test_cold_fw.py
	$(PYTHON) sim/test_suite.py

test-rtl: test-rtl-fb test-rtl-fb-mutation test-rtl-tgt test-rtl-eq test-rtl-pcm test-rtl-eq-cycles test-rtl-sdram-arbiter test-rtl-sdram-bridge test-rtl-sdram-decode test-rtl-sdram-wb-adapter test-rtl-sdram-bridge-mux test-rtl-sdram-phase2-path test-rtl-sdram-composed-path test-rtl-sdram-cpu-window-probe test-rtl-sdram-cpu-return-probe test-rtl-sdram-adapter-return-probe test-rtl-sdram-wb-return test-rtl-sdram-controller-probe test-rtl-cdc-gray-ctr test-rtl-psram-idle test-rtl-psram-async test-rtl-psram-wb-return test-rtl-psram-mutation test-rtl-psram-probe test-rtl-psram-fw test-rtl-psram-ifetch

rtl-vectors:
	$(PYTHON) tools/gen_eq_vectors.py

$(RTL_BUILD_DIR):
	mkdir -p $@

$(RTL_BUILD_DIR)/tb_mp3_fb.vvp: sim/tb_mp3_fb.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-fb: $(RTL_BUILD_DIR)/tb_mp3_fb.vvp
	$(VVP) $<

# Phase F B1/B2: each mutant MUST fail this bench.
test-rtl-fb-mutation: | $(RTL_BUILD_DIR)
	@set -e; \
	run() { $(IVERILOG) -g2012 $$1 -o $(RTL_BUILD_DIR)/tb_mp3_fb_mut.vvp sim/tb_mp3_fb.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v; \
	  if $(VVP) $(RTL_BUILD_DIR)/tb_mp3_fb_mut.vvp | grep -q "^FAILED"; then echo "mutant killed: $$1"; \
	  else echo "MUTANT SURVIVED: $$1"; exit 1; fi; }; \
	run -Ptb_mp3_fb.BUG_IGNORE_BLIT_STRIDE=1; \
	run -Ptb_mp3_fb.BUG_IGNORE_KEY=1; \
	run -Ptb_mp3_fb.BUG_SBLIT_NO_SCALE=1; \
	run -Ptb_mp3_fb.BUG_BLEND_ALWAYS_SRC=1

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

# ---- Phase F B7: SDRAM busy-cycle counter's clock-domain crossing ------------
$(RTL_BUILD_DIR)/tb_tau_cdc_gray_ctr.vvp: sim/tb_tau_cdc_gray_ctr.v src/fpga/core/tau_cdc_gray_ctr.sv | $(RTL_BUILD_DIR)
	$(IVERILOG) -g2012 -o $@ $^

test-rtl-cdc-gray-ctr: $(RTL_BUILD_DIR)/tb_tau_cdc_gray_ctr.vvp
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

PSRAM_FW_SRC = sim/tb_psram_fw.v $(RTL_BUILD_DIR)/mp3_soc_sim.v src/fpga/rtl/VexRiscv_Full.v src/fpga/core/pcm_fifo.v src/fpga/core/eq_biquad.v src/fpga/core/tau_sdram_addr_decode.sv src/fpga/core/tau_sdram_wb_adapter.sv src/fpga/core/tau_psram_probe.sv $(PSRAM_SRC)

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
PSRAM_IFETCH_SRC = sim/tb_psram_ifetch.v $(RTL_BUILD_DIR)/mp3_soc_sim.v src/fpga/rtl/VexRiscv_Full.v src/fpga/core/pcm_fifo.v src/fpga/core/eq_biquad.v src/fpga/core/tau_sdram_addr_decode.sv src/fpga/core/tau_sdram_wb_adapter.sv src/fpga/core/tau_psram_probe.sv $(PSRAM_SRC)
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
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_cpu_bridge src/fpga/core/tau_sdram_cpu_bridge.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_addr_decode src/fpga/core/tau_sdram_addr_decode.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_wb_adapter src/fpga/core/tau_sdram_wb_adapter.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_sdram_bridge_mux src/fpga/core/tau_sdram_bridge_mux.sv
	$(VERILATOR) $(VERILATOR_LINT_FLAGS) --top-module tau_cdc_gray_ctr src/fpga/core/tau_cdc_gray_ctr.sv

card-check:
	$(PYTHON) tools/library_check.py

# Decode the actual packaged RGB565/RLE loading asset, rather than previewing
# the source file.  This is a design-review aid and never alters release files.
visual-review:
	$(PYTHON) tools/visual_review.py

# QR encoder vs segno (needs work/venv-qr; about 3 min, so not part of test-host)
test-qr:
	work/venv-qr/bin/python sim/test_qr.py
