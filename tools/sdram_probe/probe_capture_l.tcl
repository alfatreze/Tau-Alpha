# Read-only STA on a COPY of a finished probe fit (MIT): the falling-edge capture register (dataout_l) against the unused
# rising-edge register (dataout_h) of the DDIO input. Run: quartus_sta -t probe_capture_l.tcl sdram_probe
load_package sta
project_open [lindex $quartus(args) 0]
create_timing_netlist
read_sdc
update_timing_netlist
proc slack_to {kind pat} {
    set s "n/a"
    set regs [get_registers -nowarn $pat]
    if {[get_collection_size $regs] == 0} { return "no-registers-match" }
    foreach_in_collection p [get_timing_paths -$kind -from [get_ports {dram_dq[*]}] -to $regs -npaths 1] { set s [get_path_info $p -slack] }
    return $s
}
foreach cond [get_available_operating_conditions] {
    set_operating_conditions $cond
    update_timing_netlist
    foreach kind {setup hold} {
        puts "CAP cond=$cond $kind dataout_l=[slack_to $kind {*g_fall.u_ddio*dataout_l*}] dataout_h=[slack_to $kind {*g_fall.u_ddio*dataout_h*}]"
    }
}
delete_timing_netlist
project_close
