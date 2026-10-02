// Offline performance and resource baseline for Juicy16.
//
//   JuicySFPerfProbe <bank.dls|sf2|sf3>
//
// Measures bank load time, render throughput, and resident memory across
// repeated bank loads, processor lifecycles, editor lifecycles, and concurrent
// instances. Growth thresholds are deliberately generous: this exists to catch
// an unbounded leak, not to police allocator noise. Absolute numbers are
// reported so a run can be compared against a recorded baseline, and are
// machine-specific — see docs/PERFORMANCE.md.

#if defined(_WIN32)
 #define NOMINMAX
 #include <windows.h>
 #include <psapi.h>
#endif
#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <vector>

#if JUCE_MAC
 #include <mach/mach.h>
#endif

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

// Unoptimized Debug builds measure throughput but cannot establish release
// realtime performance, especially on shared CI runners. Resource and
// lifecycle checks still fail normally in both configurations.
void checkTiming(bool condition, const char* name)
{
#if JUCE_DEBUG
    std::printf("  INFO  %s: %s (Debug timing is diagnostic only)\n",
                name, condition ? "met" : "not met");
#else
    check(condition, name);
#endif
}

// Current resident size. Peak (ru_maxrss) only ever grows, so it cannot show
// that memory was released and is useless for leak detection.
double residentMegabytes()
{
#if JUCE_MAC
    mach_task_basic_info info{};
    mach_msg_type_number_t count{MACH_TASK_BASIC_INFO_COUNT};
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
#elif JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS info{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
        return 0.0;
    return static_cast<double>(info.WorkingSetSize) / (1024.0 * 1024.0);
#else
    return 0.0;
#endif
}

juce::MemoryBlock stateFor(const juce::String& bankPath)
{
    juce::XmlElement xml{"MYPLUGINSETTINGS"};
    xml.setAttribute("stateVersion", 2);
    auto* ui{xml.createNewChildElement("uiState")};
    ui->setAttribute("width", 850);
    ui->setAttribute("height", 650);
    ui->setAttribute("selectedChannel", 1);
    auto* font{xml.createNewChildElement("soundFont")};
    font->setAttribute("path", bankPath);
    font->setAttribute("bookmark", "");
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    return state;
}

// Sixteen channels each holding a chord: a heavier load than a typical GM
// arrangement without reaching the configured voice ceiling.
void addSixteenChannelChords(juce::MidiBuffer& midi)
{
    for (int channel = 1; channel <= 16; ++channel)
        for (int note : {48, 55, 60, 64})
            midi.addEvent(
                juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(100)),
                (channel - 1) * 4);
}

// One channel holding one note: the light end of the range, and the figure a
// tester should compare against when a single instance feels expensive.
void addSingleNote(juce::MidiBuffer& midi)
{
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
}

// Drives the engine at its configured 512-voice ceiling. A preset may build a
// note from more than one voice, so the note count is an upper bound on notes,
// not on voices; the probe reports what FluidSynth actually allocated.
constexpr int voiceCeiling{512};

void addVoiceCeilingChords(juce::MidiBuffer& midi, int blockSize, bool sameTimestamp)
{
    constexpr int notesPerChannel{voiceCeiling / 16};
    int index{0};
    for (int channel = 1; channel <= 16; ++channel)
        for (int note = 0; note < notesPerChannel; ++note, ++index)
            midi.addEvent(
                juce::MidiMessage::noteOn(
                    channel, 36 + note * 2, static_cast<juce::uint8>(100)),
                sameTimestamp ? 0 : (index * blockSize) / voiceCeiling);
}

// A block's worth of the automation a busy game rip produces: a Program Change
// and five controllers on every channel, all timestamped within the block.
void addAutomationStorm(juce::MidiBuffer& midi, int blockSize, int programOffset)
{
    for (int channel = 1; channel <= 16; ++channel) {
        const int base{((channel - 1) * blockSize) / 16};
        midi.addEvent(
            juce::MidiMessage::programChange(channel, (programOffset + channel) % 128),
            base);
        for (const int controller : {1, 7, 10, 11, 74}) {
            midi.addEvent(
                juce::MidiMessage::controllerEvent(
                    channel, controller, (programOffset * 7 + controller) % 128),
                juce::jmin(base + controller, blockSize - 1));
        }
        midi.addEvent(
            juce::MidiMessage::pitchWheel(channel, (programOffset * 512) % 16384),
            juce::jmin(base + 6, blockSize - 1));
    }
}

// Optional, reproducible microbenchmark. It deliberately bypasses the resource
// cycling above and never changes CTest's existing no-flag workload. MIDI is
// generated once, then copied outside the timed processBlock call because the
// processor is allowed to mutate the incoming buffer.
struct BenchmarkScenario {
    const char* name;
    int notes; // 0 = silence, 1 = one note, 64 = sixteen chords, 512 = ceiling
    bool reverb;
    bool chorus;
    bool denseMidi;
    bool sameTimestamp;
    bool parameterAutomation;
    bool retrigger{false};
};

constexpr BenchmarkScenario benchmarkScenarios[]{
    {"silence", 0, false, false, false, false, false},
    {"single", 1, false, false, false, false, false},
    {"sixteen", 64, false, false, false, false, false},
    {"full", 512, false, false, false, false, false},
    {"full_reverb", 512, true, false, false, false, false},
    {"full_chorus", 512, false, true, false, false, false},
    {"full_effects", 512, true, true, false, false, false},
    {"dense_spread", 64, false, false, true, false, false},
    {"dense_same", 64, false, false, true, true, false},
    {"retrigger_same", 64, false, false, false, true, false, true},
    {"parameters", 64, true, true, false, false, true},
};

struct BenchmarkOptions {
    double seconds{0.5};
    int repeats{3};
    int rate{0};
    int blockSize{0};
    std::string scenario;
    std::string interpolation{"linear"};
};

bool parseBenchmarkOptions(int argc, char** argv, BenchmarkOptions& options)
{
    for (int index = 3; index < argc; ++index) {
        const std::string option{argv[index]};
        const auto separator{option.find('=')};
        if (separator == std::string::npos)
            return false;
        const auto key{option.substr(0, separator)};
        const auto value{option.substr(separator + 1)};
        if (key == "--scenario") {
            options.scenario = value;
            continue;
        }
        if (key == "--interpolation") {
            if (value != "seventh" && value != "linear" && value != "none")
                return false;
            options.interpolation = value;
            continue;
        }
        char* end{nullptr};
        const double number{std::strtod(value.c_str(), &end)};
        if (end == value.c_str() || *end != '\0' || !std::isfinite(number))
            return false;
        if (key == "--seconds") {
            if (number < 0.1 || number > 30.0)
                return false;
            options.seconds = number;
            continue;
        }
        // Bound the conversion before casting; rate/block/repeat are integer
        // options, so compare accepted values as integers rather than doubles.
        if (number < 1.0 || number > 192000.0 || !juce::exactlyEqual(number, std::floor(number)))
            return false;
        const int integer{static_cast<int>(number)};
        if (key == "--repeat" && integer <= 100)
            options.repeats = integer;
        else if (key == "--rate" && (integer == 44100 || integer == 48000 || integer == 96000 || integer == 192000))
            options.rate = integer;
        else if (key == "--block-size" && integer >= 64 && integer <= 4096)
            options.blockSize = integer;
        else
            return false;
    }
    if (!options.scenario.empty()) {
        bool found{false};
        for (const auto& scenario : benchmarkScenarios)
            found = found || options.scenario == scenario.name;
        if (!found)
            return false;
    }
    return true;
}

using BenchmarkClock = std::chrono::steady_clock;

double elapsedMicroseconds(BenchmarkClock::time_point start)
{
    return std::chrono::duration<double, std::micro>(BenchmarkClock::now() - start).count();
}

int activeVoices(FluidSynthModel& model)
{
    int voices{0};
    for (int channel = 0; channel < 16; ++channel) {
        FluidSynthModel::VoiceStateCounts counts;
        if (model.getVoiceStateCounts(channel, counts))
            voices += counts.playing;
    }
    return voices;
}

juce::AudioProcessorParameterWithID* benchmarkParameter(JuicySFAudioProcessor& processor,
                                                       const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
        if (auto* identified{dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter)};
            identified != nullptr && identified->paramID == id)
            return identified;
    return nullptr;
}

int runBenchmarks(const juce::File& bank, const BenchmarkOptions& options)
{
    std::printf("BENCH_SCHEMA,1\n");
    std::printf("BENCH_META,interpolation,%s\n", options.interpolation.c_str());
    std::printf("BENCH_HEADER,scenario,rate,block,repeat,blocks,events_per_block,voices_start,voices_end,"
                "audio_seconds,render_us,block_median_us,block_p95_us,block_max_us,"
                "midi_generate_us,midi_copy_us,parameter_update_us,audio_energy,peak,finite\n");
    const auto state{stateFor(bank.getFullPathName())};
    int emitted{0};
    for (const auto& scenario : benchmarkScenarios) {
        if (!options.scenario.empty() && options.scenario != scenario.name)
            continue;
        for (const int rate : {44100, 48000, 96000, 192000}) {
            if (options.rate != 0 && options.rate != rate)
                continue;
            for (const int defaultBlock : {64, 512}) {
                const int blockSize{options.blockSize == 0 ? defaultBlock : options.blockSize};
                if (options.blockSize != 0 && defaultBlock != 64)
                    continue;
                constexpr int patternSize{128};
                std::vector<juce::MidiBuffer> patterns(static_cast<size_t>(patternSize));
                const auto generationStart{BenchmarkClock::now()};
                if (scenario.denseMidi || scenario.retrigger)
                    for (int pattern = 0; pattern < patternSize; ++pattern) {
                        juce::MidiBuffer generated;
                        if (scenario.denseMidi)
                            addAutomationStorm(generated, blockSize, pattern);
                        if (scenario.retrigger)
                            for (int channel = 1; channel <= 16; ++channel) {
                                generated.addEvent(juce::MidiMessage::noteOff(channel, 60), 0);
                                generated.addEvent(juce::MidiMessage::noteOn(
                                    channel, 60, static_cast<juce::uint8>(100)), 0);
                            }
                        for (const auto metadata : generated)
                            patterns[static_cast<size_t>(pattern)].addEvent(
                                metadata.getMessage(), scenario.sameTimestamp ? 0 : metadata.samplePosition);
                    }
                const double generationUs{elapsedMicroseconds(generationStart)};
                const int eventsPerBlock{patterns.front().getNumEvents()};
                const int blocks{static_cast<int>(std::ceil(rate * options.seconds / blockSize))};
                for (int repeat = 0; repeat < options.repeats; ++repeat) {
                    JuicySFAudioProcessor processor;
                    processor.prepareToPlay(rate, blockSize);
                    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                    if (processor.getFluidSynthModel().getFontLoadStatus() != "loaded") {
                        std::fprintf(stderr, "Benchmark bank load failed\n");
                        return 1;
                    }
                    auto* reverb{benchmarkParameter(processor, "reverbOn")};
                    auto* chorus{benchmarkParameter(processor, "chorusOn")};
                    auto* interpolation{dynamic_cast<juce::AudioParameterChoice*>(
                        benchmarkParameter(processor, "interpolation"))};
                    if (reverb == nullptr || chorus == nullptr || interpolation == nullptr) {
                        std::fprintf(stderr, "Benchmark effects/interpolation parameters missing\n");
                        return 1;
                    }
                    // stateFor intentionally retains the old resource probe's
                    // schema-2 migration. Override it here so optional timing
                    // measures the shipping Linear default unless requested.
                    *interpolation = options.interpolation == "seventh" ? 0
                        : options.interpolation == "linear" ? 1 : 2;
                    reverb->setValueNotifyingHost(scenario.reverb ? 1.0f : 0.0f);
                    chorus->setValueNotifyingHost(scenario.chorus ? 1.0f : 0.0f);
                    std::vector<juce::AudioProcessorParameterWithID*> automated;
                    if (scenario.parameterAutomation) {
                        for (int channel = 1; channel <= 16; ++channel)
                            for (const char* prefix : {"volCh", "panCh"}) {
                                auto* parameter{benchmarkParameter(processor, juce::String{prefix} + juce::String{channel})};
                                if (parameter == nullptr)
                                    return 1;
                                automated.push_back(parameter);
                            }
                        for (const char* id : {"outputLevel", "reverbSize", "reverbDamp", "reverbWidth",
                                              "reverbLevel", "chorusLevel", "chorusRate", "chorusDepth"}) {
                            auto* parameter{benchmarkParameter(processor, id)};
                            if (parameter == nullptr)
                                return 1;
                            automated.push_back(parameter);
                        }
                    }
                    juce::AudioBuffer<float> audio{2, blockSize};
                    juce::MidiBuffer midi;
                    midi.ensureSize(32768);
                    if (scenario.reverb || scenario.chorus)
                        for (int channel = 1; channel <= 16; ++channel) {
                            midi.addEvent(juce::MidiMessage::controllerEvent(channel, 91, 100), 0);
                            midi.addEvent(juce::MidiMessage::controllerEvent(channel, 93, 100), 0);
                        }
                    if (scenario.notes == 1)
                        addSingleNote(midi);
                    else if (scenario.notes == 64)
                        addSixteenChannelChords(midi);
                    else if (scenario.notes == 512)
                        addVoiceCeilingChords(midi, blockSize, true);
                    audio.clear();
                    processor.processBlock(audio, midi);
                    const int warmupBlocks{static_cast<int>(std::ceil(rate * 0.1 / blockSize))};
                    for (int block = 0; block < warmupBlocks; ++block) {
                        midi.clear();
                        midi.addEvents(patterns[static_cast<size_t>(block % patternSize)], 0, blockSize, 0);
                        audio.clear();
                        processor.processBlock(audio, midi);
                    }
                    const int voicesStart{activeVoices(processor.getFluidSynthModel())};
                    if (scenario.notes == voiceCeiling && voicesStart <= voiceCeiling / 2) {
                        std::fprintf(stderr, "Benchmark did not reach a substantial fraction of the voice ceiling\n");
                        return 1;
                    }
                    std::vector<double> blockTimes;
                    blockTimes.reserve(static_cast<size_t>(blocks));
                    double renderUs{0.0}, copyUs{0.0}, parameterUs{0.0}, energy{0.0}, peak{0.0};
                    bool finite{true};
                    for (int block = 0; block < blocks; ++block) {
                        auto start{BenchmarkClock::now()};
                        midi.clear();
                        midi.addEvents(patterns[static_cast<size_t>(block % patternSize)], 0, blockSize, 0);
                        copyUs += elapsedMicroseconds(start);
                        if (!automated.empty()) {
                            start = BenchmarkClock::now();
                            for (size_t index = 0; index < automated.size(); ++index) {
                                const float value{0.2f + 0.6f * static_cast<float>(
                                    (static_cast<size_t>(block) + index * 7) % 128) / 127.0f};
                                automated[index]->setValueNotifyingHost(value);
                            }
                            parameterUs += elapsedMicroseconds(start);
                        }
                        audio.clear();
                        start = BenchmarkClock::now();
                        processor.processBlock(audio, midi);
                        const double blockUs{elapsedMicroseconds(start)};
                        renderUs += blockUs;
                        blockTimes.push_back(blockUs);
                        // Audio diagnostics are intentionally outside render timing.
                        for (int channel = 0; channel < 2; ++channel)
                            for (int sample = 0; sample < blockSize; ++sample) {
                                const double value{audio.getSample(channel, sample)};
                                finite = finite && std::isfinite(value);
                                energy += value * value;
                                peak = std::max(peak, std::abs(value));
                            }
                    }
                    const int voicesEnd{activeVoices(processor.getFluidSynthModel())};
                    std::sort(blockTimes.begin(), blockTimes.end());
                    const size_t middle{blockTimes.size() / 2};
                    const double median{blockTimes.size() % 2 == 0
                        ? (blockTimes[middle - 1] + blockTimes[middle]) * 0.5 : blockTimes[middle]};
                    const size_t p95{static_cast<size_t>(std::ceil(static_cast<double>(blockTimes.size()) * 0.95)) - 1};
                    std::printf("BENCH,%s,%d,%d,%d,%d,%d,%d,%d,%.9f,%.6f,%.6f,%.6f,%.6f,"
                                "%.6f,%.6f,%.6f,%.12e,%.9f,%d\n",
                                scenario.name, rate, blockSize, repeat, blocks, eventsPerBlock,
                                voicesStart, voicesEnd, static_cast<double>(blocks) * blockSize / rate,
                                renderUs, median, blockTimes[p95], blockTimes.back(), generationUs,
                                copyUs, parameterUs, energy, peak, finite ? 1 : 0);
                    ++emitted;
                    if (!finite || (scenario.notes > 0 && (voicesStart == 0 || energy <= 0.0))
                        || (scenario.notes == 0 && energy > 1.0e-12)) {
                        std::fprintf(stderr, "Benchmark workload failed to produce finite active audio\n");
                        return 1;
                    }
                }
            }
        }
    }
    return emitted > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    // Line-buffered so FluidSynth's unbuffered stderr interleaves correctly with
    // the section headers when a CI job captures both streams.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc < 2 || (argc > 2 && std::string{argv[2]} != "--benchmark")) {
        std::fprintf(stderr, "usage: JuicySFPerfProbe <bank.dls|sf2|sf3> [--benchmark [--seconds=N] [--repeat=N] [--scenario=NAME] [--rate=HZ] [--block-size=N] [--interpolation=linear|seventh|none]]\n");
        return 2;
    }

    const juce::File bank{juce::String{argv[1]}};
    if (!bank.existsAsFile()) {
        std::fprintf(stderr, "bank not found: %s\n", bank.getFullPathName().toRawUTF8());
        return 2;
    }

    if (argc > 2) {
        BenchmarkOptions options;
        if (!parseBenchmarkOptions(argc, argv, options)) {
            std::fprintf(stderr, "Invalid benchmark option\n");
            return 2;
        }
        return runBenchmarks(bank, options);
    }

    constexpr double sampleRate{48000.0};
    const double baselineRss{residentMegabytes()};
    std::printf("== Juicy16 performance probe ==\n");
    std::printf("  bank: %s (%.1f MB)\n",
                bank.getFileName().toRawUTF8(),
                static_cast<double>(bank.getSize()) / (1024.0 * 1024.0));
    std::printf("  resident before first load: %.1f MB\n", baselineRss);

    std::printf("\n-- load time and footprint --\n");
    double loadedRss{0.0};
    {
        JuicySFAudioProcessor processor;
        processor.prepareToPlay(sampleRate, 512);
        const auto state{stateFor(bank.getFullPathName())};
        const double start{juce::Time::getMillisecondCounterHiRes()};
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const double loadMs{juce::Time::getMillisecondCounterHiRes() - start};
        loadedRss = residentMegabytes();
        std::printf("  load time: %.0f ms\n", loadMs);
        std::printf("  resident after load: %.1f MB (+%.1f MB)\n",
                    loadedRss, loadedRss - baselineRss);
        check(processor.getFluidSynthModel().getFontLoadStatus() == "loaded",
              "bank loads for the performance probe");
    }

    std::printf("\n-- render throughput, 16 channels sounding --\n");
    for (const int blockSize : {64, 128, 256, 512, 1024}) {
        JuicySFAudioProcessor processor;
        processor.prepareToPlay(sampleRate, blockSize);
        const auto state{stateFor(bank.getFullPathName())};
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        juce::AudioBuffer<float> audio{2, blockSize};
        juce::MidiBuffer opening;
        addSixteenChannelChords(opening);
        audio.clear();
        processor.processBlock(audio, opening);

        const int blocks{static_cast<int>(sampleRate * 5.0 / blockSize)};
        const double start{juce::Time::getMillisecondCounterHiRes()};
        for (int block = 0; block < blocks; ++block) {
            juce::MidiBuffer empty;
            audio.clear();
            processor.processBlock(audio, empty);
        }
        const double elapsedMs{juce::Time::getMillisecondCounterHiRes() - start};
        const double audioMs{blocks * blockSize * 1000.0 / sampleRate};
        std::printf("  block %4d: %6.0f ms cpu for %6.0f ms audio (%.1f%% of realtime)\n",
                    blockSize, elapsedMs, audioMs, 100.0 * elapsedMs / audioMs);
        checkTiming(elapsedMs < audioMs,
              blockSize == 64
                  ? "renders faster than realtime at the smallest tested block"
                  : "renders faster than realtime");
    }

    std::printf("\n-- render throughput, one channel sounding --\n");
    {
        constexpr int blockSize{64};
        JuicySFAudioProcessor processor;
        processor.prepareToPlay(sampleRate, blockSize);
        const auto state{stateFor(bank.getFullPathName())};
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        juce::AudioBuffer<float> audio{2, blockSize};
        juce::MidiBuffer opening;
        addSingleNote(opening);
        audio.clear();
        processor.processBlock(audio, opening);

        const int blocks{static_cast<int>(sampleRate * 5.0 / blockSize)};
        const double start{juce::Time::getMillisecondCounterHiRes()};
        for (int block = 0; block < blocks; ++block) {
            juce::MidiBuffer empty;
            audio.clear();
            processor.processBlock(audio, empty);
        }
        const double elapsedMs{juce::Time::getMillisecondCounterHiRes() - start};
        const double audioMs{blocks * blockSize * 1000.0 / sampleRate};
        std::printf("  block %4d: %6.0f ms cpu for %6.0f ms audio (%.1f%% of realtime)\n",
                    blockSize, elapsedMs, audioMs, 100.0 * elapsedMs / audioMs);
        checkTiming(elapsedMs < audioMs,
              "one channel renders faster than realtime at the smallest tested block");
    }

    std::printf("\n-- voice ceiling stress --\n");
    for (const bool sameTimestamp : {true, false}) {
        std::printf("  %s\n", sameTimestamp
            ? "all note-ons at one timestamp:" : "note-ons spread across the block:");
        constexpr int blockSize{64};
        JuicySFAudioProcessor processor;
        processor.prepareToPlay(sampleRate, blockSize);
        const auto state{stateFor(bank.getFullPathName())};
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        juce::AudioBuffer<float> audio{2, blockSize};
        juce::MidiBuffer opening;
        addVoiceCeilingChords(opening, blockSize, sameTimestamp);
        audio.clear();
        processor.processBlock(audio, opening);

        int playing{0};
        auto& model{processor.getFluidSynthModel()};
        for (int channel = 0; channel < 16; ++channel) {
            FluidSynthModel::VoiceStateCounts counts;
            if (model.getVoiceStateCounts(channel, counts))
                playing += counts.playing;
        }

        const int blocks{static_cast<int>(sampleRate * 5.0 / blockSize)};
        const double start{juce::Time::getMillisecondCounterHiRes()};
        for (int block = 0; block < blocks; ++block) {
            juce::MidiBuffer empty;
            audio.clear();
            processor.processBlock(audio, empty);
        }
        const double elapsedMs{juce::Time::getMillisecondCounterHiRes() - start};
        const double audioMs{blocks * blockSize * 1000.0 / sampleRate};
        std::printf("  %d note-ons -> %d voices playing\n", voiceCeiling, playing);
        std::printf("  block %4d: %6.0f ms cpu for %6.0f ms audio (%.1f%% of realtime)\n",
                    blockSize, elapsedMs, audioMs, 100.0 * elapsedMs / audioMs);
        // The ceiling must be reachable, or the stress case is not the stress
        // case. Voice stealing below it would be a silent downgrade.
        check(playing > voiceCeiling / 2,
              "dense material reaches a substantial fraction of the 512-voice ceiling");
        checkTiming(elapsedMs < audioMs,
              "the voice ceiling renders faster than realtime at the smallest tested block");
    }

    // Phase 10 exit criterion: the envelope has to hold with the reverb running,
    // not only on the dry path. Reverb ships OFF, so every measurement above is
    // dry; this one enables it and compares the cost of the same work.
    std::printf("\n-- voice ceiling with reverb enabled --\n");
    {
        constexpr int blockSize{64};
        double dryMs{0.0}, wetMs{0.0}, audioMs{0.0};
        for (int pass = 0; pass < 2; ++pass) {
            const bool reverbOn{pass == 1};
            JuicySFAudioProcessor processor;
            processor.prepareToPlay(sampleRate, blockSize);
            const auto state{stateFor(bank.getFullPathName())};
            processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            for (auto* parameter : processor.getParameters())
                if (auto* b{dynamic_cast<juce::AudioParameterBool*>(parameter)};
                    b != nullptr && b->paramID == "reverbOn")
                    *b = reverbOn;

            juce::AudioBuffer<float> audio{2, blockSize};
            juce::MidiBuffer opening;
            // Every channel sending reverb, so the reverb bus is actually fed.
            for (int channel = 1; channel <= 16; ++channel)
                opening.addEvent(juce::MidiMessage::controllerEvent(channel, 91, 100), 0);
            addVoiceCeilingChords(opening, blockSize, true);
            audio.clear();
            processor.processBlock(audio, opening);

            const int blocks{static_cast<int>(sampleRate * 5.0 / blockSize)};
            const double start{juce::Time::getMillisecondCounterHiRes()};
            for (int block = 0; block < blocks; ++block) {
                juce::MidiBuffer empty;
                audio.clear();
                processor.processBlock(audio, empty);
            }
            const double elapsedMs{juce::Time::getMillisecondCounterHiRes() - start};
            audioMs = blocks * blockSize * 1000.0 / sampleRate;
            (reverbOn ? wetMs : dryMs) = elapsedMs;
            std::printf("  reverb %-3s block %4d: %6.0f ms cpu for %6.0f ms audio (%.1f%% of realtime)\n",
                        reverbOn ? "on" : "off", blockSize, elapsedMs, audioMs,
                        100.0 * elapsedMs / audioMs);
        }
        std::printf("  reverb cost at the voice ceiling: %+.1f%% of realtime\n",
                    100.0 * (wetMs - dryMs) / audioMs);
        checkTiming(wetMs < audioMs,
              "the voice ceiling renders faster than realtime with reverb enabled");
        // The reverb is one stereo FDN for the whole synth, not per voice, so its
        // cost must not scale with polyphony. A large multiple here would mean it
        // had been wired per voice by mistake.
        check(wetMs < dryMs * 1.5 + 0.05 * audioMs,
              "enabling reverb does not multiply the cost of dense playback");
    }

    std::printf("\n-- program change and controller automation --\n");
    for (const int blockSize : {64, 512, 1024}) {
        JuicySFAudioProcessor processor;
        processor.prepareToPlay(sampleRate, blockSize);
        const auto state{stateFor(bank.getFullPathName())};
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        juce::AudioBuffer<float> audio{2, blockSize};
        juce::MidiBuffer opening;
        addSixteenChannelChords(opening);
        audio.clear();
        processor.processBlock(audio, opening);

        const int blocks{static_cast<int>(sampleRate * 5.0 / blockSize)};
        const double start{juce::Time::getMillisecondCounterHiRes()};
        for (int block = 0; block < blocks; ++block) {
            juce::MidiBuffer midi;
            addAutomationStorm(midi, blockSize, block);
            audio.clear();
            processor.processBlock(audio, midi);
        }
        const double elapsedMs{juce::Time::getMillisecondCounterHiRes() - start};
        const double audioMs{blocks * blockSize * 1000.0 / sampleRate};
        const int events{blocks * 16 * 7};
        std::printf("  %d events (%d per block: 16 program changes, 80 CCs, 16 bends)\n",
                    events, 16 * 7);
        std::printf("  block %4d: %6.0f ms cpu for %6.0f ms audio (%.1f%% of realtime)\n",
                    blockSize, elapsedMs, audioMs, 100.0 * elapsedMs / audioMs);
        checkTiming(elapsedMs < audioMs,
              "continuous program-change and controller automation renders faster than realtime");
    }

    // Reloading the same path would not notify, the ValueTree property being
    // unchanged, so alternate between the bank and an identical temporary copy.
    std::printf("\n-- repeated bank loads --\n");
    {
        const auto copy{juce::File::createTempFile(bank.getFileExtension())};
        const bool copied{bank.copyFileTo(copy)};
        check(copied, "temporary bank copy created for reload cycling");
        if (copied) {
            JuicySFAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            const auto primary{stateFor(bank.getFullPathName())};
            const auto secondary{stateFor(copy.getFullPathName())};
            processor.setStateInformation(primary.getData(),
                                          static_cast<int>(primary.getSize()));
            const double afterFirst{residentMegabytes()};
            constexpr int cycles{20};
            for (int cycle = 0; cycle < cycles; ++cycle) {
                const auto& next{(cycle % 2 == 0) ? secondary : primary};
                processor.setStateInformation(next.getData(),
                                              static_cast<int>(next.getSize()));
            }
            const double afterCycles{residentMegabytes()};
            const double growth{afterCycles - afterFirst};
            std::printf("  %d reloads: %.1f MB -> %.1f MB (%+.1f MB)\n",
                        cycles, afterFirst, afterCycles, growth);
            // A bank leaked once per reload would grow by cycles x bank size.
            const double bankMegabytes{
                static_cast<double>(bank.getSize()) / (1024.0 * 1024.0)};
            check(growth < std::max(64.0, bankMegabytes * 2.0),
                  "repeated bank loads do not grow memory by a multiple of the bank size");
            check(processor.getFluidSynthModel().getFontLoadStatus() == "loaded",
                  "the bank is still loaded after reload cycling");
            copy.deleteFile();
        }
    }

    std::printf("\n-- processor lifecycles --\n");
    {
        const double before{residentMegabytes()};
        constexpr int cycles{20};
        for (int cycle = 0; cycle < cycles; ++cycle) {
            JuicySFAudioProcessor processor;
            processor.prepareToPlay(sampleRate, 512);
            const auto state{stateFor(bank.getFullPathName())};
            processor.setStateInformation(state.getData(),
                                          static_cast<int>(state.getSize()));
            juce::AudioBuffer<float> audio{2, 512};
            juce::MidiBuffer midi;
            addSixteenChannelChords(midi);
            audio.clear();
            processor.processBlock(audio, midi);
        }
        const double after{residentMegabytes()};
        std::printf("  %d create/destroy cycles: %.1f MB -> %.1f MB (%+.1f MB)\n",
                    cycles, before, after, after - before);
        check(after - before < 128.0,
              "repeated processor create/destroy does not grow memory unboundedly");
    }

    std::printf("\n-- editor lifecycles --\n");
    {
        JuicySFAudioProcessor processor;
        processor.prepareToPlay(sampleRate, 512);
        const auto state{stateFor(bank.getFullPathName())};
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const double before{residentMegabytes()};
        constexpr int cycles{20};
        // The first editor pulls in JUCE's font, graphics, and window machinery,
        // which is a one-time cost and would mask a per-cycle leak inside a
        // single before/after delta. Measure the steady state separately.
        double afterFirst{0.0};
        bool editorCreated{true};
        for (int cycle = 0; cycle < cycles; ++cycle) {
            std::unique_ptr<juce::AudioProcessorEditor> editor{processor.createEditor()};
            if (editor == nullptr) {
                editorCreated = false;
                break;
            }
            editor->setSize(editor->getWidth(), editor->getHeight());
            if (cycle == 0)
                afterFirst = residentMegabytes();
        }
        const double after{residentMegabytes()};
        check(editorCreated, "the editor can be created headlessly");
        std::printf("  first open/close: %.1f MB -> %.1f MB (%+.1f MB one-time init)\n",
                    before, afterFirst, afterFirst - before);
        std::printf("  %d further cycles: %.1f MB -> %.1f MB (%+.1f MB)\n",
                    cycles - 1, afterFirst, after, after - afterFirst);
        check(editorCreated && after - afterFirst < 8.0,
              "editor open/close is flat after the first, so nothing leaks per cycle");
    }

    std::printf("\n-- concurrent instances --\n");
    {
        constexpr int instanceCount{8};
        constexpr int blockSize{512};
        const double before{residentMegabytes()};
        std::vector<std::unique_ptr<JuicySFAudioProcessor>> instances;
        for (int instance = 0; instance < instanceCount; ++instance) {
            auto processor{std::make_unique<JuicySFAudioProcessor>()};
            processor->prepareToPlay(sampleRate, blockSize);
            const auto state{stateFor(bank.getFullPathName())};
            processor->setStateInformation(state.getData(),
                                           static_cast<int>(state.getSize()));
            instances.push_back(std::move(processor));
        }
        const double loaded{residentMegabytes()};

        juce::AudioBuffer<float> audio{2, blockSize};
        for (auto& processor : instances) {
            juce::MidiBuffer midi;
            addSixteenChannelChords(midi);
            audio.clear();
            processor->processBlock(audio, midi);
        }
        const int blocks{static_cast<int>(sampleRate * 2.0 / blockSize)};
        const double start{juce::Time::getMillisecondCounterHiRes()};
        for (int block = 0; block < blocks; ++block)
            for (auto& processor : instances) {
                juce::MidiBuffer empty;
                audio.clear();
                processor->processBlock(audio, empty);
            }
        const double elapsedMs{juce::Time::getMillisecondCounterHiRes() - start};
        const double audioMs{blocks * blockSize * 1000.0 / sampleRate};
        std::printf("  %d instances: %.1f MB -> %.1f MB (%+.1f MB, %.1f MB each)\n",
                    instanceCount, before, loaded, loaded - before,
                    (loaded - before) / instanceCount);
        std::printf("  %d instances rendering: %.0f ms cpu for %.0f ms audio (%.1f%% of realtime)\n",
                    instanceCount, elapsedMs, audioMs, 100.0 * elapsedMs / audioMs);
        checkTiming(elapsedMs < audioMs,
              "eight concurrent instances render faster than realtime combined");
    }

    std::printf("\n== perf_probe: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
