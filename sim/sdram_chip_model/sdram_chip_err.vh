// SDRAM chip model: error codes (bit index in err_mask). MIT licence.
// One code per rule family; the test bench checks err_mask[`SE_xxx].
`ifndef SDRAM_CHIP_ERR_VH
`define SDRAM_CHIP_ERR_VH
`define SE_TCK          0   // clock period below tCK(min) for the programmed CL
`define SE_CKE_LOW      1   // CKE not high at a chip clock edge (power-down, suspend, deep power-down entries)
`define SE_UNKNOWN      2   // X/Z on a command, CKE, bank or needed address pin at a chip edge
`define SE_SETUP        3   // setup time (tCMS/tAS/tDS/tCKS) violated at the chip pins
`define SE_HOLD         4   // hold time (tCMH/tAH/tDH/tCKH) violated at the chip pins
`define SE_INIT_ORDER   5   // init sequence: order, 100 us wait or NOP count
`define SE_MRS_VALUE    6   // MRS value not 0x027/0x037 (or BA wrong)
`define SE_EMRS_VALUE   7   // EMRS BA/A wrong (reserved bits not 0)
`define SE_MRS_LATE     8   // MRS/EMRS after init_done (the contract has no mode set command)
`define SE_TRCD         9   // column command before tRCD
`define SE_TRP          10  // ACT/REF/MRS before tRP after PRE/PREA
`define SE_TRAS         11  // PRE before tRAS
`define SE_TRC          12  // ACT to ACT, same bank, before tRC
`define SE_TRRD         13  // ACT to ACT, other bank, before tRRD
`define SE_TRFC         14  // any command before tRFC after REF
`define SE_TMRD         15  // any command before tMRD after MRS/EMRS
`define SE_TCCD         16  // column command spacing (tCCD)
`define SE_TDPL         17  // PRE before tDPL (clocks) after the last written word
`define SE_TWR          18  // PRE before tWR (ps) after the last written word
`define SE_TDAL         19  // ACT before tDAL (tWR + tRP) after the last written word
`define SE_ACT_REF      20  // REF before tRC after an ACT
`define SE_REF_OPEN     21  // REF (or MRS) with an open bank or open burst
`define SE_COL_CLOSED   22  // column command to a closed bank
`define SE_ACT_OPEN     23  // ACT to an open bank
`define SE_A10_COL      24  // A10 = 1 on RD/WR (auto precharge is illegal)
`define SE_COL_WRAP     25  // a wanted burst word wraps past column 1023
`define SE_BST_NOBURST  26  // BST with no burst open
`define SE_TREF         27  // a row went longer than the refresh limit without REF
`define SE_TRAS_MAX     28  // a bank open longer than tRAS(max)
`define SE_BUS_CONFLICT 29  // chip drive window overlaps the FPGA output enable
`define SE_CAPTURE      30  // no capture edge inside the valid window of a wanted word
`define SE_WDATA_X      31  // a written (unmasked) byte carries X/Z
`define SE_NUM          32
`endif
