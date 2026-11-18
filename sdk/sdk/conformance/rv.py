"""A small RV32IM decoder and the STATIC part of the "only host-table calls" check.

The package carries the pack image only (no symbols, no ELF), so the check works from the entry point: a recursive descent over direct control flow (branches, jal), with constants followed inside a straight line
(lui/auipc/addi pairs). It finds, in everything reachable that way: system calls, CSR and fence.i instructions, compressed or illegal instructions, jumps outside the image, and the absolute addresses the meter forms.
What it cannot follow is a jump through a register (calls through the host table are of that kind): those sites are counted and left to the dynamic run (tau_meter/runner.py), which faults on the first call that lands anywhere
but the host table or the meter's own code. SDK builds use -fno-jump-tables so a switch is a chain of branches and the descent sees all of it."""
import struct

WIN_CODE, WIN_DATA, WIN_SIZE = 0x24970000, 0xA4970000, 0x10000
SCR_ORG, SCR_SIZE = 0x26C00, 0x1000
REGS = ["zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"]


def sx(v, bits): return (v & ((1 << bits) - 1)) - ((v & (1 << (bits - 1))) << 1)


def decode(ins):
    """-> dict(kind, text, ...) for one 32-bit instruction word"""
    if ins & 3 != 3: return {"kind": "compressed", "text": ".half 0x%04x (compressed)" % (ins & 0xFFFF)}
    op, rd, f3, rs1, rs2, f7 = ins & 0x7F, (ins >> 7) & 31, (ins >> 12) & 7, (ins >> 15) & 31, (ins >> 20) & 31, ins >> 25
    R = REGS; immi = sx(ins >> 20, 12)
    if op == 0x37: return {"kind": "lui", "rd": rd, "imm": ins & 0xFFFFF000, "text": "lui %s, 0x%x" % (R[rd], ins >> 12)}
    if op == 0x17: return {"kind": "auipc", "rd": rd, "imm": ins & 0xFFFFF000, "text": "auipc %s, 0x%x" % (R[rd], ins >> 12)}
    if op == 0x6F:
        o = sx(((ins >> 31) << 20) | (((ins >> 12) & 0xFF) << 12) | (((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3FF) << 1), 21)
        return {"kind": "jal", "rd": rd, "off": o, "text": "jal %s, %+d" % (R[rd], o)}
    if op == 0x67: return {"kind": "jalr", "rd": rd, "rs1": rs1, "imm": immi, "text": "jalr %s, %d(%s)" % (R[rd], immi, R[rs1])}
    if op == 0x63:
        o = sx(((ins >> 31) << 12) | (((ins >> 7) & 1) << 11) | (((ins >> 25) & 0x3F) << 5) | (((ins >> 8) & 0xF) << 1), 13)
        if f3 in (2, 3): return {"kind": "illegal", "text": ".word 0x%08x" % ins}
        return {"kind": "branch", "off": o, "text": "%s %s, %s, %+d" % (["beq", "bne", "?", "?", "blt", "bge", "bltu", "bgeu"][f3], R[rs1], R[rs2], o)}
    if op == 0x03:
        if f3 in (3, 6, 7): return {"kind": "illegal", "text": ".word 0x%08x" % ins}
        return {"kind": "load", "rd": rd, "rs1": rs1, "imm": immi, "size": (1, 2, 4, 0, 1, 2)[f3], "text": "%s %s, %d(%s)" % (["lb", "lh", "lw", "?", "lbu", "lhu"][f3], R[rd], immi, R[rs1])}
    if op == 0x23:
        if f3 > 2: return {"kind": "illegal", "text": ".word 0x%08x" % ins}
        o = sx(((ins >> 25) << 5) | ((ins >> 7) & 31), 12)
        return {"kind": "store", "rs1": rs1, "rs2": rs2, "imm": o, "size": (1, 2, 4)[f3], "text": "%s %s, %d(%s)" % (["sb", "sh", "sw"][f3], R[rs2], o, R[rs1])}
    if op == 0x13:
        names = ["addi", "slli", "slti", "sltiu", "xori", "srli", "ori", "andi"]
        nm = names[f3] if f3 != 5 else ("srai" if f7 & 0x20 else "srli")
        return {"kind": "alui", "name": nm, "rd": rd, "rs1": rs1, "imm": immi, "text": "%s %s, %s, %d" % (nm, R[rd], R[rs1], immi)}
    if op == 0x33:
        if f7 == 1: nm = ["mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu"][f3]
        else: nm = ["add", "sll", "slt", "sltu", "xor", "srl", "or", "and"][f3] if not (f7 & 0x20) else {0: "sub", 5: "sra"}.get(f3, "?")
        return {"kind": "alu", "name": nm, "rd": rd, "rs1": rs1, "rs2": rs2, "text": "%s %s, %s, %s" % (nm, R[rd], R[rs1], R[rs2])}
    if op == 0x0F: return {"kind": "fence_i" if f3 == 1 else "fence", "text": "fence.i" if f3 == 1 else "fence"}
    if op == 0x73: return {"kind": "system", "text": ("csr%s" % f3) if f3 else ("ebreak" if (ins >> 20) == 1 else "ecall" if (ins >> 20) == 0 else "system 0x%08x" % ins)}
    return {"kind": "illegal", "text": ".word 0x%08x" % ins}


def disasm_at(image, off, ctx=2):
    """a few instructions around image offset `off`, as text lines"""
    out = []
    for o in range(max(0, off - 4 * ctx) & ~3, min(len(image) - 3, off + 4 * (ctx + 1)), 4):
        ins = struct.unpack_from("<I", image, o)[0]
        out.append("%s +0x%04x  %s" % (">>" if o == off else "  ", o, decode(ins)["text"]))
    return out


def allowed_data(addr, scratch_lo, scratch_hi, size):
    """is `addr` somewhere the meter may touch: its window's data alias, its own scratch, the stack/inputs (reached through registers, never as constants)"""
    if WIN_DATA <= addr < WIN_DATA + WIN_SIZE: return True
    if scratch_lo <= addr < scratch_hi: return True
    return False


def scan(image, entry, scratch_lo, scratch_hi):
    """-> {"findings": [(offset, text)], "reached": n instructions, "indirect": n sites, "calls_direct": n}"""
    n = len(image); seen = set(); work = [(entry, {})]; findings = []; indirect = 0
    def add(o, msg):
        if (o, msg) not in findings: findings.append((o, msg))
    while work:
        pc, regs = work.pop()
        regs = dict(regs)
        while True:
            if pc in seen: break
            if pc < 0 or pc + 4 > n or pc & 3:
                add(pc, "control flow leaves the meter's own code (to window offset 0x%x)" % pc); break
            seen.add(pc)
            ins = struct.unpack_from("<I", image, pc)[0]; d = decode(ins); k = d["kind"]
            if k in ("compressed", "illegal"): add(pc, "an instruction an RV32IM core does not have (%s)" % d["text"]); break
            if k == "system": add(pc, "%s: a meter may not make system calls or touch processor state" % d["text"]); break
            if k == "fence_i": add(pc, "fence.i: instruction-cache control belongs to the player"); break
            if k == "lui": regs[d["rd"]] = d["imm"]
            elif k == "auipc": regs[d["rd"]] = (WIN_CODE + pc + d["imm"]) & 0xFFFFFFFF
            elif k == "alui" and d["name"] == "addi" and d["rs1"] in regs: regs[d["rd"]] = (regs[d["rs1"]] + d["imm"]) & 0xFFFFFFFF
            elif k in ("alui", "alu", "load") and d.get("rd"): regs.pop(d["rd"], None)
            if k == "alui" and d["name"] == "addi" and d["rs1"] == 0: regs[d["rd"]] = d["imm"] & 0xFFFFFFFF
            if k in ("load", "store") and d["rs1"] in regs:
                a = (regs[d["rs1"]] + d["imm"]) & 0xFFFFFFFF
                if 0x80000000 <= a < 0x80001000: add(pc, "touches the hardware register 0x%08x: a meter may use only the host table" % a)
                elif not allowed_data(a, scratch_lo, scratch_hi, d["size"]) and not (WIN_CODE <= a < WIN_CODE + WIN_SIZE and k == "load"):
                    add(pc, "uses the absolute address 0x%08x, which is not the meter's own memory" % a)
                elif WIN_CODE <= a < WIN_CODE + WIN_SIZE: add(pc, "reads data through the code alias 0x%08x (use the data alias)" % a)
            if k == "branch":
                work.append((pc + d["off"], {})); pc += 4; continue
            if k == "jal":
                t = pc + d["off"]
                if d["rd"] == 0: pc = t; regs = {}; continue
                work.append((t, {})); regs = {}; pc += 4; continue
            if k == "jalr":
                indirect += 1
                if d["rd"] == 0: break                       # a return or a tail call through a register: the descent ends here
                regs = {}; pc += 4; continue
            if d.get("rd") == 0 and k in ("alui", "alu"): pass
            pc += 4
    return {"findings": sorted(findings), "reached": len(seen), "indirect": indirect}
