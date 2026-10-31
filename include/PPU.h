#pragma once

#include "LR35902.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct PPU_LR35902
{
    static constexpr int WIDTH = 160;
    static constexpr int HEIGHT = 144;
    static constexpr std::size_t PIXEL_COUNT = static_cast<std::size_t>(WIDTH) * HEIGHT;

    std::array<std::uint32_t, PIXEL_COUNT> framebuffer{};
    int cycles_into_line = 0;
    std::uint8_t mode = 0;
    bool lcd_enabled = false;
    bool framebuffer_initialized = false;
    bool stat_interrupt_line = false;
};

void update_ppu(CPU_LR35902 &cpu, PPU_LR35902 &ppu, int cycles);