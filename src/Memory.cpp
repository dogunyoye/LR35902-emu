#include "LR35902.h"
#include "PPU.h"

#include <algorithm>

// Important Game Boy Memory Ranges
const uint16_t VRAM_START = 0x8000;
const uint16_t VRAM_END = 0x9FFF;
const uint16_t ECHO_START = 0xE000;
const uint16_t ECHO_END = 0xFDFF;
const uint16_t OAM_START = 0xFE00;
const uint16_t OAM_END = 0xFE9F;
const uint16_t OAM_UNUSABLE_START = 0xFEA0;
const uint16_t OAM_UNUSABLE_END = 0xFEFF;
const uint16_t HRAM_START = 0xFF80;
const uint16_t HRAM_END = 0xFFFE;
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
        // Multicarts wire BANK2 to ROM A18-A19 and leave BANK1 bit 4 unconnected.
        const int mbc1_high_shift = cpu.cartridge_mbc1_multicart ? 4 : 5;
        const uint8_t mbc1_low_mask = cpu.cartridge_mbc1_multicart ? 0x0F : 0x1F;
        std::size_t bank = 0;
        if (address < 0x4000)
        {
            if (cpu.cartridge_mbc1 && cpu.mbc1_banking_mode != 0)
            {
                bank = static_cast<std::size_t>(cpu.mbc1_bank_high) << mbc1_high_shift;
            }
        }
        else if (cpu.cartridge_mbc5)
        {
            bank = cpu.mbc5_rom_bank;
        }
        else if (cpu.cartridge_mbc7)
        {
            bank = cpu.mbc7_rom_bank;
        }
        else if (cpu.cartridge_huc1)
        {
            bank = cpu.huc1_rom_bank;
        }
        else if (cpu.cartridge_mbc2)
        {
            bank = cpu.mbc2_rom_bank;
        }
        else if (cpu.cartridge_mbc3)
        {
            bank = cpu.mbc3_rom_bank;
        }
        else if (cpu.cartridge_mbc1)
        {
            bank = (static_cast<std::size_t>(cpu.mbc1_bank_high) << mbc1_high_shift) |
                   (cpu.mbc1_rom_bank_low & mbc1_low_mask);
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
        if (cpu.cartridge_mbc2)
        {
            return address & 0x01FF; // 512 addresses repeat across $A000-$BFFF.
        }
        std::size_t bank = cpu.mbc1_banking_mode != 0 ? cpu.mbc1_bank_high : 0;
        if (cpu.cartridge_mbc5)
        {
            bank = cpu.mbc5_ram_bank;
        }
        else if (cpu.cartridge_huc1)
        {
            bank = cpu.huc1_ram_bank;
        }
        else if (cpu.cartridge_mbc3)
        {
            bank = cpu.mbc3_ram_select & 0x03;
        }
        return (bank * 0x2000 + (address - 0xA000)) % cpu.cartridge_ram.size();
    }

    // MBC7 EEPROM: 128 16-bit words, high byte first in cartridge RAM.
    uint16_t mbc7_eeprom_word(const CPU_LR35902 &cpu, uint8_t address)
    {
        const std::size_t index = static_cast<std::size_t>(address & 0x7F) * 2;
        return static_cast<uint16_t>((cpu.cartridge_ram[index] << 8) | cpu.cartridge_ram[index + 1]);
    }

    void set_mbc7_eeprom_word(CPU_LR35902 &cpu, uint8_t address, uint16_t value)
    {
        const std::size_t index = static_cast<std::size_t>(address & 0x7F) * 2;
        cpu.cartridge_ram[index] = static_cast<uint8_t>(value >> 8);
        cpu.cartridge_ram[index + 1] = static_cast<uint8_t>(value);
    }

    enum Mbc7EepromState : uint8_t
    {
        EEPROM_IDLE,     // Waiting for a start bit.
        EEPROM_COMMAND,  // Shifting in the opcode and address.
        EEPROM_READ,     // Shifting words out.
        EEPROM_WRITE,    // Shifting in a word to write.
        EEPROM_WRITE_ALL, // Shifting in a word to write everywhere.
        EEPROM_DONE       // Waiting for chip select to drop.
    };

    // Runs a command once its 2-bit opcode and 8-bit address are shifted in.
    void run_mbc7_eeprom_command(CPU_LR35902 &cpu)
    {
        MBC7_State &mbc7 = cpu.mbc7;
        const uint8_t opcode = static_cast<uint8_t>((mbc7.shift >> 8) & 0x03);
        const uint8_t address = static_cast<uint8_t>(mbc7.shift);
        mbc7.address = address & 0x7F;
        mbc7.shift = 0;
        mbc7.bits = 0;
        mbc7.state = EEPROM_DONE;
        mbc7.data_out = true;
        switch (opcode)
        {
        case 0x02: // READ: a dummy 0, then the word, most significant bit first
            mbc7.shift = mbc7_eeprom_word(cpu, mbc7.address);
            mbc7.data_out = false;
            mbc7.state = EEPROM_READ;
            break;
        case 0x01: // WRITE
            mbc7.state = EEPROM_WRITE;
            break;
        case 0x03: // ERASE
            if (mbc7.write_enabled)
            {
                set_mbc7_eeprom_word(cpu, mbc7.address, 0xFFFF);
            }
            break;
        default:
            switch ((address >> 6) & 0x03) // Opcode 00 takes its command from the address.
            {
            case 0x00: // EWDS
                mbc7.write_enabled = false;
                break;
            case 0x01: // WRAL
                mbc7.state = EEPROM_WRITE_ALL;
                break;
            case 0x02: // ERAL
                if (mbc7.write_enabled)
                {
                    std::fill(cpu.cartridge_ram.begin(), cpu.cartridge_ram.end(), 0xFF);
                }
                break;
            default: // EWEN
                mbc7.write_enabled = true;
                break;
            }
            break;
        }
    }

    // Bit 7 is chip select, bit 6 the clock, and bit 1 data in. Data is
    // sampled and shifted out on rising clock edges while selected.
    void write_mbc7_eeprom(CPU_LR35902 &cpu, uint8_t value)
    {
        MBC7_State &mbc7 = cpu.mbc7;
        const bool cs = (value & 0x80) != 0;
        const bool clk = (value & 0x40) != 0;
        const bool di = (value & 0x02) != 0;
        if (!cs || !mbc7.cs)
        {
            mbc7.state = EEPROM_IDLE; // Selecting or deselecting the chip resets it.
            mbc7.bits = 0;
            mbc7.data_out = true;
        }
        else if (clk && !mbc7.clk)
        {
            switch (mbc7.state)
            {
            case EEPROM_IDLE:
                if (di)
                {
                    mbc7.state = EEPROM_COMMAND;
                    mbc7.shift = 0;
                    mbc7.bits = 0;
                }
                break;
            case EEPROM_COMMAND:
                mbc7.shift = static_cast<uint16_t>((mbc7.shift << 1) | di);
                if (++mbc7.bits == 10)
                {
                    run_mbc7_eeprom_command(cpu);
                }
                break;
            case EEPROM_READ:
                mbc7.data_out = (mbc7.shift & 0x8000) != 0;
                mbc7.shift = static_cast<uint16_t>(mbc7.shift << 1);
                if (++mbc7.bits == 16)
                {
                    // Reads continue into the next word.
                    mbc7.address = static_cast<uint8_t>((mbc7.address + 1) & 0x7F);
                    mbc7.shift = mbc7_eeprom_word(cpu, mbc7.address);
                    mbc7.bits = 0;
                }
                break;
            case EEPROM_WRITE:
            case EEPROM_WRITE_ALL:
                mbc7.shift = static_cast<uint16_t>((mbc7.shift << 1) | di);
                if (++mbc7.bits == 16)
                {
                    if (mbc7.write_enabled)
                    {
                        for (int address = 0; address < 0x80; ++address)
                        {
                            if (mbc7.state == EEPROM_WRITE_ALL || address == mbc7.address)
                            {
                                set_mbc7_eeprom_word(cpu, static_cast<uint8_t>(address), mbc7.shift);
                            }
                        }
                    }
                    mbc7.state = EEPROM_DONE;
                    mbc7.data_out = true; // Ready
                }
                break;
            default:
                break;
            }
        }
        mbc7.cs = cs;
        mbc7.clk = clk;
        mbc7.di = di;
    }

    // $A000-$AFFF, with address bits 4-7 selecting the register.
    uint8_t read_mbc7_register(const CPU_LR35902 &cpu, uint16_t address)
    {
        const MBC7_State &mbc7 = cpu.mbc7;
        if (!cpu.cart_ram_enabled || !mbc7.ram_enabled_2 || address >= 0xB000)
        {
            return 0xFF;
        }
        switch ((address >> 4) & 0x0F)
        {
        case 0x2:
            return static_cast<uint8_t>(mbc7.accel_x);
        case 0x3:
            return static_cast<uint8_t>(mbc7.accel_x >> 8);
        case 0x4:
            return static_cast<uint8_t>(mbc7.accel_y);
        case 0x5:
            return static_cast<uint8_t>(mbc7.accel_y >> 8);
        case 0x6:
            return 0x00;
        case 0x8:
            return static_cast<uint8_t>((mbc7.cs ? 0x80 : 0) | (mbc7.clk ? 0x40 : 0) | (mbc7.di ? 0x02 : 0) |
                                        (mbc7.data_out ? 0x01 : 0));
        default:
            return 0xFF;
        }
    }

    void write_mbc7_register(CPU_LR35902 &cpu, uint16_t address, uint8_t value)
    {
        MBC7_State &mbc7 = cpu.mbc7;
        if (!cpu.cart_ram_enabled || !mbc7.ram_enabled_2 || address >= 0xB000)
        {
            return;
        }
        switch ((address >> 4) & 0x0F)
        {
        case 0x0:
            if (value == 0x55)
            {
                mbc7.accel_x = 0x8000;
                mbc7.accel_y = 0x8000;
                mbc7.latch_reset = true;
            }
            break;
        case 0x1:
            if (value == 0xAA && mbc7.latch_reset)
            {
                // About $70 either side of level is one g of tilt.
                mbc7.accel_x = static_cast<uint16_t>(0x81D0 + cpu.mbc7_tilt_x * 0x70);
                mbc7.accel_y = static_cast<uint16_t>(0x81D0 + cpu.mbc7_tilt_y * 0x70);
                mbc7.latch_reset = false;
            }
            break;
        case 0x8:
            write_mbc7_eeprom(cpu, value);
            break;
        default:
            break;
        }
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
        if (cpu.cartridge_mbc3 && cpu.cart_ram_enabled && cpu.mbc3_ram_select >= 0x08)
        {
            return cpu.cartridge_has_rtc && cpu.mbc3_ram_select <= 0x0C ? read_cartridge_rtc(cpu) : 0xFF;
        }
        if (cpu.cartridge_mbc7)
        {
            return read_mbc7_register(cpu, address);
        }
        if (cpu.cartridge_huc1 && cpu.huc1_ir_mode)
        {
            return 0xC0; // No infrared light received.
        }
        if ((!cpu.cartridge_mbc1 && !cpu.cartridge_mbc2 && !cpu.cartridge_mbc3 && !cpu.cartridge_mbc5 &&
             !cpu.cartridge_huc1) ||
            !cpu.cart_ram_enabled || cpu.cartridge_ram.empty() || (cpu.cartridge_mbc3 && cpu.mbc3_ram_select > 0x03))
        {
            return 0xFF;
        }
        const uint8_t value = cpu.cartridge_ram[cartridge_ram_index(cpu, address)];
        return cpu.cartridge_mbc2 ? static_cast<uint8_t>(value | 0xF0) : value; // MBC2 RAM is 4 bits wide.
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

    if (address >= 0xFF10 && address <= 0xFF3F)
    {
        return apu_read(cpu.apu, address);
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

namespace
{
    uint16_t oam_word(const CPU_LR35902 &cpu, int row, int word)
    {
        const std::size_t address = OAM_START + static_cast<std::size_t>(row * 8 + word * 2);
        return static_cast<uint16_t>(cpu.memory[address] | (cpu.memory[address + 1] << 8));
    }

    void set_oam_word(CPU_LR35902 &cpu, int row, int word, uint16_t value)
    {
        const std::size_t address = OAM_START + static_cast<std::size_t>(row * 8 + word * 2);
        cpu.memory[address] = static_cast<uint8_t>(value);
        cpu.memory[address + 1] = static_cast<uint8_t>(value >> 8);
    }

    void copy_oam_row(CPU_LR35902 &cpu, int from_row, int to_row, int first_byte)
    {
        for (int index = first_byte; index < 8; ++index)
        {
            cpu.memory[OAM_START + to_row * 8 + index] = cpu.memory[OAM_START + from_row * 8 + index];
        }
    }
}

// The corrupted row is the one the PPU is reading, whatever the address. The
// first row is never corrupted.
void cpu_corrupt_oam(CPU_LR35902 &cpu, uint16_t address, OamCorruption corruption)
{
    if (address < 0xFE00 || address > 0xFEFF || !cpu.bus_ticking || cpu.ppu == nullptr)
    {
        return;
    }
    const int row = ppu_oam_scan_row(cpu, *cpu.ppu);
    if (row < 1)
    {
        return;
    }
    const uint16_t a = oam_word(cpu, row, 0);
    const uint16_t b = oam_word(cpu, row - 1, 0);
    const uint16_t c = oam_word(cpu, row - 1, 2);
    switch (corruption)
    {
    case OamCorruption::Write:
        set_oam_word(cpu, row, 0, static_cast<uint16_t>(((a ^ c) & (b ^ c)) ^ c));
        copy_oam_row(cpu, row - 1, row, 2);
        break;
    case OamCorruption::Read:
        set_oam_word(cpu, row, 0, static_cast<uint16_t>(b | (a & c)));
        copy_oam_row(cpu, row - 1, row, 2);
        break;
    case OamCorruption::IncrementRead:
        if (row >= 4 && row < 19)
        {
            const uint16_t two_back = oam_word(cpu, row - 2, 0);
            set_oam_word(cpu, row - 1, 0, static_cast<uint16_t>((b & (two_back | a | c)) | (two_back & a & c)));
            copy_oam_row(cpu, row - 1, row - 2, 0);
        }
        break;
    }
}

uint8_t cpu_read(CPU_LR35902 &cpu, uint16_t address)
{
    cpu_corrupt_oam(cpu, address, OamCorruption::Read);
    if (cpu.bus_ticking)
    {
        tick_peripherals(cpu, 4); // Reads sample the bus at the end of their M-cycle.
    }
    if (is_unmapped_dmg_io(address))
    {
        return 0xFF;
    }
    const bool hram_access = address >= HRAM_START && address <= HRAM_END;
    const bool echo_wram_access = address >= ECHO_START && address <= ECHO_END;
    const bool rom_access_during_vram_dma = address >= 0x0100 && address < 0x8000 &&
                                            cpu.oam_dma_source >= 0x80 && cpu.oam_dma_source <= 0x9F;
    if (cpu.oam_dma_active && !hram_access && !echo_wram_access &&
        !rom_access_during_vram_dma && address != 0xFF46)
    {
        return 0xFF;
    }
    return cpu_read_unrestricted(cpu, address);
}

void update_oam_dma(CPU_LR35902 &cpu, int cycles)
{
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        if (!cpu.oam_dma_active && cpu.oam_dma_start_delay == 0)
        {
            cpu.oam_dma_cycle_phase = 0;
            return;
        }

        cpu.oam_dma_cycle_phase = static_cast<uint8_t>((cpu.oam_dma_cycle_phase + 1) & 0x03);
        if (cpu.oam_dma_cycle_phase != 0)
        {
            continue;
        }

        if (cpu.oam_dma_active)
        {
            const uint16_t source_address = static_cast<uint16_t>(
                (static_cast<uint16_t>(cpu.oam_dma_source) << 8) + cpu.oam_dma_index);
            const uint16_t dma_bus_address = source_address >= 0xFE00
                                                 ? static_cast<uint16_t>(source_address - 0x2000)
                                                 : source_address;
            cpu.memory[OAM_START + cpu.oam_dma_index] = cpu_read_unrestricted(cpu, dma_bus_address);
            ++cpu.oam_dma_index;
            cpu.oam_dma_active = cpu.oam_dma_index < 0xA0;
        }

        if (cpu.oam_dma_start_delay != 0 && --cpu.oam_dma_start_delay == 0)
        {
            cpu.oam_dma_active = true;
            cpu.oam_dma_source = cpu.oam_dma_pending_source;
            cpu.oam_dma_index = 0;
        }
    }
}

namespace
{
    void tick_cpu_side(CPU_LR35902 &cpu, int cycles)
    {
        update_timers(cpu, cycles);
        update_oam_dma(cpu, cycles);
        update_serial(cpu, cycles);
        update_cartridge_rtc(cpu, cycles);
        apu_tick(cpu.apu, cycles);
    }

    void tick_ppu(CPU_LR35902 &cpu, int cycles)
    {
        if (cpu.ppu != nullptr)
        {
            update_ppu(cpu, *cpu.ppu, cycles);
        }
    }
}

void tick_peripherals(CPU_LR35902 &cpu, int cycles)
{
    tick_cpu_side(cpu, cycles);
    tick_ppu(cpu, cycles);
    if (cpu.bus_ticking)
    {
        cpu.bus_cycles += cycles;
    }
}

namespace
{
    void write_memory(CPU_LR35902 &cpu, uint16_t address, uint8_t value)
    {
        if (is_unmapped_dmg_io(address))
        {
            return;
        }
        if (cpu.oam_dma_active && address != 0xFF46 && !(address >= HRAM_START && address <= HRAM_END))
        {
            return;
        }

        if (address == 0xFF46)
        {
            cpu.memory[address] = value;
            cpu.oam_dma_pending_source = value;
            cpu.oam_dma_start_delay = 2;
            return;
        }

        if (address >= 0xFF10 && address <= 0xFF3F)
        {
            apu_write(cpu.apu, address, value);
            return;
        }

        if (address == 0xFF47 && cpu.ppu != nullptr)
        {
            cpu.ppu->bgp_write_pending = true;
            cpu.ppu->bgp_write_blend = static_cast<uint8_t>(cpu.memory[address] | value);
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
            cpu.serial_cycles_remaining = cpu.serial_transfer_active && cpu.serial_internal_clock
                                              ? static_cast<uint16_t>(4096 - (cpu.serial_divider_accumulator & 0x01FF))
                                              : 0;
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

        if (address == 0xFF44)
        {
            return; // LY is read-only
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
                else if (cpu.cartridge_mbc7)
                {
                    if (address < 0x2000)
                    {
                        cpu.cart_ram_enabled = (value & 0x0F) == 0x0A;
                    }
                    else if (address < 0x4000)
                    {
                        cpu.mbc7_rom_bank = value & 0x7F;
                        if (cpu.mbc7_rom_bank == 0)
                        {
                            cpu.mbc7_rom_bank = 1;
                        }
                    }
                    else if (address < 0x6000)
                    {
                        cpu.mbc7.ram_enabled_2 = value == 0x40; // Needed as well as the usual $0A.
                    }
                }
                else if (cpu.cartridge_huc1)
                {
                    if (address < 0x2000)
                    {
                        // $0E switches $A000-$BFFF to the infrared port; anything else to RAM.
                        cpu.huc1_ir_mode = value == 0x0E;
                        cpu.cart_ram_enabled = !cpu.huc1_ir_mode;
                    }
                    else if (address < 0x4000)
                    {
                        cpu.huc1_rom_bank = value & 0x3F;
                        if (cpu.huc1_rom_bank == 0)
                        {
                            cpu.huc1_rom_bank = 1;
                        }
                    }
                    else if (address < 0x6000)
                    {
                        cpu.huc1_ram_bank = value & 0x03;
                    }
                }
                else if (cpu.cartridge_mbc3)
                {
                    if (address < 0x2000)
                    {
                        cpu.cart_ram_enabled = (value & 0x0F) == 0x0A; // Also enables the RTC registers.
                    }
                    else if (address < 0x4000)
                    {
                        cpu.mbc3_rom_bank = value & 0x7F;
                        if (cpu.mbc3_rom_bank == 0)
                        {
                            cpu.mbc3_rom_bank = 1;
                        }
                    }
                    else if (address < 0x6000)
                    {
                        cpu.mbc3_ram_select = value;
                    }
                    else
                    {
                        // Writing $00 then $01 copies the running clock into the
                        // registers that reads return.
                        if (value == 0x01 && cpu.rtc_latch_armed)
                        {
                            cpu.rtc_latched = cpu.rtc_registers;
                        }
                        cpu.rtc_latch_armed = value == 0x00;
                    }
                }
                else if (cpu.cartridge_mbc2)
                {
                    // Address bit 8 selects RAMG or ROMB; $4000-$7FFF is not decoded.
                    if (address < 0x4000 && (address & 0x0100) == 0)
                    {
                        cpu.cart_ram_enabled = (value & 0x0F) == 0x0A;
                    }
                    else if (address < 0x4000)
                    {
                        cpu.mbc2_rom_bank = value & 0x0F;
                        if (cpu.mbc2_rom_bank == 0)
                        {
                            cpu.mbc2_rom_bank = 1;
                        }
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
            if (cpu.cartridge_mbc3 && cpu.cart_ram_enabled && cpu.mbc3_ram_select >= 0x08)
            {
                if (cpu.cartridge_has_rtc && cpu.mbc3_ram_select <= 0x0C)
                {
                    write_cartridge_rtc(cpu, value);
                }
                return;
            }
            if (cpu.cartridge_mbc7)
            {
                write_mbc7_register(cpu, address, value);
                return;
            }
            if (cpu.cartridge_huc1 && cpu.huc1_ir_mode)
            {
                return; // The infrared LED.
            }
            if ((cpu.cartridge_mbc1 || cpu.cartridge_mbc2 || cpu.cartridge_mbc3 || cpu.cartridge_mbc5 ||
                 cpu.cartridge_huc1) &&
                cpu.cart_ram_enabled && !cpu.cartridge_ram.empty() &&
                !(cpu.cartridge_mbc3 && cpu.mbc3_ram_select > 0x03))
            {
                cpu.cartridge_ram[cartridge_ram_index(cpu, address)] =
                    cpu.cartridge_mbc2 ? static_cast<uint8_t>(value & 0x0F) : value;
            }
            return;
        }

        if (address >= VRAM_START && address <= VRAM_END && cpu.ppu != nullptr &&
            ppu_blocks_vram_write(*cpu.ppu))
        {
            return;
        }

        if (address >= OAM_START && address <= OAM_END && cpu.ppu != nullptr &&
            ppu_blocks_oam_write(*cpu.ppu))
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
}

void cpu_write(CPU_LR35902 &cpu, uint16_t address, uint8_t value)
{
    cpu_corrupt_oam(cpu, address, OamCorruption::Write);
    if (cpu.bus_ticking)
    {
        // The timer, DMA, and serial clocks advance before a write lands, while
        // the PPU sees it at the start of the M-cycle.
        tick_cpu_side(cpu, 4);
        write_memory(cpu, address, value);
        tick_ppu(cpu, 4);
        cpu.bus_cycles += 4;
        return;
    }
    write_memory(cpu, address, value);
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
