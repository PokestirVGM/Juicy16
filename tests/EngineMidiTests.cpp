#include "PluginProcessor.h"
#include "PatchList.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

juce::MemoryBlock makeState(const juce::String& fontPath, int selectedChannel = 1)
{
    juce::XmlElement xml{"MYPLUGINSETTINGS"};
    xml.setAttribute("stateVersion", 2);

    auto* ui{xml.createNewChildElement("uiState")};
    ui->setAttribute("width", 850);
    ui->setAttribute("height", 650);
    ui->setAttribute("selectedChannel", selectedChannel);

    auto* font{xml.createNewChildElement("soundFont")};
    font->setAttribute("path", fontPath);
    font->setAttribute("bookmark", "");

    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    return state;
}

float magnitude(const juce::AudioBuffer<float>& audio, int start, int length)
{
    double sum{0.0};
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = start; i < start + length; ++i)
            sum += std::abs(audio.getSample(ch, i));
    return static_cast<float>(sum);
}

double estimatePeriodicFrequency(const juce::AudioBuffer<float>& audio,
                                 int start,
                                 int length,
                                 double sampleRate,
                                 double expectedFrequency)
{
    const float* samples{audio.getReadPointer(0)};
    const int minimumLag{static_cast<int>(sampleRate / (expectedFrequency * 1.2))};
    const int maximumLag{static_cast<int>(std::ceil(
        sampleRate / (expectedFrequency * 0.8)))};
    double bestCorrelation{-1.0};
    int bestLag{minimumLag};

    for (int lag = minimumLag; lag <= maximumLag; ++lag) {
        double correlation{0.0};
        double energyA{0.0};
        double energyB{0.0};
        for (int i = start + lag; i < start + length; ++i) {
            const double a{samples[i]};
            const double b{samples[i - lag]};
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double normalised{correlation / std::sqrt(energyA * energyB + 1.0e-30)};
        if (normalised > bestCorrelation) {
            bestCorrelation = normalised;
            bestLag = lag;
        }
    }
    return sampleRate / static_cast<double>(bestLag);
}

void render(JuicySFAudioProcessor& processor,
            juce::AudioBuffer<float>& audio,
            juce::MidiBuffer& midi)
{
    audio.clear();
    processor.processBlock(audio, midi);
}

void addAllSoundOff(juce::MidiBuffer& midi, int samplePosition = 0)
{
    for (int channel = 1; channel <= 16; ++channel)
        midi.addEvent(
            juce::MidiMessage::controllerEvent(channel, 120, 0), samplePosition);
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc != 2) {
        std::fprintf(stderr, "usage: JuicySFEngineMidiTests <font.dls|sf2|sf3>\n");
        return 2;
    }

    constexpr int blockSize{1024};
    JuicySFAudioProcessor processor;
    processor.prepareToPlay(48000.0, blockSize);
    const auto state{makeState(argv[1])};
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    auto& model{processor.getFluidSynthModel()};
    juce::AudioBuffer<float> audio{2, blockSize};

    check(FluidSynthModel::progParamChannel("progCh1") == 0
              && FluidSynthModel::progParamChannel("progCh16") == 15
              && FluidSynthModel::progParamChannel("progCh0") == -1
              && FluidSynthModel::progParamChannel("progCh17") == -1
              && FluidSynthModel::progParamChannel("progCh1x") == -1,
          "fixed program-parameter IDs parse without temporary String allocation");

    std::printf("== patch list ==\n");
    {
        juce::ValueTree emptyBanks{"banks"};
        check(buildPatchList(emptyBanks).empty(), "empty bank tree produces an empty patch list");

        juce::ValueTree banks{"banks"};
        juce::ValueTree percussion{"bank", {{"num", 128}}, {}};
        percussion.appendChild(
            {"preset", {{"num", 0}, {"name", "Standard Kit"}}, {}}, nullptr);
        juce::ValueTree sparse{"bank", {{"num", 7}}, {}};
        juce::ValueTree melodic{"bank", {{"num", 0}}, {}};
        const juce::String unicodeLongName{
            juce::String{juce::CharPointer_UTF8{
                "\xe9\x95\xb7\xe3\x81\x84\xe9\x9f\xb3\xe8\x89\xb2 \xf0\x9f\x8e\xb9 "}}
                + juce::String::repeatedString("Preset", 40)};
        melodic.appendChild(
            {"preset", {{"num", 5}, {"name", unicodeLongName}}, {}}, nullptr);
        melodic.appendChild(
            {"preset", {{"num", 2}, {"name", "Duplicate A"}}, {}}, nullptr);
        melodic.appendChild(
            {"preset", {{"num", 2}, {"name", "Duplicate B"}}, {}}, nullptr);
        banks.appendChild(percussion, nullptr);
        banks.appendChild(sparse, nullptr);
        banks.appendChild(melodic, nullptr);

        const auto patches{buildPatchList(banks)};
        check(patches.size() == 4
                  && patches[0].bank == 0 && patches[0].preset == 2
                  && patches[1].bank == 0 && patches[1].preset == 2
                  && patches[2].bank == 0 && patches[2].preset == 5
                  && patches[2].name == unicodeLongName
                  && patches[3].bank == 128 && patches[3].preset == 0,
              "sparse, duplicate, unsorted, Unicode, and long patch data are preserved and sorted");
    }

    std::printf("== General MIDI defaults ==\n");
    {
        int melodicBank{-1}, melodicPreset{-1};
        int percussionBank{-1}, percussionPreset{-1};
        check(model.getChannelProgram(0, melodicBank, melodicPreset)
                  && model.getChannelProgram(9, percussionBank, percussionPreset)
                  && melodicBank == 0 && percussionBank == 128,
              "fresh font load uses melodic bank 0 and channel 10 percussion bank 128");
    }
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(10, 0), 0);
        render(processor, audio, midi);
        int bank{-1}, preset{-1};
        check(model.getChannelProgram(9, bank, preset)
                  && bank == 128 && preset == 0,
              "channel 10 Program Change remains in the percussion bank");
    }

    std::printf("== sample-accurate rendering ==\n");
    {
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 256);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 256) == 0.0f,
              "note-on at sample 256 leaves the preceding segment silent");
        check(magnitude(audio, 256, blockSize - 256) > 0.001f,
              "note-on produces audio after its timestamp");
    }
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 512);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 512) > 0.001f,
              "mid-block note-off does not truncate audio before its timestamp");
    }
    {
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(100)), 256);
        midi.addEvent(juce::MidiMessage::noteOff(1, 62), 512);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 256) == 0.0f
                  && magnitude(audio, 256, 256) > 0.001f,
              "note-on and note-off in one block produce a bounded pre-release note segment");
    }
    {
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 128);
        midi.addEvent(juce::MidiMessage::noteOn(16, 72, static_cast<juce::uint8>(100)), 640);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 128) == 0.0f
                  && magnitude(audio, 128, 512) > 0.001f,
              "events on different MIDI channels retain independent timestamps");
    }
    {
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::programChange(3, 4), 400);
        midi.addEvent(juce::MidiMessage::noteOn(3, 60, static_cast<juce::uint8>(100)), 401);
        render(processor, audio, midi);
        int bank{-1}, preset{-1};
        check(magnitude(audio, 0, 400) == 0.0f
                  && magnitude(audio, 401, blockSize - 401) > 0.001f
                  && model.getChannelProgram(2, bank, preset) && preset == 4,
              "same-block Program Change selects the new channel program before the following note");
    }
    {
        bool allBlockSizesTimed{true};
        for (const int size : {32, 64, 256, 512, 1024}) {
            processor.prepareToPlay(48000.0, size);
            audio.setSize(2, size, false, false, true);

            juce::MidiBuffer silenceVoices;
            addAllSoundOff(silenceVoices);
            render(processor, audio, silenceVoices);

            juce::MidiBuffer timedNote;
            timedNote.addEvent(
                juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), size / 2);
            render(processor, audio, timedNote);
            allBlockSizesTimed = allBlockSizesTimed
                && magnitude(audio, 0, size / 2) == 0.0f;
        }
        check(allBlockSizesTimed,
              "event timestamps are respected at block sizes 32, 64, 256, 512, and 1024");
        processor.prepareToPlay(48000.0, blockSize);
        audio.setSize(2, blockSize, false, false, true);
    }
    {
        juce::MidiBuffer silenceVoices;
        addAllSoundOff(silenceVoices);
        render(processor, audio, silenceVoices);

        juce::AudioBuffer<float> mono{1, blockSize * 2};
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(100)), 256);
        render(processor, mono, midi);
        check(magnitude(mono, 0, 256) == 0.0f
                  && magnitude(mono, 256, mono.getNumSamples() - 256) > 0.001f,
              "mono downmix preserves timing and safely chunks blocks above the prepared maximum");
    }

    std::printf("== 16-channel program routing ==\n");
    {
        juce::MidiBuffer midi;
        for (int ch = 1; ch <= 16; ++ch)
            midi.addEvent(juce::MidiMessage::programChange(ch, (ch - 1) % 8), 32 + ch);
        render(processor, audio, midi);

        bool allProgramsMatch{true};
        for (int ch = 0; ch < 16; ++ch) {
            int bank{-1}, preset{-1};
            allProgramsMatch = allProgramsMatch
                && model.getChannelProgram(ch, bank, preset)
                && preset == ch % 8;
        }
        check(allProgramsMatch, "Program Change reaches every channel independently");
    }

    std::printf("== reset and same-block chase ==\n");
    {
        const juce::uint8 gmReset[]{0x7e, 0x7f, 0x09, 0x01};
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(1, 5), 10);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 100), 11);
        midi.addEvent(juce::MidiMessage::createSysExMessage(gmReset, sizeof(gmReset)), 20);
        midi.addEvent(juce::MidiMessage::programChange(1, 7), 30);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 80), 31);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 32);
        render(processor, audio, midi);
        model.handleUpdateNowIfNeeded();

        int bank{-1}, preset{-1}, cutoff{-1};
        int percussionBank{-1}, percussionPreset{-1};
        check(model.getChannelProgram(0, bank, preset) && preset == 7
                  && model.getControllerValue(0, 74, cutoff) && cutoff == 80
                  && model.getChannelProgram(9, percussionBank, percussionPreset)
                  && percussionBank == 128,
              "GM reset cannot overwrite newer same-block Program Change or CC state");
    }
    {
        const std::array<std::vector<juce::uint8>, 2> resets{{
            {0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41},
            {0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00}
        }};
        bool gsXgDeterministic{true};
        for (const auto& bytes : resets) {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::programChange(16, 2), 10);
            midi.addEvent(juce::MidiMessage::controllerEvent(16, 71, 90), 11);
            midi.addEvent(juce::MidiMessage::createSysExMessage(
                bytes.data(), static_cast<int>(bytes.size())), 20);
            midi.addEvent(juce::MidiMessage::programChange(16, 6), 30);
            midi.addEvent(juce::MidiMessage::controllerEvent(16, 71, 70), 31);
            midi.addEvent(juce::MidiMessage::noteOn(16, 60, static_cast<juce::uint8>(100)), 32);
            render(processor, audio, midi);
            int bank{-1}, preset{-1}, resonance{-1};
            int percussionBank{-1}, percussionPreset{-1};
            gsXgDeterministic = gsXgDeterministic
                && model.getChannelProgram(15, bank, preset) && preset == 6
                && model.getControllerValue(15, 71, resonance) && resonance == 70
                && model.getChannelProgram(9, percussionBank, percussionPreset)
                && percussionBank == 128;
        }
        check(gsXgDeterministic,
              "GS and XG resets cannot overwrite newer same-block Program Change or CC state");
    }

    std::printf("== controller and pitch-bend fidelity ==\n");
    {
        constexpr std::array<int, 4> channels{0, 1, 9, 15};
        constexpr std::array<int, 7> values{0, 1, 63, 64, 65, 126, 127};
        bool exhaustive{true};
        for (const int expected : values) {
            juce::MidiBuffer midi;
            for (const int channel : channels)
                for (int cc = 0; cc < 128; ++cc)
                    midi.addEvent(
                        juce::MidiMessage::controllerEvent(channel + 1, cc, expected),
                        100 + channel);
            render(processor, audio, midi);

            for (const int channel : channels) {
                for (int cc = 0; cc < 128; ++cc) {
                    int actual{-1}, sample{-1};
                    exhaustive = exhaustive
                        && model.getLastDispatchedController(channel, cc, actual, sample)
                        && actual == expected && sample == 100 + channel;
                }
            }
        }
        check(exhaustive,
              "CC0-CC127 preserve all required values, timestamps, and channels 1, 2, 10, and 16");

        // Channel-mode CC124-127 intentionally alter FluidSynth's channel mode.
        // Reset before later tests so their defined side effects cannot pollute
        // pitch/controller isolation checks.
        const juce::uint8 gmReset[]{0x7e, 0x7f, 0x09, 0x01};
        juce::MidiBuffer reset;
        reset.addEvent(juce::MidiMessage::createSysExMessage(gmReset, sizeof(gmReset)), 0);
        render(processor, audio, reset);
    }
    {
        constexpr std::array<int, 4> channels{0, 1, 9, 15};
        constexpr std::array<int, 4> volumes{17, 43, 89, 121};
        constexpr std::array<int, 4> bends{0, 8192, 16383, 4097};
        juce::MidiBuffer midi;
        for (std::size_t i = 0; i < channels.size(); ++i) {
            const int midiChannel{channels[i] + 1};
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 1, 11 + static_cast<int>(i)), 10);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 7, volumes[i]), 20);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 10, 20 + static_cast<int>(i)), 30);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 11, 90 - static_cast<int>(i)), 40);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 64, i % 2 == 0 ? 127 : 0), 50);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 66, i % 2 == 0 ? 0 : 127), 60);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 67, 64 + static_cast<int>(i)), 70);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 91, 31 + static_cast<int>(i)), 80);
            midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 93, 41 + static_cast<int>(i)), 90);
            midi.addEvent(juce::MidiMessage::pitchWheel(midiChannel, bends[i]), 100);
        }
        render(processor, audio, midi);

        bool isolated{true};
        for (std::size_t i = 0; i < channels.size(); ++i) {
            int cc{-1}, bend{-1};
            isolated = isolated
                && model.getControllerValue(channels[i], 7, cc) && cc == volumes[i]
                && model.getPitchBend(channels[i], bend) && bend == bends[i];
        }
        check(isolated, "CC values and full 14-bit bends remain isolated on channels 1, 2, 10, and 16");
    }
    {
        constexpr std::array<int, 6> soundCcs{71, 72, 73, 74, 75, 79};
        juce::MidiBuffer midi;
        for (int channel = 0; channel < 16; ++channel)
            for (std::size_t index = 0; index < soundCcs.size(); ++index)
                midi.addEvent(
                    juce::MidiMessage::controllerEvent(
                        channel + 1,
                        soundCcs[index],
                        (channel * 7 + static_cast<int>(index) * 13 + 1) % 128),
                    200 + channel);
        render(processor, audio, midi);
        model.handleUpdateNowIfNeeded();

        bool engineExact{true};
        for (int channel = 0; channel < 16; ++channel) {
            for (std::size_t index = 0; index < soundCcs.size(); ++index) {
                int actual{-1};
                const int expected{
                    (channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                engineExact = engineExact
                    && model.getControllerValue(channel, soundCcs[index], actual)
                    && actual == expected;
            }
        }

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        JuicySFAudioProcessor restored;
        restored.prepareToPlay(48000.0, blockSize);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        bool restoredExact{true};
        for (int channel = 0; channel < 16; ++channel) {
            for (std::size_t index = 0; index < soundCcs.size(); ++index) {
                int actual{-1};
                const int expected{
                    (channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                restoredExact = restoredExact
                    && restored.getFluidSynthModel().getControllerValue(
                        channel, soundCcs[index], actual)
                    && actual == expected;
            }
        }
        check(engineExact && restoredExact,
              "all six exposed sound controllers remain exact and persist independently on all 16 channels");
    }
    {
        constexpr std::array<int, 9> bendValues{
            0, 1, 4096, 8191, 8192, 8193, 12288, 16382, 16383};
        constexpr std::array<int, 4> channels{0, 1, 9, 15};
        bool exact{true};
        juce::AudioBuffer<float> tinyAudio{2, 1};
        for (const int channel : channels) {
            for (const int expected : bendValues) {
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::pitchWheel(channel + 1, expected), 0);
                render(processor, tinyAudio, midi);
                int actual{-1};
                exact = exact && model.getPitchBend(channel, actual) && actual == expected;
            }
        }
        check(exact, "all required 14-bit pitch-bend edge and center values are exact");
    }
    {
        constexpr std::array<int, 4> channels{0, 1, 9, 15};
        constexpr std::array<int, 4> ranges{2, 12, 24, 7};
        bool defaultRange{true};
        for (const int channel : channels) {
            int sensitivity{-1};
            defaultRange = defaultRange
                && model.getPitchWheelSensitivity(channel, sensitivity)
                && sensitivity == 2;
        }
        check(defaultRange, "default pitch-bend range is two semitones per channel");

        juce::MidiBuffer midi;
        for (std::size_t i = 0; i < channels.size(); ++i) {
            const int midiChannel{channels[i] + 1};
            for (const auto [cc, value] : std::array<std::pair<int, int>, 4>{
                     std::pair{101, 0}, std::pair{100, 0}, std::pair{6, ranges[i]}, std::pair{38, 0}})
                midi.addEvent(juce::MidiMessage::controllerEvent(midiChannel, cc, value), 64);
        }
        render(processor, audio, midi);
        bool independent{true};
        for (std::size_t i = 0; i < channels.size(); ++i) {
            int sensitivity{-1};
            independent = independent
                && model.getPitchWheelSensitivity(channels[i], sensitivity)
                && sensitivity == ranges[i];
        }
        check(independent,
              "RPN pitch-bend ranges remain independent on channels 1, 2, 10, and 16");

        juce::MidiBuffer nullRpn;
        for (const auto [cc, value] : std::array<std::pair<int, int>, 3>{
                 std::pair{101, 127}, std::pair{100, 127}, std::pair{6, 36}})
            nullRpn.addEvent(juce::MidiMessage::controllerEvent(16, cc, value), 64);
        render(processor, audio, nullRpn);
        int afterNull{-1};
        check(model.getPitchWheelSensitivity(15, afterNull) && afterNull == ranges.back(),
              "RPN Null prevents later Data Entry from changing pitch-bend range");

        const juce::uint8 gmReset[]{0x7e, 0x7f, 0x09, 0x01};
        juce::MidiBuffer reset;
        reset.addEvent(juce::MidiMessage::createSysExMessage(gmReset, sizeof(gmReset)), 0);
        render(processor, audio, reset);
        bool resetRange{true};
        for (const int channel : channels) {
            int sensitivity{-1};
            resetRange = resetRange
                && model.getPitchWheelSensitivity(channel, sensitivity)
                && sensitivity == 2;
        }
        check(resetRange, "GM reset restores the default two-semitone bend range");
    }
    {
        constexpr int longBlock{65536};
        constexpr std::array<int, 3> bends{0, 8192, 16383};
        juce::AudioBuffer<float> pitchAudio{2, longBlock};
        bool allSampleRates{true};
        for (const double sampleRate : {44100.0, 48000.0, 96000.0}) {
            processor.prepareToPlay(sampleRate, blockSize);
            std::array<double, bends.size()> measured{};
            for (std::size_t index = 0; index < bends.size(); ++index) {
                juce::MidiBuffer midi;
                addAllSoundOff(midi);
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 101, 0), 0);
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 100, 0), 0);
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 6, 12), 0);
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 38, 0), 0);
                midi.addEvent(juce::MidiMessage::pitchWheel(1, bends[index]), 0);
                midi.addEvent(
                    juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
                render(processor, pitchAudio, midi);

                const double semitones{
                    12.0 * static_cast<double>(bends[index] - 8192) / 8192.0};
                const double expected{440.0 * std::pow(2.0, semitones / 12.0)};
                measured[index] = estimatePeriodicFrequency(
                    pitchAudio,
                    static_cast<int>(sampleRate * 0.2),
                    static_cast<int>(sampleRate * 0.3),
                    sampleRate,
                    expected);
            }
            const double downRatio{measured[0] / measured[1]};
            const double upRatio{measured[2] / measured[1]};
            const double expectedUp{std::pow(2.0, 8191.0 / 8192.0)};
            allSampleRates = allSampleRates
                && std::abs(downRatio - 0.5) < 0.03
                && std::abs(upRatio - expectedUp) < 0.06;
        }
        check(allSampleRates,
              "audio-domain full-down/center/full-up bends follow a twelve-semitone RPN range at 44.1, 48, and 96 kHz");
        processor.prepareToPlay(48000.0, blockSize);
    }
    {
        constexpr int longBlock{65536};
        juce::AudioBuffer<float> pitchAudio{2, longBlock};
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 101, 0), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 100, 0), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 6, 12), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 38, 0), 0);
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 8192), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 0), 16384);
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 8192), 32768);
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 49152);
        render(processor, pitchAudio, midi);

        const double centreBefore{estimatePeriodicFrequency(
            pitchAudio, 4096, 8192, 48000.0, 440.0)};
        const double down{estimatePeriodicFrequency(
            pitchAudio, 20480, 8192, 48000.0, 220.0)};
        const double centreAfter{estimatePeriodicFrequency(
            pitchAudio, 36864, 8192, 48000.0, 440.0)};
        const double up{estimatePeriodicFrequency(
            pitchAudio, 53248, 8192, 48000.0, 880.0)};
        check(std::abs(down / centreBefore - 0.5) < 0.03
                  && std::abs(centreAfter / centreBefore - 1.0) < 0.03
                  && std::abs(up / centreBefore - 2.0) < 0.06,
              "multiple bends within one block affect sustained audio only after their sample offsets");
    }
    {
        constexpr int longBlock{65536};
        juce::AudioBuffer<float> ccAudio{2, longBlock};
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 120, 0), 32768);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 49152);
        render(processor, ccAudio, midi);
        check(magnitude(ccAudio, 8192, 8192) > 0.001f
                  && magnitude(ccAudio, 36864, 8192) == 0.0f
                  && magnitude(ccAudio, 53248, 8192) > 0.001f,
              "All Sound Off takes effect at its in-block timestamp and later notes still render");
    }

    std::printf("== pressure fidelity ==\n");
    {
        juce::MidiBuffer midi;
        for (int channel = 0; channel < 16; ++channel) {
            midi.addEvent(
                juce::MidiMessage::channelPressureChange(channel + 1, channel * 7 % 128),
                200 + channel);
            midi.addEvent(
                juce::MidiMessage::aftertouchChange(channel + 1, 36 + channel, 127 - channel * 3),
                300 + channel);
        }
        render(processor, audio, midi);
        bool pressureExact{true};
        for (int channel = 0; channel < 16; ++channel) {
            int channelValue{-1}, channelSample{-1};
            int keyValue{-1}, keySample{-1};
            pressureExact = pressureExact
                && model.getLastDispatchedChannelPressure(
                    channel, channelValue, channelSample)
                && channelValue == channel * 7 % 128
                && channelSample == 200 + channel
                && model.getLastDispatchedKeyPressure(
                    channel, 36 + channel, keyValue, keySample)
                && keyValue == 127 - channel * 3
                && keySample == 300 + channel;
        }
        check(pressureExact,
              "channel and polyphonic key pressure preserve value, timestamp, channel, and key on all 16 channels");
    }

    std::printf("== corrupt-state bounds ==\n");
    {
        const auto corruptState{makeState(argv[1], 999)};
        processor.setStateInformation(corruptState.getData(), static_cast<int>(corruptState.getSize()));
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        const auto xml{juce::AudioProcessor::getXmlFromBinary(
            saved.getData(), static_cast<int>(saved.getSize()))};
        const auto* ui{xml != nullptr ? xml->getChildByName("uiState") : nullptr};
        check(ui != nullptr && ui->getIntAttribute("selectedChannel") == 16,
              "out-of-range restored selectedChannel is clamped before use and save");
    }

    std::printf("== state round-trip and migration ==\n");
    {
        model.setChannelProgram(1, 0, 4);
        model.setChannelProgram(9, 128, 0);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(2, 74, 99), 0);
        render(processor, audio, midi);
        model.handleUpdateNowIfNeeded();

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        JuicySFAudioProcessor restored;
        restored.prepareToPlay(48000.0, blockSize);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        int bank{-1}, preset{-1}, cutoff{-1};
        check(restored.getFluidSynthModel().getChannelProgram(1, bank, preset)
                  && bank == 0 && preset == 4
                  && restored.getFluidSynthModel().getControllerValue(1, 74, cutoff)
                  && cutoff == 99,
              "current state round-trip restores independent channel program and sound-controller state");
        check(restored.getFluidSynthModel().getChannelProgram(9, bank, preset)
                  && bank == 128 && preset == 0,
              "manual channel 10 percussion assignment persists through state round-trip");
    }
    {
        juce::XmlElement legacy{"MYPLUGINSETTINGS"};
        legacy.setAttribute("stateVersion", 1);
        auto* channels{legacy.createNewChildElement("channelPrograms")};
        auto* ch{channels->createNewChildElement("ch")};
        ch->setAttribute("num", 0);
        ch->setAttribute("bank", 0);
        ch->setAttribute("preset", 3);
        for (const char* name : {"attack", "decay", "sustain", "release", "filterCutOff", "filterResonance"})
            ch->setAttribute(name, 0);
        auto* font{legacy.createNewChildElement("soundFont")};
        font->setAttribute("path", argv[1]);
        font->setAttribute("bookmark", "");
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary(legacy, legacyState);

        JuicySFAudioProcessor migrated;
        migrated.prepareToPlay(48000.0, blockSize);
        migrated.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
        int bank{-1}, preset{-1}, cutoff{-1};
        check(migrated.getFluidSynthModel().getChannelProgram(0, bank, preset)
                  && preset == 3
                  && migrated.getFluidSynthModel().getControllerValue(0, 74, cutoff)
                  && cutoff == 64,
              "pre-v2 state keeps program assignments but migrates old unipolar sound controls to neutral");
    }
    {
        juce::XmlElement bounded{"MYPLUGINSETTINGS"};
        bounded.setAttribute("stateVersion", 2);
        auto* channels{bounded.createNewChildElement("channelPrograms")};
        auto* ch{channels->createNewChildElement("ch")};
        ch->setAttribute("num", 0);
        ch->setAttribute("bank", 999);
        ch->setAttribute("preset", -50);
        ch->setAttribute("filterCutOff", 999);
        auto* font{bounded.createNewChildElement("soundFont")};
        font->setAttribute("path", argv[1]);
        font->setAttribute("bookmark", "");
        juce::MemoryBlock boundedState;
        juce::AudioProcessor::copyXmlToBinary(bounded, boundedState);

        JuicySFAudioProcessor boundedProcessor;
        boundedProcessor.prepareToPlay(48000.0, blockSize);
        boundedProcessor.setStateInformation(
            boundedState.getData(), static_cast<int>(boundedState.getSize()));
        int cutoff{-1};
        check(boundedProcessor.getFluidSynthModel().getControllerValue(0, 74, cutoff)
                  && cutoff == 127,
              "out-of-range saved bank, preset, and controller values are clamped before engine use");

        const char malformed[]{'n', 'o', 't', 's', 't', 'a', 't', 'e'};
        boundedProcessor.setStateInformation(malformed, sizeof(malformed));
        check(boundedProcessor.getFluidSynthModel().getControllerValue(0, 74, cutoff)
                  && cutoff == 127,
              "malformed non-XML state is ignored without corrupting current state");

        std::mt19937 random{0x53544154u};
        for (int iteration = 0; iteration < 1000; ++iteration) {
            juce::MemoryBlock fuzzState{static_cast<size_t>(random() % 2048u), true};
            auto* bytes{static_cast<juce::uint8*>(fuzzState.getData())};
            for (size_t i = 0; i < fuzzState.getSize(); ++i)
                bytes[i] = static_cast<juce::uint8>(random());
            boundedProcessor.setStateInformation(
                fuzzState.getData(), static_cast<int>(fuzzState.getSize()));
        }
        check(boundedProcessor.getFluidSynthModel().getControllerValue(0, 74, cutoff)
                  && cutoff == 127,
              "1000 deterministic malformed state blobs are ignored without state corruption");
    }
    {
        juce::XmlElement newer{"MYPLUGINSETTINGS"};
        newer.setAttribute("stateVersion", 999);
        auto* channels{newer.createNewChildElement("channelPrograms")};
        auto* ch{channels->createNewChildElement("ch")};
        ch->setAttribute("num", 0);
        ch->setAttribute("bank", 0);
        ch->setAttribute("preset", 99);
        juce::MemoryBlock newerState;
        juce::AudioProcessor::copyXmlToBinary(newer, newerState);

        int beforeBank{-1}, beforePreset{-1};
        model.getChannelProgram(0, beforeBank, beforePreset);
        processor.setStateInformation(
            newerState.getData(), static_cast<int>(newerState.getSize()));
        int afterBank{-1}, afterPreset{-1};
        check(model.getChannelProgram(0, afterBank, afterPreset)
                  && afterBank == beforeBank && afterPreset == beforePreset
                  && model.getFontLoadStatus() == "error",
              "newer state schemas are rejected visibly instead of being reinterpreted");
    }

    std::printf("== transactional bank replacement ==\n");
    {
        const juce::String originalPath{model.getLoadedFontPath()};
        const auto missingState{makeState("/this/file/does/not/exist.dls")};
        processor.setStateInformation(
            missingState.getData(), static_cast<int>(missingState.getSize()));

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 120, 0), 0);
        midi.addEvent(juce::MidiMessage::programChange(1, 0), 1);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 2);
        render(processor, audio, midi);
        juce::MemoryBlock savedAfterFailure;
        processor.getStateInformation(savedAfterFailure);
        const auto savedXml{juce::AudioProcessor::getXmlFromBinary(
            savedAfterFailure.getData(), static_cast<int>(savedAfterFailure.getSize()))};
        const auto* savedFont{savedXml != nullptr
            ? savedXml->getChildByName("soundFont") : nullptr};
        check(model.getFontLoadStatus() == "error"
                  && model.getLoadedFontPath() == originalPath
                  && savedFont != nullptr
                  && savedFont->getStringAttribute("path") == originalPath
                  && magnitude(audio, 2, blockSize - 2) > 0.001f,
              "a failed replacement reports an error and keeps the active bank in audio and saved state");
    }

    std::printf("== engine_midi_tests: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
