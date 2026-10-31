#include "LR35902.h"

// Important Game Boy Memory Ranges
const uint16_t VRAM_START = 0x8000;
const uint16_t VRAM_END = 0x9FFF;
const uint16_t WRAM_START = 0xC000;
const uint16_t WRAM_END = 0xDFFF;
const uint16_t ECHO_START = 0xE000;
const uint16_t ECHO_END = 0xFDFF;
const uint16_t OAM_START = 0xFE00;
const uint16_t OAM_END = 0xFE9F;
const uint16_t OAM_UNUSABLE_START = 0xFEA0;
const uint16_t OAM_UNUSABLE_END = 0xFEFF;
const uint16_t IO_START = 0xFF00;
const uint16_t IO_END = 0xFF7F;
const uint16_t HRAM_START = 0xFF80;
const uint16_t HRAM_END = 0xFFFE;
const uint16_t IE_REG = 0xFFFF; // Interrupt Enable Register
const uint16_t JOYP_REG = 0xFF00;

namespace
{
    uint8_t joypad_lines(const CPU_LR35902 &cpu)
    {
        const uint8_t select = cpu.memory[JOYP_REG] & 0x30;
        uint8_t lines = 0x0F;
        if ((select & 0x10) == 0)
        {
            lines &= cpu.joypad_buttons & 0x0F;
        }
        if ((select & 0x20) == 0)
        {
            lines &= (cpu.joypad_buttons >> 4) & 0x0F;
        }
        return lines;
    }

    void request_joypad_interrupt_on_falling_edge(CPU_LR35902 &cpu, uint8_t old_lines)
    {
        const uint8_t new_lines = joypad_lines(cpu);
        if ((old_lines & static_cast<uint8_t>(~new_lines) & 0x0F) != 0)
        {
            cpu.memory[0xFF0F] |= 0x10;
        }
    }
}

uint8_t cpu_read(CPU_LR35902 &cpu, uint16_t address)
{
    if (address >= ECHO_START && address <= ECHO_END)
    {
        address -= 0x2000;
    }

    if (address < 0x8000 && !cpu.cartridge_rom.empty())
    {
        return cpu.cartridge_rom[address];
    }

    if (address == JOYP_REG)
    {
        return static_cast<uint8_t>(0xC0 | (cpu.memory[JOYP_REG] & 0x30) | joypad_lines(cpu));
    }

    if (address >= VRAM_START && address <= VRAM_END && cpu.vram_locked)
    {
        return 0xFF;
    }

    if (address >= OAM_START && address <= OAM_END && cpu.oam_locked)
    {
        return 0xFF;
    }

    if (address >= OAM_UNUSABLE_START && address <= OAM_UNUSABLE_END)
    {
        return 0xFF;
    }

    return cpu.memory[address];
}

void cpu_write(CPU_LR35902 &cpu, uint16_t address, uint8_t value)
{
    if (address == JOYP_REG)
    {
        const uint8_t old_lines = joypad_lines(cpu);
        cpu.memory[JOYP_REG] = value & 0x30;
        request_joypad_interrupt_on_falling_edge(cpu, old_lines);
        return;
    }

    if (address == 0xFF04)
    {
        cpu.memory[address] = 0;
        cpu.div_cycle_accumulator = 0;
        return;
    }

    if (address < 0x8000)
    {
        if (!cpu.cartridge_rom.empty())
        {
            return;
        }

        if ((address & 0x1FFF) == 0x0000)
        {
            cpu.cart_ram_enabled = (value & 0x0F) == 0x0A;
        }
        return;
    }

    if (address >= VRAM_START && address <= VRAM_END && cpu.vram_locked)
    {
        return;
    }

    if (address >= OAM_START && address <= OAM_END && cpu.oam_locked)
    {
        return;
    }

    if (address >= OAM_UNUSABLE_START && address <= OAM_UNUSABLE_END)
    {
        return;
    }

    // Echo RAM mapping (WRAM mirror from 0xC000-0xDFFF to 0xE000-0xFDFF)
    if (address >= ECHO_START && address <= ECHO_END)
    {
        address -= 0x2000;
    }

    cpu.memory[address] = value;
}

void cpu_set_joypad_button(CPU_LR35902 &cpu, JoypadButton button, bool pressed)
{
    const uint8_t button_index = static_cast<uint8_t>(button);
    if (button_index >= 8)
    {
        return;
    }

    const uint8_t old_lines = joypad_lines(cpu);
    const uint8_t button_mask = static_cast<uint8_t>(1u << button_index);
    if (pressed)
    {
        cpu.joypad_buttons &= static_cast<uint8_t>(~button_mask);
    }
    else
    {
        cpu.joypad_buttons |= button_mask;
    }
    request_joypad_interrupt_on_falling_edge(cpu, old_lines);
}