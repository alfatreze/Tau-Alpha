#!/usr/bin/env python3
"""Generate the SignalTap session file (.stp) for the proof build (docs/JTAG_DEBUG_ACCESS.md s5).

  gen_signaltap_stp.py [--out tau_proof.stp] [--depth 2048] [--ram MLAB]

One instance, sample clock clk_sys (60 MHz), the 32 registered taps of tau_signaltap_tap
(instance g_phase2_window.u_stp_tap in mp3_soc). Bit map (keep in step with mp3_soc.v):
  0 cpu_req (sdram window, stb)   1 dWE          2 wb_ack        3 wb_unsupported
  4 bridge_req  5 bridge_write    6 accept       7 done          8..27 bridge_addr[19:0]
  28 dACK       29 d_bus_err      30 rst         31 bridge_rdata[0]

Format taken from a GUI-saved reference (tools/signaltap/reference_2taps.stp, accepted by
`quartus_stp ap_core --enable`). The trigger condition is left at the GUI default (don't
care); set it in the GUI or with the quartus_stp Tcl API. The sample clock is the clk_sys net
as seen inside tau_sdram_cpu_bridge (the same 60 MHz net; there is no top-level clk_sys node).
The layout of the first-generation file (written from memory) made quartus_stp fail with an
internal error, so do not "simplify" this structure without re-running that check.
"""
import argparse
import datetime

TAP = "core_top:ic|mp3_soc:u_soc|tau_signaltap_tap:g_phase2_window.u_stp_tap|tap"
CLOCK = "core_top:ic|tau_sdram_cpu_bridge:u_sdram_cpu_bridge|clk_sys"
N = 32
QUAL_LEVEL = """          <storage_qualifier_level type="basic">
            <power_up>
            </power_up>
            <op_node/>
          </storage_qualifier_level>"""


def build(depth, ram):
    now = datetime.datetime.now().strftime("%Y/%m/%d %H:%M:%S")
    sset = f"signal_set: {now}  #0"
    trig = f"trigger: {now}  #1"
    names = [f"{TAP}[{i}]" for i in range(N)]
    wires = "\n".join(f'          <wire name="{n}" tap_mode="classic"/>' for n in names)
    nodes = "\n".join(
        f'          <node duplicate_name_allowed="false" is_data_input="true" is_node_valid="true" '
        f'is_selected="false" is_storage_input="true" is_trigger_input="true" name="{n}" '
        f'tap_mode="classic" type="register"/>' for n in reversed(names))
    nets = "\n".join(
        f'          <net data_index="{i}" duplicate_name_allowed="false" is_data_input="true" '
        f'is_node_valid="true" is_selected="false" is_storage_input="true" is_trigger_input="true" '
        f'name="{names[i]}" storage_index="{i}" tap_mode="classic" trigger_index="{i}" type="unknown"/>'
        for i in reversed(range(N)))
    ones = "1" * N
    quals = "\n".join([QUAL_LEVEL] * 3)
    return f"""<session jtag_chain="USB-Blaster [5-3]" jtag_device="@1: 5CE(BA4|FA4) (0x02B050DD)" sof_file="">
  <display_tree gui_logging_enabled="1">
    <display_branch instance="auto_signaltap_0" signal_set="{sset}" trigger="{trig}"/>
  </display_tree>
  <global_info>
    <single attribute="active instance" value="0"/>
    <single attribute="lock mode" value="0"/>
    <single attribute="jtag widget visible" value="1"/>
    <single attribute="instance widget visible" value="1"/>
    <single attribute="config widget visible" value="1"/>
    <single attribute="hierarchy widget visible" value="0"/>
    <single attribute="data log widget visible" value="0"/>
  </global_info>
  <instance enabled="true" entity_name="sld_signaltap" is_auto_node="yes" name="auto_signaltap_0" source_file="sld_signaltap.vhd">
    <node_ip_info instance_id="0" mfg_id="110" node_id="0" version="6"/>
    <position_info>
      <single attribute="active tab" value="1"/>
      <single attribute="setup vertical scroll position" value="0"/>
      <single attribute="setup horizontal scroll position" value="0"/>
    </position_info>
    <signal_set name="{sset}">
      <clock name="{CLOCK}" polarity="posedge" tap_mode="classic"/>
      <config pipeline_level="0" ram_type="{ram}" reserved_data_nodes="0" reserved_storage_qualifier_nodes="0" reserved_trigger_nodes="0" sample_depth="{depth}" trigger_in_enable="no" trigger_out_enable="no"/>
      <top_entity/>
      <signal_vec>
        <trigger_input_vec>
{wires}
        </trigger_input_vec>
        <data_input_vec>
{wires}
        </data_input_vec>
        <storage_qualifier_input_vec>
{wires}
        </storage_qualifier_input_vec>
      </signal_vec>
      <presentation>
        <unified_setup_data_view>
{nodes}
        </unified_setup_data_view>
        <data_view>
{nets}
        </data_view>
        <setup_view>
{nets}
        </setup_view>
        <trigger_in_editor/>
        <trigger_out_editor/>
      </presentation>
      <trigger attribute_mem_mode="false" gap_record="true" name="{trig}" position="center" power_up_trigger_mode="false" record_data_gap="true" segment_size="1" storage_mode="off" storage_qualifier_disabled="no" storage_qualifier_port_is_pin="true" storage_qualifier_port_name="auto_stp_external_storage_qualifier" storage_qualifier_port_tap_mode="classic" trigger_type="circular">
        <power_up_trigger position="pre" storage_qualifier_disabled="no"/>
        <events use_custom_flow_control="no">
          <level enabled="yes" name="condition1" type="basic">
            <power_up enabled="yes">
            </power_up>
            <op_node/>
          </level>
        </events>
        <storage_qualifier_events>
          <transitional>{ones}
            <pwr_up_transitional>{ones}</pwr_up_transitional>
          </transitional>
{quals}
        </storage_qualifier_events>
      </trigger>
    </signal_set>
  </instance>
  <mnemonics/>
</session>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tau_proof.stp")
    ap.add_argument("--depth", type=int, default=2048)
    ap.add_argument("--ram", default="MLAB", choices=["MLAB", "M10K", "AUTO"])
    a = ap.parse_args()
    open(a.out, "w").write(build(a.depth, a.ram))
    print(f"wrote {a.out}: {N} taps, depth {a.depth}, {a.ram}, {N * a.depth} bits")


if __name__ == "__main__":
    main()
