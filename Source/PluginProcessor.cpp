#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiConstants.h"
#include "Util.h"
#include "GuiConstants.h"
#include <limits>
#include <cmath>

using namespace std;
using Parameter = AudioProcessorValueTreeState::Parameter;

AudioProcessor* JUCE_CALLTYPE createPluginFilter();


//==============================================================================
JuicySFAudioProcessor::JuicySFAudioProcessor()
: AudioProcessor{getBusesProperties()}
, valueTreeState{
    *this,
    nullptr,
    "MYPLUGINSETTINGS",
    createParameterLayout()}
, fluidSynthModel{valueTreeState}
, midiFilePlayer{*this, [this](juce::MidiBuffer& setup) {
    if (wrapperType != wrapperType_Standalone) return;
    // Setup/chase is prepared on the UI thread and applied while the callback
    // lock is held. Zero frames dispatch MIDI without synthesising old audio.
    keyboardState.reset();
    AudioBuffer<float> empty{2, 0};
    fluidSynthModel.processBlock(empty, setup, true);
    for (const auto event : setup)
        keyboardState.processNextMidiEvent(event.getMessage());
}}
{
    MemoryBlock bookmarkBuffer;
    MemoryBlock loadedBookmarkBuffer;
    valueTreeState.state.appendChild({ "uiState", {
            { "width", GuiConstants::minWidth },
            { "height", GuiConstants::defaultHeight },
            { "selectedChannel", 1 } // 1-indexed (1..16)
        }, {} }, nullptr);
    valueTreeState.state.appendChild({ "soundFont", {
        { "path", "" },
        { "bookmark", std::move(bookmarkBuffer) },
        { "loadStatus", "idle" },
        { "loadMessage", "No bank loaded." },
        { "lastAttemptedPath", "" },
        { "loadedPath", "" },
        { "loadedBookmark", std::move(loadedBookmarkBuffer) },
        { "usedDlsRepair", false },
    }, {} }, nullptr);
    valueTreeState.state.appendChild({ "banks", {}, {} }, nullptr);

    // One node per MIDI channel: instrument plus mixer controls (GM defaults).
    ValueTree channelPrograms{ "channelPrograms" };
    for (int i = 0; i < 16; i++) {
        channelPrograms.appendChild({ "ch", {
            { "num", i },
            { "bank", i == 9 ? 128 : 0 },
            { "preset", 0 },
            { "volume", MidiConstants::defaultChannelVolume },
            { "pan", MidiConstants::centreValue },
            { "mute", 0 },
            { "solo", 0 }
        }, {} }, nullptr);
    }
    valueTreeState.state.appendChild(channelPrograms, nullptr);

    // VST3 program list from bank 0; empty slots read "Program N".
    fluidSynthModel.onBanksRefreshed = [this] {
        StringArray names;
        for (int i = 0; i < 128; i++)
            names.add(String());
        ValueTree bank0{valueTreeState.state.getChildWithName("banks")
            .getChildWithProperty("num", 0)};
        for (int i = 0; i < bank0.getNumChildren(); i++) {
            ValueTree preset{bank0.getChild(i)};
            const int num{preset.getProperty("num", -1)};
            if (num >= 0 && num < 128)
                names.set(num, preset.getProperty("name").toString());
        }
        vst3Extensions.setProgramNames(names);
    };

    initialiseSynth();
}

// AudioParameterInt is not discrete, so JUCE's VST3 wrapper publishes stepCount
// 0. Cubase only routes Program Change to a kIsProgramChange parameter with
// stepCount == 127.
struct DiscreteParameterInt final : public AudioParameterInt {
    using AudioParameterInt::AudioParameterInt;
    bool isDiscrete() const override { return true; }
};

AudioProcessorValueTreeState::ParameterLayout JuicySFAudioProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto intParam = [] (const String& id, const String& name,
                              int minimum, int maximum, int defaultValue,
                              const String& label) {
        return make_unique<AudioParameterInt>(
            juce::ParameterID{id, 1}, name, minimum, maximum, defaultValue,
            juce::AudioParameterIntAttributes{}.withLabel(label));
    };

    // Shared params: the editor's selected channel.
    layout.add(
        // Spans 0-255: a drum channel's runtime bank is 128 + Bank Select MSB.
        intParam("bank", "which bank is selected in the SoundFont",
                 MidiConstants::midiMinValue, MidiConstants::maxChannelBank,
                 MidiConstants::midiMinValue, "Bank"),
        // Banks may be sparse and lack preset 0.
        intParam("preset", "which patch (program/instrument) is selected in the SoundFont", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Preset"));

    // Master trim over the whole plugin, applied after rendering.
    layout.add(make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputLevel", 1}, "master output level",
        juce::NormalisableRange<float>{GuiConstants::outputLevelMinDb,
                                       GuiConstants::outputLevelMaxDb, 0.1f},
        // +1.5 dB is the most the owner's 24-rip corpus allows without clipping
        // (loudest peak -1.61 dBFS). Closing the rest of the gap to VGMTrans needs a
        // limiter.
        GuiConstants::outputLevelDefaultDb,
        juce::AudioParameterFloatAttributes{}.withLabel("Out")));

    // Reverb: controls over FluidSynth's existing reverb. Off by default (owner
    // decision, 2026-08-23) so existing projects do not change; CC91 sends feed
    // it once enabled.
    layout.add(make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"reverbOn", 1}, "reverb enabled", false,
        juce::AudioParameterBoolAttributes{}.withLabel("Reverb")));
    layout.add(make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"reverbProfile", 1}, "reverb profile",
        FluidSynthModel::reverbProfileNames(), 0,
        juce::AudioParameterChoiceAttributes{}.withLabel("Profile")));
    {
        const auto reverbParam = [](const String& id, const String& name,
                                    float defaultValue, const String& label) {
            return make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{id, 1}, name,
                juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, defaultValue,
                juce::AudioParameterFloatAttributes{}.withLabel(label));
        };
        const auto* universal{&FluidSynthModel::reverbProfiles[0]};
        // Width is narrowed from FluidSynth's 0-100 to 0-1, where all useful values lie.
        layout.add(
            reverbParam("reverbSize", "reverb room size",
                        universal->values[FluidSynthModel::reverbSize], "Size"),
            reverbParam("reverbDamp", "reverb damping",
                        universal->values[FluidSynthModel::reverbDamp], "Damp"),
            reverbParam("reverbWidth", "reverb stereo width",
                        universal->values[FluidSynthModel::reverbWidth], "Width"),
            reverbParam("reverbLevel", "reverb level",
                        universal->values[FluidSynthModel::reverbLevel], "Level"));
    }

    // Per-channel volume (CC7), pan (CC10), mute and solo, automatable per channel.
    // Deliberately ungrouped: a group would publish an 18th VST3 unit that the
    // wrapper's fixed 17-unit structure (cached by Cubase) never declared.
    // Incoming CC7/CC10 overwrite them at the event's timestamp.
    for (int ch = 1; ch <= 16; ++ch)
        layout.add(intParam(
            "volCh" + String(ch), "volume (CC7) for MIDI channel " + String(ch),
            MidiConstants::midiMinValue, MidiConstants::midiMaxValue,
            MidiConstants::defaultChannelVolume,
            "Ch" + String(ch) + " Vol"));
    for (int ch = 1; ch <= 16; ++ch)
        layout.add(intParam(
            "panCh" + String(ch), "pan (CC10) for MIDI channel " + String(ch),
            MidiConstants::midiMinValue, MidiConstants::midiMaxValue,
            MidiConstants::centreValue,
            "Ch" + String(ch) + " Pan"));
    // Mute and solo are plugin-side: they drop note-ons and leave the file's CC7 alone.
    for (int ch = 1; ch <= 16; ++ch)
        layout.add(make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"muteCh" + String(ch), 1},
            "mute MIDI channel " + String(ch), false,
            juce::AudioParameterBoolAttributes{}.withLabel(
                "Ch" + String(ch) + " Mute")));
    for (int ch = 1; ch <= 16; ++ch)
        layout.add(make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"soloCh" + String(ch), 1},
            "solo MIDI channel " + String(ch), false,
            juce::AudioParameterBoolAttributes{}.withLabel(
                "Ch" + String(ch) + " Solo")));

    // progCh1..progCh16, each in group "chUnitN". JUCE derives the VST3 unitId
    // from the group, which the wrapper's IUnitInfo mirrors so Cubase maps MIDI
    // channel N to unit N.
    for (int ch = 1; ch <= 16; ++ch) {
        layout.add(make_unique<juce::AudioProcessorParameterGroup>(
            "chUnit" + String(ch),
            "Ch " + String(ch),
            "|",
            make_unique<DiscreteParameterInt>(
                juce::ParameterID{"progCh" + String(ch), 1},
                "program for MIDI channel " + String(ch),
                MidiConstants::midiMinValue, MidiConstants::midiMaxValue,
                MidiConstants::midiMinValue,
                juce::AudioParameterIntAttributes{}.withLabel("Ch" + String(ch) + " Prog"))));
    }

    // Host bend compensation, appended after the frozen manifest; both default off.
    // The override forces one range for hosts that drop the RPN; the scale undoes
    // FL Studio's +-2 semitone bend import.
    layout.add(intParam("bendRange", "pitch-bend range override (0 follows the MIDI file)",
                        0, 24, 0, "Bend Rng"));
    layout.add(intParam("bendScale", "pitch-bend scale", 1, 24, 1, "Bend x"));

    layout.add(make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"resetPolicy", 2}, "MIDI reset policy",
        juce::StringArray{"DAW recovery", "Standard MIDI"}, 0));
    for (int ch = 1; ch <= 16; ++ch)
        layout.add(make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"trimCh" + String(ch), 2},
            "independent trim for MIDI channel " + String(ch),
            juce::NormalisableRange<float>{-24.0f, 12.0f, 0.1f}, 0.0f,
            juce::AudioParameterFloatAttributes{}.withLabel("dB")));
    layout.add(make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"chorusOn", 3}, "Chorus enabled", false));
    layout.add(make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"chorusVoices", 3}, "Chorus voices", 1, 8, 3));
    layout.add(make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"chorusLevel", 3}, "Chorus level",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.6f));
    layout.add(make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"chorusRate", 3}, "Chorus rate",
        juce::NormalisableRange<float>{0.1f, 5.0f, 0.01f}, 0.2f,
        juce::AudioParameterFloatAttributes{}.withLabel("Hz")));
    layout.add(make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"chorusDepth", 3}, "Chorus depth",
        juce::NormalisableRange<float>{0.0f, 21.0f, 0.01f}, 4.25f,
        juce::AudioParameterFloatAttributes{}.withLabel("ms")));
    layout.add(make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"chorusWaveform", 3}, "Chorus waveform",
        juce::StringArray{"Sine", "Triangle"}, 0));
    for (int ch = 1; ch <= 16; ++ch)
        layout.add(make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"vibratoScaleCh" + String(ch), 4},
            "CC1 vibrato strength for MIDI channel " + String(ch), 1, 24, 1,
            juce::AudioParameterIntAttributes{}.withLabel("x")));
    // Choice order is frozen (hosts store the index). Linear is the default.
    layout.add(make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"interpolation", 5}, "Sample interpolation",
        juce::StringArray{"7th-order", "Linear", "None"}, 1));
    // CC1 vibrato for all channels; the per-channel strengths above multiply it.
    layout.add(make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"cc1VibratoScale", 6}, "CC1 vibrato strength", 1, 64, 1,
        juce::AudioParameterIntAttributes{}.withLabel("x")));
    // Choice order is frozen (hosts store the index).
    layout.add(make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"cc1VibratoRate", 6}, "CC1 vibrato rate",
        juce::StringArray{"Bank", "x1.5", "x2", "x2.4", "x3", "x4"}, 0));
    return layout;
}

JuicySFAudioProcessor::~JuicySFAudioProcessor()
{
}

void JuicySFAudioProcessor::initialiseSynth() {
    fluidSynthModel.initialise();
}

//==============================================================================
const String JuicySFAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool JuicySFAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool JuicySFAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

double JuicySFAudioProcessor::getTailLengthSeconds() const
{
    // Looped voices, long releases and reverb can ring indefinitely.
    return std::numeric_limits<double>::infinity();
}

int JuicySFAudioProcessor::getNumPrograms()
{
    // Exactly one program: with a program list, hosts consume MIDI Program Change
    // themselves instead of passing it to processBlock.
    return 1;
}

int JuicySFAudioProcessor::getCurrentProgram()
{
    return 0;
}

void JuicySFAudioProcessor::setCurrentProgram(int /*index*/)
{
    // Programs are per MIDI channel, not a host program list.
}

const String JuicySFAudioProcessor::getProgramName(int /*index*/)
{
    return {};
}

void JuicySFAudioProcessor::changeProgramName (int, const String&)
{
}

//==============================================================================
void JuicySFAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (wrapperType == wrapperType_Standalone) {
        midiFilePlayer.pause();
        midiFilePlayer.prepare(samplesPerBlock, sampleRate);
    }
    keyboardState.reset();
    fluidSynthModel.prepareToPlay(sampleRate, samplesPerBlock);

    reset();
}

void JuicySFAudioProcessor::releaseResources()
{
    if (wrapperType == wrapperType_Standalone) midiFilePlayer.pause();
    keyboardState.reset();
}

bool JuicySFAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo; input must match output or be disabled.
    const AudioChannelSet& mainOutput = layouts.getMainOutputChannelSet();
    const AudioChannelSet& mainInput  = layouts.getMainInputChannelSet();

    if (! mainInput.isDisabled() && mainInput != mainOutput)
        return false;

    if (mainOutput.isDisabled())
        return false;

    return mainOutput.size() <= 2;
}

AudioProcessor::BusesProperties JuicySFAudioProcessor::getBusesProperties() {
    return BusesProperties()
            .withOutput ("Output", AudioChannelSet::stereo(), true);
}

void JuicySFAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    jassert (!isUsingDoublePrecision());

    // Clear outputs that have no matching input.
    for (int i = getTotalNumInputChannels();
         i < juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels()); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (wrapperType == wrapperType_Standalone) {
        auto& events = midiFilePlayer.getRenderBuffer();
        events.clear();
        events.addEvents(midiMessages, 0, -1, 0);
        if (fluidSynthModel.isSampleRateSupported())
            midiFilePlayer.appendNextBlock(events, buffer.getNumSamples(), midiFilePlayer.getPreparedSampleRate());
        keyboardState.processNextMidiBuffer(events, 0, buffer.getNumSamples(), true);
        fluidSynthModel.processBlock(buffer, events, midiFilePlayer.hasSong());
    } else {
        // Preserve the existing host MIDI and program-parameter routes.
        keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);
        fluidSynthModel.processBlock(buffer, midiMessages);
    }


    // No MIDI output (the VST3 wrapper asserts otherwise).
    midiMessages.clear();
}

//==============================================================================
bool JuicySFAudioProcessor::hasEditor() const
{
    return true;
}

AudioProcessorEditor* JuicySFAudioProcessor::createEditor()
{
    return new JuicySFAudioProcessorEditor (*this, valueTreeState);
}

//==============================================================================
void JuicySFAudioProcessor::getStateInformation (MemoryBlock& destData)
{

    XmlElement xml{"MYPLUGINSETTINGS"};
    // Schema history: v12 global CC1 vibrato strength and rate; v11 saved accent; v10 interpolation; v9 vibrato strength; v8 chorus; v7 reset
    // policy, trims and remembered controllers; v6 reverb; v5 per-channel mixer
    // parameters; v4 bank spans 0-255; v3 volume/pan replaced CC71-79 (v1-v2).
    xml.setAttribute("stateVersion", currentStateVersion);

    XmlElement* params{xml.createNewChildElement("params")};
    for (auto* param : getParameters()) {
         if (auto* p = dynamic_cast<AudioProcessorParameterWithID*> (param)) {
             float value = p->getValue();
             int channelIndex = FluidSynthModel::progParamChannel(p->paramID);
             if (p->paramID.startsWith("volCh") || p->paramID.startsWith("panCh")) {
                 channelIndex = p->paramID.substring(5).getIntValue() - 1;
                 if (channelIndex >= 0 && channelIndex < 16)
                     value = static_cast<float>(fluidSynthModel.savedMixerValue(
                         channelIndex, p->paramID.startsWith("volCh") ? 0 : 1)) / 127.0f;
             } else if (channelIndex >= 0 || p->paramID == "bank" || p->paramID == "preset") {
                 if (channelIndex < 0) channelIndex = fluidSynthModel.getSelectedChannel();
                 int bank{0}, program{0};
                 if (fluidSynthModel.getAppliedChannelProgram(channelIndex, bank, program))
                     value = p->paramID == "bank" ? static_cast<float>(bank) / 255.0f
                         : static_cast<float>(program) / 127.0f;
             }
             params->setAttribute(p->paramID, value);
         }
    }
    {
        ValueTree tree{valueTreeState.state.getChildWithName("uiState")};
        XmlElement* newElement{xml.createNewChildElement("uiState")};
        {
            double value{tree.getProperty("width", GuiConstants::minWidth)};
            newElement->setAttribute("width", value);
        }
        {
            double value{tree.getProperty("height", GuiConstants::defaultHeight)};
            newElement->setAttribute("height", value);
        }
        {
            int value{tree.getProperty("selectedChannel", 1)};
            newElement->setAttribute("selectedChannel", value);
        }
        newElement->setAttribute("accent", Juicy16::accentName(Juicy16::accentFromName(
            tree.getProperty("accent", "sage").toString())));
    }
    {
        // Per-channel instrument and mixer state.
        ValueTree tree{valueTreeState.state.getChildWithName("channelPrograms")};
        XmlElement* channelProgramsElement{xml.createNewChildElement("channelPrograms")};
        for (int i = 0; i < tree.getNumChildren(); i++) {
            ValueTree ch{tree.getChild(i)};
            XmlElement* chElement{channelProgramsElement->createNewChildElement("ch")};
            chElement->setAttribute("num", static_cast<int>(ch.getProperty("num", i)));
            int liveBank{0}, liveProgram{0};
            const bool live = fluidSynthModel.getAppliedChannelProgram(i, liveBank, liveProgram);
            chElement->setAttribute("expression", fluidSynthModel.rememberedExpression(i));
            chElement->setAttribute("bendRange", fluidSynthModel.rememberedBendRange(i));
            // One schema list for writer and reader.
            for (const String& p : FluidSynthModel::perChannelParams) {
                chElement->setAttribute(
                    p, p == "bank" && live ? liveBank
                       : p == "preset" && live ? liveProgram
                       : p == "volume" ? fluidSynthModel.savedMixerValue(i, 0)
                       : p == "pan" ? fluidSynthModel.savedMixerValue(i, 1)
                       : static_cast<int>(ch.getProperty(p, FluidSynthModel::defaultParamValue(p))));
            }
        }
    }
    {
        ValueTree tree{valueTreeState.state.getChildWithName("soundFont")};
        XmlElement* newElement{xml.createNewChildElement("soundFont")};
        {
            String value = tree.getProperty("path", "");
            newElement->setAttribute("path", value);
        }
        {
            MemoryBlock buffer;
            var value = tree.getProperty("bookmark", buffer);
            jassert(value.isBinaryData());
            newElement->setAttribute("bookmark", value.getBinaryData()->toBase64Encoding());
        }
    }
    
#if JUICYSF_TRACE_STATE
    DEBUG_PRINT(xml.toString());
#endif
    
    copyXmlToBinary(xml, destData);
}

void JuicySFAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    shared_ptr<XmlElement> xmlState{getXmlFromBinary(data, sizeInBytes)};

    if (xmlState.get() != nullptr) {
#if JUICYSF_TRACE_STATE
        DEBUG_PRINT(xmlState->toString());
#endif
        if (xmlState->hasTagName(valueTreeState.state.getType())) {
            const int stateVersion{xmlState->getIntAttribute("stateVersion", 1)};
            if (stateVersion > currentStateVersion) {
                ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
                fontState.setProperty("loadStatus", "error", nullptr);
                fontState.setProperty(
                    "loadMessage",
                    "This project uses Juicy16 state version " + String(stateVersion)
                        + "; this build supports up to version "
                        + String(currentStateVersion) + ". No newer state was applied.",
                    nullptr);
                return;
            }
            // v1/v2 stored CC71-79 values, which have no mixer equivalent; those channels
            // keep the GM defaults.
            const bool restoreMixer{stateVersion >= 3};
            fluidSynthModel.discardPendingStateUpdates();
            for (int ch = 0; ch < 16; ++ch)
                fluidSynthModel.restoreRememberedControllers(ch, -1, -1);
            if (stateVersion < 5) {
                auto channels = valueTreeState.state.getChildWithName("channelPrograms");
                for (auto channel : channels) {
                    channel.setProperty("mute", 0, nullptr);
                    channel.setProperty("solo", 0, nullptr);
                    if (!restoreMixer) {
                        channel.setProperty("volume", MidiConstants::defaultChannelVolume, nullptr);
                        channel.setProperty("pan", MidiConstants::centreValue, nullptr);
                    }
                }
            }
            if (stateVersion < 6) {
                for (const String& id : {String{"reverbOn"}, String{"reverbProfile"},
                        FluidSynthModel::reverbParamId(0), FluidSynthModel::reverbParamId(1),
                        FluidSynthModel::reverbParamId(2), FluidSynthModel::reverbParamId(3)}) {
                    auto* control = valueTreeState.getParameter(id);
                    control->setValueNotifyingHost(control->getDefaultValue());
                }
            }
            if (stateVersion < 7) {
                valueTreeState.getParameter("resetPolicy")->setValueNotifyingHost(0.0f);
                for (int ch = 1; ch <= 16; ++ch)
                    valueTreeState.getParameter("trimCh" + String(ch))->setValueNotifyingHost(2.0f / 3.0f);
            }
            // Older projects keep the 7th-order sound they were made with.
            if (stateVersion < 10)
                valueTreeState.getParameter("interpolation")->setValueNotifyingHost(0.0f);
            if (stateVersion < 12)
                for (const char* id : {"cc1VibratoScale", "cc1VibratoRate"})
                    valueTreeState.getParameter(id)->setValueNotifyingHost(0.0f);
            if (stateVersion < 9)
                for (int ch = 1; ch <= 16; ++ch)
                    valueTreeState.getParameter("vibratoScaleCh" + String(ch))->setValueNotifyingHost(0.0f);
            if (stateVersion < 8)
                for (const auto& id : FluidSynthModel::chorusParamIds) {
                    auto* control = valueTreeState.getParameter(id);
                    control->setValueNotifyingHost(control->getDefaultValue());
                }
            // Before the font, so the font load applies them.
            {
                XmlElement* channelProgramsElement{xmlState->getChildByName("channelPrograms")};
                if (channelProgramsElement) {
                    ValueTree tree{valueTreeState.state.getChildWithName("channelPrograms")};
                    for (auto* chElement : channelProgramsElement->getChildIterator()) {
                        int num{chElement->getIntAttribute("num", -1)};
                        ValueTree ch{tree.getChildWithProperty("num", num)};
                        if (ch.isValid()) {
                            for (const String& p : FluidSynthModel::perChannelParams) {
                                if (!restoreMixer && (p == "volume" || p == "pan"))
                                    continue;
                                const int maximum{p == "bank"
                                    ? MidiConstants::maxChannelBank
                                    : (p == "mute" || p == "solo")
                                        ? 1
                                        : MidiConstants::midiMaxValue};
                                const int restored{chElement->getIntAttribute(
                                    p, static_cast<int>(ch.getProperty(p, 0)))};
                                ch.setProperty(
                                    p,
                                    juce::jlimit(MidiConstants::midiMinValue, maximum, restored),
                                    nullptr);
                            }
                        }
                    }
                }
            }
            // Pre-v5 saves hold volume/pan only in channelPrograms. Derive the parameters
            // now; the loop below keeps current values for absent attributes.
            fluidSynthModel.syncMixerParamsFromState();
            {
                ValueTree tree{valueTreeState.state.getChildWithName("uiState")};
                XmlElement* xmlElement{xmlState->getChildByName("uiState")};
                tree.setProperty("accent", Juicy16::accentName(Juicy16::accentFromName(
                    xmlElement != nullptr ? xmlElement->getStringAttribute("accent", "sage")
                                          : String{"sage"})), nullptr);
                if (xmlElement) {
                    {
                        Value value{tree.getPropertyAsValue("width", nullptr)};
                        value = xmlElement->getIntAttribute("width", value.getValue());
                    }
                    {
                        Value value{tree.getPropertyAsValue("height", nullptr)};
                        value = xmlElement->getIntAttribute("height", value.getValue());
                    }
                    {
                        Value value{tree.getPropertyAsValue("selectedChannel", nullptr)};
                        value = juce::jlimit(1, 16, xmlElement->getIntAttribute("selectedChannel", 1));
                    }
                }
            }
            {
                XmlElement* xmlElement{xmlState->getChildByName("soundFont")};
                if (xmlElement) {
                    ValueTree tree{valueTreeState.state.getChildWithName("soundFont")};
                    MemoryBlock emptyBookmark;
                    const var previousBookmark{tree.getProperty("bookmark", emptyBookmark)};
                    jassert(previousBookmark.isBinaryData());
                    const String currentPath{tree.getProperty("path", "").toString()};
                    const String restoredPath{xmlElement->getStringAttribute("path", currentPath)};
                    // A path-only record selecting another bank cannot carry a
                    // bookmark for the previous bank into that selection.
                    const bool newPathWithoutBookmark{xmlElement->hasAttribute("path")
                        && restoredPath != currentPath && !xmlElement->hasAttribute("bookmark")};
                    const String encodedBookmark{!newPathWithoutBookmark && previousBookmark.isBinaryData()
                        ? previousBookmark.getBinaryData()->toBase64Encoding() : String{}};
                    MemoryBlock bookmark;
                    bookmark.fromBase64Encoding(xmlElement->getStringAttribute(
                        "bookmark", encodedBookmark));
                    fluidSynthModel.restoreFontSelection(
                        restoredPath, bookmark);
                }
            }

            XmlElement* params{xmlState->getChildByName("params")};
            if (params) {
                for (auto* param : getParameters()) {
                    if (auto* p = dynamic_cast<AudioProcessorParameterWithID*>(param)) {
                        double stored{params->getDoubleAttribute(p->paramID, p->getValue())};
                        // Corrupt normalized values must never reach a float gain
                        // or an integer migration. Keep the existing value for NaN/Inf.
                        if (!std::isfinite(stored))
                            continue;
                        stored = juce::jlimit(0.0, 1.0, stored);
                        // v4 widened `bank` to 0-255; rescale v3's normalised value by bank number.
                        if (stateVersion < 4 && p->paramID == "bank")
                            stored = juce::jlimit(
                                0.0,
                                1.0,
                                static_cast<double>(juce::roundToInt(
                                    stored * MidiConstants::percussionBank))
                                    / MidiConstants::maxChannelBank);
                        p->setValueNotifyingHost(static_cast<float>(stored));
                    }
                }
                // v9-v11 had only per-channel strengths. One shared value moves to
                // the global control; differing values stay per channel.
                if (stateVersion < 12)
                    migrateSharedVibratoScale();
            }
            // Reflect the restored selection now that the font is loaded.
            fluidSynthModel.syncToSelectedChannel();
            if (auto* channels = xmlState->getChildByName("channelPrograms"))
                for (auto* saved : channels->getChildIterator()) {
                    const int ch = saved->getIntAttribute("num", -1);
                    if (ch >= 0 && ch < 16)
                        fluidSynthModel.restoreRememberedControllers(ch,
                            stateVersion >= 7 ? saved->getIntAttribute("expression", -1) : -1,
                            stateVersion >= 7 ? saved->getIntAttribute("bendRange", -1) : -1);
                }
            fluidSynthModel.finishStateRestore(xmlState->getChildByName("channelPrograms") != nullptr);
        }
    }
}

void JuicySFAudioProcessor::migrateSharedVibratoScale() {
    auto strength = [this](int ch) {
        auto* control = valueTreeState.getParameter("vibratoScaleCh" + String(ch));
        return juce::roundToInt(control->convertFrom0to1(control->getValue()));
    };
    const int shared = strength(1);
    for (int ch = 2; ch <= 16; ++ch)
        if (strength(ch) != shared)
            return;
    auto* global = valueTreeState.getParameter("cc1VibratoScale");
    global->setValueNotifyingHost(global->convertTo0to1(static_cast<float>(shared)));
    for (int ch = 1; ch <= 16; ++ch)
        valueTreeState.getParameter("vibratoScaleCh" + String(ch))->setValueNotifyingHost(0.0f);
}

// FluidSynth renders float only.
bool JuicySFAudioProcessor::supportsDoublePrecisionProcessing() const {
    return false;
}

FluidSynthModel& JuicySFAudioProcessor::getFluidSynthModel() {
    return fluidSynthModel;
}

//==============================================================================
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicySFAudioProcessor();
}
