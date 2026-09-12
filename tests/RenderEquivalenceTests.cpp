#include "PluginProcessor.h"
#include "SyntheticSf2.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Optional argument writes raw audio plus field-by-field diagnostics for a
// byte-for-byte comparison between two source revisions on the same platform.
// No golden audio is tied to a compiler or FluidSynth version in the repository.
namespace {
bool setParameter(JuicySFAudioProcessor& p, const juce::String& id, float value) {
    for (auto* candidate : p.getParameters())
        if (auto* control = dynamic_cast<juce::RangedAudioParameter*>(candidate))
            if (control->paramID == id) {
                control->setValueNotifyingHost(control->convertTo0to1(value));
                return true;
            }
    return false;
}

bool prepare(JuicySFAudioProcessor& p, const juce::File& bank, double rate,
             int capacity, int effects, int policy) {
    p.prepareToPlay(rate, capacity);
    juce::XmlElement state{"MYPLUGINSETTINGS"};
    state.setAttribute("stateVersion", 6);
    state.createNewChildElement("soundFont")->setAttribute("path", bank.getFullPathName());
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(state, bytes);
    p.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    return p.getFluidSynthModel().getFontLoadStatus() == "loaded"
        && setParameter(p, "reverbOn", static_cast<float>(effects & 1))
        && setParameter(p, "chorusOn", static_cast<float>((effects >> 1) & 1))
        && setParameter(p, "resetPolicy", static_cast<float>(policy));
}

void eventsFor(juce::MidiBuffer& midi, int block, int samples) {
    if (block >= 80 || samples == 0) return;
    if (block == 0 || block == 40) {
        const juce::uint8 reset[]{0x7e, 0x7f, 9, 1};
        midi.addEvent(juce::MidiMessage::createSysExMessage(reset, 4), 0);
    }
    for (int ch = 1; ch <= 16; ++ch) {
        const int at = (ch * 7 + block * 13) % samples;
        if (block == 0 || block == 40) {
            midi.addEvent(juce::MidiMessage::controllerEvent(ch, 0, 0), 0);
            midi.addEvent(juce::MidiMessage::programChange(ch, ch == 10 ? 0 : ch % 2), 0);
            for (int cc : {101, 100, 6, 38, 91, 93})
                midi.addEvent(juce::MidiMessage::controllerEvent(ch, cc,
                    cc == 6 ? 12 : cc == 38 ? 25 : cc >= 91 && cc <= 93 ? 100 : 0), 0);
            midi.addEvent(juce::MidiMessage::noteOn(ch, 48 + ch, juce::uint8(90)), 0);
        }
        if (block == 79) {
            midi.addEvent(juce::MidiMessage::controllerEvent(ch, 64, 0), at);
            midi.addEvent(juce::MidiMessage::allNotesOff(ch), at);
        } else {
            const int controllers[]{1, 7, 10, 11, 64, 74, 91, 93};
            midi.addEvent(juce::MidiMessage::controllerEvent(ch, controllers[block % 8],
                (block * 11 + ch * 3) % 128), at);
            midi.addEvent(juce::MidiMessage::pitchWheel(ch, (block * 113 + ch * 509) % 16384), at);
            midi.addEvent(juce::MidiMessage::channelPressureChange(ch, (block + ch) % 128), at);
            midi.addEvent(juce::MidiMessage::aftertouchChange(ch, 48 + ch, (block + ch) % 128), at);
        }
    }
    // Force one-sample render segments after previously writing long blocks.
    if (block % 9 == 1)
        for (int at = 0; at < samples; ++at)
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, at % 128), at);
    // Exercise bounded timestamp scratch and its host-order overflow fallback.
    if (block == 22)
        for (int i = 0; i < 2050; ++i)
            midi.addEvent(juce::MidiMessage::controllerEvent(i % 16 + 1, 74, i % 128), 0);
}

void diagnostics(FluidSynthModel& m, juce::MemoryOutputStream& out) {
    for (int ch = 0; ch < 16; ++ch) {
        const auto d = m.getChannelDiagnostics(ch);
        out.writeInt64(static_cast<juce::int64>(d.midiEvents));
        out.writeFloat(d.peak);
        for (int v : {d.expression, d.bendRange, d.pitchBend, d.sustain,
                      d.chorusSend, d.modulation, d.soundingBank, d.soundingPreset})
            out.writeInt(v);
    }
    out.writeFloat(m.consumeMasterPeak());
    out.writeBool(m.hasOutputOverload());
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI initialiser;
    if (argc > 2) return 2;
    juce::TemporaryFile bank{".sf2"};
    const auto fixture = SyntheticSf2::build({{0, 0, 441.0, "First"},
        {0, 1, 882.0, "Second"}, {128, 0, 220.5, "Drums"}});
    if (!bank.getFile().replaceWithData(fixture.getData(), fixture.getSize())) return 2;
    std::unique_ptr<juce::FileOutputStream> snapshot;
    if (argc == 2) {
        snapshot = juce::File{juce::String{argv[1]}}.createOutputStream();
        if (snapshot == nullptr || !snapshot->setPosition(0) || snapshot->truncate().failed()) return 2;
    }
    int failures{0}, cases{0};
    for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0})
        for (int channels : {1, 2})
            for (int effects : {0, 1, 2, 3})
                for (int policy : {0, 1}) {
                    JuicySFAudioProcessor a, b;
                    bool ok = prepare(a, bank.getFile(), rate, 1024, effects, policy);
                    ok = prepare(b, bank.getFile(), rate, 4096, effects, policy) && ok;
                    double energy{0};
                    for (int block = 0; block < 180; ++block) {
                        const int sizes[]{1024, 1, 7, 63, 64, 65, 127, 512, 0};
                        const int size = block < 80 ? sizes[block % 9] : 1024;
                        if (block % 13 == 0 && block < 80) {
                            const juce::String id = "trimCh" + juce::String(block % 16 + 1);
                            const float gainDb = block % 26 == 0 ? -12.0f : 3.0f;
                            ok = setParameter(a, id, gainDb) && ok;
                            ok = setParameter(b, id, gainDb) && ok;
                            ok = setParameter(a, "outputLevel", gainDb) && ok;
                            ok = setParameter(b, "outputLevel", gainDb) && ok;
                        }
                        juce::MidiBuffer ma, mb;
                        eventsFor(ma, block, size); mb = ma;
                        juce::AudioBuffer<float> aa{channels, size}, ab{channels, size};
                        a.processBlock(aa, ma); b.processBlock(ab, mb);
                        for (int ch = 0; ch < channels; ++ch) {
                            const auto bytes = static_cast<size_t>(size) * sizeof(float);
                            if (size > 0) {
                                ok = std::memcmp(aa.getReadPointer(ch), ab.getReadPointer(ch), bytes) == 0 && ok;
                                if (snapshot) ok = snapshot->write(aa.getReadPointer(ch), bytes) && ok;
                            }
                            for (int i = 0; i < size; ++i) {
                                const double sample = aa.getSample(ch, i);
                                ok = std::isfinite(sample) && ok;
                                energy += sample * sample;
                            }
                        }
                        juce::MemoryOutputStream da, db;
                        diagnostics(a.getFluidSynthModel(), da); diagnostics(b.getFluidSynthModel(), db);
                        ok = da.getMemoryBlock() == db.getMemoryBlock() && ok;
                        if (snapshot) ok = snapshot->write(da.getData(), da.getDataSize()) && ok;
                    }
                    ok = energy > 1.0e-8 && ok;
                    for (int ch = 0; ch < 16; ++ch)
                        ok = a.getFluidSynthModel().getChannelDiagnostics(ch).midiEvents > 0 && ok;
                    std::printf("%s rate=%.0f channels=%d effects=%d policy=%d energy=%.9g\n",
                        ok ? "PASS" : "FAIL", rate, channels, effects, policy, energy);
                    ++cases;
                    if (!ok) ++failures;
                }
    // Cached scratch pointers must survive repeated preparation, both growing
    // and shrinking storage. At native rates, also exceed the declared maximum
    // to exercise chunking; the existing high-rate overflow policy is separate.
    for (double rate : {48000.0, 192000.0}) {
        JuicySFAudioProcessor a, b;
        bool ok = prepare(a, bank.getFile(), rate, 64, 3, 0);
        ok = prepare(b, bank.getFile(), rate, 4096, 3, 0) && ok;
        double energy{0};
        for (int capacity : {64, 2048, 128, 1024, 64}) {
            a.prepareToPlay(rate, capacity);
            b.prepareToPlay(rate, 4096);
            for (int size : {capacity, 1, rate <= 96000.0 ? capacity + 127 : capacity}) {
                juce::MidiBuffer ma, mb;
                eventsFor(ma, 0, size); mb = ma;
                juce::AudioBuffer<float> aa{2, size}, ab{2, size};
                a.processBlock(aa, ma); b.processBlock(ab, mb);
                for (int ch = 0; ch < 2; ++ch) {
                    ok = std::memcmp(aa.getReadPointer(ch), ab.getReadPointer(ch),
                        static_cast<size_t>(size) * sizeof(float)) == 0 && ok;
                    for (int i = 0; i < size; ++i) {
                        const double sample = aa.getSample(ch, i);
                        ok = std::isfinite(sample) && ok;
                        energy += sample * sample;
                    }
                }
            }
        }
        ok = energy > 1.0e-8 && ok;
        std::printf("%s repeated prepare and buffer bounds rate=%.0f\n", ok ? "PASS" : "FAIL", rate);
        ++cases;
        if (!ok) ++failures;
    }
    if (snapshot) {
        snapshot->flush();
        if (snapshot->getStatus().failed()) ++failures;
    }
    std::printf("%d cases, %d failures\n", cases, failures);
    return failures == 0 ? 0 : 1;
}
