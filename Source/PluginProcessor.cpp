/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiConstants.h"
#include "Util.h"
#include "GuiConstants.h"

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
    // no properties, no subtrees (yet)
    valueTreeState.state.appendChild({ "banks", {}, {} }, nullptr);

    // one program assignment (instrument + ADSR/filter) per MIDI channel (0..15).
    // Sound controllers default to 64 = neutral (MIDI convention for CC70-79).
    ValueTree channelPrograms{ "channelPrograms" };
    for (int i = 0; i < 16; i++) {
        channelPrograms.appendChild({ "ch", {
            { "num", i },
            { "bank", i == 9 ? 128 : 0 },
            { "preset", 0 },
            { "attack", 64 },
            { "decay", 64 },
            { "sustain", 64 },
            { "release", 64 },
            { "filterCutOff", 64 },
            { "filterResonance", 64 }
        }, {} }, nullptr);
    }
    valueTreeState.state.appendChild(channelPrograms, nullptr);

    // Feed the VST3 unit interface's shared program list from the loaded font:
    // names come from bank 0 (GM program numbers 0..127); missing slots fall back
    // to "Program N" inside the extension.
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

// AudioParameterInt does not report itself as discrete, so JUCE's VST3 wrapper
// publishes ParameterInfo.stepCount = 0 for it (juce_audio_plugin_client_VST3.cpp:
// "if (! param.isDiscrete()) return 0"). Hosts that route MIDI Program Change via
// the unit/program-list mechanism (Cubase) require the kIsProgramChange parameter
// to be a discrete stepper with stepCount == programCount - 1 (127) to translate
// program numbers onto it — with stepCount 0 they treat it as a continuous knob
// and won't deliver PCs to it at all.
struct DiscreteParameterInt final : public AudioParameterInt {
    using AudioParameterInt::AudioParameterInt;
    bool isDiscrete() const override { return true; }
};

AudioProcessorValueTreeState::ParameterLayout JuicySFAudioProcessor::createParameterLayout() {
    // Sound-controller params default to 64 = neutral (bipolar modulators; the MIDI
    // convention for CC70-79). 0/127 are full negative/positive modulation.
    const int neutral{64};
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto intParam = [] (const String& id, const String& name,
                              int minimum, int maximum, int defaultValue,
                              const String& label) {
        return make_unique<AudioParameterInt>(
            juce::ParameterID{id, 0}, name, minimum, maximum, defaultValue,
            juce::AudioParameterIntAttributes{}.withLabel(label));
    };

    // global params: represent the currently-selected channel in the UI
    layout.add(
        // SoundFont 2.4 spec section 7.2: zero through 127, or 128.
        intParam("bank", "which bank is selected in the SoundFont", MidiConstants::midiMinValue, 128, MidiConstants::midiMinValue, "Bank"),
        // note: banks may be sparse, and lack a 0th preset. so defend against this.
        intParam("preset", "which patch (program/instrument) is selected in the SoundFont", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Preset"),
        intParam("attack", "volume envelope attack time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, neutral, "A"),
        intParam("decay", "volume envelope decay time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, neutral, "D"),
        intParam("sustain", "volume envelope sustain level", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, neutral, "S"),
        intParam("release", "volume envelope release time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, neutral, "R"),
        intParam("filterCutOff", "low-pass filter cut-off frequency", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, neutral, "Cut"),
        intParam("filterResonance", "low-pass filter resonance", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, neutral, "Res"));

    // Per-channel program parameters ("progCh1".."progCh16"), each in its own
    // parameter group. Two purposes:
    //  - hosts can select any channel's instrument via automation in every format;
    //  - in VST3, JUCE derives each parameter's unitId from its GROUP id
    //    (hashCode of "chUnit<n>"), which our custom IUnitInfo mirrors so hosts
    //    like Cubase can associate MIDI channel N with unit N (HALion-style
    //    multitimbral program routing).
    for (int ch = 1; ch <= 16; ++ch) {
        layout.add(make_unique<juce::AudioProcessorParameterGroup>(
            "chUnit" + String(ch),
            "Ch " + String(ch),
            "|",
            make_unique<DiscreteParameterInt>(
                juce::ParameterID{"progCh" + String(ch), 0},
                "program for MIDI channel " + String(ch),
                MidiConstants::midiMinValue, MidiConstants::midiMaxValue,
                MidiConstants::midiMinValue,
                juce::AudioParameterIntAttributes{}.withLabel("Ch" + String(ch) + " Prog"))));
    }

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
    return 0.0;
}

int JuicySFAudioProcessor::getNumPrograms()
{
    // Report exactly one program. If we advertise a program LIST (we used to expose
    // 128), VST3/VST2/AU hosts intercept incoming MIDI Program Change messages and
    // consume them as host program-list changes (routed to setCurrentProgram) instead
    // of delivering them as MIDI events to processBlock. That broke the core workflow:
    // per-channel GM program changes from the DAW never reached the synth, so every
    // channel stayed on its default patch. With a single program, the host passes
    // Program Change through as MIDI, where FluidSynthModel applies it per channel.
    return 1;
}

int JuicySFAudioProcessor::getCurrentProgram()
{
    return 0;
}

void JuicySFAudioProcessor::setCurrentProgram(int /*index*/)
{
    // no-op: instruments are chosen per MIDI channel (via incoming Program Change
    // or the per-channel dropdowns), not via a host program list.
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
    keyboardState.reset();
    fluidSynthModel.prepareToPlay(sampleRate, samplesPerBlock);

    reset();
}

void JuicySFAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    keyboardState.reset();
}

bool JuicySFAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Only mono/stereo and input/output must have same layout
    const AudioChannelSet& mainOutput = layouts.getMainOutputChannelSet();
    const AudioChannelSet& mainInput  = layouts.getMainInputChannelSet();

    // input and output layout must either be the same or the input must be disabled altogether
    if (! mainInput.isDisabled() && mainInput != mainOutput)
        return false;

    // do not allow disabling the main buses
    if (mainOutput.isDisabled())
        return false;

    // only allow stereo and mono
    return mainOutput.size() <= 2;
}

AudioProcessor::BusesProperties JuicySFAudioProcessor::getBusesProperties() {
    return BusesProperties()
            .withOutput ("Output", AudioChannelSet::stereo(), true);
}

void JuicySFAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    jassert (!isUsingDoublePrecision());

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (int i = getTotalNumInputChannels();
         i < juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels()); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Now pass any incoming midi messages to our keyboard state object, and let it
    // add messages to the buffer if the user is clicking on the on-screen keys
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);
    
    fluidSynthModel.processBlock(buffer, midiMessages);

    // and now get our synth to process these midi events and generate its output.
    // synth.renderNextBlock(buffer, midiMessages, 0, numSamples);

    // (see juce_VST3_Wrapper.cpp for the assertion this would trip otherwise)
    // we are !JucePlugin_ProducesMidiOutput, so clear remaining MIDI messages from our buffer
    midiMessages.clear();
}

//==============================================================================
bool JuicySFAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* JuicySFAudioProcessor::createEditor()
{
    // grab a raw pointer to it for our own use
    return /*pluginEditor = */new JuicySFAudioProcessorEditor (*this, valueTreeState);
}

//==============================================================================
void JuicySFAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    // Create an outer XML element..
    XmlElement xml{"MYPLUGINSETTINGS"};
    // v2: sound-controller values are bipolar with 64 = neutral (older saves stored
    // unipolar values where 0 was neutral — those are not restored, see setStateInformation)
    xml.setAttribute("stateVersion", currentStateVersion);

    // Store the values of all our parameters, using their param ID as the XML attribute
    XmlElement* params{xml.createNewChildElement("params")};
    for (auto* param : getParameters()) {
         if (auto* p = dynamic_cast<AudioProcessorParameterWithID*> (param)) {
             params->setAttribute(p->paramID, p->getValue());
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
    }
    {
        // per-channel instrument + ADSR/filter assignments
        ValueTree tree{valueTreeState.state.getChildWithName("channelPrograms")};
        XmlElement* channelProgramsElement{xml.createNewChildElement("channelPrograms")};
        for (int i = 0; i < tree.getNumChildren(); i++) {
            ValueTree ch{tree.getChild(i)};
            XmlElement* chElement{channelProgramsElement->createNewChildElement("ch")};
            chElement->setAttribute("num", static_cast<int>(ch.getProperty("num", i)));
            for (const String& p : { "bank", "preset", "attack", "decay",
                                     "sustain", "release", "filterCutOff", "filterResonance" }) {
                chElement->setAttribute(p, static_cast<int>(ch.getProperty(p, 0)));
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
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    // This getXmlFromBinary() helper function retrieves our XML from the binary blob..
    shared_ptr<XmlElement> xmlState{getXmlFromBinary(data, sizeInBytes)};

    if (xmlState.get() != nullptr) {
#if JUICYSF_TRACE_STATE
        DEBUG_PRINT(xmlState->toString());
#endif
        // make sure that it's actually our type of XML object..
        if (xmlState->hasTagName(valueTreeState.state.getType())) {
            const int stateVersion{xmlState->getIntAttribute("stateVersion", 1)};
            if (stateVersion > currentStateVersion) {
                ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
                fontState.setProperty("loadStatus", "error", nullptr);
                fontState.setProperty(
                    "loadMessage",
                    "This project uses JuicySF state version " + String(stateVersion)
                        + "; this build supports up to version "
                        + String(currentStateVersion) + ". No newer state was applied.",
                    nullptr);
                return;
            }
            // Pre-v2 saves stored sound-controller values with unipolar semantics
            // (0 = neutral); v2 is bipolar (64 = neutral). Restoring old values
            // unchanged would apply full negative modulation, so old saves keep
            // their bank/preset assignments but reset the six sound controllers
            // to the new neutral.
            const bool restoreSoundCtrls{stateVersion >= 2};
            const StringArray soundCtrlParams{ "attack", "decay", "sustain",
                                               "release", "filterCutOff", "filterResonance" };
            // Restore per-channel assignments BEFORE the soundFont, so that the
            // font load (triggered below) re-applies them to the synth.
            {
                XmlElement* channelProgramsElement{xmlState->getChildByName("channelPrograms")};
                if (channelProgramsElement) {
                    ValueTree tree{valueTreeState.state.getChildWithName("channelPrograms")};
                    for (auto* chElement : channelProgramsElement->getChildIterator()) {
                        int num{chElement->getIntAttribute("num", -1)};
                        ValueTree ch{tree.getChildWithProperty("num", num)};
                        if (ch.isValid()) {
                            for (const String& p : { "bank", "preset", "attack", "decay",
                                                     "sustain", "release", "filterCutOff", "filterResonance" }) {
                                if (!restoreSoundCtrls && soundCtrlParams.contains(p))
                                    continue;
                                const int maximum{p == "bank" ? 128 : MidiConstants::midiMaxValue};
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
            {
                ValueTree tree{valueTreeState.state.getChildWithName("uiState")};
                XmlElement* xmlElement{xmlState->getChildByName("uiState")};
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
                    {
                        Value value{tree.getPropertyAsValue("path", nullptr)};
                        value = xmlElement->getStringAttribute("path", value.getValue());
                    }
                    {
                        Value value{tree.getPropertyAsValue("bookmark", nullptr)};
                        jassert(value.getValue().isBinaryData());
                        MemoryBlock buffer;
                        buffer.fromBase64Encoding(xmlElement->getStringAttribute("bookmark", value.getValue()));
                        value = buffer;
                    }
                }
            }
            XmlElement* params{xmlState->getChildByName("params")};
            if (params) {
                for (auto* param : getParameters()) {
                    if (auto* p = dynamic_cast<AudioProcessorParameterWithID*>(param)) {
                        if (!restoreSoundCtrls && soundCtrlParams.contains(p->paramID))
                            continue; // pre-v2 save: leave sound controllers at neutral
                        p->setValueNotifyingHost(static_cast<float>(params->getDoubleAttribute(p->paramID, p->getValue())));
                    }
                }
            }
            // Ensure the global params + UI reflect the restored selected channel,
            // now that the font has loaded.
            fluidSynthModel.syncToSelectedChannel();
        }
    }
}

// FluidSynth only supports float in its process function, so that's all we can support.
bool JuicySFAudioProcessor::supportsDoublePrecisionProcessing() const {
    return false;
}

FluidSynthModel& JuicySFAudioProcessor::getFluidSynthModel() {
    return fluidSynthModel;
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicySFAudioProcessor();
}
