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
    uint8_t read_cartridge_rom(const CPU_LR35902 &cpu, uint16_t address)
    {
        const std::size_t bank_count = cpu.cartridge_rom.size() / 0x4000;
        std::size_t bank = 0;
        if (address < 0x4000)
        {
            if (cpu.cartridge_mbc1 && cpu.mbc1_banking_mode != 0)
            {
                bank = static_cast<std::size_t>(cpu.mbc1_bank_high) << 5;
            }
        }
        else if (cpu.cartridge_mbc1)
        {
            bank = (static_cast<std::size_t>(cpu.mbc1_bank_high) << 5) | cpu.mbc1_rom_bank_low;
        }
        else
        {
            bank = 1;
        }

        bank %= bank_count;
        return cpu.cartridge_rom[bank * 0x4000 + (address & 0x3FFF)];
    }

    std::size_t cartridge_ram_index(const CPU_LR35902 &cpu, uint16_t address)
    {
        const std::size_t bank = cpu.mbc1_banking_mode != 0 ? cpu.mbc1_bank_high : 0;
        return (bank * 0x2000 + (address - 0xA000)) % cpu.cartridge_ram.size();
    }

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
            cpu.stopped = false;
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
        return read_cartridge_rom(cpu, address);
    }

    if (address >= 0xA000 && address < 0xC000 && !cpu.cartridge_rom.empty())
    {
        if (!cpu.cartridge_mbc1 || !cpu.cart_ram_enabled || cpu.cartridge_ram.empty())
        {
            return 0xFF;
        }
        return cpu.cartridge_ram[cartridge_ram_index(cpu, address)];
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
            if (cpu.cartridge_mbc1)
            {
                if (address < 0x2000)
                {
                    cpu.cart_ram_enabled = !cpu.cartridge_ram.empty() && (value & 0x0F) == 0x0A;
                }
                else if (address < 0x4000)
                {
                    cpu.mbc1_rom_bank_low = value & 0x1F;
                    if (cpu.mbc1_rom_bank_low == 0)
                    {
                        cpu.mbc1_rom_bank_low = 1;
                    }
                }
                else if (address < 0x6000)
                {
                    cpu.mbc1_bank_high = value & 0x03;
                }
                else
                {
                    cpu.mbc1_banking_mode = value & 0x01;
                }
            }
            return;
        }

        if ((address & 0x1FFF) == 0x0000)
        {
            cpu.cart_ram_enabled = (value & 0x0F) == 0x0A;
        }
        return;
    }

    if (address >= 0xA000 && address < 0xC000 && !cpu.cartridge_rom.empty())
    {
        if (cpu.cartridge_mbc1 && cpu.cart_ram_enabled && !cpu.cartridge_ram.empty())
        {
            cpu.cartridge_ram[cartridge_ram_index(cpu, address)] = value;
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
    const bool was_pressed = (cpu.joypad_buttons & button_mask) == 0;
    if (pressed)
    {
        cpu.joypad_buttons &= static_cast<uint8_t>(~button_mask);
        if (!was_pressed)
        {
            cpu.stopped = false;
        }
    }
    else
    {
        cpu.joypad_buttons |= button_mask;
    }
    request_joypad_interrupt_on_falling_edge(cpu, old_lines);
}