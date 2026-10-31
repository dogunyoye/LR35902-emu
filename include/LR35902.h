#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
    bool ime; // Interrupt Master Enable flag
    bool halt_bug = false;
    uint8_t ime_enable_delay = 0;
    uint16_t div_cycle_accumulator = 0;
    uint16_t tima_cycle_accumulator = 0;
    uint8_t joypad_buttons = 0xFF;

    // Memory routing state.
    bool cart_ram_enabled = false;
    bool cartridge_mbc1 = false;
    uint8_t mbc1_rom_bank_low = 1;
    uint8_t mbc1_bank_high = 0;
    uint8_t mbc1_banking_mode = 0;
    bool vram_locked = false;
    bool oam_locked = false;
    std::vector<uint8_t> cartridge_rom;
    std::vector<uint8_t> cartridge_ram;

    // 64KB Flat Address Space (will be replaced later with custom read/write routers)
    std::array<uint8_t, 65536> memory;
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
bool load_rom_only_cartridge(CPU_LR35902 &cpu, std::vector<uint8_t> rom, std::string &error);
bool load_cartridge(CPU_LR35902 &cpu, std::vector<uint8_t> rom, std::string &error);