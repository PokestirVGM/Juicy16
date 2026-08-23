// Amplitude-domain diagnostic: velocity response, CC7 volume, CC10 pan, and the
// CC71-79 sound controllers, measured through the shipping plugin and through a
// stock FluidSynth loaded with the same bank in the same process.
//
// The point is the *delta*. Anything the plugin does to dynamics shows up as a
// difference between the two columns; anything identical in both columns is
// FluidSynth or the bank, not Juicy16.
//
//   JuicySFDynamicsProbe <bank.sf2|sf3|dls> [program]

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate{48000.0};
constexpr int kBlockSize{512};
constexpr int kNote{60};

double toDb(double linear)
{
    return linear <= 1.0e-9 ? -200.0 : 20.0 * std::log10(linear);
}

struct Measurement {
    double peak{0.0};
    double rms{0.0};
    double leftRms{0.0};
    double rightRms{0.0};
    double timeToPeakSeconds{0.0};
    double tailRms{0.0};  // RMS of the last 10% of the window
};

Measurement analyse(const juce::AudioBuffer<float>& audio)
{
    Measurement result;
    const int frames{audio.getNumSamples()};
    double sum{0.0}, sumLeft{0.0}, sumRight{0.0};
    int peakFrame{0};
    for (int i = 0; i < frames; ++i) {
        const double left{audio.getSample(0, i)};
        const double right{audio.getSample(1, i)};
        const double magnitude{std::max(std::abs(left), std::abs(right))};
        if (magnitude > result.peak) {
            result.peak = magnitude;
            peakFrame = i;
        }
        sum += left * left + right * right;
        sumLeft += left * left;
        sumRight += right * right;
    }
    result.rms = std::sqrt(sum / std::max(1, frames * 2));
    result.leftRms = std::sqrt(sumLeft / std::max(1, frames));
    result.rightRms = std::sqrt(sumRight / std::max(1, frames));
    result.timeToPeakSeconds = peakFrame / kSampleRate;

    const int tailStart{frames - frames / 10};
    double tail{0.0};
    for (int i = tailStart; i < frames; ++i)
        tail += audio.getSample(0, i) * audio.getSample(0, i)
              + audio.getSample(1, i) * audio.getSample(1, i);
    result.tailRms = std::sqrt(tail / std::max(1, (frames - tailStart) * 2));
    return result;
}

// ---------------------------------------------------------------------------
// Stock FluidSynth reference: settings straight out of new_fluid_settings(),
// nothing configured, no added modulators.
// ---------------------------------------------------------------------------

class StockSynth {
public:
    explicit StockSynth(const juce::String& bankPath)
    {
        settings = new_fluid_settings();
        fluid_settings_setnum(settings, "synth.sample-rate", kSampleRate);
        synth = new_fluid_synth(settings);
        fontId = fluid_synth_sfload(synth, bankPath.toRawUTF8(), 1);
    }

    ~StockSynth()
    {
        if (synth != nullptr) delete_fluid_synth(synth);
        if (settings != nullptr) delete_fluid_settings(settings);
    }

    bool loaded() const { return synth != nullptr && fontId != FLUID_FAILED; }

    void reset()
    {
        fluid_synth_system_reset(synth);
        for (int channel = 0; channel < 16; ++channel) {
            fluid_synth_cc(synth, channel, 7, 100);
            fluid_synth_cc(synth, channel, 10, 64);
            fluid_synth_cc(synth, channel, 11, 127);
            fluid_synth_cc(synth, channel, 91, 0);
            fluid_synth_cc(synth, channel, 93, 0);
            for (const int soundCc : {71, 72, 73, 74, 75, 79})
                fluid_synth_cc(synth, channel, soundCc, 64);
            fluid_synth_pitch_bend(synth, channel, 8192);
        }
    }

    void program(int channel, int bank, int preset)
    {
        fluid_synth_bank_select(synth, channel, bank);
        fluid_synth_program_change(synth, channel, preset);
    }

    void cc(int channel, int controller, int value)
    {
        fluid_synth_cc(synth, channel, controller, value);
    }

    void noteOn(int channel, int note, int velocity)
    {
        fluid_synth_noteon(synth, channel, note, velocity);
    }

    void noteOff(int channel, int note) { fluid_synth_noteoff(synth, channel, note); }

    void render(juce::AudioBuffer<float>& audio)
    {
        audio.clear();
        const int frames{audio.getNumSamples()};
        for (int position = 0; position < frames; position += kBlockSize) {
            const int chunk{std::min(kBlockSize, frames - position)};
            float* outputs[]{audio.getWritePointer(0, position),
                             audio.getWritePointer(1, position)};
            fluid_synth_process(synth, chunk, 0, nullptr, 2, outputs);
        }
    }

    double gain() const
    {
        double value{0.0};
        fluid_settings_getnum(settings, "synth.gain", &value);
        return value;
    }

    int intSetting(const char* name) const
    {
        int value{0};
        fluid_settings_getint(settings, name, &value);
        return value;
    }

    double numSetting(const char* name) const
    {
        double value{0.0};
        fluid_settings_getnum(settings, name, &value);
        return value;
    }

    void setGain(double value) { fluid_synth_set_gain(synth, static_cast<float>(value)); }
    void setEffects(bool on)
    {
        fluid_synth_reverb_on(synth, -1, on ? 1 : 0);
        fluid_synth_chorus_on(synth, -1, on ? 1 : 0);
    }

private:
    fluid_settings_t* settings{nullptr};
    fluid_synth_t* synth{nullptr};
    int fontId{FLUID_FAILED};
};

// ---------------------------------------------------------------------------
// The shipping plugin, driven exactly as a host drives it.
// ---------------------------------------------------------------------------

juce::MemoryBlock makeState(const juce::String& fontPath)
{
    juce::XmlElement xml{"MYPLUGINSETTINGS"};
    xml.setAttribute("stateVersion", 2);
    auto* ui{xml.createNewChildElement("uiState")};
    ui->setAttribute("width", 850);
    ui->setAttribute("height", 650);
    ui->setAttribute("selectedChannel", 1);
    auto* font{xml.createNewChildElement("soundFont")};
    font->setAttribute("path", fontPath);
    font->setAttribute("bookmark", "");
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    return state;
}

class PluginHarness {
public:
    explicit PluginHarness(const juce::String& bankPath)
    {
        processor.prepareToPlay(kSampleRate, kBlockSize);
        const auto state{makeState(bankPath)};
        processor.setStateInformation(state.getData(),
                                      static_cast<int>(state.getSize()));
    }

    bool loaded() { return processor.getFluidSynthModel().getFontLoadStatus() == "loaded"; }

    void queue(const juce::MidiMessage& message) { pending.addEvent(message, 0); }

    void render(juce::AudioBuffer<float>& audio)
    {
        audio.clear();
        const int frames{audio.getNumSamples()};
        for (int position = 0; position < frames; position += kBlockSize) {
            const int chunk{std::min(kBlockSize, frames - position)};
            juce::AudioBuffer<float> slice{audio.getArrayOfWritePointers(), 2, position, chunk};
            juce::MidiBuffer midi;
            if (position == 0)
                midi = pending;
            processor.processBlock(slice, midi);
            processor.getFluidSynthModel().handleUpdateNowIfNeeded();
        }
        pending.clear();
    }

    // All Sound Off silences voices but deliberately does NOT reset controllers,
    // so a long-lived instance would carry CC7/CC10 from the previous
    // measurement into the next one. Restore the controllers this probe touches
    // to FluidSynth's own channel defaults explicitly.
    void reset()
    {
        juce::AudioBuffer<float> flush{2, kBlockSize};
        for (int channel = 1; channel <= 16; ++channel) {
            queue(juce::MidiMessage::controllerEvent(channel, 120, 0));
            queue(juce::MidiMessage::controllerEvent(channel, 7, 100));
            queue(juce::MidiMessage::controllerEvent(channel, 10, 64));
            queue(juce::MidiMessage::controllerEvent(channel, 11, 127));
            queue(juce::MidiMessage::controllerEvent(channel, 91, 0));
            queue(juce::MidiMessage::controllerEvent(channel, 93, 0));
            for (const int soundCc : {71, 72, 73, 74, 75, 79})
                queue(juce::MidiMessage::controllerEvent(channel, soundCc, 64));
            queue(juce::MidiMessage::pitchWheel(channel, 8192));
        }
        render(flush);
    }

    JuicySFAudioProcessor processor;

private:
    juce::MidiBuffer pending;
};

// ---------------------------------------------------------------------------

Measurement measurePlugin(PluginHarness& plugin, int bank, int preset, int velocity,
                          const std::vector<std::pair<int, int>>& ccs,
                          double seconds, bool release)
{
    plugin.reset();
    juce::AudioBuffer<float> audio{2, static_cast<int>(seconds * kSampleRate)};
    plugin.queue(juce::MidiMessage::controllerEvent(1, 0, bank));
    plugin.queue(juce::MidiMessage::programChange(1, preset));
    for (const auto& [controller, value] : ccs)
        plugin.queue(juce::MidiMessage::controllerEvent(1, controller, value));
    plugin.queue(juce::MidiMessage::noteOn(1, kNote, static_cast<juce::uint8>(velocity)));
    if (release)
        plugin.queue(juce::MidiMessage::noteOff(1, kNote));
    plugin.render(audio);
    return analyse(audio);
}

Measurement measureStock(StockSynth& stock, int bank, int preset, int velocity,
                         const std::vector<std::pair<int, int>>& ccs,
                         double seconds, bool release)
{
    stock.reset();
    juce::AudioBuffer<float> audio{2, static_cast<int>(seconds * kSampleRate)};
    stock.program(0, bank, preset);
    for (const auto& [controller, value] : ccs)
        stock.cc(0, controller, value);
    stock.noteOn(0, kNote, velocity);
    if (release)
        stock.noteOff(0, kNote);
    stock.render(audio);
    return analyse(audio);
}

void heading(const char* text)
{
    std::printf("\n== %s ==\n", text);
}

} // namespace

namespace {

// Render a whole MIDI file through the plugin and report what it actually peaks
// at. Gain is a pure linear scale on the output, so one render answers "what
// would gain g have produced" for every g without re-rendering.
int runMidiLevelSurvey(const juce::String& bankPath, const juce::File& midiFile)
{
    juce::FileInputStream stream{midiFile};
    juce::MidiFile file;
    if (stream.failedToOpen() || !file.readFrom(stream)) {
        std::fprintf(stderr, "could not read MIDI file\n");
        return 2;
    }
    file.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence merged;
    for (int track = 0; track < file.getNumTracks(); ++track)
        merged.addSequence(*file.getTrack(track), 0.0);
    merged.updateMatchedPairs();
    merged.sort();

    PluginHarness harness{bankPath};
    if (!harness.loaded()) {
        std::fprintf(stderr, "plugin could not load %s\n", bankPath.toRawUTF8());
        return 2;
    }
    auto& model{harness.processor.getFluidSynthModel()};

    const double duration{merged.getEndTime() + 2.0};
    const int totalFrames{static_cast<int>(duration * kSampleRate)};
    juce::AudioBuffer<float> block{2, kBlockSize};
    double peak{0.0}, energy{0.0};
    long long overs{0}, counted{0};
    int event{0};
    for (int position = 0; position < totalFrames; position += kBlockSize) {
        const double blockStart{position / kSampleRate};
        const double blockEnd{(position + kBlockSize) / kSampleRate};
        juce::MidiBuffer midi;
        while (event < merged.getNumEvents()
               && merged.getEventPointer(event)->message.getTimeStamp() < blockEnd) {
            const auto& message{merged.getEventPointer(event)->message};
            midi.addEvent(message, juce::jlimit(0, kBlockSize - 1,
                static_cast<int>((message.getTimeStamp() - blockStart) * kSampleRate)));
            ++event;
        }
        block.clear();
        model.processBlock(block, midi);
        model.handleUpdateNowIfNeeded();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlockSize; ++i) {
                const double sample{std::abs(block.getSample(ch, i))};
                peak = std::max(peak, sample);
                energy += sample * sample;
                if (sample > 1.0) ++overs;
                ++counted;
            }
    }
    const double rms{std::sqrt(energy / std::max(1LL, counted))};
    std::printf("\n== Real material: %s ==\n", midiFile.getFileName().toRawUTF8());
    std::printf("duration %.1f s, rendered at the shipping configuration\n", duration);
    std::printf("peak %.4f (%.2f dBFS)   RMS %.2f dBFS   samples over 0 dBFS: "
                "%lld of %lld (%.2f%%)\n",
                peak, toDb(peak), toDb(rms), overs, counted,
                100.0 * static_cast<double>(overs) / static_cast<double>(counted));

    std::printf("\nWhat other output trims would have produced (gain scales the\n"
                "output linearly, so one render answers all of them):\n");
    std::printf("\n%8s | %12s | %12s | %s\n",
                "x", "peak dBFS", "RMS dBFS", "verdict");
    for (const double candidate : {1.0, 0.6, 0.5, 0.4, 0.3, 0.2, 0.15, 0.1}) {
        const double scaledPeak{toDb(peak * candidate)};
        const double scaledRms{toDb(rms * candidate)};
        const char* verdict{scaledPeak > 0.0 ? "CLIPS"
                            : scaledPeak > -3.0 ? "almost no headroom"
                            : scaledPeak > -9.0 ? "usable headroom"
                                                : "conservative"};
        std::printf("%8.2f | %12.2f | %12.2f | %s%s\n", candidate, scaledPeak,
                    scaledRms, verdict,
                    std::abs(candidate - 1.0) < 1e-9 ? "   <- as shipped (0 dB trim)"
                                                     : "");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc < 2) {
        std::fprintf(stderr, "usage: JuicySFDynamicsProbe <bank> [program]\n"
                             "       JuicySFDynamicsProbe <bank> --midi <file.mid>\n");
        return 2;
    }
    const juce::String bankPath{argv[1]};
    if (argc >= 4 && juce::String{argv[2]} == "--midi")
        return runMidiLevelSurvey(bankPath, juce::File{argv[3]});
    const int preset{argc >= 3 ? juce::String{argv[2]}.getIntValue() : 0};

    StockSynth stock{bankPath};
    if (!stock.loaded()) {
        std::fprintf(stderr, "stock FluidSynth could not load %s\n", argv[1]);
        return 2;
    }
    PluginHarness plugin{bankPath};
    if (!plugin.loaded()) {
        std::fprintf(stderr, "plugin could not load %s\n", argv[1]);
        return 2;
    }

    // A completely fresh instance: no reset, no prior message of any kind. This
    // separates "the plugin is created this way" from "something a message did".
    {
        PluginHarness fresh{bankPath};
        juce::AudioBuffer<float> audio{2, static_cast<int>(1.0 * kSampleRate)};
        fresh.queue(juce::MidiMessage::controllerEvent(1, 0, 0));
        fresh.queue(juce::MidiMessage::programChange(1, preset));
        fresh.queue(juce::MidiMessage::noteOn(1, kNote, static_cast<juce::uint8>(127)));
        fresh.render(audio);
        std::printf("fresh instance, first message ever, note at velocity 127: "
                    "rms %.6f (%.2f dBFS peak)\n",
                    analyse(audio).rms, toDb(analyse(audio).peak));
    }

    // THE decisive test. Each case gets its own brand-new instance, because the
    // effect only fires on the FIRST channel-mode message an instance sees:
    // restoreSixteenChannelLayout reconfigures the basic channel, and FluidSynth
    // re-initialises controllers when that configuration actually changes.
    // Afterwards the call is a no-op, which is why a probe that resets first sees
    // nothing.
    std::printf("\n== First channel-mode message on a fresh instance ==\n");
    std::printf("Set ch2 to CC7=40 (quiet) and CC10=0 (hard left), send one mode\n"
                "message on ch1, then play ch2. Per MIDI, only CC121 may reset\n"
                "controllers - and never on another channel.\n\n");
    std::printf("%-34s | %-12s | %-12s | %s\n",
                "mode message on ch1", "ch2 level", "ch2 L-R", "verdict");
    for (const auto& [modeCc, label] : std::vector<std::pair<int, const char*>>{
             {-1,  "(none - baseline)"},
             {120, "CC120 All Sound Off"},
             {123, "CC123 All Notes Off"},
             {121, "CC121 Reset All Controllers"}}) {
        PluginHarness fresh{bankPath};
        juce::AudioBuffer<float> audio{2, static_cast<int>(1.0 * kSampleRate)};
        fresh.queue(juce::MidiMessage::controllerEvent(2, 0, 0));
        fresh.queue(juce::MidiMessage::programChange(2, preset));
        fresh.queue(juce::MidiMessage::controllerEvent(2, 7, 40));
        fresh.queue(juce::MidiMessage::controllerEvent(2, 10, 0));
        if (modeCc > 0)
            fresh.queue(juce::MidiMessage::controllerEvent(1, modeCc, 0));
        fresh.queue(juce::MidiMessage::noteOn(2, kNote, static_cast<juce::uint8>(127)));
        fresh.render(audio);
        const auto m{analyse(audio)};
        const double balance{toDb(m.leftRms) - toDb(m.rightRms)};
        char level[24], pan[24];
        std::snprintf(level, sizeof level, "%.2f dBFS", toDb(m.peak));
        std::snprintf(pan, sizeof pan, "%+.1f dB", balance);
        std::printf("%-34s | %-12s | %-12s | %s\n", label, level, pan,
                    modeCc < 0 ? "reference"
                               : (toDb(m.peak) > -20.0 || balance < 20.0
                                      ? "*** ch2 VOLUME/PAN WIPED ***"
                                      : "ch2 survived"));
    }
    std::printf("\n");

    std::printf("bank: %s   program: %d\n", argv[1], preset);
    std::printf("stock FluidSynth defaults: gain %.3f  reverb %d  chorus %d  "
                "polyphony %d\n",
                stock.gain(), stock.intSetting("synth.reverb.active"),
                stock.intSetting("synth.chorus.active"),
                stock.intSetting("synth.polyphony"));

    // -- velocity ------------------------------------------------------------
    heading("Velocity response (peak dB, referenced to velocity 127)");
    std::printf("%4s | %12s | %12s | %10s\n", "vel", "stock dB", "Juicy16 dB", "delta dB");
    Measurement stockRef{measureStock(stock, 0, preset, 127, {}, 1.0, false)};
    Measurement pluginRef{measurePlugin(plugin, 0, preset, 127, {}, 1.0, false)};
    for (const int velocity : {1, 8, 16, 24, 32, 48, 64, 80, 96, 112, 127}) {
        const auto s{measureStock(stock, 0, preset, velocity, {}, 1.0, false)};
        const auto p{measurePlugin(plugin, 0, preset, velocity, {}, 1.0, false)};
        const double sDb{toDb(s.peak) - toDb(stockRef.peak)};
        const double pDb{toDb(p.peak) - toDb(pluginRef.peak)};
        std::printf("%4d | %12.2f | %12.2f | %10.2f\n", velocity, sDb, pDb, pDb - sDb);
    }

    // -- CC7 volume ----------------------------------------------------------
    heading("CC7 channel volume (peak dB, referenced to CC7=127)");
    std::printf("%4s | %12s | %12s | %10s\n", "cc7", "stock dB", "Juicy16 dB", "delta dB");
    stockRef = measureStock(stock, 0, preset, 100, {{7, 127}}, 1.0, false);
    pluginRef = measurePlugin(plugin, 0, preset, 100, {{7, 127}}, 1.0, false);
    for (const int value : {0, 16, 32, 48, 64, 80, 96, 112, 127}) {
        const auto s{measureStock(stock, 0, preset, 100, {{7, value}}, 1.0, false)};
        const auto p{measurePlugin(plugin, 0, preset, 100, {{7, value}}, 1.0, false)};
        const double sDb{toDb(s.peak) - toDb(stockRef.peak)};
        const double pDb{toDb(p.peak) - toDb(pluginRef.peak)};
        std::printf("%4d | %12.2f | %12.2f | %10.2f\n", value, sDb, pDb, pDb - sDb);
    }

    // -- CC10 pan ------------------------------------------------------------
    heading("CC10 pan (L/R balance in dB; negative = left louder)");
    std::printf("%5s | %14s | %14s | %10s\n", "cc10", "stock L-R dB", "Juicy16 L-R dB", "delta dB");
    for (const int value : {0, 16, 32, 48, 64, 80, 96, 112, 127}) {
        const auto s{measureStock(stock, 0, preset, 100, {{10, value}}, 1.0, false)};
        const auto p{measurePlugin(plugin, 0, preset, 100, {{10, value}}, 1.0, false)};
        const double sBalance{toDb(s.leftRms) - toDb(s.rightRms)};
        const double pBalance{toDb(p.leftRms) - toDb(p.rightRms)};
        std::printf("%5d | %14.2f | %14.2f | %10.2f\n", value, sBalance, pBalance,
                    pBalance - sBalance);
    }

    // -- CC71-79 sound controllers ------------------------------------------
    // These are Juicy16's own modulators; stock FluidSynth has no modulator for
    // them at all, so the stock column is the "no effect" baseline.
    heading("CC71-79 sound controllers (Juicy16 modulators; stock has none)");
    struct ControllerCase { int cc; const char* name; };
    for (const ControllerCase& item : {ControllerCase{73, "attack (CC73)"},
                                       ControllerCase{75, "decay (CC75)"},
                                       ControllerCase{79, "sustain (CC79)"},
                                       ControllerCase{72, "release (CC72)"},
                                       ControllerCase{74, "cutoff (CC74)"},
                                       ControllerCase{71, "resonance (CC71)"}}) {
        std::printf("\n%s\n", item.name);
        std::printf("%5s | %10s | %12s | %14s | %12s | %12s\n",
                    "value", "peak dB", "t-peak ms", "tail dB", "vs cc=64",
                    "stock t-peak");
        const auto neutral{measurePlugin(plugin, 0, preset, 100, {{item.cc, 64}}, 2.0, false)};
        for (const int value : {0, 32, 64, 96, 127}) {
            const auto p{measurePlugin(plugin, 0, preset, 100, {{item.cc, value}}, 2.0, false)};
            const auto st{measureStock(stock, 0, preset, 100, {{item.cc, value}}, 2.0, false)};
            std::printf("%5d | %10.2f | %12.1f | %14.2f | %12.2f | %12.1f\n",
                        value, toDb(p.peak), p.timeToPeakSeconds * 1000.0,
                        toDb(p.tailRms), toDb(p.peak) - toDb(neutral.peak),
                        st.timeToPeakSeconds * 1000.0);
        }
    }

    // -- release, measured properly ------------------------------------------
    // CC72 only does anything after a note-off, so the sweep above (which holds
    // the note) cannot see it. Play a note, release it a quarter of the way in,
    // and measure how much energy is left well after the release.
    heading("CC72 release, with an actual note-off at 0.5 s of a 3 s window");
    std::printf("%5s | %14s | %14s | %12s\n",
                "value", "energy @1.5s", "energy @2.5s", "vs cc=64");
    {
        const auto measureRelease{[&](int value) {
            plugin.reset();
            juce::AudioBuffer<float> audio{2, static_cast<int>(3.0 * kSampleRate)};
            plugin.queue(juce::MidiMessage::controllerEvent(1, 0, 0));
            plugin.queue(juce::MidiMessage::programChange(1, preset));
            plugin.queue(juce::MidiMessage::controllerEvent(1, 72, value));
            plugin.queue(juce::MidiMessage::noteOn(1, kNote, static_cast<juce::uint8>(100)));
            plugin.render(audio);
            // Note-off is queued for the block that lands at 0.5 s; simplest is to
            // render in two passes instead.
            return audio;
        }};
        const auto windowDb{[](const juce::AudioBuffer<float>& audio, double at) {
            const int start{static_cast<int>(at * kSampleRate)};
            const int length{static_cast<int>(0.1 * kSampleRate)};
            double energy{0.0};
            for (int ch = 0; ch < 2; ++ch)
                for (int i = start; i < start + length && i < audio.getNumSamples(); ++i)
                    energy += audio.getSample(ch, i) * audio.getSample(ch, i);
            return toDb(std::sqrt(energy / (length * 2)));
        }};
        double neutralAt15{0.0};
        for (const int value : {0, 32, 64, 96, 127}) {
            plugin.reset();
            juce::AudioBuffer<float> audio{2, static_cast<int>(3.0 * kSampleRate)};
            audio.clear();
            auto& model{plugin.processor.getFluidSynthModel()};
            const int offFrame{static_cast<int>(0.5 * kSampleRate)};
            for (int position = 0; position < audio.getNumSamples(); position += kBlockSize) {
                const int chunk{std::min(kBlockSize, audio.getNumSamples() - position)};
                juce::AudioBuffer<float> slice{audio.getArrayOfWritePointers(), 2,
                                               position, chunk};
                juce::MidiBuffer midi;
                if (position == 0) {
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 0, 0), 0);
                    midi.addEvent(juce::MidiMessage::programChange(1, preset), 0);
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, 72, value), 0);
                    midi.addEvent(juce::MidiMessage::noteOn(1, kNote,
                                                            static_cast<juce::uint8>(100)), 0);
                }
                if (position <= offFrame && offFrame < position + chunk)
                    midi.addEvent(juce::MidiMessage::noteOff(1, kNote), offFrame - position);
                model.processBlock(slice, midi);
            }
            const double at15{windowDb(audio, 1.5)};
            const double at25{windowDb(audio, 2.5)};
            if (value == 64) neutralAt15 = at15;
            std::printf("%5d | %14.2f | %14.2f | %12.2f\n",
                        value, at15, at25, at15 - neutralAt15);
        }
        juce::ignoreUnused(measureRelease);
    }

    // -- absolute output level ----------------------------------------------
    heading("Absolute output level, single note at velocity 127");
    const auto sAbs{measureStock(stock, 0, preset, 127, {}, 1.0, false)};
    const auto pAbs{measurePlugin(plugin, 0, preset, 127, {}, 1.0, false)};
    std::printf("stock (gain 0.2)   peak %.4f (%.2f dBFS)   rms %.5f\n",
                sAbs.peak, toDb(sAbs.peak), sAbs.rms);
    std::printf("Juicy16 (gain 1.0) peak %.4f (%.2f dBFS)   rms %.5f\n",
                pAbs.peak, toDb(pAbs.peak), pAbs.rms);
    std::printf("difference: %.2f dB   (gain ratio alone would be %.2f dB)\n",
                toDb(pAbs.peak) - toDb(sAbs.peak), 20.0 * std::log10(1.0 / 0.2));

    // Same measurement with the stock synth's gain raised to Juicy16's, which
    // separates "the gain setting" from "everything else".
    stock.setGain(1.0);
    const auto sMatched{measureStock(stock, 0, preset, 127, {}, 1.0, false)};
    std::printf("stock at gain 1.0  peak %.4f (%.2f dBFS)\n",
                sMatched.peak, toDb(sMatched.peak));
    std::printf("residual difference after matching gain: %.2f dB\n",
                toDb(pAbs.peak) - toDb(sMatched.peak));

    // Effects contribution, at matched gain.
    stock.setEffects(false);
    const auto sDry{measureStock(stock, 0, preset, 127, {}, 1.0, false)};
    stock.setEffects(true);
    std::printf("stock at gain 1.0, reverb+chorus OFF: peak %.4f (%.2f dBFS)"
                "  -> effects add %.2f dB\n",
                sDry.peak, toDb(sDry.peak), toDb(sMatched.peak) - toDb(sDry.peak));
    stock.setGain(0.2);

    // -- ablation: which plugin setting causes the residual ------------------
    // Rebuild a stock synth from scratch for each step, applying Juicy16's
    // configuration one item at a time. The step that moves the number is the
    // cause; guessing is not required.
    heading("Ablation: stock FluidSynth + one Juicy16 setting at a time");
    {
        struct Step { const char* name; bool gain; bool interp; bool mods; bool settings; };
        const Step steps[]{
            {"stock defaults",                 false, false, false, false},
            {"+ gain 1.0",                      true, false, false, false},
            {"+ gain, 7th-order interpolation", true,  true, false, false},
            {"+ gain, interp, CC71-79 mods",    true,  true,  true, false},
            {"+ all of the above, + settings",  true,  true,  true,  true},
        };
        for (const Step& step : steps) {
            fluid_settings_t* settings{new_fluid_settings()};
            fluid_settings_setnum(settings, "synth.sample-rate", kSampleRate);
            if (step.settings) {
                fluid_settings_setstr(settings, "synth.midi-bank-select", "gs");
                fluid_settings_setint(settings, "synth.threadsafe-api", 1);
                fluid_settings_setint(settings, "synth.polyphony", 512);
            }
            fluid_synth_t* synth{new_fluid_synth(settings)};
            if (step.gain)
                fluid_synth_set_gain(synth, 1.0f);
            if (step.interp)
                fluid_synth_set_interp_method(synth, -1, FLUID_INTERP_HIGHEST);
            if (step.mods) {
                const struct { int cc; int dest; double amount; } contracts[]{
                    {71, GEN_FILTERQ, 960.0}, {72, GEN_VOLENVRELEASE, 12000.0},
                    {73, GEN_VOLENVATTACK, 12000.0}, {74, GEN_FILTERFC, 2400.0},
                    {75, GEN_VOLENVDECAY, 12000.0}, {79, GEN_VOLENVSUSTAIN, -1000.0},
                };
                for (const auto& contract : contracts) {
                    fluid_mod_t* mod{new_fluid_mod()};
                    fluid_mod_set_source1(mod, contract.cc,
                        FLUID_MOD_CC | FLUID_MOD_BIPOLAR | FLUID_MOD_LINEAR | FLUID_MOD_POSITIVE);
                    fluid_mod_set_source2(mod, 0, 0);
                    fluid_mod_set_dest(mod, contract.dest);
                    fluid_mod_set_amount(mod, contract.amount);
                    fluid_synth_add_default_mod(synth, mod, FLUID_SYNTH_ADD);
                    delete_fluid_mod(mod);
                }
            }
            fluid_synth_sfload(synth, bankPath.toRawUTF8(), 1);
            juce::AudioBuffer<float> audio{2, static_cast<int>(1.0 * kSampleRate)};
            audio.clear();
            fluid_synth_bank_select(synth, 0, 0);
            fluid_synth_program_change(synth, 0, preset);
            fluid_synth_noteon(synth, 0, kNote, 127);
            for (int position = 0; position < audio.getNumSamples(); position += kBlockSize) {
                const int chunk{std::min(kBlockSize, audio.getNumSamples() - position)};
                float* outputs[]{audio.getWritePointer(0, position),
                                 audio.getWritePointer(1, position)};
                fluid_synth_process(synth, chunk, 0, nullptr, 2, outputs);
            }
            const auto m{analyse(audio)};
            std::printf("%-36s peak %.4f (%7.2f dBFS)\n", step.name, m.peak, toDb(m.peak));
            delete_fluid_synth(synth);
            delete_fluid_settings(settings);
        }
        std::printf("%-36s peak %.4f (%7.2f dBFS)\n", "Juicy16 (all of the above)",
                    pAbs.peak, toDb(pAbs.peak));
    }

    // -- is the residual level, or a different sound? ------------------------
    // Normalise both renderings to their own peak and correlate. ~1.0 means the
    // same waveform at a different level; well below means a different preset,
    // a different layer count, or extra voices.
    heading("Residual: same waveform at a different level, or a different sound?");
    {
        fluid_settings_t* settings{new_fluid_settings()};
        fluid_settings_setnum(settings, "synth.sample-rate", kSampleRate);
        fluid_synth_t* synth{new_fluid_synth(settings)};
        fluid_synth_set_gain(synth, 1.0f);
        fluid_synth_set_interp_method(synth, -1, FLUID_INTERP_HIGHEST);
        fluid_synth_sfload(synth, bankPath.toRawUTF8(), 1);
        juce::AudioBuffer<float> reference{2, static_cast<int>(1.0 * kSampleRate)};
        reference.clear();
        fluid_synth_bank_select(synth, 0, 0);
        fluid_synth_program_change(synth, 0, preset);
        fluid_synth_noteon(synth, 0, kNote, 127);
        for (int position = 0; position < reference.getNumSamples(); position += kBlockSize) {
            const int chunk{std::min(kBlockSize, reference.getNumSamples() - position)};
            float* outputs[]{reference.getWritePointer(0, position),
                             reference.getWritePointer(1, position)};
            fluid_synth_process(synth, chunk, 0, nullptr, 2, outputs);
        }
        {
            int cc91{-1}, cc93{-1}, cc7{-1}, cc10{-1};
            fluid_synth_get_cc(synth, 0, 91, &cc91);
            fluid_synth_get_cc(synth, 0, 93, &cc93);
            fluid_synth_get_cc(synth, 0, 7, &cc7);
            fluid_synth_get_cc(synth, 0, 10, &cc10);
            std::printf("stock channel 1 after load (no system reset): "
                        "CC7=%d CC10=%d CC91(reverb)=%d CC93(chorus)=%d\n",
                        cc7, cc10, cc91, cc93);
            fluid_synth_system_reset(synth);
            fluid_synth_get_cc(synth, 0, 91, &cc91);
            fluid_synth_get_cc(synth, 0, 93, &cc93);
            fluid_synth_get_cc(synth, 0, 7, &cc7);
            fluid_synth_get_cc(synth, 0, 10, &cc10);
            std::printf("stock channel 1 after fluid_synth_system_reset:  "
                        "CC7=%d CC10=%d CC91(reverb)=%d CC93(chorus)=%d\n",
                        cc7, cc10, cc91, cc93);
        }
        std::printf("stock active voices for one note: %d\n",
                    fluid_synth_get_active_voice_count(synth));
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);

        plugin.reset();
        juce::AudioBuffer<float> actual{2, static_cast<int>(1.0 * kSampleRate)};
        plugin.queue(juce::MidiMessage::controllerEvent(1, 0, 0));
        plugin.queue(juce::MidiMessage::programChange(1, preset));
        plugin.queue(juce::MidiMessage::noteOn(1, kNote, static_cast<juce::uint8>(127)));
        plugin.render(actual);

        const auto refPeak{analyse(reference).peak};
        const auto actPeak{analyse(actual).peak};
        double dot{0.0}, energyA{0.0}, energyB{0.0};
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < reference.getNumSamples(); ++i) {
                const double a{reference.getSample(ch, i) / std::max(1.0e-9, refPeak)};
                const double b{actual.getSample(ch, i) / std::max(1.0e-9, actPeak)};
                dot += a * b; energyA += a * a; energyB += b * b;
            }
        }
        juce::MemoryBlock saved;
        plugin.processor.getStateInformation(saved);
        juce::String repairFlag{"unknown"};
        if (auto xml{juce::AudioProcessor::getXmlFromBinary(
                saved.getData(), static_cast<int>(saved.getSize()))}) {
            if (auto* font{xml->getChildByName("soundFont")})
                repairFlag = font->getStringAttribute("usedDlsRepair", "absent");
        }
        std::printf("plugin load status: %s   usedDlsRepair: %s\n",
                    plugin.processor.getFluidSynthModel().getFontLoadStatus().toRawUTF8(),
                    repairFlag.toRawUTF8());

        int pluginBank{-1}, pluginPreset{-1};
        plugin.processor.getFluidSynthModel().getChannelProgram(0, pluginBank, pluginPreset);
        int noteBank{-1}, notePreset{-1}, noteSample{-1};
        plugin.processor.getFluidSynthModel().getLastDispatchedNoteOnProgram(
            0, noteBank, notePreset, noteSample);
        std::printf("plugin channel 1 reports bank %d preset %d; "
                    "note sounded with bank %d preset %d\n",
                    pluginBank, pluginPreset, noteBank, notePreset);

        // Fundamental frequency by autocorrelation over a steady segment. A pitch
        // difference means the two synths are running at different sample rates;
        // equal pitch with unequal level means a gain-path difference.
        const auto estimateF0{[](const juce::AudioBuffer<float>& audio) {
            const int start{static_cast<int>(0.20 * kSampleRate)};
            const int length{static_cast<int>(0.20 * kSampleRate)};
            double best{0.0}; int bestLag{0};
            for (int lag = static_cast<int>(kSampleRate / 800.0);
                 lag < static_cast<int>(kSampleRate / 50.0); ++lag) {
                double sum{0.0};
                for (int i = 0; i < length; ++i)
                    sum += audio.getSample(0, start + i) * audio.getSample(0, start + i + lag);
                if (sum > best) { best = sum; bestLag = lag; }
            }
            return bestLag > 0 ? kSampleRate / bestLag : 0.0;
        }};
        std::printf("fundamental: stock %.2f Hz, Juicy16 %.2f Hz  (MIDI note %d is %.2f Hz)\n",
                    estimateF0(reference), estimateF0(actual), kNote,
                    440.0 * std::pow(2.0, (kNote - 69) / 12.0));

        // Same note again, but straight into FluidSynthModel::processBlock, which
        // skips JuicySFAudioProcessor::processBlock and therefore skips
        // keyboardState.processNextMidiBuffer. If this matches stock, the extra
        // level is being introduced by the processor wrapper, not the engine.
        {
            juce::AudioBuffer<float> direct{2, static_cast<int>(1.0 * kSampleRate)};
            direct.clear();
            auto& model{plugin.processor.getFluidSynthModel()};
            juce::MidiBuffer flushMidi;
            for (int channel = 1; channel <= 16; ++channel)
                flushMidi.addEvent(juce::MidiMessage::controllerEvent(channel, 120, 0), 0);
            juce::AudioBuffer<float> flush{2, kBlockSize};
            model.processBlock(flush, flushMidi);

            juce::MidiBuffer first;
            first.addEvent(juce::MidiMessage::controllerEvent(1, 0, 0), 0);
            first.addEvent(juce::MidiMessage::programChange(1, preset), 0);
            first.addEvent(juce::MidiMessage::noteOn(1, kNote,
                                                     static_cast<juce::uint8>(127)), 0);
            for (int position = 0; position < direct.getNumSamples(); position += kBlockSize) {
                const int chunk{std::min(kBlockSize, direct.getNumSamples() - position)};
                juce::AudioBuffer<float> slice{direct.getArrayOfWritePointers(), 2,
                                               position, chunk};
                juce::MidiBuffer midi;
                if (position == 0) midi = first;
                model.processBlock(slice, midi);
            }
            const auto directMeasure{analyse(direct)};
            std::printf("via FluidSynthModel directly (no keyboardState): "
                        "peak %.4f (%.2f dBFS)\n",
                        directMeasure.peak, toDb(directMeasure.peak));
            std::printf("via JuicySFAudioProcessor (with keyboardState):  "
                        "peak %.4f (%.2f dBFS)   difference %.2f dB\n",
                        actPeak, toDb(actPeak), toDb(actPeak) - toDb(directMeasure.peak));
        }

        // Is the plugin's extra energy reverb/chorus? Send the sends to zero and
        // re-measure. Stock measured CC91=0/CC93=0, so if the plugin is at
        // FluidSynth's GM defaults instead, this is where the difference lives.
        {
            const auto dry{measurePlugin(plugin, 0, preset, 127,
                                         {{91, 0}, {93, 0}}, 1.0, false)};
            std::printf("plugin with CC91=0 and CC93=0: peak %.4f (%.2f dBFS)"
                        "  -> sends were adding %.2f dB\n",
                        dry.peak, toDb(dry.peak), toDb(actPeak) - toDb(dry.peak));
        }

        // FluidSynth initialises a channel's CC7 to 100. If the plugin's channel
        // is somewhere else, that alone is a constant level offset.
        for (const int explicitVolume : {100, 127}) {
            const auto m{measurePlugin(plugin, 0, preset, 127,
                                       {{7, explicitVolume}}, 1.0, false)};
            std::printf("plugin with explicit CC7=%3d: peak %.4f (%.2f dBFS)\n",
                        explicitVolume, m.peak, toDb(m.peak));
        }
        std::printf("plugin with CC7 untouched:   peak %.4f (%.2f dBFS)\n",
                    actPeak, toDb(actPeak));
        std::printf("stock  with CC7 untouched:   peak %.4f (%.2f dBFS)\n",
                    refPeak, toDb(refPeak));

        // RMS (not peak) at matched channel volume. Peak is unreliable here because
        // a few samples of timing offset move which sample is the maximum.
        {
            const auto pluginAt100{measurePlugin(plugin, 0, preset, 127,
                                                 {{7, 100}}, 1.0, false)};
            fluid_settings_t* matchSettings{new_fluid_settings()};
            fluid_settings_setnum(matchSettings, "synth.sample-rate", kSampleRate);
            fluid_synth_t* match{new_fluid_synth(matchSettings)};
            fluid_synth_set_gain(match, 1.0f);
            fluid_synth_set_interp_method(match, -1, FLUID_INTERP_HIGHEST);
            fluid_synth_sfload(match, bankPath.toRawUTF8(), 1);
            juce::AudioBuffer<float> matchAudio{2, static_cast<int>(1.0 * kSampleRate)};
            matchAudio.clear();
            fluid_synth_bank_select(match, 0, 0);
            fluid_synth_program_change(match, 0, preset);
            fluid_synth_cc(match, 0, 7, 100);
            fluid_synth_noteon(match, 0, kNote, 127);
            for (int position = 0; position < matchAudio.getNumSamples(); position += kBlockSize) {
                const int chunk{std::min(kBlockSize, matchAudio.getNumSamples() - position)};
                float* outputs[]{matchAudio.getWritePointer(0, position),
                                 matchAudio.getWritePointer(1, position)};
                fluid_synth_process(match, chunk, 0, nullptr, 2, outputs);
            }
            const auto stockAt100{analyse(matchAudio)};
            delete_fluid_synth(match);
            delete_fluid_settings(matchSettings);
            std::printf("at matched CC7=100 -- RMS: stock %.6f, Juicy16 %.6f, "
                        "difference %.2f dB\n",
                        stockAt100.rms, pluginAt100.rms,
                        toDb(pluginAt100.rms) - toDb(stockAt100.rms));
        }

        // Envelope comparison in 50 ms windows. A constant offset is a level
        // difference; a changing offset is an envelope or timbre difference.
        std::printf("\n%8s | %12s | %12s | %10s\n",
                    "time ms", "stock dB", "Juicy16 dB", "delta dB");
        for (int windowStart = 0; windowStart < reference.getNumSamples();
             windowStart += static_cast<int>(0.05 * kSampleRate)) {
            const int windowLength{std::min(static_cast<int>(0.05 * kSampleRate),
                                            reference.getNumSamples() - windowStart)};
            double refEnergy{0.0}, actEnergy{0.0};
            for (int ch = 0; ch < 2; ++ch)
                for (int i = windowStart; i < windowStart + windowLength; ++i) {
                    refEnergy += reference.getSample(ch, i) * reference.getSample(ch, i);
                    actEnergy += actual.getSample(ch, i) * actual.getSample(ch, i);
                }
            const double refRms{std::sqrt(refEnergy / (windowLength * 2))};
            const double actRms{std::sqrt(actEnergy / (windowLength * 2))};
            if (windowStart <= static_cast<int>(0.5 * kSampleRate))
                std::printf("%8.0f | %12.2f | %12.2f | %10.2f\n",
                            windowStart * 1000.0 / kSampleRate,
                            toDb(refRms), toDb(actRms), toDb(actRms) - toDb(refRms));
        }

        const double correlation{dot / std::sqrt(energyA * energyB + 1.0e-30)};
        std::printf("peak-normalised waveform correlation: %.4f\n", correlation);
        // Which stock (bank, preset) actually produces the plugin's waveform? If
        // the plugin is on a different raw program than it reports, this finds it.
        if (correlation <= 0.99) {
            fluid_settings_t* searchSettings{new_fluid_settings()};
            fluid_settings_setnum(searchSettings, "synth.sample-rate", kSampleRate);
            fluid_synth_t* search{new_fluid_synth(searchSettings)};
            fluid_synth_set_gain(search, 1.0f);
            fluid_synth_set_interp_method(search, -1, FLUID_INTERP_HIGHEST);
            fluid_synth_sfload(search, bankPath.toRawUTF8(), 1);
            double best{-1.0}; int bestBank{-1}, bestPreset{-1}; double bestPeak{0.0};
            juce::AudioBuffer<float> candidate{2, static_cast<int>(1.0 * kSampleRate)};
            // Enumerate the font's real (bank, preset) pairs rather than guessing
            // a range, so a bank offset or an unexpected bank cannot hide the match.
            std::vector<std::pair<int, int>> presets;
            if (auto* sfont{fluid_synth_get_sfont_by_id(search, 1)}) {
                fluid_sfont_iteration_start(sfont);
                while (fluid_preset_t* item{fluid_sfont_iteration_next(sfont)})
                    presets.emplace_back(fluid_preset_get_banknum(item),
                                         fluid_preset_get_num(item));
            }
            std::printf("font contains %zu presets\n", presets.size());
            for (const auto& [searchBank, searchPreset] : presets) {
                {
                    fluid_synth_system_reset(search);
                    candidate.clear();
                    fluid_synth_bank_select(search, 0, searchBank);
                    if (fluid_synth_program_change(search, 0, searchPreset) != FLUID_OK)
                        continue;
                    fluid_synth_noteon(search, 0, kNote, 127);
                    for (int position = 0; position < candidate.getNumSamples();
                         position += kBlockSize) {
                        const int chunk{std::min(kBlockSize,
                                                 candidate.getNumSamples() - position)};
                        float* outputs[]{candidate.getWritePointer(0, position),
                                         candidate.getWritePointer(1, position)};
                        fluid_synth_process(search, chunk, 0, nullptr, 2, outputs);
                    }
                    const double candidatePeak{analyse(candidate).peak};
                    double d{0.0}, ea{0.0}, eb{0.0};
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < candidate.getNumSamples(); ++i) {
                            const double a{candidate.getSample(ch, i)
                                / std::max(1.0e-9, candidatePeak)};
                            const double b{actual.getSample(ch, i)
                                / std::max(1.0e-9, actPeak)};
                            d += a * b; ea += a * a; eb += b * b;
                        }
                    const double c{d / std::sqrt(ea * eb + 1.0e-30)};
                    if (c > best) { best = c; bestBank = searchBank;
                                    bestPreset = searchPreset; bestPeak = candidatePeak; }
                }
            }
            std::printf("best stock match: bank %d preset %d, correlation %.4f, "
                        "peak %.4f (%.2f dBFS)\n",
                        bestBank, bestPreset, best, bestPeak, toDb(bestPeak));
            delete_fluid_synth(search);
            delete_fluid_settings(searchSettings);
        }

        std::printf("%s\n", correlation > 0.99
            ? "-> identical waveform: the residual is pure level."
            : "-> DIFFERENT waveform: the residual is not just level "
              "(different preset, layers, or voice count).");
    }

    // -- do channel-mode messages wipe controller state? ---------------------
    // Hosts send All Notes Off / All Sound Off routinely: on stop, on locate, at
    // loop boundaries. Game rips send them too. Per MIDI, CC120 and CC123 must
    // NOT reset channel volume, pan, or expression - only CC121 does that.
    heading("Controller survival across channel-mode messages (CC120/CC121/CC123)");
    {
        struct Case { int cc; const char* name; bool shouldReset; };
        const Case cases[]{
            {120, "CC120 All Sound Off", false},
            {123, "CC123 All Notes Off", false},
            {121, "CC121 Reset All Controllers", true},
        };
        std::printf("%-30s | %-22s | %-22s\n", "after...", "Juicy16 vol/pan", "stock vol/pan");
        for (const Case& item : cases) {
            // Juicy16: set a quiet, hard-left channel, send the mode message,
            // then play a note and see whether the settings survived.
            plugin.reset();
            juce::AudioBuffer<float> pluginAudio{2, static_cast<int>(1.0 * kSampleRate)};
            plugin.queue(juce::MidiMessage::controllerEvent(1, 0, 0));
            plugin.queue(juce::MidiMessage::programChange(1, preset));
            plugin.queue(juce::MidiMessage::controllerEvent(1, 7, 40));
            plugin.queue(juce::MidiMessage::controllerEvent(1, 10, 0));
            plugin.queue(juce::MidiMessage::controllerEvent(1, item.cc, 0));
            plugin.queue(juce::MidiMessage::noteOn(1, kNote, static_cast<juce::uint8>(127)));
            plugin.render(pluginAudio);
            const auto p{analyse(pluginAudio)};

            fluid_settings_t* refSettings{new_fluid_settings()};
            fluid_settings_setnum(refSettings, "synth.sample-rate", kSampleRate);
            fluid_synth_t* refSynth{new_fluid_synth(refSettings)};
            fluid_synth_set_gain(refSynth, 1.0f);
            fluid_synth_sfload(refSynth, bankPath.toRawUTF8(), 1);
            juce::AudioBuffer<float> stockAudio{2, static_cast<int>(1.0 * kSampleRate)};
            stockAudio.clear();
            fluid_synth_bank_select(refSynth, 0, 0);
            fluid_synth_program_change(refSynth, 0, preset);
            fluid_synth_cc(refSynth, 0, 7, 40);
            fluid_synth_cc(refSynth, 0, 10, 0);
            fluid_synth_cc(refSynth, 0, item.cc, 0);
            fluid_synth_noteon(refSynth, 0, kNote, 127);
            for (int position = 0; position < stockAudio.getNumSamples(); position += kBlockSize) {
                const int chunk{std::min(kBlockSize, stockAudio.getNumSamples() - position)};
                float* outputs[]{stockAudio.getWritePointer(0, position),
                                 stockAudio.getWritePointer(1, position)};
                fluid_synth_process(refSynth, chunk, 0, nullptr, 2, outputs);
            }
            const auto st{analyse(stockAudio)};
            int refVolume{-1}, refPan{-1};
            fluid_synth_get_cc(refSynth, 0, 7, &refVolume);
            fluid_synth_get_cc(refSynth, 0, 10, &refPan);
            delete_fluid_synth(refSynth);
            delete_fluid_settings(refSettings);

            char pluginText[64], stockText[64];
            std::snprintf(pluginText, sizeof pluginText, "%.1f dBFS, L-R %+.1f dB",
                          toDb(p.peak), toDb(p.leftRms) - toDb(p.rightRms));
            std::snprintf(stockText, sizeof stockText,
                          "%.1f dBFS, L-R %+.1f dB (cc7=%d cc10=%d)",
                          toDb(st.peak), toDb(st.leftRms) - toDb(st.rightRms),
                          refVolume, refPan);
            std::printf("%-30s | %-22s | %-22s%s\n", item.name, pluginText, stockText,
                        item.shouldReset ? "   <- reset is correct here" : "");
        }
        std::printf("\nExpected: after CC120 and CC123 the note must still be quiet "
                    "and hard left.\n");

        // Cross-channel. restoreSixteenChannelLayout calls
        // fluid_synth_reset_basic_channel(synth, -1), which touches EVERY channel,
        // not just the one the message arrived on. A host sending All Notes Off on
        // one channel must not disturb the other fifteen.
        std::printf("\nCross-channel: set ch2 quiet + hard left, then send the mode "
                    "message on ch1, then play ch2.\n");
        for (const int modeCc : {120, 123}) {
            plugin.reset();
            juce::AudioBuffer<float> audio{2, static_cast<int>(1.0 * kSampleRate)};
            plugin.queue(juce::MidiMessage::controllerEvent(2, 0, 0));
            plugin.queue(juce::MidiMessage::programChange(2, preset));
            plugin.queue(juce::MidiMessage::controllerEvent(2, 7, 40));
            plugin.queue(juce::MidiMessage::controllerEvent(2, 10, 0));
            plugin.queue(juce::MidiMessage::controllerEvent(1, modeCc, 0));
            plugin.queue(juce::MidiMessage::noteOn(2, kNote, static_cast<juce::uint8>(127)));
            plugin.render(audio);
            const auto after{analyse(audio)};

            plugin.reset();
            juce::AudioBuffer<float> control{2, static_cast<int>(1.0 * kSampleRate)};
            plugin.queue(juce::MidiMessage::controllerEvent(2, 0, 0));
            plugin.queue(juce::MidiMessage::programChange(2, preset));
            plugin.queue(juce::MidiMessage::controllerEvent(2, 7, 40));
            plugin.queue(juce::MidiMessage::controllerEvent(2, 10, 0));
            plugin.queue(juce::MidiMessage::noteOn(2, kNote, static_cast<juce::uint8>(127)));
            plugin.render(control);
            const auto baseline{analyse(control)};

            std::printf("  CC%d on ch1: ch2 note is %.2f dBFS (no mode message: "
                        "%.2f dBFS)  -> %s\n",
                        modeCc, toDb(after.peak), toDb(baseline.peak),
                        std::abs(toDb(after.peak) - toDb(baseline.peak)) < 0.5
                            ? "ch2 settings survived"
                            : "*** ch2 SETTINGS WERE WIPED ***");
        }
    }

    // -- headroom under real 16-channel load ---------------------------------
    // A single note tells you nothing about whether a mix clips. This plays a
    // four-note chord on all 16 channels at once, which is ordinary density for
    // the game rips this plugin exists to play.
    heading("Headroom: 4-note chord on all 16 channels, velocity 127");
    {
        juce::AudioBuffer<float> audio{2, static_cast<int>(2.0 * kSampleRate)};
        plugin.reset();
        for (int channel = 1; channel <= 16; ++channel) {
            plugin.queue(juce::MidiMessage::controllerEvent(channel, 0, 0));
            plugin.queue(juce::MidiMessage::programChange(channel, preset));
            for (const int offset : {0, 4, 7, 12})
                plugin.queue(juce::MidiMessage::noteOn(
                    channel, kNote + offset, static_cast<juce::uint8>(127)));
        }
        plugin.render(audio);
        const auto dense{analyse(audio)};
        int overs{0};
        for (int i = 0; i < audio.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                if (std::abs(audio.getSample(ch, i)) > 1.0f) { ++overs; break; }
        std::printf("Juicy16 peak %.4f (%.2f dBFS)   samples over 0 dBFS: %d of %d (%.1f%%)\n",
                    dense.peak, toDb(dense.peak), overs, audio.getNumSamples(),
                    100.0 * overs / audio.getNumSamples());
        std::printf("headroom remaining: %.2f dB\n", -toDb(dense.peak));
    }

    return 0;
}
