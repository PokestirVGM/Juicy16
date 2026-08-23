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

#include "PluginProcessor.h"

#include <algorithm>
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

} // namespace

int main(int argc, char** argv)
{
    // Line-buffered so FluidSynth's unbuffered stderr interleaves correctly with
    // the section headers when a CI job captures both streams.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc != 2) {
        std::fprintf(stderr, "usage: JuicySFPerfProbe <bank.dls|sf2|sf3>\n");
        return 2;
    }

    const juce::File bank{juce::String{argv[1]}};
    if (!bank.existsAsFile()) {
        std::fprintf(stderr, "bank not found: %s\n", bank.getFullPathName().toRawUTF8());
        return 2;
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
        check(elapsedMs < audioMs,
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
        check(elapsedMs < audioMs,
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
        check(elapsedMs < audioMs,
              "the voice ceiling renders faster than realtime at the smallest tested block");
    }

    std::printf("\n-- program change and controller automation --\n");
    {
        constexpr int blockSize{64};
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
        check(elapsedMs < audioMs,
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
        check(elapsedMs < audioMs,
              "eight concurrent instances render faster than realtime combined");
    }

    std::printf("\n== perf_probe: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
