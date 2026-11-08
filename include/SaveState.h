#pragma once

#include "LR35902.h"
#include "PPU.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Save states hold the whole emulated machine: CPU, memory, cartridge RAM and
// banking, timers, PPU, and APU. A state only loads with the same save-state
// format version and the same ROM.
std::vector<uint8_t> serialize_state(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu);
// Leaves the machine unchanged if the state cannot be loaded.
bool deserialize_state(CPU_LR35902 &cpu, PPU_LR35902 &ppu, const std::vector<uint8_t> &state, std::string &error);

bool save_state_file(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu, const std::filesystem::path &path,
                     std::string &error);
bool load_state_file(CPU_LR35902 &cpu, PPU_LR35902 &ppu, const std::filesystem::path &path, std::string &error);
