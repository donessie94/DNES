#include "../include/cpu6502.h"
#include "../include/bus.h"

namespace
{
    std::string hex8(Byte value)
    {
        std::ostringstream oss;
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(value);
        return oss.str();
    }

    std::string hex16(Word value)
    {
        std::ostringstream oss;
        oss << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(value);
        return oss.str();
    }

    std::string mnemonic_to_string(Mnemonic mnemonic)
    {
        switch (mnemonic)
        {
        case Mnemonic::ADC: return "ADC";
        case Mnemonic::AND: return "AND";
        case Mnemonic::ASL: return "ASL";
        case Mnemonic::BCC: return "BCC";
        case Mnemonic::BCS: return "BCS";
        case Mnemonic::BEQ: return "BEQ";
        case Mnemonic::BIT: return "BIT";
        case Mnemonic::BMI: return "BMI";
        case Mnemonic::BNE: return "BNE";
        case Mnemonic::BPL: return "BPL";
        case Mnemonic::BRK: return "BRK";
        case Mnemonic::BVC: return "BVC";
        case Mnemonic::BVS: return "BVS";
        case Mnemonic::CLC: return "CLC";
        case Mnemonic::CLD: return "CLD";
        case Mnemonic::CLI: return "CLI";
        case Mnemonic::CLV: return "CLV";
        case Mnemonic::CMP: return "CMP";
        case Mnemonic::CPX: return "CPX";
        case Mnemonic::CPY: return "CPY";
        case Mnemonic::DEC: return "DEC";
        case Mnemonic::DEX: return "DEX";
        case Mnemonic::DEY: return "DEY";
        case Mnemonic::EOR: return "EOR";
        case Mnemonic::INC: return "INC";
        case Mnemonic::INX: return "INX";
        case Mnemonic::INY: return "INY";
        case Mnemonic::JMP: return "JMP";
        case Mnemonic::JSR: return "JSR";
        case Mnemonic::LDA: return "LDA";
        case Mnemonic::LDX: return "LDX";
        case Mnemonic::LDY: return "LDY";
        case Mnemonic::LSR: return "LSR";
        case Mnemonic::NOP: return "NOP";
        case Mnemonic::ORA: return "ORA";
        case Mnemonic::PHA: return "PHA";
        case Mnemonic::PHP: return "PHP";
        case Mnemonic::PLA: return "PLA";
        case Mnemonic::PLP: return "PLP";
        case Mnemonic::ROL: return "ROL";
        case Mnemonic::ROR: return "ROR";
        case Mnemonic::RTI: return "RTI";
        case Mnemonic::RTS: return "RTS";
        case Mnemonic::SBC: return "SBC";
        case Mnemonic::SEC: return "SEC";
        case Mnemonic::SED: return "SED";
        case Mnemonic::SEI: return "SEI";
        case Mnemonic::STA: return "STA";
        case Mnemonic::STX: return "STX";
        case Mnemonic::STY: return "STY";
        case Mnemonic::TAX: return "TAX";
        case Mnemonic::TAY: return "TAY";
        case Mnemonic::TSX: return "TSX";
        case Mnemonic::TXA: return "TXA";
        case Mnemonic::TXS: return "TXS";
        case Mnemonic::TYA: return "TYA";
        default:            return "???";
        }
    }

    std::string format_operand_text(AddrMode mode, Word pc, Byte b1, Byte b2)
    {
        switch (mode)
        {
        case AddrMode::Implied:
            return "";

        case AddrMode::Accumulator:
            return "A";

        case AddrMode::Immediate:
            return "#$" + hex8(b1);

        case AddrMode::ZeroPage:
            return "$" + hex8(b1);

        case AddrMode::ZeroPageX:
            return "$" + hex8(b1) + ",X";

        case AddrMode::ZeroPageY:
            return "$" + hex8(b1) + ",Y";

        case AddrMode::Absolute:
            return "$" + hex16((Word(b2) << 8) | b1);

        case AddrMode::AbsoluteX:
            return "$" + hex16((Word(b2) << 8) | b1) + ",X";

        case AddrMode::AbsoluteY:
            return "$" + hex16((Word(b2) << 8) | b1) + ",Y";

        case AddrMode::Indirect:
            return "($" + hex16((Word(b2) << 8) | b1) + ")";

        case AddrMode::IndexedIndirect:
            return "($" + hex8(b1) + ",X)";

        case AddrMode::IndirectIndexed:
            return "($" + hex8(b1) + "),Y";

        case AddrMode::Relative:
        {
            int8_t rel = static_cast<int8_t>(b1);
            Word target = static_cast<Word>(pc + 2 + rel);
            return "$" + hex16(target);
        }

        default:
            return "";
        }
    }

    std::string format_bytes_column(Byte opcode, Byte b1, Byte b2, Byte instr_bytes)
    {
        std::ostringstream oss;
        oss << hex8(opcode);

        if (instr_bytes >= 2) {
            oss << " " << hex8(b1);
        } else {
            oss << "   ";
        }

        if (instr_bytes >= 3) {
            oss << " " << hex8(b2);
        } else {
            oss << "   ";
        }

        return oss.str();
    }
}

std::string CPU6502::format_trace_line() const
{
    const Word pc = regs.pc;

    const Byte opcode = bus_read(pc);
    const Byte b1 = bus_read(pc + 1);
    const Byte b2 = bus_read(pc + 2);

    const Instr instr = instr_table[opcode];

    const std::string mnemonic = mnemonic_to_string(instr.mnemonic);
    const std::string operand_text = format_operand_text(instr.mode, pc, b1, b2);
    const std::string bytes_col = format_bytes_column(opcode, b1, b2, instr.bytes);

    std::ostringstream oss;

    // Example shape:
    // C000  A2 00     LDX #$00                        A:00 X:00 Y:00 P:24 SP:FD CYC:7
    oss << hex16(pc) << "  ";
    oss << std::left << std::setw(8) << bytes_col;
    oss << "  ";
    oss << mnemonic;

    if (!operand_text.empty()) {
        oss << " " << operand_text;
    }

    oss << std::left << std::setw(28 - static_cast<int>(mnemonic.size() + (operand_text.empty() ? 0 : 1 + operand_text.size()))) << "";

    oss << "A:"  << hex8(regs.a) << " ";
    oss << "X:"  << hex8(regs.x) << " ";
    oss << "Y:"  << hex8(regs.y) << " ";
    oss << "P:"  << hex8(regs.p.to_byte()) << " ";
    oss << "SP:" << hex8(regs.sp) << " ";
    oss << "CYC:" << total_cpu_cycles;

    return oss.str();
}

void CPU6502::log_trace_line(std::ostream& out) const
{
    out << format_trace_line() << '\n';
}

void CPU6502::run_test_mode(std::ostream& out1, std::ostream& out2, int instruction_count)
{
    for (int i = 0; i < instruction_count; i++)
    {
        log_trace_line(out1);
        log_trace_line(out2);
        exec_nxt_instr();
    }
}