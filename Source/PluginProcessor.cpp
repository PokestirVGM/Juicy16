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

    // one assignment per MIDI channel (0..15): instrument, plus the mixer
    // controls. Volume and pan default to the GM channel defaults, which are also
    // FluidSynth's own channel initialisation values.
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
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto intParam = [] (const String& id, const String& name,
                              int minimum, int maximum, int defaultValue,
                              const String& label) {
        return make_unique<AudioParameterInt>(
            juce::ParameterID{id, 1}, name, minimum, maximum, defaultValue,
            juce::AudioParameterIntAttributes{}.withLabel(label));
    };

    // global params: represent the currently-selected channel in the UI
    layout.add(
        // A font's own banks are 0-127 melodic plus 128 percussion (SF2 2.04
        // section 7.2), but a channel's runtime bank reaches 255 once FluidSynth
        // adds its drum offset to the Bank Select MSB. The parameter carries the
        // channel value, so it has to span the wider range or the UI cannot show
        // what the engine and the saved state hold.
        intParam("bank", "which bank is selected in the SoundFont",
                 MidiConstants::midiMinValue, MidiConstants::maxChannelBank,
                 MidiConstants::midiMinValue, "Bank"),
        // note: banks may be sparse, and lack a 0th preset. so defend against this.
        intParam("preset", "which patch (program/instrument) is selected in the SoundFont", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Preset"));

    // Master output trim. Not a MIDI controller and not per channel: it is the
    // user's gain-staging control over the whole plugin, applied after rendering.
    layout.add(make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputLevel", 1}, "master output level",
        juce::NormalisableRange<float>{GuiConstants::outputLevelMinDb,
                                       GuiConstants::outputLevelMaxDb, 0.1f},
        0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("Out")));

    // Reverb. FluidSynth has always been running one - `synth.reverb.active`
    // defaults to on and Juicy16 never touched it - so until Beta 1 every rip was
    // played through generic defaults chosen by nobody for this product. These
    // are a control surface over that existing reverb: no new DSP, no new
    // dependency, no new licence obligation. Per-channel CC91 sends still reach
    // the engine at their own timestamps and feed the reverb these controls set.
    // OFF by default, by owner decision on 2026-08-23. Juicy16's reverb was
    // never audible before this release, so switching it on by default would
    // change how every existing project sounds without the user asking. The
    // controls are there and the GM default send feeds them the moment it is
    // switched on; the choice to add reverb is the user's to make.
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
        // Size, damping and level take FluidSynth's own 0-1 ranges. WIDTH DOES
        // NOT: FluidSynth accepts 0-100 there, but its own default is 0.8 and
        // everything musically useful lives below 1, so the full range would put
        // the entire useful span inside the first one percent of a knob's
        // travel. Narrowed to 0-1 deliberately; nothing reachable is lost.
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

    // Per-channel mixer parameters: volume (CC7), pan (CC10), mute, and solo, one
    // of each for all 16 channels. Real host parameters rather than editor-only
    // state, so a host can automate any channel and a right-click on a knob
    // offers the host's own automation and controller-link menu.
    //
    // They are deliberately NOT in a parameter group. JUCE derives a VST3
    // parameter's unitId from its group, and the vendored wrapper serves a FIXED
    // 17-unit structure (root plus chUnit1..16) that Cubase caches before the
    // component connection exists. Any group here would publish a parameter
    // pointing at an 18th unit the host was never told about. Ungrouped, these
    // land in the root unit beside bank, preset, and outputLevel, and the
    // structure Cubase's program-change routing depends on is untouched.
    //
    // Incoming CC7/CC10 on a channel overwrite that channel's volume/pan exactly
    // as Program Change overwrites a manual instrument pick: what the knob sets
    // is a starting point, and the next event on that channel replaces it at the
    // event's own timestamp.
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
    // Mute and solo are the plugin's own, not MIDI controllers: nothing in a MIDI
    // file changes them, and a silenced channel drops note-ons rather than having
    // its CC7 forced to zero, so the file's own volume survives being muted.
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

    // Per-channel program parameters ("progCh1".."progCh16"), each in its own
    // parameter group. Two purposes:
    //  - hosts can select any channel's instrument via automation in every format;
    //  - in VST3, JUCE derives each parameter's unitId from its GROUP id
    //    (hashCode of "chUnit<n>"), which the pinned wrapper IUnitInfo mirrors so hosts
    //    like Cubase can associate MIDI channel N with unit N (HALion-style
    //    multitimbral program routing).
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
    // 128), VST3/AU hosts intercept incoming MIDI Program Change messages and
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
    // v6: the reverb gained a control surface - reverbOn, reverbProfile, and the
    // four engine parameters. A v5 save has none of them and opens on the
    // Universal profile, which is a deliberate default rather than an inherited
    // one; see docs/CONTROLLER_SUPPORT.md.
    // v5: volume and pan became per-channel parameters (volCh1..panCh16) and mute
    // and solo were added, so the two selected-channel `volume`/`pan` parameters
    // are gone and each channelPrograms node carries mute and solo as well.
    // v4: as v3, but the `bank` parameter spans 0-255 instead of 0-128, so its
    // normalised value means a different bank number than it did.
    // v3: per-channel state is bank/preset plus the mixer controls volume and pan.
    // v1 and v2 stored six CC71-79 sound-controller values instead; those are read
    // as absent rather than migrated (see setStateInformation).
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
        // per-channel instrument + mixer assignments
        ValueTree tree{valueTreeState.state.getChildWithName("channelPrograms")};
        XmlElement* channelProgramsElement{xml.createNewChildElement("channelPrograms")};
        for (int i = 0; i < tree.getNumChildren(); i++) {
            ValueTree ch{tree.getChild(i)};
            XmlElement* chElement{channelProgramsElement->createNewChildElement("ch")};
            chElement->setAttribute("num", static_cast<int>(ch.getProperty("num", i)));
            // Driven off the model's own list so the writer and the reader cannot
            // drift apart when the per-channel schema changes.
            for (const String& p : FluidSynthModel::perChannelParams) {
                chElement->setAttribute(
                    p, static_cast<int>(
                           ch.getProperty(p, FluidSynthModel::defaultParamValue(p))));
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
                    "This project uses Juicy16 state version " + String(stateVersion)
                        + "; this build supports up to version "
                        + String(currentStateVersion) + ". No newer state was applied.",
                    nullptr);
                return;
            }
            // v3 replaced the six per-channel sound controllers (CC71-79) with
            // volume and pan (CC7/CC10). A v1 or v2 save has no volume/pan
            // attributes at all, so those channels simply keep the GM defaults
            // this instance was constructed with; the obsolete attributes are
            // ignored rather than migrated, because there is no meaningful
            // mapping from an envelope control to a mixer control.
            const bool restoreMixer{stateVersion >= 3};
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
            // channelPrograms is the only record a pre-v5 save has of per-channel
            // volume and pan, and it has none at all of mute and solo. Derive the
            // parameters from it now: the params loop below reads each parameter's
            // CURRENT value when the attribute is absent, so this becomes the
            // restored value there rather than being overwritten by a default.
            fluidSynthModel.syncMixerParamsFromState();
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
                        double stored{params->getDoubleAttribute(p->paramID, p->getValue())};
                        // v4 widened `bank` from 0-128 to 0-255. Parameters are
                        // stored normalised, so the same 1.0 that meant bank 128
                        // in a v3 save would restore as 255 here. Rescale through
                        // the bank number itself rather than the ratio, so the
                        // restored value is the bank the user actually saved.
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
