#include "LR35902.h"
#include <iostream>
#include <stdexcept>

// Helper declarations
uint8_t cpu_read(CPU_LR35902 &cpu, uint16_t address);
void cpu_write(CPU_LR35902 &cpu, uint16_t address, uint8_t value);

namespace
{
    uint8_t &reg8_ref(CPU_LR35902 &cpu, uint8_t reg)
    {
        switch (reg)
        {
        case 0:
            return cpu.bc.reg8.high; // B
        case 1:
            return cpu.bc.reg8.low; // C
        case 2:
            return cpu.de.reg8.high; // D
        case 3:
            return cpu.de.reg8.low; // E
        case 4:
            return cpu.hl.reg8.high; // H
        case 5:
            return cpu.hl.reg8.low; // L
        case 6:
            return cpu.memory[cpu.hl.reg16];
        case 7:
            return cpu.af.reg8.high; // A
        default:
            throw std::runtime_error("Invalid 8-bit register index");
        }
    }

    uint8_t read_reg8(CPU_LR35902 &cpu, uint8_t reg)
    {
        if (reg == 6)
            return cpu_read(cpu, cpu.hl.reg16);
        return reg8_ref(cpu, reg);
    }

    void write_reg8(CPU_LR35902 &cpu, uint8_t reg, uint8_t value)
    {
        if (reg == 6)
        {
            cpu_write(cpu, cpu.hl.reg16, value);
            return;
        }
        reg8_ref(cpu, reg) = value;
    }

    uint16_t read_reg16(CPU_LR35902 &cpu, uint8_t reg_pair)
    {
        switch (reg_pair)
        {
        case 0:
            return cpu.bc.reg16;
        case 1:
            return cpu.de.reg16;
        case 2:
            return cpu.hl.reg16;
        case 3:
            return cpu.sp;
        default:
            throw std::runtime_error("Invalid 16-bit register index");
        }
    }

    void write_reg16(CPU_LR35902 &cpu, uint8_t reg_pair, uint16_t value)
    {
        switch (reg_pair)
        {
        case 0:
            cpu.bc.reg16 = value;
            break;
        case 1:
            cpu.de.reg16 = value;
            break;
        case 2:
            cpu.hl.reg16 = value;
            break;
        case 3:
            cpu.sp = value;
            break;
        default:
            throw std::runtime_error("Invalid 16-bit register index");
        }
    }

    void set_z_flag(CPU_LR35902 &cpu, bool value)
    {
        set_flag(cpu, FLAG_Z, value);
    }

    void set_n_flag(CPU_LR35902 &cpu, bool value)
    {
        set_flag(cpu, FLAG_N, value);
    }

    void set_h_flag(CPU_LR35902 &cpu, bool value)
    {
        set_flag(cpu, FLAG_H, value);
    }

    void set_c_flag(CPU_LR35902 &cpu, bool value)
    {
        set_flag(cpu, FLAG_C, value);
    }

    uint8_t make_8bit_add_result(uint8_t a, uint8_t b, bool carry_in = false)
    {
        return static_cast<uint8_t>(a + b + static_cast<uint8_t>(carry_in));
    }

    uint8_t make_8bit_sub_result(uint8_t a, uint8_t b, bool carry_in = false)
    {
        return static_cast<uint8_t>(a - b - static_cast<uint8_t>(carry_in));
    }

    void apply_add_flags(CPU_LR35902 &cpu, uint8_t a, uint8_t b, bool carry_in, uint8_t result)
    {
        uint16_t total = static_cast<uint16_t>(a) + static_cast<uint16_t>(b) + (carry_in ? 1u : 0u);
        cpu.af.reg8.low = 0;
        set_z_flag(cpu, (result == 0));
        set_n_flag(cpu, false);
        set_h_flag(cpu, (((a & 0x0F) + (b & 0x0F) + (carry_in ? 1u : 0u)) > 0x0F));
        set_c_flag(cpu, (total > 0xFF));
    }

    void apply_adc_flags(CPU_LR35902 &cpu, uint8_t a, uint8_t b)
    {
        bool carry = get_flag(cpu, FLAG_C);
        uint16_t total = static_cast<uint16_t>(a) + static_cast<uint16_t>(b) + (carry ? 1u : 0u);
        cpu.af.reg8.low = 0;
        set_z_flag(cpu, ((total & 0xFF) == 0));
        set_n_flag(cpu, false);
        set_h_flag(cpu, (((a & 0x0F) + (b & 0x0F) + (carry ? 1u : 0u)) > 0x0F));
        set_c_flag(cpu, (total > 0xFF));
        cpu.af.reg8.high = static_cast<uint8_t>(total & 0xFF);
    }

    void apply_sub_flags(CPU_LR35902 &cpu, uint8_t a, uint8_t b, bool carry_in, uint8_t result)
    {
        cpu.af.reg8.low = 0;
        set_z_flag(cpu, (result == 0));
        set_n_flag(cpu, true);
        set_h_flag(cpu, (((a & 0x0F) - (b & 0x0F) - (carry_in ? 1u : 0u)) & 0x10) == 0x10);
        set_c_flag(cpu, ((static_cast<int>(a) - static_cast<int>(b) - (carry_in ? 1 : 0)) < 0));
    }

    void apply_logic_flags(CPU_LR35902 &cpu, uint8_t result, bool is_zero, bool set_n = false)
    {
        cpu.af.reg8.low = 0;
        set_z_flag(cpu, is_zero);
        set_n_flag(cpu, set_n);
        set_h_flag(cpu, false);
        set_c_flag(cpu, false);
        cpu.af.reg8.high = result;
    }

    void apply_inc_flags(CPU_LR35902 &cpu, uint8_t old_value, uint8_t new_value, bool update_accumulator = false)
    {
        cpu.af.reg8.low = 0;
        set_z_flag(cpu, (new_value == 0));
        set_n_flag(cpu, false);
        set_h_flag(cpu, (((old_value & 0x0F) + 1) > 0x0F));
        if (update_accumulator)
        {
            cpu.af.reg8.high = new_value;
        }
    }

    void apply_dec_flags(CPU_LR35902 &cpu, uint8_t old_value, uint8_t new_value, bool update_accumulator = false)
    {
        cpu.af.reg8.low = 0;
        set_z_flag(cpu, (new_value == 0));
        set_n_flag(cpu, true);
        set_h_flag(cpu, ((old_value & 0x0F) < 1));
        if (update_accumulator)
        {
            cpu.af.reg8.high = new_value;
        }
    }

    void increment_register(CPU_LR35902 &cpu, uint8_t reg)
    {
        uint8_t old_value = read_reg8(cpu, reg);
        uint8_t new_value = static_cast<uint8_t>(old_value + 1);
        write_reg8(cpu, reg, new_value);
        apply_inc_flags(cpu, old_value, new_value, reg == 7);
    }

    void decrement_register(CPU_LR35902 &cpu, uint8_t reg)
    {
        uint8_t old_value = read_reg8(cpu, reg);
        uint8_t new_value = static_cast<uint8_t>(old_value - 1);
        write_reg8(cpu, reg, new_value);
        apply_dec_flags(cpu, old_value, new_value, reg == 7);
    }

    void handle_cb_instruction(CPU_LR35902 &cpu, uint8_t cb_opcode)
    {
        const uint8_t reg_index = cb_opcode & 0x07;
        const uint8_t bit_index = (cb_opcode >> 3) & 0x07;
        const uint8_t op_group = (cb_opcode >> 3) & 0x18;

        auto read_cb_operand = [&](bool &is_memory) -> uint8_t
        {
            if (reg_index == 6)
            {
                is_memory = true;
                return cpu_read(cpu, cpu.hl.reg16);
            }
            is_memory = false;
            return read_reg8(cpu, reg_index);
        };

        auto write_cb_operand = [&](uint8_t value, bool is_memory)
        {
            if (is_memory)
            {
                cpu_write(cpu, cpu.hl.reg16, value);
            }
            else
            {
                write_reg8(cpu, reg_index, value);
            }
        };

        switch (cb_opcode & 0xF8)
        {
        case 0x00:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            bool carry = (value & 0x80) != 0;
            uint8_t result = static_cast<uint8_t>((value << 1) | (carry ? 1u : 0u));
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, carry);
            break;
        }
        case 0x08:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            bool carry = (value & 0x01) != 0;
            uint8_t result = static_cast<uint8_t>((value >> 1) | (carry ? 0x80u : 0u));
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, carry);
            break;
        }
        case 0x10:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            bool carry = (value & 0x80) != 0;
            uint8_t result = static_cast<uint8_t>((value << 1) | (get_flag(cpu, FLAG_C) ? 1u : 0u));
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, carry);
            break;
        }
        case 0x18:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            bool carry = (value & 0x01) != 0;
            uint8_t result = static_cast<uint8_t>((value >> 1) | (get_flag(cpu, FLAG_C) ? 0x80u : 0u));
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, carry);
            break;
        }
        case 0x20:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            bool carry = (value & 0x80) != 0;
            uint8_t result = static_cast<uint8_t>(value << 1);
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, carry);
            break;
        }
        case 0x28:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            bool carry = (value & 0x01) != 0;
            uint8_t result = static_cast<uint8_t>((value >> 1) | (value & 0x80));
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, carry);
            break;
        }
        case 0x30:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            uint8_t result = static_cast<uint8_t>((value << 4) | (value >> 4));
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, false);
            break;
        }
        case 0x38:
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            bool carry = (value & 0x01) != 0;
            uint8_t result = static_cast<uint8_t>(value >> 1);
            write_cb_operand(result, is_memory);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, (result == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, false);
            set_c_flag(cpu, carry);
            break;
        }
        default:
            break;
        }

        const uint8_t main_group = (cb_opcode >> 6) & 0x03;
        if (main_group == 1)
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            uint8_t bitmask = static_cast<uint8_t>(1u << bit_index);
            const bool carry = get_flag(cpu, FLAG_C);
            cpu.af.reg8.low = 0;
            set_z_flag(cpu, ((value & bitmask) == 0));
            set_n_flag(cpu, false);
            set_h_flag(cpu, true);
            set_c_flag(cpu, carry);
            return;
        }

        if (main_group == 2)
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            uint8_t bitmask = static_cast<uint8_t>(~(1u << bit_index));
            value &= bitmask;
            write_cb_operand(value, is_memory);
            return;
        }

        if (main_group == 3)
        {
            bool is_memory = false;
            uint8_t value = read_cb_operand(is_memory);
            uint8_t bitmask = static_cast<uint8_t>(1u << bit_index);
            value |= bitmask;
            write_cb_operand(value, is_memory);
            return;
        }

        (void)op_group;
    }

    void apply_hl_add_flags(CPU_LR35902 &cpu, uint16_t lhs, uint16_t rhs)
    {
        const uint32_t total = static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs);
        set_n_flag(cpu, false);
        set_h_flag(cpu, (((lhs & 0x0FFFu) + (rhs & 0x0FFFu)) > 0x0FFFu));
        set_c_flag(cpu, total > 0xFFFFu);
    }

    uint16_t add_sp_offset(CPU_LR35902 &cpu, uint8_t offset)
    {
        const uint16_t old_sp = cpu.sp;
        const uint16_t result = static_cast<uint16_t>(static_cast<int32_t>(old_sp) + static_cast<int8_t>(offset));
        cpu.af.reg8.low = 0;
        set_h_flag(cpu, ((old_sp & 0x0F) + (offset & 0x0F)) > 0x0F);
        set_c_flag(cpu, ((old_sp & 0xFF) + offset) > 0xFF);
        return result;
    }
} // namespace

// Read a 16-bit word from memory (Little-Endian: Low byte at address, High byte at address + 1)
uint16_t cpu_read16(CPU_LR35902 &cpu, uint16_t address)
{
    uint8_t low = cpu_read(cpu, address);
    uint8_t high = cpu_read(cpu, address + 1);
    return static_cast<uint16_t>((high << 8) | low);
}

// Push a 16-bit value onto the stack (SP decrements by 2)
void cpu_push16(CPU_LR35902 &cpu, uint16_t val)
{
    cpu.sp -= 2;
    cpu_write(cpu, cpu.sp, val & 0xFF);
    cpu_write(cpu, cpu.sp + 1, (val >> 8) & 0xFF);
}

// Pop a 16-bit value off the stack (SP increments by 2)
uint16_t cpu_pop16(CPU_LR35902 &cpu)
{
    uint8_t low = cpu_read(cpu, cpu.sp);
    uint8_t high = cpu_read(cpu, cpu.sp + 1);
    cpu.sp += 2;
    return static_cast<uint16_t>((high << 8) | low);
}

int execute_instruction(CPU_LR35902 &cpu)
{
    uint8_t opcode = cpu_read(cpu, cpu.pc++);
    int cycles = 4;

    switch (opcode)
    {
    case 0x00: // NOP
        cycles = 4;
        break;

    case 0x01: // LD BC, d16
        cpu.bc.reg16 = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cycles = 12;
        break;

    case 0x03: // INC BC
        cpu.bc.reg16 += 1;
        cycles = 8;
        break;

    case 0x04: // INC B
        increment_register(cpu, 0);
        cycles = 4;
        break;

    case 0x05: // DEC B
        decrement_register(cpu, 0);
        cycles = 4;
        break;

    case 0x07: // RLCA
    {
        uint8_t value = cpu.af.reg8.high;
        bool carry = (value & 0x80) != 0;
        value = static_cast<uint8_t>((value << 1) | (carry ? 1u : 0u));
        cpu.af.reg8.high = value;
        cpu.af.reg8.low = 0;
        set_c_flag(cpu, carry);
        set_z_flag(cpu, false);
        set_n_flag(cpu, false);
        set_h_flag(cpu, false);
        cycles = 4;
        break;
    }

    case 0x09: // ADD HL, BC
        cpu.hl.reg16 = static_cast<uint16_t>(cpu.hl.reg16 + cpu.bc.reg16);
        apply_hl_add_flags(cpu, cpu.hl.reg16 - cpu.bc.reg16, cpu.bc.reg16);
        cycles = 8;
        break;

    case 0x02: // LD (BC), A
        cpu_write(cpu, cpu.bc.reg16, cpu.af.reg8.high);
        cycles = 8;
        break;

    case 0x06: // LD B, d8
        cpu.bc.reg8.high = cpu_read(cpu, cpu.pc++);
        cycles = 8;
        break;

    case 0x08: // LD (a16), SP
    {
        uint16_t addr = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cpu_write(cpu, addr, cpu.sp & 0xFF);
        cpu_write(cpu, addr + 1, (cpu.sp >> 8) & 0xFF);
        cycles = 20;
        break;
    }

    case 0x0A: // LD A, (BC)
        cpu.af.reg8.high = cpu_read(cpu, cpu.bc.reg16);
        cycles = 8;
        break;

    case 0x0B: // DEC BC
        cpu.bc.reg16 -= 1;
        cycles = 8;
        break;

    case 0x0C: // INC C
        increment_register(cpu, 1);
        cycles = 4;
        break;

    case 0x0D: // DEC C
        decrement_register(cpu, 1);
        cycles = 4;
        break;

    case 0x0E: // LD C, d8
        cpu.bc.reg8.low = cpu_read(cpu, cpu.pc++);
        cycles = 8;
        break;

    case 0x0F: // RRCA
    {
        uint8_t value = cpu.af.reg8.high;
        bool carry = (value & 0x01) != 0;
        value = static_cast<uint8_t>((value >> 1) | (carry ? 0x80u : 0u));
        cpu.af.reg8.high = value;
        cpu.af.reg8.low = 0;
        set_c_flag(cpu, carry);
        set_z_flag(cpu, false);
        set_n_flag(cpu, false);
        set_h_flag(cpu, false);
        cycles = 4;
        break;
    }

    case 0x11: // LD DE, d16
        cpu.de.reg16 = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cycles = 12;
        break;

    case 0x13: // INC DE
        cpu.de.reg16 += 1;
        cycles = 8;
        break;

    case 0x12: // LD (DE), A
        cpu_write(cpu, cpu.de.reg16, cpu.af.reg8.high);
        cycles = 8;
        break;

    case 0x16: // LD D, d8
        cpu.de.reg8.high = cpu_read(cpu, cpu.pc++);
        cycles = 8;
        break;

    case 0x14: // INC D
        increment_register(cpu, 2);
        cycles = 4;
        break;

    case 0x15: // DEC D
        decrement_register(cpu, 2);
        cycles = 4;
        break;

    case 0x17: // RLA
    {
        uint8_t value = cpu.af.reg8.high;
        bool carry = (value & 0x80) != 0;
        value = static_cast<uint8_t>((value << 1) | (get_flag(cpu, FLAG_C) ? 1u : 0u));
        cpu.af.reg8.high = value;
        cpu.af.reg8.low = 0;
        set_c_flag(cpu, carry);
        set_z_flag(cpu, false);
        set_n_flag(cpu, false);
        set_h_flag(cpu, false);
        cycles = 4;
        break;
    }

    case 0x19: // ADD HL, DE
        cpu.hl.reg16 = static_cast<uint16_t>(cpu.hl.reg16 + cpu.de.reg16);
        apply_hl_add_flags(cpu, cpu.hl.reg16 - cpu.de.reg16, cpu.de.reg16);
        cycles = 8;
        break;

    case 0x1A: // LD A, (DE)
        cpu.af.reg8.high = cpu_read(cpu, cpu.de.reg16);
        cycles = 8;
        break;

    case 0x1B: // DEC DE
        cpu.de.reg16 -= 1;
        cycles = 8;
        break;

    case 0x1C: // INC E
        increment_register(cpu, 3);
        cycles = 4;
        break;

    case 0x1D: // DEC E
        decrement_register(cpu, 3);
        cycles = 4;
        break;

    case 0x1F: // RRA
    {
        uint8_t value = cpu.af.reg8.high;
        bool carry = (value & 0x01) != 0;
        value = static_cast<uint8_t>((value >> 1) | (get_flag(cpu, FLAG_C) ? 0x80u : 0u));
        cpu.af.reg8.high = value;
        cpu.af.reg8.low = 0;
        set_c_flag(cpu, carry);
        set_z_flag(cpu, false);
        set_n_flag(cpu, false);
        set_h_flag(cpu, false);
        cycles = 4;
        break;
    }

    case 0x1E: // LD E, d8
        cpu.de.reg8.low = cpu_read(cpu, cpu.pc++);
        cycles = 8;
        break;

    case 0x20: // JR NZ, e8
        if (!get_flag(cpu, FLAG_Z))
        {
            int8_t offset = static_cast<int8_t>(cpu_read(cpu, cpu.pc++));
            cpu.pc = static_cast<uint16_t>(cpu.pc + offset);
            cycles = 12;
        }
        else
        {
            cpu.pc++;
            cycles = 8;
        }
        break;

    case 0x21: // LD HL, d16
        cpu.hl.reg16 = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cycles = 12;
        break;

    case 0x23: // INC HL
        cpu.hl.reg16 += 1;
        cycles = 8;
        break;

    case 0x22: // LD (HL+), A
        cpu_write(cpu, cpu.hl.reg16, cpu.af.reg8.high);
        cpu.hl.reg16 += 1;
        cycles = 8;
        break;

    case 0x24: // INC H
        increment_register(cpu, 4);
        cycles = 4;
        break;

    case 0x25: // DEC H
        decrement_register(cpu, 4);
        cycles = 4;
        break;

    case 0x26: // LD H, d8
        cpu.hl.reg8.high = cpu_read(cpu, cpu.pc++);
        cycles = 8;
        break;

    case 0x27: // DAA
    {
        uint8_t value = cpu.af.reg8.high;
        if (get_flag(cpu, FLAG_C) || (!get_flag(cpu, FLAG_N) && (value & 0x0F) > 0x09))
        {
            value += 0x06;
            set_c_flag(cpu, true);
        }
        if (get_flag(cpu, FLAG_H) && !get_flag(cpu, FLAG_N))
        {
            value += 0x06;
        }
        cpu.af.reg8.high = value;
        set_z_flag(cpu, (cpu.af.reg8.high == 0));
        set_h_flag(cpu, false);
        cycles = 4;
        break;
    }

    case 0x29: // ADD HL, HL
        cpu.hl.reg16 = static_cast<uint16_t>(cpu.hl.reg16 + cpu.hl.reg16);
        apply_hl_add_flags(cpu, cpu.hl.reg16 - cpu.hl.reg16, cpu.hl.reg16);
        cycles = 8;
        break;

    case 0x2A: // LD A, (HL+)
        cpu.af.reg8.high = cpu_read(cpu, cpu.hl.reg16);
        cpu.hl.reg16 += 1;
        cycles = 8;
        break;

    case 0x2B: // DEC HL
        cpu.hl.reg16 -= 1;
        cycles = 8;
        break;

    case 0x2C: // INC L
        increment_register(cpu, 5);
        cycles = 4;
        break;

    case 0x2D: // DEC L
        decrement_register(cpu, 5);
        cycles = 4;
        break;

    case 0x2E: // LD L, d8
        cpu.hl.reg8.low = cpu_read(cpu, cpu.pc++);
        cycles = 8;
        break;

    case 0x31: // LD SP, d16
        cpu.sp = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cycles = 12;
        break;

    case 0x39: // ADD HL, SP
        cpu.hl.reg16 = static_cast<uint16_t>(cpu.hl.reg16 + cpu.sp);
        apply_hl_add_flags(cpu, cpu.hl.reg16 - cpu.sp, cpu.sp);
        cycles = 8;
        break;

    case 0x32: // LD (HL-), A
        cpu_write(cpu, cpu.hl.reg16, cpu.af.reg8.high);
        cpu.hl.reg16 -= 1;
        cycles = 8;
        break;

    case 0x33: // INC SP
        cpu.sp += 1;
        cycles = 8;
        break;

    case 0x34: // INC (HL)
    {
        uint8_t old_value = cpu_read(cpu, cpu.hl.reg16);
        uint8_t value = static_cast<uint8_t>(old_value + 1);
        cpu_write(cpu, cpu.hl.reg16, value);
        apply_inc_flags(cpu, old_value, value, false);
        cycles = 12;
        break;
    }

    case 0x35: // DEC (HL)
    {
        uint8_t old_value = cpu_read(cpu, cpu.hl.reg16);
        uint8_t value = static_cast<uint8_t>(old_value - 1);
        cpu_write(cpu, cpu.hl.reg16, value);
        apply_dec_flags(cpu, old_value, value, false);
        cycles = 12;
        break;
    }

    case 0x37: // SCF
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, true);
        cycles = 4;
        break;

    case 0x3A: // LD A, (HL-)
        cpu.af.reg8.high = cpu_read(cpu, cpu.hl.reg16);
        cpu.hl.reg16 -= 1;
        cycles = 8;
        break;

    case 0x3B: // DEC SP
        cpu.sp -= 1;
        cycles = 8;
        break;

    case 0x3C: // INC A
        increment_register(cpu, 7);
        cycles = 4;
        break;

    case 0x3D: // DEC A
        decrement_register(cpu, 7);
        cycles = 4;
        break;

    case 0x3E: // LD A, d8
        cpu.af.reg8.high = cpu_read(cpu, cpu.pc++);
        cycles = 8;
        break;

    case 0x2F: // CPL
        cpu.af.reg8.high = static_cast<uint8_t>(~cpu.af.reg8.high);
        set_n_flag(cpu, true);
        set_h_flag(cpu, true);
        cycles = 4;
        break;

    case 0x36: // LD (HL), d8
        cpu_write(cpu, cpu.hl.reg16, cpu_read(cpu, cpu.pc++));
        cycles = 12;
        break;

    case 0x3F: // CCF
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, !get_flag(cpu, FLAG_C));
        cycles = 4;
        break;

    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4A:
    case 0x4B:
    case 0x4C:
    case 0x4D:
    case 0x4E:
    case 0x4F:
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x58:
    case 0x59:
    case 0x5A:
    case 0x5B:
    case 0x5C:
    case 0x5D:
    case 0x5E:
    case 0x5F:
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6A:
    case 0x6B:
    case 0x6C:
    case 0x6D:
    case 0x6E:
    case 0x6F:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7B:
    case 0x7C:
    case 0x7D:
    case 0x7E:
    case 0x7F:
    {
        const uint8_t dest = (opcode >> 3) & 0x07;
        const uint8_t src = opcode & 0x07;
        if (opcode == 0x76)
        {
            cpu.halted = true;
            cycles = 4;
            break;
        }
        write_reg8(cpu, dest, read_reg8(cpu, src));
        cycles = (dest == 6 || src == 6) ? 8 : 4;
        break;
    }

    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
    {
        uint8_t operand = read_reg8(cpu, opcode & 0x07);
        uint8_t a = cpu.af.reg8.high;
        uint16_t total = static_cast<uint16_t>(a) + static_cast<uint16_t>(operand);
        apply_add_flags(cpu, a, operand, false, static_cast<uint8_t>(total & 0xFF));
        cpu.af.reg8.high = static_cast<uint8_t>(total & 0xFF);
        cycles = 4;
        break;
    }

    case 0x88:
    case 0x89:
    case 0x8A:
    case 0x8B:
    case 0x8C:
    case 0x8D:
    case 0x8E:
    case 0x8F:
    {
        uint8_t operand = read_reg8(cpu, opcode & 0x07);
        uint8_t a = cpu.af.reg8.high;
        uint16_t total = static_cast<uint16_t>(a) + static_cast<uint16_t>(operand) + (get_flag(cpu, FLAG_C) ? 1u : 0u);
        apply_adc_flags(cpu, a, operand);
        cpu.af.reg8.high = static_cast<uint8_t>(total & 0xFF);
        cycles = 4;
        break;
    }

    case 0x90:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97:
    {
        uint8_t operand = read_reg8(cpu, opcode & 0x07);
        uint8_t a = cpu.af.reg8.high;
        uint8_t result = static_cast<uint8_t>(a - operand);
        apply_sub_flags(cpu, a, operand, false, result);
        cpu.af.reg8.high = result;
        cycles = 4;
        break;
    }

    case 0x98:
    case 0x99:
    case 0x9A:
    case 0x9B:
    case 0x9C:
    case 0x9D:
    case 0x9E:
    case 0x9F:
    {
        uint8_t operand = read_reg8(cpu, opcode & 0x07);
        uint8_t a = cpu.af.reg8.high;
        uint8_t result = static_cast<uint8_t>(a - operand - (get_flag(cpu, FLAG_C) ? 1u : 0u));
        apply_sub_flags(cpu, a, operand, get_flag(cpu, FLAG_C), result);
        cpu.af.reg8.high = result;
        cycles = 4;
        break;
    }

    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3:
    case 0xA4:
    case 0xA5:
    case 0xA6:
    case 0xA7:
        cpu.af.reg8.high = cpu.af.reg8.high & read_reg8(cpu, opcode & 0x07);
        apply_logic_flags(cpu, cpu.af.reg8.high, cpu.af.reg8.high == 0);
        cycles = 4;
        break;

    case 0xA8:
    case 0xA9:
    case 0xAA:
    case 0xAB:
    case 0xAC:
    case 0xAD:
    case 0xAE:
    case 0xAF:
        cpu.af.reg8.high = cpu.af.reg8.high ^ read_reg8(cpu, opcode & 0x07);
        apply_logic_flags(cpu, cpu.af.reg8.high, cpu.af.reg8.high == 0);
        cycles = 4;
        break;

    case 0xB0:
    case 0xB1:
    case 0xB2:
    case 0xB3:
    case 0xB4:
    case 0xB5:
    case 0xB6:
    case 0xB7:
        cpu.af.reg8.high = cpu.af.reg8.high | read_reg8(cpu, opcode & 0x07);
        apply_logic_flags(cpu, cpu.af.reg8.high, cpu.af.reg8.high == 0);
        cycles = 4;
        break;

    case 0xB8:
    case 0xB9:
    case 0xBA:
    case 0xBB:
    case 0xBC:
    case 0xBD:
    case 0xBE:
    case 0xBF:
    {
        uint8_t operand = read_reg8(cpu, opcode & 0x07);
        uint8_t a = cpu.af.reg8.high;
        uint8_t result = a - operand;
        apply_sub_flags(cpu, a, operand, false, result);
        cycles = 4;
        break;
    }

    case 0xC1: // POP BC
        cpu.bc.reg16 = cpu_pop16(cpu);
        cycles = 12;
        break;

    case 0xC2: // JP NZ, a16
        if (!get_flag(cpu, FLAG_Z))
        {
            cpu.pc = cpu_read16(cpu, cpu.pc);
            cycles = 16;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xC3: // JP a16
        cpu.pc = cpu_read16(cpu, cpu.pc);
        cycles = 16;
        break;

    case 0xC4: // CALL NZ, a16
        if (!get_flag(cpu, FLAG_Z))
        {
            uint16_t target = cpu_read16(cpu, cpu.pc);
            cpu.pc += 2;
            cpu_push16(cpu, cpu.pc);
            cpu.pc = target;
            cycles = 24;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xCC: // CALL Z, a16
        if (get_flag(cpu, FLAG_Z))
        {
            const uint16_t target = cpu_read16(cpu, cpu.pc);
            cpu.pc += 2;
            cpu_push16(cpu, cpu.pc);
            cpu.pc = target;
            cycles = 24;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xC5: // PUSH BC
        cpu_push16(cpu, cpu.bc.reg16);
        cycles = 16;
        break;

    case 0xC6: // ADD A, d8
    {
        uint8_t immediate = cpu_read(cpu, cpu.pc++);
        uint8_t a = cpu.af.reg8.high;
        uint16_t total = static_cast<uint16_t>(a) + static_cast<uint16_t>(immediate);
        apply_add_flags(cpu, a, immediate, false, static_cast<uint8_t>(total & 0xFF));
        cpu.af.reg8.high = static_cast<uint8_t>(total & 0xFF);
        cycles = 8;
        break;
    }

    case 0xC9: // RET
        cpu.pc = cpu_pop16(cpu);
        cycles = 16;
        break;

    case 0xCA: // JP Z, a16
        if (get_flag(cpu, FLAG_Z))
        {
            cpu.pc = cpu_read16(cpu, cpu.pc);
            cycles = 16;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xCB: // Prefix CB
    {
        uint8_t cb_opcode = cpu_read(cpu, cpu.pc++);
        handle_cb_instruction(cpu, cb_opcode);
        const uint8_t operand = cb_opcode & 0x07;
        const uint8_t group = cb_opcode >> 6;
        cycles = operand == 6 ? (group == 1 ? 12 : 16) : 8;
        break;
    }

    case 0xCD: // CALL a16
    {
        uint16_t target = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cpu_push16(cpu, cpu.pc);
        cpu.pc = target;
        cycles = 24;
        break;
    }

    case 0xCE: // ADC A, d8
    {
        uint8_t immediate = cpu_read(cpu, cpu.pc++);
        apply_adc_flags(cpu, cpu.af.reg8.high, immediate);
        cycles = 8;
        break;
    }

    case 0xD1: // POP DE
        cpu.de.reg16 = cpu_pop16(cpu);
        cycles = 12;
        break;

    case 0xD0: // RET NC
        if (!get_flag(cpu, FLAG_C))
        {
            cpu.pc = cpu_pop16(cpu);
            cycles = 20;
        }
        else
        {
            cycles = 8;
        }
        break;

    case 0xD2: // JP NC, a16
        if (!get_flag(cpu, FLAG_C))
        {
            cpu.pc = cpu_read16(cpu, cpu.pc);
            cycles = 16;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xD4: // CALL NC, a16
        if (!get_flag(cpu, FLAG_C))
        {
            const uint16_t target = cpu_read16(cpu, cpu.pc);
            cpu.pc += 2;
            cpu_push16(cpu, cpu.pc);
            cpu.pc = target;
            cycles = 24;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xD5: // PUSH DE
        cpu_push16(cpu, cpu.de.reg16);
        cycles = 16;
        break;

    case 0xD6: // SUB d8
    {
        uint8_t immediate = cpu_read(cpu, cpu.pc++);
        uint8_t result = static_cast<uint8_t>(cpu.af.reg8.high - immediate);
        apply_sub_flags(cpu, cpu.af.reg8.high, immediate, false, result);
        cpu.af.reg8.high = result;
        cycles = 8;
        break;
    }

    case 0xD9: // RETI
        cpu.pc = cpu_pop16(cpu);
        cpu.ime = true;
        cycles = 16;
        break;

    case 0xDA: // JP C, a16
        if (get_flag(cpu, FLAG_C))
        {
            cpu.pc = cpu_read16(cpu, cpu.pc);
            cycles = 16;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xDC: // CALL C, a16
        if (get_flag(cpu, FLAG_C))
        {
            const uint16_t target = cpu_read16(cpu, cpu.pc);
            cpu.pc += 2;
            cpu_push16(cpu, cpu.pc);
            cpu.pc = target;
            cycles = 24;
        }
        else
        {
            cpu.pc += 2;
            cycles = 12;
        }
        break;

    case 0xDE: // SBC A, d8
    {
        const uint8_t immediate = cpu_read(cpu, cpu.pc++);
        const uint8_t a = cpu.af.reg8.high;
        const bool carry = get_flag(cpu, FLAG_C);
        const uint8_t result = make_8bit_sub_result(a, immediate, carry);
        apply_sub_flags(cpu, a, immediate, carry, result);
        cpu.af.reg8.high = result;
        cycles = 8;
        break;
    }

    case 0xE0: // LDH (a8), A
    {
        uint8_t addr = cpu_read(cpu, cpu.pc++);
        cpu_write(cpu, 0xFF00u + addr, cpu.af.reg8.high);
        cycles = 12;
        break;
    }

    case 0xE1: // POP HL
        cpu.hl.reg16 = cpu_pop16(cpu);
        cycles = 12;
        break;

    case 0xE2: // LD (C), A
        cpu_write(cpu, 0xFF00u + cpu.bc.reg8.low, cpu.af.reg8.high);
        cycles = 8;
        break;

    case 0xE5: // PUSH HL
        cpu_push16(cpu, cpu.hl.reg16);
        cycles = 16;
        break;

    case 0xE6: // AND d8
    {
        uint8_t immediate = cpu_read(cpu, cpu.pc++);
        cpu.af.reg8.high &= immediate;
        apply_logic_flags(cpu, cpu.af.reg8.high, cpu.af.reg8.high == 0);
        cycles = 8;
        break;
    }

    case 0xE8: // ADD SP, e8
        cpu.sp = add_sp_offset(cpu, cpu_read(cpu, cpu.pc++));
        cycles = 16;
        break;

    case 0xE9: // JP (HL)
        cpu.pc = cpu.hl.reg16;
        cycles = 4;
        break;

    case 0xEA: // LD (a16), A
    {
        uint16_t addr = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cpu_write(cpu, addr, cpu.af.reg8.high);
        cycles = 16;
        break;
    }

    case 0xEE: // XOR d8
    {
        cpu.af.reg8.high ^= cpu_read(cpu, cpu.pc++);
        apply_logic_flags(cpu, cpu.af.reg8.high, cpu.af.reg8.high == 0);
        cycles = 8;
        break;
    }

    case 0xF0: // LDH A, (a8)
    {
        uint8_t addr = cpu_read(cpu, cpu.pc++);
        cpu.af.reg8.high = cpu_read(cpu, 0xFF00u + addr);
        cycles = 12;
        break;
    }

    case 0xF2: // LD A, (C)
        cpu.af.reg8.high = cpu_read(cpu, static_cast<uint16_t>(0xFF00u + cpu.bc.reg8.low));
        cycles = 8;
        break;

    case 0xF1: // POP AF
        cpu.af.reg16 = cpu_pop16(cpu);
        cpu.af.reg8.low &= 0xF0;
        cycles = 12;
        break;

    case 0xF5: // PUSH AF
        cpu_push16(cpu, cpu.af.reg16);
        cycles = 16;
        break;

    case 0xF6: // OR d8
    {
        uint8_t immediate = cpu_read(cpu, cpu.pc++);
        cpu.af.reg8.high |= immediate;
        apply_logic_flags(cpu, cpu.af.reg8.high, cpu.af.reg8.high == 0);
        cycles = 8;
        break;
    }

    case 0xF8: // LD HL, SP+e8
        cpu.hl.reg16 = add_sp_offset(cpu, cpu_read(cpu, cpu.pc++));
        cycles = 12;
        break;

    case 0xF9: // LD SP, HL
        cpu.sp = cpu.hl.reg16;
        cycles = 8;
        break;

    case 0xFA: // LD A, (a16)
    {
        uint16_t addr = cpu_read16(cpu, cpu.pc);
        cpu.pc += 2;
        cpu.af.reg8.high = cpu_read(cpu, addr);
        cycles = 16;
        break;
    }

    case 0xFE: // CP d8
    {
        uint8_t immediate = cpu_read(cpu, cpu.pc++);
        uint8_t result = static_cast<uint8_t>(cpu.af.reg8.high - immediate);
        apply_sub_flags(cpu, cpu.af.reg8.high, immediate, false, result);
        cycles = 8;
        break;
    }

    case 0xFB: // EI
        cpu.ime = true;
        cycles = 4;
        break;

    case 0xF3: // DI
        cpu.ime = false;
        cycles = 4;
        break;

    case 0x76: // HALT
        cpu.halted = true;
        cycles = 4;
        break;

    case 0x18: // JR e8
    {
        int8_t offset = static_cast<int8_t>(cpu_read(cpu, cpu.pc++));
        cpu.pc = static_cast<uint16_t>(cpu.pc + offset);
        cycles = 12;
        break;
    }

    case 0x28: // JR Z, e8
    {
        int8_t offset = static_cast<int8_t>(cpu_read(cpu, cpu.pc++));
        if (get_flag(cpu, FLAG_Z))
        {
            cpu.pc = static_cast<uint16_t>(cpu.pc + offset);
            cycles = 12;
        }
        else
        {
            cycles = 8;
        }
        break;
    }

    case 0x38: // JR C, e8
    {
        int8_t offset = static_cast<int8_t>(cpu_read(cpu, cpu.pc++));
        if (get_flag(cpu, FLAG_C))
        {
            cpu.pc = static_cast<uint16_t>(cpu.pc + offset);
            cycles = 12;
        }
        else
        {
            cycles = 8;
        }
        break;
    }

    case 0xC0: // RET NZ
        if (!get_flag(cpu, FLAG_Z))
        {
            cpu.pc = cpu_pop16(cpu);
            cycles = 20;
        }
        else
        {
            cycles = 8;
        }
        break;

    case 0xC8: // RET Z
        if (get_flag(cpu, FLAG_Z))
        {
            cpu.pc = cpu_pop16(cpu);
            cycles = 20;
        }
        else
        {
            cycles = 8;
        }
        break;

    case 0xD8: // RET C
        if (get_flag(cpu, FLAG_C))
        {
            cpu.pc = cpu_pop16(cpu);
            cycles = 20;
        }
        else
        {
            cycles = 8;
        }
        break;

    case 0xC7:
    case 0xCF:
    case 0xD7:
    case 0xDF:
    case 0xE7:
    case 0xEF:
    case 0xF7:
    case 0xFF:
        // RST vectors
        cpu_push16(cpu, cpu.pc);
        cpu.pc = static_cast<uint16_t>(opcode & 0x38);
        cycles = 16;
        break;

    default:
        std::cerr << "Unimplemented opcode: 0x" << std::hex << static_cast<int>(opcode) << std::endl;
        cpu.halted = true;
        cycles = 4;
        break;
    }

    return cycles;
}