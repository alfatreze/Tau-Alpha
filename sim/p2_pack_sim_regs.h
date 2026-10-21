/* Included (-include) when a pack is built for tools/rv32sim.py: the hardware registers are reached through the simulator's device window (tools/p2_dev_model.py) instead of 0x8000_00xx. */
#define P2_HOST 1
#define REG_RD(a) (*(volatile uint32_t *)(uintptr_t)(0xF0001000u + ((a) & 0xFFFu)))
#define REG_WR(a, v) (*(volatile uint32_t *)(uintptr_t)(0xF0001000u + ((a) & 0xFFFu)) = (v))
