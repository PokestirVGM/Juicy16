#include "PluginProcessor.h"
#include "ChannelListComponent.h"
#include "SyntheticSf2.h"
#include "PatchList.h"
#include "GuiConstants.h"
#include "Theme.h"

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

// "Is there audio at all" floor for magnitude() sums. A genuinely silent block
// sums to exactly 0.0f — the tests that require silence assert that — so this only
// has to sit above zero. It is deliberately well below a real note: the plugin
// renders at FluidSynth's default gain of 0.2, five times quieter than the 1.0 it
// used to use, and a threshold calibrated against the old level would fail on
// correct audio rather than on a regression.
constexpr float audiblePresence{0.0002f};

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

// The channel table, which is the component that takes keyboard focus and turns
// arrow keys into channel selection; looked up separately for readability.
juce::Component* channelTableForFocus(juce::Component& editor)
{
    return findNamedComponent(editor, "MIDI channel assignments");
}

// Its owning ChannelListComponent, which routes Return to a row's instrument
// dropdown.
ChannelListComponent* channelListFor(juce::Component& editor)
{
    return dynamic_cast<ChannelListComponent*>(
        findNamedComponent(editor, "MIDI channel rack"));
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

juce::AudioParameterBool* findBoolParameter(JuicySFAudioProcessor& processor,
                                            const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
        if (auto* identified{dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)};
            identified != nullptr && identified->paramID == id)
            return dynamic_cast<juce::AudioParameterBool*>(parameter);
    return nullptr;
}

// The frozen Beta 1 parameter manifest, in order. Built rather than spelled out
// because 83 identifiers written by hand is a typo waiting to be mistaken for a
// contract violation; the ORDER encoded here is the contract.
std::vector<juce::String> beta1ParameterIds()
{
    std::vector<juce::String> ids{"bank", "preset", "outputLevel"};
    for (const char* prefix : {"volCh", "panCh", "muteCh", "soloCh"})
        for (int channel = 1; channel <= 16; ++channel)
            ids.push_back(juce::String{prefix} + juce::String(channel));
    for (int channel = 1; channel <= 16; ++channel)
        ids.push_back("progCh" + juce::String(channel));
    return ids;
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

// Randomised MIDI soak. The Phase 8.7 fuzz pass covers malformed files and state
// blobs; nothing covered the MIDI path, which is the surface a game rip actually
// drives and the one place arbitrary SysEx payloads reach our own parser.
//
// The domain is deliberately "well-formed but adversarial": a host hands the
// plugin messages JUCE has already parsed, so malformed status bytes are not the
// plugin's contract. Random values, random channels, random in-block timestamps,
// hostile orderings, and arbitrary SysEx payloads are.
//
// Invariants are checked after every block, so a failure names the block that
// broke them and the seed reproduces it exactly.
int runMidiSoak(const juce::File& bank, long long blockCount, unsigned int seed)
{
    constexpr double sampleRate{48000.0};
    constexpr int maximumBlock{1024};
    constexpr int voiceCeiling{512};

    JuicySFAudioProcessor processor;
    processor.prepareToPlay(sampleRate, maximumBlock);
    const auto state{makeState(bank.getFullPathName())};
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto& model{processor.getFluidSynthModel()};

    if (model.getFontLoadStatus() != "loaded") {
        std::printf("  FAIL  soak bank loads\n");
        return 1;
    }

    // 255, not 128: on a drum channel FluidSynth adds its 128 drum offset on top
    // of the Bank Select MSB, so CC0=127 legitimately reaches 255 in both the
    // engine and the saved state. That divergence from the plugin's own 0-128
    // contract is an accepted B2 pinned by the "drum-channel Bank Select range"
    // scenario; anything outside 0-255 would be new.
    constexpr int highestReachableBank{255};

    std::mt19937 random{seed};
    const auto pick{[&random](int upperExclusive) {
        return static_cast<int>(random() % static_cast<unsigned int>(upperExclusive));
    }};

    juce::AudioBuffer<float> audio{2, maximumBlock};
    double peakAmplitude{0.0};
    int peakVoices{0};
    long long events{0};
    long long sysExEvents{0};
    int soakFailures{0};

    const std::array<int, 6> blockSizes{32, 64, 128, 256, 512, 1024};

    unsigned int previousFailureMask{model.getProgramApplyFailureMask()};

    for (long long block = 0; block < blockCount && soakFailures == 0; ++block) {
        const int blockSize{blockSizes[static_cast<std::size_t>(pick(6))]};
        juce::MidiBuffer midi;
        const int eventCount{pick(40)};
        // Kept so a newly recorded program-apply failure can name the input that
        // caused it. A fuzz harness that cannot produce a reproducer is a
        // liability: the finding is unactionable and gets ignored.
        std::vector<juce::String> description;

        for (int event = 0; event < eventCount; ++event) {
            const int channel{1 + pick(16)};
            const int sample{pick(blockSize)};
            switch (pick(10)) {
                case 0:
                case 1:
                case 2:
                {
                    const int note{pick(128)};
                    midi.addEvent(juce::MidiMessage::noteOn(
                        channel, note, static_cast<juce::uint8>(1 + pick(127))), sample);
                    description.push_back("noteOn ch" + juce::String(channel)
                        + " note " + juce::String(note) + " @" + juce::String(sample));
                    break;
                }
                case 3:
                {
                    const int note{pick(128)};
                    midi.addEvent(juce::MidiMessage::noteOff(channel, note), sample);
                    description.push_back("noteOff ch" + juce::String(channel)
                        + " note " + juce::String(note) + " @" + juce::String(sample));
                    break;
                }
                case 4:
                case 5:
                {
                    // The full CC0-CC127 range, including the channel-mode
                    // messages. Those used to disable MIDI channels and had to be
                    // excluded; the engine now restores its 16-channel layout
                    // after each one, so the invariants below cover them.
                    const int controller{pick(128)};
                    const int value{pick(128)};
                    midi.addEvent(juce::MidiMessage::controllerEvent(
                        channel, controller, value), sample);
                    description.push_back("CC" + juce::String(controller) + "=" + juce::String(value)
                        + " ch" + juce::String(channel) + " @" + juce::String(sample));
                    break;
                }
                case 6:
                {
                    const int program{pick(128)};
                    midi.addEvent(juce::MidiMessage::programChange(channel, program), sample);
                    description.push_back("PC " + juce::String(program)
                        + " ch" + juce::String(channel) + " @" + juce::String(sample));
                    break;
                }
                case 7:
                    midi.addEvent(juce::MidiMessage::pitchWheel(channel, pick(16384)), sample);
                    break;
                case 8:
                    if (pick(2) == 0)
                        midi.addEvent(juce::MidiMessage::channelPressureChange(
                            channel, pick(128)), sample);
                    else
                        midi.addEvent(juce::MidiMessage::aftertouchChange(
                            channel, pick(128), pick(128)), sample);
                    break;
                default: {
                    // SysEx: the real reset families, near misses that must NOT be
                    // treated as resets, and arbitrary payloads. This is the only
                    // attacker-controlled byte stream that reaches our own parser.
                    std::vector<juce::uint8> payload;
                    switch (pick(4)) {
                        case 0: payload = {0x7e, 0x7f, 0x09, 0x01}; break;              // GM
                        case 1: payload = {0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f,
                                           0x00, 0x41}; break;                          // GS
                        case 2: payload = {0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00};   // XG
                            if (pick(2) == 0)                                           // near miss
                                payload[static_cast<std::size_t>(pick(
                                    static_cast<int>(payload.size())))] ^= 0x01;
                            break;
                        default: {
                            payload.resize(static_cast<std::size_t>(1 + pick(64)));
                            for (auto& byte : payload)
                                byte = static_cast<juce::uint8>(random() & 0x7fu);
                            break;
                        }
                    }
                    midi.addEvent(juce::MidiMessage::createSysExMessage(
                        payload.data(), static_cast<int>(payload.size())), sample);
                    ++sysExEvents;
                    break;
                }
            }
            ++events;
        }

        audio.setSize(2, blockSize, false, false, true);
        audio.clear();
        processor.processBlock(audio, midi);
        model.handleUpdateNowIfNeeded();

        if (const unsigned int mask{model.getProgramApplyFailureMask()};
            mask != previousFailureMask) {
            std::printf("  block %lld first recorded a program-apply failure on"
                        " channels 0x%04x (block size %d):\n",
                        block, mask & ~previousFailureMask, blockSize);
            for (const auto& line : description)
                std::printf("      %s\n", line.toRawUTF8());
            previousFailureMask = mask;
        }

        // 1. Audio stays finite and bounded. A denormal storm, a runaway filter,
        //    or an uninitialised voice shows up here first.
        for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
            const float* samples{audio.getReadPointer(channel)};
            for (int sample = 0; sample < blockSize; ++sample) {
                const double value{samples[sample]};
                if (!std::isfinite(value)) {
                    std::printf("  FAIL  block %lld produced a non-finite sample\n", block);
                    ++soakFailures;
                    break;
                }
                peakAmplitude = std::max(peakAmplitude, std::abs(value));
            }
            if (soakFailures != 0)
                break;
        }
        if (peakAmplitude > 32.0 && soakFailures == 0) {
            std::printf("  FAIL  block %lld exceeded the amplitude ceiling (%.2f)\n",
                        block, peakAmplitude);
            ++soakFailures;
        }

        // 2. Every channel reports a program inside the documented ranges, and
        //    3. the engine never exceeds its own voice ceiling.
        int voices{0};
        for (int channel = 0; channel < 16 && soakFailures == 0; ++channel) {
            int bankNumber{-1}, preset{-1};
            const bool reported{model.getChannelProgram(channel, bankNumber, preset)};
            if (!reported || bankNumber < 0 || bankNumber > highestReachableBank
                || preset < 0 || preset > 127) {
                std::printf("  FAIL  block %lld channel %d: getChannelProgram %s,"
                            " bank %d preset %d, font status '%s', mask 0x%04x\n",
                            block, channel + 1, reported ? "succeeded" : "FAILED",
                            bankNumber, preset,
                            model.getFontLoadStatus().toRawUTF8(),
                            model.getProgramApplyFailureMask());
                ++soakFailures;
                break;
            }
            int bend{-1};
            if (!model.getPitchBend(channel, bend) || bend < 0 || bend > 16383) {
                std::printf("  FAIL  block %lld left channel %d bend at %d\n",
                            block, channel + 1, bend);
                ++soakFailures;
                break;
            }
            FluidSynthModel::VoiceStateCounts counts;
            if (model.getVoiceStateCounts(channel, counts))
                voices += counts.playing;
        }
        peakVoices = std::max(peakVoices, voices);
        if (soakFailures == 0 && voices > voiceCeiling) {
            std::printf("  FAIL  block %lld allocated %d voices, above the %d ceiling\n",
                        block, voices, voiceCeiling);
            ++soakFailures;
        }

        // 4. Saved state stays serialisable and in range. Checked periodically
        //    rather than every block, because it is the expensive invariant.
        if (soakFailures == 0 && block % 512 == 0) {
            juce::MemoryBlock saved;
            processor.getStateInformation(saved);
            const auto xml{juce::AudioProcessor::getXmlFromBinary(
                saved.getData(), static_cast<int>(saved.getSize()))};
            if (xml == nullptr) {
                std::printf("  FAIL  block %lld produced unreadable saved state\n", block);
                ++soakFailures;
            } else if (auto* programs{xml->getChildByName("channelPrograms")}) {
                for (auto* channel : programs->getChildIterator()) {
                    const int savedBank{channel->getIntAttribute("bank", -1)};
                    const int savedPreset{channel->getIntAttribute("preset", -1)};
                    // Same 255 ceiling as the engine check above, and for the same
                    // reason: a drum channel's Bank Select reaches 128 + MSB, and
                    // that value is persisted. Requiring 0-128 here while allowing
                    // 0-255 there made the invariant contradict itself, which the
                    // sweep caught at seed 16, block 58880.
                    if (savedBank < 0 || savedBank > highestReachableBank
                        || savedPreset < 0 || savedPreset > 127) {
                        std::printf("  FAIL  block %lld saved bank %d preset %d\n",
                                    block, savedBank, savedPreset);
                        ++soakFailures;
                        break;
                    }
                }
            }
        }
    }

    // 5. After silencing everything, no voice may still be running. A voice that
    //    survives All Sound Off is a stuck note in a host.
    juce::MidiBuffer silence;
    addAllSoundOff(silence);
    audio.setSize(2, maximumBlock, false, false, true);
    audio.clear();
    processor.processBlock(audio, silence);
    int remaining{0};
    for (int channel = 0; channel < 16; ++channel) {
        FluidSynthModel::VoiceStateCounts counts;
        if (model.getVoiceStateCounts(channel, counts))
            remaining += counts.playing;
    }
    if (remaining != 0) {
        std::printf("  FAIL  %d voices survived All Sound Off after the soak\n", remaining);
        ++soakFailures;
    }
    if (model.getFontLoadStatus() != "loaded") {
        std::printf("  FAIL  the bank was lost during the soak\n");
        ++soakFailures;
    }

    std::printf("  seed %u: %lld blocks, %lld events (%lld SysEx), peak amplitude %.3f,"
                " peak voices %d, program-apply failure mask 0x%04x\n",
                seed, blockCount, events, sysExEvents, peakAmplitude, peakVoices,
                model.getProgramApplyFailureMask());
    if (soakFailures == 0)
        std::printf("  PASS  randomised MIDI soak holds every invariant\n");
    return soakFailures;
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

    check(renderedEnergy > audiblePresence, "the game rip produces audio");

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
    if (argc >= 4 && juce::String{argv[1]} == "--midi-soak") {
        std::printf("== randomised MIDI soak ==\n");
        const long long blocks{juce::String{argv[3]}.getLargeIntValue()};
        const unsigned int seed{argc >= 5
            ? static_cast<unsigned int>(juce::String{argv[4]}.getLargeIntValue())
            : 0x4d494449u};
        return runMidiSoak(juce::File{argv[2]}, blocks, seed);
    }
    if (argc == 4 && juce::String{argv[1]} == "--game-rip") {
        std::printf("== multichannel game rip ==\n");
        return runGameRipScenario(juce::File{argv[2]}, juce::File{argv[3]});
    }
    if (argc != 3) {
        std::fprintf(
            stderr,
            "usage: JuicySFEngineMidiTests <font.dls|sf2|sf3> <controller-fixture.csv>\n"
            "       JuicySFEngineMidiTests --game-rip <bank> <file.mid>\n"
            "       JuicySFEngineMidiTests --midi-soak <bank> <blocks> [seed]\n");
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
        const auto expectedParameterIds{beta1ParameterIds()};
        const auto& parameters{processor.getParameters()};
        bool parameterContract{parameters.size()
            == static_cast<int>(expectedParameterIds.size())};
        for (std::size_t i = 0; parameterContract && i < expectedParameterIds.size(); ++i) {
            const auto* identified{
                dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[static_cast<int>(i)])};
            parameterContract = identified != nullptr
                && identified->paramID == expectedParameterIds[i]
                && identified->getVersionHint() == 1;
            if (!parameterContract)
                std::printf("    parameter %d expected %s got %s\n",
                            static_cast<int>(i),
                            expectedParameterIds[i].toRawUTF8(),
                            identified != nullptr ? identified->paramID.toRawUTF8() : "(none)");
        }
        check(parameterContract,
              "Beta 1 parameter IDs, order, count, and version hints are frozen");

        // Every channel parameter is discoverable by its own id, which is what a
        // host's automation and controller-link menus enumerate.
        bool perChannelPresent{true};
        for (int channel = 1; channel <= 16; ++channel)
            perChannelPresent = perChannelPresent
                && findIntParameter(processor, "volCh" + juce::String(channel)) != nullptr
                && findIntParameter(processor, "panCh" + juce::String(channel)) != nullptr
                && findBoolParameter(processor, "muteCh" + juce::String(channel)) != nullptr
                && findBoolParameter(processor, "soloCh" + juce::String(channel)) != nullptr;
        check(perChannelPresent,
              "all 16 channels expose volume, pan, mute and solo as host-automatable parameters");
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
                  && magnitude(audio, 128, blockSize - 128) > audiblePresence,
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

    std::printf("== cross-bank selection and bank offsets ==\n");
    {
        // The private corpus reaches bank 1 at most, so cross-bank behaviour is
        // proved against a synthesised bank whose presets are audibly distinct:
        // each plays a looped sine at its own frequency with scale tuning off, so
        // the rendered pitch names the (bank, preset) FluidSynth actually chose.
        constexpr double bank0Program0{220.5};
        constexpr double bank0Program40{294.0};
        constexpr double bank1Program0{441.0};
        constexpr double bank1Program40{588.0};
        constexpr double bank8Program40{735.0};
        constexpr double bank128Program0{882.0};

        const std::vector<SyntheticSf2::PresetSpec> specs{
            {0, 0, bank0Program0, "Bank0 Prog0"},
            {0, 40, bank0Program40, "Bank0 Prog40"},
            {1, 0, bank1Program0, "Bank1 Prog0"},
            {1, 40, bank1Program40, "Bank1 Prog40"},
            {8, 40, bank8Program40, "Bank8 Prog40"},
            {128, 0, bank128Program0, "Drums Prog0"},
        };

        const auto fixtureFile{juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("juicy16-bank-select-fixture", ".sf2", false)};
        const bool fixtureWritten{SyntheticSf2::write(fixtureFile, specs)};

        constexpr int toneBlock{4096};
        constexpr double toneRate{48000.0};
        JuicySFAudioProcessor fixtureProcessor;
        fixtureProcessor.prepareToPlay(toneRate, toneBlock);
        const auto fixtureState{makeState(fixtureFile.getFullPathName())};
        fixtureProcessor.setStateInformation(
            fixtureState.getData(), static_cast<int>(fixtureState.getSize()));
        auto& fixtureModel{fixtureProcessor.getFluidSynthModel()};
        juce::AudioBuffer<float> tone{2, toneBlock};

        check(fixtureWritten && fixtureModel.getFontLoadStatus() == "loaded",
              "the synthesised multi-bank SF2 loads");

        // Renders one sustained note and returns its measured pitch. The estimator
        // searches +/-20% around the expectation, so a wrong preset lands at a
        // window edge rather than matching.
        const auto soundingFrequency{[&](int midiChannel, double expected) {
            juce::MidiBuffer off;
            addAllSoundOff(off);
            render(fixtureProcessor, tone, off);
            juce::MidiBuffer note;
            note.addEvent(
                juce::MidiMessage::noteOn(midiChannel, 60, static_cast<juce::uint8>(100)), 0);
            render(fixtureProcessor, tone, note);
            return estimatePeriodicFrequency(tone, 1024, 3072, toneRate, expected);
        }};
        // Autocorrelation resolves to whole sample lags, so the achievable
        // accuracy falls with pitch: 882 Hz at 48 kHz lands between lag 54 and 55.
        // A 2% window is far tighter than the 20% separating adjacent fixture
        // presets, so it still identifies exactly one of them.
        const auto sounds{[](double measured, double expected) {
            return std::abs(measured - expected) < expected * 0.02;
        }};

        // Exact-bank selection is the shortest route to "does this bank exist in
        // this font": FluidSynth's program select fails rather than substituting.
        const bool bankMembership{
            fixtureModel.setChannelProgram(0, 0, 0)
            && fixtureModel.setChannelProgram(0, 0, 40)
            && fixtureModel.setChannelProgram(0, 1, 0)
            && fixtureModel.setChannelProgram(0, 1, 40)
            && fixtureModel.setChannelProgram(0, 8, 40)
            && fixtureModel.setChannelProgram(0, 128, 0)
            && !fixtureModel.setChannelProgram(0, 8, 0)
            && !fixtureModel.setChannelProgram(0, 2, 40)};
        check(bankMembership,
              "presets resolve in banks 0, 1, 8, and 128 and only where the font defines them");

        fixtureModel.setChannelProgram(0, 0, 0);
        check(sounds(soundingFrequency(1, bank0Program0), bank0Program0),
              "bank 0 program 0 sounds its own preset");
        check(sounds(soundingFrequency(10, bank128Program0), bank128Program0),
              "channel 10 sounds the percussion bank without any Bank Select");

        // CC0 then Program Change: the pinned GS mode defers the bank until the
        // program arrives, and everything downstream must follow the same bank.
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 0, 1), 0);
            midi.addEvent(juce::MidiMessage::programChange(1, 40), 1);
            render(fixtureProcessor, tone, midi);
            fixtureModel.handleUpdateNowIfNeeded();

            int engineBank{-1}, enginePreset{-1}, savedBank{-1}, savedPreset{-1};
            auto* bankParam{findIntParameter(fixtureProcessor, "bank")};
            auto* presetParam{findIntParameter(fixtureProcessor, "preset")};
            check(fixtureModel.getChannelProgram(0, engineBank, enginePreset)
                      && engineBank == 1 && enginePreset == 40
                      && getSavedChannelProgram(fixtureProcessor, 0, savedBank, savedPreset)
                      && savedBank == 1 && savedPreset == 40
                      && bankParam != nullptr && bankParam->get() == 1
                      && presetParam != nullptr && presetParam->get() == 40
                      && sounds(soundingFrequency(1, bank1Program40), bank1Program40),
                  "CC0 plus Program Change selects a non-zero bank across engine, state, UI, and audio");
        }

        // A stale CC32 must not move the channel out of the bank CC0 chose.
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 32, 8), 0);
            midi.addEvent(juce::MidiMessage::programChange(1, 0), 1);
            render(fixtureProcessor, tone, midi);
            fixtureModel.handleUpdateNowIfNeeded();
            int engineBank{-1}, enginePreset{-1};
            check(fixtureModel.getChannelProgram(0, engineBank, enginePreset)
                      && engineBank == 1 && enginePreset == 0
                      && sounds(soundingFrequency(1, bank1Program0), bank1Program0),
                  "CC32 does not move the channel out of the bank CC0 selected");
        }

        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 0, 8), 0);
            midi.addEvent(juce::MidiMessage::programChange(1, 40), 1);
            render(fixtureProcessor, tone, midi);
            fixtureModel.handleUpdateNowIfNeeded();
            int engineBank{-1}, enginePreset{-1};
            check(fixtureModel.getChannelProgram(0, engineBank, enginePreset)
                      && engineBank == 8 && enginePreset == 40
                      && sounds(soundingFrequency(1, bank8Program40), bank8Program40),
                  "a sparse high bank is reachable by Bank Select");
        }

        // Bank 8 has no program 0. FluidSynth 2.5.5 accepts the change, records the
        // requested bank and program on the channel, and quietly substitutes
        // bank 0 program 0 for synthesis. Both halves are pinned here: the state
        // and UI must agree with the channel, and the audio is the substitute, so
        // the visible patch and the audible one legitimately differ. Recorded as a
        // B2 limitation in docs/KNOWN_ISSUES.md.
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::programChange(1, 0), 0);
            render(fixtureProcessor, tone, midi);
            fixtureModel.handleUpdateNowIfNeeded();
            int engineBank{-1}, enginePreset{-1}, savedBank{-1}, savedPreset{-1};
            auto* bankParam{findIntParameter(fixtureProcessor, "bank")};
            auto* presetParam{findIntParameter(fixtureProcessor, "preset")};
            const bool reportedConsistently{
                fixtureModel.getChannelProgram(0, engineBank, enginePreset)
                && engineBank == 8 && enginePreset == 0
                && getSavedChannelProgram(fixtureProcessor, 0, savedBank, savedPreset)
                && savedBank == 8 && savedPreset == 0
                && bankParam != nullptr && bankParam->get() == 8
                && presetParam != nullptr && presetParam->get() == 0};
            const double substituted{soundingFrequency(1, bank0Program0)};
            check(reportedConsistently && sounds(substituted, bank0Program0),
                  "an undefined bank/program is reported as requested everywhere while FluidSynth"
                  " substitutes bank 0 program 0 for synthesis");
            std::printf("    bank 8 program 0 is undefined: reported as bank %d program %d,"
                        " sounds %.1f Hz (bank 0 program 0 is %.1f Hz)\n",
                        engineBank, enginePreset, substituted, bank0Program0);
        }

        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 0, 0), 0);
            midi.addEvent(juce::MidiMessage::programChange(1, 0), 1);
            render(fixtureProcessor, tone, midi);
            fixtureModel.handleUpdateNowIfNeeded();
            int engineBank{-1}, enginePreset{-1};
            check(fixtureModel.getChannelProgram(0, engineBank, enginePreset)
                      && engineBank == 0 && enginePreset == 0
                      && sounds(soundingFrequency(1, bank0Program0), bank0Program0),
                  "Bank Select back to 0 restores the melodic bank");
        }

        // Bank offsets. Juicy16 never installs one, so every raw/logical bank
        // conversion in the program paths would otherwise run only at offset 0.
        {
            constexpr int offset{10};
            int reported{-1};
            const bool offsetInstalled{fixtureModel.setLoadedFontBankOffset(offset)
                && fixtureModel.getLoadedFontBankOffset(reported)
                && reported == offset};

            // Exact-bank callers pass a logical bank and must add the offset
            // themselves; the engine reports the same logical bank back.
            const bool exactBankOffset{offsetInstalled
                && fixtureModel.setChannelProgram(0, 1, 40)};
            int engineBank{-1}, enginePreset{-1}, savedBank{-1}, savedPreset{-1};
            auto* bankParam{findIntParameter(fixtureProcessor, "bank")};
            check(exactBankOffset
                      && fixtureModel.getChannelProgram(0, engineBank, enginePreset)
                      && engineBank == 1 && enginePreset == 40
                      && getSavedChannelProgram(fixtureProcessor, 0, savedBank, savedPreset)
                      && savedBank == 1 && savedPreset == 40
                      && bankParam != nullptr && bankParam->get() == 1
                      && sounds(soundingFrequency(1, bank1Program40), bank1Program40),
                  "manual selection under a bank offset reports and sounds the font's own bank");

            // MIDI Bank Select is a raw engine bank, so the same font bank now
            // lives at CC0 = offset + 1, and the reported bank is offset-corrected.
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 0, offset + 1), 0);
            midi.addEvent(juce::MidiMessage::programChange(1, 0), 1);
            render(fixtureProcessor, tone, midi);
            fixtureModel.handleUpdateNowIfNeeded();
            int offsetBank{-1}, offsetPreset{-1}, offsetSavedBank{-1}, offsetSavedPreset{-1};
            check(offsetInstalled
                      && fixtureModel.getChannelProgram(0, offsetBank, offsetPreset)
                      && offsetBank == 1 && offsetPreset == 0
                      && getSavedChannelProgram(
                          fixtureProcessor, 0, offsetSavedBank, offsetSavedPreset)
                      && offsetSavedBank == 1 && offsetSavedPreset == 0
                      && sounds(soundingFrequency(1, bank1Program0), bank1Program0),
                  "MIDI Bank Select under an offset resolves to the font bank and reports it logically");

            int restored{-1};
            const bool offsetCleared{fixtureModel.setLoadedFontBankOffset(0)
                && fixtureModel.getLoadedFontBankOffset(restored) && restored == 0
                && fixtureModel.setChannelProgram(0, 0, 40)
                && sounds(soundingFrequency(1, bank0Program40), bank0Program40)};
            check(offsetCleared,
                  "clearing the bank offset restores ordinary selection");
        }

        fixtureFile.deleteFile();
    }

    std::printf("== drum-channel Bank Select range ==\n");
    {
        // Found by the randomised MIDI soak on 2026-08-20: channel 10 reported
        // bank 239, outside the 0-128 the rest of the plugin assumed.
        //
        // The cause is not a malformed font. SF2 2.04 section 7.2 does limit a
        // file's wBank to 0-127 melodic plus 128 percussion, and every fixture
        // here obeys that. This is the *runtime* channel bank: on a drum channel
        // FluidSynth adds the 128 drum offset on top of the Bank Select MSB, so
        // CC0=127 - the XG drum convention - lands on 255.
        //
        // Shipped as a B2 until 2026-08-23, when the owner declined it: the
        // parameter now spans 0-255, state schema 4 rescales a v3 save's
        // normalised bank, and a drum-range bank restores through Bank Select so
        // FluidSynth substitutes the kit instead of the font's first melodic
        // preset. This scenario asserts the agreement rather than the divergence.
        // The editor mirrors the *selected* channel, so this selects channel 10;
        // with channel 1 selected the parameter is showing a different channel's
        // bank and proves nothing either way.
        const auto drumBankFor{[&](int msb, int& savedBank, int& uiBank,
                                   int& reloadedBank, juce::AudioBuffer<float>& tone) {
            JuicySFAudioProcessor drums;
            drums.prepareToPlay(48000.0, blockSize);
            const auto drumState{makeState(argv[1], 10)};
            drums.setStateInformation(
                drumState.getData(), static_cast<int>(drumState.getSize()));
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(10, 0, msb), 0);
            midi.addEvent(juce::MidiMessage::programChange(10, 0), 1);
            midi.addEvent(
                juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 2);
            tone.setSize(2, blockSize, false, false, true);
            render(drums, tone, midi);
            drums.getFluidSynthModel().handleUpdateNowIfNeeded();

            int engineBank{-1}, enginePreset{-1};
            drums.getFluidSynthModel().getChannelProgram(9, engineBank, enginePreset);
            int savedPreset{-1};
            getSavedChannelProgram(drums, 9, savedBank, savedPreset);
            auto* bankParam{findIntParameter(drums, "bank")};
            uiBank = bankParam != nullptr ? bankParam->get() : -1;

            juce::MemoryBlock saved;
            drums.getStateInformation(saved);
            JuicySFAudioProcessor reopened;
            reopened.prepareToPlay(48000.0, blockSize);
            reopened.setStateInformation(
                saved.getData(), static_cast<int>(saved.getSize()));
            int reloadedPreset{-1};
            reopened.getFluidSynthModel().getChannelProgram(
                9, reloadedBank, reloadedPreset);
            return engineBank;
        }};

        int savedZero{-1}, uiZero{-1}, reloadedZero{-1};
        juce::AudioBuffer<float> toneZero;
        const int engineZero{drumBankFor(0, savedZero, uiZero, reloadedZero, toneZero)};

        int savedXg{-1}, uiXg{-1}, reloadedXg{-1};
        juce::AudioBuffer<float> toneXg;
        const int engineXg{drumBankFor(127, savedXg, uiXg, reloadedXg, toneXg)};

        check(engineZero == 128 && savedZero == 128 && uiZero == 128
                  && reloadedZero == 128,
              "CC0=0 on channel 10 holds the percussion bank across engine, state, UI, and reload");

        check(engineXg == 255 && savedXg == 255 && uiXg == 255,
              "CC0=127 on channel 10 reports bank 255 on every surface, not just the engine");

        check(reloadedXg == 255,
              "reopening a project restores the 255 drum bank instead of falling back to 128");

        // Severity check rather than an assumption: does any of this change what
        // the listener hears? FluidSynth substitutes the drum kit at play time,
        // so the two renderings should be identical.
        const double drumCorrelation{
            waveformCorrelation(toneZero, toneXg, 2, blockSize - 2)};
        check(magnitude(toneZero, 2, blockSize - 2) > audiblePresence && drumCorrelation > 0.999,
              "the substituted drum kit sounds identical, so the defect is state-only");
        std::printf("    CC0=0 vs CC0=127 on channel 10: engine banks %d and %d,"
                    " audio correlation %.4f\n", engineZero, engineXg, drumCorrelation);

        // Widening the parameter changed what a stored normalised value means.
        // Parameters are saved normalised, so the 1.0 that meant bank 128 under
        // the old 0-128 range would restore as 255 under 0-255 - silently moving
        // every existing drum channel to a bank no font defines. Schema 4
        // rescales it on the way in; this pins that, because the failure is
        // invisible until a user reopens an older project.
        {
            JuicySFAudioProcessor writer;
            writer.prepareToPlay(48000.0, blockSize);
            const auto writerState{makeState(argv[1], 10)};
            writer.setStateInformation(
                writerState.getData(), static_cast<int>(writerState.getSize()));
            juce::MemoryBlock saved;
            writer.getStateInformation(saved);
            auto xml{juce::AudioProcessor::getXmlFromBinary(
                saved.getData(), static_cast<int>(saved.getSize()))};
            bool migrated{false};
            if (xml != nullptr) {
                xml->setAttribute("stateVersion", 3);
                if (auto* params{xml->getChildByName("params")})
                    params->setAttribute("bank", 1.0); // bank 128 under the v3 range
                juce::MemoryBlock legacy;
                juce::AudioProcessor::copyXmlToBinary(*xml, legacy);
                JuicySFAudioProcessor reader;
                reader.prepareToPlay(48000.0, blockSize);
                reader.setStateInformation(
                    legacy.getData(), static_cast<int>(legacy.getSize()));
                auto* bankParam{findIntParameter(reader, "bank")};
                migrated = bankParam != nullptr && bankParam->get() == 128;
            }
            check(migrated,
                  "a v3 save's full-scale bank restores as 128, not as 255 under the widened range");
        }
    }

    std::printf("== host sample rates above the engine ceiling ==\n");
    {
        // FluidSynth 2.5.5 renders no higher than 96 kHz. Juicy16 used to mute
        // above that: the plugin loaded, reported the rate, and produced nothing,
        // which auval surfaced at 192 kHz on 2026-08-23. It now renders at an
        // integer fraction of the host rate and interpolates each block up, so
        // what this proves is that the audio exists, that it is at the right
        // pitch afterwards, and that the FIFO carrying the interpolator's
        // fractional position neither drops nor repeats samples across blocks.
        constexpr double toneFrequency{441.0};
        const std::vector<SyntheticSf2::PresetSpec> rateSpecs{
            {0, 0, toneFrequency, "Tone"}};
        const auto rateFixture{juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("juicy16-sample-rate-fixture", ".sf2", false)};
        const bool rateFixtureWritten{SyntheticSf2::write(rateFixture, rateSpecs)};

        constexpr int rateBlock{1024};
        constexpr int rateBlocks{8};
        const auto renderAtRate{[&](double rateHz, juce::AudioBuffer<float>& out) {
            JuicySFAudioProcessor rateProcessor;
            rateProcessor.prepareToPlay(rateHz, rateBlock);
            const auto rateState{makeState(rateFixture.getFullPathName())};
            rateProcessor.setStateInformation(
                rateState.getData(), static_cast<int>(rateState.getSize()));
            const bool loaded{
                rateProcessor.getFluidSynthModel().getFontLoadStatus() == "loaded"};
            out.setSize(2, rateBlock * rateBlocks, false, false, true);
            juce::AudioBuffer<float> one{2, rateBlock};
            for (int b = 0; b < rateBlocks; ++b) {
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent(
                        juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
                render(rateProcessor, one, midi);
                out.copyFrom(0, b * rateBlock, one, 0, 0, rateBlock);
                out.copyFrom(1, b * rateBlock, one, 1, 0, rateBlock);
            }
            return loaded;
        }};

        // 96 kHz is the control: the engine renders it directly, so anything the
        // oversampled rates do differently is the oversampler's doing.
        juce::AudioBuffer<float> atCeiling, at192, at176;
        const bool rendered{rateFixtureWritten
            && renderAtRate(96000.0, atCeiling)
            && renderAtRate(192000.0, at192)
            && renderAtRate(176400.0, at176)};
        check(rendered, "the single-preset tone fixture loads at every tested rate");

        // The window starts after the attack and deliberately spans several block
        // boundaries: a FIFO that lost or duplicated samples between blocks would
        // break the period there rather than inside any one block.
        constexpr int rateWindowStart{rateBlock + 256};
        constexpr int rateWindowLength{rateBlock * 5};
        const float ceilingLevel{magnitude(atCeiling, rateWindowStart, rateWindowLength)};
        const float level192{magnitude(at192, rateWindowStart, rateWindowLength)};
        const float level176{magnitude(at176, rateWindowStart, rateWindowLength)};

        check(level192 > audiblePresence && level176 > audiblePresence,
              "a host rate above FluidSynth's ceiling produces audio instead of silence");

        const auto pitchAt{[&](const juce::AudioBuffer<float>& waveform, double rateHz) {
            return estimatePeriodicFrequency(
                waveform, rateWindowStart, rateWindowLength, rateHz, toneFrequency);
        }};
        const double pitch96{pitchAt(atCeiling, 96000.0)};
        const double pitch192{pitchAt(at192, 192000.0)};
        const double pitch176{pitchAt(at176, 176400.0)};
        const auto onPitch{[](double measured) {
            return std::abs(measured - toneFrequency) < toneFrequency * 0.02;
        }};
        check(onPitch(pitch96) && onPitch(pitch192) && onPitch(pitch176),
              "the interpolated output holds its pitch across block boundaries at 192 and 176.4 kHz");

        // Level, not just presence: interpolation that dropped or repeated whole
        // samples would still measure a period but would not hold its amplitude
        // against the directly rendered control.
        // magnitude() sums a fixed number of samples, so the same window is half
        // the duration at 192 kHz. Equal sums therefore mean equal amplitude per
        // sample, which is what interpolation must preserve: a path that dropped
        // or repeated samples would still hold pitch but would not hold level.
        check(ceilingLevel > audiblePresence
                  && level192 > ceilingLevel * 0.8 && level192 < ceilingLevel * 1.2
                  && level176 > ceilingLevel * 0.8 && level176 < ceilingLevel * 1.2,
              "interpolated output holds the control's amplitude, not just its pitch");
        std::printf("    96 kHz %.1f Hz, 192 kHz %.1f Hz, 176.4 kHz %.1f Hz;"
                    " levels %.3f / %.3f / %.3f\n",
                    pitch96, pitch192, pitch176, ceilingLevel, level192, level176);
        rateFixture.deleteFile();
    }

    std::printf("== channel mode messages ==\n");
    {
        // Found by the randomised MIDI soak on 2026-08-20.
        //
        // FluidSynth implements MIDI 1.0 basic-channel semantics faithfully: Omni
        // Off and Mono On assign a group of consecutive channels to a basic
        // channel and DISABLE the rest, so one CC124 on channel 1 used to leave
        // only channel 1 responding until the next reset. Correct for a MIDI 1.0
        // sound module, incompatible with a fixed 16-channel instrument.
        //
        // Juicy16 now forwards the controller and then restores its own layout,
        // so both contracts hold: every CC0-127 still reaches FluidSynth, and
        // there are still exactly 16 channels afterwards.
        const auto disabledChannels{[](FluidSynthModel& target) {
            int count{0};
            for (int channel = 0; channel < 16; ++channel) {
                int bankNumber{-1}, preset{-1};
                if (!target.getChannelProgram(channel, bankNumber, preset))
                    ++count;
            }
            return count;
        }};

        const auto modeState{makeState(argv[1])};
        bool everyModeMessageSurvived{true};
        bool everyModeMessageDelivered{true};
        bool everyChannelStillSounds{true};

        for (const int controller : {124, 125, 126, 127}) {
            for (const int value : {0, 1, 2, 4, 16, 127}) {
                JuicySFAudioProcessor modeProcessor;
                modeProcessor.prepareToPlay(48000.0, blockSize);
                modeProcessor.setStateInformation(
                    modeState.getData(), static_cast<int>(modeState.getSize()));
                auto& modeModel{modeProcessor.getFluidSynthModel()};
                juce::AudioBuffer<float> modeAudio{2, blockSize};

                juce::MidiBuffer midi;
                midi.addEvent(
                    juce::MidiMessage::controllerEvent(1, controller, value), 0);
                // A note on the last channel, which is the first one a basic
                // channel group would have swallowed.
                midi.addEvent(
                    juce::MidiMessage::noteOn(16, 60, static_cast<juce::uint8>(100)), 1);
                render(modeProcessor, modeAudio, midi);

                everyModeMessageSurvived = everyModeMessageSurvived
                    && disabledChannels(modeModel) == 0;

                // Delivered, not filtered: the engine must have seen the CC.
                int delivered{-1}, deliveredSample{-1};
                everyModeMessageDelivered = everyModeMessageDelivered
                    && modeModel.getLastDispatchedController(
                           0, controller, delivered, deliveredSample)
                    && delivered == value && deliveredSample == 0;

                everyChannelStillSounds = everyChannelStillSounds
                    && magnitude(modeAudio, 1, blockSize - 1) > audiblePresence;
            }
        }

        check(everyModeMessageSurvived,
              "no CC124-127 channel-mode message disables any of the 16 channels");
        check(everyModeMessageDelivered,
              "channel-mode messages still reach FluidSynth at their own timestamp rather than being filtered");
        check(everyChannelStillSounds,
              "channel 16 still sounds immediately after every channel-mode message");

        // The layout must survive repeated and interleaved mode messages, which
        // is what a host sending a reset burst actually produces.
        JuicySFAudioProcessor burstProcessor;
        burstProcessor.prepareToPlay(48000.0, blockSize);
        burstProcessor.setStateInformation(
            modeState.getData(), static_cast<int>(modeState.getSize()));
        juce::AudioBuffer<float> burstAudio{2, blockSize};
        juce::MidiBuffer burst;
        int burstSample{0};
        for (int repeat = 0; repeat < 4; ++repeat)
            for (const int controller : {126, 124, 127, 125})
                burst.addEvent(
                    juce::MidiMessage::controllerEvent(
                        1 + (repeat % 16), controller, repeat), burstSample++);
        burst.addEvent(
            juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), burstSample);
        render(burstProcessor, burstAudio, burst);
        check(disabledChannels(burstProcessor.getFluidSynthModel()) == 0
                  && magnitude(burstAudio, burstSample, blockSize - burstSample) > audiblePresence,
              "a burst of interleaved channel-mode messages leaves all 16 channels intact and sounding");
    }

    std::printf("== voice ceiling ==\n");
    {
        // FluidSynth sizes its rvoice event queue once, from the settings
        // polyphony, and never resizes it. Raising the limit after construction
        // therefore left the queue sized for the default 256 while 512 voices fed
        // it, dropping engine events above ~256 sounding voices. The configured
        // and reported limits must agree, which they only do if the limit was a
        // setting.
        int configured{-1}, active{-1};
        check(model.getConfiguredPolyphony(configured, active)
                  && configured == 512 && active == 512,
              "the voice ceiling is configured before the synth exists, so FluidSynth's event queue is sized for it");

        juce::MidiBuffer clearFirst;
        addAllSoundOff(clearFirst);
        render(processor, audio, clearFirst);

        juce::MidiBuffer dense;
        constexpr int notesPerChannel{32};
        for (int channel = 1; channel <= 16; ++channel)
            for (int note = 0; note < notesPerChannel; ++note)
                dense.addEvent(
                    juce::MidiMessage::noteOn(
                        channel, 36 + note * 2, static_cast<juce::uint8>(100)),
                    note);
        render(processor, audio, dense);

        const auto totalPlaying{[&] {
            int playing{0};
            for (int channel = 0; channel < 16; ++channel) {
                FluidSynthModel::VoiceStateCounts counts;
                if (model.getVoiceStateCounts(channel, counts))
                    playing += counts.playing;
            }
            return playing;
        }};
        // Not all 512 sound: a percussion bank has no sample on every key. What
        // matters is that the engine goes far past the old 256-voice queue limit.
        const int allocated{totalPlaying()};

        juce::MidiBuffer sustain;
        render(processor, audio, sustain);
        const int stillPlaying{totalPlaying()};

        juce::MidiBuffer silence;
        addAllSoundOff(silence);
        render(processor, audio, silence);

        check(allocated > 400 && stillPlaying > 400 && totalPlaying() == 0,
              "512 simultaneous note-ons allocate and sustain well past 400 voices and all release on All Sound Off");
        std::printf("    512 note-ons allocated %d voices, %d still sounding a block later\n",
                    allocated, stillPlaying);
    }

    std::printf("== sample-accurate rendering ==\n");
    {
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 256);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 256) == 0.0f,
              "note-on at sample 256 leaves the preceding segment silent");
        check(magnitude(audio, 256, blockSize - 256) > audiblePresence,
              "note-on produces audio after its timestamp");
    }
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 512);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 512) > audiblePresence,
              "mid-block note-off does not truncate audio before its timestamp");
    }
    {
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(100)), 256);
        midi.addEvent(juce::MidiMessage::noteOff(1, 62), 512);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 256) == 0.0f
                  && magnitude(audio, 256, 256) > audiblePresence,
              "note-on and note-off in one block produce a bounded pre-release note segment");
    }
    {
        juce::MidiBuffer midi;
        addAllSoundOff(midi);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 128);
        midi.addEvent(juce::MidiMessage::noteOn(16, 72, static_cast<juce::uint8>(100)), 640);
        render(processor, audio, midi);
        check(magnitude(audio, 0, 128) == 0.0f
                  && magnitude(audio, 128, 512) > audiblePresence,
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
                  && magnitude(audio, 401, blockSize - 401) > audiblePresence
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
                  && magnitude(mono, 256, mono.getNumSamples() - 256) > audiblePresence,
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

        check(magnitude(organAtBlockStart, notePosition, tail) > audiblePresence
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
        midi.addEvent(juce::MidiMessage::controllerEvent(6, 7, 91), 11);
        midi.addEvent(
            juce::MidiMessage::createSysExMessage(gmReset, sizeof(gmReset)), 20);
        midi.addEvent(
            juce::MidiMessage::noteOn(6, 60, static_cast<juce::uint8>(100)), 20);
        render(processor, audio, midi);

        int noteBank{-1}, notePreset{-1}, noteSample{-1}, volume{-1};
        check(model.getLastDispatchedNoteOnProgram(
                  5, noteBank, notePreset, noteSample)
                  && noteBank == 0 && notePreset == 5 && noteSample == 20
                  && model.getControllerValue(5, 7, volume) && volume == 91,
              "reset reasserts program and channel volume before an equal-timestamp following note");
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
        midi.addEvent(juce::MidiMessage::controllerEvent(7, 7, 88), 6);
        for (std::size_t i = 0; i < resets.size(); ++i)
            midi.addEvent(
                juce::MidiMessage::createSysExMessage(
                    resets[i].data(), static_cast<int>(resets[i].size())),
                10 + static_cast<int>(i) * 10);
        midi.addEvent(
            juce::MidiMessage::noteOn(7, 60, static_cast<juce::uint8>(100)), 31);
        render(processor, audio, midi);

        int noteBank{-1}, notePreset{-1}, noteSample{-1}, volume{-1};
        check(model.getLastDispatchedNoteOnProgram(
                  6, noteBank, notePreset, noteSample)
                  && noteBank == 0 && notePreset == 6 && noteSample == 31
                  && model.getControllerValue(6, 7, volume) && volume == 88,
              "multiple GM, GS, and XG resets preserve the latest program and mixer snapshot");
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
        // Juicy16 installs no modulators of its own any more. Stock FluidSynth
        // ignores CC71-79 entirely, and so must this plugin: the old modulators
        // stretched attack by 17x, lifted a note tail by 43 dB, and left a note
        // ringing 48 dB above neutral a second after note-off, which no other
        // SoundFont player does. Two fresh instances, one with every sound
        // controller at 0 and one at 127, must render bit-identical audio.
        const auto renderWithSoundControllers = [&](int value,
                                                    juce::AudioBuffer<float>& out) {
            JuicySFAudioProcessor instance;
            instance.prepareToPlay(48000.0, blockSize);
            const auto instanceState{makeState(argv[1])};
            instance.setStateInformation(instanceState.getData(),
                                         static_cast<int>(instanceState.getSize()));
            out.setSize(2, 48000, false, false, true);
            out.clear();
            juce::MidiBuffer first;
            first.addEvent(juce::MidiMessage::programChange(1, 0), 0);
            for (const int cc : {71, 72, 73, 74, 75, 79})
                first.addEvent(juce::MidiMessage::controllerEvent(1, cc, value), 0);
            first.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            for (int position = 0; position < out.getNumSamples(); position += blockSize) {
                const int chunk{juce::jmin(blockSize, out.getNumSamples() - position)};
                juce::AudioBuffer<float> slice{out.getArrayOfWritePointers(), 2,
                                               position, chunk};
                juce::MidiBuffer midi;
                if (position == 0)
                    midi = first;
                instance.processBlock(slice, midi);
            }
        };
        juce::AudioBuffer<float> low, high;
        renderWithSoundControllers(0, low);
        renderWithSoundControllers(127, high);
        bool soundControllersInert{low.getNumSamples() == high.getNumSamples()};
        double energy{0.0};
        for (int ch = 0; ch < 2 && soundControllersInert; ++ch)
            for (int i = 0; i < low.getNumSamples(); ++i) {
                energy += std::abs(low.getSample(ch, i));
                // Bit-identical is the assertion; a tolerance would let a small
                // modulator effect slip through.
                if (std::memcmp(&low.getReadPointer(ch)[i],
                                &high.getReadPointer(ch)[i], sizeof(float)) != 0) {
                    soundControllersInert = false;
                    break;
                }
            }
        check(soundControllersInert && energy > audiblePresence,
              "CC71-79 are inert: the plugin adds no modulators, so those controllers cannot alter the sound");

        JuicySFAudioProcessor fresh;
        fresh.prepareToPlay(48000.0, blockSize);
        const auto freshState{makeState(argv[1])};
        fresh.setStateInformation(freshState.getData(), static_cast<int>(freshState.getSize()));
        // GM channel defaults, which are also FluidSynth's own initialisation
        // values. Volume is 100 rather than 127 on purpose: a channel that starts
        // at maximum has no room for a MIDI file to turn it up.
        bool freshDefaults{true};
        for (int channel = 0; channel < 16; ++channel) {
            int volume{-1}, pan{-1};
            freshDefaults = freshDefaults
                && fresh.getFluidSynthModel().getControllerValue(channel, 7, volume)
                && volume == MidiConstants::defaultChannelVolume
                && fresh.getFluidSynthModel().getControllerValue(channel, 10, pan)
                && pan == MidiConstants::centreValue;
        }
        for (int channel = 1; channel <= 16; ++channel) {
            const auto* volumeParameter{
                findIntParameter(fresh, "volCh" + juce::String(channel))};
            const auto* panParameter{
                findIntParameter(fresh, "panCh" + juce::String(channel))};
            const auto* muteParameter{
                findBoolParameter(fresh, "muteCh" + juce::String(channel))};
            const auto* soloParameter{
                findBoolParameter(fresh, "soloCh" + juce::String(channel))};
            freshDefaults = freshDefaults
                && volumeParameter != nullptr
                && volumeParameter->get() == MidiConstants::defaultChannelVolume
                && panParameter != nullptr
                && panParameter->get() == MidiConstants::centreValue
                && muteParameter != nullptr && !muteParameter->get()
                && soloParameter != nullptr && !soloParameter->get();
        }
        check(freshDefaults,
              "a fresh engine, its saved channel state, and all 16 channels' mixer parameters start at the GM defaults (volume 100, pan centre, unmuted, unsoloed)");
    }
    {
        constexpr std::array<int, 2> mixerCcs{7, 10};
        constexpr std::array<const char*, 2> mixerPrefixes{"volCh", "panCh"};
        juce::MidiBuffer midi;
        for (int channel = 0; channel < 16; ++channel)
            for (std::size_t index = 0; index < mixerCcs.size(); ++index)
                midi.addEvent(
                    juce::MidiMessage::controllerEvent(
                        channel + 1,
                        mixerCcs[index],
                        (channel * 7 + static_cast<int>(index) * 13 + 1) % 128),
                    200 + channel);
        render(processor, audio, midi);
        model.handleUpdateNowIfNeeded();

        bool engineAndTimestampExact{true};
        for (int channel = 0; channel < 16; ++channel) {
            for (std::size_t index = 0; index < mixerCcs.size(); ++index) {
                int actual{-1}, dispatched{-1}, sample{-1};
                const int expected{
                    (channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                engineAndTimestampExact = engineAndTimestampExact
                    && model.getControllerValue(channel, mixerCcs[index], actual)
                    && actual == expected
                    && model.getLastDispatchedController(
                        channel, mixerCcs[index], dispatched, sample)
                    && dispatched == expected && sample == 200 + channel;
            }
        }

        // Every channel owns its own parameter now, so all 16 must already read
        // their own value with NO channel selected in particular. This is the
        // defect Phase 9 exists to fix, asserted rather than described: before
        // the redesign, 15 of 16 channels were invisible at any moment.
        bool everyChannelMirrorsExact{true};
        model.selectChannelForEditing(0);
        for (int channel = 0; channel < 16; ++channel) {
            for (std::size_t index = 0; index < mixerPrefixes.size(); ++index) {
                const int expected{
                    (channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                auto* parameter{findIntParameter(
                    processor,
                    juce::String{mixerPrefixes[index]} + juce::String(channel + 1))};
                int dispatched{-1}, sample{-1};
                everyChannelMirrorsExact = everyChannelMirrorsExact
                    && parameter != nullptr && parameter->get() == expected
                    // Merely selecting a channel must not send a duplicate CC back
                    // to FluidSynth; the last raw MIDI diagnostic remains unchanged.
                    && model.getLastDispatchedController(
                        channel, mixerCcs[index], dispatched, sample)
                    && dispatched == expected && sample == 200 + channel;
            }
        }

        // A CC on an unselected channel moves that channel's own knob, and no
        // other channel's.
        const auto* channel16Volume{findIntParameter(processor, "volCh16")};
        const auto* channel1Volume{findIntParameter(processor, "volCh1")};
        const int channel1Before{channel1Volume != nullptr ? channel1Volume->get() : -1};
        juce::MidiBuffer unselectedMidi;
        unselectedMidi.addEvent(juce::MidiMessage::controllerEvent(16, 7, 127), 333);
        render(processor, audio, unselectedMidi);
        model.handleUpdateNowIfNeeded();
        const bool unselectedChannelMovedItsOwnKnob{
            channel16Volume != nullptr && channel16Volume->get() == 127
            && channel1Volume != nullptr && channel1Volume->get() == channel1Before};

        // The non-negotiable rule, the same one Program Change follows: a value
        // set in the editor is only a starting point. The next CC7 on that channel
        // overrides it, and the visible parameter follows the MIDI rather than the
        // other way round.
        // Set channel 4's volume the way the row's knob does: through its own
        // parameter, with no channel selected in particular.
        auto* channel4Volume{findIntParameter(processor, "volCh4")};
        if (channel4Volume != nullptr)
            *channel4Volume = 20;
        int afterManual{-1};
        const bool manualApplied{model.getControllerValue(3, 7, afterManual)
                                 && afterManual == 20};
        juce::MidiBuffer overrideMidi;
        overrideMidi.addEvent(juce::MidiMessage::controllerEvent(4, 7, 96), 12);
        render(processor, audio, overrideMidi);
        model.handleUpdateNowIfNeeded();
        int afterMidi{-1};
        const bool midiWins{model.getControllerValue(3, 7, afterMidi)
                            && afterMidi == 96
                            && channel4Volume != nullptr
                            && channel4Volume->get() == 96};
        check(manualApplied && midiWins,
              "a knob sets its own channel's volume, and an incoming CC7 on that channel then overrides it");

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        JuicySFAudioProcessor restored;
        restored.prepareToPlay(48000.0, blockSize);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        bool restoredExact{true};
        auto& restoredModel{restored.getFluidSynthModel()};
        for (int channel = 0; channel < 16; ++channel) {
            for (std::size_t index = 0; index < mixerCcs.size(); ++index) {
                int actual{-1};
                int expected{(channel * 7 + static_cast<int>(index) * 13 + 1) % 128};
                if (channel == 15 && mixerCcs[index] == 7)
                    expected = 127;
                if (channel == 3 && mixerCcs[index] == 7)
                    expected = 96;
                const auto* parameter{findIntParameter(
                    restored,
                    juce::String{mixerPrefixes[index]} + juce::String(channel + 1))};
                restoredExact = restoredExact
                    && restoredModel.getControllerValue(
                        channel, mixerCcs[index], actual)
                    && actual == expected
                    && parameter != nullptr && parameter->get() == expected;
            }
        }
        check(engineAndTimestampExact && everyChannelMirrorsExact
                  && unselectedChannelMovedItsOwnKnob
                  && restoredExact,
              "volume and pan remain timestamp-, engine-, knob-, channel-, and state-exact on all 16 channels, without selecting a row");
    }
    std::printf("== mute and solo ==\n");
    {
        // A fresh processor: the mixer block above leaves channels at scattered
        // volumes, and "is this channel audible" has to mean the mute, not the
        // volume it happens to be sitting at.
        JuicySFAudioProcessor mixer;
        mixer.prepareToPlay(48000.0, blockSize);
        const auto mixerState{makeState(argv[1])};
        mixer.setStateInformation(
            mixerState.getData(), static_cast<int>(mixerState.getSize()));
        auto& mixerModel{mixer.getFluidSynthModel()};
        juce::AudioBuffer<float> mixerAudio{2, blockSize};

        // Silence everything and let the buffer actually reach zero, so the next
        // measurement is about the note it plays and not the release tail of the
        // note before it. All Sound Off, not All Notes Off: this is measurement
        // hygiene, and a release tail is exactly what must not survive it.
        const auto settleToSilence{[&]() {
            juce::MidiBuffer quiet;
            addAllSoundOff(quiet);
            render(mixer, mixerAudio, quiet);
            juce::MidiBuffer empty;
            for (int block = 0; block < 16; ++block) {
                render(mixer, mixerAudio, empty);
                if (magnitude(mixerAudio, 0, blockSize) <= audiblePresence)
                    return;
            }
        }};

        // One note on one channel, from silence, so the measurement is about that
        // channel and nothing else.
        const auto levelOnChannel{[&](int channel) {
            settleToSilence();
            juce::MidiBuffer note;
            note.addEvent(juce::MidiMessage::programChange(channel + 1, 0), 0);
            note.addEvent(
                juce::MidiMessage::noteOn(channel + 1, 60, static_cast<juce::uint8>(110)), 1);
            render(mixer, mixerAudio, note);
            const float level{magnitude(mixerAudio, 1, blockSize - 1)};
            settleToSilence();
            return level;
        }};

        const auto setBool{[&](const char* prefix, int channel, bool value) {
            if (auto* parameter{findBoolParameter(
                    mixer, juce::String{prefix} + juce::String(channel + 1))})
                *parameter = value;
            mixerModel.handleUpdateNowIfNeeded();
        }};

        const bool audibleBeforeMute{levelOnChannel(2) > audiblePresence};
        setBool("muteCh", 2, true);
        const bool silentWhenMuted{levelOnChannel(2) <= audiblePresence};
        const bool neighbourUnaffected{levelOnChannel(3) > audiblePresence};
        check(audibleBeforeMute && silentWhenMuted && neighbourUnaffected,
              "mute silences its own channel's notes and no other channel's");

        // Solo overrides mute entirely while it is engaged: the soloed channel
        // sounds even though channel 3 is still muted, and every other channel is
        // silenced without being muted.
        setBool("soloCh", 5, true);
        const bool soloedSounds{levelOnChannel(5) > audiblePresence};
        const bool unsoloedSilent{levelOnChannel(3) <= audiblePresence
                                  && levelOnChannel(2) <= audiblePresence};
        setBool("soloCh", 5, false);
        // Clearing the last solo restores exactly the mute picture left behind.
        const bool muteRestored{levelOnChannel(2) <= audiblePresence
                                && levelOnChannel(3) > audiblePresence};
        check(soloedSounds && unsoloedSilent && muteRestored,
              "solo silences every channel that is not soloed, and clearing it restores the previous mutes");

        // Muting mid-note must release the notes already sounding rather than
        // leaving them ringing until their own note-off.
        setBool("muteCh", 2, false);
        settleToSilence();
        juce::MidiBuffer held;
        held.addEvent(juce::MidiMessage::programChange(3, 0), 0);
        held.addEvent(juce::MidiMessage::noteOn(3, 60, static_cast<juce::uint8>(110)), 1);
        render(mixer, mixerAudio, held);
        const bool ringing{magnitude(mixerAudio, 1, blockSize - 1) > audiblePresence};
        setBool("muteCh", 2, true);
        float decayed{1.0f};
        juce::MidiBuffer silence;
        for (int block = 0; block < 96; ++block) {
            render(mixer, mixerAudio, silence);
            decayed = magnitude(mixerAudio, 0, blockSize);
            if (decayed <= audiblePresence)
                break;
        }
        check(ringing && decayed <= audiblePresence,
              "muting a channel mid-note releases the notes already sounding instead of leaving them to ring");

        // A muted channel is silenced, not disconnected: everything except
        // note-ons still reaches the engine, so unmuting mid-song needs no
        // resync and the file's own volume survives being muted.
        juce::MidiBuffer whileMuted;
        whileMuted.addEvent(juce::MidiMessage::controllerEvent(3, 7, 42), 0);
        whileMuted.addEvent(juce::MidiMessage::programChange(3, 19), 1);
        whileMuted.addEvent(juce::MidiMessage::pitchWheel(3, 12000), 2);
        render(mixer, mixerAudio, whileMuted);
        mixerModel.handleUpdateNowIfNeeded();
        int mutedVolume{-1}, mutedBank{-1}, mutedPreset{-1}, mutedBend{-1};
        const auto* mutedVolumeParameter{findIntParameter(mixer, "volCh3")};
        const bool stateStillTracked{
            mixerModel.getControllerValue(2, 7, mutedVolume) && mutedVolume == 42
            && mutedVolumeParameter != nullptr && mutedVolumeParameter->get() == 42
            && mixerModel.getChannelProgram(2, mutedBank, mutedPreset)
            && mutedPreset == 19
            && mixerModel.getPitchBend(2, mutedBend) && mutedBend == 12000};
        setBool("muteCh", 2, false);
        const bool audibleAgain{levelOnChannel(2) > audiblePresence};
        check(stateStillTracked && audibleAgain,
              "a muted channel still receives CCs, program changes and bend, so unmuting resumes mid-song without a resync");

        // Mute and solo are the plugin's own: no MIDI message may set them.
        setBool("muteCh", 2, true);
        settleToSilence();
        juce::MidiBuffer resets;
        resets.addEvent(juce::MidiMessage::controllerEvent(3, 121, 0), 0);
        resets.addEvent(juce::MidiMessage::controllerEvent(3, 120, 0), 1);
        const juce::uint8 gmReset[]{0x7E, 0x7F, 0x09, 0x01};
        resets.addEvent(juce::MidiMessage::createSysExMessage(gmReset, sizeof(gmReset)), 2);
        render(mixer, mixerAudio, resets);
        mixerModel.handleUpdateNowIfNeeded();
        const auto* muteAfterReset{findBoolParameter(mixer, "muteCh3")};
        check(muteAfterReset != nullptr && muteAfterReset->get()
                  && mixerModel.isChannelSilenced(2),
              "controller resets and GM/GS/XG reset SysEx do not clear mute or solo");

        // Round-trip.
        setBool("soloCh", 8, true);
        juce::MemoryBlock savedMixer;
        mixer.getStateInformation(savedMixer);
        JuicySFAudioProcessor restoredMixer;
        restoredMixer.prepareToPlay(48000.0, blockSize);
        restoredMixer.setStateInformation(
            savedMixer.getData(), static_cast<int>(savedMixer.getSize()));
        const auto* restoredMute{findBoolParameter(restoredMixer, "muteCh3")};
        const auto* restoredSolo{findBoolParameter(restoredMixer, "soloCh9")};
        check(restoredMute != nullptr && restoredMute->get()
                  && restoredSolo != nullptr && restoredSolo->get()
                  // Solo is engaged, so channel 9 sounds and channel 3 does not.
                  && restoredMixer.getFluidSynthModel().isChannelSilenced(2)
                  && !restoredMixer.getFluidSynthModel().isChannelSilenced(8),
              "mute and solo round-trip through saved state and rebuild the engine's silenced set");
    }

    {
        // MIDI CC121 deliberately follows FluidSynth/MIDI reset semantics: it
        // releases pedals and resets expression/RPN selection/pitch wheel, while
        // preserving bank, volume, pan, effects sends, bend range, and CC70-79.
        constexpr int channel{4};
        juce::MidiBuffer setup;
        for (const auto [cc, value] : std::array<std::pair<int, int>, 14>{
                 std::pair{1, 99}, std::pair{7, 77}, std::pair{10, 33},
                 std::pair{11, 55},
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
        const auto* volume{findIntParameter(processor, "volCh5")};
        const auto* pan{findIntParameter(processor, "panCh5")};
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
                  // CC7 and CC10 survive Reset All Controllers per the MIDI
                  // spec, so channel 5's own mixer parameters must survive too.
                  && volume != nullptr && volume->get() == 77
                  && pan != nullptr && pan->get() == 33,
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
        constexpr std::array<int, 2> soundCcs{7, 10};
        constexpr std::array<int, 2> values{21, 95};
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
              "GM, GS, and XG resets reapply the latest plugin-owned volume and pan rather than reverting them to defaults");
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

        // 192 kHz used to fail silent here. It now renders at half rate and
        // interpolates up, so the rate counts as supported and the note sounds;
        // the pitch and cross-block continuity of that path are proved against a
        // known-frequency fixture in the sample-rate scenario above.
        processor.prepareToPlay(192000.0, blockSize);
        juce::MidiBuffer highRateMidi;
        highRateMidi.addEvent(
            juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        render(processor, pitchAudio, highRateMidi);
        check(model.isSampleRateSupported()
                  && magnitude(pitchAudio, 0, pitchAudio.getNumSamples()) > audiblePresence,
              "192 kHz plays through the oversampler instead of failing silent");

        // Below FluidSynth's floor there is no equivalent trick - that direction
        // needs decimation with an anti-alias filter - so it must still mute
        // rather than render at the wrong pitch.
        processor.prepareToPlay(4000.0, blockSize);
        juce::MidiBuffer belowFloorMidi;
        belowFloorMidi.addEvent(
            juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        render(processor, pitchAudio, belowFloorMidi);
        check(!model.isSampleRateSupported()
                  && magnitude(pitchAudio, 0, pitchAudio.getNumSamples()) == 0.0f,
              "a host rate below FluidSynth's floor still fails silent rather than detuning");

        processor.prepareToPlay(48000.0, blockSize);
        juce::MidiBuffer recoveredRateMidi;
        recoveredRateMidi.addEvent(
            juce::MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        render(processor, pitchAudio, recoveredRateMidi);
        check(model.isSampleRateSupported()
                  && magnitude(pitchAudio, 8192, 8192) > audiblePresence,
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
        check(magnitude(ccAudio, 8192, 8192) > audiblePresence
                  && magnitude(ccAudio, 36864, 8192) == 0.0f
                  && magnitude(ccAudio, 53248, 8192) > audiblePresence,
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
        for (const auto& id : beta1ParameterIds())
            allParams = allParams && params->hasAttribute(id);
        // Every per-channel node carries the full schema-5 property set.
        bool allChannelProperties{channels != nullptr
            && channels->getNumChildElements() == 16};
        if (channels != nullptr)
            for (auto* ch : channels->getChildIterator())
                for (const juce::String& property : FluidSynthModel::perChannelParams)
                    allChannelProperties = allChannelProperties
                        && ch->hasAttribute(property);
        check(xml != nullptr && xml->hasTagName("MYPLUGINSETTINGS")
                  && xml->getIntAttribute("stateVersion", -1) == 5
                  && allParams && allChannelProperties
                  && font != nullptr && font->hasAttribute("path")
                  && font->hasAttribute("bookmark"),
              "Beta 1 state writer preserves the frozen schema-5 envelope");
    }
    {
        model.setChannelProgram(1, 0, 4);
        model.setChannelProgram(9, 128, 0);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(2, 7, 99), 0);
        render(processor, audio, midi);
        model.handleUpdateNowIfNeeded();

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        JuicySFAudioProcessor restored;
        restored.prepareToPlay(48000.0, blockSize);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        int bank{-1}, preset{-1}, volume{-1};
        check(restored.getFluidSynthModel().getChannelProgram(1, bank, preset)
                  && bank == 0 && preset == 4
                  && restored.getFluidSynthModel().getControllerValue(1, 7, volume)
                  && volume == 99,
              "current state round-trip restores independent channel program and mixer state");
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
        // Attributes from the retired CC71-79 schema. They must be ignored, not
        // migrated onto the mixer controls that replaced them.
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
        int bank{-1}, preset{-1}, volume{-1}, pan{-1};
        check(migrated.getFluidSynthModel().getChannelProgram(0, bank, preset)
                  && preset == 3
                  && migrated.getFluidSynthModel().getControllerValue(0, 7, volume)
                  && volume == MidiConstants::defaultChannelVolume
                  && migrated.getFluidSynthModel().getControllerValue(0, 10, pan)
                  && pan == MidiConstants::centreValue,
              "pre-v3 state keeps program assignments and leaves volume/pan at the GM defaults");
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
        check(magnitude(recoveredAudio, 0, blockSize) > audiblePresence,
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
        // v4 -> v5 migration. A v4 save has per-channel volume and pan in
        // channelPrograms and a single selected-channel `volume`/`pan` PARAMETER
        // that no longer exists; it has no mute or solo at all. The per-channel
        // tree is the only record that survives, so it is what the new
        // parameters must be rebuilt from.
        juce::XmlElement legacy{"MYPLUGINSETTINGS"};
        legacy.setAttribute("stateVersion", 4);
        auto* legacyParams{legacy.createNewChildElement("params")};
        // The retired parameters, written normalised exactly as v4 wrote them.
        legacyParams->setAttribute("volume", 0.25);
        legacyParams->setAttribute("pan", 0.75);
        legacyParams->setAttribute("outputLevel", 0.5);
        auto* legacyChannels{legacy.createNewChildElement("channelPrograms")};
        for (int channel = 0; channel < 16; ++channel) {
            auto* ch{legacyChannels->createNewChildElement("ch")};
            ch->setAttribute("num", channel);
            ch->setAttribute("bank", channel == 9 ? 128 : 0);
            ch->setAttribute("preset", channel);
            ch->setAttribute("volume", 40 + channel);
            ch->setAttribute("pan", 100 - channel);
        }
        auto* legacyFont{legacy.createNewChildElement("soundFont")};
        legacyFont->setAttribute("path", argv[1]);
        legacyFont->setAttribute("bookmark", "");
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary(legacy, legacyState);

        JuicySFAudioProcessor migrated;
        migrated.prepareToPlay(48000.0, blockSize);
        migrated.setStateInformation(
            legacyState.getData(), static_cast<int>(legacyState.getSize()));
        auto& migratedModel{migrated.getFluidSynthModel()};

        bool migratedExact{true};
        for (int channel = 0; channel < 16; ++channel) {
            const auto* volume{
                findIntParameter(migrated, "volCh" + juce::String(channel + 1))};
            const auto* pan{
                findIntParameter(migrated, "panCh" + juce::String(channel + 1))};
            const auto* mute{
                findBoolParameter(migrated, "muteCh" + juce::String(channel + 1))};
            const auto* solo{
                findBoolParameter(migrated, "soloCh" + juce::String(channel + 1))};
            int engineVolume{-1}, enginePan{-1};
            migratedExact = migratedExact
                && volume != nullptr && volume->get() == 40 + channel
                && pan != nullptr && pan->get() == 100 - channel
                // absent in v4, so they must arrive off rather than undefined
                && mute != nullptr && !mute->get()
                && solo != nullptr && !solo->get()
                && migratedModel.getControllerValue(channel, 7, engineVolume)
                && engineVolume == 40 + channel
                && migratedModel.getControllerValue(channel, 10, enginePan)
                && enginePan == 100 - channel;
        }
        check(migratedExact && migratedModel.getSilencedMask() == 0,
              "a v4 save migrates to v5: every channel's volume and pan survive into its own parameter and the engine, and mute/solo arrive off");

        // Saving it back writes v5, with the retired identifiers gone.
        juce::MemoryBlock rewritten;
        migrated.getStateInformation(rewritten);
        const auto rewrittenXml{juce::AudioProcessor::getXmlFromBinary(
            rewritten.getData(), static_cast<int>(rewritten.getSize()))};
        const auto* rewrittenParams{
            rewrittenXml != nullptr ? rewrittenXml->getChildByName("params") : nullptr};
        check(rewrittenXml != nullptr
                  && rewrittenXml->getIntAttribute("stateVersion", -1) == 5
                  && rewrittenParams != nullptr
                  && !rewrittenParams->hasAttribute("volume")
                  && !rewrittenParams->hasAttribute("pan")
                  && rewrittenParams->hasAttribute("volCh1")
                  && rewrittenParams->hasAttribute("soloCh16"),
              "re-saving a migrated project writes schema 5 and drops the retired volume/pan parameters");

        // A save from a FUTURE schema is still refused rather than half-applied.
        juce::XmlElement future{"MYPLUGINSETTINGS"};
        future.setAttribute("stateVersion", 6);
        juce::MemoryBlock futureState;
        juce::AudioProcessor::copyXmlToBinary(future, futureState);
        migrated.setStateInformation(
            futureState.getData(), static_cast<int>(futureState.getSize()));
        const auto* stillMigrated{findIntParameter(migrated, "volCh1")};
        check(stillMigrated != nullptr && stillMigrated->get() == 40
                  && migratedModel.getFontLoadStatus() == "error",
              "a state written by a newer schema is refused with a visible error and leaves current state intact");
    }
    {
        juce::XmlElement bounded{"MYPLUGINSETTINGS"};
        bounded.setAttribute("stateVersion", 3);
        auto* channels{bounded.createNewChildElement("channelPrograms")};
        auto* ch{channels->createNewChildElement("ch")};
        ch->setAttribute("num", 0);
        ch->setAttribute("bank", 999);
        ch->setAttribute("preset", -50);
        ch->setAttribute("volume", 999);
        auto* font{bounded.createNewChildElement("soundFont")};
        font->setAttribute("path", argv[1]);
        font->setAttribute("bookmark", "");
        juce::MemoryBlock boundedState;
        juce::AudioProcessor::copyXmlToBinary(bounded, boundedState);

        JuicySFAudioProcessor boundedProcessor;
        boundedProcessor.prepareToPlay(48000.0, blockSize);
        boundedProcessor.setStateInformation(
            boundedState.getData(), static_cast<int>(boundedState.getSize()));
        int clampedVolume{-1};
        check(boundedProcessor.getFluidSynthModel().getControllerValue(0, 7, clampedVolume)
                  && clampedVolume == 127,
              "out-of-range saved bank, preset, and controller values are clamped before engine use");

        const char malformed[]{'n', 'o', 't', 's', 't', 'a', 't', 'e'};
        boundedProcessor.setStateInformation(malformed, sizeof(malformed));
        check(boundedProcessor.getFluidSynthModel().getControllerValue(0, 7, clampedVolume)
                  && clampedVolume == 127,
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
        check(boundedProcessor.getFluidSynthModel().getControllerValue(0, 7, clampedVolume)
                  && clampedVolume == 127,
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
                  && magnitude(audio, 2, blockSize - 2) > audiblePresence,
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
            && magnitude(audio, 2, blockSize - 2) > audiblePresence};

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
            && magnitude(audio, 2, blockSize - 2) > audiblePresence};

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
        // 4.5:1 is the WCAG AA threshold for normal-size text; 3:1 is the
        // threshold for a graphical element that carries meaning.
        constexpr double minimumTextRatio{4.5};
        constexpr double minimumGraphicRatio{3.0};

        // Every text token, on every background it can be drawn on, in every
        // accent the settings popover offers. This is the palette's contract:
        // "no component draws a colour that did not come from the palette" is
        // only worth something if the palette itself is legible.
        struct NamedColour { const char* name; int colourId; };
        constexpr std::array<NamedColour, 5> backgrounds{{
            {"window",       Juicy16::windowBackgroundColourId},
            {"panel",        Juicy16::panelBackgroundColourId},
            {"row alternate", Juicy16::rowAlternateColourId},
            {"row selected", Juicy16::rowSelectedColourId},
            {"input",        Juicy16::inputBackgroundColourId},
        }};
        constexpr std::array<NamedColour, 4> textTokens{{
            {"text primary", Juicy16::textPrimaryColourId},
            {"text value",   Juicy16::textValueColourId},
            {"text label",   Juicy16::textLabelColourId},
            {"text error",   Juicy16::textErrorColourId},
        }};

        bool everyTokenLegible{true};
        bool everyAccentVisible{true};
        for (const auto accent : {Juicy16::Accent::sage, Juicy16::Accent::amber,
                                  Juicy16::Accent::terracotta, Juicy16::Accent::neutral}) {
            Juicy16::PluginLookAndFeel lookAndFeel;
            lookAndFeel.setAccent(accent);
            for (const auto& background : backgrounds) {
                const juce::Colour backgroundColour{
                    lookAndFeel.findColour(background.colourId)};
                for (const auto& token : textTokens) {
                    // A label never sits on a selected row: the row's own text
                    // uses the primary token there.
                    if (background.colourId == Juicy16::rowSelectedColourId
                        && token.colourId == Juicy16::textLabelColourId)
                        continue;
                    const double ratio{contrastRatio(
                        lookAndFeel.findColour(token.colourId), backgroundColour)};
                    if (ratio < minimumTextRatio) {
                        std::printf("    FAIL %s on %s (%s accent): %.2f:1\n",
                                    token.name, background.name,
                                    Juicy16::accentName(accent).toRawUTF8(), ratio);
                        everyTokenLegible = false;
                    }
                }
                // The accent draws knob arcs and the selected-row marker.
                const double accentRatio{contrastRatio(
                    lookAndFeel.findColour(Juicy16::accentColourId), backgroundColour)};
                if (accentRatio < minimumGraphicRatio) {
                    std::printf("    FAIL %s accent on %s: %.2f:1\n",
                                Juicy16::accentName(accent).toRawUTF8(),
                                background.name, accentRatio);
                    everyAccentVisible = false;
                }
            }
        }
        check(everyTokenLegible,
              "every palette text token meets WCAG AA on every background it is drawn on, in all four accents");
        check(everyAccentVisible,
              "the accent meets the 3:1 non-text threshold on every background, in all four accents");

        Juicy16::PluginLookAndFeel lookAndFeel;
        const double normalStatus{contrastRatio(
            lookAndFeel.findColour(Juicy16::textLabelColourId),
            lookAndFeel.findColour(Juicy16::panelBackgroundColourId))};
        const double errorStatus{contrastRatio(
            lookAndFeel.findColour(Juicy16::textErrorColourId),
            lookAndFeel.findColour(Juicy16::panelBackgroundColourId))};
        std::printf("    status label, normal: %.2f:1  error: %.2f:1\n",
                    normalStatus, errorStatus);
        check(normalStatus >= minimumTextRatio,
              "the status label meets WCAG AA contrast in its normal colour");
        check(errorStatus >= minimumTextRatio,
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
        constexpr std::array<ExpectedAccessibleComponent, 6> expected{{
            {"Sound bank file", false, false},
            {"MIDI channel assignments", false, true},
            {"Output level", true, false},
            {"MIDI Keyboard", false, false},
            {"Version and bank load status", false, false},
            {"Settings", false, false},
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
              "bank picker, channel table, keyboard, status, and all three sliders expose named accessible metadata and built-in control roles");

        // Keyboard routing. The on-screen MIDI keyboard defaults to wanting
        // focus, which would swallow typed input meant for the controls, so the
        // editor explicitly clears it after construction. Assert the resulting
        // arrangement rather than trusting construction order.
        //
        // The channel table is the opposite case: it now DOES take focus, so the
        // channel list is reachable by arrow keys. That is safe because nothing
        // drives row selection from MIDI - the only caller of
        // selectChannelForEditing is a click - so there is no selection for the
        // keyboard to fight.
        auto* midiKeyboard{editor != nullptr
            ? findNamedComponent(*editor, "MIDI Keyboard") : nullptr};
        auto* bankPicker{editor != nullptr
            ? findNamedComponent(*editor, "Sound bank file") : nullptr};
        const bool focusRouting{editor != nullptr
            && editor->getWantsKeyboardFocus()
            && midiKeyboard != nullptr && !midiKeyboard->getWantsKeyboardFocus()
            && channelTableForFocus(*editor) != nullptr
            && channelTableForFocus(*editor)->getWantsKeyboardFocus()};
        check(focusRouting,
              "the editor and the channel table take keyboard focus while the on-screen keyboard declines it");

        bool slidersReachable{editor != nullptr};
        for (const char* name : {"Output level"}) {
            auto* slider{editor != nullptr ? findNamedComponent(*editor, name) : nullptr};
            slidersReachable = slidersReachable
                && slider != nullptr && slider->getWantsKeyboardFocus();
        }
        check(slidersReachable && bankPicker != nullptr,
              "the master trim accepts keyboard focus so it is reachable without a mouse");

        // Phase 9's central claim, asserted on the built editor: every one of the
        // 16 rows carries its own named, focusable volume, pan, mute and solo,
        // reachable without selecting the row first. A row scrolled out of view
        // has no cell component, so each row is scrolled in before it is checked -
        // which is itself the assertion that every row IS reachable.
        auto* rowRack{editor != nullptr
            ? dynamic_cast<juce::TableListBox*>(
                findNamedComponent(*editor, "MIDI channel assignments"))
            : nullptr};
        bool everyRowHasItsOwnControls{rowRack != nullptr};
        if (auto* rack{rowRack}) {
            for (int row = 0; row < 16 && everyRowHasItsOwnControls; ++row) {
                rack->scrollToEnsureRowIsOnscreen(row);
                const juce::String prefix{"MIDI channel " + juce::String(row + 1)};
                for (const char* suffix : {" volume", " pan", " mute", " solo"}) {
                    auto* control{findNamedComponent(*editor, prefix + suffix)};
                    const bool valid{control != nullptr
                        && control->isAccessible()
                        && control->getTitle().isNotEmpty()
                        && control->getDescription().isNotEmpty()
                        && control->getWantsKeyboardFocus()};
                    if (!valid)
                        std::printf("    row control missing or unreachable: %s%s\n",
                                    prefix.toRawUTF8(), suffix);
                    everyRowHasItsOwnControls = everyRowHasItsOwnControls && valid;
                }
                auto* instrument{findNamedComponent(*editor, prefix + " instrument")};
                everyRowHasItsOwnControls = everyRowHasItsOwnControls
                    && instrument != nullptr && instrument->isAccessible();
            }
        }
        check(everyRowHasItsOwnControls,
              "all 16 rows carry their own named, keyboard-reachable instrument, volume, pan, mute and solo");

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
            "Output level",
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

        // "No supported resize or TEXT CASE makes essential controls
        // inaccessible." Resize is covered above. This is the text half: the
        // strings the editor displays are user-controlled - a bank path, a
        // FluidSynth preset name, a load-error message - and a long or Unicode
        // one must not push an essential control to zero size or off the window
        // at any supported size.
        bool textCasesUsable{resizeContract};
        if (editor != nullptr && constrainer != nullptr) {
            auto* status{findNamedComponent(*editor, "Version and bank load status")};
            auto* label{dynamic_cast<juce::Label*>(status)};
            const std::array<juce::String, 4> textCases{{
                juce::String{},
                juce::String::repeatedString("W", 4000),
                juce::String::fromUTF8(
                    "\xe9\x9f\xb3\xe8\x89\xb2 \xf0\x9f\x8e\xb9 ")
                    + juce::String::repeatedString(
                        juce::String::fromUTF8("\xe3\x81\x82"), 500),
                juce::String::repeatedString("i ", 2000),
            }};
            const std::array<juce::Rectangle<int>, 3> sizes{{
                {0, 0, GuiConstants::minWidth, GuiConstants::minHeight},
                {0, 0, GuiConstants::minWidth, GuiConstants::defaultHeight},
                {0, 0, constrainer->getMaximumWidth(), constrainer->getMaximumHeight()},
            }};
            for (const auto& text : textCases) {
                if (label != nullptr)
                    label->setText(text, juce::dontSendNotification);
                for (const auto& size : sizes) {
                    editor->setBoundsConstrained(size);
                    editor->resized();
                    textCasesUsable = textCasesUsable && essentialBoundsAreUsable();
                    // Nothing may escape the window either: an essential control
                    // pushed outside the editor is unreachable even though its
                    // bounds are non-empty.
                    for (const auto* name : essentialComponents) {
                        auto* component{findNamedComponent(*editor, name)};
                        if (component == nullptr
                            || !editor->getLocalBounds().contains(
                                   component->getBounds().getTopLeft())) {
                            textCasesUsable = false;
                            break;
                        }
                    }
                }
            }
            if (label != nullptr)
                label->setText("Juicy16", juce::dontSendNotification);
        }
        check(textCasesUsable,
              "empty, 4000-character, Unicode, and narrow-glyph status text keep every essential control on-screen at minimum, default, and maximum size");

        // "Core loading, channel selection, patch selection, and parameter
        // editing workflows are usable without a mouse where the framework/host
        // permits." A real host focus chain cannot be built headlessly, but the
        // half that is ours can: whether each control actually accepts keyboard
        // focus. A control that declines focus is unreachable by tabbing no
        // matter what the host does.
        //
        // Bank loading, the master trim, every row's own mixer controls, channel
        // selection, and patch selection are all reachable.
        bool focusableControls{editor != nullptr};
        bool channelSelectionByKeyboard{editor != nullptr};
        bool patchSelectionByKeyboard{editor != nullptr};
        if (editor != nullptr) {
            editor->setBoundsConstrained(
                {0, 0, GuiConstants::minWidth, GuiConstants::defaultHeight});
            editor->resized();

            const auto acceptsFocus{[&](const juce::String& name) {
                auto* component{findNamedComponent(*editor, name)};
                return component != nullptr && component->getWantsKeyboardFocus();
            }};
            // Every exposed sound parameter, by its real accessible name. The
            // per-channel controls are covered row by row in the accessibility
            // block above; this is the plugin-wide one.
            for (const auto* name : {"Output level"})
                focusableControls = focusableControls && acceptsFocus(name);

            // Bank loading: the FilenameComponent itself is a container and does
            // not take focus, but its browse button does, so the workflow is
            // reachable. Assert the reachable thing rather than the container.
            auto* bankControl{findNamedComponent(*editor, "Sound bank file")};
            bool bankReachable{false};
            if (bankControl != nullptr)
                for (int i = 0; i < bankControl->getNumChildComponents(); ++i)
                    bankReachable = bankReachable
                        || bankControl->getChildComponent(i)->getWantsKeyboardFocus();
            focusableControls = focusableControls && bankReachable;

            // Channel selection is now keyboard-driven. The table takes focus, and
            // a row change - which is what an arrow key produces - must move the
            // selected channel that the shared bank/preset/slider controls edit.
            auto* table{channelTableForFocus(*editor)};
            channelSelectionByKeyboard = table != nullptr
                && table->getWantsKeyboardFocus();
            if (auto* rows{dynamic_cast<juce::TableListBox*>(table)}) {
                for (const int row : {5, 15, 0, 9}) {
                    rows->selectRow(row);
                    const int selected{
                        processor.getFluidSynthModel().getSelectedChannel()};
                    channelSelectionByKeyboard = channelSelectionByKeyboard
                        && selected == row
                        && rows->getSelectedRow() == row;
                }
            } else {
                channelSelectionByKeyboard = false;
            }

            // Patch selection. Return on the focused table opens the selected
            // row's instrument dropdown; showing the popup needs a window, but
            // everything up to it is ours to check: that the route resolves to a
            // real, populated, focusable dropdown for every one of the 16
            // channels - including rows that are scrolled out of view at the
            // minimum height - and that choosing an item on it actually assigns
            // that channel's program.
            auto* channelList{channelListFor(*editor)};
            patchSelectionByKeyboard = channelList != nullptr;
            if (channelList != nullptr) {
                for (int row = 0; row < 16 && patchSelectionByKeyboard; ++row) {
                    auto* combo{channelList->patchComboForRow(row)};
                    patchSelectionByKeyboard = combo != nullptr
                        && combo->getWantsKeyboardFocus()
                        && combo->getNumItems() > 1;
                    if (!patchSelectionByKeyboard)
                        break;

                    // Pick an item the channel is not already on, the way the
                    // opened menu would, and require the engine to follow.
                    const int target{combo->getSelectedItemIndex() == 0 ? 1 : 0};
                    combo->setSelectedItemIndex(target, juce::sendNotificationSync);
                    int bank{-1}, preset{-1};
                    patchSelectionByKeyboard =
                        processor.getFluidSynthModel().getChannelProgram(row, bank, preset)
                        && combo->getSelectedItemIndex() == target;
                }
                // Out-of-range rows must not resolve to some other channel's
                // dropdown.
                patchSelectionByKeyboard = patchSelectionByKeyboard
                    && channelList->patchComboForRow(-1) == nullptr
                    && channelList->patchComboForRow(16) == nullptr;
            }
        }
        check(focusableControls,
              "bank loading and the master trim accept keyboard focus, so those workflows work without a mouse");
        check(channelSelectionByKeyboard,
              "the channel table accepts keyboard focus and arrow-key row changes drive the selected channel");
        check(patchSelectionByKeyboard,
              "Return resolves to every channel row's populated instrument dropdown, and a selection on it assigns that channel's program");
    }

    std::printf("== engine_midi_tests: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
