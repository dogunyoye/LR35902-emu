#include "SaveState.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <type_traits>

namespace
{
    constexpr char MAGIC[8] = {'L', 'R', '3', '5', '9', '0', '2', 'S'};
    // Bump whenever the saved fields or the PPU layout change.
    constexpr uint32_t VERSION = 6;

    static_assert(std::is_trivially_copyable_v<PPU_LR35902>, "PPU state is saved as raw bytes");
    static_assert(std::is_trivially_copyable_v<APU_LR35902::Channel>, "APU channels are saved as raw bytes");
    static_assert(std::is_trivially_copyable_v<MBC7_State>, "MBC7 state is saved as raw bytes");

    class StateWriter
    {
    public:
        std::vector<uint8_t> data;

        template <typename T>
        void operator()(T &value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
            data.insert(data.end(), bytes, bytes + sizeof(T));
        }

        void bytes(std::vector<uint8_t> &values)
        {
            uint32_t size = static_cast<uint32_t>(values.size());
            (*this)(size);
            data.insert(data.end(), values.begin(), values.end());
        }
    };

    class StateReader
    {
    public:
        explicit StateReader(const std::vector<uint8_t> &data) : data_(data) {}

        bool ok = true;

        template <typename T>
        void operator()(T &value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (!ok || data_.size() - position_ < sizeof(T))
            {
                ok = false;
                return;
            }
            std::memcpy(&value, data_.data() + position_, sizeof(T));
            position_ += sizeof(T);
        }

        // The size is fixed by the cartridge, so it must match.
        void bytes(std::vector<uint8_t> &values)
        {
            uint32_t size = 0;
            (*this)(size);
            if (!ok || size != values.size() || data_.size() - position_ < size)
            {
                ok = false;
                return;
            }
            if (size > 0)
            {
                std::memcpy(values.data(), data_.data() + position_, size);
            }
            position_ += size;
        }

        bool at_end() const { return position_ == data_.size(); }

    private:
        const std::vector<uint8_t> &data_;
        std::size_t position_ = 0;
    };

    // FNV-1a, to tell whether a state belongs to the loaded ROM.
    uint64_t rom_hash(const CPU_LR35902 &cpu)
    {
        uint64_t hash = 0xCBF29CE484222325ull;
        const auto mix = [&hash](uint8_t byte) {
            hash ^= byte;
            hash *= 0x100000001B3ull;
        };
        if (cpu.cartridge_rom.empty())
        {
            for (std::size_t address = 0; address < 0x8000; ++address)
            {
                mix(cpu.memory[address]);
            }
        }
        for (const uint8_t byte : cpu.cartridge_rom)
        {
            mix(byte);
        }
        return hash;
    }

    // Host-side settings (sample output and rate) and the pending samples are
    // not part of the machine state.
    template <typename Archive>
    void transfer_apu(Archive &archive, APU_LR35902 &apu)
    {
        archive(apu.channels);
        archive(apu.cgb);
        archive(apu.registers);
        archive(apu.wave_ram);
        archive(apu.powered);
        archive(apu.frame_step);
        archive(apu.sweep_shadow);
        archive(apu.sweep_timer);
        archive(apu.sweep_enabled);
        archive(apu.sweep_negated);
        archive(apu.sample_accumulator_left);
        archive(apu.sample_accumulator_right);
        archive(apu.sample_cycles);
        archive(apu.sample_phase);
        archive(apu.highpass_left);
        archive(apu.highpass_right);
    }

    // Keep in step with CPU_LR35902. The ROM itself is not saved, and the PPU
    // pointer, bus bookkeeping, serial test output, and the buttons and tilt
    // currently held are left alone.
    template <typename Archive>
    void transfer_cpu(Archive &archive, CPU_LR35902 &cpu)
    {
        archive(cpu.af.reg16);
        archive(cpu.bc.reg16);
        archive(cpu.de.reg16);
        archive(cpu.hl.reg16);
        archive(cpu.sp);
        archive(cpu.pc);
        archive(cpu.halted);
        archive(cpu.stopped);
        archive(cpu.ime);
        archive(cpu.halt_bug);
        archive(cpu.ime_enable_delay);
        archive(cpu.div_cycle_accumulator);
        archive(cpu.tima_reload_delay);
        archive(cpu.tima_reloaded_this_cycle);
        archive(cpu.oam_dma_active);
        archive(cpu.oam_dma_source);
        archive(cpu.oam_dma_index);
        archive(cpu.oam_dma_cycle_phase);
        archive(cpu.oam_dma_start_delay);
        archive(cpu.oam_dma_pending_source);
        transfer_apu(archive, cpu.apu);
        archive(cpu.serial_cycles_remaining);
        archive(cpu.serial_shift_register);
        archive(cpu.serial_transfer_active);
        archive(cpu.serial_internal_clock);
        archive(cpu.serial_divider_accumulator);
        archive(cpu.cart_ram_enabled);
        archive(cpu.cartridge_mbc1);
        archive(cpu.cartridge_mbc1_multicart);
        archive(cpu.cartridge_mbc2);
        archive(cpu.cartridge_mbc3);
        archive(cpu.cartridge_has_rtc);
        archive(cpu.cartridge_mbc5);
        archive(cpu.cartridge_mbc7);
        archive(cpu.cartridge_huc1);
        archive(cpu.cartridge_battery_backed);
        archive(cpu.mbc1_rom_bank_low);
        archive(cpu.mbc1_bank_high);
        archive(cpu.mbc1_banking_mode);
        archive(cpu.mbc2_rom_bank);
        archive(cpu.mbc3_rom_bank);
        archive(cpu.mbc3_ram_select);
        archive(cpu.rtc_registers);
        archive(cpu.rtc_latched);
        archive(cpu.rtc_latch_armed);
        archive(cpu.rtc_cycles);
        archive(cpu.mbc5_rom_bank);
        archive(cpu.mbc5_ram_bank);
        archive(cpu.mbc7_rom_bank);
        archive(cpu.mbc7);
        archive(cpu.huc1_rom_bank);
        archive(cpu.huc1_ram_bank);
        archive(cpu.huc1_ir_mode);
        archive(cpu.vram_locked);
        archive(cpu.oam_locked);
        archive.bytes(cpu.cartridge_ram);
        archive(cpu.memory);
        archive(cpu.cgb_hardware);
        archive(cpu.cgb_mode);
        archive(cpu.double_speed);
        archive(cpu.speed_switch_armed);
        archive(cpu.vram_bank);
        archive(cpu.wram_bank);
        archive(cpu.vram_bank1);
        archive(cpu.wram_banks);
        archive(cpu.bg_palette_ram);
        archive(cpu.obj_palette_ram);
        archive(cpu.bg_palette_index);
        archive(cpu.obj_palette_index);
        archive(cpu.object_priority_mode);
        archive(cpu.infrared);
        archive(cpu.undocumented_registers);
        archive(cpu.hdma_source);
        archive(cpu.hdma_destination);
        archive(cpu.hdma_blocks);
        archive(cpu.hdma_hblank_active);
        archive(cpu.hdma_stall_cycles);
    }
}

std::vector<uint8_t> serialize_state(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu)
{
    StateWriter writer;
    char magic[sizeof(MAGIC)];
    std::memcpy(magic, MAGIC, sizeof(MAGIC));
    writer(magic);
    uint32_t version = VERSION;
    writer(version);
    uint64_t hash = rom_hash(cpu);
    writer(hash);
    uint32_t ppu_size = sizeof(PPU_LR35902);
    writer(ppu_size);
    // The writer only reads, so the casts leave the machine unchanged.
    transfer_cpu(writer, const_cast<CPU_LR35902 &>(cpu));
    writer(const_cast<PPU_LR35902 &>(ppu));
    return std::move(writer.data);
}

bool deserialize_state(CPU_LR35902 &cpu, PPU_LR35902 &ppu, const std::vector<uint8_t> &state, std::string &error)
{
    StateReader reader(state);
    char magic[sizeof(MAGIC)] = {};
    uint32_t version = 0;
    uint64_t hash = 0;
    uint32_t ppu_size = 0;
    reader(magic);
    reader(version);
    reader(hash);
    reader(ppu_size);
    if (!reader.ok || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
    {
        error = "not a save state.";
        return false;
    }
    if (version != VERSION || ppu_size != sizeof(PPU_LR35902))
    {
        error = "the save state is from a different version of the emulator.";
        return false;
    }
    if (hash != rom_hash(cpu))
    {
        error = "the save state is for a different ROM.";
        return false;
    }

    // Decode into a copy so a damaged state leaves the machine as it was. The
    // ROM is moved aside rather than copied.
    std::vector<uint8_t> rom = std::move(cpu.cartridge_rom);
    CPU_LR35902 loaded_cpu = cpu;
    PPU_LR35902 loaded_ppu = ppu;
    transfer_cpu(reader, loaded_cpu);
    reader(loaded_ppu);
    if (!reader.ok || !reader.at_end())
    {
        cpu.cartridge_rom = std::move(rom);
        error = "the save state is damaged.";
        return false;
    }

    loaded_cpu.cartridge_rom = std::move(rom);
    loaded_cpu.bus_ticking = false;
    loaded_cpu.bus_cycles = 0;
    loaded_cpu.apu.samples.clear();
    cpu = std::move(loaded_cpu);
    ppu = loaded_ppu;
    return true;
}

bool save_state_file(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu, const std::filesystem::path &path,
                     std::string &error)
{
    const std::vector<uint8_t> state = serialize_state(cpu, ppu);
    // Write beside the target and rename, so a failed save keeps the old state.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(state.data()), static_cast<std::streamsize>(state.size()));
        if (!file)
        {
            error = "could not write " + temporary.string() + ".";
            return false;
        }
    }
    std::error_code rename_error;
    std::filesystem::rename(temporary, path, rename_error);
    if (rename_error)
    {
        error = "could not replace " + path.string() + ": " + rename_error.message();
        return false;
    }
    return true;
}

bool load_state_file(CPU_LR35902 &cpu, PPU_LR35902 &ppu, const std::filesystem::path &path, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "no save state at " + path.string() + ".";
        return false;
    }
    const std::vector<uint8_t> state((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return deserialize_state(cpu, ppu, state, error);
}
