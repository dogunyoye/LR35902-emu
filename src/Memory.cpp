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
const uint16_t SB_REG = 0xFF01;
const uint16_t SC_REG = 0xFF02;

namespace
{
    uint8_t cpu_read_unrestricted(CPU_LR35902 &cpu, uint16_t address);

    bool is_unmapped_dmg_io(uint16_t address)
    {
        return address == 0xFF03 || (address >= 0xFF08 && address <= 0xFF0E) ||
               address == 0xFF15 || address == 0xFF1F ||
               (address >= 0xFF27 && address <= 0xFF29) ||
               (address >= 0xFF4C && address <= 0xFF7F);
    }

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
        else if (cpu.cartridge_mbc5)
        {
            bank = cpu.mbc5_rom_bank;
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
        const std::size_t bank = cpu.cartridge_mbc5 ? cpu.mbc5_ram_bank :
                                 (cpu.mbc1_banking_mode != 0 ? cpu.mbc1_bank_high : 0);
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

namespace
{
    uint8_t cpu_read_unrestricted(CPU_LR35902 &cpu, uint16_t address)
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
        if ((!cpu.cartridge_mbc1 && !cpu.cartridge_mbc5) || !cpu.cart_ram_enabled || cpu.cartridge_ram.empty())
        {
            return 0xFF;
        }
        return cpu.cartridge_ram[cartridge_ram_index(cpu, address)];
    }

    if (address == JOYP_REG)
    {
        return static_cast<uint8_t>(0xC0 | (cpu.memory[JOYP_REG] & 0x30) | joypad_lines(cpu));
    }

    if (address == SC_REG)
    {
        return static_cast<uint8_t>(0x7E | (cpu.memory[SC_REG] & 0x81));
    }

    if (address == 0xFF0F)
    {
        return static_cast<uint8_t>(0xE0 | (cpu.memory[address] & 0x1F));
    }

    if (address == 0xFF41)
    {
        return static_cast<uint8_t>(0x80 | cpu.memory[address]);
    }

    if (address == 0xFF07)
    {
        return static_cast<uint8_t>(0xF8 | (cpu.memory[address] & 0x07));
    }

    switch (address)
    {
    case 0xFF10:
        return static_cast<uint8_t>(cpu.memory[address] | 0x80);
    case 0xFF1A:
        return static_cast<uint8_t>((cpu.memory[address] & 0x80) | 0x7F);
    case 0xFF1C:
        return static_cast<uint8_t>((cpu.memory[address] & 0x60) | 0x9F);
    case 0xFF20:
        return static_cast<uint8_t>((cpu.memory[address] & 0x3F) | 0xC0);
    case 0xFF23:
        return static_cast<uint8_t>((cpu.memory[address] & 0xC0) | 0x3F);
    case 0xFF26:
        return static_cast<uint8_t>((cpu.memory[address] & 0x8F) | 0x70);
    default:
        break;
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
}

uint8_t cpu_read(CPU_LR35902 &cpu, uint16_t address)
{
    if (is_unmapped_dmg_io(address))
    {
        return 0xFF;
    }
    const bool hram_access = address >= HRAM_START && address <= HRAM_END;
    const bool echo_wram_access = address >= ECHO_START && address <= ECHO_END;
    if (cpu.oam_dma_bus_blocked && !hram_access && !echo_wram_access && address != 0xFF46)
    {
        return 0xFF;
    }
    return cpu_read_unrestricted(cpu, address);
}

void update_oam_dma(CPU_LR35902 &cpu, int cycles)
{
    if (!cpu.oam_dma_active || cycles <= 0)
    {
        return;
    }

    for (int cycle = 0; cycle < cycles && cpu.oam_dma_active; ++cycle)
    {
        if (cpu.oam_dma_startup_delay != 0)
        {
            --cpu.oam_dma_startup_delay;
            if (cpu.oam_dma_startup_delay == 0)
            {
                cpu.oam_dma_bus_blocked = true;
            }
        }

        if (cpu.oam_dma_first_byte_delay != 0)
        {
            --cpu.oam_dma_first_byte_delay;
            if (cpu.oam_dma_first_byte_delay == 0)
            {
                const uint16_t source_address = static_cast<uint16_t>(
                    (static_cast<uint16_t>(cpu.oam_dma_source) << 8) + cpu.oam_dma_index);
                const uint16_t dma_bus_address = source_address >= 0xFE00
                                                     ? static_cast<uint16_t>(source_address - 0x2000)
                                                     : source_address;
                cpu.memory[OAM_START + cpu.oam_dma_index] = cpu_read_unrestricted(cpu, dma_bus_address);
                ++cpu.oam_dma_index;
            }
            continue;
        }

        ++cpu.oam_dma_cycle_phase;
        if (cpu.oam_dma_cycle_phase != 4)
        {
            continue;
        }

        cpu.oam_dma_cycle_phase = 0;
        const uint16_t source_address = static_cast<uint16_t>(
            (static_cast<uint16_t>(cpu.oam_dma_source) << 8) + cpu.oam_dma_index);
        const uint16_t destination_address = static_cast<uint16_t>(OAM_START + cpu.oam_dma_index);
        const uint16_t dma_bus_address = source_address >= 0xFE00
                                             ? static_cast<uint16_t>(source_address - 0x2000)
                                             : source_address;
        cpu.memory[destination_address] = cpu_read_unrestricted(cpu, dma_bus_address);
        ++cpu.oam_dma_index;
        if (cpu.oam_dma_index == 0xA0)
        {
            cpu.oam_dma_active = false;
            cpu.oam_dma_bus_blocked = false;
        }
    }
}

void advance_oam_dma_for_cpu_instruction(CPU_LR35902 &cpu, int cycles)
{
    if (cycles <= 0)
    {
        return;
    }
    update_oam_dma(cpu, cycles);
    if (cpu.cpu_instruction_active)
    {
        cpu.cpu_instruction_dma_cycles += cycles;
    }
}

void cpu_write(CPU_LR35902 &cpu, uint16_t address, uint8_t value)
{
    if (is_unmapped_dmg_io(address))
    {
        return;
    }
    if (cpu.oam_dma_bus_blocked && address != 0xFF46 && !(address >= HRAM_START && address <= HRAM_END))
    {
        return;
    }

    if (address == 0xFF46)
    {
        cpu.memory[address] = value;
        cpu.oam_dma_source = value;
        cpu.oam_dma_index = 0;
        cpu.oam_dma_cycle_phase = 0;
        cpu.oam_dma_active = true;
        cpu.oam_dma_bus_blocked = false;
        cpu.oam_dma_startup_delay = 4;
        cpu.oam_dma_first_byte_delay = 4;
        return;
    }

    if (address == JOYP_REG)
    {
        const uint8_t old_lines = joypad_lines(cpu);
        cpu.memory[JOYP_REG] = value & 0x30;
        request_joypad_interrupt_on_falling_edge(cpu, old_lines);
        return;
    }

    if (address == SC_REG)
    {
        cpu.memory[SC_REG] = value & 0x81;
        cpu.serial_transfer_active = (value & 0x80) != 0;
        cpu.serial_internal_clock = (value & 0x01) != 0;
        cpu.serial_cycles_remaining = cpu.serial_transfer_active && cpu.serial_internal_clock ? 8192 : 0;
        if (cpu.serial_transfer_active)
        {
            cpu.serial_shift_register = cpu.memory[SB_REG];
        }
        return;
    }

    if (address == 0xFF0F)
    {
        cpu.memory[address] = value & 0x1F;
        return;
    }

    if (address == 0xFF41)
    {
        cpu.memory[address] = static_cast<uint8_t>((cpu.memory[address] & 0x07) | (value & 0x78));
        return;
    }

    if (address == 0xFF04)
    {
        reset_divider(cpu);
        return;
    }

    if (address == 0xFF05)
    {
        if (cpu.tima_reload_delay == 1 || cpu.tima_reloaded_this_cycle)
        {
            return;
        }
        if (cpu.tima_reload_delay > 1)
        {
            cpu.tima_reload_delay = 0;
        }
        cpu.memory[address] = value;
        return;
    }

    if (address == 0xFF06)
    {
        cpu.memory[address] = value;
        if (cpu.tima_reloaded_this_cycle)
        {
            cpu.memory[0xFF05] = value;
        }
        return;
    }

    if (address == 0xFF07)
    {
        write_timer_control(cpu, value);
        return;
    }

    if (address < 0x8000)
    {
        if (!cpu.cartridge_rom.empty())
        {
            if (cpu.cartridge_mbc5)
            {
                if (address < 0x2000)
                {
                    cpu.cart_ram_enabled = !cpu.cartridge_ram.empty() && (value & 0x0F) == 0x0A;
                }
                else if (address < 0x3000)
                {
                    cpu.mbc5_rom_bank = static_cast<uint16_t>((cpu.mbc5_rom_bank & 0x100) | value);
                }
                else if (address < 0x4000)
                {
                    cpu.mbc5_rom_bank = static_cast<uint16_t>((cpu.mbc5_rom_bank & 0xFF) | ((value & 0x01) << 8));
                }
                else if (address < 0x6000)
                {
                    cpu.mbc5_ram_bank = value & 0x0F;
                }
            }
            else if (cpu.cartridge_mbc1)
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
        if ((cpu.cartridge_mbc1 || cpu.cartridge_mbc5) && cpu.cart_ram_enabled && !cpu.cartridge_ram.empty())
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
