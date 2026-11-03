#include "APU.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    constexpr uint16_t NR10 = 0xFF10;
    constexpr uint16_t NR52 = 0xFF26;
    constexpr uint16_t WAVE_RAM_START = 0xFF30;
    constexpr double NOMINAL_CYCLES_PER_SAMPLE = 4194304.0 / APU_LR35902::SAMPLE_RATE;
    // Keeps at most a second of audio if nobody drains the samples.
    constexpr std::size_t MAX_BUFFERED_SAMPLES = APU_LR35902::SAMPLE_RATE * 2;

    // Bits that read back as 1 for $FF10-$FF2F.
    constexpr std::array<uint8_t, 0x20> READ_MASKS = {
        0x80, 0x3F, 0x00, 0xFF, 0xBF, // NR10-NR14
        0xFF, 0x3F, 0x00, 0xFF, 0xBF, // unused, NR21-NR24
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF, // NR30-NR34
        0xFF, 0xFF, 0x00, 0x00, 0xBF, // unused, NR41-NR44
        0x00, 0x00, 0x70,             // NR50-NR52
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    constexpr std::array<uint8_t, 4> DUTY_PATTERNS = {0x01, 0x81, 0x87, 0x7E};
    // Retriggering channel 3 within this many T-cycles of a sample read
    // corrupts wave RAM on the DMG.
    constexpr int WAVE_TRIGGER_CORRUPTION_WINDOW = 2;

    uint8_t &reg(APU_LR35902 &apu, uint16_t address)
    {
        return apu.registers[static_cast<std::size_t>(address - NR10)];
    }

    uint8_t reg(const APU_LR35902 &apu, uint16_t address)
    {
        return apu.registers[static_cast<std::size_t>(address - NR10)];
    }

    // Base address of a channel's five registers (NRx0-NRx4).
    uint16_t channel_base(int channel)
    {
        return static_cast<uint16_t>(NR10 + channel * 5);
    }

    int max_length(int channel)
    {
        return channel == 2 ? 256 : 64;
    }

    // The frame sequencer clocks length counters on even steps; frame_step is
    // the next step to run.
    bool next_step_clocks_length(const APU_LR35902 &apu)
    {
        return (apu.frame_step & 0x01) == 0;
    }

    int square_period(const APU_LR35902::Channel &channel)
    {
        return (2048 - channel.frequency) * 4;
    }

    int wave_period(const APU_LR35902::Channel &channel)
    {
        return (2048 - channel.frequency) * 2;
    }

    int noise_period(const APU_LR35902 &apu)
    {
        const uint8_t nr43 = reg(apu, 0xFF22);
        const int divisor = (nr43 & 0x07) == 0 ? 8 : (nr43 & 0x07) * 16;
        return divisor << (nr43 >> 4);
    }

    int sweep_period(const APU_LR35902 &apu)
    {
        const int period = (reg(apu, NR10) >> 4) & 0x07;
        return period == 0 ? 8 : period;
    }

    // Computes the next swept frequency, disabling channel 1 on overflow.
    uint16_t calculate_sweep(APU_LR35902 &apu)
    {
        const uint8_t nr10 = reg(apu, NR10);
        const uint16_t delta = static_cast<uint16_t>(apu.sweep_shadow >> (nr10 & 0x07));
        uint16_t frequency;
        if ((nr10 & 0x08) != 0)
        {
            apu.sweep_negated = true;
            frequency = static_cast<uint16_t>(apu.sweep_shadow - delta);
        }
        else
        {
            frequency = static_cast<uint16_t>(apu.sweep_shadow + delta);
        }
        if (frequency > 2047)
        {
            apu.channels[0].enabled = false;
        }
        return frequency;
    }

    void clock_lengths(APU_LR35902 &apu)
    {
        for (APU_LR35902::Channel &channel : apu.channels)
        {
            if (channel.length_enabled && channel.length > 0 && --channel.length == 0)
            {
                channel.enabled = false;
            }
        }
    }

    void clock_sweep(APU_LR35902 &apu)
    {
        if (--apu.sweep_timer > 0)
        {
            return;
        }
        apu.sweep_timer = sweep_period(apu);
        const uint8_t nr10 = reg(apu, NR10);
        if (!apu.sweep_enabled || ((nr10 >> 4) & 0x07) == 0)
        {
            return;
        }
        const uint16_t frequency = calculate_sweep(apu);
        if (frequency <= 2047 && (nr10 & 0x07) != 0)
        {
            apu.sweep_shadow = frequency;
            apu.channels[0].frequency = frequency;
            reg(apu, 0xFF13) = static_cast<uint8_t>(frequency);
            reg(apu, 0xFF14) = static_cast<uint8_t>((reg(apu, 0xFF14) & 0xF8) | (frequency >> 8));
            calculate_sweep(apu); // A second calculation only checks for overflow.
        }
    }

    void clock_envelopes(APU_LR35902 &apu)
    {
        for (const int index : {0, 1, 3})
        {
            APU_LR35902::Channel &channel = apu.channels[static_cast<std::size_t>(index)];
            const uint8_t envelope = reg(apu, static_cast<uint16_t>(channel_base(index) + 2));
            const int period = envelope & 0x07;
            if (period == 0 || !channel.envelope_running || --channel.envelope_timer > 0)
            {
                continue;
            }
            channel.envelope_timer = period;
            if ((envelope & 0x08) != 0 && channel.volume < 15)
            {
                ++channel.volume;
            }
            else if ((envelope & 0x08) == 0 && channel.volume > 0)
            {
                --channel.volume;
            }
            else
            {
                channel.envelope_running = false;
            }
        }
    }

    void trigger(APU_LR35902 &apu, int index)
    {
        APU_LR35902::Channel &channel = apu.channels[static_cast<std::size_t>(index)];
        const uint16_t base = channel_base(index);
        channel.enabled_before_trigger = channel.enabled;
        channel.enabled = true;
        if (channel.length == 0)
        {
            // Reloading a zero length while the next frame sequencer step will
            // not clock lengths loses one count.
            channel.length = max_length(index);
            if (channel.length_enabled && !next_step_clocks_length(apu))
            {
                --channel.length;
            }
        }

        switch (index)
        {
        case 0:
        case 1:
            channel.timer = square_period(channel);
            break;
        case 2:
            if (channel.enabled_before_trigger && channel.timer <= WAVE_TRIGGER_CORRUPTION_WINDOW)
            {
                // On the DMG, retriggering just as a sample is read overwrites
                // the start of wave RAM with the bytes being read.
                const std::size_t byte = ((channel.wave_position + 1) & 0x1F) / 2;
                if (byte < 4)
                {
                    apu.wave_ram[0] = apu.wave_ram[byte];
                }
                else
                {
                    std::copy_n(apu.wave_ram.begin() + static_cast<std::ptrdiff_t>(byte & ~std::size_t{3}), 4, apu.wave_ram.begin());
                }
            }
            channel.timer = wave_period(channel) + 6; // The first sample is read after a short delay.
            channel.wave_position = 0;
            break;
        default:
            channel.timer = noise_period(apu);
            channel.lfsr = 0x7FFF;
            break;
        }

        if (index != 2)
        {
            const uint8_t envelope = reg(apu, static_cast<uint16_t>(base + 2));
            channel.volume = envelope >> 4;
            channel.envelope_timer = (envelope & 0x07) == 0 ? 8 : envelope & 0x07;
            channel.envelope_running = true;
        }

        if (index == 0)
        {
            const uint8_t nr10 = reg(apu, NR10);
            apu.sweep_shadow = channel.frequency;
            apu.sweep_timer = sweep_period(apu);
            apu.sweep_enabled = (nr10 & 0x77) != 0;
            apu.sweep_negated = false;
            if ((nr10 & 0x07) != 0)
            {
                calculate_sweep(apu);
            }
        }

        if (!channel.dac_enabled)
        {
            channel.enabled = false;
        }
    }

    void write_length(APU_LR35902 &apu, int index, uint8_t value)
    {
        apu.channels[static_cast<std::size_t>(index)].length =
            index == 2 ? 256 - value : 64 - (value & 0x3F);
    }

    void write_channel_control(APU_LR35902 &apu, int index, uint8_t value)
    {
        APU_LR35902::Channel &channel = apu.channels[static_cast<std::size_t>(index)];
        if (index != 3)
        {
            channel.frequency = static_cast<uint16_t>((channel.frequency & 0xFF) | ((value & 0x07) << 8));
        }

        // Enabling the length counter while the next step will not clock it
        // clocks it once immediately.
        const bool was_enabled = channel.length_enabled;
        channel.length_enabled = (value & 0x40) != 0;
        if (!was_enabled && channel.length_enabled && !next_step_clocks_length(apu) && channel.length > 0)
        {
            if (--channel.length == 0 && (value & 0x80) == 0)
            {
                channel.enabled = false;
            }
        }

        if ((value & 0x80) != 0)
        {
            trigger(apu, index);
        }
    }

    void power_off(APU_LR35902 &apu)
    {
        // Lengths survive power on the DMG; everything else is cleared.
        std::array<int, 4> lengths{};
        for (std::size_t index = 0; index < apu.channels.size(); ++index)
        {
            lengths[index] = apu.channels[index].length;
        }
        apu.channels = {};
        for (std::size_t index = 0; index < apu.channels.size(); ++index)
        {
            apu.channels[index].length = lengths[index];
        }
        std::fill(apu.registers.begin(), apu.registers.begin() + (NR52 - NR10), 0);
        apu.sweep_enabled = false;
        apu.sweep_negated = false;
        apu.powered = false;
    }

    // Digital output (0-15) of a channel.
    int channel_output(const APU_LR35902 &apu, int index)
    {
        const APU_LR35902::Channel &channel = apu.channels[static_cast<std::size_t>(index)];
        if (!channel.enabled)
        {
            return 0;
        }
        switch (index)
        {
        case 0:
        case 1:
            return ((DUTY_PATTERNS[channel.duty] >> (7 - channel.duty_step)) & 0x01) != 0 ? channel.volume : 0;
        case 2:
        {
            const int volume_code = (reg(apu, 0xFF1C) >> 5) & 0x03;
            return volume_code == 0 ? 0 : channel.wave_sample >> (volume_code - 1);
        }
        default:
            return (channel.lfsr & 0x01) == 0 ? channel.volume : 0;
        }
    }

    void mix_cycles(APU_LR35902 &apu, int cycles)
    {
        const uint8_t panning = reg(apu, 0xFF25);
        double left = 0;
        double right = 0;
        for (int index = 0; index < 4; ++index)
        {
            if (!apu.channels[static_cast<std::size_t>(index)].dac_enabled)
            {
                continue;
            }
            const double analog = channel_output(apu, index) / 7.5 - 1.0;
            if ((panning & (0x10 << index)) != 0)
            {
                left += analog;
            }
            if ((panning & (0x01 << index)) != 0)
            {
                right += analog;
            }
        }
        const uint8_t volume = reg(apu, 0xFF24);
        left *= (((volume >> 4) & 0x07) + 1) / 8.0;
        right *= ((volume & 0x07) + 1) / 8.0;

        apu.sample_accumulator_left += left * cycles;
        apu.sample_accumulator_right += right * cycles;
        apu.sample_cycles += cycles;
        apu.sample_phase += cycles;
        if (apu.sample_phase < apu.cycles_per_sample)
        {
            return;
        }
        apu.sample_phase -= apu.cycles_per_sample;

        // Average the interval, then remove DC like the hardware's output
        // capacitor does.
        static const double charge_factor = std::pow(0.999958, NOMINAL_CYCLES_PER_SAMPLE);
        const double average_left = apu.sample_accumulator_left / apu.sample_cycles / 4.0;
        const double average_right = apu.sample_accumulator_right / apu.sample_cycles / 4.0;
        const double out_left = average_left - apu.highpass_left;
        const double out_right = average_right - apu.highpass_right;
        apu.highpass_left = average_left - out_left * charge_factor;
        apu.highpass_right = average_right - out_right * charge_factor;
        apu.sample_accumulator_left = 0;
        apu.sample_accumulator_right = 0;
        apu.sample_cycles = 0;

        if (apu.samples.size() >= MAX_BUFFERED_SAMPLES)
        {
            apu.samples.clear();
        }
        apu.samples.push_back(static_cast<int16_t>(std::clamp(out_left, -1.0, 1.0) * 32767 * 0.5));
        apu.samples.push_back(static_cast<int16_t>(std::clamp(out_right, -1.0, 1.0) * 32767 * 0.5));
    }
}

void apu_power_on_after_boot(APU_LR35902 &apu, bool channel1_on)
{
    apu = APU_LR35902{};
    apu.powered = true;
    const std::array<std::pair<uint16_t, uint8_t>, 17> boot_registers = {{
        {0xFF10, 0x80}, {0xFF11, 0xBF}, {0xFF12, 0xF3}, {0xFF13, 0xC1}, {0xFF14, 0x87},
        {0xFF16, 0x3F}, {0xFF17, 0x00}, {0xFF19, 0xBF}, {0xFF1A, 0x7F}, {0xFF1B, 0xFF},
        {0xFF1C, 0x9F}, {0xFF1E, 0xBF}, {0xFF20, 0xFF}, {0xFF21, 0x00}, {0xFF23, 0xBF},
        {0xFF24, 0x77}, {0xFF25, 0xF3}}};
    for (const auto &[address, value] : boot_registers)
    {
        reg(apu, address) = value;
    }
    APU_LR35902::Channel &square = apu.channels[0];
    square.dac_enabled = true;
    square.enabled = channel1_on;
    square.duty = 2;
    square.frequency = 0x7C1;
    square.timer = square_period(square);
}

uint8_t apu_read(const APU_LR35902 &apu, uint16_t address)
{
    if (address >= WAVE_RAM_START)
    {
        const APU_LR35902::Channel &wave = apu.channels[2];
        if (wave.enabled)
        {
            // While playing, the CPU only reaches wave RAM if the channel reads
            // it on the same T-cycle, and then sees the byte being read.
            return wave.wave_read_cycles == 0 ? apu.wave_ram[wave.wave_position / 2] : 0xFF;
        }
        return apu.wave_ram[static_cast<std::size_t>(address - WAVE_RAM_START)];
    }
    if (address == NR52)
    {
        uint8_t status = static_cast<uint8_t>(0x70 | (apu.powered ? 0x80 : 0x00));
        for (int index = 0; index < 4; ++index)
        {
            if (apu.channels[static_cast<std::size_t>(index)].enabled)
            {
                status |= static_cast<uint8_t>(1u << index);
            }
        }
        return status;
    }
    const std::size_t index = static_cast<std::size_t>(address - NR10);
    return static_cast<uint8_t>(apu.registers[index] | READ_MASKS[index]);
}

void apu_write(APU_LR35902 &apu, uint16_t address, uint8_t value)
{
    if (address >= WAVE_RAM_START)
    {
        APU_LR35902::Channel &wave = apu.channels[2];
        if (!wave.enabled)
        {
            apu.wave_ram[static_cast<std::size_t>(address - WAVE_RAM_START)] = value;
        }
        else if (wave.wave_read_cycles == 0)
        {
            apu.wave_ram[wave.wave_position / 2] = value;
        }
        return;
    }

    if (address == NR52)
    {
        if ((value & 0x80) == 0 && apu.powered)
        {
            power_off(apu);
        }
        else if ((value & 0x80) != 0 && !apu.powered)
        {
            apu.powered = true;
            apu.frame_step = 0;
        }
        return;
    }

    if (!apu.powered)
    {
        // Only the length counters can be written while powered off.
        switch (address)
        {
        case 0xFF11:
            write_length(apu, 0, value);
            break;
        case 0xFF16:
            write_length(apu, 1, value);
            break;
        case 0xFF1B:
            write_length(apu, 2, value);
            break;
        case 0xFF20:
            write_length(apu, 3, value);
            break;
        default:
            break;
        }
        return;
    }

    if (address > 0xFF25)
    {
        return;
    }
    const uint8_t old_value = reg(apu, address);
    reg(apu, address) = value;
    const int index = (address - NR10) / 5;
    APU_LR35902::Channel &channel = apu.channels[static_cast<std::size_t>(std::min(index, 3))];
    switch (address)
    {
    case 0xFF10:
        // Clearing negate after a negated sweep calculation disables channel 1.
        if ((old_value & 0x08) != 0 && (value & 0x08) == 0 && apu.sweep_negated)
        {
            apu.channels[0].enabled = false;
        }
        break;
    case 0xFF11:
    case 0xFF16:
        channel.duty = value >> 6;
        write_length(apu, index, value);
        break;
    case 0xFF1B:
    case 0xFF20:
        write_length(apu, index, value);
        break;
    case 0xFF12:
    case 0xFF17:
    case 0xFF21:
        channel.dac_enabled = (value & 0xF8) != 0;
        if (!channel.dac_enabled)
        {
            channel.enabled = false;
        }
        break;
    case 0xFF1A:
        channel.dac_enabled = (value & 0x80) != 0;
        if (!channel.dac_enabled)
        {
            channel.enabled = false;
        }
        break;
    case 0xFF13:
    case 0xFF18:
    case 0xFF1D:
        channel.frequency = static_cast<uint16_t>((channel.frequency & 0x700) | value);
        break;
    case 0xFF14:
    case 0xFF19:
    case 0xFF1E:
    case 0xFF23:
        write_channel_control(apu, index, value);
        break;
    default:
        break;
    }
}

void apu_tick(APU_LR35902 &apu, int cycles)
{
    if (!apu.powered)
    {
        return;
    }

    for (int index = 0; index < 4; ++index)
    {
        APU_LR35902::Channel &channel = apu.channels[static_cast<std::size_t>(index)];
        if (index == 2)
        {
            channel.wave_read_cycles = std::min(channel.wave_read_cycles + cycles, 0x10000);
        }
        if (!channel.enabled)
        {
            continue;
        }
        const int timer_before = channel.timer;
        channel.timer -= cycles;
        if (index == 2 && timer_before > 0 && channel.timer <= 0)
        {
            // Cycles since the last sample read, counted from the read's
            // T-cycle within this tick.
            channel.wave_read_cycles = -channel.timer % wave_period(channel);
        }
        while (channel.timer <= 0)
        {
            switch (index)
            {
            case 0:
            case 1:
                channel.timer += square_period(channel);
                channel.duty_step = static_cast<uint8_t>((channel.duty_step + 1) & 0x07);
                break;
            case 2:
            {
                channel.timer += wave_period(channel);
                channel.wave_position = static_cast<uint8_t>((channel.wave_position + 1) & 0x1F);
                const uint8_t pair = apu.wave_ram[channel.wave_position / 2];
                channel.wave_sample = (channel.wave_position & 0x01) == 0 ? pair >> 4 : pair & 0x0F;
                break;
            }
            default:
            {
                channel.timer += noise_period(apu);
                const uint16_t bit = static_cast<uint16_t>((channel.lfsr ^ (channel.lfsr >> 1)) & 0x01);
                channel.lfsr = static_cast<uint16_t>((channel.lfsr >> 1) | (bit << 14));
                if ((reg(apu, 0xFF22) & 0x08) != 0)
                {
                    channel.lfsr = static_cast<uint16_t>((channel.lfsr & ~0x40) | (bit << 6)); // 7-bit mode
                }
                break;
            }
            }
        }
    }

    if (apu.sample_output)
    {
        mix_cycles(apu, cycles);
    }
}

void apu_frame_sequencer_step(APU_LR35902 &apu)
{
    if (!apu.powered)
    {
        return;
    }
    const uint8_t step = apu.frame_step;
    apu.frame_step = static_cast<uint8_t>((step + 1) & 0x07);
    if ((step & 0x01) == 0)
    {
        clock_lengths(apu);
    }
    if (step == 2 || step == 6)
    {
        clock_sweep(apu);
    }
    if (step == 7)
    {
        clock_envelopes(apu);
    }
}
