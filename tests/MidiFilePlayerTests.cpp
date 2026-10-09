#include "PluginProcessor.h"
#include "MidiFilePlayer.h"
#include "MidiPlayerComponent.h"
#include "GuiConstants.h"
#include "FilePicker.h"
#include "SyntheticSf2.h"
#include "SyntheticDls.h"
#include "SyntheticFixtures.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <limits>
#include <vector>

namespace {
int failures{0};
constexpr double rate{48000.0};
class AssertionLogger final : public juce::Logger {
public:
    std::atomic<int> assertions{0};
    void logMessage(const juce::String& text) override {
        std::fprintf(stderr, "%s\n", text.toRawUTF8());
        if (text.startsWith("JUCE Assertion failure")) ++assertions;
    }
};
void check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) ++failures;
}
bool near(double a, double b) { return std::abs(a - b) < 1.0e-7; }
void add(juce::MidiMessageSequence& sequence, juce::MidiMessage event, double ticks) {
    event.setTimeStamp(ticks);
    sequence.addEvent(event);
}
void end(juce::MidiMessageSequence& sequence, double ticks) {
    add(sequence, juce::MidiMessage::endOfTrack(), ticks);
}
struct MidiFixture {
    juce::TemporaryFile temporary{".mid"};
    MidiFixture(const std::vector<juce::MidiMessageSequence>& tracks, int type = 1,
                bool smpte = false, int resolution = 480) {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(resolution);
        for (const auto& track : tracks) midi.addTrack(track);
        juce::MemoryOutputStream stream;
        const bool written = midi.writeTo(stream, type);
        juce::MemoryBlock bytes{stream.getData(), stream.getDataSize()};
        if (smpte && bytes.getSize() >= 14) {
            // JUCE 8.0.14 setSmpteTimeFormat shifts a negative integer (UB).
            // Encode the two unsigned header bytes directly for this fixture.
            auto* header = static_cast<juce::uint8*>(bytes.getData());
            header[12] = 256 - 25;
            header[13] = 40;
        }
        check(written && temporary.getFile().replaceWithData(bytes.getData(), bytes.getSize()), "MIDI fixture written");
    }
    const juce::File& file() const { return temporary.getFile(); }
};
bool import(MidiFilePlayer& player, const juce::File& file) {
    juce::String error;
    const bool loaded = player.loadFile(file, error);
    if (!loaded) std::fprintf(stderr, "Import: %s\n", error.toRawUTF8());
    check(loaded && error.isEmpty(), "valid MIDI imports without error");
    return loaded;
}
std::vector<int> noteOffsets(const juce::MidiBuffer& midi, int note = -1) {
    std::vector<int> result;
    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        if (message.isNoteOn() && (note < 0 || message.getNoteNumber() == note))
            result.push_back(metadata.samplePosition);
    }
    return result;
}
int matchingEvents(const juce::MidiBuffer& midi, int channel, int controller, int value) {
    int count{0};
    for (const auto event : midi) {
        const auto message = event.getMessage();
        if (message.isController() && message.getChannel() == channel
            && message.getControllerNumber() == controller && message.getControllerValue() == value)
            ++count;
    }
    return count;
}
void render(JuicySFAudioProcessor& processor, juce::AudioBuffer<float>& audio) {
    juce::MidiBuffer midi;
    audio.clear();
    processor.processBlock(audio, midi);
}
double energy(const juce::AudioBuffer<float>& audio) {
    double sum{0};
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
            const double value = audio.getSample(channel, sample);
            sum += value * value;
        }
    return sum;
}
std::unique_ptr<JuicySFAudioProcessor> makeProcessor(
        juce::AudioProcessor::WrapperType wrapper = juce::AudioProcessor::wrapperType_Standalone) {
    juce::AudioProcessor::setTypeOfNextNewPlugin(wrapper);
    auto processor = std::make_unique<JuicySFAudioProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    return processor;
}
void loadBank(JuicySFAudioProcessor& processor, const juce::File& bank) {
    processor.prepareToPlay(rate, 1024);
    juce::XmlElement state{"MYPLUGINSETTINGS"};
    state.setAttribute("stateVersion", 6);
    state.createNewChildElement("soundFont")->setAttribute("path", bank.getFullPathName());
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(state, bytes);
    processor.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    check(processor.getFluidSynthModel().getFontLoadStatus() == "loaded", "fixture bank loads");
}
juce::Component* namedChild(juce::Component& root, const juce::String& name) {
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = namedChild(*child, name)) return found;
    return nullptr;
}
void timingTests() {
    for (int type : {0, 1}) {
        juce::MidiMessageSequence tempo, notes;
        add(tempo, juce::MidiMessage::tempoMetaEvent(500000), 0);
        add(tempo, juce::MidiMessage::tempoMetaEvent(250000), 480);
        add(notes, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 96);
        add(notes, juce::MidiMessage::noteOff(1, 60), 192);
        add(notes, juce::MidiMessage::noteOn(16, 61, static_cast<juce::uint8>(100)), 480);
        add(notes, juce::MidiMessage::noteOff(16, 61), 576);
        add(notes, juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(100)), 960);
        end(notes, 1440);
        if (type == 0) notes.addSequence(tempo, 0.0);
        MidiFixture fixture{type == 0 ? std::vector<juce::MidiMessageSequence>{notes}
                                      : std::vector<juce::MidiMessageSequence>{tempo, notes}, type};
        JuicySFAudioProcessor processor;
        auto& player = processor.getMidiFilePlayer();
        if (!import(player, fixture.file())) continue;
        const auto status = player.getStatus();
        check(status.trackCount == (type == 0 ? 1 : 2) && status.channelCount == 2
            && near(status.durationSeconds, 1.0), "type 0/1 metadata includes every track, channel, and tempo change");
        check(near(status.tempoBpm, 120.0) && near(status.playbackTempoBpm, 120.0),
            "type 0/1 import exposes the explicit initial tempo");
        player.play();
        juce::MidiBuffer midi;
        player.appendNextBlock(midi, 500, 1000.0);
        check(noteOffsets(midi) == std::vector<int>{100}, "tempo-aware dispatch excludes the next block boundary");
        check(near(player.getStatus().tempoBpm, 240.0),
            "tempo metadata changes at the exact conductor-track boundary");
        midi.clear();
        player.appendNextBlock(midi, 500, 1000.0);
        check(noteOffsets(midi) == std::vector<int>({0, 250}), "boundary note occurs once at sample zero and changed tempo reaches sample 250");
        check(!player.getStatus().playing && near(player.getStatus().positionSeconds, 1.0),
            "natural end stops at the MIDI end-of-track duration");
        player.seek(0.25);
        check(near(player.getStatus().tempoBpm, 120.0), "backward seek restores the earlier source tempo");
        player.seek(0.75);
        player.setSpeed(1.5);
        check(near(player.getStatus().tempoBpm, 240.0) && near(player.getStatus().playbackTempoBpm, 360.0),
            "seek and speed report source BPM and effective playback BPM separately");
        player.setLoopRange(0.2, 0.6);
        player.setLooping(true);
        player.seek(0.55);
        player.play();
        midi.clear();
        player.appendNextBlock(midi, 100, 1000.0);
        check(near(player.getStatus().positionSeconds, 0.3) && near(player.getStatus().tempoBpm, 120.0)
            && near(player.getStatus().playbackTempoBpm, 180.0),
            "loop wrap restores the tempo at marker A while retaining the chosen speed");
    }
    juce::MidiMessageSequence ticks;
    add(ticks, juce::MidiMessage::tempoMetaEvent(1000000), 0);
    add(ticks, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 125);
    end(ticks, 500);
    MidiFixture fixture{{ticks}, 0, true};
    JuicySFAudioProcessor processor;
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, fixture.file())) return;
    player.play();
    juce::MidiBuffer midi;
    player.appendNextBlock(midi, 500, 1000.0);
    check(near(player.getStatus().durationSeconds, 0.5) && noteOffsets(midi) == std::vector<int>{125},
        "SMPTE division stays independent of tempo meta events");
    check(near(player.getStatus().tempoBpm, 60.0), "SMPTE files still expose their explicit tempo metadata");
}
void transportTests() {
    juce::MidiMessageSequence track;
    add(track, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 96);
    add(track, juce::MidiMessage::noteOff(1, 60), 192);
    add(track, juce::MidiMessage::noteOn(1, 61, static_cast<juce::uint8>(100)), 288);
    add(track, juce::MidiMessage::noteOff(1, 61), 384);
    end(track, 960);
    MidiFixture fixture{{track}, 0};
    JuicySFAudioProcessor processor;
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, fixture.file())) return;
    check(near(player.getStatus().tempoBpm, 120.0) && near(player.getStatus().playbackTempoBpm, 120.0),
        "files without tempo events use the standard 120 BPM default");
    player.setSpeed(2.0);
    check(near(player.getStatus().tempoBpm, 120.0) && near(player.getStatus().playbackTempoBpm, 240.0),
        "speed changes effective BPM without replacing the source tempo");
    player.seek(std::numeric_limits<double>::quiet_NaN());
    player.seek(std::numeric_limits<double>::infinity());
    player.setSpeed(std::numeric_limits<double>::quiet_NaN());
    player.setSpeed(std::numeric_limits<double>::infinity());
    check(near(player.getStatus().positionSeconds, 0.0) && near(player.getStatus().speed, 2.0),
        "nonfinite seeks and playback speeds preserve the valid transport state");
    player.play();
    juce::MidiBuffer midi;
    player.appendNextBlock(midi, 125, 1000.0);
    check(noteOffsets(midi) == std::vector<int>{50} && near(player.getStatus().positionSeconds, 0.25),
        "double speed advances the song clock and event sample offsets together");
    player.pause();
    midi.clear();
    player.appendNextBlock(midi, 500, 1000.0);
    check(noteOffsets(midi).empty() && near(player.getStatus().positionSeconds, 0.25),
        "pause silences transport without advancing position");
    player.play();
    midi.clear();
    player.appendNextBlock(midi, 100, 1000.0);
    check(noteOffsets(midi, 61) == std::vector<int>{25}, "resume keeps the remaining event's position at current speed");
    player.stop();
    midi.clear();
    player.appendNextBlock(midi, 100, 1000.0);
    check(!player.getStatus().playing && near(player.getStatus().positionSeconds, 0.0)
        && noteOffsets(midi).empty(), "stop returns to zero and dispatches no song notes");
    player.setSpeed(1.0);
    player.setLoopRange(0.2, 0.4);
    player.setLooping(true);
    player.seek(0.2);
    player.play();
    midi.clear();
    player.appendNextBlock(midi, 500, 1000.0);
    check(noteOffsets(midi, 61) == std::vector<int>({100, 300})
        && near(player.getStatus().positionSeconds, 0.3) && player.getStatus().playing,
        "A/B looping wraps repeatedly inside a block without including the end boundary");
    player.setLoopRange(0.0, 1.0);
    player.seek(0.95);
    midi.clear();
    player.appendNextBlock(midi, 200, 1000.0);
    check(noteOffsets(midi, 60) == std::vector<int>{150}
        && near(player.getStatus().positionSeconds, 0.15), "whole-song loop preserves sample offsets after the duration boundary");
    player.setLooping(false);
    player.seek(100.0);
    check(near(player.getStatus().positionSeconds, 1.0), "seek is bounded by the imported duration");
    player.seek(-1.0);
    check(near(player.getStatus().positionSeconds, 0.0), "negative seek is clamped to zero");
}
void fractionalBoundaryTests() {
    juce::MidiMessageSequence track;
    add(track, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 1999);
    end(track, 2000);
    MidiFixture file{{track}, 0, false, 1000};
    JuicySFAudioProcessor owner;
    MidiFilePlayer player{owner, [](juce::MidiBuffer&) {}};
    if (!import(player, file.file())) return;
    player.play();
    juce::MidiBuffer midi;
    player.appendNextBlock(midi, 1000, 1000.0);
    check(noteOffsets(midi) == std::vector<int>{999} && !player.getStatus().playing,
        "a note in the final half sample is delivered before the exact song-end boundary");
    player.play();
    midi.clear();
    player.appendNextBlock(midi, 100, 1000.0);
    check(matchingEvents(midi, 1, 120, 0) == 0 && matchingEvents(midi, 1, 123, 0) == 0,
        "Play before the next callback cancels the previous song-end panic");

    juce::MidiMessageSequence loop;
    add(loop, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 100);
    add(loop, juce::MidiMessage::noteOff(1, 60), 200);
    add(loop, juce::MidiMessage::noteOn(1, 61, static_cast<juce::uint8>(100)), 399);
    end(loop, 2000);
    MidiFixture loopFile{{loop}, 0, false, 1000};
    using ScheduledEvent = std::pair<int, std::vector<juce::uint8>>;
    const auto schedule = [&](const std::vector<int>& blocks) {
        MidiFilePlayer transport{owner, [](juce::MidiBuffer&) {}};
        import(transport, loopFile.file());
        transport.setLoopRange(0.0, 0.2005);
        transport.setLooping(true);
        transport.play();
        std::vector<ScheduledEvent> scheduled;
        int base{0};
        for (const int length : blocks) {
            juce::MidiBuffer events;
            transport.appendNextBlock(events, length, 1000.0);
            for (const auto event : events) {
                const auto message = event.getMessage();
                scheduled.emplace_back(base + event.samplePosition,
                    std::vector<juce::uint8>(message.getRawData(),
                        message.getRawData() + message.getRawDataSize()));
            }
            base += length;
        }
        return scheduled;
    };
    const auto whole = schedule({1000});
    const auto split = schedule({127, 73, 1, 200, 199, 400});
    check(whole == split,
        "fractional loop boundaries schedule identical notes, cleanup, and setup across block partitions");
    std::vector<int> lastNotes;
    for (const auto& event : whole)
        if (event.second.size() == 3 && event.second[0] == 0x90 && event.second[1] == 61)
            lastNotes.push_back(event.first);
    check(lastNotes == std::vector<int>({199, 400, 600, 801}),
        "the last fractional note before marker B survives every loop wrap");
}
void importGuardTests() {
    juce::MidiMessageSequence dense;
    add(dense, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    for (int i = 0; i < 8193; ++i)
        add(dense, juce::MidiMessage::controllerEvent(1, 96, 0), 48);
    end(dense, 1920);
    MidiFixture file{{dense}, 0};
    JuicySFAudioProcessor owner;
    MidiFilePlayer player{owner, [](juce::MidiBuffer&) {}};
    if (!import(player, file.file())) return;
    check(player.setLoopRange(0.0, 0.5), "a loop before dense controller history is accepted");
    player.setLooping(true);
    check(!player.setLoopRange(1.0, 1.25) && !player.setLoopRange(0.3, 0.31)
        && near(player.getStatus().loopStartSeconds, 0.0)
        && near(player.getStatus().loopEndSeconds, 0.5) && player.getStatus().looping,
        "complex or too-short A/B setup is rejected while preserving the valid active loop range");
    std::vector<juce::uint8> payload(65534, 0);
    payload[0] = 0x7d;
    juce::MidiMessageSequence oversized;
    add(oversized, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    add(oversized, juce::MidiMessage::createSysExMessage(payload.data(), static_cast<int>(payload.size())), 0);
    end(oversized, 480);
    MidiFixture oversizedFile{{oversized}, 0};
    const auto before = player.getStatus();
    juce::String error;
    check(!player.loadFile(oversizedFile.file(), error) && error.isNotEmpty()
        && player.getStatus().fileName == before.fileName,
        "an oversized complete SysEx packet is rejected before replacing the current song");

    juce::MidiMessageSequence shortSong;
    add(shortSong, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    end(shortSong, 24);
    MidiFixture shortFile{{shortSong}, 0};
    if (!import(player, shortFile.file())) return;
    player.setLooping(true);
    player.play();
    juce::MidiBuffer events;
    player.appendNextBlock(events, 50, 1000.0);
    check(!player.getStatus().looping && !player.getStatus().playing
        && near(player.getStatus().positionSeconds, 0.025)
        && noteOffsets(events) == std::vector<int>{0},
        "a song shorter than 50 ms plays once and declines looping");
}
void importFailureTests() {
    juce::MidiMessageSequence notes;
    add(notes, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 96);
    end(notes, 960);
    MidiFixture valid{{notes}, 0};
    JuicySFAudioProcessor processor;
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, valid.file())) return;
    player.seek(0.25);
    player.play();
    const auto before = player.getStatus();
    juce::TemporaryFile broken{".mid"};
    check(broken.getFile().replaceWithText("not a MIDI file"), "invalid fixture written");
    juce::String error;
    check(!player.loadFile(broken.getFile(), error) && error.isNotEmpty(), "malformed file produces an actionable import error");
    const auto after = player.getStatus();
    check(after.fileName == before.fileName && after.playing == before.playing
        && near(after.positionSeconds, before.positionSeconds) && near(after.durationSeconds, before.durationSeconds),
        "failed import preserves the currently loaded song and transport");
    juce::MidiMessageSequence empty;
    end(empty, 480);
    MidiFixture emptyFile{{empty}, 0};
    check(!player.loadFile(emptyFile.file(), error) && error.isNotEmpty(), "MIDI without playable events is rejected");
    MidiFixture asynchronous{{notes}, 2};
    check(!player.loadFile(asynchronous.file(), error) && error.isNotEmpty(), "type 2 independent patterns are rejected explicitly");
    juce::TemporaryFile truncated{".mid"};
    juce::MemoryBlock data;
    valid.file().loadFileAsData(data);
    check(truncated.getFile().replaceWithData(data.getData(), data.getSize() - 3), "truncated fixture written");
    check(!player.loadFile(truncated.getFile(), error) && error.isNotEmpty(), "truncated track is rejected instead of silently imported");
}
void runningStatusTests() {
    // Construct this track directly: a convenience MIDI writer may sort events
    // at equal ticks, concealing ordering regressions in the import decoder.
    const juce::uint8 track[]{0x60, 0x9f, 60, 100, 0, 61, 90, 0, 0xcf, 40, 0, 0x8f, 60, 0,
        0x60, 0x9f, 62, 100, 0x82, 0x20, 0xff, 0x2f, 0};
    juce::MemoryOutputStream bytes;
    bytes.write("MThd", 4);
    bytes.writeIntBigEndian(6);
    bytes.writeShortBigEndian(0);
    bytes.writeShortBigEndian(1);
    bytes.writeShortBigEndian(480);
    bytes.write("MTrk", 4);
    bytes.writeIntBigEndian(static_cast<int>(sizeof(track)));
    bytes.write(track, sizeof(track));
    juce::TemporaryFile fixture{".mid"};
    check(fixture.getFile().replaceWithData(bytes.getData(), bytes.getDataSize()), "raw running-status fixture written");
    JuicySFAudioProcessor processor;
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, fixture.getFile())) return;
    player.play();
    juce::MidiBuffer midi;
    player.appendNextBlock(midi, 300, 1000.0);
    std::vector<int> events;
    for (const auto event : midi) {
        const auto message = event.getMessage();
        if (message.isNoteOnOrOff())
            events.push_back(event.samplePosition * 1000 + (message.isNoteOn() ? 100 : 0)
                + message.getNoteNumber());
        else if (message.isProgramChange())
            events.push_back(event.samplePosition * 1000 + 300 + message.getProgramChangeNumber());
    }
    check(events == std::vector<int>({100160, 100161, 100340, 100060, 200162}),
        "running status preserves channel sixteen and original order at equal timestamps");
}
void controllerDispatchTests() {
    juce::MidiMessageSequence track;
    for (int controller = 0; controller < 128; ++controller)
        add(track, juce::MidiMessage::controllerEvent(16, controller, 37), 96);
    add(track, juce::MidiMessage::pitchWheel(16, 16383), 96);
    add(track, juce::MidiMessage::channelPressureChange(16, 83), 96);
    add(track, juce::MidiMessage::aftertouchChange(16, 64, 79), 96);
    const juce::uint8 reset[]{0x7e, 0x7f, 9, 1};
    add(track, juce::MidiMessage::createSysExMessage(reset, 4), 192);
    add(track, juce::MidiMessage::noteOn(16, 60, static_cast<juce::uint8>(100)), 400);
    end(track, 480);
    MidiFixture fixture{{track}, 0};
    JuicySFAudioProcessor processor;
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, fixture.file())) return;
    player.play();
    juce::MidiBuffer midi;
    player.appendNextBlock(midi, 300, 1000.0);
    bool controllers{true}, bend{false}, pressure{false}, keyPressure{false}, sysex{false};
    for (int controller = 0; controller < 128; ++controller)
        controllers = matchingEvents(midi, 16, controller, 37) == 1 && controllers;
    for (const auto event : midi) {
        const auto message = event.getMessage();
        if (message.isPitchWheel()) bend = message.getChannel() == 16
            && message.getPitchWheelValue() == 16383 && event.samplePosition == 100;
        if (message.isChannelPressure()) pressure = message.getChannel() == 16
            && message.getChannelPressureValue() == 83 && event.samplePosition == 100;
        if (message.isAftertouch()) keyPressure = message.getChannel() == 16
            && message.getNoteNumber() == 64 && message.getAfterTouchValue() == 79
            && event.samplePosition == 100;
        if (message.isSysEx()) sysex = message.getSysExDataSize() == 4
            && std::memcmp(message.getSysExData(), reset, 4) == 0 && event.samplePosition == 200;
    }
    check(controllers && bend && pressure && keyPressure && sysex,
        "file dispatch preserves all CCs, full 14-bit bend, both pressures, SysEx and channel sixteen");
}
void channelTests(const juce::File& bank) {
    juce::MidiMessageSequence track;
    for (int channel = 1; channel <= 16; ++channel) {
        const int selectedBank = channel == 10 ? 0 : channel % 2;
        const int program = channel % 2;
        add(track, juce::MidiMessage::controllerEvent(channel, 0, selectedBank), 12);
        add(track, juce::MidiMessage::controllerEvent(channel, 32, 0), 12);
        add(track, juce::MidiMessage::programChange(channel, program), 12);
        add(track, juce::MidiMessage::controllerEvent(channel, 11, 70 + channel), 12);
        add(track, juce::MidiMessage::noteOn(channel, 60, static_cast<juce::uint8>(100)), 12);
    }
    end(track, 960);
    MidiFixture midi{{track}, 0};
    auto instance = makeProcessor();
    auto& processor = *instance;
    loadBank(processor, bank);
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, midi.file())) return;
    check(player.getStatus().channelCount == 16, "import reports all sixteen MIDI channels");
    player.play();
    juce::AudioBuffer<float> audio{2, 1024};
    render(processor, audio);
    bool all{true};
    for (int channel = 1; channel <= 16; ++channel) {
        int actualBank{-1}, program{-1}, sample{-1}, expression{-1};
        const int expectedBank = channel == 10 ? 128 : channel % 2;
        all = processor.getFluidSynthModel().getLastDispatchedNoteOnProgram(channel - 1,
                actualBank, program, sample) && actualBank == expectedBank
            && program == channel % 2 && sample == 600
            && processor.getFluidSynthModel().getControllerValue(channel - 1, 11, expression)
            && expression == 70 + channel && all;
    }
    check(all && energy(audio) > 0.0, "standalone playback automatically assigns banks and programs on all sixteen channels at sample 600");
}
void pendingBankAndPrepareTests(const juce::File& bank) {
    juce::MidiMessageSequence track;
    add(track, juce::MidiMessage::controllerEvent(1, 0, 1), 0);
    add(track, juce::MidiMessage::programChange(1, 1), 0);
    add(track, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 48);
    add(track, juce::MidiMessage::controllerEvent(1, 0, 0), 96);
    add(track, juce::MidiMessage::programChange(1, 1), 480);
    add(track, juce::MidiMessage::noteOn(1, 61, static_cast<juce::uint8>(100)), 490);
    end(track, 1920);
    MidiFixture file{{track}, 0};
    auto instance = makeProcessor();
    auto& processor = *instance;
    loadBank(processor, bank);
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, file.file())) return;
    player.seek(0.25);
    player.play();
    juce::AudioBuffer<float> audio{2, 1024};
    render(processor, audio);
    int soundingBank{-1}, program{-1}, sample{-1};
    check(processor.getFluidSynthModel().getLastDispatchedNoteOnProgram(0, soundingBank, program, sample)
        && soundingBank == 1 && program == 1 && energy(audio) > 0.0,
        "seek after a pending Bank Select keeps the instrument selected by the previous Program Change");
    for (int i = 0; i < 12; ++i) render(processor, audio);
    check(processor.getFluidSynthModel().getLastDispatchedNoteOnProgram(0, soundingBank, program, sample)
        && soundingBank == 0 && program == 1,
        "the later Program Change applies the bank pending at the seek position");
    const double position = player.getStatus().positionSeconds;
    processor.prepareToPlay(96000.0, 512);
    check(!player.getStatus().playing && near(player.getStatus().positionSeconds, position),
        "audio reconfiguration pauses playback and preserves song position");
    juce::AudioBuffer<float> rebuilt{2, 512};
    render(processor, rebuilt);
    check(energy(rebuilt) == 0.0 && near(player.getStatus().positionSeconds, position),
        "a reconfigured device stays silent until transport resumes");
    player.play();
    render(processor, rebuilt);
    render(processor, rebuilt);
    check(energy(rebuilt) > 0.0
        && near(player.getStatus().positionSeconds, position + 1024.0 / 96000.0),
        "resume after device reconfiguration restores held notes and advances at the new sample rate");
}
void seekAndCleanupTests(const juce::File& bank) {
    juce::MidiMessageSequence track;
    add(track, juce::MidiMessage::programChange(16, 1), 0);
    add(track, juce::MidiMessage::controllerEvent(16, 11, 43), 0);
    add(track, juce::MidiMessage::controllerEvent(16, 101, 0), 0);
    add(track, juce::MidiMessage::controllerEvent(16, 100, 0), 0);
    add(track, juce::MidiMessage::controllerEvent(16, 6, 12), 0);
    add(track, juce::MidiMessage::controllerEvent(16, 38, 25), 0);
    add(track, juce::MidiMessage::pitchWheel(16, 12001), 0);
    add(track, juce::MidiMessage::channelPressureChange(16, 31), 0);
    add(track, juce::MidiMessage::controllerEvent(16, 64, 127), 24);
    add(track, juce::MidiMessage::noteOn(16, 60, static_cast<juce::uint8>(100)), 48);
    add(track, juce::MidiMessage::noteOff(16, 60), 192);
    add(track, juce::MidiMessage::controllerEvent(16, 64, 0), 768);
    add(track, juce::MidiMessage::noteOn(16, 70, static_cast<juce::uint8>(100)), 912);
    end(track, 960);
    MidiFixture midi{{track}, 0};
    auto instance = makeProcessor();
    auto& processor = *instance;
    loadBank(processor, bank);
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, midi.file())) return;
    juce::AudioBuffer<float> audio{2, 1024};
    player.seek(0.1);
    player.play();
    render(processor, audio);
    render(processor, audio);
    auto& model = processor.getFluidSynthModel();
    const auto diagnostics = model.getChannelDiagnostics(15);
    int bankNumber{-1}, program{-1}, expression{-1}, bend{-1}, sensitivity{-1}, pressure{-1}, sample{-1};
    check(energy(audio) > 0.0 && model.getChannelProgram(15, bankNumber, program) && program == 1
        && model.getControllerValue(15, 11, expression) && expression == 43
        && model.getPitchBend(15, bend) && bend == 12001
        && model.getPitchWheelSensitivity(15, sensitivity) && sensitivity == 12
        && diagnostics.bendRange == ((12 << 7) | 25)
        && model.getLastDispatchedChannelPressure(15, pressure, sample) && pressure == 31,
        "seek reconstructs held notes, channel program, expression, full pitch bend, RPN cents, and pressure");
    player.pause();
    for (int i = 0; i < 8; ++i) render(processor, audio);
    int pedal{-1};
    check(energy(audio) < 1.0e-10 && model.getControllerValue(15, 64, pedal) && pedal == 0,
        "pause kills sounding notes and drops sustain");
    const double paused = player.getStatus().positionSeconds;
    player.play();
    render(processor, audio);
    render(processor, audio);
    check(energy(audio) > 0.0 && player.getStatus().positionSeconds > paused,
        "resume restores the held note instead of waiting for a later note-on");
    player.seek(0.3);
    render(processor, audio);
    render(processor, audio);
    check(energy(audio) > 0.0 && model.getControllerValue(15, 64, pedal) && pedal == 127,
        "seeking past note-off restores a note still held by sustain");
    player.stop();
    for (int i = 0; i < 16; ++i) render(processor, audio);
    check(energy(audio) < 1.0e-10 && model.getControllerValue(15, 64, pedal) && pedal == 0,
        "stop releases pedal-held voices without a stuck tail");
    player.seek(0.9);
    player.play();
    render(processor, audio);
    render(processor, audio);
    check(energy(audio) < 1.0e-10, "seeking past pedal release does not resurrect completed notes");
    player.seek(1.0);
    render(processor, audio);
    render(processor, audio);
    check(energy(audio) < 1.0e-10, "seek to the song end keeps an unterminated final note silent");
}
void pedalChaseTests() {
    juce::MidiMessageSequence track;
    add(track, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    add(track, juce::MidiMessage::controllerEvent(1, 66, 127), 96);
    add(track, juce::MidiMessage::noteOff(1, 60), 192);
    add(track, juce::MidiMessage::noteOn(1, 61, static_cast<juce::uint8>(100)), 240);
    add(track, juce::MidiMessage::controllerEvent(1, 66, 127), 288);
    add(track, juce::MidiMessage::noteOff(1, 61), 384);
    add(track, juce::MidiMessage::controllerEvent(1, 66, 0), 576);
    end(track, 960);
    MidiFixture midi{{track}, 0};
    JuicySFAudioProcessor owner;
    juce::MidiBuffer setup;
    MidiFilePlayer player{owner, [&](juce::MidiBuffer& events) { setup = events; }};
    if (!import(player, midi.file())) return;
    player.seek(0.45);
    player.play();
    std::vector<int> held;
    for (const auto event : setup)
        if (event.getMessage().isNoteOn()) held.push_back(event.getMessage().getNoteNumber());
    check(held == std::vector<int>({60, 61}),
        "seek matches the backend's repeated sostenuto-down capture of later held notes");
    player.seek(0.7);
    check(noteOffsets(setup).empty(), "sostenuto release removes every captured note from seek reconstruction");

    juce::MidiMessageSequence both;
    add(both, juce::MidiMessage::controllerEvent(1, 64, 127), 0);
    add(both, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    add(both, juce::MidiMessage::controllerEvent(1, 66, 127), 96);
    add(both, juce::MidiMessage::noteOff(1, 60), 192);
    add(both, juce::MidiMessage::controllerEvent(1, 66, 0), 288);
    add(both, juce::MidiMessage::controllerEvent(1, 64, 0), 576);
    end(both, 960);
    MidiFixture bothFile{{both}, 0};
    if (!import(player, bothFile.file())) return;
    player.seek(0.4);
    player.play();
    check(noteOffsets(setup).empty() && matchingEvents(setup, 1, 64, 127) >= 1,
        "seek matches the backend's sostenuto release while sustain stays down");
    player.seek(0.7);
    check(noteOffsets(setup).empty(), "releasing both pedals removes the note from seek reconstruction");
}
void resetChaseTests(const juce::File& bank) {
    for (const juce::uint8 mode : {juce::uint8{2}, juce::uint8{3}}) {
        juce::MidiMessageSequence track;
        add(track, juce::MidiMessage::controllerEvent(16, 64, 127), 0);
        add(track, juce::MidiMessage::noteOn(16, 60, static_cast<juce::uint8>(100)), 0);
        const juce::uint8 reset[]{0x7e, 0x7f, 9, mode};
        add(track, juce::MidiMessage::createSysExMessage(reset, 4), 192);
        end(track, 960);
        MidiFixture midi{{track}, 0};
        auto instance = makeProcessor();
        auto& processor = *instance;
        loadBank(processor, bank);
        auto& player = processor.getMidiFilePlayer();
        if (!import(player, midi.file())) continue;
        player.seek(0.4);
        player.play();
        juce::AudioBuffer<float> audio{2, 1024};
        render(processor, audio);
        render(processor, audio);
        check(mode == 2 ? energy(audio) > 0.0 : energy(audio) < 1.0e-10,
            "seek preserves notes for GM Off and clears them for GM2 On, matching live reset handling");
    }
}
void dlsTests() {
    juce::TemporaryFile bank{".dls"};
    const auto bytes = SyntheticDls::buildStereoPair();
    check(bank.getFile().replaceWithData(bytes.getData(), bytes.getSize()), "synthetic DLS fixture written");
    juce::MidiMessageSequence track;
    add(track, juce::MidiMessage::controllerEvent(16, 0, 0), 0);
    add(track, juce::MidiMessage::programChange(16, 0), 0);
    add(track, juce::MidiMessage::noteOn(16, 69, static_cast<juce::uint8>(100)), 0);
    end(track, 480);
    MidiFixture midi{{track}, 0};
    auto instance = makeProcessor();
    auto& processor = *instance;
    loadBank(processor, bank.getFile());
    auto& player = processor.getMidiFilePlayer();
    if (!import(player, midi.file())) return;
    player.play();
    juce::AudioBuffer<float> audio{2, 1024};
    render(processor, audio);
    render(processor, audio);
    check(energy(audio) > 0.0, "standalone imported MIDI reaches the DLS loader on channel sixteen");
}
juce::String selectedBankPath(JuicySFAudioProcessor& processor) {
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    const auto* font = xml != nullptr ? xml->getChildByName("soundFont") : nullptr;
    return font != nullptr ? font->getStringAttribute("path") : juce::String{};
}
void multiFileChooserTests(const juce::File& firstBank) {
    juce::TemporaryFile secondBank{".sf2"}, unsupported{".txt"}, corruptMidi{".mid"}, corruptBank{".sf2"};
    juce::MemoryBlock bankBytes;
    check(firstBank.loadFileAsData(bankBytes)
        && secondBank.getFile().replaceWithData(bankBytes.getData(), bankBytes.getSize()),
        "second selectable SoundFont fixture written");
    check(unsupported.getFile().replaceWithText("unsupported file")
        && corruptMidi.getFile().replaceWithText("broken MIDI")
        && corruptBank.getFile().replaceWithText("broken bank"), "invalid chooser fixtures written");
    juce::MidiMessageSequence notes;
    add(notes, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    end(notes, 19200);
    MidiFixture firstMidi{{notes}, 0}, secondMidi{{notes}, 0};
    auto instance = makeProcessor();
    auto& processor = *instance;
    loadBank(processor, firstBank);
    std::unique_ptr<juce::AudioProcessorEditor> editor{processor.createEditor()};
    auto* picker = dynamic_cast<FilePicker*>(namedChild(*editor, "Sound bank file picker"));
    check(picker != nullptr, "standalone bank chooser exposes its exact multi-file handler");
    if (picker == nullptr) return;
    auto& player = processor.getMidiFilePlayer();
    juce::String error;
    check(picker->loadSelectedFiles({firstBank, firstMidi.file()}, error) && error.isEmpty()
        && selectedBankPath(processor) == firstBank.getFullPathName()
        && player.getStatus().fileName == firstMidi.file().getFileName(),
        "standalone chooser accepts bank then MIDI together");
    check(picker->loadSelectedFiles({secondMidi.file(), secondBank.getFile()}, error) && error.isEmpty()
        && selectedBankPath(processor) == secondBank.getFile().getFullPathName()
        && player.getStatus().fileName == secondMidi.file().getFileName(),
        "standalone chooser accepts MIDI then bank together");
    check(picker->loadSelectedFiles({firstMidi.file()}, error) && error.isEmpty()
        && selectedBankPath(processor) == secondBank.getFile().getFullPathName()
        && player.getStatus().fileName == firstMidi.file().getFileName(),
        "MIDI-only selection retains the loaded bank");
    check(picker->loadSelectedFiles({firstBank}, error) && error.isEmpty()
        && selectedBankPath(processor) == firstBank.getFullPathName()
        && player.getStatus().fileName == firstMidi.file().getFileName(),
        "bank-only selection retains the imported MIDI");
    player.seek(0.25);
    player.play();
    const auto reject = [&](const juce::Array<juce::File>& files, const char* message) {
        const auto before = player.getStatus();
        const auto previousBank = selectedBankPath(processor);
        error.clear();
        const bool accepted = picker->loadSelectedFiles(files, error);
        const auto after = player.getStatus();
        // macOS bookmarks may canonicalize /var to /private/var. Compare file
        // identity so an alias change cannot look like a replaced bank.
        std::error_code bankError;
        const bool sameBank = std::filesystem::equivalent(previousBank.toStdString(),
            selectedBankPath(processor).toStdString(), bankError) && !bankError;
        if (accepted || error.isEmpty() || !sameBank
            || after.fileName != before.fileName || !near(after.positionSeconds, before.positionSeconds)
            || after.playing != before.playing)
            std::printf("REJECTION diagnostic accepted=%d error=%s bank=%s previous=%s midi=%s previousMidi=%s position=%.9f previousPosition=%.9f playing=%d previousPlaying=%d\n",
                accepted, error.toRawUTF8(), selectedBankPath(processor).toRawUTF8(), previousBank.toRawUTF8(),
                after.fileName.toRawUTF8(), before.fileName.toRawUTF8(), after.positionSeconds,
                before.positionSeconds, after.playing, before.playing);
        check(!accepted && error.isNotEmpty() && sameBank
            && after.fileName == before.fileName && near(after.positionSeconds, before.positionSeconds)
            && after.playing == before.playing, message);
    };
    const auto beforeCancel = player.getStatus();
    error = "previous error";
    check(picker->loadSelectedFiles({}, error) && error.isEmpty()
        && selectedBankPath(processor) == firstBank.getFullPathName()
        && player.getStatus().fileName == beforeCancel.fileName
        && near(player.getStatus().positionSeconds, beforeCancel.positionSeconds)
        && player.getStatus().playing == beforeCancel.playing,
        "cancelled chooser selection is a successful no-op and clears stale error text");
    reject({firstBank, secondBank.getFile()}, "duplicate banks are rejected before changing either selection");
    reject({firstMidi.file(), secondMidi.file()}, "duplicate MIDI files are rejected before changing either selection");
    reject({secondBank.getFile(), unsupported.getFile()}, "unsupported file types preserve the bank, MIDI, and transport");
    const auto missing = unsupported.getFile().getParentDirectory()
        .getNonexistentChildFile("juicy16-missing-chooser-input", ".mid", false);
    reject({missing}, "nonexistent chooser input preserves the current selection");
    reject({secondBank.getFile(), corruptMidi.getFile()}, "corrupt MIDI selected with a bank is rejected before bank replacement");
    reject({corruptBank.getFile(), secondMidi.file()}, "failed bank loading preserves the prior bank and MIDI selection");

    auto pathOnlyInstance = makeProcessor();
    auto& pathOnly = *pathOnlyInstance;
    loadBank(pathOnly, firstBank);
    import(pathOnly.getMidiFilePlayer(), firstMidi.file());
    pathOnly.getMidiFilePlayer().seek(0.25);
    pathOnly.getMidiFilePlayer().play();
    const auto pathOnlyBefore = pathOnly.getMidiFilePlayer().getStatus();
    std::unique_ptr<juce::AudioProcessorEditor> pathOnlyEditor{pathOnly.createEditor()};
    auto* pathOnlyPicker = dynamic_cast<FilePicker*>(namedChild(*pathOnlyEditor, "Sound bank file picker"));
    check(pathOnlyPicker != nullptr && !pathOnlyPicker->loadSelectedFiles({corruptBank.getFile(), secondMidi.file()}, error)
        && error.isNotEmpty() && selectedBankPath(pathOnly) == firstBank.getFullPathName()
        && pathOnly.getFluidSynthModel().getLoadedFontPath() == firstBank.getFullPathName()
        && pathOnly.getMidiFilePlayer().getStatus().fileName == pathOnlyBefore.fileName
        && near(pathOnly.getMidiFilePlayer().getStatus().positionSeconds, pathOnlyBefore.positionSeconds)
        && pathOnly.getMidiFilePlayer().getStatus().playing,
        "failed combined bank import with an empty prior bookmark retains the playing bank/MIDI pair");
    check(corruptBank.deleteTemporaryFile(), "failed bank import releases the rejected file while the previous pair stays loaded");
    auto* folder = dynamic_cast<juce::Button*>(namedChild(*editor, "Load sound bank and MIDI files"));
    check(folder != nullptr && static_cast<bool>(folder->onClick), "standalone folder button has the combined chooser callback");
    if (auto* theme = dynamic_cast<Juicy16::PluginLookAndFeel*>(&editor->getLookAndFeel())) {
        theme->setAccent(theme->getAccent() == Juicy16::Accent::ice ? Juicy16::Accent::terracotta : Juicy16::Accent::ice);
        editor->sendLookAndFeelChange();
    }
    folder = dynamic_cast<juce::Button*>(namedChild(*editor, "Load sound bank and MIDI files"));
    check(folder != nullptr && static_cast<bool>(folder->onClick)
        && picker->loadSelectedFiles({firstMidi.file()}, error) && error.isEmpty(),
        "accent refresh preserves the combined folder callback and the exact chooser selection handler");

    for (const auto wrapper : {juce::AudioProcessor::wrapperType_AudioUnit,
                               juce::AudioProcessor::wrapperType_VST3}) {
        auto pluginInstance = makeProcessor(wrapper);
        auto& plugin = *pluginInstance;
        loadBank(plugin, firstBank);
        std::unique_ptr<juce::AudioProcessorEditor> pluginEditor{plugin.createEditor()};
        auto* pluginPicker = dynamic_cast<FilePicker*>(namedChild(*pluginEditor, "Sound bank file picker"));
        check(pluginPicker != nullptr, "AU/VST3 bank chooser uses the shared selection handler");
        if (pluginPicker == nullptr) continue;
        check(pluginPicker->loadSelectedFiles({secondBank.getFile()}, error) && error.isEmpty()
            && selectedBankPath(plugin) == secondBank.getFile().getFullPathName(),
            "AU/VST3 chooser accepts a bank-only selection");
        check(!pluginPicker->loadSelectedFiles({firstMidi.file()}, error) && error.isNotEmpty()
            && selectedBankPath(plugin) == secondBank.getFile().getFullPathName()
            && plugin.getMidiFilePlayer().getStatus().fileName.isEmpty(),
            "AU/VST3 chooser rejects standalone MIDI files without changing the bank");
        check(!pluginPicker->loadSelectedFiles({firstBank, firstMidi.file()}, error) && error.isNotEmpty()
            && selectedBankPath(plugin) == secondBank.getFile().getFullPathName(),
            "AU/VST3 chooser rejects mixed bank/MIDI selections before replacing the bank");
    }
    pathOnlyEditor.reset();
    pathOnlyInstance.reset();
    editor.reset();
    instance.reset();
    for (const auto* temporary : {&secondBank, &unsupported, &corruptMidi, &corruptBank}) {
        if (!temporary->deleteTemporaryFile()) {
            std::fprintf(stderr, "Chooser fixture remains open: %s\n", temporary->getFile().getFullPathName().toRawUTF8());
            check(false, "chooser teardown releases its generated fixture files");
        }
    }
}
void loopUiTests(const juce::File& bank) {
    juce::MidiMessageSequence notes;
    add(notes, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    end(notes, 19200);
    MidiFixture file{{notes}, 0};
    auto instance = makeProcessor();
    auto& processor = *instance;
    loadBank(processor, bank);
    import(processor.getMidiFilePlayer(), file.file());
    std::unique_ptr<juce::AudioProcessorEditor> editor{processor.createEditor()};
    editor->setBoundsConstrained({0, 0, GuiConstants::minWidth,
        GuiConstants::minHeight + MidiPlayerComponent::preferredHeight});
    auto* component = dynamic_cast<MidiPlayerComponent*>(namedChild(*editor, "MIDI file player"));
    auto* mode = dynamic_cast<juce::ComboBox*>(namedChild(*editor, "MIDI loop mode"));
    auto* start = dynamic_cast<juce::Slider*>(namedChild(*editor, "Loop section start"));
    auto* endHandle = dynamic_cast<juce::Slider*>(namedChild(*editor, "Loop section end"));
    auto* speed = dynamic_cast<juce::ComboBox*>(namedChild(*editor, "MIDI playback speed"));
    auto* bpm = dynamic_cast<juce::Label*>(namedChild(*editor, "MIDI tempo"));
    check(component != nullptr && mode != nullptr && start != nullptr && endHandle != nullptr
        && speed != nullptr && bpm != nullptr, "standalone exposes named loop handles, mode, speed, and tempo controls");
    if (component == nullptr || mode == nullptr || start == nullptr || endHandle == nullptr
        || speed == nullptr || bpm == nullptr) return;
    check(bpm->getText().contains("120"), "tempo readout displays the loaded file's default BPM");
    speed->setSelectedId(6, juce::sendNotificationSync);
    check(bpm->getText().contains("180") && bpm->getText().contains("120"),
        "tempo readout shows effective and source BPM when playback speed changes");
    mode->setSelectedId(2, juce::sendNotificationSync);
    start->setValue(4.0, juce::sendNotificationSync);
    endHandle->setValue(14.0, juce::sendNotificationSync);
    auto& player = processor.getMidiFilePlayer();
    check(near(player.getStatus().loopStartSeconds, 4.0) && near(player.getStatus().loopEndSeconds, 14.0),
        "accessible loop-handle values commit the section boundaries");
    auto* startField = dynamic_cast<juce::Label*>(namedChild(*editor, "Set loop start"));
    auto* endField = dynamic_cast<juce::Label*>(namedChild(*editor, "Set loop end"));
    check(startField != nullptr && endField != nullptr, "section boundaries expose editable native time fields");
    if (startField != nullptr && endField != nullptr) {
        startField->setText("0:06.5", juce::sendNotificationSync);
        endField->setText("12.75", juce::sendNotificationSync);
        check(near(player.getStatus().loopStartSeconds, 6.5) && near(player.getStatus().loopEndSeconds, 12.75),
            "section time fields accept minutes:seconds with fractions and plain seconds");
        const auto previousEndText = endField->getText();
        endField->setText("not a time", juce::sendNotificationSync);
        check(near(player.getStatus().loopStartSeconds, 6.5) && near(player.getStatus().loopEndSeconds, 12.75)
            && endField->getText() == previousEndText,
            "invalid section time restores its formatted field without changing the previous range");
        startField->setText("13", juce::sendNotificationSync);
        endField->setText("0:01", juce::sendNotificationSync);
        check(player.getStatus().loopEndSeconds - player.getStatus().loopStartSeconds >= 0.05 - 1.0e-7
            && player.getStatus().loopEndSeconds <= 12.75 + 1.0e-7,
            "typed section boundaries clamp crossing input to a valid 50 ms range");
        start->setValue(4.0, juce::sendNotificationSync);
        endHandle->setValue(14.0, juce::sendNotificationSync);
    }
    bool fits = start->isVisible() && endHandle->isVisible();
    for (auto* control : {start, endHandle})
        fits = control->getWidth() > 0 && control->getHeight() > 0
            && component->getLocalBounds().contains(component->getLocalArea(control, control->getLocalBounds())) && fits;
    check(fits, "section handles remain visible and inside the timeline at minimum editor size");
    check(endHandle->keyPressed(juce::KeyPress{juce::KeyPress::rightKey})
        && near(player.getStatus().loopEndSeconds, 14.1), "arrow keys move a loop boundary by one tenth of a second");
    check(endHandle->keyPressed(juce::KeyPress{juce::KeyPress::leftKey, juce::ModifierKeys{juce::ModifierKeys::shiftModifier}, 0})
        && near(player.getStatus().loopEndSeconds, 13.1), "Shift-arrow moves a loop boundary by one second");

    // A native peer satisfies the focus requirement of a real mouse-down; its
    // transparent window avoids interrupting the user with a test preview.
    editor->setAlpha(0.0f);
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    if (startField != nullptr) {
        startField->showEditor();
        if (auto* text = startField->getCurrentTextEditor()) {
            text->setText("0:05.25");
            startField->hideEditor(false);
            check(near(player.getStatus().loopStartSeconds, 5.25),
                "committing the native text editor updates the section boundary");
        } else
            check(false, "editable section field opens its native text editor");
        start->setValue(4.0, juce::sendNotificationSync);
    }
    const auto mouse = [](juce::Slider& control, float x, bool dragged) {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(), {x, 10.0f},
            juce::ModifierKeys{juce::ModifierKeys::leftButtonModifier}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &control, &control, now, {3.0f, 10.0f}, now, 1, dragged};
    };
    start->mouseDown(mouse(*start, 3.0f, false));
    start->mouseDrag(mouse(*start, 43.0f, true));
    check(near(player.getStatus().loopStartSeconds, 4.0), "dragging a loop handle previews without repeatedly rebuilding transport setup");
    start->mouseUp(mouse(*start, 43.0f, true));
    check(player.getStatus().loopStartSeconds > 4.0 && player.getStatus().loopStartSeconds < 13.1,
        "releasing the dragged loop handle commits its previewed boundary");
    editor->setVisible(false);
    editor->removeFromDesktop();
    start->setValue(100.0, juce::sendNotificationSync);
    endHandle->setValue(-1.0, juce::sendNotificationSync);
    check(player.getStatus().loopEndSeconds - player.getStatus().loopStartSeconds >= 0.05 - 1.0e-7,
        "loop handles clamp crossing values to a valid minimum section length");
    mode->setSelectedId(1, juce::sendNotificationSync);
    check(near(player.getStatus().loopStartSeconds, 0.0)
        && near(player.getStatus().loopEndSeconds, player.getStatus().durationSeconds)
        && !start->isVisible() && !endHandle->isVisible()
        && (startField == nullptr || !startField->isVisible()) && (endField == nullptr || !endField->isVisible()),
        "Whole song mode resets the range and hides section handles and editable time fields");
}
void isolationAndUiTests(const juce::File& bank, const char* pngPath, const char* sectionPngPath) {
    juce::MidiMessageSequence track;
    // A musical overview makes visual QA exercise quiet sections and dense
    // passages, while isolation still checks that no imported notes reach hosts.
    for (int channel = 1; channel <= 16; ++channel)
        for (int beat = 0; beat < 64; ++beat) {
            if (beat >= 24 && beat < 32) continue;
            if ((beat + channel) % (channel % 4 + 1) != 0) continue;
            const int note = 48 + (beat + channel) % 24;
            add(track, juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(100)), beat * 480);
            add(track, juce::MidiMessage::noteOff(channel, note), beat * 480 + 360);
        }
    end(track, 64 * 480);
    MidiFixture midi{{track}, 0};
    for (const auto wrapper : {juce::AudioProcessor::wrapperType_AudioUnit,
                               juce::AudioProcessor::wrapperType_VST3}) {
        auto instance = makeProcessor(wrapper);
        auto& processor = *instance;
        loadBank(processor, bank);
        auto& player = processor.getMidiFilePlayer();
        if (!import(player, midi.file())) continue;
        player.play();
        juce::AudioBuffer<float> audio{2, 1024};
        render(processor, audio);
        check(energy(audio) == 0.0 && near(player.getStatus().positionSeconds, 0.0),
            "AU/VST3 processing neither injects standalone file MIDI nor advances its transport");
        std::unique_ptr<juce::AudioProcessorEditor> editor{processor.createEditor()};
        check(namedChild(*editor, "MIDI file player") == nullptr, "AU/VST3 editor omits standalone transport controls");
        juce::MidiBuffer hostMidi;
        hostMidi.addEvent(juce::MidiMessage::noteOn(16, 60, static_cast<juce::uint8>(100)), 17);
        processor.processBlock(audio, hostMidi);
        check(energy(audio) > 0.0, "host MIDI still renders when standalone file playback is excluded");
    }
    auto instance = makeProcessor();
    auto& processor = *instance;
    loadBank(processor, bank);
    import(processor.getMidiFilePlayer(), midi.file());
    std::unique_ptr<juce::AudioProcessorEditor> editor{processor.createEditor()};
    auto* player = namedChild(*editor, "MIDI file player");
    check(player != nullptr, "standalone editor exposes the MIDI player");
    if (player == nullptr) return;
    const char* names[]{"Load MIDI file", "Play or pause MIDI file", "Stop MIDI file",
        "MIDI playback position", "Loop MIDI file", "MIDI playback speed",
        "Set loop start", "Set loop end", "Reset MIDI loop range", "MIDI file name",
        "MIDI file details", "MIDI player guidance", "MIDI playback time", "MIDI loop range",
        "MIDI loop mode", "MIDI tempo"};
    bool fits{true};
    for (int width : {GuiConstants::minWidth, GuiConstants::minWidth + 180}) {
        editor->setBoundsConstrained({0, 0, width,
            GuiConstants::minHeight + MidiPlayerComponent::preferredHeight});
        fits = editor->getLocalBounds().contains(editor->getLocalArea(player, player->getLocalBounds())) && fits;
        for (const auto* name : names) {
            auto* control = namedChild(*player, name);
            fits = control != nullptr && control->getWidth() > 0 && control->getHeight() > 0
                && player->getLocalBounds().contains(player->getLocalArea(control, control->getLocalBounds())) && fits;
        }
    }
    check(fits, "standalone transport controls and readouts fit at minimum and wider editor sizes");
    editor->setBoundsConstrained({0, 0, GuiConstants::minWidth,
        GuiConstants::minHeight + MidiPlayerComponent::preferredHeight});
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    check(snapshot.isValid(), "standalone player paints at minimum editor size");
    if (pngPath != nullptr) {
        juce::FileOutputStream output{juce::File{pngPath}};
        juce::PNGImageFormat png;
        check(output.openedOk() && png.writeImageToStream(snapshot, output), "standalone QA snapshot written");
    }
    if (sectionPngPath != nullptr) {
        auto& transport = processor.getMidiFilePlayer();
        transport.setLoopRange(6.0, 18.0);
        transport.setLooping(true);
        transport.seek(10.0);
        transport.setSpeed(1.25);
        if (auto* component = dynamic_cast<MidiPlayerComponent*>(player))
            component->showLoadResult({});
        juce::FileOutputStream output{juce::File{sectionPngPath}};
        juce::PNGImageFormat png;
        check(output.openedOk()
            && png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), output),
            "standalone Section-mode QA snapshot written");
    }
    const auto savedSize = editor->getLocalBounds();
    editor.reset();
    editor.reset(processor.createEditor());
    check(editor->getLocalBounds() == savedSize,
        "reopening the standalone editor does not add transport height a second time");
}
void keyboardFocusTests(const juce::File& bank) {
    auto instance = makeProcessor();
    loadBank(*instance, bank);
    juce::MidiMessageSequence notes;
    add(notes, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    end(notes, 19200);
    MidiFixture midi{{notes}, 0};
    import(instance->getMidiFilePlayer(), midi.file());
    std::unique_ptr<juce::AudioProcessorEditor> editor{instance->createEditor()};
    auto* mode = dynamic_cast<juce::ComboBox*>(namedChild(*editor, "MIDI loop mode"));
    if (mode != nullptr) mode->setSelectedId(2, juce::sendNotificationSync);
    editor->setAlpha(0.0f);
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    for (const auto* name : {"Load sound bank and MIDI files", "Set loop start", "Set loop end"}) {
        auto* control = namedChild(*editor, name);
        check(control != nullptr && control->getWantsKeyboardFocus(), "custom controls are reachable by keyboard");
        if (control == nullptr) continue;
        control->grabKeyboardFocus();
        check(control->hasKeyboardFocus(false), "custom control receives keyboard focus");
        Juicy16::setFocusRingsVisible(false);
        const auto mouse = control->createComponentSnapshot(control->getLocalBounds());
        Juicy16::setFocusRingsVisible(true);
        const auto keyboard = control->createComponentSnapshot(control->getLocalBounds());
        bool different{false};
        for (int y = 0; y < mouse.getHeight(); ++y)
            for (int x = 0; x < mouse.getWidth(); ++x)
                different = different || mouse.getPixelAt(x, y) != keyboard.getPixelAt(x, y);
        check(different, "focused folder and loop time fields show a visible keyboard focus indicator");
        editor->createComponentSnapshot(editor->getLocalBounds());
        check(control->hasKeyboardFocus(false), "first editor painting preserves the focused child control");
    }
    auto* speed = namedChild(*editor, "MIDI playback speed");
    check(speed != nullptr, "playback speed is available for keyboard interaction");
    if (speed != nullptr) {
        speed->grabKeyboardFocus();
        Juicy16::setFocusRingsVisible(false);
        editor->getPeer()->handleKeyPress(juce::KeyPress{juce::KeyPress::rightKey});
        check(Juicy16::focusRingsVisible(), "arrow-key interaction restores focus rings after mouse use");
    }
    editor->setVisible(false);
    editor->removeFromDesktop();
    Juicy16::setFocusRingsVisible(false);
}
void uiAuditSnapshots(const juce::File& bank, const juce::File& directory) {
    check(directory.createDirectory().wasOk(), "UI audit output directory created");
    auto instance = makeProcessor();
    auto& processor = *instance;
    processor.setRateAndBufferSizeDetails(rate, 1024);
    processor.prepareToPlay(rate, 1024);
    std::unique_ptr<juce::AudioProcessorEditor> editor{processor.createEditor()};
    auto* component = dynamic_cast<MidiPlayerComponent*>(namedChild(*editor, "MIDI file player"));
    if (component == nullptr) return;
    const auto capture = [&](const juce::String& state, bool wide = false) {
        processor.getFluidSynthModel().handleUpdateNowIfNeeded();
        const auto flush = [](auto&& self, juce::Component& child) -> void {
            if (auto* updater = dynamic_cast<juce::AsyncUpdater*>(&child)) updater->handleUpdateNowIfNeeded();
            for (auto* nested : child.getChildren()) self(self, *nested);
        };
        flush(flush, *editor);
        juce::Thread::sleep(60);
        juce::Timer::callPendingTimersSynchronously();
        editor->setBoundsConstrained({0, 0, GuiConstants::minWidth + (wide ? 180 : 0),
            GuiConstants::minHeight + MidiPlayerComponent::preferredHeight});
        for (int scale : {1, 2}) {
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, static_cast<float>(scale));
            juce::MemoryOutputStream out;
            juce::PNGImageFormat png;
            check(image.isValid() && image.getWidth() == editor->getWidth() * scale
                && png.writeImageToStream(image, out)
                && directory.getChildFile(state + "-" + juce::String(scale) + "x.png")
                    .replaceWithData(out.getData(), out.getDataSize()), "UI state renders at 1x and 2x");
        }
    };
    capture("empty");
    juce::MidiMessageSequence notes;
    for (int channel = 1; channel <= 16; ++channel) {
        add(notes, juce::MidiMessage::programChange(channel, channel % 2), 0);
        add(notes, juce::MidiMessage::noteOn(channel, 60, static_cast<juce::uint8>(100)), 0);
        add(notes, juce::MidiMessage::noteOff(channel, 60), 960);
    }
    end(notes, 19200);
    MidiFixture midi{{notes}, 0};
    import(processor.getMidiFilePlayer(), midi.file());
    component->showLoadResult({});
    auto* play = namedChild(*editor, "Play or pause MIDI file");
    check(play != nullptr && !play->isEnabled(), "MIDI-only UI explains the missing bank and disables playback");
    capture("midi-only");
    loadBank(processor, bank);
    component->showLoadResult({});
    check(play != nullptr && play->isEnabled(), "ready UI enables playback after the bank loads");
    capture("ready");
    capture("ready-wide", true);
    auto& transport = processor.getMidiFilePlayer();
    transport.play();
    juce::AudioBuffer<float> audio{2, 1024};
    render(processor, audio);
    component->showLoadResult({});
    capture("playing");
    transport.pause();
    component->showLoadResult({});
    capture("paused");
    transport.stop();
    component->showLoadResult({});
    capture("stopped");
    transport.seek(transport.getStatus().durationSeconds);
    component->showLoadResult({});
    capture("song-end");
    transport.setLoopRange(4.0, 14.0);
    transport.setLooping(true);
    transport.seek(6.0);
    transport.setSpeed(1.25);
    component->showLoadResult({});
    capture("section");
    capture("section-wide", true);
    component->showLoadResult("Choose one MIDI file at a time, optionally with one sound bank.");
    capture("load-error");
    component->showLoadResult({});
    for (const auto* effect : {"Show reverb controls", "Show chorus controls"}) {
        if (auto* tab = dynamic_cast<juce::Button*>(namedChild(*editor, effect)); tab != nullptr && tab->onClick)
            tab->onClick();
        const bool chorus = juce::String{effect}.contains("chorus");
        auto* toggle = dynamic_cast<juce::Button*>(namedChild(*editor, chorus ? "Chorus enabled" : "Reverb enabled"));
        if (toggle != nullptr) toggle->setToggleState(false, juce::sendNotificationSync);
        capture(chorus ? "chorus-off" : "reverb-off");
        if (toggle != nullptr) toggle->setToggleState(true, juce::sendNotificationSync);
        capture(chorus ? "chorus-on" : "reverb-on");
    }
    if (auto* settings = dynamic_cast<juce::Button*>(namedChild(*editor, "Settings")); settings != nullptr && settings->onClick)
        settings->onClick();
    capture("settings");
    auto* accentBox = dynamic_cast<juce::ComboBox*>(namedChild(*editor, "Accent colour"));
    check(accentBox != nullptr, "settings accent selector is available");
    if (accentBox != nullptr) {
        int choice{1};
        for (const auto accent : Juicy16::allAccents()) {
            accentBox->setSelectedId(choice++, juce::sendNotificationSync);
            capture("settings-" + Juicy16::accentName(accent));
        }
    }
}
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc == 3 && juce::String{argv[1]} == "--write-fixtures")
        return SyntheticFixtures::write(juce::File{argv[2]}) ? 0 : 1;
    AssertionLogger logger;
    juce::Logger::setCurrentLogger(&logger);
    juce::TemporaryFile bank{".sf2"};
    const auto bytes = SyntheticSf2::build({{0, 0, 220.5, "Bank 0 / Program 0"},
        {0, 1, 294.0, "Bank 0 / Program 1"}, {1, 0, 441.0, "Bank 1 / Program 0"},
        {1, 1, 588.0, "Bank 1 / Program 1"}, {128, 0, 882.0, "Drum program 0"},
        {128, 1, 735.0, "Drum program 1"}});
    check(bank.getFile().replaceWithData(bytes.getData(), bytes.getSize()), "multi-bank SF2 fixture written");
    timingTests();
    transportTests();
    fractionalBoundaryTests();
    importGuardTests();
    importFailureTests();
    runningStatusTests();
    controllerDispatchTests();
    channelTests(bank.getFile());
    pendingBankAndPrepareTests(bank.getFile());
    seekAndCleanupTests(bank.getFile());
    pedalChaseTests();
    resetChaseTests(bank.getFile());
    dlsTests();
    loopUiTests(bank.getFile());
    multiFileChooserTests(bank.getFile());
    keyboardFocusTests(bank.getFile());
    const bool audit = argc == 3 && juce::String{argv[1]} == "--ui-audit";
    isolationAndUiTests(bank.getFile(), !audit && argc > 1 ? argv[1] : nullptr, !audit && argc > 2 ? argv[2] : nullptr);
    if (audit) uiAuditSnapshots(bank.getFile(), juce::File{argv[2]});
    check(logger.assertions.load() == 0, "MIDI playback, transport, editor construction, and painting produce zero JUCE assertions");
    juce::Logger::setCurrentLogger(nullptr);
    return failures == 0 ? 0 : 1;
}
