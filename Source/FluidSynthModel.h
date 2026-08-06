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
    ~FluidSynthModel() override;

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

    // Read-only engine diagnostics used by automated tests and future opt-in
    // diagnostic reporting. Inputs are zero-based FluidSynth channel numbers.
    bool getControllerValue(int channel, int controller, int& value) const;
    bool getPitchBend(int channel, int& value) const;
    bool getPitchWheelSensitivity(int channel, int& semitones) const;
    bool getChannelProgram(int channel, int& bank, int& preset) const;
    bool getLastDispatchedController(int channel, int controller, int& value, int& sample) const;
    bool getLastDispatchedChannelPressure(int channel, int& value, int& sample) const;
    bool getLastDispatchedKeyPressure(int channel, int key, int& value, int& sample) const;
    String getFontLoadStatus() const;
    String getLoadedFontPath() const;

    // invoked on the message thread after refreshBanks rebuilds the `banks` tree
    // (font load/unload). Used to push program names to the VST3 unit interface.
    std::function<void()> onBanksRefreshed;

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages);

    // marshals MIDI-driven per-channel program changes (captured on the audio
    // thread) onto the message thread, where the display/params are updated.
    void handleAsyncUpdate() override;


    void setSampleRate(float sampleRate);
    
    //==============================================================================
    void parameterChanged (const String& parameterID, float newValue) override;
    
    void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                   const Identifier& property) override;
    void valueTreeChildAdded (ValueTree&, ValueTree&) override {}
    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override {}
    void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    void valueTreeParentChanged (ValueTree&) override {}
    void valueTreeRedirected (ValueTree&) override {}

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

    // True only on the thread synchronously mirroring engine/channel state into
    // parameters. Thread-local storage avoids suppressing unrelated host automation
    // arriving concurrently on the audio thread.
    static thread_local bool mirroringParameters;

    void loadSelectedChannel(int newChannel);
    void saveParamToChannel(const String& parameterID, int value);
    void dispatchMidiEvent(const MidiMessage& message, int samplePosition);
    void renderSamples(AudioBuffer<float>& buffer, int startSample, int numSamples);
    // message thread: push every channel's saved program + sound CCs from
    // channelPrograms back into the synth (used after a system-reset SysEx).
    // mirror a channel's current program into its progChN parameter (guarded so
    // parameterChanged doesn't re-apply it to the synth). Message thread only.
    void syncProgParam(int ch, int preset);

    static constexpr int kNumChannels{16};
    static constexpr int kNumSoundCcs{6}; // CC71,72,73,74,75,79 (see ccIndexOrder)

    // shared audio-thread path for "set this channel's program" (MIDI PC or the
    // VST3 progChN parameters): applies to the synth and captures the result for
    // handleAsyncUpdate.
    // NOTE: an earlier CC85-as-program-select fallback (v0.3.8) was REMOVED in
    // v0.3.10 — native PC routing works in all hosts since the v0.3.9 unit-timing
    // fix, and intercepting a CC is hazardous: host chase/reset machinery that
    // sprays controller resets (value 0) would silently reset every channel's
    // program.
    void applyProgramChangeFromAudioThread(int midiCh, int program);
    // per-channel program captured on the audio thread when a MIDI program
    // change arrives; consumed on the message thread in handleAsyncUpdate.
    std::atomic<int> midiBank[kNumChannels];
    std::atomic<int> midiPreset[kNumChannels];
    std::atomic<unsigned int> midiProgramDirtyMask{0}; // bit per channel

    // last engine program per channel (RAW synth bank incl. offset + preset),
    // maintained at every program_select/change site; used for the immediate
    // audio-thread re-assert after a reset SysEx so the very next notes are correct.
    std::atomic<int> engineBank[kNumChannels];
    std::atomic<int> enginePreset[kNumChannels];

    static bool isSystemResetSysex(const uint8_t* data, int size);

    // per-channel sound-controller values captured on the audio thread when a
    // mapped CC (71/72/73/74/75/79) arrives; −1 = nothing pending. Consumed on the
    // message thread in handleAsyncUpdate — ValueTree/param writes must NOT happen
    // on the audio thread.
    std::atomic<int> midiCcValue[kNumChannels][kNumSoundCcs];
    std::atomic<unsigned int> midiCcDirtyMask{0}; // bit per channel
    // Last value actually sent to the synth for the six exposed sound CCs. Reset
    // SysEx restoration reads only these atomics, so it cannot race a newer MIDI
    // event by consulting stale message-thread ValueTrees.
    std::atomic<int> engineCc[kNumChannels][kNumSoundCcs];
    // Exact input trace used by the offline conformance suite. These atomics are
    // diagnostics only; synthesis behavior still comes from FluidSynth.
    std::atomic<int> lastCcValue[kNumChannels][128];
    std::atomic<int> lastCcSample[kNumChannels][128];
    std::atomic<int> lastChannelPressureValue[kNumChannels];
    std::atomic<int> lastChannelPressureSample[kNumChannels];
    std::atomic<int> lastKeyPressureValue[kNumChannels][128];
    std::atomic<int> lastKeyPressureSample[kNumChannels][128];
    static const fluid_midi_control_change ccIndexOrder[kNumSoundCcs];
    static int ccToIndex(int cc); // −1 if not one of ours


    // there's no bimap in the standard library!
    static const map<fluid_midi_control_change, String> controllerToParam;
    static const map<String, fluid_midi_control_change> paramToController;

    void refreshBanks();
    void createSynth();
    void reloadFontFromState();

    AudioProcessorValueTreeState& valueTreeState;
    // Guards the transactional rollback of path/bookmark properties after a
    // rejected replacement. Other ValueTree listeners still see the active path,
    // but this model must not interpret that rollback as a new load request.
    bool suppressFontStateReload{false};

    // https://stackoverflow.com/questions/38980315/is-stdunique-ptr-deletion-order-guaranteed
    // members are destroyed in reverse of the order they're declared
    // http://www.fluidsynth.org/api/
    // in their examples, they destroy the synth before destroying the settings
    unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings;
    unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth;

    float currentSampleRate;

    bool unloadAndLoadFont(const String& absPath);
    void publishFontLoadResult(bool success,
                               const String& requestedPath,
                               const String& message,
                               bool repaired);

    // Repairs malformed-but-playable DLS files (bad RIFF sizes from some exporters)
    // by writing a corrected copy to a temp file; returns that file, or an invalid
    // File if no repair was needed / it isn't a DLS. The temp copy is kept alive
    // (repairedTempFile) for the loaded font's lifetime and removed on unload.
    juce::File writeRepairedTempCopy(const juce::File& src);
    void clearRepairedTemp();
    juce::File repairedTempFile;

    std::atomic<int> sfont_id;
    // the channel currently being edited in the UI. Written on the message thread
    // (loadSelectedChannel), read on the audio thread (processBlock) — atomic.
    std::atomic<unsigned int> channel;

    // stereo scratch for hosts giving us a mono output bus: FluidSynth can only
    // render stereo pairs, so we render here and downmix. Sized in prepareToPlay.
    AudioBuffer<float> stereoScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidSynthModel)
};
