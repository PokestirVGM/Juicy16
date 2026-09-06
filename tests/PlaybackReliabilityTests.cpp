#include "PluginProcessor.h"
#include "SyntheticSf2.h"
#include "GuiConstants.h"
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
int failures{0};
class AssertionLogger : public juce::Logger {
public:
    std::atomic<int> assertionCount{0};
    void logMessage(const juce::String& message) override {
        std::fprintf(stderr, "%s\n", message.toRawUTF8());
        if (message.startsWith("JUCE Assertion failure")) assertionCount.fetch_add(1);
    }
};
void check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) ++failures;
}
juce::RangedAudioParameter* findParameter(JuicySFAudioProcessor& p, const juce::String& id) {
    for (auto* candidate : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(candidate))
            if (ranged->paramID == id) return ranged;
    return nullptr;
}
void parameter(JuicySFAudioProcessor& p, const juce::String& id, float value) {
    auto* control = findParameter(p, id);
    check(control != nullptr, ("parameter exists: " + id).toRawUTF8());
    if (control != nullptr) control->setValueNotifyingHost(control->convertTo0to1(value));
}
juce::MemoryBlock stateFor(const juce::File& bank) {
    juce::XmlElement state{"MYPLUGINSETTINGS"};
    state.setAttribute("stateVersion", 6);
    state.createNewChildElement("soundFont")->setAttribute("path", bank.getFullPathName());
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(state, bytes);
    return bytes;
}
void load(JuicySFAudioProcessor& p, const juce::File& bank) {
    p.prepareToPlay(48000.0, 1024);
    const auto bytes = stateFor(bank);
    p.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    parameter(p, "outputLevel", 0.0f);
}
void render(JuicySFAudioProcessor& p, juce::AudioBuffer<float>& out, juce::MidiBuffer& events) {
    out.clear();
    p.processBlock(out, events);
}
void setup(juce::MidiBuffer& events, int ch, int expression = 43) {
    events.addEvent(juce::MidiMessage::controllerEvent(ch, 11, expression), 0);
    events.addEvent(juce::MidiMessage::controllerEvent(ch, 101, 0), 0);
    events.addEvent(juce::MidiMessage::controllerEvent(ch, 100, 0), 0);
    events.addEvent(juce::MidiMessage::controllerEvent(ch, 6, 12), 0);
    events.addEvent(juce::MidiMessage::controllerEvent(ch, 38, 25), 0);
}
void gm(juce::MidiBuffer& events) {
    const juce::uint8 reset[]{0x7e, 0x7f, 9, 1};
    events.addEvent(juce::MidiMessage::createSysExMessage(reset, 4), 0);
}
double energy(const juce::AudioBuffer<float>& b) {
    double sum{0};
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const double sample = b.getSample(ch, i);
            sum += sample * sample;
        }
    return sum;
}
juce::Component* namedChild(juce::Component& root, const juce::String& name) {
    if (root.getName() == name) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = namedChild(*child, name)) return found;
    return nullptr;
}
int firstSound(const juce::AudioBuffer<float>& b) {
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs(b.getSample(0, i)) > 1.0e-7f || std::abs(b.getSample(1, i)) > 1.0e-7f)
            return i;
    return -1;
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    AssertionLogger logger;
    juce::Logger::setCurrentLogger(&logger);
    juce::TemporaryFile file{".sf2"};
    const auto fixture = SyntheticSf2::build({{0, 0, 441.0, "Reference"}, {0, 1, 882.0, "Second"}, {128, 0, 220.5, "Drums"}});
    check(file.getFile().replaceWithData(fixture.getData(), fixture.getSize()), "synthetic bank written");
    const auto bank = file.getFile();
    check(juce::String{fluid_version_str()} == "2.5.7", "test actually links FluidSynth 2.5.7");
    juce::AudioBuffer<float> block{2, 1024};
    {
        JuicySFAudioProcessor p; load(p, bank);
        juce::MidiBuffer events; setup(events, 3);
        events.addEvent(juce::MidiMessage::programChange(3, 1), 1);
        events.addEvent(juce::MidiMessage::controllerEvent(3, 7, 36), 2);
        render(p, block, events);
        // Save before pumping AsyncUpdater: this catches stale ValueTree state.
        juce::MemoryBlock saved; p.getStateInformation(saved);
        JuicySFAudioProcessor restored; restored.prepareToPlay(48000.0, 1024);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        auto& m = restored.getFluidSynthModel();
        int b{-1}, pr{-1}, volume{-1}, expression{-1};
        check(m.getChannelProgram(2, b, pr) && pr == 1
            && m.getControllerValue(2, 7, volume) && volume == 36,
            "immediate save captures latest program and CC7 before UI synchronization");
        gm(events); render(restored, block, events);
        check(m.getControllerValue(2, 11, expression) && expression == 43
            && m.rememberedBendRange(2) == ((12 << 7) | 25),
            "project reopen and recovery reset retain expression and cents bend range");
        parameter(restored, "resetPolicy", 1.0f);
        gm(events); render(restored, block, events);
        int sensitivity{-1};
        check(m.getControllerValue(2, 11, expression) && expression == 127
            && m.getPitchWheelSensitivity(2, sensitivity) && sensitivity == 2
            && m.getChannelProgram(2, b, pr) && pr == 0,
            "standard reset clears previous song's expression, range and program");
        setup(events, 3, 29); render(restored, block, events);
        events.addEvent(juce::MidiMessage::controllerEvent(3, 121, 0), 0);
        render(restored, block, events);
        check(m.getControllerValue(2, 11, expression) && expression == 127,
            "standard CC121 restores expression to 127");
        parameter(restored, "trimCh3", -12.0f);
        const auto old = stateFor(bank);
        restored.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
        check(std::abs(findParameter(restored, "trimCh3")->convertFrom0to1(findParameter(restored, "trimCh3")->getValue())) < 0.00001f
            && findParameter(restored, "resetPolicy")->convertFrom0to1(findParameter(restored, "resetPolicy")->getValue()) == 0.0f
            && m.rememberedExpression(2) == -1 && m.rememberedBendRange(2) == -1,
            "older project loaded into used instance gets unity trim and recovery defaults");
        check(std::isinf(restored.getTailLengthSeconds()), "host is told instrument tails are unbounded");
    }
    // Audio ratios prove the trim is a real post-synthesis gain, independent of
    // CC7, with the SAME gain on the channel's reverb and no cross-channel leak.
    for (int ch : {1, 3, 10, 16}) {
        for (bool wet : {false, true}) {
            double total[2]{};
            for (int pass = 0; pass < 2; ++pass) {
                JuicySFAudioProcessor p; load(p, bank);
                parameter(p, "trimCh" + juce::String(ch), pass == 0 ? 0.0f : -12.0f);
                parameter(p, "reverbOn", wet ? 1.0f : 0.0f);
                juce::MidiBuffer events;
                for (int i = 0; i < 2; ++i) render(p, block, events); // gain settles
                events.addEvent(juce::MidiMessage::controllerEvent(ch, 7, 80), 0);
                events.addEvent(juce::MidiMessage::controllerEvent(ch, 91, 127), 0);
                events.addEvent(juce::MidiMessage::noteOn(ch, 60, static_cast<juce::uint8>(100)), 0);
                for (int i = 0; i < 20; ++i) {
                    if (i == 3 && wet) events.addEvent(juce::MidiMessage::noteOff(ch, 60), 0);
                    render(p, block, events);
                    if (!wet || i >= 8) total[pass] += energy(block);
                }
                const auto d = p.getFluidSynthModel().getChannelDiagnostics(ch - 1);
                int cc7{-1};
                check(p.getFluidSynthModel().getControllerValue(ch - 1, 7, cc7) && cc7 == 80,
                    "audio trim leaves MIDI CC7 unchanged");
                check(d.midiEvents >= 3, "channel meter receives MIDI activity");
                const int other = ch == 16 ? 0 : 15;
                check(p.getFluidSynthModel().getChannelDiagnostics(other).peak < 1.0e-7f,
                    "unplayed channel stays below -140 dBFS (reverb anti-denormal noise)");
            }
            const double measuredDb = 10.0 * std::log10(total[1] / total[0]);
            std::printf("channel %d %s trim: %.4f dB\n", ch, wet ? "wet tail" : "dry", measuredDb);
            check(total[0] > 1.0e-10 && std::abs(measuredDb + 12.0) < 0.02,
                "independent channel trim scales audio by exactly -12 dB");
        }
    }
    {
        JuicySFAudioProcessor p; load(p, bank);
        parameter(p, "muteCh5", 1.0f);
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::noteOn(5, 60, static_cast<juce::uint8>(100)), 0);
        render(p, block, events);
        const auto d = p.getFluidSynthModel().getChannelDiagnostics(4);
        check(d.midiEvents > 0 && d.peak == 0.0f, "muted channel shows MIDI but no audio");
        events.addEvent(juce::MidiMessage::programChange(1, 127), 0);
        render(p, block, events);
        const auto fallback = p.getFluidSynthModel().getChannelDiagnostics(0);
        check(fallback.soundingBank == 0 && fallback.soundingPreset == 0,
            "diagnostics identify actual fallback independently of requested program");
        p.getFluidSynthModel().handleUpdateNowIfNeeded();
        juce::MemoryBlock legacyWindow; p.getStateInformation(legacyWindow);
        auto legacyUi = juce::AudioProcessor::getXmlFromBinary(legacyWindow.getData(), static_cast<int>(legacyWindow.getSize()));
        legacyUi->getChildByName("uiState")->setAttribute("width", 797);
        legacyUi->getChildByName("uiState")->setAttribute("height", 598);
        juce::AudioProcessor::copyXmlToBinary(*legacyUi, legacyWindow);
        p.setStateInformation(legacyWindow.getData(), static_cast<int>(legacyWindow.getSize()));
        std::unique_ptr<juce::AudioProcessorEditor> editor{p.createEditor()};
        check(editor->getWidth() >= GuiConstants::minWidth && editor->getHeight() >= GuiConstants::minHeight,
            "older saved window sizes expand to fit the current interface on first open");
        auto* table = dynamic_cast<juce::TableListBox*>(namedChild(*editor, "MIDI channel assignments"));
        bool aligned = table != nullptr;
        for (int width : {GuiConstants::minWidth, GuiConstants::minWidth + 180, GuiConstants::minWidth}) {
            editor->setBoundsConstrained({0, 0, width, GuiConstants::defaultHeight});
            if (table != nullptr)
                for (int i = 0; i < table->getHeader().getNumColumns(true); ++i) {
                    const int id = table->getHeader().getColumnIdOfIndex(i, true);
                    if (auto* cell = table->getCellComponent(id, 0)) {
                        const auto actual = table->getLocalArea(cell, cell->getLocalBounds());
                        const auto expected = table->getCellPosition(id, 0, true);
                        aligned = actual.getX() == expected.getX() && actual.getWidth() == expected.getWidth() && aligned;
                    }
                }
        }
        check(aligned, "rack cells align with headings immediately at minimum and wider sizes");
        check(namedChild(*editor, "Selected channel CC1 vibrato strength") == nullptr,
            "CC1 control is absent from the main sidebar");
        auto* settings = dynamic_cast<juce::Button*>(namedChild(*editor, "Settings"));
        check(settings != nullptr, "settings button is available");
        if (settings != nullptr) settings->onClick();
        auto* vibrato = dynamic_cast<juce::ComboBox*>(namedChild(*editor, "Selected channel CC1 vibrato strength"));
        check(vibrato != nullptr && editor->getLocalBounds().contains(
            editor->getLocalArea(vibrato, vibrato->getLocalBounds())), "vibrato selector fits beside pitch controls in settings");
        if (vibrato != nullptr) {
            parameter(p, "vibratoScaleCh16", 10);
            p.getFluidSynthModel().selectChannelForEditing(15);
            check(vibrato->getSelectedId() == 10, "channel selection immediately rebinds vibrato multiplier");
            vibrato->setSelectedId(6, juce::sendNotificationSync);
            check(std::abs(findParameter(p, "vibratoScaleCh16")->convertFrom0to1(findParameter(p, "vibratoScaleCh16")->getValue()) - 6.0f) < 0.0001f
                && findParameter(p, "vibratoScaleCh1")->getValue() == 0.0f,
                "selected channel dropdown edits only that channel");
            p.getFluidSynthModel().selectChannelForEditing(0);
            check(vibrato->getSelectedId() == 1, "returning to channel 1 shows its independent unity multiplier");
        }
        auto* channelChoice = dynamic_cast<juce::ComboBox*>(namedChild(*editor, "CC1 MIDI channel"));
        if (channelChoice != nullptr && vibrato != nullptr) {
            channelChoice->setSelectedId(16, juce::sendNotificationSync);
            check(p.getFluidSynthModel().getSelectedChannel() == 15 && vibrato->getSelectedId() == 6,
                "settings channel picker selects the rack channel and its multiplier");
            p.getFluidSynthModel().setChannelControllerValue(15, 1, 5);
            channelChoice->setSelectedId(1, juce::sendNotificationSync);
            channelChoice->setSelectedId(16, juce::sendNotificationSync);
            auto* received = dynamic_cast<juce::Label*>(namedChild(*editor, "Received CC1 value"));
            check(received != nullptr && received->getText() == "5", "settings shows the actual received CC1 value");
        }
        if (argc > 1) {
            juce::FileOutputStream out{juce::File{juce::String(argv[1]) + ".settings.png"}};
            out.setPosition(0); out.truncate();
            juce::PNGImageFormat png;
            check(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), out),
                "MIDI settings preview written");
        }
        if (vibrato != nullptr)
            if (auto* popup = vibrato->findParentComponentOfClass<juce::CallOutBox>()) {
                popup->exitModalState(0); popup->setVisible(false);
            }
        auto* sizeControl = dynamic_cast<juce::Slider*>(namedChild(*editor, "Reverb size"));
        check(sizeControl != nullptr && sizeControl->getTextFromValue(0.5) == "50%"
            && std::abs(sizeControl->getValueFromText("75%") - 0.75) < 0.00001,
            "effects percentage readouts round-trip typed values correctly");
        bool diagnosticsFit{true};
        for (const char* label : {"expression", "sustain", "bend range", "pitch bend", "chorus send (cc93)"}) {
            auto* value = namedChild(*editor, "Selected channel " + juce::String(label));
            diagnosticsFit = value != nullptr && value->getHeight() >= 18
                && editor->getLocalBounds().contains(editor->getLocalArea(value, value->getLocalBounds())) && diagnosticsFit;
        }
        check(diagnosticsFit, "all diagnostic readouts fit at the minimum editor size");
        const auto reverbPreview = editor->createComponentSnapshot(editor->getLocalBounds());
        auto* chorusPage = dynamic_cast<juce::Button*>(namedChild(*editor, "Show chorus controls"));
        auto* reverbPage = dynamic_cast<juce::Button*>(namedChild(*editor, "Show reverb controls"));
        check(chorusPage != nullptr && reverbPage != nullptr, "both effects pages are available");
        if (chorusPage != nullptr && reverbPage != nullptr) {
            chorusPage->onClick();
            const auto chorusPreview = editor->createComponentSnapshot(editor->getLocalBounds());
            check(reverbPreview.isValid() && chorusPreview.isValid(), "both effects pages render at minimum size");
            reverbPage->onClick();
        }
        // Optional visual QA artifact contains only the generated test bank.
        if (argc == 2) {
            juce::Thread::sleep(100);
            juce::Timer::callPendingTimersSynchronously();
            auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
            juce::FileOutputStream out{juce::File{argv[1]}};
            out.setPosition(0);
            out.truncate();
            juce::PNGImageFormat png;
            check(png.writeImageToStream(snapshot, out), "editor QA image written");
            auto* chorus = dynamic_cast<juce::Button*>(namedChild(*editor, "Show chorus controls"));
            check(chorus != nullptr, "chorus tab is accessible by name");
            if (chorus != nullptr) {
                chorus->onClick();
                auto* waveform = namedChild(*editor, "Chorus waveform");
                check(waveform != nullptr && waveform->isVisible(), "chorus tab reveals its controls");
                auto* reverb = namedChild(*editor, "Reverb profile");
                check(reverb != nullptr && !reverb->isVisible(), "reverb controls do not overlap chorus");
                juce::FileOutputStream chorusOut{juce::File{juce::String(argv[1]) + ".chorus.png"}};
                chorusOut.setPosition(0); chorusOut.truncate();
                check(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), chorusOut),
                    "chorus editor QA image written");
            }
        }
        // Reopen after dismissal, then destroy the editor with settings still modal.
        // The controls must release their processor references synchronously.
        if (settings != nullptr) settings->onClick();
        check(namedChild(*editor, "Selected channel CC1 vibrato strength") != nullptr,
            "settings can reopen before deferred modal cleanup");
        editor.reset();
        juce::Timer::callPendingTimersSynchronously();
        check(true, "editor closes safely with MIDI settings open");
    }
    {
        JuicySFAudioProcessor p; load(p, bank);
        parameter(p, "outputLevel", 12.0f);
        parameter(p, "trimCh1", 12.0f);
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::controllerEvent(1, 7, 127), 0);
        for (int note = 36; note < 84; ++note)
            events.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), 0);
        for (int i = 0; i < 4; ++i) render(p, block, events);
        auto& model = p.getFluidSynthModel();
        check(model.consumeMasterPeak() > 1.0f && model.hasOutputOverload(),
            "master meter measures post-trim overs and latches overload");
        check(model.consumeMasterPeak() == 0.0f && model.hasOutputOverload(),
            "reading the peak does not clear overload history");
        model.clearOutputOverload();
        check(!model.hasOutputOverload(), "overload can be cleared explicitly");
        parameter(p, "resetPolicy", 1.0f);
        juce::MemoryBlock saved; p.getStateInformation(saved);
        JuicySFAudioProcessor restored; restored.prepareToPlay(48000.0, 1024);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        check(findParameter(restored, "trimCh1")->getValue() == 1.0f
            && findParameter(restored, "resetPolicy")->getValue() == 1.0f,
            "new project recalls independent trim and standard reset policy");
    }
    // Render actual CC93-routed audio: bypass, zero send, both waveforms, the
    // last channel, and trim scaling of the combined dry/chorus contribution.
    auto chorusAudio = [&](int ch, bool enabled, int send, int waveform, float trim) {
        JuicySFAudioProcessor p; load(p, bank);
        parameter(p, "chorusOn", enabled ? 1.0f : 0.0f);
        parameter(p, "chorusWaveform", static_cast<float>(waveform));
        parameter(p, "trimCh" + juce::String(ch), trim);
        juce::MidiBuffer midi;
        for (int i = 0; i < 2; ++i) render(p, block, midi);
        gm(midi); // chorus must remain enabled after a song reset
        midi.addEvent(juce::MidiMessage::controllerEvent(ch, 93, send), 0);
        midi.addEvent(juce::MidiMessage::noteOn(ch, 60, static_cast<juce::uint8>(100)), 0);
        std::vector<float> samples;
        for (int i = 0; i < 20; ++i) {
            render(p, block, midi);
            for (int side = 0; side < 2; ++side)
                samples.insert(samples.end(), block.getReadPointer(side), block.getReadPointer(side) + 1024);
        }
        int received{-1};
        check(p.getFluidSynthModel().getControllerValue(ch - 1, 93, received) && received == send,
            "chorus controls preserve the incoming CC93 send");
        check(p.getFluidSynthModel().getChannelDiagnostics(ch - 1).chorusSend == send,
            "selected-channel diagnostics show the actual CC93 send");
        midi.addEvent(juce::MidiMessage::controllerEvent(ch, 121, 0), 0);
        render(p, block, midi);
        check(p.getFluidSynthModel().getChannelDiagnostics(ch - 1).chorusSend == send,
            "CC121 retains chorus-send diagnostics, matching the engine");
        return samples;
    };
    auto difference = [](const std::vector<float>& a, const std::vector<float>& b, double scale = 1.0) {
        double sum{0};
        for (size_t i = 0; i < a.size(); ++i) {
            const double delta = static_cast<double>(a[i]) - static_cast<double>(b[i]) * scale;
            sum += delta * delta;
        }
        return sum;
    };
    for (int ch : {1, 16}) {
        const auto dry = chorusAudio(ch, false, 127, 0, 0.0f);
        const auto zeroSend = chorusAudio(ch, true, 0, 0, 0.0f);
        check(difference(dry, zeroSend) < 1.0e-8, "zero CC93 preserves dry audio when chorus is enabled");
        const auto sine = chorusAudio(ch, true, 127, 0, 0.0f);
        const auto triangle = chorusAudio(ch, true, 127, 1, 0.0f);
        check(difference(dry, sine) > 0.001, "enabled chorus produces an audible CC93-routed contribution");
        check(difference(sine, triangle) > 0.001, "sine and triangle produce different modulation audio");
        const auto trimmed = chorusAudio(ch, true, 127, 0, -12.0f);
        check(difference(trimmed, sine, std::pow(10.0, -12.0 / 20.0)) < 1.0e-8,
            "channel trim includes chorus with no untrimmed wet leakage");
    }
    {
        JuicySFAudioProcessor p; load(p, bank);
        const float values[]{1.0f, 8.0f, 0.9f, 4.0f, 21.0f, 1.0f};
        for (int i = 0; i < FluidSynthModel::numChorusParams; ++i)
            parameter(p, FluidSynthModel::chorusParamIds[i], values[i]);
        juce::MidiBuffer midi;
        for (int i = 0; i < 3; ++i) render(p, block, midi);
        juce::MemoryBlock saved; p.getStateInformation(saved);
        JuicySFAudioProcessor restored; restored.prepareToPlay(48000.0, 1024);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        gm(midi); render(restored, block, midi);
        for (int i = 0; i < 3; ++i) render(restored, block, midi);
        auto checkSettings = [&](const char* message) {
            bool match{true};
            for (int group = 0; group < 16; ++group)
                for (int i = FluidSynthModel::chorusVoices; i < FluidSynthModel::numChorusParams; ++i) {
                    double actual{-1};
                    match = restored.getFluidSynthModel().getChorusSetting(i, group, actual)
                        && std::abs(actual - values[i]) < 0.0001 && match;
                }
            check(match, message);
        };
        checkSettings("chorus settings survive project recall and reset on all 16 effects groups");
        restored.prepareToPlay(96000.0, 1024);
        checkSettings("chorus settings and safe maximum depth survive 96 kHz synth rebuild");
        auto old = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
        old->setAttribute("stateVersion", 7);
        for (const auto& id : FluidSynthModel::chorusParamIds)
            old->getChildByName("params")->removeAttribute(id);
        juce::MemoryBlock legacy; juce::AudioProcessor::copyXmlToBinary(*old, legacy);
        restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
        bool defaults{true};
        for (const auto& id : FluidSynthModel::chorusParamIds) {
            auto* control = findParameter(restored, id);
            defaults = std::abs(control->getValue() - control->getDefaultValue()) < 0.00001f && defaults;
        }
        check(defaults, "schema 7 loaded into used instance resets chorus to its off defaults");
    }

    // Audio validation of the actual CC1 pitch-depth mapping, including held notes.
    auto vibratoAudio = [&](const juce::File& font, int ch, int scale, int cc1,
                            int pressure = 0, bool changeHeld = false, bool reset = false, bool boostOther = false) {
        JuicySFAudioProcessor p; load(p, font);
        parameter(p, "vibratoScaleCh" + juce::String(ch), static_cast<float>(changeHeld ? 1 : scale));
        if (boostOther) parameter(p, "vibratoScaleCh" + juce::String(ch == 1 ? 16 : 1), 24.0f);
        juce::MidiBuffer midi;
        if (reset) gm(midi);
        midi.addEvent(juce::MidiMessage::controllerEvent(ch, 1, cc1), 0);
        midi.addEvent(juce::MidiMessage::channelPressureChange(ch, pressure), 0);
        midi.addEvent(juce::MidiMessage::noteOn(ch, 60, static_cast<juce::uint8>(100)), 0);
        std::vector<float> samples;
        for (int i = 0; i < 96; ++i) {
            if (i == 24 && changeHeld) parameter(p, "vibratoScaleCh" + juce::String(ch), static_cast<float>(scale));
            render(p, block, midi);
            if (i >= 48) samples.insert(samples.end(), block.getReadPointer(0), block.getReadPointer(0) + block.getNumSamples());
        }
        int received{-1};
        check(p.getFluidSynthModel().getControllerValue(ch - 1, 1, received) && received == cc1,
            "vibrato strength leaves the original CC1 value intact");
        check(p.getFluidSynthModel().getChannelDiagnostics(ch - 1).modulation == cc1,
            "live CC1 diagnostics match the rendered channel controller");
        midi.addEvent(juce::MidiMessage::controllerEvent(ch, 121, 0), 0);
        render(p, block, midi);
        check(p.getFluidSynthModel().getChannelDiagnostics(ch - 1).modulation == 0,
            "CC121 clears the live modulation readout");
        return samples;
    };
    auto excursion = [](const std::vector<float>& samples) {
        double last{-1}, low{1.0e9}, high{-1.0e9};
        for (size_t i = 1; i < samples.size(); ++i)
            if (samples[i - 1] <= 0 && samples[i] > 0) {
                const double crossing = static_cast<double>(i - 1)
                    - samples[i - 1] / static_cast<double>(samples[i] - samples[i - 1]);
                if (last >= 0) {
                    const double cents = 1200.0 * std::log2(48000.0 / (crossing - last) / 441.0);
                    low = std::min(low, cents); high = std::max(high, cents);
                }
                last = crossing;
            }
        return (high - low) / 2.0;
    };
    for (int ch : {1, 16}) {
        const auto base = vibratoAudio(bank, ch, 1, 8);
        const auto boosted = vibratoAudio(bank, ch, 10, 8);
        const auto equivalent = vibratoAudio(bank, ch, 1, 80);
        std::printf("Vibrato channel %d: unity %.3f cents, x10 %.3f cents\n", ch, excursion(base), excursion(boosted));
        // At 3 cents, cycle-length estimation includes sub-cent interpolation
        // and the engine's 64-sample LFO stepping. Also compare exact audio below.
        check(std::abs(excursion(base) - 3.125) < 1.0 && std::abs(excursion(boosted) - 31.25) < 1.0,
            "rendered pitch excursion matches the expected small-CC1 depth on channels 1 and 16");
        check(difference(boosted, equivalent) < 1.0e-8, "x10 CC1=8 equals native CC1=80 audio");
        check(difference(base, vibratoAudio(bank, ch, 1, 8, 0, false, false, true)) < 1.0e-8,
            "boosting another channel leaves this channel's audio unchanged");
        check(std::abs(excursion(boosted) - excursion(vibratoAudio(bank, ch, 10, 8, 0, true))) < 0.1,
            "changing strength updates already held notes");
        check(difference(boosted, vibratoAudio(bank, ch, 10, 8, 0, false, true)) < 1.0e-8,
            "vibrato strength survives a GM reset");
    }
#if JUCE_MAC
    const juce::File systemDls{"/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls"};
    check(systemDls.existsAsFile(), "macOS DLS bank is available for native-loader vibrato validation");
    if (systemDls.existsAsFile()) {
        const auto baselineDls = vibratoAudio(systemDls, 1, 1, 8);
        const auto boostedDls = vibratoAudio(systemDls, 1, 10, 8);
        check(difference(boostedDls, baselineDls) > 1.0e-6,
            "CC1 strength changes audible native DLS vibrato");
        check(difference(boostedDls, vibratoAudio(systemDls, 1, 1, 80)) < 1.0e-8,
            "native DLS x10 CC1=8 matches its native CC1=80 audio");
    }
#endif
    const double highDepth = excursion(vibratoAudio(bank, 1, 10, 32));
    const double lowDepth = excursion(vibratoAudio(bank, 1, 10, 16));
    const double nativeDepth = excursion(vibratoAudio(bank, 1, 1, 32));
    std::printf("DEPTH native32=%.6f x10_16=%.6f x10_32=%.6f ratio=%.6f\n", nativeDepth, lowDepth, highDepth, highDepth / nativeDepth);
    check(std::abs(highDepth / nativeDepth - 10.0) < 0.5,
        "measured pitch excursion grows tenfold within 5% cycle-estimator tolerance");
    check(std::abs(highDepth / lowDepth - 2.0) < 0.03,
        "CC1 values above 12 retain proportional depth at x10 without 127 saturation");
    check(difference(vibratoAudio(bank, 1, 1, 0, 80), vibratoAudio(bank, 1, 24, 0, 80)) < 1.0e-8,
        "CC1 strength leaves channel-pressure vibrato unchanged");
    juce::TemporaryFile customFile{".sf2"};
    auto customBank = [&](const std::vector<SyntheticSf2::ModSpec>& mods) {
        const auto bytes = SyntheticSf2::build({{0, 0, 441.0, "Custom modulation"}}, mods);
        check(customFile.getFile().replaceWithData(bytes.getData(), bytes.getSize()), "custom modulation fixture written");
        return customFile.getFile();
    };
    auto font = customBank({{129, 6, 25}});
    check(difference(vibratoAudio(font, 1, 10, 8), vibratoAudio(bank, 1, 5, 8)) < 1.0e-8,
        "bank-defined CC1 pitch amount is scaled instead of replaced");
    font = customBank({{129, 6, 0}, {0, 6, 50, 129}});
    check(difference(vibratoAudio(font, 1, 10, 8), vibratoAudio(bank, 1, 10, 8)) < 1.0e-8,
        "CC1 as a secondary modulator source is scaled exactly once");
    font = customBank({{129, 6, 0}, {129, 8, -1200}});
    check(difference(vibratoAudio(font, 1, 1, 80), vibratoAudio(font, 1, 24, 80)) < 1.0e-8,
        "bank zero-depth override and CC1 filter routing remain unchanged");
    {
        JuicySFAudioProcessor p; load(p, bank);
        for (int ch = 1; ch <= 16; ++ch) parameter(p, "vibratoScaleCh" + juce::String(ch), static_cast<float>(ch));
        juce::MemoryBlock saved; p.getStateInformation(saved);
        JuicySFAudioProcessor restored; restored.prepareToPlay(48000.0, 1024);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        bool match{true};
        for (int ch = 1; ch <= 16; ++ch) {
            auto* control = findParameter(restored, "vibratoScaleCh" + juce::String(ch));
            match = std::abs(control->convertFrom0to1(control->getValue()) - static_cast<float>(ch)) < 0.0001f && match;
        }
        check(match, "all sixteen independent vibrato strengths save and restore immediately");
        auto recalledDepth = [&](double sampleRate) {
            restored.prepareToPlay(sampleRate, 1024);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(16, 120, 0), 0);
            midi.addEvent(juce::MidiMessage::controllerEvent(16, 121, 0), 0);
            midi.addEvent(juce::MidiMessage::controllerEvent(16, 1, 8), 0);
            midi.addEvent(juce::MidiMessage::noteOn(16, 60, static_cast<juce::uint8>(100)), 0);
            std::vector<float> samples;
            for (int i = 0; i < 96; ++i) {
                render(restored, block, midi);
                if (i >= 48) samples.insert(samples.end(), block.getReadPointer(0), block.getReadPointer(0) + block.getNumSamples());
            }
            // Excursion is a difference in cents, independent of sample-rate offset.
            return excursion(samples);
        };
        check(std::abs(recalledDepth(48000.0) - 50.0) < 1.5,
            "recalled channel 16 strength reaches the engine and survives CC121");
        check(std::abs(recalledDepth(96000.0) - 50.0) < 1.5,
            "vibrato strength survives a 96 kHz synth rebuild");
        auto old = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
        old->setAttribute("stateVersion", 8);
        for (int ch = 1; ch <= 16; ++ch) old->getChildByName("params")->removeAttribute("vibratoScaleCh" + juce::String(ch));
        juce::MemoryBlock legacy; juce::AudioProcessor::copyXmlToBinary(*old, legacy);
        restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
        match = true;
        for (int ch = 1; ch <= 16; ++ch) match = findParameter(restored, "vibratoScaleCh" + juce::String(ch))->getValue() == 0.0f && match;
        check(match, "schema 8 resets vibrato to unity even in a previously used instance");
    }

    // Characterization, not a claim of exact onset: the stock engine buffers 64
    // samples. This test keeps that known limitation explicit and bounded.
    int reference{-1}; bool bounded{true};
    for (int offset : {0, 1, 17, 63, 64, 65, 127, 128, 401}) {
        JuicySFAudioProcessor p; load(p, bank);
        juce::MidiBuffer empty; render(p, block, empty);
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), offset);
        render(p, block, events);
        const int onset = firstSound(block);
        if (offset == 0) reference = onset;
        const int extra = onset - reference - offset;
        std::printf("TIMING requested=%d onset=%d additionalDelay=%d samples\n", offset, onset, extra);
        bounded = bounded && onset >= 0 && extra >= 0 && extra <= 63;
    }
    check(bounded, "measured engine onset quantization stays within its documented 63-sample bound");
    check(logger.assertionCount.load() == 0, "processing, editor construction and painting produce no JUCE assertions");
    juce::Logger::setCurrentLogger(nullptr);
    return failures == 0 ? 0 : 1;
}
