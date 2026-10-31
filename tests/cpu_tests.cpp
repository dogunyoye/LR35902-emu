#include "LR35902.h"

#include <cassert>
#include <utility>

static void test_load_and_add()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x3E; // LD A, d8
    cpu.memory[0x0001] = 0x42;
    cpu.memory[0x0002] = 0x06; // LD B, d8
    cpu.memory[0x0003] = 0x05;
    cpu.memory[0x0004] = 0x80; // ADD A, B
    cpu.memory[0x0005] = 0x47; // LD B, A
    cpu.pc = 0x0000;

    int cycles = execute_instruction(cpu);
    assert(cycles == 8);
    assert(cpu.af.reg8.high == 0x42);
    assert(cpu.pc == 0x0002);

    cycles = execute_instruction(cpu);
    assert(cycles == 8);
    assert(cpu.bc.reg8.high == 0x05);
    assert(cpu.pc == 0x0004);

    cycles = execute_instruction(cpu);
    assert(cycles == 4);
    assert(cpu.af.reg8.high == 0x47);
    assert(cpu.pc == 0x0005);

    cycles = execute_instruction(cpu);
    assert(cycles == 4);
    assert(cpu.bc.reg8.high == 0x47);
}

static void test_stack_and_loads()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x01; // LD BC, d16
    cpu.memory[0x0001] = 0x34;
    cpu.memory[0x0002] = 0x12;
    cpu.memory[0x0003] = 0x21; // LD HL, d16
    cpu.memory[0x0004] = 0x00;
    cpu.memory[0x0005] = 0x10;
    cpu.memory[0x0006] = 0xC5; // PUSH BC
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.bc.reg16 == 0x1234);
    assert(cpu.pc == 0x0003);

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x1000);
    assert(cpu.pc == 0x0006);

    execute_instruction(cpu);
    assert(cpu.sp == 0xFFFE);
    assert(cpu.memory[0xFFFE] == 0x34);
    assert(cpu.memory[0xFFFF] == 0x12);
}

static void test_inc_dec_registers()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x04; // INC B
    cpu.memory[0x0001] = 0x05; // DEC B
    cpu.memory[0x0002] = 0x00; // NOP
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.bc.reg8.high == 0x01);
    assert(cpu.pc == 0x0001);

    execute_instruction(cpu);
    assert(cpu.bc.reg8.high == 0x00);
    assert(cpu.pc == 0x0002);

    execute_instruction(cpu);
    assert(cpu.pc == 0x0003);
}

static void test_add_hl_registers()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x21; // LD HL, d16
    cpu.memory[0x0001] = 0x00;
    cpu.memory[0x0002] = 0x10;
    cpu.memory[0x0003] = 0x01; // LD BC, d16
    cpu.memory[0x0004] = 0x34;
    cpu.memory[0x0005] = 0x12;
    cpu.memory[0x0006] = 0x09; // ADD HL, BC
    cpu.memory[0x0007] = 0x31; // LD SP, d16
    cpu.memory[0x0008] = 0x05;
    cpu.memory[0x0009] = 0x00;
    cpu.memory[0x000A] = 0x39; // ADD HL, SP
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x1000);

    execute_instruction(cpu);
    assert(cpu.bc.reg16 == 0x1234);

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x2234);

    execute_instruction(cpu);
    assert(cpu.sp == 0x0005);

    execute_instruction(cpu);
    assert(cpu.hl.reg16 == 0x2239);
}

static void test_timer_registers()
{
    CPU_LR35902 cpu{};
    cpu.memory[0xFF05] = 0xFF;
    cpu.memory[0xFF06] = 0x10;
    cpu.memory[0xFF07] = 0x04;

    update_timers(cpu, 1024);

    assert(cpu.memory[0xFF05] == 0x10);
    assert(cpu.memory[0xFF0F] == 0x04);
}

static void test_flag_behavior_add_sub()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0x3E; // LD A, d8
    cpu.memory[0x0001] = 0x0F;
    cpu.memory[0x0002] = 0x06; // LD B, d8
    cpu.memory[0x0003] = 0x01;
    cpu.memory[0x0004] = 0x80; // ADD A, B
    cpu.memory[0x0005] = 0xD6; // SUB A, d8
    cpu.memory[0x0006] = 0x01;
    cpu.pc = 0x0000;

    execute_instruction(cpu); // LD A, 0x0F
    assert(cpu.af.reg8.high == 0x0F);

    execute_instruction(cpu); // LD B, 0x01
    assert(cpu.bc.reg8.high == 0x01);

    execute_instruction(cpu); // ADD A, B => 0x10
    assert(cpu.af.reg8.high == 0x10);
    assert(get_flag(cpu, FLAG_Z) == false);
    assert(get_flag(cpu, FLAG_N) == false);
    assert(get_flag(cpu, FLAG_H) == true);
    assert(get_flag(cpu, FLAG_C) == false);

    execute_instruction(cpu); // SUB A, 0x01 => 0x0F
    assert(cpu.af.reg8.high == 0x0F);
    assert(get_flag(cpu, FLAG_Z) == false);
    assert(get_flag(cpu, FLAG_N) == true);
    assert(get_flag(cpu, FLAG_H) == true);
    assert(get_flag(cpu, FLAG_C) == false);
}

static void test_cb_prefixed_bit_and_rotate()
{
    CPU_LR35902 cpu{};
    cpu.bc.reg8.high = 0x80; // B = 0x80
    cpu.af.reg8.high = 0x80; // A = 0x80
    cpu.memory[0x0000] = 0xCB;
    cpu.memory[0x0001] = 0x00; // RLC B
    cpu.memory[0x0002] = 0xCB;
    cpu.memory[0x0003] = 0x7F; // BIT 7, A
    cpu.pc = 0x0000;

    execute_instruction(cpu);
    assert(cpu.bc.reg8.high == 0x01);
    assert(get_flag(cpu, FLAG_C) == true);
    assert(get_flag(cpu, FLAG_Z) == false);

    execute_instruction(cpu);
    assert(get_flag(cpu, FLAG_Z) == false);
    assert(get_flag(cpu, FLAG_N) == false);
    assert(get_flag(cpu, FLAG_H) == true);
    assert(get_flag(cpu, FLAG_C) == true);
}

static void test_instruction_cycle_counts()
{
    CPU_LR35902 cpu{};
    cpu.hl.reg16 = 0xC000;
    cpu.af.reg8.high = 0x5A;
    cpu.memory[0xC000] = 0x80;
    cpu.memory[0x0000] = 0x46; // LD B, (HL)
    cpu.memory[0x0001] = 0x70; // LD (HL), B
    cpu.memory[0x0002] = 0x40; // LD B, B
    cpu.memory[0x0003] = 0xCB;
    cpu.memory[0x0004] = 0x00; // RLC B
    cpu.memory[0x0005] = 0xCB;
    cpu.memory[0x0006] = 0x46; // BIT 0, (HL)
    cpu.memory[0x0007] = 0xCB;
    cpu.memory[0x0008] = 0x06; // RLC (HL)

    assert(execute_instruction(cpu) == 8);
    assert(cpu.bc.reg8.high == 0x80);
    assert(execute_instruction(cpu) == 8);
    assert(cpu.memory[0xC000] == 0x80);
    assert(execute_instruction(cpu) == 4);
    assert(execute_instruction(cpu) == 8);
    assert(get_flag(cpu, FLAG_C));
    assert(cpu.af.reg8.high == 0x5A);
    assert(execute_instruction(cpu) == 12);
    assert(get_flag(cpu, FLAG_Z));
    assert(execute_instruction(cpu) == 16);
    assert(cpu.memory[0xC000] == 0x01);
    assert(get_flag(cpu, FLAG_C));
}

static void test_control_flow_cycle_counts()
{
    CPU_LR35902 cpu{};
    cpu.memory[0x0000] = 0xC2; // JP NZ, a16
    cpu.memory[0x0001] = 0x06;
    cpu.memory[0x0002] = 0x00;
    cpu.memory[0x0006] = 0xCA; // JP Z, a16
    cpu.memory[0x0007] = 0x09;
    cpu.memory[0x0008] = 0x00;
    cpu.memory[0x0009] = 0x76; // HALT

    assert(execute_instruction(cpu) == 16);
    assert(cpu.pc == 0x0006);

    set_flag(cpu, FLAG_Z, false);
    assert(execute_instruction(cpu) == 12);
    assert(cpu.pc == 0x0009);

    assert(execute_instruction(cpu) == 4);
    assert(cpu.halted);
    assert(cpu.pc == 0x000A);
}

static void test_rom_only_cartridge()
{
    CPU_LR35902 cpu{};
    std::vector<uint8_t> rom(0x8000, 0);
    rom[0x0100] = 0x3E; // LD A, d8
    rom[0x0101] = 0x42;
    rom[0x0147] = 0x00; // ROM ONLY
    rom[0x0148] = 0x00; // 32 KiB
    std::string error;

    assert(load_rom_only_cartridge(cpu, std::move(rom), error));
    assert(error.empty());
    assert(cpu.pc == 0x0100);
    assert(cpu.af.reg16 == 0x01B0);
    assert(cpu.sp == 0xFFFE);
    assert(cpu_read(cpu, 0x0100) == 0x3E);
    assert(execute_instruction(cpu) == 8);
    assert(cpu.af.reg8.high == 0x42);

    cpu_write(cpu, 0x0100, 0x00);
    assert(cpu_read(cpu, 0x0100) == 0x3E);

    std::vector<uint8_t> unsupported_type(0x8000, 0);
    unsupported_type[0x0147] = 0x01;
    assert(!load_rom_only_cartridge(cpu, std::move(unsupported_type), error));
    assert(!error.empty());
    assert(cpu_read(cpu, 0x0100) == 0x3E);

    std::vector<uint8_t> wrong_size(0x4000, 0);
    assert(!load_rom_only_cartridge(cpu, std::move(wrong_size), error));
    assert(!error.empty());
}

static void test_cb_rotate_shift_opcodes()
{
    struct RotateCase
    {
        uint8_t opcode;
        uint8_t input;
        bool initial_carry;
        uint8_t expected;
        bool expected_carry;
        bool expected_zero;
    };

    const RotateCase cases[] = {
        {0x08, 0x01, false, 0x80, true, false}, // RRC B
        {0x10, 0x80, true, 0x01, true, false},  // RL B
        {0x18, 0x01, true, 0x80, true, false},  // RR B
        {0x20, 0x80, false, 0x00, true, true},  // SLA B
        {0x28, 0x81, false, 0xC0, true, false}, // SRA B
        {0x30, 0xF0, true, 0x0F, false, false}, // SWAP B
        {0x38, 0x01, false, 0x00, true, true},  // SRL B
    };

    for (const RotateCase &test_case : cases)
    {
        CPU_LR35902 cpu{};
        cpu.bc.reg8.high = test_case.input;
        set_flag(cpu, FLAG_C, test_case.initial_carry);
        cpu.memory[0x0000] = 0xCB;
        cpu.memory[0x0001] = test_case.opcode;

        assert(execute_instruction(cpu) == 8);
        assert(cpu.bc.reg8.high == test_case.expected);
        assert(get_flag(cpu, FLAG_Z) == test_case.expected_zero);
        assert(!get_flag(cpu, FLAG_N));
        assert(!get_flag(cpu, FLAG_H));
        assert(get_flag(cpu, FLAG_C) == test_case.expected_carry);
    }

    CPU_LR35902 cpu{};
    cpu.hl.reg16 = 0xC000;
    cpu.memory[0x0000] = 0xCB;
    cpu.memory[0x0001] = 0x0E; // RRC (HL)
    cpu.memory[0xC000] = 0x01;
    assert(execute_instruction(cpu) == 16);
    assert(cpu.memory[0xC000] == 0x80);
    assert(get_flag(cpu, FLAG_C));
}

static void test_missing_control_flow_opcodes()
{
    struct ConditionalCase
    {
        uint8_t opcode;
        uint8_t flag;
        bool branch_when_set;
    };

    const ConditionalCase call_cases[] = {
        {0xCC, FLAG_Z, true},  // CALL Z, a16
        {0xD4, FLAG_C, false}, // CALL NC, a16
        {0xDC, FLAG_C, true},  // CALL C, a16
    };

    for (const ConditionalCase &test_case : call_cases)
    {
        for (bool taken : {false, true})
        {
            CPU_LR35902 cpu{};
            cpu.sp = 0xFFFE;
            set_flag(cpu, test_case.flag, test_case.branch_when_set == taken);
            cpu.memory[0] = test_case.opcode;
            cpu.memory[1] = 0x34;
            cpu.memory[2] = 0x12;

            assert(execute_instruction(cpu) == (taken ? 24 : 12));
            assert(cpu.pc == (taken ? 0x1234 : 0x0003));
            assert(cpu.sp == (taken ? 0xFFFC : 0xFFFE));
            if (taken)
            {
                assert(cpu.memory[0xFFFC] == 0x03);
                assert(cpu.memory[0xFFFD] == 0x00);
            }
        }
    }

    const ConditionalCase jump_cases[] = {
        {0xD2, FLAG_C, false}, // JP NC, a16
        {0xDA, FLAG_C, true},  // JP C, a16
    };

    for (const ConditionalCase &test_case : jump_cases)
    {
        for (bool taken : {false, true})
        {
            CPU_LR35902 cpu{};
            set_flag(cpu, test_case.flag, test_case.branch_when_set == taken);
            cpu.memory[0] = test_case.opcode;
            cpu.memory[1] = 0x34;
            cpu.memory[2] = 0x12;

            assert(execute_instruction(cpu) == (taken ? 16 : 12));
            assert(cpu.pc == (taken ? 0x1234 : 0x0003));
        }
    }

    CPU_LR35902 ret_cpu{};
    ret_cpu.sp = 0xC000;
    ret_cpu.memory[0] = 0xD0; // RET NC
    ret_cpu.memory[0xC000] = 0x34;
    ret_cpu.memory[0xC001] = 0x12;
    assert(execute_instruction(ret_cpu) == 20);
    assert(ret_cpu.pc == 0x1234);
    assert(ret_cpu.sp == 0xC002);

    CPU_LR35902 reti_cpu{};
    reti_cpu.sp = 0xC000;
    reti_cpu.memory[0] = 0xD9; // RETI
    reti_cpu.memory[0xC000] = 0x78;
    reti_cpu.memory[0xC001] = 0x56;
    assert(execute_instruction(reti_cpu) == 16);
    assert(reti_cpu.pc == 0x5678);
    assert(reti_cpu.sp == 0xC002);
    assert(reti_cpu.ime);
}

static void test_missing_arithmetic_and_load_opcodes()
{
    CPU_LR35902 sbc_cpu{};
    sbc_cpu.af.reg8.high = 0x10;
    set_flag(sbc_cpu, FLAG_C, true);
    sbc_cpu.memory[0] = 0xDE; // SBC A, d8
    sbc_cpu.memory[1] = 0x0F;
    assert(execute_instruction(sbc_cpu) == 8);
    assert(sbc_cpu.af.reg8.high == 0x00);
    assert(get_flag(sbc_cpu, FLAG_Z));
    assert(get_flag(sbc_cpu, FLAG_N));
    assert(get_flag(sbc_cpu, FLAG_H));
    assert(!get_flag(sbc_cpu, FLAG_C));

    CPU_LR35902 xor_cpu{};
    xor_cpu.af.reg8.high = 0x0F;
    xor_cpu.memory[0] = 0xEE; // XOR d8
    xor_cpu.memory[1] = 0xF0;
    assert(execute_instruction(xor_cpu) == 8);
    assert(xor_cpu.af.reg8.high == 0xFF);
    assert(!get_flag(xor_cpu, FLAG_Z));
    assert(!get_flag(xor_cpu, FLAG_N));
    assert(!get_flag(xor_cpu, FLAG_H));
    assert(!get_flag(xor_cpu, FLAG_C));

    CPU_LR35902 load_cpu{};
    load_cpu.bc.reg8.low = 0x80;
    load_cpu.memory[0] = 0xF2; // LD A, (C)
    load_cpu.memory[0xFF80] = 0xA5;
    assert(execute_instruction(load_cpu) == 8);
    assert(load_cpu.af.reg8.high == 0xA5);

    CPU_LR35902 add_sp_cpu{};
    add_sp_cpu.sp = 0x0001;
    add_sp_cpu.memory[0] = 0xE8; // ADD SP, e8
    add_sp_cpu.memory[1] = 0xFF;
    assert(execute_instruction(add_sp_cpu) == 16);
    assert(add_sp_cpu.sp == 0x0000);
    assert(!get_flag(add_sp_cpu, FLAG_Z));
    assert(!get_flag(add_sp_cpu, FLAG_N));
    assert(get_flag(add_sp_cpu, FLAG_H));
    assert(get_flag(add_sp_cpu, FLAG_C));

    CPU_LR35902 load_hl_cpu{};
    load_hl_cpu.sp = 0xFFF8;
    load_hl_cpu.memory[0] = 0xF8; // LD HL, SP+e8
    load_hl_cpu.memory[1] = 0x08;
    assert(execute_instruction(load_hl_cpu) == 12);
    assert(load_hl_cpu.hl.reg16 == 0x0000);
    assert(load_hl_cpu.sp == 0xFFF8);
    assert(!get_flag(load_hl_cpu, FLAG_Z));
    assert(!get_flag(load_hl_cpu, FLAG_N));
    assert(get_flag(load_hl_cpu, FLAG_H));
    assert(get_flag(load_hl_cpu, FLAG_C));
}

static void test_ei_is_delayed_until_after_next_instruction()
{
    CPU_LR35902 cpu{};
    cpu.memory[0] = 0xFB; // EI
    cpu.memory[1] = 0x00; // NOP

    assert(execute_instruction(cpu) == 4);
    assert(!cpu.ime);
    assert(execute_instruction(cpu) == 4);
    assert(cpu.ime);
}

static void test_interrupt_dispatch_and_priority()
{
    CPU_LR35902 cpu{};
    cpu.pc = 0x1234;
    cpu.sp = 0xFFFE;
    cpu.ime = true;
    cpu.memory[0xFFFF] = 0x15;
    cpu.memory[0xFF0F] = 0x15;

    assert(step_cpu(cpu) == 20);
    assert(cpu.pc == 0x0040);
    assert(cpu.sp == 0xFFFC);
    assert(cpu.memory[0xFFFC] == 0x34);
    assert(cpu.memory[0xFFFD] == 0x12);
    assert(cpu.memory[0xFF0F] == 0x14);
    assert(!cpu.ime);
}

static void test_ei_delay_before_interrupt_dispatch()
{
    CPU_LR35902 cpu{};
    cpu.sp = 0xFFFE;
    cpu.memory[0] = 0xFB; // EI
    cpu.memory[1] = 0x00; // NOP
    cpu.memory[0xFFFF] = 0x01;
    cpu.memory[0xFF0F] = 0x01;

    assert(step_cpu(cpu) == 4);
    assert(!cpu.ime);
    assert(step_cpu(cpu) == 4);
    assert(cpu.ime);
    assert(cpu.pc == 2);
    assert(step_cpu(cpu) == 20);
    assert(cpu.pc == 0x0040);
    assert(cpu.memory[0xFFFC] == 2);
    assert(cpu.memory[0xFFFD] == 0);
}

static void test_halt_idle_wake_and_bug()
{
    CPU_LR35902 cpu{};
    cpu.pc = 0x0100;
    cpu.halted = true;
    cpu.memory[0x0100] = 0x3E; // LD A, d8
    cpu.memory[0x0101] = 0x99;

    assert(step_cpu(cpu) == 4);
    assert(cpu.halted);
    assert(cpu.pc == 0x0100);

    cpu.memory[0xFFFF] = 0x01;
    cpu.memory[0xFF0F] = 0x01;
    assert(step_cpu(cpu) == 8);
    assert(!cpu.halted);
    assert(cpu.af.reg8.high == 0x99);
    assert(cpu.pc == 0x0102);

    CPU_LR35902 halt_bug_cpu{};
    halt_bug_cpu.memory[0] = 0x76; // HALT with a pending interrupt and IME clear
    halt_bug_cpu.memory[1] = 0x3E; // LD A, d8
    halt_bug_cpu.memory[2] = 0x42;
    halt_bug_cpu.memory[0xFFFF] = 0x01;
    halt_bug_cpu.memory[0xFF0F] = 0x01;
    assert(step_cpu(halt_bug_cpu) == 4);
    assert(!halt_bug_cpu.halted);
    assert(halt_bug_cpu.halt_bug);
    assert(step_cpu(halt_bug_cpu) == 8);
    assert(halt_bug_cpu.af.reg8.high == 0x3E);
    assert(halt_bug_cpu.pc == 2);
}

int main()
{
    test_load_and_add();
    test_stack_and_loads();
    test_inc_dec_registers();
    test_add_hl_registers();
    test_timer_registers();
    test_flag_behavior_add_sub();
    test_cb_prefixed_bit_and_rotate();
    test_instruction_cycle_counts();
    test_control_flow_cycle_counts();
    test_rom_only_cartridge();
    test_cb_rotate_shift_opcodes();
    test_missing_control_flow_opcodes();
    test_missing_arithmetic_and_load_opcodes();
    test_ei_is_delayed_until_after_next_instruction();
    test_interrupt_dispatch_and_priority();
    test_ei_delay_before_interrupt_dispatch();
    test_halt_idle_wake_and_bug();
    return 0;
}
