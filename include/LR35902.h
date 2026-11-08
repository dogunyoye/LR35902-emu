#pragma once
#include "APU.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct PPU_LR35902;

// MBC7 cartridge peripherals: a two-axis accelerometer and a 93LC56 serial
// EEPROM (128 16-bit words, stored in cartridge_ram).
struct MBC7_State
{
    bool ram_enabled_2 = false;
    // Accelerometer readings, latched by writing $55 then $AA. $81D0 is level.
    uint16_t accel_x = 0x8000;
    uint16_t accel_y = 0x8000;
    bool latch_reset = false;
    // EEPROM pins as last written, and the command being shifted in or out.
    bool cs = false;
    bool clk = false;
    bool di = false;
    bool data_out = true;
    bool write_enabled = false;
    uint8_t state = 0;
    uint8_t address = 0;
    uint16_t shift = 0;
    int bits = 0;
};

union RegPair
{
    uint16_t reg16;
    struct
    {
        uint8_t low;  // e.g., F (Flags) or C
        uint8_t high; // e.g., A (Accumulator) or B
    } reg8;
};

struct CPU_LR35902
{
    RegPair af;
    RegPair bc;
    RegPair de;
    RegPair hl;

    uint16_t sp; // Stack Pointer
    uint16_t pc; // Program Counter

    bool halted;
    bool stopped = false;
    bool ime; // Interrupt Master Enable flag
    bool halt_bug = false;
    uint8_t ime_enable_delay = 0;
    uint16_t div_cycle_accumulator = 0;
    uint8_t tima_reload_delay = 0;
    bool tima_reloaded_this_cycle = false;
    // OAM DMA copies one byte per M-cycle while active. A write to $FF46
    // starts (or restarts) a transfer two M-cycles later.
    bool oam_dma_active = false;
    uint8_t oam_dma_source = 0;
    uint16_t oam_dma_index = 0;
    uint8_t oam_dma_cycle_phase = 0;
    uint8_t oam_dma_start_delay = 0;
    uint8_t oam_dma_pending_source = 0;
    // While an instruction executes, each memory access advances the
    // peripherals by one M-cycle; bus_cycles counts the T-cycles advanced.
    bool bus_ticking = false;
    int bus_cycles = 0;
    // PPU advanced alongside the CPU, if any.
    PPU_LR35902 *ppu = nullptr;
    APU_LR35902 apu;
    uint16_t serial_cycles_remaining = 0;
    uint8_t joypad_buttons = 0xFF;
    uint8_t serial_shift_register = 0xFF;
    bool serial_transfer_active = false;
    bool serial_internal_clock = false;
    uint16_t serial_divider_accumulator = 0;

    // Memory routing state.
    bool cart_ram_enabled = false;
    bool cartridge_mbc1 = false;
    bool cartridge_mbc1_multicart = false;
    bool cartridge_mbc2 = false;
    bool cartridge_mbc3 = false;
    bool cartridge_has_rtc = false;
    bool cartridge_mbc5 = false;
    bool cartridge_mbc7 = false;
    bool cartridge_huc1 = false;
    bool cartridge_battery_backed = false;
    uint8_t mbc1_rom_bank_low = 1;
    uint8_t mbc1_bank_high = 0;
    uint8_t mbc1_banking_mode = 0;
    uint8_t mbc2_rom_bank = 1;
    uint8_t mbc3_rom_bank = 1;
    // $00-$03 selects a RAM bank, $08-$0C an RTC register.
    uint8_t mbc3_ram_select = 0;
    // MBC3 real-time clock: seconds, minutes, hours, day low, day high
    // (bit 0 day bit 8, bit 6 halt, bit 7 day carry), the copy latched for
    // reading, and the T-cycles counted toward the next second.
    std::array<uint8_t, 5> rtc_registers{};
    std::array<uint8_t, 5> rtc_latched{};
    bool rtc_latch_armed = false;
    uint32_t rtc_cycles = 0;
    uint16_t mbc5_rom_bank = 1;
    uint8_t mbc5_ram_bank = 0;
    uint8_t mbc7_rom_bank = 1;
    MBC7_State mbc7{};
    // Tilt from the host's controls, -1 to 1 on each axis; not machine state.
    int mbc7_tilt_x = 0;
    int mbc7_tilt_y = 0;
    // HuC1: ROM and RAM banks, and whether $A000-$BFFF is the infrared port.
    uint8_t huc1_rom_bank = 1;
    uint8_t huc1_ram_bank = 0;
    bool huc1_ir_mode = false;
    bool vram_locked = false;
    bool oam_locked = false;
    std::vector<uint8_t> cartridge_rom;
    std::vector<uint8_t> cartridge_ram;
    std::string serial_output;

    // Backing store for VRAM, WRAM, OAM, I/O registers, HRAM, and IE (and ROM when
    // no cartridge is loaded). Access goes through cpu_read/cpu_write, which handle
    // cartridge banking, echo RAM, register masks, and PPU/DMA lockouts.
    std::array<uint8_t, 65536> memory;
};

enum class GameBoyModel : uint8_t
{
    Dmg0,
    DmgABC,
    Mgb,
    Sgb,
    Sgb2
};

enum class JoypadButton : uint8_t
{
    Right = 0,
    Left,
    Up,
    Down,
    A,
    B,
    Select,
    Start
};

// Flag Bit Masks
const uint8_t FLAG_Z = 0x80;
const uint8_t FLAG_N = 0x40;
const uint8_t FLAG_H = 0x20;
const uint8_t FLAG_C = 0x10;

// Inline Helpers
inline void set_flag(CPU_LR35902 &cpu, uint8_t flag, bool value)
{
    if (value)
    {
        cpu.af.reg8.low |= flag;
    }
    else
    {
        cpu.af.reg8.low &= ~flag;
    }
}

inline bool get_flag(const CPU_LR35902 &cpu, uint8_t flag)
{
    return (cpu.af.reg8.low & flag) != 0;
}

int execute_instruction(CPU_LR35902 &cpu);
int step_cpu(CPU_LR35902 &cpu);
uint8_t cpu_read(CPU_LR35902 &cpu, uint16_t address);
void cpu_write(CPU_LR35902 &cpu, uint16_t address, uint8_t value);
void cpu_set_joypad_button(CPU_LR35902 &cpu, JoypadButton button, bool pressed);
uint16_t cpu_read16(CPU_LR35902 &cpu, uint16_t address);
void cpu_push16(CPU_LR35902 &cpu, uint16_t val);
uint16_t cpu_pop16(CPU_LR35902 &cpu);
void update_timers(CPU_LR35902 &cpu, int cycles);
void update_oam_dma(CPU_LR35902 &cpu, int cycles);
void tick_peripherals(CPU_LR35902 &cpu, int cycles);
// The DMG corrupts OAM when the CPU puts an address in $FE00-$FEFF on the bus
// while the PPU is scanning OAM. IncrementRead is the extra corruption of a
// read that also increments the address (POP, LD A,(HL+), LD A,(HL-)); the
// read itself then causes a Read corruption.
enum class OamCorruption : uint8_t
{
    Write,
    Read,
    IncrementRead
};
void cpu_corrupt_oam(CPU_LR35902 &cpu, uint16_t address, OamCorruption corruption);
void reset_divider(CPU_LR35902 &cpu);
void write_timer_control(CPU_LR35902 &cpu, uint8_t value);
void update_serial(CPU_LR35902 &cpu, int cycles);
void update_cartridge_rtc(CPU_LR35902 &cpu, int cycles);
void advance_cartridge_rtc_seconds(CPU_LR35902 &cpu, uint64_t seconds);
uint8_t read_cartridge_rtc(const CPU_LR35902 &cpu);
void write_cartridge_rtc(CPU_LR35902 &cpu, uint8_t value);
bool load_cartridge(CPU_LR35902 &cpu, std::vector<uint8_t> rom, std::string &error,
                    GameBoyModel model = GameBoyModel::DmgABC);
bool load_cartridge_ram(CPU_LR35902 &cpu, const std::filesystem::path &path, std::string &error);
bool save_cartridge_ram(const CPU_LR35902 &cpu, const std::filesystem::path &path, std::string &error);
