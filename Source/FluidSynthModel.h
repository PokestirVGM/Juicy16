//
// Created by Alex Birch on 10/09/2017.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <fluidsynth.h>
#include <memory>
#include <map>
#include <atomic>
#include "MidiConstants.h"

using namespace std;

class FluidSynthModel
: public ValueTree::Listener
, public AudioProcessorValueTreeState::Listener
, public juce::AsyncUpdater {
public:
    FluidSynthModel(
        AudioProcessorValueTreeState& valueTreeState
        );
     ~FluidSynthModel();

    void initialise();

    // called from prepareToPlay: applies the host sample rate and pre-allocates
    // the stereo scratch buffer used for mono-output downmixing.
    void prepareToPlay(double sampleRate, int samplesPerBlock);

    void setControllerValue(int controller, int value);

    // push the currently-selected channel's saved program into the params/UI
    // (used after restoring plugin state)
    void syncToSelectedChannel();

    // assign a patch to an arbitrary channel (used by the per-row patch dropdowns).
    // applies to the synth, persists into channelPrograms, and keeps the global
    // params consistent if `channel` happens to be the selected one.
    void setChannelProgram(int channel, int bank, int preset);

    // invoked on the message thread after refreshBanks rebuilds the `banks` tree
    // (font load/unload). Used to push program names to the VST3 unit interface.
    std::function<void()> onBanksRefreshed;

    // TEMPORARY diagnostic (Cubase VST3 bring-up): what MIDI/parameter input has
    // been seen. Shown in the status bar; remove once VST3 routing is confirmed.
    juce::String getMidiMonitorText();

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages);

    // marshals MIDI-driven per-channel program changes (captured on the audio
    // thread) onto the message thread, where the display/params are updated.
    void handleAsyncUpdate() override;


    void setSampleRate(float sampleRate);
    
    //==============================================================================
    virtual void parameterChanged (const String& parameterID, float newValue) override;
    
    virtual void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                           const Identifier& property) override;
    inline virtual void valueTreeChildAdded (ValueTree& parentTree,
                                             ValueTree& childWhichHasBeenAdded) override {};
    inline virtual void valueTreeChildRemoved (ValueTree& parentTree,
                                               ValueTree& childWhichHasBeenRemoved,
                                               int indexFromWhichChildWasRemoved) override {};
    inline virtual void valueTreeChildOrderChanged (ValueTree& parentTreeWhoseChildrenHaveMoved,
                                                    int oldIndex, int newIndex) override {};
    inline virtual void valueTreeParentChanged (ValueTree& treeWhoseParentHasChanged) override {};
    inline virtual void valueTreeRedirected (ValueTree& treeWhichHasBeenChanged) override {};

    // default value for a per-channel parameter: sound controllers are neutral at 64
    // (MIDI/GS convention), bank/preset default to 0. Shared with PluginProcessor.
    static int defaultParamValue(const String& parameterID);

    // per-channel program parameter ids ("progCh1".."progCh16", 0-based channel in,
    // 1-based name out) and the reverse mapping (-1 if not a program param).
    static String progParamId(int chZeroBased);
    static int progParamChannel(const String& parameterID);

private:
    static const StringArray programChangeParams;
    // every parameter that is stored independently per MIDI channel
    static const StringArray perChannelParams;

    // true while we're pushing a channel's saved values into the params during a
    // channel switch; suppresses the save-back so we don't clobber the tree.
    bool loadingChannel{false};

    void loadSelectedChannel(int newChannel);
    void saveParamToChannel(const String& parameterID, int value);
    // mirror a channel's current program into its progChN parameter (guarded so
    // parameterChanged doesn't re-apply it to the synth). Message thread only.
    void syncProgParam(int ch, int preset);

    static constexpr int kNumChannels{16};
    static constexpr int kNumSoundCcs{6}; // CC71,72,73,74,75,79 (see ccIndexOrder)
    // per-channel program captured on the audio thread when a MIDI program
    // change arrives; consumed on the message thread in handleAsyncUpdate.
    std::atomic<int> midiBank[kNumChannels];
    std::atomic<int> midiPreset[kNumChannels];
    std::atomic<unsigned int> midiProgramDirtyMask{0}; // bit per channel

    // per-channel sound-controller values captured on the audio thread when a
    // mapped CC (71/72/73/74/75/79) arrives; −1 = nothing pending. Consumed on the
    // message thread in handleAsyncUpdate — ValueTree/param writes must NOT happen
    // on the audio thread.
    std::atomic<int> midiCcValue[kNumChannels][kNumSoundCcs];
    std::atomic<unsigned int> midiCcDirtyMask{0}; // bit per channel
    static const fluid_midi_control_change ccIndexOrder[kNumSoundCcs];
    static int ccToIndex(int cc); // −1 if not one of ours

    // --- TEMPORARY diagnostic MIDI/param input monitor (see getMidiMonitorText) ---
    std::atomic<int> monNoteOn[kNumChannels]{};
    std::atomic<int> monLastPcChannel{-1};    // last MIDI program-change event
    std::atomic<int> monLastPcProgram{-1};
    std::atomic<int> monLastParamChannel{-1}; // last progChN PARAMETER change (VST3 units path)
    std::atomic<int> monLastParamProgram{-1};

    // there's no bimap in the standard library!
    static const map<fluid_midi_control_change, String> controllerToParam;
    static const map<String, fluid_midi_control_change> paramToController;

    void refreshBanks();

    AudioProcessorValueTreeState& valueTreeState;

    // https://stackoverflow.com/questions/38980315/is-stdunique-ptr-deletion-order-guaranteed
    // members are destroyed in reverse of the order they're declared
    // http://www.fluidsynth.org/api/
    // in their examples, they destroy the synth before destroying the settings
    unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings;
    unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth;

    float currentSampleRate;

    void unloadAndLoadFont(const String &absPath);
    void loadFont(const String &absPath);

    // Repairs malformed-but-playable DLS files (bad RIFF sizes from some exporters)
    // by writing a corrected copy to a temp file; returns that file, or an invalid
    // File if no repair was needed / it isn't a DLS. The temp copy is kept alive
    // (repairedTempFile) for the loaded font's lifetime and removed on unload.
    juce::File writeRepairedTempCopy(const juce::File& src);
    void clearRepairedTemp();
    juce::File repairedTempFile;

    int sfont_id;
    // the channel currently being edited in the UI. Written on the message thread
    // (loadSelectedChannel), read on the audio thread (processBlock) — atomic.
    std::atomic<unsigned int> channel;

    // stereo scratch for hosts giving us a mono output bus: FluidSynth can only
    // render stereo pairs, so we render here and downmix. Sized in prepareToPlay.
    AudioBuffer<float> stereoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidSynthModel)
};
