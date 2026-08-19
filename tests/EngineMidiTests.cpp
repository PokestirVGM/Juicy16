#include "PluginProcessor.h"
#include "PatchList.h"
#include "GuiConstants.h"
#include "MyColours.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <map>
#include <set>
#include <vector>

#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

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

// Normalized cross-correlation of two renderings of the same segment. Identical
// synth state gives ~1.0; a different instrument at the same pitch drops well
// below that, because the programs differ in spectrum rather than fundamental.
double waveformCorrelation(const juce::AudioBuffer<float>& a,
                           const juce::AudioBuffer<float>& b,
                           int start,
                           int length)
{
    double dot{0.0}, energyA{0.0}, energyB{0.0};
    for (int ch = 0; ch < juce::jmin(a.getNumChannels(), b.getNumChannels()); ++ch) {
        for (int i = start; i < start + length; ++i) {
            const double x{a.getSample(ch, i)};
            const double y{b.getSample(ch, i)};
            dot += x * y;
            energyA += x * x;
            energyB += y * y;
        }
    }
    return dot / std::sqrt(energyA * energyB + 1.0e-30);
}

// WCAG 2.x relative luminance and contrast ratio. Used to check that text
// remains legible against the surfaces it is actually drawn on, rather than
// eyeballing a screenshot.
juce::Component* findNamedComponent(juce::Component& root, const juce::String& name);

// The channel table declines focus so arrow keys do not fight the row selection
// the plugin drives from MIDI; looked up separately for readability.
juce::Component* channelTableForFocus(juce::Component& editor)
{
    return findNamedComponent(editor, "MIDI channel assignments");
}

double relativeLuminance(juce::Colour colour)
{
    const auto channel{[](double value) {
        return value <= 0.03928 ? value / 12.92
                                : std::pow((value + 0.055) / 1.055, 2.4);
    }};
    return 0.2126 * channel(colour.getFloatRed())
         + 0.7152 * channel(colour.getFloatGreen())
         + 0.0722 * channel(colour.getFloatBlue());
}

double contrastRatio(juce::Colour foreground, juce::Colour background)
{
    const double a{relativeLuminance(foreground)};
    const double b{relativeLuminance(background)};
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
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

juce::AudioParameterInt* findIntParameter(JuicySFAudioProcessor& processor,
                                           const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
        if (auto* identified{dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)};
            identified != nullptr && identified->paramID == id)
            return dynamic_cast<juce::AudioParameterInt*>(parameter);
    return nullptr;
}

bool getSavedChannelProgram(JuicySFAudioProcessor& processor,
                            int channel,
                            int& bank,
                            int& preset)
{
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    const auto xml{juce::AudioProcessor::getXmlFromBinary(
        saved.getData(), static_cast<int>(saved.getSize()))};
    const auto* channels{xml != nullptr ? xml->getChildByName("channelPrograms") : nullptr};
    if (channels == nullptr)
        return false;
    for (const auto* child : channels->getChildIterator()) {
        if (child->getIntAttribute("num", -1) == channel) {
            bank = child->getIntAttribute("bank", -1);
            preset = child->getIntAttribute("preset", -1);
            return true;
        }
    }
    return false;
}

struct ControllerFixtureEvent {
    int sample{-1};
    bool pitchBend{false};
    int channel{-1}; // one-based MIDI channel
    int numberOrBend{-1};
    int value{-1};
};

bool loadControllerFixture(const juce::File& file,
                           std::vector<ControllerFixtureEvent>& events,
                           juce::String& error)
{
    juce::StringArray lines;
    if (!file.existsAsFile()) {
        error = "could not read " + file.getFullPathName();
        return false;
    }
    file.readLines(lines);

    std::set<std::pair<int, int>> controllerKeys;
    std::set<int> bendChannels;
    int previousSample{-1};
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const juce::String line{lines[lineIndex].trim()};
        if (line.isEmpty() || line.startsWithChar('#'))
            continue;

        juce::StringArray fields;
        fields.addTokens(line, ",", "");
        fields.trim();
        if (fields.size() != 5) {
            error = "line " + juce::String(lineIndex + 1) + " must have five fields";
            return false;
        }

        ControllerFixtureEvent event;
        event.sample = fields[0].getIntValue();
        event.pitchBend = fields[1] == "pitchbend";
        event.channel = fields[2].getIntValue();
        event.numberOrBend = fields[3].getIntValue();
        event.value = fields[4].getIntValue();
        const bool knownType{event.pitchBend || fields[1] == "cc"};
        const bool validPayload{event.pitchBend
            ? event.numberOrBend >= 0 && event.numberOrBend <= 16383
            : event.numberOrBend >= 0 && event.numberOrBend <= 127
                && event.value >= 0 && event.value <= 127};
        if (!knownType || event.sample < previousSample || event.sample < 0
            || event.sample >= 1024 || event.channel < 1 || event.channel > 16
            || !validPayload) {
            error = "invalid event at line " + juce::String(lineIndex + 1);
            return false;
        }

        // The harness compares the engine's final per-controller/per-channel state;
        // uniqueness makes that comparison an unambiguous trace assertion.
        const bool unique{event.pitchBend
            ? bendChannels.insert(event.channel).second
            : controllerKeys.insert({event.channel, event.numberOrBend}).second};
        if (!unique) {
            error = "duplicate controller/bend key at line " + juce::String(lineIndex + 1);
            return false;
        }
        previousSample = event.sample;
        events.push_back(event);
    }

    if (events.empty()) {
        error = "fixture contains no events";
        return false;
    }
    return true;
}

juce::Component* findNamedComponent(juce::Component& root,
                                    const juce::String& name)
{
    if (root.getName() == name)
        return &root;
    for (int index = 0; index < root.getNumChildComponents(); ++index)
        if (auto* match{findNamedComponent(*root.getChildComponent(index), name)})
            return match;
    return nullptr;
}

void appendFourCC(std::vector<juce::uint8>& bytes, const char (&fourCC)[5])
{
    bytes.insert(bytes.end(), fourCC, fourCC + 4);
}

void appendLittleEndian32(std::vector<juce::uint8>& bytes, juce::uint32 value)
{
    for (int byte = 0; byte < 4; ++byte)
        bytes.push_back(static_cast<juce::uint8>((value >> (byte * 8)) & 0xff));
}

std::vector<juce::uint8> makeZeroInstrumentDls()
{
    std::vector<juce::uint8> bytes;
    appendFourCC(bytes, "RIFF");
    appendLittleEndian32(bytes, 0);
    appendFourCC(bytes, "DLS ");
    appendFourCC(bytes, "colh");
    appendLittleEndian32(bytes, 4);
    appendLittleEndian32(bytes, 0); // zero instruments
    appendFourCC(bytes, "ptbl");
    appendLittleEndian32(bytes, 8);
    appendLittleEndian32(bytes, 8); // pool-table header size
    appendLittleEndian32(bytes, 0); // zero wave-pool cues
    appendFourCC(bytes, "LIST");
    appendLittleEndian32(bytes, 4);
    appendFourCC(bytes, "lins");   // empty instrument list
    const auto riffSize{static_cast<juce::uint32>(bytes.size() - 8)};
    for (int byte = 0; byte < 4; ++byte)
        bytes[4 + static_cast<std::size_t>(byte)]
            = static_cast<juce::uint8>((riffSize >> (byte * 8)) & 0xff);
    return bytes;
}

// Plays a real multichannel MIDI file through the plugin exactly as a host would,
// then checks that every channel carrying a Program Change ended on that program
// and that its notes sounded with it. Expectations come from the file itself, so
// this works for any corpus rip rather than one hard-coded song.
int runGameRipScenario(const juce::File& bank, const juce::File& midiFile)
{
    constexpr int blockSize{512};
    constexpr double sampleRate{48000.0};

    juce::FileInputStream stream{midiFile};
    juce::MidiFile file;
    if (stream.failedToOpen() || !file.readFrom(stream)) {
        std::fprintf(stderr, "could not read MIDI file: %s\n",
                     midiFile.getFullPathName().toRawUTF8());
        return 2;
    }
    file.convertTimestampTicksToSeconds();

    juce::MidiMessageSequence merged;
    for (int track = 0; track < file.getNumTracks(); ++track)
        merged.addSequence(*file.getTrack(track), 0.0);
    merged.updateMatchedPairs();
    merged.sort();

    // Last Program Change per channel, and whether the channel ever plays.
    std::map<int, int> expectedProgram;
    std::set<int> channelsWithNotes;
    for (int i = 0; i < merged.getNumEvents(); ++i) {
        const auto& message{merged.getEventPointer(i)->message};
        if (message.isProgramChange())
            expectedProgram[message.getChannel() - 1] = message.getProgramChangeNumber();
        if (message.isNoteOn() && message.getVelocity() > 0)
            channelsWithNotes.insert(message.getChannel() - 1);
    }
    if (expectedProgram.empty()) {
        std::fprintf(stderr, "MIDI file contains no Program Change to verify\n");
        return 2;
    }

    JuicySFAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);
    const auto state{makeState(bank.getFullPathName())};
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto& model{processor.getFluidSynthModel()};

    check(model.getFontLoadStatus() == "loaded",
          "game-rip bank loads");

    juce::AudioBuffer<float> audio{2, blockSize};
    double renderedEnergy{0.0};
    int event{0};
    const double lastTimestamp{merged.getNumEvents() > 0
        ? merged.getEndTime() : 0.0};
    const int totalBlocks{
        static_cast<int>(lastTimestamp * sampleRate / blockSize) + 4};

    // No manual patch assignment anywhere: the file alone drives the instruments.
    for (int block = 0; block < totalBlocks; ++block) {
        const double blockStart{block * blockSize / sampleRate};
        const double blockEnd{(block + 1) * blockSize / sampleRate};
        juce::MidiBuffer midi;
        while (event < merged.getNumEvents()
               && merged.getEventPointer(event)->message.getTimeStamp() < blockEnd) {
            const auto& message{merged.getEventPointer(event)->message};
            const int position{juce::jlimit(
                0, blockSize - 1,
                static_cast<int>((message.getTimeStamp() - blockStart) * sampleRate))};
            midi.addEvent(message, position);
            ++event;
        }
        render(processor, audio, midi);
        model.handleUpdateNowIfNeeded();
        renderedEnergy += magnitude(audio, 0, blockSize);
    }

    check(renderedEnergy > 0.001, "the game rip produces audio");

    bool everyProgramSelected{true};
    juce::String mismatches;
    for (const auto& [channel, program] : expectedProgram) {
        int bankNumber{-1}, preset{-1};
        const bool ok{model.getChannelProgram(channel, bankNumber, preset)
                      && preset == program};
        if (!ok) {
            everyProgramSelected = false;
            mismatches << " ch" << (channel + 1) << " expected " << program
                       << " got " << preset;
        }
    }
    if (!mismatches.isEmpty())
        std::fprintf(stderr, "program mismatches:%s\n", mismatches.toRawUTF8());
    check(everyProgramSelected,
          "every channel ends on the program its Program Change selected");

    // Channel 10 keeps the percussion bank even though its program is 0, which is
    // what distinguishes a correct GM rip from a silent-drums regression.
    if (expectedProgram.count(9) > 0) {
        int drumBank{-1}, drumPreset{-1};
        check(model.getChannelProgram(9, drumBank, drumPreset) && drumBank == 128,
              "channel 10 stays in the percussion bank across the whole rip");
    }

    bool everyPlayedChannelSounded{true};
    for (const int channel : channelsWithNotes) {
        int noteBank{-1}, notePreset{-1}, noteSample{-1};
        if (!model.getLastDispatchedNoteOnProgram(channel, noteBank, notePreset, noteSample))
            everyPlayedChannelSounded = false;
        else if (expectedProgram.count(channel) > 0 && notePreset != expectedProgram[channel])
            everyPlayedChannelSounded = false;
    }
    check(everyPlayedChannelSounded,
          "every channel that plays notes did so with its selected program");

    check(model.getProgramApplyFailureMask() == 0u,
          "no channel recorded a failed program assignment during the rip");

    std::printf("== game_rip: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc == 4 && juce::String{argv[1]} == "--game-rip") {
        std::printf("== multichannel game rip ==\n");
        return runGameRipScenario(juce::File{argv[2]}, juce::File{argv[3]});
    }
    if (argc != 3) {
        std::fprintf(
            stderr,
            "usage: JuicySFEngineMidiTests <font.dls|sf2|sf3> <controller-fixture.csv>\n"
            "       JuicySFEngineMidiTests --game-rip <bank> <file.mid>\n");
        return 2;
    }

    constexpr int blockSize{1024};
    JuicySFAudioProcessor processor;
    processor.prepareToPlay(48000.0, blockSize);
    const auto state{makeState(argv[1])};
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    auto& model{processor.getFluidSynthModel()};
    juce::AudioBuffer<float> audio{2, blockSize};

    {
        const std::array<juce::String, 24> expectedParameterIds{
            "bank", "preset", "attack", "decay", "sustain", "release",
            "filterCutOff", "filterResonance",
            "progCh1", "progCh2", "progCh3", "progCh4",
            "progCh5", "progCh6", "progCh7", "progCh8",
            "progCh9", "progCh10", "progCh11", "progCh12",
            "progCh13", "progCh14", "progCh15", "progCh16"
        };
        const auto& parameters{processor.getParameters()};
        bool parameterContract{parameters.size() == expectedParameterIds.size()};
        for (std::size_t i = 0; parameterContract && i < expectedParameterIds.size(); ++i) {
            const auto* identified{
                dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[static_cast<int>(i)])};
            parameterContract = identified != nullptr
                && identified->paramID == expectedParameterIds[i]
                && identified->getVersionHint() == 1;
        }
        check(parameterContract,
              "Beta 1 parameter IDs, order, count, and version hints are frozen");
    }

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
        // A normal GM file may begin with a channel-10 drum note and no Bank
        // Select or Program Change. Prove the default is not merely saved state:
        // it must already produce percussion audio at the note timestamp.
        juce::MidiBuffer midi;
        midi.addEvent(
            juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 128);
        render(processor, audio, midi);
        int bank{-1}, preset{-1}, sample{-1};
        check(model.getLastDispatchedNoteOnProgram(9, bank, preset, sample)
                  && bank == 128 && preset == 0 && sample == 128
                  && magnitude(audio, 0, 128) == 0.0f
                  && magnitude(audio, 128, blockSize - 128) > 0.001f,
              "channel 10 plays the default percussion kit without Bank Select or Program Change");

        juce::MidiBuffer silence;
        silence.addEvent(juce::MidiMessage::controllerEvent(10, 120, 0), 0);
        render(processor, audio, silence);
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

    std::printf("== Bank Select state ==\n");
    {
        // The macOS system DLS has program 38 in both bank 0 and bank 1. Beta 1
        // pins FluidSynth's GS mode: CC32 is retained but ignored for bank
        // selection, while CC0 affects only the next Program Change.
        juce::MidiBuffer lsbOnly;
        lsbOnly.addEvent(juce::MidiMessage::controllerEvent(1, 32, 1), 10);
        lsbOnly.addEvent(juce::MidiMessage::programChange(1, 38), 20);
        render(processor, audio, lsbOnly);
        model.handleUpdateNowIfNeeded();
        int bankAfterLsb{-1}, presetAfterLsb{-1}, lsbValue{-1};
        const bool lsbIgnoredForBank{
            model.getChannelProgram(0, bankAfterLsb, presetAfterLsb)
            && model.getControllerValue(0, 32, lsbValue)
            && bankAfterLsb == 0 && presetAfterLsb == 38 && lsbValue == 1};
        check(lsbIgnoredForBank,
              "GS mode stores CC32 but ignores it when selecting the next program bank");

        juce::MidiBuffer msbWithoutProgram;
        msbWithoutProgram.addEvent(juce::MidiMessage::controllerEvent(1, 0, 1), 10);
        render(processor, audio, msbWithoutProgram);
        int bankBeforeProgram{-1}, presetBeforeProgram{-1};
        int savedBankBeforeProgram{-1}, savedPresetBeforeProgram{-1};
        const bool bankSelectPendingOnly{
            model.getChannelProgram(0, bankBeforeProgram, presetBeforeProgram)
            && bankBeforeProgram == 1
            && getSavedChannelProgram(
                processor, 0, savedBankBeforeProgram, savedPresetBeforeProgram)
            && savedBankBeforeProgram == 0 && savedPresetBeforeProgram == 38};
        check(bankSelectPendingOnly,
              "CC0 updates FluidSynth's pending bank without claiming a persisted patch change");

        juce::MidiBuffer selectBankOne;
        selectBankOne.addEvent(juce::MidiMessage::programChange(1, 38), 20);
        selectBankOne.addEvent(
            juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 21);
        render(processor, audio, selectBankOne);
        model.handleUpdateNowIfNeeded();
        int bankAfterProgram{-1}, presetAfterProgram{-1};
        int noteBank{-1}, notePreset{-1}, noteSample{-1};
        int savedBank{-1}, savedPreset{-1};
        auto* bankParam{findIntParameter(processor, "bank")};
        auto* presetParam{findIntParameter(processor, "preset")};
        check(model.getChannelProgram(0, bankAfterProgram, presetAfterProgram)
                  && bankAfterProgram == 1 && presetAfterProgram == 38
                  && model.getLastDispatchedNoteOnProgram(
                      0, noteBank, notePreset, noteSample)
                  && noteBank == 1 && notePreset == 38 && noteSample == 21
                  && getSavedChannelProgram(
                      processor, 0, savedBank, savedPreset)
                  && savedBank == 1 && savedPreset == 38
                  && bankParam != nullptr && bankParam->get() == 1
                  && presetParam != nullptr && presetParam->get() == 38,
              "pinned GS Bank Select is deferred until Program Change and synchronizes engine, note, UI, and state");
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
    {
        // The state-level checks above prove the engine reports the new program.
        // This proves it in the audio domain: a Program Change placed mid-block
        // must render the following note with the new instrument, sample for
        // sample, not merely update the reported state.
        constexpr int pianoProgram{0};
        constexpr int organProgram{19};
        constexpr int notePosition{256};

        const auto renderWithProgram =
            [&](int program, int programPosition, juce::AudioBuffer<float>& out) {
                JuicySFAudioProcessor isolated;
                isolated.prepareToPlay(48000.0, blockSize);
                const auto isolatedState{makeState(argv[1])};
                isolated.setStateInformation(
                    isolatedState.getData(), static_cast<int>(isolatedState.getSize()));
                juce::MidiBuffer midi;
                addAllSoundOff(midi);
                midi.addEvent(
                    juce::MidiMessage::programChange(1, program), programPosition);
                midi.addEvent(
                    juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)),
                    notePosition);
                out.setSize(2, blockSize);
                render(isolated, out, midi);
            };

        juce::AudioBuffer<float> pianoAtBlockStart, organAtBlockStart, organMidBlock;
        renderWithProgram(pianoProgram, 0, pianoAtBlockStart);
        renderWithProgram(organProgram, 0, organAtBlockStart);
        // One sample before the note, so the two renderings share identical synth
        // state at the note and must agree exactly if the timestamp is honoured.
        renderWithProgram(organProgram, notePosition - 1, organMidBlock);

        const int tail{blockSize - notePosition};
        const double differentPrograms{
            waveformCorrelation(pianoAtBlockStart, organAtBlockStart, notePosition, tail)};
        const double sameProgram{
            waveformCorrelation(organAtBlockStart, organMidBlock, notePosition, tail)};

        check(magnitude(organAtBlockStart, notePosition, tail) > 0.001f
                  && differentPrograms < 0.9,
              "two different programs render audibly different audio for the same note");
        check(sameProgram > 0.999,
              "a Program Change one sample before a note renders identically to one at block start");
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

    std::printf("== unified program state ==\n");
    {
        model.handleUpdateNowIfNeeded();
        int savedBank{-1}, savedPreset{-1};
        auto* progCh16{findIntParameter(processor, "progCh16")};
        check(getSavedChannelProgram(processor, 15, savedBank, savedPreset)
                  && savedBank == 0 && savedPreset == 7
                  && progCh16 != nullptr && progCh16->get() == 7,
              "raw MIDI Program Change synchronizes engine, channel state, and progChN");
    }
    {
        auto* progCh3{findIntParameter(processor, "progCh3")};
        if (progCh3 != nullptr)
            *progCh3 = 4;
        model.handleUpdateNowIfNeeded();
        int bank{-1}, preset{-1}, savedBank{-1}, savedPreset{-1};
        check(progCh3 != nullptr
                  && model.getChannelProgram(2, bank, preset) && bank == 0 && preset == 4
                  && getSavedChannelProgram(processor, 2, savedBank, savedPreset)
                  && savedBank == 0 && savedPreset == 4,
              "progChN host automation uses the same engine and persisted-state path");
    }
    {
        auto* bankParam{findIntParameter(processor, "bank")};
        auto* presetParam{findIntParameter(processor, "preset")};
        if (bankParam != nullptr)
            *bankParam = 0;
        if (presetParam != nullptr)
            *presetParam = 5;
        int bank{-1}, preset{-1}, savedBank{-1}, savedPreset{-1};
        auto* progCh1{findIntParameter(processor, "progCh1")};
        check(bankParam != nullptr && presetParam != nullptr
                  && model.getChannelProgram(0, bank, preset) && bank == 0 && preset == 5
                  && getSavedChannelProgram(processor, 0, savedBank, savedPreset)
                  && savedBank == 0 && savedPreset == 5
                  && progCh1 != nullptr && progCh1->get() == 5,
              "selected-channel bank/preset automation synchronizes engine, state, and progChN");
    }
    {
        const bool applied{model.setChannelProgram(4, 0, 6)};
        int bank{-1}, preset{-1}, savedBank{-1}, savedPreset{-1};
        auto* progCh5{findIntParameter(processor, "progCh5")};
        check(applied
                  && model.getChannelProgram(4, bank, preset) && bank == 0 && preset == 6
                  && getSavedChannelProgram(processor, 4, savedBank, savedPreset)
                  && savedBank == 0 && savedPreset == 6
                  && progCh5 != nullptr && progCh5->get() == 6,
              "manual dropdown assignment synchronizes engine, state, and progChN");

        const bool rejected{!model.setChannelProgram(4, 129, 0)};
        int afterBank{-1}, afterPreset{-1}, afterSavedBank{-1}, afterSavedPreset{-1};
        check(rejected
                  && model.getChannelProgram(4, afterBank, afterPreset)
                  && afterBank == bank && afterPreset == preset
                  && getSavedChannelProgram(
                      processor, 4, afterSavedBank, afterSavedPreset)
                  && afterSavedBank == savedBank && afterSavedPreset == savedPreset
                  && (model.getProgramApplyFailureMask() & (1u << 4)) != 0,
              "rejected program assignment preserves engine/state and records its channel");
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
    {
        const juce::uint8 gmReset[]{0x7e, 0x7f, 0x09, 0x01};
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::programChange(6, 5), 10);
        midi.addEvent(juce::MidiMessage::controllerEvent(6, 74, 91), 11);
        midi.addEvent(
            juce::MidiMessage::createSysExMessage(gmReset, sizeof(gmReset)), 20);
        midi.addEvent(
            juce::MidiMessage::noteOn(6, 60, static_cast<juce::uint8>(100)), 20);
        render(processor, audio, midi);

        int noteBank{-1}, notePreset{-1}, noteSample{-1}, cutoff{-1};
        check(model.getLastDispatchedNoteOnProgram(
                  5, noteBank, notePreset, noteSample)
                  && noteBank == 0 && notePreset == 5 && noteSample == 20
                  && model.getControllerValue(5, 74, cutoff) && cutoff == 91,
              "reset reasserts program and sound controls before an equal-timestamp following note");
    }
    {
        const std::array<std::vector<juce::uint8>, 3> resets{{
            {0x7e, 0x7f, 0x09, 0x01},
            {0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41},
            {0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00}
        }};
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::programChange(7, 6), 5);
        midi.addEvent(juce::MidiMessage::controllerEvent(7, 71, 88), 6);
        for (std::size_t i = 0; i < resets.size(); ++i)
            midi.addEvent(
                juce::MidiMessage::createSysExMessage(
                    resets[i].data(), static_cast<int>(resets[i].size())),
                10 + static_cast<int>(i) * 10);
        midi.addEvent(
            juce::MidiMessage::noteOn(7, 60, static_cast<juce::uint8>(100)), 31);
        render(processor, audio, midi);

        int noteBank{-1}, notePreset{-1}, noteSample{-1}, resonance{-1};
        check(model.getLastDispatchedNoteOnProgram(
                  6, noteBank, notePreset, noteSample)
                  && noteBank == 0 && notePreset == 6 && noteSample == 31
                  && model.getControllerValue(6, 71, resonance) && resonance == 88,
              "multiple GM, GS, and XG resets preserve the latest program and sound-control snapshot");
    }
    {
        const std::array<std::vector<juce::uint8>, 3> resets{{
            {0x7e, 0x7f, 0x09, 0x01},
            {0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41},
            {0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00}
        }};
        bool replayStable{true};
        for (std::size_t restart = 0; restart < resets.size(); ++restart) {
            const int expectedPreset{2 + static_cast<int>(restart)};
            const int expectedCutoff{70 + static_cast<int>(restart)};
            juce::MidiBuffer midi;
            addAllSoundOff(midi);
            midi.addEvent(
                juce::MidiMessage::createSysExMessage(
                    resets[restart].data(), static_cast<int>(resets[restart].size())), 0);
            midi.addEvent(juce::MidiMessage::programChange(8, expectedPreset), 1);
            midi.addEvent(
                juce::MidiMessage::controllerEvent(8, 74, expectedCutoff), 2);
            midi.addEvent(
                juce::MidiMessage::noteOn(8, 60, static_cast<juce::uint8>(100)), 3);
            render(processor, audio, midi);
            model.handleUpdateNowIfNeeded();

            int noteBank{-1}, notePreset{-1}, noteSample{-1};
            int engineBank{-1}, enginePreset{-1}, cutoff{-1};
            int savedBank{-1}, savedPreset{-1};
            replayStable = replayStable
                && model.getLastDispatchedNoteOnProgram(
                    7, noteBank, notePreset, noteSample)
                && noteBank == 0 && notePreset == expectedPreset && noteSample == 3
                && model.getChannelProgram(7, engineBank, enginePreset)
                && engineBank == 0 && enginePreset == expectedPreset
                && model.getControllerValue(7, 74, cutoff) && cutoff == expectedCutoff
                && getSavedChannelProgram(processor, 7, savedBank, savedPreset)
                && savedBank == 0 && savedPreset == expectedPreset;
        }
        check(replayStable,
              "repeated restart-style reset/setup playback keeps note, engine, controller, and saved state current");
    }

    {
        // processBlock dispatches SysEx straight from the MidiBuffer once a
        // message exceeds MidiMessage's four-byte inline storage, and through
        // MidiMessage below it. Both routes must behave identically, and a
        // non-reset SysEx must never reassert programs.
        juce::MidiBuffer setup;
        addAllSoundOff(setup);
        setup.addEvent(juce::MidiMessage::programChange(3, 42), 0);
        render(processor, audio, setup);
        model.handleUpdateNowIfNeeded();

        // 3 payload bytes -> 5 framed bytes: the raw-pointer route.
        const juce::uint8 longUnknown[]{0x7d, 0x01, 0x02};
        // 1 payload byte -> 3 framed bytes: the MidiMessage route.
        const juce::uint8 shortUnknown[]{0x7d};
        juce::MidiBuffer unknownSysEx;
        unknownSysEx.addEvent(
            juce::MidiMessage::createSysExMessage(longUnknown, sizeof(longUnknown)), 5);
        unknownSysEx.addEvent(
            juce::MidiMessage::createSysExMessage(shortUnknown, sizeof(shortUnknown)), 6);
        unknownSysEx.addEvent(juce::MidiMessage::programChange(3, 7), 10);
        unknownSysEx.addEvent(
            juce::MidiMessage::noteOn(3, 60, static_cast<juce::uint8>(100)), 20);
        render(processor, audio, unknownSysEx);
        model.handleUpdateNowIfNeeded();

        int bank{-1}, preset{-1}, noteBank{-1}, notePreset{-1}, noteSample{-1};
        check(model.getChannelProgram(2, bank, preset)
                  && bank == 0 && preset == 7
                  && model.getLastDispatchedNoteOnProgram(
                      2, noteBank, notePreset, noteSample)
                  && noteBank == 0 && notePreset == 7 && noteSample == 20,
              "unknown SysEx on either dispatch route is forwarded without reasserting programs");

        // The same reset payload either side of the four-byte boundary: the GM
        // family is 4 payload bytes (6 framed), so exercise it against a
        // deliberately truncated 2-byte variant that must not match.
        const juce::uint8 gmReset[]{0x7e, 0x7f, 0x09, 0x01};
        const juce::uint8 truncatedReset[]{0x7e, 0x7f};
        juce::MidiBuffer resetPair;
        resetPair.addEvent(juce::MidiMessage::programChange(3, 11), 0);
        resetPair.addEvent(
            juce::MidiMessage::createSysExMessage(
                truncatedReset, sizeof(truncatedReset)), 5);
        resetPair.addEvent(
            juce::MidiMessage::createSysExMessage(gmReset, sizeof(gmReset)), 10);
        resetPair.addEvent(
            juce::MidiMessage::noteOn(3, 62, static_cast<juce::uint8>(100)), 15);
        render(processor, audio, resetPair);
        model.handleUpdateNowIfNeeded();

        int resetBank{-1}, resetPreset{-1};
        int resetNoteBank{-1}, resetNotePreset{-1}, resetNoteSample{-1};
        check(model.getChannelProgram(2, resetBank, resetPreset)
                  && resetBank == 0 && resetPreset == 11
                  && model.getLastDispatchedNoteOnProgram(
                      2, resetNoteBank, resetNotePreset, resetNoteSample)
                  && resetNoteBank == 0 && resetNotePreset == 11
                  && resetNoteSample == 15,
              "a framed GM reset dispatched from buffer storage still reasserts the current program");
    }

    std::printf("== controller and pitch-bend fidelity ==\n");
    {
        std::vector<ControllerFixtureEvent> events;
        juce::String fixtureError;
        const bool loaded{loadControllerFixture(juce::File{argv[2]}, events, fixtureError)};
        check(loaded, "deterministic controller fixture parses and validates");
        if (!loaded) {
            std::fprintf(stderr, "fixture error: %s\n", fixtureError.toRawUTF8());
        } else {
            // CC124-127 may reconfigure FluidSynth's basic-channel groups. Run the
            // canonical trace in a disposable instance so its defined side effects
            // cannot alter later independent scenarios.
            JuicySFAudioProcessor fixtureProcessor;
            fixtureProcessor.prepareToPlay(48000.0, blockSize);
            const auto fixtureState{makeState(argv[1])};
            fixtureProcessor.setStateInformation(
                fixtureState.getData(), static_cast<int>(fixtureState.getSize()));
            auto& fixtureModel{fixtureProcessor.getFluidSynthModel()};
            juce::AudioBuffer<float> fixtureAudio{2, blockSize};
            juce::MidiBuffer midi;
            for (const auto& event : events) {
                midi.addEvent(
                    event.pitchBend
                        ? juce::MidiMessage::pitchWheel(
                            event.channel, event.numberOrBend)
                        : juce::MidiMessage::controllerEvent(
                            event.channel, event.numberOrBend, event.value),
                    event.sample);
            }
            render(fixtureProcessor, fixtureAudio, midi);
            fixtureModel.handleUpdateNowIfNeeded();

            bool traceExact{true};
            for (const auto& event : events) {
                if (event.pitchBend) {
                    int actual{-1};
                    traceExact = traceExact
                        && fixtureModel.getPitchBend(event.channel - 1, actual)
                        && actual == event.numberOrBend;
                } else {
                    int actual{-1}, sample{-1};
                    traceExact = traceExact
                        && fixtureModel.getLastDispatchedController(
                            event.channel - 1,
                            event.numberOrBend,
                            actual,
                            sample)
                        && actual == event.value && sample == event.sample;
                }
            }
            int bendRange{-1};
            traceExact = traceExact
                && fixtureModel.getPitchWheelSensitivity(2, bendRange)
                && bendRange == 12;
            check(traceExact,
                  "offline fixture matches its exact controller timestamp/value/channel, bend, and ordered RPN trace");

        }
    }
    {
        // Put channel 1 (the basic channel capable of changing the whole group)
        // last. Recreate the disposable engine for every value so CC124-127 never
        // pollutes the next parameterized case.
        constexpr std::array<int, 4> channels{15, 9, 1, 0};
        constexpr std::array<int, 7> values{0, 1, 63, 64, 65, 126, 127};
        bool exhaustive{true};
        for (const int expected : values) {
            JuicySFAudioProcessor exhaustiveProcessor;
            exhaustiveProcessor.prepareToPlay(48000.0, blockSize);
            const auto exhaustiveState{makeState(argv[1])};
            exhaustiveProcessor.setStateInformation(
                exhaustiveState.getData(), static_cast<int>(exhaustiveState.getSize()));
            auto& exhaustiveModel{exhaustiveProcessor.getFluidSynthModel()};
            juce::AudioBuffer<float> exhaustiveAudio{2, blockSize};
            juce::MidiBuffer midi;
            for (const int channel : channels)
                for (int cc = 0; cc < 128; ++cc)
                    midi.addEvent(
                        juce::MidiMessage::controllerEvent(channel + 1, cc, expected),
                        100 + (15 - channel));
            render(exhaustiveProcessor, exhaustiveAudio, midi);

            for (const int channel : channels) {
                for (int cc = 0; cc < 128; ++cc) {
                    int actual{-1}, sample{-1};
                    exhaustive = exhaustive
                        && exhaustiveModel.getLastDispatchedController(
                            channel, cc, actual, sample)
                        && actual == expected && sample == 100 + (15 - channel);
                }
            }
        }
        check(exhaustive,
              "CC0-CC127 preserve all required values, timestamps, and channels 1, 2, 10, and 16");

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
        struct ExpectedContract {
            int cc;
            const char* parameter;
            int generator;
            double amount;
        };
        constexpr std::array<ExpectedContract, 6> expectedContracts{{
            {71, "filterResonance", GEN_FILTERQ, FLUID_PEAK_ATTENUATION},
            {72, "release", GEN_VOLENVRELEASE, 12000.0},
            {73, "attack", GEN_VOLENVATTACK, 12000.0},
            {74, "filterCutOff", GEN_FILTERFC, 2400.0},
            {75, "decay", GEN_VOLENVDECAY, 12000.0},
            {79, "sustain", GEN_VOLENVSUSTAIN, -1000.0},
        }};
        const int expectedFlags{
            FLUID_MOD_CC | FLUID_MOD_BIPOLAR | FLUID_MOD_LINEAR | FLUID_MOD_POSITIVE};
        bool contractExact{model.soundControllerModulatorsReady()};
        for (const auto& expected : expectedContracts) {
            FluidSynthModel::SoundControllerContract actual;
            contractExact = contractExact
                && FluidSynthModel::getSoundControllerContract(expected.cc, actual)
                && actual.controller == expected.cc
                && actual.parameterId == expected.parameter
                && actual.generator == expected.generator
                && std::abs(actual.amount - expected.amount) < 1.0e-9
                && actual.sourceFlags == expectedFlags;
        }
        FluidSynthModel::SoundControllerContract unsupported;
        contractExact = contractExact
            && !FluidSynthModel::getSoundControllerContract(70, unsupported);

        // FluidSynth's pinned linear bipolar map divides by 128: 64 maps to
        // exactly zero, values below it are negative, and values above are
        // positive. The configured amount sign therefore freezes each UI direction.
        const auto bipolarMap = [](int value) {
            const double normalised{static_cast<double>(value) / 128.0};
            return value == 127 ? normalised : -1.0 + 2.0 * normalised;
        };
        bool neutralAndDirectionExact{
            bipolarMap(64) == 0.0 && bipolarMap(0) < 0.0 && bipolarMap(127) > 0.0};
        for (const auto& expected : expectedContracts) {
            const double below{expected.amount * bipolarMap(0)};
            const double above{expected.amount * bipolarMap(127)};
            neutralAndDirectionExact = neutralAndDirectionExact
                && (expected.cc == 79
                    ? below > 0.0 && above < 0.0 // attenuation: slider up is louder
                    : below < 0.0 && above > 0.0);
        }
        check(contractExact && neutralAndDirectionExact,
              "installed sound-controller modulators freeze neutral 64 and the documented destination/direction contract");

        JuicySFAudioProcessor fresh;
        fresh.prepareToPlay(48000.0, blockSize);
        const auto freshState{makeState(argv[1])};
        fresh.setStateInformation(freshState.getData(), static_cast<int>(freshState.getSize()));
        bool freshNeutral{fresh.getFluidSynthModel().soundControllerModulatorsReady()};
        for (const auto& expected : expectedContracts) {
            int controllerValue{-1};
            const auto* parameter{findIntParameter(fresh, expected.parameter)};
            freshNeutral = freshNeutral
                && fresh.getFluidSynthModel().getControllerValue(0, expected.cc, controllerValue)
                && controllerValue == 64
                && parameter != nullptr && parameter->get() == 64;
        }
        check(freshNeutral,
              "fresh engine, saved channel state, and visible sound parameters all start at neutral 64");
    }
    {
        constexpr std::array<int, 6> soundCcs{71, 72, 73, 74, 75, 79};
        constexpr std::array<const char*, 6> soundParams{
            "filterResonance", "release", "attack", "filterCutOff", "decay", "sustain"};
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

        bool engineAndTimestampExact{true};
        for (int channel = 0; channel < 16; ++channel) {
            for (std::size_t index = 0; index < soundCcs.size(); ++index) {
                int actual{-1}, dispatched{-1}, sample{-1};
                const int expected{
                    (channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                engineAndTimestampExact = engineAndTimestampExact
                    && model.getControllerValue(channel, soundCcs[index], actual)
                    && actual == expected
                    && model.getLastDispatchedController(
                        channel, soundCcs[index], dispatched, sample)
                    && dispatched == expected && sample == 200 + channel;
            }
        }

        bool selectedChannelMirrorsExact{true};
        for (int channel = 0; channel < 16; ++channel) {
            model.selectChannelForEditing(channel);
            for (std::size_t index = 0; index < soundParams.size(); ++index) {
                const int expected{
                    (channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                auto* parameter{findIntParameter(processor, soundParams[index])};
                int dispatched{-1}, sample{-1};
                selectedChannelMirrorsExact = selectedChannelMirrorsExact
                    && parameter != nullptr && parameter->get() == expected
                    // Merely selecting a channel must not send a duplicate CC back
                    // to FluidSynth; the last raw MIDI diagnostic remains unchanged.
                    && model.getLastDispatchedController(
                        channel, soundCcs[index], dispatched, sample)
                    && dispatched == expected && sample == 200 + channel;
            }
        }

        model.selectChannelForEditing(0);
        const auto* selectedCutoff{findIntParameter(processor, "filterCutOff")};
        const int selectedBefore{selectedCutoff != nullptr ? selectedCutoff->get() : -1};
        juce::MidiBuffer unselectedMidi;
        unselectedMidi.addEvent(juce::MidiMessage::controllerEvent(16, 74, 127), 333);
        render(processor, audio, unselectedMidi);
        model.handleUpdateNowIfNeeded();
        const bool unselectedDidNotMoveSlider{
            selectedCutoff != nullptr && selectedCutoff->get() == selectedBefore};
        model.selectChannelForEditing(15);
        const bool selectingRevealsLatest{
            selectedCutoff != nullptr && selectedCutoff->get() == 127};

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        JuicySFAudioProcessor restored;
        restored.prepareToPlay(48000.0, blockSize);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        bool restoredExact{true};
        auto& restoredModel{restored.getFluidSynthModel()};
        for (int channel = 0; channel < 16; ++channel) {
            restoredModel.selectChannelForEditing(channel);
            for (std::size_t index = 0; index < soundCcs.size(); ++index) {
                int actual{-1};
                const int expected{channel == 15 && soundCcs[index] == 74
                    ? 127
                    : (channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                const auto* parameter{findIntParameter(restored, soundParams[index])};
                restoredExact = restoredExact
                    && restoredModel.getControllerValue(
                        channel, soundCcs[index], actual)
                    && actual == expected
                    && parameter != nullptr && parameter->get() == expected;
            }
        }
        check(engineAndTimestampExact && selectedChannelMirrorsExact
                  && unselectedDidNotMoveSlider && selectingRevealsLatest
                  && restoredExact,
              "all six exposed sound controllers remain timestamp-, engine-, slider-, channel-, and state-exact on all 16 channels");
    }
    {
        // MIDI CC121 deliberately follows FluidSynth/MIDI reset semantics: it
        // releases pedals and resets expression/RPN selection/pitch wheel, while
        // preserving bank, volume, pan, effects sends, bend range, and CC70-79.
        constexpr int channel{4};
        juce::MidiBuffer setup;
        for (const auto [cc, value] : std::array<std::pair<int, int>, 13>{
                 std::pair{1, 99}, std::pair{7, 77}, std::pair{11, 55},
                 std::pair{64, 127}, std::pair{71, 9}, std::pair{74, 111},
                 std::pair{91, 88}, std::pair{93, 89}, std::pair{101, 0},
                 std::pair{100, 0}, std::pair{6, 12}, std::pair{38, 0},
                 std::pair{65, 127}})
            setup.addEvent(juce::MidiMessage::controllerEvent(channel + 1, cc, value), 8);
        setup.addEvent(juce::MidiMessage::pitchWheel(channel + 1, 16383), 8);
        render(processor, audio, setup);
        model.handleUpdateNowIfNeeded();

        juce::MidiBuffer resetControllers;
        resetControllers.addEvent(
            juce::MidiMessage::controllerEvent(channel + 1, 121, 0), 123);
        render(processor, audio, resetControllers);
        model.handleUpdateNowIfNeeded();

        const auto controllerEquals = [&](int cc, int expected) {
            int actual{-1};
            return model.getControllerValue(channel, cc, actual) && actual == expected;
        };
        int bend{-1}, bendRange{-1};
        model.selectChannelForEditing(channel);
        const auto* resonance{findIntParameter(processor, "filterResonance")};
        const auto* cutoff{findIntParameter(processor, "filterCutOff")};
        check(controllerEquals(1, 0)
                  && controllerEquals(11, 127)
                  && controllerEquals(64, 0)
                  && controllerEquals(65, 0)
                  && controllerEquals(7, 77)
                  && controllerEquals(71, 9)
                  && controllerEquals(74, 111)
                  && controllerEquals(91, 88)
                  && controllerEquals(93, 89)
                  && controllerEquals(100, 127)
                  && controllerEquals(101, 127)
                  && model.getPitchBend(channel, bend) && bend == 8192
                  && model.getPitchWheelSensitivity(channel, bendRange) && bendRange == 12
                  && resonance != nullptr && resonance->get() == 9
                  && cutoff != nullptr && cutoff->get() == 111,
              "Reset All Controllers releases switches and resets expression/RPN/bend while preserving MIDI-defined persistent controls");
    }
    {
        JuicySFAudioProcessor pedalProcessor;
        pedalProcessor.prepareToPlay(48000.0, blockSize);
        const auto pedalState{makeState(argv[1])};
        pedalProcessor.setStateInformation(
            pedalState.getData(), static_cast<int>(pedalState.getSize()));
        auto& pedalModel{pedalProcessor.getFluidSynthModel()};
        juce::AudioBuffer<float> pedalAudio{2, blockSize};
        constexpr int channel{0};
        juce::MidiBuffer sustain;
        sustain.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 120, 0), 0);
        sustain.addEvent(
            juce::MidiMessage::noteOn(channel + 1, 60, static_cast<juce::uint8>(100)), 1);
        sustain.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 64, 127), 2);
        sustain.addEvent(juce::MidiMessage::noteOff(channel + 1, 60), 3);
        render(pedalProcessor, pedalAudio, sustain);
        FluidSynthModel::VoiceStateCounts held;
        bool sustainExact{pedalModel.getVoiceStateCounts(channel, held)
            && held.playing > 0 && held.on == 0 && held.sustained > 0};

        juce::MidiBuffer release;
        release.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 64, 0), 0);
        render(pedalProcessor, pedalAudio, release);
        int pedalValue{-1};
        sustainExact = sustainExact
            && pedalModel.getControllerValue(channel, 64, pedalValue)
            && pedalValue == 0;
        for (int block = 0; block < 256; ++block) {
            juce::MidiBuffer silence;
            render(pedalProcessor, pedalAudio, silence);
        }
        FluidSynthModel::VoiceStateCounts released;
        sustainExact = sustainExact
            && pedalModel.getVoiceStateCounts(channel, released)
            && released.playing == 0;
        if (!sustainExact)
            std::printf("    sustain counts held=%d/%d/%d/%d released=%d/%d/%d/%d\n",
                        held.playing, held.on, held.sustained, held.sostenuto,
                        released.playing, released.on, released.sustained, released.sostenuto);
        check(sustainExact,
              "sustain holds a released note and pedal-up removes its sustained voice state");
    }
    {
        JuicySFAudioProcessor pedalProcessor;
        pedalProcessor.prepareToPlay(48000.0, blockSize);
        const auto pedalState{makeState(argv[1])};
        pedalProcessor.setStateInformation(
            pedalState.getData(), static_cast<int>(pedalState.getSize()));
        auto& pedalModel{pedalProcessor.getFluidSynthModel()};
        juce::AudioBuffer<float> pedalAudio{2, blockSize};
        constexpr int channel{0};
        juce::MidiBuffer sostenuto;
        sostenuto.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 120, 0), 0);
        sostenuto.addEvent(
            juce::MidiMessage::noteOn(channel + 1, 60, static_cast<juce::uint8>(100)), 1);
        sostenuto.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 66, 127), 2);
        sostenuto.addEvent(juce::MidiMessage::noteOff(channel + 1, 60), 3);
        sostenuto.addEvent(
            juce::MidiMessage::noteOn(channel + 1, 64, static_cast<juce::uint8>(100)), 4);
        sostenuto.addEvent(juce::MidiMessage::noteOff(channel + 1, 64), 5);
        render(pedalProcessor, pedalAudio, sostenuto);
        FluidSynthModel::VoiceStateCounts held;
        bool sostenutoExact{pedalModel.getVoiceStateCounts(channel, held)
            && held.sostenuto > 0 && held.sustained == 0};

        juce::MidiBuffer release;
        release.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 66, 0), 0);
        render(pedalProcessor, pedalAudio, release);
        int pedalValue{-1};
        sostenutoExact = sostenutoExact
            && pedalModel.getControllerValue(channel, 66, pedalValue)
            && pedalValue == 0;
        for (int block = 0; block < 256; ++block) {
            juce::MidiBuffer silence;
            render(pedalProcessor, pedalAudio, silence);
        }
        FluidSynthModel::VoiceStateCounts released;
        sostenutoExact = sostenutoExact
            && pedalModel.getVoiceStateCounts(channel, released)
            && released.playing == 0;
        if (!sostenutoExact)
            std::printf("    sostenuto counts held=%d/%d/%d/%d released=%d/%d/%d/%d\n",
                        held.playing, held.on, held.sustained, held.sostenuto,
                        released.playing, released.on, released.sustained, released.sostenuto);
        check(sostenutoExact,
              "sostenuto captures only already-active voices and pedal-up removes their sostenuto state");
    }
    {
        constexpr int channel{8};
        juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::controllerEvent(channel + 1, 120, 0), 0);
        notes.addEvent(
            juce::MidiMessage::noteOn(channel + 1, 60, static_cast<juce::uint8>(100)), 1);
        notes.addEvent(
            juce::MidiMessage::noteOn(channel + 1, 64, static_cast<juce::uint8>(100)), 2);
        render(processor, audio, notes);
        FluidSynthModel::VoiceStateCounts before;
        bool channelModeExact{model.getVoiceStateCounts(channel, before) && before.on > 0};

        juce::MidiBuffer allNotesOff;
        allNotesOff.addEvent(
            juce::MidiMessage::controllerEvent(channel + 1, 123, 0), 111);
        render(processor, audio, allNotesOff);
        FluidSynthModel::VoiceStateCounts afterNotesOff;
        channelModeExact = channelModeExact
            && model.getVoiceStateCounts(channel, afterNotesOff)
            && afterNotesOff.on == 0;

        juce::MidiBuffer allSoundOff;
        allSoundOff.addEvent(
            juce::MidiMessage::controllerEvent(channel + 1, 120, 0), 222);
        render(processor, audio, allSoundOff);
        FluidSynthModel::VoiceStateCounts afterSoundOff;
        channelModeExact = channelModeExact
            && model.getVoiceStateCounts(channel, afterSoundOff)
            && afterSoundOff.playing == 0;
        check(channelModeExact,
              "All Notes Off releases active notes while All Sound Off immediately removes playing voices");
    }
    {
        constexpr std::array<int, 6> soundCcs{71, 72, 73, 74, 75, 79};
        constexpr std::array<int, 6> values{7, 21, 35, 81, 95, 109};
        constexpr std::array<std::array<juce::uint8, 9>, 3> resetBytes{{
            {{0x7e, 0x7f, 0x09, 0x01, 0, 0, 0, 0, 0}},
            {{0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41}},
            {{0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0, 0}},
        }};
        constexpr std::array<int, 3> resetSizes{4, 9, 7};
        constexpr int channel{5};
        juce::MidiBuffer setup;
        for (std::size_t i = 0; i < soundCcs.size(); ++i)
            setup.addEvent(
                juce::MidiMessage::controllerEvent(channel + 1, soundCcs[i], values[i]), 1);
        render(processor, audio, setup);
        model.handleUpdateNowIfNeeded();

        bool resetsPreserveLatest{true};
        for (std::size_t resetIndex = 0; resetIndex < resetBytes.size(); ++resetIndex) {
            juce::MidiBuffer reset;
            reset.addEvent(
                juce::MidiMessage::createSysExMessage(
                    resetBytes[resetIndex].data(), resetSizes[resetIndex]),
                0);
            render(processor, audio, reset);
            for (std::size_t i = 0; i < soundCcs.size(); ++i) {
                int actual{-1};
                resetsPreserveLatest = resetsPreserveLatest
                    && model.getControllerValue(channel, soundCcs[i], actual)
                    && actual == values[i];
            }
        }
        check(resetsPreserveLatest,
              "GM, GS, and XG resets reapply the latest plugin-owned sound controls instead of changing their direction or neutral model");
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
        for (const double sampleRate : {44100.0, 48000.0, 88200.0, 96000.0}) {
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
              "audio-domain full-down/center/full-up bends follow a twelve-semitone RPN range at 44.1, 48, 88.2, and 96 kHz");

        // RPN 0,0 carries semitones in Data Entry MSB and cents in the LSB. The
        // diagnostic API reports whole semitones only, so whether the cents are
        // honoured can be settled in the audio domain and nowhere else. Measure
        // it rather than claiming or denying 14-bit support.
        {
            constexpr double sampleRate{48000.0};
            processor.prepareToPlay(sampleRate, blockSize);
            const auto measureFullUpRatio{[&](int semitones, int cents) {
                double reference{0.0}, bent{0.0};
                for (int pass = 0; pass < 2; ++pass) {
                    juce::MidiBuffer midi;
                    addAllSoundOff(midi);
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 101, 0), 0);
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 100, 0), 0);
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 6, semitones), 0);
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 38, cents), 0);
                    midi.addEvent(
                        juce::MidiMessage::pitchWheel(1, pass == 0 ? 8192 : 16383), 0);
                    midi.addEvent(
                        juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
                    render(processor, pitchAudio, midi);
                    const double expected{pass == 0 ? 440.0
                        : 440.0 * std::pow(2.0, semitones / 12.0)};
                    const double measured{estimatePeriodicFrequency(
                        pitchAudio,
                        static_cast<int>(sampleRate * 0.2),
                        static_cast<int>(sampleRate * 0.3),
                        sampleRate, expected)};
                    (pass == 0 ? reference : bent) = measured;
                }
                return bent / reference;
            }};

            const double wholeRatio{measureFullUpRatio(2, 0)};
            const double centsRatio{measureFullUpRatio(2, 50)};
            const double expectedWhole{std::pow(2.0, 2.0 * 8191.0 / (12.0 * 8192.0))};
            const double expectedWithCents{
                std::pow(2.0, 2.5 * 8191.0 / (12.0 * 8192.0))};
            const bool centsHonoured{
                std::abs(centsRatio - expectedWithCents)
                    < std::abs(centsRatio - expectedWhole)};
            std::printf(
                "    RPN 0,0 full-up ratio: 2 semitones %.4f (expect %.4f), "
                "2 semitones + 50 cents %.4f (expect %.4f with cents, %.4f without)\n",
                wholeRatio, expectedWhole, centsRatio, expectedWithCents, expectedWhole);
            std::printf("    Data Entry LSB cents are %s by FluidSynth 2.5.5\n",
                        centsHonoured ? "honoured" : "ignored");

            check(std::abs(wholeRatio - expectedWhole) < 0.02,
                  "a two-semitone RPN range bends by two semitones in the audio domain");
            // Whichever way it resolves, the behaviour must be stable and
            // documented; CONTROLLER_SUPPORT.md records the observed answer.
            check(centsHonoured
                      ? std::abs(centsRatio - expectedWithCents) < 0.02
                      : std::abs(centsRatio - expectedWhole) < 0.02,
                  "Data Entry LSB cents behave consistently with the documented contract");
        }

        processor.prepareToPlay(192000.0, blockSize);
        juce::MidiBuffer unsupportedRateMidi;
        unsupportedRateMidi.addEvent(
            juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        render(processor, pitchAudio, unsupportedRateMidi);
        check(!model.isSampleRateSupported()
                  && magnitude(pitchAudio, 0, pitchAudio.getNumSamples()) == 0.0f,
              "unsupported 192 kHz playback fails silent instead of rendering at the wrong pitch");

        processor.prepareToPlay(48000.0, blockSize);
        juce::MidiBuffer recoveredRateMidi;
        recoveredRateMidi.addEvent(
            juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        render(processor, pitchAudio, recoveredRateMidi);
        check(model.isSampleRateSupported()
                  && magnitude(pitchAudio, 8192, 8192) > 0.001f,
              "returning from an unsupported rate recreates the engine and resumes audio");
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
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        const auto xml{juce::AudioProcessor::getXmlFromBinary(
            saved.getData(), static_cast<int>(saved.getSize()))};
        const auto* params{xml != nullptr ? xml->getChildByName("params") : nullptr};
        const auto* channels{
            xml != nullptr ? xml->getChildByName("channelPrograms") : nullptr};
        const auto* font{xml != nullptr ? xml->getChildByName("soundFont") : nullptr};
        bool allParams{params != nullptr};
        for (const auto& id : std::array<juce::String, 24>{
                 "bank", "preset", "attack", "decay", "sustain", "release",
                 "filterCutOff", "filterResonance",
                 "progCh1", "progCh2", "progCh3", "progCh4",
                 "progCh5", "progCh6", "progCh7", "progCh8",
                 "progCh9", "progCh10", "progCh11", "progCh12",
                 "progCh13", "progCh14", "progCh15", "progCh16"})
            allParams = allParams && params->hasAttribute(id);
        check(xml != nullptr && xml->hasTagName("MYPLUGINSETTINGS")
                  && xml->getIntAttribute("stateVersion", -1) == 2
                  && allParams && channels != nullptr
                  && channels->getNumChildElements() == 16
                  && font != nullptr && font->hasAttribute("path")
                  && font->hasAttribute("bookmark"),
              "Beta 1 state writer preserves the frozen schema-2 envelope");
    }
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
#if JUCE_MAC
    {
        // A saved session can carry a security-scoped bookmark that no longer
        // resolves, or that resolves to a file which no longer loads. Either way
        // the stored path must still be tried, or the user's bank silently
        // disappears on project reload.
        const auto makeBookmarkedState =
            [&](const juce::String& path, const juce::MemoryBlock& bookmarkBytes) {
                juce::XmlElement xml{"MYPLUGINSETTINGS"};
                xml.setAttribute("stateVersion", 2);
                auto* ui{xml.createNewChildElement("uiState")};
                ui->setAttribute("width", 850);
                ui->setAttribute("height", 650);
                ui->setAttribute("selectedChannel", 1);
                auto* font{xml.createNewChildElement("soundFont")};
                font->setAttribute("path", path);
                font->setAttribute("bookmark", bookmarkBytes.toBase64Encoding());
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(xml, block);
                return block;
            };

        juce::MemoryBlock corruptBookmark{64, true};
        auto* bookmarkBytes{static_cast<juce::uint8*>(corruptBookmark.getData())};
        for (size_t i = 0; i < corruptBookmark.getSize(); ++i)
            bookmarkBytes[i] = static_cast<juce::uint8>(0xa5 ^ i);

        JuicySFAudioProcessor recovered;
        recovered.prepareToPlay(48000.0, blockSize);
        const auto corruptState{makeBookmarkedState(argv[1], corruptBookmark)};
        recovered.setStateInformation(
            corruptState.getData(), static_cast<int>(corruptState.getSize()));
        auto& recoveredModel{recovered.getFluidSynthModel()};
        check(recoveredModel.getFontLoadStatus() == "loaded"
                  && recoveredModel.getLoadedFontPath() == juce::String{argv[1]},
              "an unresolvable security-scoped bookmark falls back to the saved path");

        juce::AudioBuffer<float> recoveredAudio{2, blockSize};
        juce::MidiBuffer recoveredMidi;
        recoveredMidi.addEvent(
            juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        render(recovered, recoveredAudio, recoveredMidi);
        check(magnitude(recoveredAudio, 0, blockSize) > 0.001f,
              "the bank recovered through the path fallback actually sounds");

        // Bookmark that cannot resolve AND a path that no longer exists: the
        // failure must be reported rather than silently reported as loaded.
        JuicySFAudioProcessor missing;
        missing.prepareToPlay(48000.0, blockSize);
        const auto missingState{makeBookmarkedState(
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("juicy16-does-not-exist.sf2").getFullPathName(),
            corruptBookmark)};
        missing.setStateInformation(
            missingState.getData(), static_cast<int>(missingState.getSize()));
        check(missing.getFluidSynthModel().getFontLoadStatus() == "error"
                  && !missing.getFluidSynthModel().isBookmarkStale(),
              "an unresolvable bookmark with a missing path reports an error and no stale flag");

        // The harder case, and the one that actually regressed: a bookmark that
        // RESOLVES but whose target is not a loadable bank. Resolution succeeding
        // used to be treated as success outright, so the saved path was never
        // tried and the user's bank vanished on reload.
        const juce::File decoy{
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("juicy16-not-a-bank.sf2")};
        decoy.replaceWithText("this is deliberately not a SoundFont");
        juce::MemoryBlock decoyBookmark;
        {
            const auto url{decoy.getFullPathName().toRawUTF8()};
            CFURLRef cfURL{CFURLCreateFromFileSystemRepresentation(
                nullptr, reinterpret_cast<const UInt8*>(url),
                static_cast<CFIndex>(std::strlen(url)), false)};
            if (cfURL != nullptr) {
                // Security scope first, as FilePicker creates them; a plain
                // bookmark still exercises the same resolve-then-load path.
                CFDataRef data{CFURLCreateBookmarkData(
                    nullptr, cfURL, kCFURLBookmarkCreationWithSecurityScope,
                    nullptr, nullptr, nullptr)};
                if (data == nullptr)
                    data = CFURLCreateBookmarkData(
                        nullptr, cfURL, 0, nullptr, nullptr, nullptr);
                if (data != nullptr) {
                    decoyBookmark.append(CFDataGetBytePtr(data),
                                         static_cast<size_t>(CFDataGetLength(data)));
                    CFRelease(data);
                }
                CFRelease(cfURL);
            }
        }

        if (decoyBookmark.getSize() == 0) {
            std::printf("  SKIP  could not create a bookmark for the resolve-then-fail case\n");
        } else {
            JuicySFAudioProcessor resolvedButUnloadable;
            resolvedButUnloadable.prepareToPlay(48000.0, blockSize);
            const auto decoyState{makeBookmarkedState(argv[1], decoyBookmark)};
            resolvedButUnloadable.setStateInformation(
                decoyState.getData(), static_cast<int>(decoyState.getSize()));
            auto& decoyModel{resolvedButUnloadable.getFluidSynthModel()};
            check(decoyModel.getFontLoadStatus() == "loaded"
                      && decoyModel.getLoadedFontPath() == juce::String{argv[1]},
                  "a bookmark that resolves to an unloadable file still falls back to the saved path");
        }
        decoy.deleteFile();
    }
#endif
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
    {
        const juce::String originalPath{model.getLoadedFontPath()};
        std::vector<juce::File> temporaryFiles;
        const auto makeTemp{[&](const juce::String& suffix) -> juce::File {
            auto file{juce::File::createTempFile(suffix)};
            temporaryFiles.push_back(file);
            return file;
        }};
        const auto rejectWithoutReplacing{[&](const juce::String& path) {
            int beforeBank{-1}, beforePreset{-1};
            const bool hadProgram{model.getChannelProgram(0, beforeBank, beforePreset)};
            const auto attemptedState{makeState(path)};
            processor.setStateInformation(
                attemptedState.getData(), static_cast<int>(attemptedState.getSize()));
            const bool rejected{model.getFontLoadStatus() == "error"};
            int afterBank{-1}, afterPreset{-1};
            return hadProgram && rejected
                && model.getFontLoadStatus() == "error"
                && model.getFontLoadMessage().isNotEmpty()
                && model.getLastAttemptedFontPath() == path
                && model.getLoadedFontPath() == originalPath
                && model.getChannelProgram(0, afterBank, afterPreset)
                && beforeBank == afterBank && beforePreset == afterPreset;
        }};

        const auto movedFile{makeTemp(".dls")};
        movedFile.replaceWithText("moved before restore");
        const auto movedPath{movedFile.getFullPathName()};
        movedFile.deleteFile();
        const bool movedHandled{rejectWithoutReplacing(movedPath)
            && model.getFontLoadMessage().containsIgnoreCase("missing")};

        const auto directoryPath{juce::File::getSpecialLocation(
            juce::File::tempDirectory).getNonexistentChildFile(
                "juicy16-bank-is-directory", {}, false)};
        directoryPath.createDirectory();
        const bool nonFileHandled{rejectWithoutReplacing(directoryPath.getFullPathName())
            && model.getFontLoadMessage().containsIgnoreCase("unreadable")};
        directoryPath.deleteRecursively();

        const auto unsupportedFile{makeTemp(".txt")};
        unsupportedFile.replaceWithText("This is not an SF2, SF3, or DLS bank.");
        const bool unsupportedHandled{rejectWithoutReplacing(
            unsupportedFile.getFullPathName())};

        const auto corruptFile{makeTemp(".sf2")};
        const std::array<juce::uint8, 16> corruptBytes{
            'R', 'I', 'F', 'F', 0xff, 0xff, 0xff, 0x7f,
            's', 'f', 'b', 'k', 'b', 'a', 'd', '!'};
        corruptFile.replaceWithData(corruptBytes.data(), corruptBytes.size());
        const bool corruptHandled{rejectWithoutReplacing(corruptFile.getFullPathName())};

        const auto emptyDlsFile{makeTemp(".dls")};
        const auto emptyDls{makeZeroInstrumentDls()};
        emptyDlsFile.replaceWithData(emptyDls.data(), emptyDls.size());
        const bool emptyDlsHandled{rejectWithoutReplacing(emptyDlsFile.getFullPathName())};

        bool unreadableHandled{true};
       #if ! JUCE_WINDOWS
        const auto unreadableFile{makeTemp(".sf2")};
        unreadableFile.replaceWithData(corruptBytes.data(), corruptBytes.size());
        const auto unreadablePath{unreadableFile.getFullPathName()};
        const bool permissionsRemoved{::chmod(unreadablePath.toRawUTF8(), 0000) == 0};
        unreadableHandled = permissionsRemoved && rejectWithoutReplacing(unreadablePath);
        ::chmod(unreadablePath.toRawUTF8(), 0600);
       #endif

        juce::MidiBuffer retainedBankMidi;
        retainedBankMidi.addEvent(juce::MidiMessage::controllerEvent(1, 120, 0), 0);
        retainedBankMidi.addEvent(juce::MidiMessage::programChange(1, 0), 1);
        retainedBankMidi.addEvent(
            juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 2);
        render(processor, audio, retainedBankMidi);
        juce::MemoryBlock savedAfterRejectedFiles;
        processor.getStateInformation(savedAfterRejectedFiles);
        const auto savedAfterRejectedXml{juce::AudioProcessor::getXmlFromBinary(
            savedAfterRejectedFiles.getData(),
            static_cast<int>(savedAfterRejectedFiles.getSize()))};
        const auto* savedAfterRejectedFont{savedAfterRejectedXml != nullptr
            ? savedAfterRejectedXml->getChildByName("soundFont") : nullptr};
        const bool activeBankRetained{savedAfterRejectedFont != nullptr
            && savedAfterRejectedFont->getStringAttribute("path") == originalPath
            && magnitude(audio, 2, blockSize - 2) > 0.001f};

        check(movedHandled && nonFileHandled && unsupportedHandled
                  && corruptHandled && emptyDlsHandled && unreadableHandled
                  && activeBankRetained,
              "missing/moved, non-file, unreadable, unsupported, corrupt, and zero-instrument banks fail visibly without replacing audio or saved state");

        for (const auto& file : temporaryFiles)
            if (file.existsAsFile())
                file.deleteFile();
    }
    {
        // Hostile and awkward file shapes a tester can realistically select.
        const juce::String originalPath{model.getLoadedFontPath()};
        std::vector<juce::File> temporaryFiles;
        const auto makeTemp{[&](const juce::String& suffix) -> juce::File {
            auto file{juce::File::createTempFile(suffix)};
            temporaryFiles.push_back(file);
            return file;
        }};
        const auto attempt{[&](const juce::String& path) {
            const auto attemptedState{makeState(path)};
            processor.setStateInformation(
                attemptedState.getData(), static_cast<int>(attemptedState.getSize()));
        }};

        const auto zeroByteFile{makeTemp(".dls")};
        zeroByteFile.replaceWithData("", 0);
        attempt(zeroByteFile.getFullPathName());
        const bool zeroByteHandled{model.getFontLoadStatus() == "error"
            && model.getLoadedFontPath() == originalPath};

        // Valid DLS header, body cut off part-way through.
        const auto truncatedFile{makeTemp(".dls")};
        {
            const std::array<juce::uint8, 20> truncated{
                'R', 'I', 'F', 'F', 0xff, 0xff, 0xff, 0x0f,
                'D', 'L', 'S', ' ', 'c', 'o', 'l', 'h',
                0x04, 0x00, 0x00, 0x00};
            truncatedFile.replaceWithData(truncated.data(), truncated.size());
        }
        attempt(truncatedFile.getFullPathName());
        const bool truncatedHandled{model.getFontLoadStatus() == "error"
            && model.getLoadedFontPath() == originalPath};

        // Sparse file that claims to be a DLS and is far larger than the repair
        // cap. Repair must decline it by size rather than reading it into memory.
        const auto hugeFile{makeTemp(".dls")};
        bool hugeCreated{false};
        {
            hugeFile.deleteFile();
            juce::FileOutputStream out{hugeFile};
            if (out.openedOk()) {
                const std::array<juce::uint8, 12> header{
                    'R', 'I', 'F', 'F', 0xff, 0xff, 0xff, 0xff,
                    'D', 'L', 'S', ' '};
                out.write(header.data(), header.size());
                // Sparse: seeking past the end costs no blocks on APFS.
                hugeCreated = out.setPosition(768ll * 1024 * 1024)
                    && out.writeByte(0);
                out.flush();
            }
        }
        const juce::int64 hugeStart{juce::Time::getMillisecondCounterHiRes() > 0.0
            ? static_cast<juce::int64>(juce::Time::getMillisecondCounterHiRes()) : 0};
        if (hugeCreated)
            attempt(hugeFile.getFullPathName());
        const juce::int64 hugeElapsed{
            static_cast<juce::int64>(juce::Time::getMillisecondCounterHiRes()) - hugeStart};
        const bool hugeHandled{!hugeCreated
            || (model.getFontLoadStatus() == "error"
                && model.getLoadedFontPath() == originalPath
                && hugeElapsed < 5000)};

        // A read-only bank is legitimate: repair writes to a temporary copy, so
        // the source never needs to be writable and the load must succeed.
        bool readOnlyHandled{true};
       #if ! JUCE_WINDOWS
        const auto readOnlyFile{makeTemp(".dls")};
        readOnlyFile.deleteFile();
        const bool readOnlyCopied{juce::File{argv[1]}.copyFileTo(readOnlyFile)};
        if (readOnlyCopied) {
            ::chmod(readOnlyFile.getFullPathName().toRawUTF8(), 0444);
            attempt(readOnlyFile.getFullPathName());
            readOnlyHandled = model.getFontLoadStatus() == "loaded"
                && model.getLoadedFontPath() == readOnlyFile.getFullPathName();
            ::chmod(readOnlyFile.getFullPathName().toRawUTF8(), 0600);
        }
       #endif

        // Removed after a successful load: the next restore of that path must
        // fail visibly instead of leaving a phantom bank selected.
        const auto vanishingFile{makeTemp(".dls")};
        vanishingFile.deleteFile();
        bool vanishHandled{true};
        if (juce::File{argv[1]}.copyFileTo(vanishingFile)) {
            attempt(vanishingFile.getFullPathName());
            const bool loadedFirst{model.getFontLoadStatus() == "loaded"};
            const auto vanishedPath{vanishingFile.getFullPathName()};
            vanishingFile.deleteFile();
            // A fresh instance, because reopening a project is how a user meets
            // this: restoring the same path into the same instance would not
            // even notify, the ValueTree property being unchanged.
            JuicySFAudioProcessor reopened;
            reopened.prepareToPlay(48000.0, blockSize);
            const auto vanishedState{makeState(vanishedPath)};
            reopened.setStateInformation(
                vanishedState.getData(), static_cast<int>(vanishedState.getSize()));
            vanishHandled = loadedFirst
                && reopened.getFluidSynthModel().getFontLoadStatus() == "error";
        }

        // Restore a known-good bank so later scenarios start from a loaded state.
        attempt(originalPath);

        check(zeroByteHandled, "a zero-byte bank is rejected without replacing the active one");
        check(truncatedHandled, "a truncated DLS is rejected without replacing the active one");
        check(hugeHandled, "an oversized DLS is declined by size instead of being read into memory");
        check(readOnlyHandled, "a read-only bank still loads, because repair writes to a temporary copy");
        check(vanishHandled, "a bank removed after loading fails visibly when restored again");
        check(model.getFontLoadStatus() == "loaded",
              "a known-good bank reloads after the hostile-input sequence");

        for (const auto& file : temporaryFiles) {
            if (file.exists()) {
               #if ! JUCE_WINDOWS
                ::chmod(file.getFullPathName().toRawUTF8(), 0600);
               #endif
                file.deleteFile();
            }
        }
    }
    {
        const juce::String originalPath{model.getLoadedFontPath()};
        const juce::String unicodeSegment{
            juce::String::fromUTF8("long-path-\xe9\x9f\xb3\xe8\x89\xb2-\xf0\x9f\x8e\xb9-")
            + juce::String::repeatedString("segment", 8)};
        auto longRoot{juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("juicy16-long-unicode-bank-path", {}, false)};
        auto longDirectory{longRoot.getChildFile(unicodeSegment)
            .getChildFile(unicodeSegment)};
        const bool directoryCreated{longDirectory.createDirectory().wasOk()};
        const auto longBank{longDirectory.getChildFile(
            juce::String::fromUTF8("bank-\xe9\x95\xb7\xe3\x81\x84-\xf0\x9f\x8e\xb9.dls"))};
        const bool copied{directoryCreated
            && juce::File{argv[1]}.copyFileTo(longBank)};
        const auto longState{makeState(longBank.getFullPathName())};
        if (copied)
            processor.setStateInformation(
                longState.getData(), static_cast<int>(longState.getSize()));

        juce::MidiBuffer longPathMidi;
        longPathMidi.addEvent(juce::MidiMessage::controllerEvent(1, 120, 0), 0);
        longPathMidi.addEvent(juce::MidiMessage::programChange(1, 0), 1);
        longPathMidi.addEvent(
            juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 2);
        render(processor, audio, longPathMidi);
        juce::MemoryBlock longPathSaved;
        processor.getStateInformation(longPathSaved);
        const auto longPathXml{juce::AudioProcessor::getXmlFromBinary(
            longPathSaved.getData(), static_cast<int>(longPathSaved.getSize()))};
        const auto* longPathFont{longPathXml != nullptr
            ? longPathXml->getChildByName("soundFont") : nullptr};
        const bool longPathLoaded{copied
            && longBank.getFullPathName().length() > 200
            && model.getFontLoadStatus() == "loaded"
            && model.getLoadedFontPath() == longBank.getFullPathName()
            && longPathFont != nullptr
            && longPathFont->getStringAttribute("path") == longBank.getFullPathName()
            && magnitude(audio, 2, blockSize - 2) > 0.001f};

        const auto restoreState{makeState(originalPath)};
        processor.setStateInformation(
            restoreState.getData(), static_cast<int>(restoreState.getSize()));
        const bool restored{model.getFontLoadStatus() == "loaded"
            && model.getLoadedFontPath() == originalPath};
        longRoot.deleteRecursively();
        check(longPathLoaded && restored,
              "a long nested Unicode bank path loads, renders, serializes exactly, and restores the prior bank");
    }

    std::printf("== colour contrast ==\n");
    {
        auto& lookAndFeel{juce::LookAndFeel::getDefaultLookAndFeel()};
        const juce::Colour rowText{lookAndFeel.findColour(juce::ListBox::textColourId)};
        const juce::Colour rowBackground{
            lookAndFeel.findColour(juce::ListBox::backgroundColourId)};
        const juce::Colour selectedRow{MyColours::getUIColourIfAvailable(
            juce::LookAndFeel_V4::ColourScheme::UIColour::highlightedFill,
            juce::Colours::steelblue)};
        const juce::Colour selectedText{MyColours::getUIColourIfAvailable(
            juce::LookAndFeel_V4::ColourScheme::UIColour::highlightedText,
            juce::Colours::white)};

        // 4.5:1 is the WCAG AA threshold for normal-size text.
        constexpr double minimumRatio{4.5};

        const double unselected{contrastRatio(rowText, rowBackground)};
        const double selected{contrastRatio(selectedText, selectedRow)};
        std::printf("    row text on unselected background: %.2f:1\n", unselected);
        std::printf("    row text on selected background:   %.2f:1\n", selected);
        check(unselected >= minimumRatio,
              "channel-row text meets WCAG AA contrast on an unselected row");
        check(selected >= minimumRatio,
              "channel-row text meets WCAG AA contrast on the selected row");

        const juce::Colour statusBackground{
            lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId)};
        const double normalStatus{
            contrastRatio(juce::Colours::lightgrey, statusBackground)};
        const double errorStatus{
            contrastRatio(juce::Colours::salmon.brighter(0.25f), statusBackground)};
        std::printf("    status label, normal: %.2f:1  error: %.2f:1\n",
                    normalStatus, errorStatus);
        check(normalStatus >= minimumRatio,
              "the status label meets WCAG AA contrast in its normal colour");
        check(errorStatus >= minimumRatio,
              "the status label meets WCAG AA contrast in its error colour");
    }

    std::printf("== accessibility metadata ==\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor{processor.createEditor()};
        struct ExpectedAccessibleComponent {
            const char* name;
            bool requireSliderRole;
            bool requireTableRole;
        };
        constexpr std::array<ExpectedAccessibleComponent, 10> expected{{
            {"Sound bank file", false, false},
            {"MIDI channel assignments", false, true},
            {"Attack (CC73)", true, false},
            {"Decay (CC75)", true, false},
            {"Sustain level (CC79)", true, false},
            {"Release (CC72)", true, false},
            {"Filter cutoff (CC74)", true, false},
            {"Filter resonance (CC71)", true, false},
            {"MIDI Keyboard", false, false},
            {"Version and bank load status", false, false},
        }};
        bool accessibleMetadata{editor != nullptr};
        for (const auto& item : expected) {
            auto* component{editor != nullptr
                ? findNamedComponent(*editor, item.name) : nullptr};
            // JUCE creates native accessibility handlers only after a component
            // has a peer/window handle. Keep this headless test deterministic by
            // checking the metadata consumed by those handlers and the concrete
            // built-in Slider/TableListBox classes that supply their roles.
            const bool itemValid{component != nullptr
                && component->isAccessible()
                && component->getTitle().isNotEmpty()
                && component->getDescription().isNotEmpty()
                && (!item.requireSliderRole
                    || dynamic_cast<juce::Slider*>(component) != nullptr)
                && (!item.requireTableRole
                    || dynamic_cast<juce::TableListBox*>(component) != nullptr)};
            if (!itemValid) {
                std::printf(
                    "    accessibility mismatch name=%s found=%d title=%s description=%s\n",
                    item.name,
                    component != nullptr ? 1 : 0,
                    component != nullptr ? component->getTitle().toRawUTF8() : "",
                    component != nullptr ? component->getDescription().toRawUTF8() : "");
            }
            accessibleMetadata = accessibleMetadata && itemValid;
        }
        check(accessibleMetadata,
              "bank picker, channel table, keyboard, status, and all six sliders expose named accessible metadata and built-in control roles");

        // Keyboard routing. The on-screen MIDI keyboard defaults to wanting
        // focus, which would swallow typed input meant for the controls, so the
        // editor explicitly clears it after construction. Assert the resulting
        // arrangement rather than trusting construction order.
        auto* midiKeyboard{editor != nullptr
            ? findNamedComponent(*editor, "MIDI Keyboard") : nullptr};
        auto* bankPicker{editor != nullptr
            ? findNamedComponent(*editor, "Sound bank file") : nullptr};
        const bool focusRouting{editor != nullptr
            && editor->getWantsKeyboardFocus()
            && midiKeyboard != nullptr && !midiKeyboard->getWantsKeyboardFocus()
            && channelTableForFocus(*editor) != nullptr
            && !channelTableForFocus(*editor)->getWantsKeyboardFocus()};
        check(focusRouting,
              "the editor takes keyboard focus while the on-screen keyboard and table decline it");

        bool slidersReachable{editor != nullptr};
        for (const char* name : {"Attack (CC73)", "Decay (CC75)", "Sustain level (CC79)",
                                 "Release (CC72)", "Filter cutoff (CC74)",
                                 "Filter resonance (CC71)"}) {
            auto* slider{editor != nullptr ? findNamedComponent(*editor, name) : nullptr};
            slidersReachable = slidersReachable
                && slider != nullptr && slider->getWantsKeyboardFocus();
        }
        check(slidersReachable && bankPicker != nullptr,
              "every sound-control slider accepts keyboard focus so it is reachable without a mouse");

        auto* constrainer{editor != nullptr ? editor->getConstrainer() : nullptr};
        auto* channelTable{editor != nullptr
            ? dynamic_cast<juce::TableListBox*>(
                findNamedComponent(*editor, "MIDI channel assignments"))
            : nullptr};
        const bool resizeContract{editor != nullptr
            && editor->isResizable()
            && constrainer != nullptr
            && constrainer->getMinimumWidth() == GuiConstants::minWidth
            && constrainer->getMinimumHeight() == GuiConstants::minHeight
            && constrainer->getMaximumWidth() >= GuiConstants::minWidth
            && constrainer->getMaximumHeight() == GuiConstants::maxHeight};

        constexpr std::array<const char*, 5> essentialComponents{{
            "Sound bank file",
            "MIDI channel assignments",
            "Attack (CC73)",
            "MIDI Keyboard",
            "Version and bank load status",
        }};
        const auto essentialBoundsAreUsable = [&]() {
            for (const auto* name : essentialComponents) {
                auto* component{editor != nullptr
                    ? findNamedComponent(*editor, name) : nullptr};
                if (component == nullptr
                    || component->getWidth() <= 0
                    || component->getHeight() <= 0)
                    return false;
            }
            return true;
        };

        bool minimumSizeUsable{resizeContract};
        if (editor != nullptr && constrainer != nullptr) {
            editor->setBoundsConstrained({0, 0, 1, 1});
            minimumSizeUsable = minimumSizeUsable
                && editor->getWidth() == constrainer->getMinimumWidth()
                && editor->getHeight() == constrainer->getMinimumHeight()
                && essentialBoundsAreUsable();
        }
        bool firstAndLastRowsReachable{channelTable != nullptr};
        if (channelTable != nullptr) {
            channelTable->scrollToEnsureRowIsOnscreen(15);
            const bool lastVisible{channelTable->getComponentForRowNumber(15) != nullptr
                && channelTable->getRowPosition(15, true)
                    .intersects(channelTable->getLocalBounds())};
            channelTable->scrollToEnsureRowIsOnscreen(0);
            const bool firstVisible{channelTable->getComponentForRowNumber(0) != nullptr
                && channelTable->getRowPosition(0, true)
                    .intersects(channelTable->getLocalBounds())};
            firstAndLastRowsReachable = firstVisible && lastVisible;
        }
        check(minimumSizeUsable && firstAndLastRowsReachable,
              "minimum editor size keeps essential controls usable and all 16 channel rows scroll-reachable");

        bool defaultSizeShowsAllRows{resizeContract && channelTable != nullptr};
        if (editor != nullptr && channelTable != nullptr) {
            editor->setBoundsConstrained(
                {0, 0, GuiConstants::minWidth, GuiConstants::defaultHeight});
            channelTable->setVerticalPosition(0.0);
            defaultSizeShowsAllRows = defaultSizeShowsAllRows
                && editor->getWidth() == GuiConstants::minWidth
                && editor->getHeight() == GuiConstants::defaultHeight
                && essentialBoundsAreUsable()
                && channelTable->getRowPosition(0, true)
                    .intersects(channelTable->getLocalBounds())
                && channelTable->getRowPosition(15, true)
                    .intersects(channelTable->getLocalBounds());
        }
        check(defaultSizeShowsAllRows,
              "default editor height exposes all 16 channel rows without scrolling");

        bool maximumSizeUsable{resizeContract};
        if (editor != nullptr && constrainer != nullptr) {
            editor->setBoundsConstrained({
                0,
                0,
                constrainer->getMaximumWidth() + 100,
                constrainer->getMaximumHeight() + 100,
            });
            maximumSizeUsable = maximumSizeUsable
                && editor->getWidth() == constrainer->getMaximumWidth()
                && editor->getHeight() == constrainer->getMaximumHeight()
                && essentialBoundsAreUsable();
        }
        check(maximumSizeUsable,
              "maximum editor size is constrained to useful keyboard and layout bounds");
    }

    std::printf("== engine_midi_tests: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
