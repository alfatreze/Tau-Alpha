# Timing report of the SDRAM probe on the FITTED project (run by sdram_probe.py after the compile; MIT licence).
#   quartus_sta -t probe_report.tcl sdram_probe
# Per operating condition (slow 85C, slow 0C, fast 85C, fast 0C of the Cyclone V) it prints the worst setup and hold
# slack of
#   CORE  clk -> clk                    (the PHY and the scheduler at 100 MHz)
#   OUT   clk -> sdram_clk_pin          (every output pin register against the chip: command, address, DQM, CKE, data, OE)
#   IN    sdram_clk_pin -> clk          (the read data against the capture register, FALL mode)
# plus the worst output slack per pin group and the estimated Fmax of the core paths (1000 / (10 - setup slack)).
# Lines start with RESULT / GROUP / FMAX so that sdram_probe.py can pick them up. NOT run on a real fit yet.
load_package sta
project_open [lindex $quartus(args) 0]
create_timing_netlist
read_sdc
update_timing_netlist

set clk100 ""
foreach_in_collection c [all_clocks] {
    set n [get_clock_info -name $c]
    if {[string match {*general?3?*} $n]} { set clk100 $n }
}
if {$clk100 eq ""} {
    puts "RESULT ERROR no 100 MHz PLL clock (general\[3\]) found among the clocks"
    project_close
    return
}
puts "RESULT clk100 = $clk100"

proc slack_of {kind args} {
    set s "n/a"
    set paths [eval get_timing_paths -$kind $args -npaths 1]
    foreach_in_collection p $paths { set s [get_path_info $p -slack] }
    return $s
}

foreach cond [get_available_operating_conditions] {
    set_operating_conditions $cond
    update_timing_netlist
    foreach kind {setup hold} {
        set core [slack_of $kind -from_clock [list $clk100] -to_clock [list $clk100]]
        set out  [slack_of $kind -from_clock [list $clk100] -to_clock [list sdram_clk_pin]]
        set in   [slack_of $kind -from_clock [list sdram_clk_pin] -to_clock [list $clk100]]
        puts "RESULT cond=$cond $kind CORE=$core OUT=$out IN=$in"
        if {$kind eq "setup" && $core ne "n/a"} {
            puts "FMAX cond=$cond core_paths_estimate_MHz=[format %.1f [expr {1000.0 / (10.0 - $core)}]]"
        }
        foreach {name pat} {addr {dram_a[*]} ba {dram_ba[*]} dqm {dram_dqm[*]} ras dram_ras_n cas dram_cas_n we dram_we_n cke dram_cke dq {dram_dq[*]}} {
            puts "GROUP cond=$cond $kind $name slack=[slack_of $kind -to [get_ports $pat]]"
        }
        puts "GROUP cond=$cond $kind dq_read_capture slack=[slack_of $kind -from [get_ports {dram_dq[*]}]]"
    }
}
set_operating_conditions [lindex [get_available_operating_conditions] 0]
update_timing_netlist
report_timing -setup -from_clock [list sdram_clk_pin] -to_clock [list $clk100] -npaths 1 -detail full_path -file probe_in_setup.txt
report_timing -hold  -from_clock [list sdram_clk_pin] -to_clock [list $clk100] -npaths 1 -detail full_path -file probe_in_hold.txt
report_timing -setup -from_clock [list $clk100] -to_clock [list sdram_clk_pin] -npaths 1 -detail full_path -file probe_out_setup.txt
report_timing -setup -from_clock [list $clk100] -to_clock [list $clk100] -npaths 5 -detail full_path -file probe_core_setup.txt
delete_timing_netlist
project_close
