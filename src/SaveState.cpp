#include "SaveState.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <type_traits>

namespace
{
    constexpr char MAGIC[8] = {'L', 'R', '3', '5', '9', '0', '2', 'S'};
    // Bump whenever the saved fields change.
    constexpr uint32_t VERSION = 8;

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
            if constexpr (std::is_same_v<T, bool>)
            {
                // Any byte but 0 or 1 is not a bool.
                const uint8_t byte = data_[position_];
                ok = byte <= 1;
                value = byte == 1;
            }
            else
            {
                std::memcpy(&value, data_.data() + position_, sizeof(T));
            }
            position_ += sizeof(T);
        }

        template <std::size_t N>
        void operator()(std::array<bool, N> &values)
        {
            for (bool &value : values)
            {
                (*this)(value);
            }
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
        for (APU_LR35902::Channel &channel : apu.channels)
        {
            archive(channel.enabled);
            archive(channel.enabled_before_trigger);
            archive(channel.dac_enabled);
            archive(channel.length_enabled);
            archive(channel.length);
            archive(channel.timer);
            archive(channel.frequency);
            archive(channel.duty);
            archive(channel.duty_step);
            archive(channel.volume);
            archive(channel.envelope_timer);
            archive(channel.envelope_running);
            archive(channel.wave_position);
            archive(channel.wave_sample);
            archive(channel.wave_read_cycles);
            archive(channel.lfsr);
        }
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
        archive(cpu.mbc7.ram_enabled_2);
        archive(cpu.mbc7.accel_x);
        archive(cpu.mbc7.accel_y);
        archive(cpu.mbc7.latch_reset);
        archive(cpu.mbc7.cs);
        archive(cpu.mbc7.clk);
        archive(cpu.mbc7.di);
        archive(cpu.mbc7.data_out);
        archive(cpu.mbc7.write_enabled);
        archive(cpu.mbc7.state);
        archive(cpu.mbc7.address);
        archive(cpu.mbc7.shift);
        archive(cpu.mbc7.bits);
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
        archive(cpu.speed_switch_cycles);
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

    // Keep in step with PPU_LR35902.
    template <typename Archive>
    void transfer_ppu(Archive &archive, PPU_LR35902 &ppu)
    {
        archive(ppu.cycles_into_line);
        archive(ppu.mode3_end);
        archive(ppu.ly_for_comparison);
        archive(ppu.window_line);
        archive(ppu.window_y_triggered);
        for (PPU_LR35902::LineObject &object : ppu.line_objects)
        {
            archive(object.oam_index);
            archive(object.x);
            archive(object.y);
            archive(object.fetched);
        }
        archive(ppu.line_object_count);
        archive(ppu.fetch_step);
        archive(ppu.fetch_tile_x);
        archive(ppu.fetch_window);
        archive(ppu.fetch_first_tile);
        archive(ppu.fetch_tile_id);
        archive(ppu.fetch_attributes);
        archive(ppu.fetch_low);
        archive(ppu.fetch_high);
        archive(ppu.background_low);
        archive(ppu.background_high);
        archive(ppu.background_attributes);
        archive(ppu.background_count);
        archive(ppu.object_colors);
        archive(ppu.object_attributes);
        archive(ppu.object_oam_indices);
        archive(ppu.pixel_x);
        archive(ppu.invisible_pixels);
        archive(ppu.discard_pixels);
        for (PPU_LR35902::PendingPixel &pixel : ppu.pending_pixels)
        {
            archive(pixel.delay);
            archive(pixel.x);
            archive(pixel.background_color);
            archive(pixel.object_color);
            archive(pixel.object_attributes);
            archive(pixel.background_attributes);
        }
        archive(ppu.pending_count);
        archive(ppu.first_pixel_delayed);
        archive(ppu.drawing);
        archive(ppu.bgp_write_pending);
        archive(ppu.bgp_write_blend);
        archive(ppu.resolve_lcdc);
        archive(ppu.stall_dots);
        archive(ppu.stalled_object);
        archive(ppu.window_active);
        archive(ppu.window_drawn);
        archive(ppu.window_index_late);
        archive(ppu.window_start_x);
        archive(ppu.penalised_tiles);
        archive(ppu.mode);
        archive(ppu.lcd_enabled);
        archive(ppu.has_started);
        archive(ppu.startup_mode0);
        archive(ppu.line_start_hold);
        archive(ppu.on_line_153);
        archive(ppu.framebuffer_initialized);
        archive(ppu.stat_interrupt_line);
        archive(ppu.framebuffer);
    }

    // Values the emulator uses as indices or sizes, which a damaged state could
    // otherwise push out of range. The cartridge type comes from the ROM, so it
    // must match the one loaded.
    bool state_in_range(const CPU_LR35902 &cpu, const PPU_LR35902 &ppu, const CPU_LR35902 &current)
    {
        const bool same_cartridge =
            cpu.cartridge_mbc1 == current.cartridge_mbc1 &&
            cpu.cartridge_mbc1_multicart == current.cartridge_mbc1_multicart &&
            cpu.cartridge_mbc2 == current.cartridge_mbc2 && cpu.cartridge_mbc3 == current.cartridge_mbc3 &&
            cpu.cartridge_has_rtc == current.cartridge_has_rtc && cpu.cartridge_mbc5 == current.cartridge_mbc5 &&
            cpu.cartridge_mbc7 == current.cartridge_mbc7 && cpu.cartridge_huc1 == current.cartridge_huc1 &&
            cpu.cartridge_battery_backed == current.cartridge_battery_backed;
        if (!same_cartridge || (cpu.cgb_mode && !cpu.cgb_hardware) || cpu.vram_bank > 1 || cpu.wram_bank > 7 ||
            cpu.oam_dma_index > 0xA0 || cpu.memory[0xFF44] > 153)
        {
            return false;
        }
        for (const APU_LR35902::Channel &channel : cpu.apu.channels)
        {
            if (channel.duty > 3 || channel.duty_step > 7 || channel.wave_position > 0x1F)
            {
                return false;
            }
        }

        if (ppu.cycles_into_line < 0 || ppu.cycles_into_line >= 456 || ppu.line_object_count < 0 ||
            ppu.line_object_count > static_cast<int>(ppu.line_objects.size()) || ppu.pending_count < 0 ||
            ppu.pending_count >= static_cast<int>(ppu.pending_pixels.size()) || ppu.pixel_x < 0 ||
            ppu.pixel_x > PPU_LR35902::WIDTH || ppu.stalled_object < -1 ||
            ppu.stalled_object >= ppu.line_object_count || (ppu.stall_dots > 0 && ppu.stalled_object < 0) ||
            (ppu.drawing && cpu.memory[0xFF44] >= PPU_LR35902::HEIGHT))
        {
            return false;
        }
        for (int index = 0; index < ppu.line_object_count; ++index)
        {
            // Objects are fetched on the line the OAM scan found them on.
            const PPU_LR35902::LineObject &object = ppu.line_objects[static_cast<std::size_t>(index)];
            const int row = cpu.memory[0xFF44] - (object.y - 16);
            if (object.oam_index >= 40 || (ppu.drawing && (row < 0 || row >= 16)))
            {
                return false;
            }
        }
        for (const std::uint8_t color : ppu.object_colors)
        {
            if (color > 3)
            {
                return false;
            }
        }
        for (int index = 0; index < ppu.pending_count; ++index)
        {
            const PPU_LR35902::PendingPixel &pixel = ppu.pending_pixels[static_cast<std::size_t>(index)];
            if (pixel.x >= PPU_LR35902::WIDTH || pixel.background_color > 3 || pixel.object_color > 3)
            {
                return false;
            }
        }
        return true;
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
    // The writer only reads, so the casts leave the machine unchanged.
    transfer_cpu(writer, const_cast<CPU_LR35902 &>(cpu));
    transfer_ppu(writer, const_cast<PPU_LR35902 &>(ppu));
    return std::move(writer.data);
}

bool deserialize_state(CPU_LR35902 &cpu, PPU_LR35902 &ppu, const std::vector<uint8_t> &state, std::string &error)
{
    StateReader reader(state);
    char magic[sizeof(MAGIC)] = {};
    uint32_t version = 0;
    uint64_t hash = 0;
    reader(magic);
    reader(version);
    reader(hash);
    if (!reader.ok || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
    {
        error = "not a save state.";
        return false;
    }
    if (version != VERSION)
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
    transfer_ppu(reader, loaded_ppu);
    if (!reader.ok || !reader.at_end() || !state_in_range(loaded_cpu, loaded_ppu, cpu))
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
        file.close(); // Flushes, so a failed final write is caught here.
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
