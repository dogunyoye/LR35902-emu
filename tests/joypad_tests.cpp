#include "LR35902.h"

#include <cassert>

static void test_joypad_group_selection_and_active_low_bits()
{
    CPU_LR35902 cpu{};
    cpu_write(cpu, 0xFF00, 0x30); // Neither group selected
    assert(cpu_read(cpu, 0xFF00) == 0xFF);

    cpu_write(cpu, 0xFF00, 0x20); // Select directions
    cpu_set_joypad_button(cpu, JoypadButton::Up, true);
    assert(cpu_read(cpu, 0xFF00) == 0xEB);

    cpu_set_joypad_button(cpu, JoypadButton::A, true);
    assert(cpu_read(cpu, 0xFF00) == 0xEB);

    cpu_set_joypad_button(cpu, JoypadButton::A, false);
    cpu_write(cpu, 0xFF00, 0x10); // Select action buttons
    assert(cpu_read(cpu, 0xFF00) == 0xDF);
    cpu_set_joypad_button(cpu, JoypadButton::A, true);
    assert(cpu_read(cpu, 0xFF00) == 0xDE);

    cpu_write(cpu, 0xFF00, 0x00); // Both groups selected, lines are ANDed
    cpu_set_joypad_button(cpu, JoypadButton::Right, true);
    assert(cpu_read(cpu, 0xFF00) == 0xCA);
}

static void test_joypad_interrupt_edges_and_selection()
{
    CPU_LR35902 cpu{};
    cpu_write(cpu, 0xFF00, 0x20); // Select directions
    cpu_set_joypad_button(cpu, JoypadButton::Right, true);
    assert((cpu.memory[0xFF0F] & 0x10) != 0);

    cpu.memory[0xFF0F] &= static_cast<uint8_t>(~0x10);
    cpu_set_joypad_button(cpu, JoypadButton::Right, true);
    assert((cpu.memory[0xFF0F] & 0x10) == 0);
    cpu_set_joypad_button(cpu, JoypadButton::Right, false);
    assert((cpu.memory[0xFF0F] & 0x10) == 0);
    cpu_set_joypad_button(cpu, JoypadButton::Right, true);
    assert((cpu.memory[0xFF0F] & 0x10) != 0);

    cpu.memory[0xFF0F] &= static_cast<uint8_t>(~0x10);
    cpu_write(cpu, 0xFF00, 0x10); // Directions are now unselected
    cpu_set_joypad_button(cpu, JoypadButton::Down, true);
    assert((cpu.memory[0xFF0F] & 0x10) == 0);
    cpu_write(cpu, 0xFF00, 0x20); // Selecting the held button creates a falling edge
    assert((cpu.memory[0xFF0F] & 0x10) != 0);
}

static void test_joypad_interrupt_dispatch()
{
    CPU_LR35902 cpu{};
    cpu.sp = 0xFFFE;
    cpu.ime = true;
    cpu.memory[0xFFFF] = 0x10;
    cpu_write(cpu, 0xFF00, 0x10);
    cpu_set_joypad_button(cpu, JoypadButton::Start, true);

    assert(step_cpu(cpu) == 20);
    assert(cpu.pc == 0x0060);
    assert(cpu.sp == 0xFFFC);
    assert(cpu.memory[0xFFFC] == 0x00);
    assert(cpu.memory[0xFFFD] == 0x00);
    assert((cpu.memory[0xFF0F] & 0x10) == 0);
}

int main()
{
    test_joypad_group_selection_and_active_low_bits();
    test_joypad_interrupt_edges_and_selection();
    test_joypad_interrupt_dispatch();
    return 0;
}