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
    // Same, addressed to a specific channel rather than the selected one. Used by
    // the per-channel volCh/panCh parameters, which every row's knob drives.
    void setChannelControllerValue(int channel, int controller, int value);

    // Per-channel mute/solo. While any channel is soloed, every channel that is
    // not soloed is silenced; otherwise the muted channels are. A silenced
    // channel drops incoming note-ons and is sent all-notes-off at the moment it
    // becomes silenced, so held notes release rather than hang. Note-offs, CCs,
    // program changes, and bend are never dropped, so channel state stays correct
    // and unmuting resumes mid-song without a resync.
    bool isChannelSilenced(int channel) const;
    unsigned int getSilencedMask() const;

    // Push channelPrograms' saved mixer values into the volCh/panCh/muteCh/soloCh
    // parameters. Used after restoring state written before those parameters
    // existed, where the per-channel tree is the only record of them.
    void syncMixerParamsFromState();

    // push the currently-selected channel's saved program into the params/UI
    // (used after restoring plugin state)
    void syncToSelectedChannel();

    // Select the channel shown by the editor's shared bank/program and sound-control
    // parameters. The public method keeps selection bounds and ValueTree notification
    // behavior in one place for the channel list and other editor surfaces.
    void selectChannelForEditing(int channel);

    // assign a patch to an arbitrary channel (used by the per-row patch dropdowns).
    // applies to the synth, persists into channelPrograms, and keeps the global
    // params consistent if `channel` happens to be the selected one. Returns false
    // without publishing the requested state when validation/FluidSynth rejects it.
    bool setChannelProgram(int channel, int bank, int preset);

    // Read-only engine diagnostics used by automated tests and future opt-in
    // diagnostic reporting. Inputs are zero-based FluidSynth channel numbers.
    bool getControllerValue(int channel, int controller, int& value) const;
    bool getPitchBend(int channel, int& value) const;
    bool getPitchWheelSensitivity(int channel, int& semitones) const;
    bool getChannelProgram(int channel, int& bank, int& preset) const;
    unsigned int getProgramApplyFailureMask() const;
    bool getLastDispatchedController(int channel, int controller, int& value, int& sample) const;
    bool getLastDispatchedNoteOnProgram(int channel,
                                        int& bank,
                                        int& preset,
                                        int& sample) const;
    bool getLastDispatchedChannelPressure(int channel, int& value, int& sample) const;
    bool getLastDispatchedKeyPressure(int channel, int key, int& value, int& sample) const;
    String getFontLoadStatus() const;
    bool isBookmarkStale() const;
    String getFontLoadMessage() const;
    String getLastAttemptedFontPath() const;
    String getLoadedFontPath() const;
    // FluidSynth 2.5.5 accepts host rates from 8 to 96 kHz. Unsupported host
    // rates are intentionally rendered as silence instead of running the synth
    // at a stale rate and producing incorrectly pitched audio.
    bool isSampleRateSupported() const;

    // Which MIDI channel the shared bank/preset/sound-control parameters edit,
    // zero-based. Read-only view of uiState.selectedChannel.
    int getSelectedChannel() const;

    // Voice limit as configured and as the synth reports it. FluidSynth sizes its
    // rvoice event queue from the settings value at construction, so the two must
    // agree or dense material silently drops engine events.
    bool getConfiguredPolyphony(int& configured, int& active) const;

    // FluidSynth supports a per-font bank offset, so every program path here
    // converts between the raw engine bank and the font's own logical bank.
    // Juicy16 never sets an offset, which would leave that conversion untested;
    // these let the offline harness install one. Message thread, processing stopped.
    bool getLoadedFontBankOffset(int& offset) const;
    bool setLoadedFontBankOffset(int offset);

    struct VoiceStateCounts {
        int playing{0};
        int on{0};
        int sustained{0};
        int sostenuto{0};
    };

    // Offline-harness diagnostic. Call only while the caller owns/suspends audio
    // processing; FluidSynth voice pointers are sampled and consumed internally.
    bool getVoiceStateCounts(int channel, VoiceStateCounts& counts) const;

    // Master output level in dB, applied after rendering with smoothing so a
    // host automating it cannot produce a step discontinuity. FluidSynth's own
    // gain stays at its documented default; this is the user-facing trim.
    void setOutputLevelDb(float decibels);

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

    // default value for a per-channel property: volume is the GM default 100, pan
    // is centre 64, mute and solo are off, bank/preset default to 0. Shared with
    // PluginProcessor so the state writer and reader agree.
    static int defaultParamValue(const String& parameterID);

    // Per-channel mixer parameter ids ("volCh1".."panCh16", "muteCh1".."soloCh16";
    // 0-based channel in, 1-based name out). ccIndex indexes ccIndexOrder.
    static String mixerParamId(int ccIndex, int chZeroBased);
    static String muteParamId(int chZeroBased);
    static String soloParamId(int chZeroBased);

    // per-channel program parameter ids ("progCh1".."progCh16", 0-based channel in,
    // 1-based name out) and the reverse mapping (-1 if not a program param).
    static String progParamId(int chZeroBased);
    static int progParamChannel(const String& parameterID);

    // Every property stored independently per MIDI channel, in channelPrograms.
    // Public so the state writer and reader in PluginProcessor share one
    // definition of the per-channel schema instead of repeating it and drifting.
    static const StringArray perChannelParams;

private:
    static const StringArray programChangeParams;

    // True only on the thread synchronously mirroring engine/channel state into
    // parameters. Thread-local storage avoids suppressing unrelated host automation
    // arriving concurrently on the audio thread.
    static thread_local bool mirroringParameters;

    void loadSelectedChannel(int newChannel);
    void dispatchMidiEvent(const MidiMessage& message, int samplePosition);
    // Audio thread. Takes the payload between 0xF0 and 0xF7 so processBlock can
    // dispatch from the MidiBuffer directly, without MidiMessage's heap copy.
    void dispatchSysEx(const uint8_t* payload, int payloadBytes);
    void renderSamples(AudioBuffer<float>& buffer, int startSample, int numSamples);
    // Audio thread. Renders into the oversampling FIFO instead of the host block,
    // used only when the host rate is above FluidSynth's ceiling.
    void renderIntoFifo(int startSample, int numSamples);
    void renderThroughOversampler(AudioBuffer<float>& buffer,
                                  MidiBuffer& midiMessages,
                                  int numSamples);

    // Walks the block's events in timestamp order, rendering the gap before each
    // one through `renderSegment(from, count)` and then dispatching it.
    // `mapPosition` converts a host sample position into the domain being
    // rendered: the host block itself normally, or the internal FIFO when
    // oversampling. Both callers share this so the SysEx handling below cannot
    // drift between them.
    template <typename RenderSegment, typename MapPosition>
    void dispatchTimestampedEvents(MidiBuffer& midiMessages,
                                   int numSamples,
                                   int renderLimit,
                                   RenderSegment&& renderSegment,
                                   MapPosition&& mapPosition) {
        int renderPosition{0};
        for (const auto metadata : midiMessages) {
            const int eventPosition{juce::jlimit(0, numSamples, metadata.samplePosition)};
            const int target{juce::jlimit(0, renderLimit, mapPosition(eventPosition))};
            if (target > renderPosition) {
                renderSegment(renderPosition, target - renderPosition);
                renderPosition = target;
            }
            // MidiMessage copies anything longer than four bytes to the heap, which
            // would allocate on the audio thread for every SysEx - and game rips
            // carry a GM/GS/XG reset at tick 0. Dispatch those straight from the
            // buffer's own storage instead; short messages stay inline and free.
            if (metadata.numBytes > 4 && metadata.data[0] == 0xf0) {
                if (metadata.numBytes >= 2)
                    dispatchSysEx(metadata.data + 1, metadata.numBytes - 2);
                continue;
            }
            dispatchMidiEvent(metadata.getMessage(), eventPosition);
        }
        if (renderLimit > renderPosition)
            renderSegment(renderPosition, renderLimit - renderPosition);
    }
    // mirror a channel's current program into its progChN parameter (guarded so
    // parameterChanged doesn't re-apply it to the synth). Message thread only.
    void syncProgParam(int ch, int preset);

    struct AppliedProgram {
        int rawBank{-1};
        int preset{-1};
    };

    // The only function that mutates a channel's FluidSynth program. Exact-bank
    // callers pass a raw FluidSynth bank (including the loaded font's offset);
    // MIDI and progChN callers retain the engine's current Bank Select state.
    // Every successful route captures the actual engine result in the same
    // atomics. Audio-thread routes additionally queue message-thread state/UI
    // synchronization.
    bool applyProgramToEngine(int midiCh,
                              int rawBank,
                              int preset,
                              bool retainCurrentBank,
                              bool queueStateSync,
                              AppliedProgram* applied = nullptr);
    void syncAppliedProgramOnMessageThread(int midiCh,
                                           const AppliedProgram& applied);
    void recordProgramApplyFailure(int midiCh);
    // Undo the basic-channel reconfiguration a CC124-127 channel-mode message
    // performs, which would otherwise disable most of the 16 channels. No-op for
    // every other controller. Audio thread.
    void restoreSixteenChannelLayout(int controller);
    // The loaded font's FluidSynth bank offset, or 0 when no font is loaded.
    // Message thread only: FluidSynth takes its API lock to answer.
    int loadedFontBankOffset() const;

    // Voice ceiling. Dense 16-channel material must never steal voices.
    static constexpr int maximumPolyphony{512};
    static constexpr int kNumChannels{16};
    static constexpr int kNumMixerCcs{2}; // CC7 volume, CC10 pan (see ccIndexOrder)

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
    // Sticky diagnostic bit per channel. This records rejected/failed program
    // applications for paths that cannot return an error (MIDI/host automation).
    std::atomic<unsigned int> programApplyFailureMask{0};

    // last engine program per channel (RAW synth bank incl. offset + preset),
    // maintained at every program_select/change site; used for the immediate
    // audio-thread re-assert after a reset SysEx so the very next notes are correct.
    std::atomic<int> engineBank[kNumChannels];
    std::atomic<int> enginePreset[kNumChannels];

    static bool isSystemResetSysex(const uint8_t* data, int size);

    // per-channel mixer values captured on the audio thread when a mapped CC
    // (7 volume / 10 pan) arrives; −1 = nothing pending. Consumed on the message
    // thread in handleAsyncUpdate — ValueTree/param writes must NOT happen on the
    // audio thread.
    std::atomic<int> midiCcValue[kNumChannels][kNumMixerCcs];
    std::atomic<unsigned int> midiCcDirtyMask{0}; // bit per channel
    // Last value actually sent to the synth for the exposed mixer CCs. Reset
    // SysEx restoration reads only these atomics, so it cannot race a newer MIDI
    // event by consulting stale message-thread ValueTrees.
    std::atomic<int> engineCc[kNumChannels][kNumMixerCcs];
    // Mute/solo. Written from the message thread or a host automation thread and
    // read on the audio thread for every note-on, so the derived mask is stored
    // rather than recomputed per event.
    std::atomic<unsigned int> muteMask{0};
    std::atomic<unsigned int> soloMask{0};
    std::atomic<unsigned int> silencedMask{0};
    static unsigned int deriveSilencedMask(unsigned int mutes, unsigned int solos);
    // Recompute silencedMask and send all-notes-off to channels that just became
    // silenced. Safe on the audio thread: no allocation, no locks of our own.
    void refreshSilencedMask();
    // Master trim. Written from the message thread or a host automation thread,
    // read on the audio thread; the smoother lives on the audio thread only.
    std::atomic<float> outputLevelGain{1.0f};
    juce::SmoothedValue<float> outputLevelSmoother;
    // Exact input trace used by the offline conformance suite. These atomics are
    // diagnostics only; synthesis behavior still comes from FluidSynth.
    std::atomic<int> lastCcValue[kNumChannels][128];
    std::atomic<int> lastCcSample[kNumChannels][128];
    std::atomic<int> lastNoteOnBank[kNumChannels];
    std::atomic<int> lastNoteOnPreset[kNumChannels];
    std::atomic<int> lastNoteOnSample[kNumChannels];
    std::atomic<int> lastChannelPressureValue[kNumChannels];
    std::atomic<int> lastChannelPressureSample[kNumChannels];
    std::atomic<int> lastKeyPressureValue[kNumChannels][128];
    std::atomic<int> lastKeyPressureSample[kNumChannels][128];
    static const fluid_midi_control_change ccIndexOrder[kNumMixerCcs];
    // Per-channel parameter IDs. One parser for every "<prefix><1..16>" family,
    // allocation-free because parameterChanged may run on the audio thread.
    enum class ChannelParamKind { none, volume, pan, mute, solo };
    static int channelSuffixOf(const String& parameterID,
                               const char* prefix,
                               int prefixLength);
    static ChannelParamKind parseChannelParam(const String& parameterID,
                                              int& chZeroBased);
    static int ccToIndex(int cc); // −1 if not one of ours


    // there's no bimap in the standard library!
    static const map<fluid_midi_control_change, String> ccToChannelProperty;
    static const map<String, fluid_midi_control_change> channelPropertyToCc;

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

    // The rate FluidSynth renders at. Equal to the host rate unless the host is
    // above FluidSynth's ceiling, when it is hostSampleRate / oversampleFactor.
    float currentSampleRate;
    float hostSampleRate{44100.0f};
    std::atomic<bool> sampleRateSupported{true};

    bool unloadAndLoadFont(const String& absPath);
    static bool riffContainerOverrunsFile(const juce::File& src);
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

    // FluidSynth 2.5.5 renders no higher than 96 kHz. Above that the engine runs
    // at an integer fraction of the host rate and each block is interpolated back
    // up, so a 192 kHz project plays instead of falling silent. 1 means the host
    // rate is directly supported and none of this is used.
    int oversampleFactor{1};
    AudioBuffer<float> oversampleFifo;
    // Rendered-but-unconsumed internal samples, carried to the next block so the
    // interpolator's fractional position never costs or duplicates audio.
    int oversampleFifoFill{0};
    juce::LagrangeInterpolator oversampleInterpolators[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidSynthModel)
};
