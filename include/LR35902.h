#pragma once
#include <cstdint>
#include <array>

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

    // 64KB Flat Address Space (will be replaced later with custom read/write routers)
    std::array<uint8_t, 65536> memory;
};