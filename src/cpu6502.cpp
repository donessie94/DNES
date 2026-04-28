#include "../include/cpu6502.h"
#include "../include/bus.h"
#include "cpu6502.h"

void CPU6502::reset()
{
    // force a clean known CPU state before execution begins
    regs.a = 0;
    regs.x = 0;
    regs.y = 0;
    total_cpu_cycles = 0;

    // The 6502 stack pointer starts at 0xFD after reset
    regs.sp = 0xFD;

    // standard reset state.
    regs.p.carry = false;
    regs.p.zero = false;
    regs.p.interrupt_disable = true;
    regs.p.decimal = false;
    regs.p.brk = false;
    regs.p.unused = true;
    regs.p.overflow = false;
    regs.p.negative = false;

    // The reset vector lives at 0xFFFC-0xFFFD
    // The CPU reads those two bytes and uses them as the first program counter value after reset
    // Low byte is stored first, then high byte, because the 6502 is little endian
    Word lo = Word(bus_read(0xFFFC));
    Word hi = Word(bus_read(0xFFFD));
    regs.pc = (hi << 8) | lo;
}

Byte CPU6502::bus_read(Word addr) const { return bus_ref->cpu_read(addr); }

void CPU6502::bus_write(Word addr, Byte val) const { return bus_ref->cpu_write(addr, val); }

Byte CPU6502::fetch_opCode()
{
    Byte op = bus_read(regs.pc); // fetch opCode
    regs.pc++; // increment PC counter to point to next instruction (next instr addrs)
    return op;
}

int CPU6502::exec_nxt_instr()
{
    Byte op = fetch_opCode();
    Instr instr_metadata = instr_table[op];
    if(!instr_metadata.implemented) return -1;

    auto idx = static_cast<std::size_t>(instr_metadata.mnemonic); // convert enum class to int
    InstrHandler handler = handler_table[idx];
    Byte extra_cycles = (this->*handler)(instr_metadata.mode); // calling the correct member fucntion
    total_cpu_cycles += instr_metadata.cycles;
    total_cpu_cycles += extra_cycles;
    return instr_metadata.cycles + extra_cycles; // how many cycles this instr took
}

OperandResult CPU6502::fetch_operand(AddrMode mode)
{
    // Implied handled outside
    switch (mode)
    {
    case AddrMode::Accumulator:         return get_accumulator_operand();     // needs result back into regs.a (destination)
    case AddrMode::Immediate:           return get_immediate_operand();
    case AddrMode::ZeroPage:            return get_zeropage_operand();
    case AddrMode::ZeroPageX:           return get_zeropageX_operand();
    case AddrMode::ZeroPageY:           return get_zeropageY_operand();
    case AddrMode::Absolute:            return get_absolute_operand();
    case AddrMode::AbsoluteX:           return get_absoluteX_operand();
    case AddrMode::AbsoluteY:           return get_absoluteY_operand();
    case AddrMode::IndexedIndirect:     return get_indexedIndirect_operand();   // (zp,X)
    case AddrMode::IndirectIndexed:     return get_indirectIndexed_operand();   // (zp),Y
    case AddrMode::Relative:            return get_relative_offset();           // signed so it need works
    default:                            return {};
    }
}

OperandResult CPU6502::resolve_address(AddrMode mode)
{
    switch (mode)
    {
    case AddrMode::ZeroPage:            return get_zeropage_address();
    case AddrMode::ZeroPageX:           return get_zeropageX_address();
    case AddrMode::ZeroPageY:           return get_zeropageY_address();
    case AddrMode::Absolute:            return get_absolute_address();
    case AddrMode::AbsoluteX:           return get_absoluteX_address();
    case AddrMode::AbsoluteY:           return get_absoluteY_address();
    case AddrMode::Indirect:            return get_indirect_address();
    case AddrMode::IndexedIndirect:     return get_indexedIndirect_address();
    case AddrMode::IndirectIndexed:     return get_indirectIndexed_address();
    default:                            return {};
    }
}

void CPU6502::handle_nmi_interrupt()
{
    // push return address high byte
    // push return address low byte
    // push processor status register (P)
    // read vector from $FFFA-$FFFB (cartridge)
    // set PC to that address
    // clear pending NMI
    // set the interrupt-disable flag as appropriate for interrupt entry

    Byte hi = Byte(regs.pc >> 8);
    bus_write(0x0100 | regs.sp, hi);
    regs.sp--;
    Byte lo = Byte(regs.pc & 0b0000000011111111);
    bus_write(0x0100 | regs.sp, lo);
    regs.sp--;

    StatusFlags pushed_flags = regs.p;
    pushed_flags.brk = false;
    pushed_flags.unused = true;
    bus_write(0x0100 | regs.sp, pushed_flags.to_byte());
    regs.sp--;

    Word addr_hi = bus_read(0xFFFB);
    Word addr_lo = bus_read(0xFFFA); // cuz little endian
    Word addr    = (addr_hi << 8) | addr_lo;
    regs.pc = addr;
    regs.p.interrupt_disable = true;
}

OperandResult CPU6502::get_accumulator_operand()
{
    // 1 byte instruction since no operand fetching needed (operand is content of A reg)
    return {.value = regs.a};
}

OperandResult CPU6502::get_immediate_operand()
{
    // 2 byte instruction, first byte was opCode (fetch_opCode) second one the literal val.
    return {.value = bus_read(regs.pc++)}; // we advance the PC to next memory adrss
}

OperandResult CPU6502::get_zeropage_operand()
{
    OperandResult res = get_zeropage_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_zeropageX_operand()
{
    OperandResult res = get_zeropageX_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_zeropageY_operand()
{
    OperandResult res = get_zeropageY_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_absolute_operand()
{
    OperandResult res = get_absolute_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_absoluteX_operand()
{
    OperandResult res = get_absoluteX_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_absoluteY_operand()
{
    OperandResult res = get_absoluteY_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_relative_offset()
{
    // 2-byte instruction. 1: fetch opCode, 2: relative_offset fetch.
    // if branch taken (Flags must be checked) add relative_offset to PC later
    Byte relative_offset = bus_read(regs.pc++);
    return {.value = relative_offset}; // cast to int8_t when actually used
}

OperandResult CPU6502::get_indexedIndirect_operand()
{
    OperandResult res = get_indexedIndirect_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_indirectIndexed_operand()
{
    OperandResult res = get_indirectIndexed_address();
    res.value = bus_read(res.address);
    return res;
}

OperandResult CPU6502::get_zeropage_address()
{
    // 2-byte instruction. 1: fetch opCode, 2: effective address in page zero of memory.
    // 0x_ _ | _ _ (16-bit addr, high byte is page, low byte is offset of that page)
    //   page|offset(BAL base adress low)
    // Note: here page (high byte) is zero (duh)
    Word zero = 0x00; // high byte is implicitly page 00
    Byte bal  = bus_read(regs.pc++); // fetch 8-bit base address
    Word addr = zero | Word(bal); // final address is therefore 0x00BAL
    return {.address = addr};
}

OperandResult CPU6502::get_zeropageX_address()
{
    Word zero = 0x00;
    Byte bal  = bus_read(regs.pc++);
    bal += regs.x; // addition wraps in 8 bits, so address stays in page 00
    Word addr = zero | Word(bal);
    return {.address = addr};
}

OperandResult CPU6502::get_zeropageY_address()
{
    Word zero = 0x00;
    Byte bal  = bus_read(regs.pc++);
    bal += regs.y;
    Word addr = zero | Word(bal);
    return {.address = addr};
}

OperandResult CPU6502::get_absolute_address()
{
    Word bal = Word(bus_read(regs.pc++));       // fetch BAL (low byte of address)
    Word bah = Word(bus_read(regs.pc++));       // fetch BAH (high byte of address)
    return {.address = Word((bah << 8) | bal)}; // combine into final 16-bit address
}

OperandResult CPU6502::get_absoluteX_address()
{
    // page crossing is allowed/intended here
    Word bal  = Word(bus_read(regs.pc++));   // read 16-bit absolute base address
    Word bah  = Word(bus_read(regs.pc++));
    Word base = (bah << 8) | bal;
    Word addr = base + regs.x;
    // if both high byte are not equal ( of base and addr) then we crossed the page
    bool page_crossed = ((base & 0xFF00) != (addr & 0xFF00));
    return {.address = addr, .page_crossed = page_crossed};
}

OperandResult CPU6502::get_absoluteY_address()
{
    Word bal  = Word(bus_read(regs.pc++));
    Word bah  = Word(bus_read(regs.pc++));
    Word base = (bah << 8) | bal;
    Word addr = base + regs.y;
    bool page_crossed = ((base & 0xFF00) != (addr & 0xFF00));
    return {.address = addr, .page_crossed = page_crossed};
}

// IT MUST EMULATE WELL KNOWN NES HARDWARE BUG !!!
OperandResult CPU6502::get_indirect_address()
{
    // 3-byte instruction. 1: fetch opCode, 2: fetch BAL, 3: fetch BAH
    Word bal = Word(bus_read(regs.pc++));
    Word bah = Word(bus_read(regs.pc++));
    Word ptr = (bah << 8) | bal;

    Word adl = Word(bus_read(ptr));

    // Correct behavior would be:
    // Word adh = Word(bus_read(ptr + 1)); // next memory cell basically

    // 6502 hardware bug:
    // if ptr ends in 0xFF, the high byte fetch wraps within the same page
    // instead of going to the next page.
    // Example: JMP ($03FF) reads from $03FF and $0300, not $0400.
    Word adh = Word(bus_read((ptr & 0xFF00) | Word(Byte(ptr + 1))));

    return {.address = Word((adh << 8) | adl)};
}

OperandResult CPU6502::get_indexedIndirect_address()
{
    // 2-byte instruction. 1: fetch opCode, 2: fetch BAL (base adress low byte (zero paged)),
    // Then:
    // - add X with zero page wrap
    // - read low/high bytes of final address from zero page
    Word zero   = 0x00;
    Byte bal    = bus_read(regs.pc++);
    Byte zp_ptr = bal + regs.x; // 8-bit wraparound is intendeed behavior
    zp_ptr      = zero | Word(zp_ptr); // for the sake of explicity
    Word adl    = Word(bus_read(Word(zp_ptr)));
    Word adh    = Word(bus_read(Word(Byte(zp_ptr + 1)))); // must wrap in zero page too

    return {.address = Word((adh << 8) | adl)};
}

OperandResult CPU6502::get_indirectIndexed_address()
{
    // 2-byte instruction. 1: fetch opCode, 2: fetch IAL (indirect adress low byte (zero paged))
    Word zero   = 0x00;
    Byte ial    = bus_read(regs.pc++);
    Byte zp_ptr = ial;
    zp_ptr      = zero | Word(zp_ptr); // for the sake of explicity
    Word bal    = Word(bus_read(Word(zp_ptr)));
    Word bah    = Word(bus_read(Word(Byte(zp_ptr + 1)))); // must wrap in zero page too (8-bit)
    Word base   = (bah << 8) | bal;
    Word addr   = base + regs.y; // normal 16-bit wraparound is the intended behavior here
    bool page_crossed = ((base & 0xFF00) != (addr & 0xFF00));

    return {.address = addr, .page_crossed = page_crossed};
}

Byte CPU6502::exec_adc(AddrMode mode)
{
    //   A:   1100 0000   NOTE:
    //   M:   0111 0000   Carry: T, Negative: F (bit 7 is 0 so result is not negative)
    //   C:   0           Overflow: F because signed overflow only happens when:
    //   ---------------                - A and M have the same sign (they dont here)
    // Sum: 1 0011 0000                 - but the result has a different sign
    //   R:   0011 0000

    OperandResult fetch_data = fetch_operand(mode);
    Word sum = Word(regs.a) + Word(fetch_data.value) + (regs.p.carry ? 1 : 0);

    Byte result = Byte(sum & 0x00FF);

    regs.p.carry = (sum > 0x00FF);
    regs.p.zero = (result == 0);               //        0x   8   0
    regs.p.negative = ((result & 0x80) != 0);  // 0x80 = 0b 1000 0000 (bit 7 mask (0 indexed))

    // Overflow: if A and M had same sign, but result has different sign
    regs.p.overflow = (((regs.a ^ result) & (fetch_data.value ^ result) & 0x80) != 0);

    regs.a = result;    // result saved in A register (architecture intended)
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_and(AddrMode mode)
{
    // A = A & memory
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = regs.a & fetch_data.value;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.a = result;
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_asl(AddrMode mode)
{
    // value = value << 1
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = Byte(fetch_data.value << 1);
    regs.p.carry = ((fetch_data.value & 0x80) != 0);
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    if(mode == AddrMode::Accumulator) { regs.a = result; }
    else { bus_write(fetch_data.address, result); }
    return 0; // no extra cycles here
}

Byte CPU6502::exec_bcc(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    // Note the PC+2 is done in fetch_opCode() and then get_relative_offset() functions
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.carry == 0); // branch if carry clear
    // only if branch is taken page crossed matters cuz the CPU will have to go to that other page
    // in the next instruction (since now PC points to a different page location)
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_bcs(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.carry == 1); // branch if carry set
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_beq(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.zero == 1); // branch if zero flag set
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_bit(AddrMode mode)
{
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = Byte(regs.a & fetch_data.value);
    regs.p.zero = (result == 0);
    regs.p.negative = (fetch_data.value & 0x80); // set -> T, clear -> F
    regs.p.overflow = (fetch_data.value & 0x40); // 0x40 = 0100 0000 (6th bit mask)
    return 0; // no extra cycles here
}

Byte CPU6502::exec_bmi(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.negative == 1); // branch if negative flag set
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_bne(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.zero == 0); // branch if zero flag clear
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_bpl(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.negative == 0); // branch if negative flag clear
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_brk(AddrMode)
{
    // BRK triggers a software IRQ. It pushes the return PC and processor flags
    // to the stack, sets the interrupt disable flag, and jumps through the IRQ/BRK vector.
    //
    // push PC + 2 high byte to stack
    // push PC + 2 low byte to stack
    // push NV11DIZC flags to stack
    // set interrupt_disable flag after the old flags are pushed to the stack
    // AND:
    // PC = ($FFFE) -> in 6502 notation parentheses mean dereference so this reads as:
    //                  "PC = contents of memory at vector starting at 0xFFFE"

    // BRK skips the following byte, so pushed return address is PC+2 from opcode fetch start
    regs.pc++;

    Byte pc_hi = Byte((regs.pc & 0xFF00) >> 8);
    Byte pc_lo = Byte(regs.pc & 0x00FF);

    // NOTE: Stack designated space in CPU6502 is from 0x0100 to 0x01FF (page 1)
    // thus we OR with 0x0100 (high byte) so we can target the correct address in memory for the stack
    bus_write(0x0100 | regs.sp, pc_hi);
    regs.sp--; // the stack grows from higher to lower memory

    bus_write(0x0100 | regs.sp, pc_lo);
    regs.sp--;

    StatusFlags pushed_flags = regs.p;
    pushed_flags.brk = true;
    pushed_flags.unused = true;
    bus_write(0x0100 | regs.sp, pushed_flags.to_byte());
    regs.sp--;

    regs.p.interrupt_disable = true;

    // IRQ/BRK vector gives the handler address we jump to next
    Word lo = Word(bus_read(0xFFFE));
    Word hi = Word(bus_read(0xFFFF)); // 0xFFFE + 1 = 0xFFFF (next location)
    regs.pc = (hi << 8) | lo;

    return 0; // no extra cycles here
}

Byte CPU6502::exec_bvc(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.overflow == 0); // branch if overflow flag clear
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_bvs(AddrMode mode)
{
    // PC = PC + 2 + memory (signed)
    OperandResult fetch_data = fetch_operand(mode);
    Word new_addr = regs.pc + int8_t(fetch_data.value);
    fetch_data.branch_taken = (regs.p.overflow == 1); // branch if overflow flag set
    fetch_data.page_crossed = fetch_data.branch_taken && ((regs.pc & 0xFF00) != (new_addr & 0xFF00));
    regs.pc = fetch_data.branch_taken ? new_addr : regs.pc;
    Byte extra_cycles_taken = fetch_data.branch_taken + fetch_data.page_crossed;
    return extra_cycles_taken;
}

Byte CPU6502::exec_clc(AddrMode)
{
    regs.p.carry = false;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_cld(AddrMode)
{
    regs.p.decimal = false;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_cli(AddrMode)
{
    regs.p.interrupt_disable = false; // effect on IRQ recognition is delayed by 1 instruction
    return 0; // no extra cycles here
}

Byte CPU6502::exec_clv(AddrMode)
{
    regs.p.overflow = false;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_cmp(AddrMode mode)
{
    // A - memory (the comparison its implemented as substraction in hardware)
    OperandResult fetch_data = fetch_operand(mode);
    regs.p.carry = (regs.a >= fetch_data.value) ? true : false;
    regs.p.zero  = (regs.a == fetch_data.value) ? true : false;
    Byte result  = regs.a - fetch_data.value;
    regs.p.negative = ((result & 0x80) != 0);
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_cpx(AddrMode mode)
{
    // X - memory (the comparison its implemented as substraction in hardware)
    OperandResult fetch_data = fetch_operand(mode);
    regs.p.carry = (regs.x >= fetch_data.value) ? true : false;
    regs.p.zero  = (regs.x == fetch_data.value) ? true : false;
    Byte result  = regs.x - fetch_data.value;
    regs.p.negative = ((result & 0x80) != 0);
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_cpy(AddrMode mode)
{
    // Y - memory (the comparison its implemented as substraction in hardware)
    OperandResult fetch_data = fetch_operand(mode);
    regs.p.carry = (regs.y >= fetch_data.value) ? true : false;
    regs.p.zero  = (regs.y == fetch_data.value) ? true : false;
    Byte result  = regs.y - fetch_data.value;
    regs.p.negative = ((result & 0x80) != 0);
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_dec(AddrMode mode)
{
    // mem_val = mem_val - 1
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = fetch_data.value - 1;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    bus_write(fetch_data.address, result);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_dex(AddrMode)
{
    // X = X - 1 (Implied)
    Byte result = regs.x - 1;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.x = result;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_dey(AddrMode)
{
    // Y = Y - 1 (Implied)
    Byte result = regs.y - 1;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.y = result;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_eor(AddrMode mode)
{
    // A = A ^ memory
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = (regs.a ^ fetch_data.value); // XOR
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.a = result;
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_inc(AddrMode mode)
{
    // mem_val = mem_val + 1
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = fetch_data.value + 1;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    bus_write(fetch_data.address, result);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_inx(AddrMode)
{
    // X = X + 1 (Implied)
    Byte result = regs.x + 1;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.x = result;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_iny(AddrMode)
{
    // X = X + 1 (Implied)
    Byte result = regs.y + 1;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.y = result;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_jmp(AddrMode mode)
{
    // PC = resolved target address
    OperandResult fetch_data = resolve_address(mode);
    regs.pc = fetch_data.address;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_jsr(AddrMode mode)
{
    // resolve_address() already advanced PC to the next instruction
    // JSR pushes (PC - 1) to the stack
    // PC = memory
    OperandResult fetch_data = resolve_address(mode);
    Byte pc_hi = Byte(((regs.pc-1) & 0xFF00) >> 8);
    Byte pc_lo = Byte((regs.pc-1) & 0x00FF);

    // NOTE: Stack designated space in CPU6502 is from 0x0100 to 0x01FF (page 1)
    // thus we OR with 0x0100 (high byte) so we can target the correct address in memory for the stack
    bus_write(0x0100 | regs.sp, pc_hi);
    regs.sp--; // the stack grows from higher to lower memory
    bus_write(0x0100 | regs.sp, pc_lo);
    regs.sp--;
    regs.pc = fetch_data.address;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_lda(AddrMode mode)
{
    // A = memory
    OperandResult fetch_data = fetch_operand(mode);
    regs.a = fetch_data.value;
    regs.p.zero = (fetch_data.value == 0);
    regs.p.negative = ((fetch_data.value & 0x80) != 0);
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_ldx(AddrMode mode)
{
    // X = memory
    OperandResult fetch_data = fetch_operand(mode);
    regs.x = fetch_data.value;
    regs.p.zero = (fetch_data.value == 0);
    regs.p.negative = ((fetch_data.value & 0x80) != 0);
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_ldy(AddrMode mode)
{
    // Y = memory
    OperandResult fetch_data = fetch_operand(mode);
    regs.y = fetch_data.value;
    regs.p.zero = (fetch_data.value == 0);
    regs.p.negative = ((fetch_data.value & 0x80) != 0);
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_lsr(AddrMode mode)
{
    // value = value >> 1
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = Byte(fetch_data.value >> 1);
    regs.p.carry = ((fetch_data.value & 0x01) != 0);
    regs.p.zero = (result == 0);
    regs.p.negative = false; // alwasy false cuz we shift right so highest bit is 0 (signed positive)
    if(mode == AddrMode::Accumulator) { regs.a = result; }
    else { bus_write(fetch_data.address, result); }
    return 0; // no extra cycles here
}

Byte CPU6502::exec_nop(AddrMode) { return 0; }

Byte CPU6502::exec_ora(AddrMode mode)
{
    // A = A | memory
    OperandResult fetch_data = fetch_operand(mode);
    Byte result = (regs.a | fetch_data.value); // OR
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.a = result;
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_pha(AddrMode)
{
    // ($0100 + SP) = A
    // SP = SP - 1
    bus_write((0x0100 | regs.sp), regs.a);
    regs.sp--;
    return 0; // no extra cycles here


}

Byte CPU6502::exec_php(AddrMode)
{
    // ($0100 + SP) = NV11DIZC (push flags to the stack pointer position)
    // SP = SP - 1
    StatusFlags pushed_flags = regs.p;
    pushed_flags.brk = true;
    pushed_flags.unused = true;
    bus_write(0x0100 | regs.sp, pushed_flags.to_byte());
    regs.sp--;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_pla(AddrMode)
{
    // SP = SP + 1
    // A = ($0100 + SP)
    regs.sp++;
    Byte val = bus_read(0x0100 | regs.sp);
    regs.a = val;
    regs.p.zero = (regs.a == 0);
    regs.p.negative = ((regs.a & 0x80) != 0);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_plp(AddrMode)
{
    // SP = SP + 1
    // NVxxDIZC = ($0100 + SP)
    // “delayed 1 instruction” IRQ recognition nuance (for later)
    regs.sp++;
    Byte val = bus_read(0x0100 | regs.sp);
    regs.p.set_from_byte(val);

    // normalization, these flags are not real "CPU status" that need restoring
    // thus the convention is to normalize them to these values
    regs.p.unused = true;
	regs.p.brk = false;

    return 0; // no extra cycles here
}

Byte CPU6502::exec_rol(AddrMode mode)
{
    // value = value << 1 through C, or visually: C <- [76543210] <- C
    OperandResult fetch_data = fetch_operand(mode);

    Byte first = regs.p.carry ? 1 : 0;                 // old carry goes into bit 0
    bool last = ((fetch_data.value & 0x80) != 0);      // old bit 7 goes into carry

    Byte result = Byte((fetch_data.value << 1) | first);

    regs.p.carry = last;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);

    if (mode == AddrMode::Accumulator) { regs.a = result; }
    else { bus_write(fetch_data.address, result); }

    return 0; // no extra cycles here
}

Byte CPU6502::exec_ror(AddrMode mode)
{
    // value = value >> 1 through C, or visually: C -> [76543210] -> C
    OperandResult fetch_data = fetch_operand(mode);

    Byte first = regs.p.carry ? 0x80 : 0x00;           // old carry goes into bit 7
    bool last = ((fetch_data.value & 0x01) != 0);      // old bit 0 goes into carry

    Byte result = Byte((fetch_data.value >> 1) | first);

    regs.p.carry = last;
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);

    if (mode == AddrMode::Accumulator) { regs.a = result; }
    else { bus_write(fetch_data.address, result); }

    return 0; // no extra cycles here
}

Byte CPU6502::exec_rti(AddrMode)
{
    // pull NVxxDIZC flags from stack
    // pull PC low byte from stack
    // pull PC high byte from stack
    regs.sp++;
    Byte flags = bus_read(0x0100 | regs.sp);
    regs.p.set_from_byte(flags);

    // normalization, these flags are not real "CPU status" that need restoring
    // thus the convention is to normalize them to these values
    regs.p.unused = true;
	regs.p.brk = false;

    regs.sp++;
    Word pc_lo = Word(bus_read(0x0100 | regs.sp));

    regs.sp++;
    Word pc_hi = Word(bus_read(0x0100 | regs.sp));

    Word addr = (pc_hi << 8) | pc_lo;
    regs.pc = addr;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_rts(AddrMode)
{
    // pull PC low byte from stack
    // pull PC high byte from stack
    // PC = pulled_address + 1
    regs.sp++;
    Word pc_lo = Word(bus_read(0x0100 | regs.sp));

    regs.sp++;
    Word pc_hi = Word(bus_read(0x0100 | regs.sp));

    Word addr = (pc_hi << 8) | pc_lo;
    regs.pc = addr + 1;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_sbc(AddrMode mode)
{
    // A = A + ~memory + C
    OperandResult fetch_data = fetch_operand(mode);

    Word diff = Word(regs.a) + Word(Byte(~fetch_data.value)) + (regs.p.carry ? 1 : 0);
    Byte result = Byte(diff & 0x00FF);

    regs.p.carry = (diff > 0x00FF); // no borrow
    regs.p.zero = (result == 0);
    regs.p.negative = ((result & 0x80) != 0);
    regs.p.overflow = (((regs.a ^ result) & (regs.a ^ fetch_data.value) & 0x80) != 0);

    regs.a = result;
    Byte extra_cycles_taken = fetch_data.page_crossed ? 1 : 0;
    return extra_cycles_taken;
}

Byte CPU6502::exec_sec(AddrMode)
{
    regs.p.carry = true;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_sed(AddrMode)
{
    regs.p.decimal = true;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_sei(AddrMode)
{
    regs.p.interrupt_disable = true; // effect on IRQ recognition is delayed by 1 instruction
    return 0; // no extra cycles here
}

Byte CPU6502::exec_sta(AddrMode mode)
{
    // memory = A
    OperandResult fetch_data = resolve_address(mode);
    bus_write(fetch_data.address, regs.a);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_stx(AddrMode mode)
{
    // memory = X
    OperandResult fetch_data = resolve_address(mode);
    bus_write(fetch_data.address, regs.x);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_sty(AddrMode mode)
{
    // memory = Y
    OperandResult fetch_data = resolve_address(mode);
    bus_write(fetch_data.address, regs.y);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_tax(AddrMode)
{
    // X = A
    regs.x = regs.a;
    regs.p.zero = (regs.x == 0);
    regs.p.negative = ((regs.x & 0x80) != 0);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_tay(AddrMode)
{
    // Y = A
    regs.y = regs.a;
    regs.p.zero = (regs.y == 0);
    regs.p.negative = ((regs.y & 0x80) != 0);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_tsx(AddrMode)
{
    // X = SP
    regs.x = regs.sp;
    regs.p.zero = (regs.x == 0);
    regs.p.negative = ((regs.x & 0x80) != 0);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_txa(AddrMode)
{
    // A = X
    regs.a = regs.x;
    regs.p.zero = (regs.a == 0);
    regs.p.negative = ((regs.a & 0x80) != 0);
    return 0; // no extra cycles here
}

Byte CPU6502::exec_txs(AddrMode)
{
    // SP = X
    regs.sp = regs.x;
    return 0; // no extra cycles here
}

Byte CPU6502::exec_tya(AddrMode)
{
    // A = Y
    regs.a = regs.y;
    regs.p.zero = (regs.a == 0);
    regs.p.negative = ((regs.a & 0x80) != 0);
    return 0; // no extra cycles here
}