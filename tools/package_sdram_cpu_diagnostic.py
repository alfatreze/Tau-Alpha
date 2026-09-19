#!/usr/bin/env python3
"""Build a side-by-side Pocket bundle for the Phase 2 CPU SDRAM smoke test.

This deliberately packages the ROM with the separately fitted
``TAU_PHASE2_WINDOW`` RBF.  It must never be installed as a normal TAU build:
the ROM writes destructively to 2--3 MiB through the uncached CPU aperture.
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE_CORE = ROOT / "dist/Cores/alfatreze.TAU"
SOURCE_PLATFORM_IMAGE = ROOT / "dist/Platforms/_images/tau.bin"
# A-056's isolated macro-enabled fit. This is deliberately a hard gate: the
# legacy version register is identical in macro-on and macro-off bitstreams, so
# accepting an arbitrary RBF here would let the CPU-window ROM be paired with
# the wrong hardware map without a runtime warning. A future enabled build
# needs a documented audit update before this value changes.
EXPECTED_PHASE2_RBF_SHA256 = (
    "0d01f61409f42aec6f372166f8e81f17aa1c3b0482361a03ccba3465c942217e"
)
EXPECTED_PROBE_RBF_SHA256 = (
    "921d6f941b8d40dd0f662857b6fe1b34e09a22bade0f372a950c9ed8c88df97c"
)
EXPECTED_A060_PROBE_RBF_SHA256 = (
    "9ea5e38c20145125627b8d23c2bf4ab02b7c5b3998778cea80598bcd72b4c5ee"
)
EXPECTED_A062_PROBE_RBF_SHA256 = (
    "d7f60eb7e52705a5f622d449312b266040399acf2940a352acebc0b77dcde0a4"
)
EXPECTED_A063_PROBE_RBF_SHA256 = (
    "acce05b2145b34780da31a8a315557ac64ae2416546f104c5a242f2241cbfaaa"
)
EXPECTED_A064_PROBE_RBF_SHA256 = (
    "60abb545fef5e6c725c84717af21b6a53f4f994d9219cd0245b0dbd1577c32dc"
)
EXPECTED_A065_PROBE_RBF_SHA256 = (
    "dcdc78107dbb0dc729a71f9559f55dda3e6fd7e950c46468255f4c90e0878bdb"
)
EXPECTED_A066_PROBE_RBF_SHA256 = (
    "8b4e1b75960ab36f1ae168b51a32626b3ab42ee8ff07e10d4bf194258d574293"
)
EXPECTED_A067_PROBE_RBF_SHA256 = (
    "fb2b8b1db6ec67384e244f089f47f585317c30c0573d762f3c44970b68b8f580"
)
EXPECTED_A074_PROBE_RBF_SHA256 = (
    "d4b6295d168351704dc185abf358bb230be5cc2b77460a3adbaeea48c95b6c98"
)
EXPECTED_A076_PROBE_RBF_SHA256 = (
    "9ef62ebc4002abf4f5d29c84c59c08d997c18369d55e7c134beac5b97c832ef1"
)
EXPECTED_A077_PROBE_RBF_SHA256 = (
    "53b11ee8fbfd2ff401a8a84255c88c4edd994333210933dfb1825d8b6bc6806f"
)
EXPECTED_A079_PROBE_RBF_SHA256 = (
    "246202a00fab50c6b96a38e8dd1acc4d835e5041b9d1784f325d2223421531e8"
)
EXPECTED_A080_PROBE_RBF_SHA256 = (
    "f21a9ba0fe0d4d43d49c3d2f102eda8fdc5445516581928fc87730687a14baa4"
)


def profile(probe: bool, probe_a060: bool, probe_a060_readback: bool,
            probe_a062: bool, probe_a063: bool, probe_a064: bool,
            probe_a065: bool, probe_a066: bool, probe_a067: bool,
            probe_a074: bool, probe_a076: bool, probe_a077: bool,
            probe_a079: bool, probe_a080: bool, probe_a082: bool,
            probe_a083: bool, probe_a084: bool, probe_a085: bool,
            probe_a086: bool, probe_a087: bool = False,
            probe_a088: bool = False,
            probe_a089: bool = False,
            probe_a090: bool = False,
            probe_a091: bool = False) -> dict[str, object]:
    """Return the immutable package identity/provenance for one diagnostic."""
    if probe_a062:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a062/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a062/pocket",
            "platform_id": "tau_sdram_prb62",
            "core_id": "alfatreze.TAU_SDRAM_PRB62",
            "shortname": "TAU_SDRAM_PRB62",
            "name": "TAU CPU SDRAM Probe A062",
            "description": "TAU Phase 2 CPU store-direction probe A062",
            "expected_hash": EXPECTED_A062_PROBE_RBF_SHA256,
        }
    if probe_a063:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a063/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a063/pocket",
            "platform_id": "tau_sdram_prb63",
            "core_id": "alfatreze.TAU_SDRAM_PRB63",
            "shortname": "TAU_SDRAM_PRB63",
            "name": "TAU CPU SDRAM Probe A063",
            "description": "TAU Phase 2 CPU store-payload probe A063",
            "expected_hash": EXPECTED_A063_PROBE_RBF_SHA256,
        }
    if probe_a064:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a064/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a064/pocket",
            "platform_id": "tau_sdram_prb64",
            "core_id": "alfatreze.TAU_SDRAM_PRB64",
            "shortname": "TAU_SDRAM_PRB64",
            "name": "TAU CPU SDRAM Probe A064",
            "description": "TAU Phase 2 full-width CPU store-payload probe A064",
            "expected_hash": EXPECTED_A064_PROBE_RBF_SHA256,
        }
    if probe_a065:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a065/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a065/pocket",
            "platform_id": "tau_sdram_prb65",
            "core_id": "alfatreze.TAU_SDRAM_PRB65",
            "shortname": "TAU_SDRAM_PRB65",
            "name": "TAU CPU SDRAM Probe A065",
            "description": "TAU Phase 2 all-ones CPU store probe A065",
            "expected_hash": EXPECTED_A065_PROBE_RBF_SHA256,
        }
    if probe_a066:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a066/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a066/pocket",
            "platform_id": "tau_sdram_prb66",
            "core_id": "alfatreze.TAU_SDRAM_PRB66",
            "shortname": "TAU_SDRAM_PRB66",
            "name": "TAU CPU SDRAM Probe A066",
            "description": "TAU Phase 2 controller-boundary probe A066",
            "expected_hash": EXPECTED_A066_PROBE_RBF_SHA256,
        }
    if probe_a067:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a067/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a067/pocket",
            "platform_id": "tau_sdram_prb67",
            "core_id": "alfatreze.TAU_SDRAM_PRB67",
            "shortname": "TAU_SDRAM_PRB67",
            "name": "TAU CPU SDRAM Probe A067",
            "description": "TAU Phase 2 bridge read-timing probe A067",
            "expected_hash": EXPECTED_A067_PROBE_RBF_SHA256,
        }
    if probe_a074:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a074/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a074/pocket",
            "platform_id": "tau_sdram_prb74",
            "core_id": "alfatreze.TAU_SDRAM_PRB74",
            "shortname": "TAU_SDRAM_PRB74",
            "name": "TAU CPU SDRAM Probe A074",
            "description": "TAU Phase 2 bridge-response probe A074",
            "expected_hash": EXPECTED_A074_PROBE_RBF_SHA256,
        }
    if probe_a076:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a076/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a076/pocket",
            "platform_id": "tau_sdram_prb76",
            "core_id": "alfatreze.TAU_SDRAM_PRB76",
            "shortname": "TAU_SDRAM_PRB76",
            "name": "TAU CPU SDRAM Probe A076",
            "description": "TAU Phase 2 CPU-facing return-path probe A076",
            "expected_hash": EXPECTED_A076_PROBE_RBF_SHA256,
        }
    if probe_a077:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a077/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a077/pocket",
            "platform_id": "tau_sdram_prb77",
            "core_id": "alfatreze.TAU_SDRAM_PRB77",
            "shortname": "TAU_SDRAM_PRB77",
            "name": "TAU CPU SDRAM Probe A077",
            "description": "TAU Phase 2 adapter-return probe A077",
            "expected_hash": EXPECTED_A077_PROBE_RBF_SHA256,
        }
    if probe_a079:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a079/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a079/pocket",
            "platform_id": "tau_sdram_prb79",
            "core_id": "alfatreze.TAU_SDRAM_PRB79",
            "shortname": "TAU_SDRAM_PRB79",
            "name": "TAU CPU SDRAM Probe A079",
            "description": "TAU Phase 2 owner-mux return probe A079",
            "expected_hash": EXPECTED_A079_PROBE_RBF_SHA256,
        }
    if probe_a080:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-probe-a080/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a080/pocket",
            "platform_id": "tau_sdram_prb80",
            "core_id": "alfatreze.TAU_SDRAM_PRB80",
            "shortname": "TAU_SDRAM_PRB80",
            "name": "TAU CPU SDRAM Probe A080",
            "description": "TAU Phase 2 mux probe with persistent result log A080",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a082:
        return {
            # Firmware-only diagnostic discriminator: this intentionally reuses
            # the A-080 rev-23 RBF, which contains target write/flush support.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-probe/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a082/pocket",
            "platform_id": "tau_sdram_prb82",
            "core_id": "alfatreze.TAU_SDRAM_PRB82",
            "shortname": "TAU_SDRAM_PRB82",
            "name": "TAU CPU SDRAM Probe A082",
            "description": "TAU target-write and flush status probe A082",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a083:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a083/pocket",
            "platform_id": "tau_sdram_prb83",
            "core_id": "alfatreze.TAU_SDRAM_PRB83",
            "shortname": "TAU_SDRAM_PRB83",
            "name": "TAU CPU SDRAM Probe A083",
            "description": "TAU target-slot readback probe A083",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a084:
        return {
            # Firmware-only: A-084 adds the documented 0190 -> copied 0192
            # lifecycle before target read/write; it reuses A-080's fitted RBF.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-open/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a084/pocket",
            "platform_id": "tau_sdram_prb84",
            "core_id": "alfatreze.TAU_SDRAM_PRB84",
            "shortname": "TAU_SDRAM_PRB84",
            "name": "TAU CPU SDRAM Probe A084",
            "description": "TAU result-slot open/write/read probe A084",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a085:
        return {
            # Firmware-only: wait for two short, successful 0180 responses
            # after 0192 before A-085 writes the same isolated slot.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-settle/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a085/pocket",
            "platform_id": "tau_sdram_prb85",
            "core_id": "alfatreze.TAU_SDRAM_PRB85",
            "shortname": "TAU_SDRAM_PRB85",
            "name": "TAU CPU SDRAM Probe A085",
            "description": "TAU result-slot settle/write/read probe A085",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a091:
        return {
            # Firmware-only result channel: 16 interact.json persist words,
            # APF-stored; no slot 5, no 0184/0188, no Saves file.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-interact/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a091/pocket",
            "platform_id": "tau_sdram_prb91",
            "core_id": "alfatreze.TAU_SDRAM_PRB91",
            "shortname": "TAU_SDRAM_PRB91",
            "name": "TAU CPU SDRAM Probe A091",
            "description": "TAU result via interact.json persist A091",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
            "interact_result": True,
        }
    if probe_a090:
        return {
            # Firmware-only: table-size/integrity, 10 s flush with timing and
            # a post-flush re-read. Parameters revert to the original 0x22
            # because A-089's nonvolatile bit did not help and upstream hung.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-table/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a090/pocket",
            "platform_id": "tau_sdram_prb90",
            "core_id": "alfatreze.TAU_SDRAM_PRB90",
            "shortname": "TAU_SDRAM_PRB90",
            "name": "TAU CPU SDRAM Probe A090",
            "description": "TAU result slot table and flush timing A090",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a089:
        return {
            # Packaging-only: same A-088 ROM; slot 5 gains the nonvolatile
            # bit (0x86 = core-specific | nonvolatile | deferload).
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-bridge/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a089/pocket",
            "platform_id": "tau_sdram_prb89",
            "core_id": "alfatreze.TAU_SDRAM_PRB89",
            "shortname": "TAU_SDRAM_PRB89",
            "name": "TAU CPU SDRAM Probe A089",
            "description": "TAU result slot nonvolatile parameters A089",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
            "slot_parameters": "0x86",
        }
    if probe_a088:
        return {
            # Firmware-only: corrects the datatable bridge base to 0xF8002000.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-bridge/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a088/pocket",
            "platform_id": "tau_sdram_prb88",
            "core_id": "alfatreze.TAU_SDRAM_PRB88",
            "shortname": "TAU_SDRAM_PRB88",
            "name": "TAU CPU SDRAM Probe A088",
            "description": "TAU result bridge-address fix A088",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a087:
        return {
            # Firmware-only: shows datatable words 200..203 sampled just
            # before 0184 so payload absence and APF mapping faults differ.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-source/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a087/pocket",
            "platform_id": "tau_sdram_prb87",
            "core_id": "alfatreze.TAU_SDRAM_PRB87",
            "shortname": "TAU_SDRAM_PRB87",
            "name": "TAU CPU SDRAM Probe A087",
            "description": "TAU result source-buffer discriminator A087",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a086:
        return {
            # Firmware-only: immediate readback now occurs before 0188 flush,
            # preventing a timed-out flush from obscuring the write boundary.
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a080/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-log-write-read/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a086/pocket",
            "platform_id": "tau_sdram_prb86",
            "core_id": "alfatreze.TAU_SDRAM_PRB86",
            "shortname": "TAU_SDRAM_PRB86",
            "name": "TAU CPU SDRAM Probe A086",
            "description": "TAU result write-before-flush readback A086",
            "expected_hash": EXPECTED_A080_PROBE_RBF_SHA256,
        }
    if probe_a060_readback:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a060/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu-readback/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-readback/pocket",
            "platform_id": "tau_sdram_rd60",
            "core_id": "alfatreze.TAU_SDRAM_RD60",
            "shortname": "TAU_SDRAM_RD60",
            "name": "TAU CPU SDRAM Readback A060",
            "description": "TAU Phase 2 mailbox-to-CPU readback diagnostic A060",
            "expected_hash": EXPECTED_A060_PROBE_RBF_SHA256,
        }
    if probe_a060:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe-a060/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe-a060/pocket",
            "platform_id": "tau_sdram_prb60",
            "core_id": "alfatreze.TAU_SDRAM_PRB60",
            "shortname": "TAU_SDRAM_PRB60",
            "name": "TAU CPU SDRAM Probe A060",
            "description": "TAU Phase 2 CPU SDRAM back-to-back probe A060",
            "expected_hash": EXPECTED_A060_PROBE_RBF_SHA256,
        }
    if probe:
        return {
            "raw_rbf": ROOT / "work/diagnostics/sdram-cpu-probe/fpga/ap_core.rbf",
            "rom": ROOT / "work/diagnostics/sdram-cpu/tau.rom",
            "output": ROOT / "work/diagnostics/sdram-cpu-probe/pocket",
            "platform_id": "tau_sdram_probe",
            "core_id": "alfatreze.TAU_SDRAM_PROBE",
            "shortname": "TAU_SDRAM_PROBE",
            "name": "TAU CPU SDRAM Probe",
            "description": "TAU Phase 2 CPU SDRAM hardware-path probe",
            "expected_hash": EXPECTED_PROBE_RBF_SHA256,
        }
    return {
        "raw_rbf": ROOT / "work/diagnostics/sdram-cpu/fpga/ap_core.rbf",
        "rom": ROOT / "work/diagnostics/sdram-cpu/tau.rom",
        "output": ROOT / "work/diagnostics/sdram-cpu/pocket",
        "platform_id": "tau_sdram_cpu",
        "core_id": "alfatreze.TAU_SDRAM_CPU",
        "shortname": "TAU_SDRAM_CPU",
        "name": "TAU CPU SDRAM Diagnostic",
        "description": "TAU Phase 2 uncached CPU SDRAM diagnostic",
        "expected_hash": EXPECTED_PHASE2_RBF_SHA256,
    }


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def save_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=4) + "\n", encoding="utf-8")


def bit_reverse(source: Path, destination: Path) -> None:
    table = bytes(int(f"{value:08b}"[::-1], 2) for value in range(256))
    destination.write_bytes(bytes(source.read_bytes()).translate(table))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--probe", action="store_true",
                      help="package the A-059 hardware-path probe beside the CPU diagnostic")
    mode.add_argument("--probe-a060", action="store_true",
                      help="package the corrected A-060 probe beside prior diagnostics")
    mode.add_argument("--probe-a060-readback", action="store_true",
                      help="package the A-060 mailbox-to-CPU readback discriminator")
    mode.add_argument("--probe-a062", action="store_true",
                      help="package the A-062 two-request direction probe")
    mode.add_argument("--probe-a063", action="store_true",
                      help="package the A-063 CPU-store payload trace probe")
    mode.add_argument("--probe-a064", action="store_true",
                      help="package the A-064 full-width store-payload probe")
    mode.add_argument("--probe-a065", action="store_true",
                      help="package the A-065 all-ones store-payload probe")
    mode.add_argument("--probe-a066", action="store_true",
                      help="package the A-066 controller-boundary probe")
    mode.add_argument("--probe-a067", action="store_true",
                      help="package the A-067 bridge read-timing probe")
    mode.add_argument("--probe-a074", action="store_true",
                      help="package the A-074 bridge-response probe")
    mode.add_argument("--probe-a076", action="store_true",
                      help="package the A-076 CPU-facing return-path probe")
    mode.add_argument("--probe-a077", action="store_true",
                      help="package the A-077 adapter-return probe")
    mode.add_argument("--probe-a079", action="store_true",
                      help="package the A-079 owner-mux return probe")
    mode.add_argument("--probe-a080", action="store_true",
                      help="package the A-080 persistent-result-log probe")
    mode.add_argument("--probe-a082", action="store_true",
                      help="package the A-082 target-write/flush status probe")
    mode.add_argument("--probe-a083", action="store_true",
                      help="package the A-083 target-slot readback probe")
    mode.add_argument("--probe-a084", action="store_true",
                      help="package the A-084 result-slot lifecycle probe")
    mode.add_argument("--probe-a085", action="store_true",
                      help="package the A-085 result-slot settle probe")
    mode.add_argument("--probe-a091", action="store_true",
                      help="package the A-091 interact.json result probe")
    mode.add_argument("--probe-a090", action="store_true",
                      help="package the A-090 table/flush-timing probe")
    mode.add_argument("--probe-a089", action="store_true",
                      help="package the A-089 nonvolatile-slot probe")
    mode.add_argument("--probe-a088", action="store_true",
                      help="package the A-088 bridge-address fix probe")
    mode.add_argument("--probe-a087", action="store_true",
                      help="package the A-087 source-buffer probe")
    mode.add_argument("--probe-a086", action="store_true",
                      help="package the A-086 write-before-flush probe")
    args = parser.parse_args()
    cfg = profile(args.probe, args.probe_a060, args.probe_a060_readback,
                  args.probe_a062, args.probe_a063, args.probe_a064,
                  args.probe_a065, args.probe_a066, args.probe_a067,
                  args.probe_a074, args.probe_a076, args.probe_a077,
                  args.probe_a079, args.probe_a080, args.probe_a082,
                  args.probe_a083, args.probe_a084, args.probe_a085,
                  args.probe_a086, args.probe_a087, args.probe_a088, args.probe_a089, args.probe_a090, args.probe_a091)
    raw_rbf = cfg["raw_rbf"]
    diag_rom = cfg["rom"]
    output = cfg["output"]
    platform_id = cfg["platform_id"]
    core_id = cfg["core_id"]
    for required in (SOURCE_CORE, SOURCE_PLATFORM_IMAGE, raw_rbf, diag_rom):
        if not required.exists():
            raise SystemExit(f"missing CPU-window diagnostic input: {required}")
    rbf_digest = digest(raw_rbf)
    if rbf_digest != cfg["expected_hash"]:
        raise SystemExit(
            "refusing to package an unaudited Phase 2 RBF: "
            f"expected {cfg['expected_hash']}, got {rbf_digest}"
        )

    temp = output.with_name(output.name + ".tmp")
    if temp.exists():
        shutil.rmtree(temp)
    temp.mkdir(parents=True)

    core_dir = temp / "Cores" / core_id
    shutil.copytree(SOURCE_CORE, core_dir)
    bit_reverse(raw_rbf, core_dir / "bitstream.rbf_r")

    core = load_json(core_dir / "core.json")
    metadata = core["core"]["metadata"]
    metadata["platform_ids"] = [platform_id]
    if len(platform_id) > 15 or not re.fullmatch(r"[a-z0-9][a-z0-9_]*", platform_id):
        raise ValueError(f"invalid Analogue Pocket platform shortname: {platform_id!r}")
    metadata["shortname"] = cfg["shortname"]
    metadata["description"] = cfg["description"]
    save_json(core_dir / "core.json", core)
    expected_core_id = f'{metadata["author"]}.{metadata["shortname"]}'
    if core_dir.name != expected_core_id:
        raise RuntimeError(
            f"core folder {core_dir.name!r} does not match metadata identity "
            f"{expected_core_id!r}"
        )

    data = load_json(core_dir / "data.json")
    # Slot 5 is intentionally the only writable diagnostic slot. It must
    # never be substituted with music, artwork, or playlist data.
    interact_result = bool(cfg.get("interact_result"))
    data["data"]["data_slots"] = [data["data"]["data_slots"][0]] if interact_result else [
        data["data"]["data_slots"][0],
        {
            "name": "Diag result log",
            "id": 5,
            "required": False,
            "nonvolatile": True,
            "deferload": True,
            "parameters": cfg.get("slot_parameters", "0x22"),
            "filename": "last-result.tlog",
            "extensions": ["tlog"],
            "size_exact": 64,
            "size_maximum": 64,
        },
    ]
    save_json(core_dir / "data.json", data)

    input_config = load_json(core_dir / "input.json")
    input_config["input"]["controllers"][0]["mappings"] = [{
        "id": 0, "name": "Run CPU-window diagnostic again", "key": "pad_btn_a"
    }]
    save_json(core_dir / "input.json", input_config)

    interact = load_json(core_dir / "interact.json")
    # A-091: one persist variable per interact word (APF stores signed int32,
    # so the range stays within 0..2^31-1; firmware publishes 31-bit values).
    interact["interact"]["variables"] = [] if not interact_result else [
        {
            "name": f"(diag) result word {i}",
            "id": 30 + i,
            "type": "slider_u32",
            "enabled": True,
            "persist": True,
            "address": f"0x{0x20000000 + 4 * i:08X}",
            "defaultval": 0,
            "graphical": {"signed": False, "min": 0, "max": 2147483647,
                          "adjust_small": 1, "adjust_large": 1},
        }
        for i in range(16)
    ]
    interact["interact"]["messages"] = []
    save_json(core_dir / "interact.json", interact)

    assets_common = temp / "Assets" / platform_id / "common"
    assets_instance = temp / "Assets" / platform_id / core_id
    assets_common.mkdir(parents=True)
    assets_instance.mkdir(parents=True)
    shutil.copy2(diag_rom, assets_common / "tau.rom")
    save_json(assets_instance / f"{cfg['name']}.json", {
        "instance": {
            "magic": "APF_VER_1",
            "variant_select": {"id": 0, "select": False},
            "data_path": "",
            "data_slots": [
                {"id": 1, "filename": "tau.rom"},
            ] + ([] if interact_result else [{"id": 5, "filename": "last-result.tlog"}]),
            "memory_writes": [],
        }
    })

    # Pre-create the fixed-size file rather than rely on undocumented creation
    # timing for a defer-loaded slot. Firmware writes and flushes this file.
    save_file = temp / "Saves" / platform_id / core_id / "last-result.tlog"
    if not interact_result:
        save_file.parent.mkdir(parents=True)
        save_file.write_bytes(b"\0" * 64)

    platform_dir = temp / "Platforms"
    (platform_dir / "_images").mkdir(parents=True)
    shutil.copy2(SOURCE_PLATFORM_IMAGE, platform_dir / "_images" / f"{platform_id}.bin")
    save_json(platform_dir / f"{platform_id}.json", {
        "platform": {
            "category": "Media Players",
            "name": cfg["name"],
            "year": 2026,
            "manufacturer": "alfatreze",
        }
    })

    hashes = {
        "phase2_ap_core.rbf": rbf_digest,
        "packaged_bitstream.rbf_r": digest(core_dir / "bitstream.rbf_r"),
        "diagnostic_tau.rom": digest(assets_common / "tau.rom"),
    }
    if not interact_result:
        hashes["diagnostic_last-result.tlog"] = digest(save_file)
    (temp / "SHA256SUMS.txt").write_text(
        "".join(f"{value}  {name}\n" for name, value in hashes.items()),
        encoding="utf-8",
    )
    (temp / "INSTALL.txt").write_text(
        f"{cfg['name']} - DEVELOPER BUILD\n\n"
        "Copy the Cores, Assets, and Platforms folders to the Pocket SD root.\n"
        "This installs beside the normal TAU core under Media Players.\n"
        "It REQUIRES the packaged Phase 2 RBF; do not combine this ROM with a normal TAU RBF.\n"
        "The test destructively writes only physical SDRAM 2-3 MiB via 0xA0200000.\n"
        f"Launch {cfg['name']} and photograph PASS or the complete FAIL screen.\n"
        + ("After the run QUIT the core to the menu; APF then writes the result words to "
           "Settings/<core>/Interact/_core/interact_persist.json.\n" if interact_result else
           "The matching 64-byte result log is flushed to Saves/<platform>/<core>/last-result.tlog.\n") +
        "Press A to repeat the diagnostic.\n",
        encoding="utf-8",
    )

    if output.exists():
        shutil.rmtree(output)
    temp.rename(output)
    print(f"wrote side-by-side Pocket CPU-window diagnostic bundle: {output}")
    for name, value in hashes.items():
        print(f"  {value}  {name}")


if __name__ == "__main__":
    main()
