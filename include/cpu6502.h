#pragma once
#include <iomanip>
#include <iosfwd>
#include <sstream>
#include"types.h"
#include"cpu6502state.h"

class Bus;

class CPU6502 {
public:
    void reset();

    void run_test_mode(std::ostream& out1, std::ostream& out2, int instruction_count);
    std::string format_trace_line() const;
    void log_trace_line(std::ostream& out) const;

    Byte bus_read(Word addr) const;
    void bus_write(Word addr, Byte val) const;
    Byte fetch_opCode();
    void exec_nxt_instr();
    OperandResult fetch_operand(AddrMode mode);
    OperandResult resolve_address(AddrMode mode);

    OperandResult get_accumulator_operand();
    OperandResult get_immediate_operand();
    OperandResult get_zeropage_operand();
    OperandResult get_zeropageX_operand();
    OperandResult get_zeropageY_operand();
    OperandResult get_absolute_operand();
    OperandResult get_absoluteX_operand();
    OperandResult get_absoluteY_operand();
    OperandResult get_relative_offset();
    OperandResult get_indexedIndirect_operand();
    OperandResult get_indirectIndexed_operand();

    OperandResult get_zeropage_address();
    OperandResult get_zeropageX_address();
    OperandResult get_zeropageY_address();
    OperandResult get_absolute_address();
    OperandResult get_absoluteX_address();
    OperandResult get_absoluteY_address();
    OperandResult get_indirect_address();
    OperandResult get_indexedIndirect_address();
    OperandResult get_indirectIndexed_address();

    int me_la_suda();

    Byte exec_adc(AddrMode mode);
    Byte exec_and(AddrMode mode);
    Byte exec_asl(AddrMode mode);
    Byte exec_bcc(AddrMode mode);
    Byte exec_bcs(AddrMode mode);
    Byte exec_beq(AddrMode mode);
    Byte exec_bit(AddrMode mode);
    Byte exec_bmi(AddrMode mode);
    Byte exec_bne(AddrMode mode);
    Byte exec_bpl(AddrMode mode);
    Byte exec_brk(AddrMode mode);
    Byte exec_bvc(AddrMode mode);
    Byte exec_bvs(AddrMode mode);
    Byte exec_clc(AddrMode mode);
    Byte exec_cld(AddrMode mode);
    Byte exec_cli(AddrMode mode);
    Byte exec_clv(AddrMode mode);
    Byte exec_cmp(AddrMode mode);
    Byte exec_cpx(AddrMode mode);
    Byte exec_cpy(AddrMode mode);
    Byte exec_dec(AddrMode mode);
    Byte exec_dex(AddrMode mode);
    Byte exec_dey(AddrMode mode);
    Byte exec_eor(AddrMode mode);
    Byte exec_inc(AddrMode mode);
    Byte exec_inx(AddrMode mode);
    Byte exec_iny(AddrMode mode);
    Byte exec_jmp(AddrMode mode);
    Byte exec_jsr(AddrMode mode);
    Byte exec_lda(AddrMode mode);
    Byte exec_ldx(AddrMode mode);
    Byte exec_ldy(AddrMode mode);
    Byte exec_lsr(AddrMode mode);
    Byte exec_nop(AddrMode mode);
    Byte exec_ora(AddrMode mode);
    Byte exec_pha(AddrMode mode);
    Byte exec_php(AddrMode mode);
    Byte exec_pla(AddrMode mode);
    Byte exec_plp(AddrMode mode);
    Byte exec_rol(AddrMode mode);
    Byte exec_ror(AddrMode mode);
    Byte exec_rti(AddrMode mode);
    Byte exec_rts(AddrMode mode);
    Byte exec_sbc(AddrMode mode);
    Byte exec_sec(AddrMode mode);
    Byte exec_sed(AddrMode mode);
    Byte exec_sei(AddrMode mode);
    Byte exec_sta(AddrMode mode);
    Byte exec_stx(AddrMode mode);
    Byte exec_sty(AddrMode mode);
    Byte exec_tax(AddrMode mode);
    Byte exec_tay(AddrMode mode);
    Byte exec_tsx(AddrMode mode);
    Byte exec_txa(AddrMode mode);
    Byte exec_txs(AddrMode mode);
    Byte exec_tya(AddrMode mode);

    CpuRegisters regs;
    uint64_t total_cpu_cycles{};
    Bus* bus_ref = nullptr;
};

//  type alias for a pointer to a member function of CPU6502.
using InstrHandler = Byte (CPU6502::*)(AddrMode);
inline constexpr std::array<InstrHandler, static_cast<std::size_t>(Mnemonic::COUNT)> handler_table = {
    &CPU6502::exec_adc,   // ADC
    &CPU6502::exec_and,   // AND
    &CPU6502::exec_asl,   // ASL
    &CPU6502::exec_bcc,   // BCC
    &CPU6502::exec_bcs,   // BCS
    &CPU6502::exec_beq,   // BEQ
    &CPU6502::exec_bit,   // BIT
    &CPU6502::exec_bmi,   // BMI
    &CPU6502::exec_bne,   // BNE
    &CPU6502::exec_bpl,   // BPL
    &CPU6502::exec_brk,   // BRK
    &CPU6502::exec_bvc,   // BVC
    &CPU6502::exec_bvs,   // BVS
    &CPU6502::exec_clc,   // CLC
    &CPU6502::exec_cld,   // CLD
    &CPU6502::exec_cli,   // CLI
    &CPU6502::exec_clv,   // CLV
    &CPU6502::exec_cmp,   // CMP
    &CPU6502::exec_cpx,   // CPX
    &CPU6502::exec_cpy,   // CPY
    &CPU6502::exec_dec,   // DEC
    &CPU6502::exec_dex,   // DEX
    &CPU6502::exec_dey,   // DEY
    &CPU6502::exec_eor,   // EOR
    &CPU6502::exec_inc,   // INC
    &CPU6502::exec_inx,   // INX
    &CPU6502::exec_iny,   // INY
    &CPU6502::exec_jmp,   // JMP
    &CPU6502::exec_jsr,   // JSR
    &CPU6502::exec_lda,   // LDA
    &CPU6502::exec_ldx,   // LDX
    &CPU6502::exec_ldy,   // LDY
    &CPU6502::exec_lsr,   // LSR
    &CPU6502::exec_nop,   // NOP
    &CPU6502::exec_ora,   // ORA
    &CPU6502::exec_pha,   // PHA
    &CPU6502::exec_php,   // PHP
    &CPU6502::exec_pla,   // PLA
    &CPU6502::exec_plp,   // PLP
    &CPU6502::exec_rol,   // ROL
    &CPU6502::exec_ror,   // ROR
    &CPU6502::exec_rti,   // RTI
    &CPU6502::exec_rts,   // RTS
    &CPU6502::exec_sbc,   // SBC
    &CPU6502::exec_sec,   // SEC
    &CPU6502::exec_sed,   // SED
    &CPU6502::exec_sei,   // SEI
    &CPU6502::exec_sta,   // STA
    &CPU6502::exec_stx,   // STX
    &CPU6502::exec_sty,   // STY
    &CPU6502::exec_tax,   // TAX
    &CPU6502::exec_tay,   // TAY
    &CPU6502::exec_tsx,   // TSX
    &CPU6502::exec_txa,   // TXA
    &CPU6502::exec_txs,   // TXS
    &CPU6502::exec_tya    // TYA
};