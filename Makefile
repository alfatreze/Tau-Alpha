.PHONY: check check-firmware check-fpga firmware firmware-advanced fpga package test test-host test-rtl card-check visual-review

PYTHON ?= python3
QUARTUS_SH ?= quartus_sh

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

fpga:
	cd src/fpga && $(QUARTUS_SH) --flow compile ap_core.qpf

package:
	$(PYTHON) package.py

test: test-host test-rtl

test-host:
	$(PYTHON) sim/test_m3u_parse.py
	$(PYTHON) tools/check_splash_asset.py
	$(PYTHON) tools/check_tau_package.py
	$(PYTHON) tools/check_ui_snapshot_renderer.py

test-rtl:
	$(PYTHON) tools/gen_eq_vectors.py
	iverilog -g2012 -o /tmp/tau-alpha-tb-fb.vvp sim/tb_mp3_fb.v src/fpga/core/mp3_fb.sv src/fpga/core/font_rom.v
	vvp /tmp/tau-alpha-tb-fb.vvp
	iverilog -g2012 -o /tmp/tau-alpha-tb-tgt.vvp sim/tb_tgt_cmd.v src/fpga/core/tgt_cmd.v
	vvp /tmp/tau-alpha-tb-tgt.vvp
	iverilog -g2005-sv -o /tmp/tau-alpha-tb-eq.vvp -I src/fpga/core sim/tb_eq_biquad.v src/fpga/core/eq_biquad.v
	vvp /tmp/tau-alpha-tb-eq.vvp
	iverilog -g2012 -o /tmp/tau-alpha-tb-pcm.vvp sim/tb_pcm_decay.v src/fpga/core/pcm_fifo.v
	vvp /tmp/tau-alpha-tb-pcm.vvp
	iverilog -g2012 -o /tmp/tau-alpha-tb-eq-cycles.vvp -I src/fpga/core sim/tb_eq_cycles.v src/fpga/core/eq_biquad.v
	vvp /tmp/tau-alpha-tb-eq-cycles.vvp

card-check:
	$(PYTHON) tools/library_check.py

# Decode the actual packaged RGB565/RLE loading asset, rather than previewing
# the source file.  This is a design-review aid and never alters release files.
visual-review:
	$(PYTHON) tools/visual_review.py
