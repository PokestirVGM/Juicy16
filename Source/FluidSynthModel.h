#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <fluidsynth.h>
#include <memory>
#include <map>
#include <atomic>
#include <array>
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

    // Applies the host rate and preallocates scratch buffers.
    void prepareToPlay(double sampleRate, int samplesPerBlock);

    void setControllerValue(int controller, int value);
    // Addressed to a specific channel; used by the volChN/panChN parameters.
    void setChannelControllerValue(int channel, int controller, int value);

    // Silenced = muted, or not soloed while any channel is. A silenced channel drops
    // note-ons and gets all-notes-off; CCs, program changes and bend still pass so
    // unmuting needs no resync.
    bool isChannelSilenced(int channel) const;
    unsigned int getSilencedMask() const;

    // Copies saved per-channel mixer values into the parameters, for states
    // written before those parameters existed.
    void syncMixerParamsFromState();

    // Pushes the selected channel's saved program into params/UI after restore.
    void syncToSelectedChannel();

    // Complete project recall even when its bank path and parameter values did
    // not change. Called with processing suspended, after all saved values apply.
    void finishStateRestore(bool restoreChannelRecords);

    // Restore the saved path/bookmark pair before resolving either one. This
    // prevents an intermediate path assignment from loading the bank twice.
    void restoreFontSelection(const String& path, const juce::MemoryBlock& bookmark,
                              bool forceReload = false);

    // Selects the channel the editor's shared controls edit.
    void selectChannelForEditing(int channel);

    // Assigns a patch to any channel: synth, channelPrograms and the shared params.
    // Returns false, publishing nothing, if validation or FluidSynth rejects it.
    bool setChannelProgram(int channel, int bank, int preset);

    // Read-only diagnostics for tests. Zero-based channels.
    bool getControllerValue(int channel, int controller, int& value) const;
    bool getPitchBend(int channel, int& value) const;
    bool getPitchWheelSensitivity(int channel, int& semitones) const;
    bool getChannelProgram(int channel, int& bank, int& preset) const;
    bool getAppliedChannelProgram(int channel, int& bank, int& preset) const;
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
    // Unsupported host rates render silence rather than mis-pitched audio.
    bool isSampleRateSupported() const;
    // Shared by the file player's note chase and the synth reset path.
    static bool isSystemResetSysex(const uint8_t* data, int size);

    // Zero-based view of uiState.selectedChannel.
    int getSelectedChannel() const;

    // Configured vs active voice limit. They must agree: FluidSynth sizes its event
    // queue from the setting.
    bool getConfiguredPolyphony(int& configured, int& active) const;

    // Lets tests install a font bank offset, which Juicy16 never sets itself.
    // Message thread, processing stopped.
    bool getLoadedFontBankOffset(int& offset) const;
    bool setLoadedFontBankOffset(int offset);

    struct VoiceStateCounts {
        int playing{0};
        int on{0};
        int sustained{0};
        int sostenuto{0};
    };

    // Test-only; call with audio processing suspended.
    bool getVoiceStateCounts(int channel, VoiceStateCounts& counts) const;

    // ---- Reverb: controls over FluidSynth's reverb. CC91 sends are untouched.
    enum ReverbParam { reverbSize, reverbDamp, reverbWidth, reverbLevel,
                       numReverbParams };

    // A profile is a named set of values. Selecting one moves the controls; editing
    // a control switches to Custom. Leaves room for a future algorithm without new
    // parameter IDs.
    struct ReverbProfile {
        const char* name;
        float values[numReverbParams];
    };
    static const ReverbProfile reverbProfiles[];
    static int numReverbProfiles();
    // Trailing "Custom" entry; no fixed values.
    static int customReverbProfileIndex();
    static juce::StringArray reverbProfileNames();
    static const String& reverbParamId(int reverbParam);

    // For tests.
    bool isReverbEnabled() const;
    bool getReverbSetting(int reverbParam, double& value) const;

    // Master trim in dB, smoothed after rendering. FluidSynth gain stays fixed.
    void setOutputLevelDb(float decibels);

    // Message thread, after refreshBanks. Feeds VST3 program names.
    std::function<void()> onBanksRefreshed;

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages, bool midiFilePlayback = false);

    // Applies audio-thread program/CC captures to state and UI on the message thread.
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

    // Per-channel property defaults (volume 100, pan 64, others 0), shared with
    // the state reader/writer.
    static int defaultParamValue(const String& parameterID);

    // "volCh1".."panCh16", "muteCh1".."soloCh16" (0-based in, 1-based name).
    static String mixerParamId(int ccIndex, int chZeroBased);
    static String muteParamId(int chZeroBased);
    static String soloParamId(int chZeroBased);

    // "progCh1".."progCh16", and the reverse (-1 if not a program param).
    static String progParamId(int chZeroBased);
    static int progParamChannel(const String& parameterID);

    // Per-channel properties in channelPrograms; one schema for reader and writer.
    static const StringArray perChannelParams;

    // Appended host parameters: order and AU hint are frozen.
    inline static const StringArray chorusParamIds{"chorusOn", "chorusVoices",
        "chorusLevel", "chorusRate", "chorusDepth", "chorusWaveform"};
    enum ChorusParam { chorusOn, chorusVoices, chorusLevel, chorusRate, chorusDepth,
                       chorusWaveform, numChorusParams };
    bool getChorusSetting(int parameter, int group, double& value) const;

    struct ChannelDiagnostics {
        unsigned int midiEvents{0};
        float peak{0.0f};
        int expression{127}, bendRange{256}, pitchBend{8192}, sustain{0}, chorusSend{0}, modulation{0};
        int soundingBank{-1}, soundingPreset{-1};
    };
    ChannelDiagnostics getChannelDiagnostics(int channel) const;
    int rememberedExpression(int channel) const;
    int rememberedBendRange(int channel) const;
    int savedMixerValue(int channel, int index) const;
    void restoreRememberedControllers(int channel, int expression, int range);
    void discardPendingStateUpdates();
    float consumeMasterPeak();
    bool hasOutputOverload() const;
    void clearOutputOverload();

private:
    void restoreSavedChannelState(bool preserveRequestedPrograms = false);
    std::atomic<float> chorusTarget[numChorusParams]{};
    float chorusApplied[numChorusParams]{};
    bool chorusEverApplied{false};
    juce::SmoothedValue<float> chorusSmoother[3]; // level, rate, depth
    void resetChorusToParameters();
    void applyChorusFromAudioThread(int numSamples);
    std::atomic<bool> standardMidiResets{false};
    bool processingMidiFile{false};
    std::atomic<float> channelTrimGain[16];
    juce::SmoothedValue<float> channelTrimSmoother[16];
    std::atomic<float> channelPeak[16];
    // Audio-thread-only cache for identical meter decay calculations. The rate
    // key invalidates it naturally after a synth/sample-rate rebuild.
    int meterDecaySamples{-1};
    float meterDecayRate{0.0f}, meterDecayValue{0.0f};
    struct MeterDecayEntry {
        int samples{-1};
        float value{0.0f};
    };
    std::array<MeterDecayEntry, 8> meterDecayCache{};
    size_t meterDecayCacheNext{0};
    std::atomic<unsigned int> channelMidiEvents[16];
    std::atomic<int> soundingBank[16], soundingPreset[16];
    std::atomic<int> diagnosticExpression[16], diagnosticBendRange[16];
    std::atomic<int> diagnosticBend[16], diagnosticSustain[16];
    std::atomic<float> masterPeak{0.0f};
    std::atomic<bool> outputOverload{false};
    AudioBuffer<float> channelScratch;
    static const StringArray programChangeParams;

    // Set on the thread mirroring engine state into parameters; thread-local so
    // concurrent host automation is not suppressed.
    static thread_local bool mirroringParameters;

    void loadSelectedChannel(int newChannel);
    void dispatchMidiEvent(const MidiMessage& message, int samplePosition);
    // Audio thread. Payload between F0 and F7, straight from the MidiBuffer.
    void dispatchSysEx(const uint8_t* payload, int payloadBytes);
    void renderSamples(AudioBuffer<float>& buffer, int startSample, int numSamples);
    // Audio thread. Renders into the oversampling FIFO (host rate above ceiling).
    void renderIntoFifo(int startSample, int numSamples);
    void renderThroughOversampler(AudioBuffer<float>& buffer,
                                  MidiBuffer& midiMessages,
                                  int numSamples);

    // Renders the gap before each timestamp, then dispatches that timestamp's
    // events via dispatchTimestampGroup. mapPosition maps host positions into the
    // rendered domain (host block or oversampling FIFO).
    template <typename RenderSegment, typename MapPosition>
    void dispatchTimestampedEvents(MidiBuffer& midiMessages,
                                   int numSamples,
                                   int renderLimit,
                                   RenderSegment&& renderSegment,
                                   MapPosition&& mapPosition) {
        int renderPosition{0};
        for (auto it = midiMessages.begin(); it != midiMessages.end();) {
            const int eventPosition{juce::jlimit(0, numSamples, (*it).samplePosition)};
            const int target{juce::jlimit(0, renderLimit, mapPosition(eventPosition))};
            if (target > renderPosition) {
                renderSegment(renderPosition, target - renderPosition);
                renderPosition = target;
            }
            auto groupEnd = it;
            while (groupEnd != midiMessages.end()
                   && juce::jlimit(0, numSamples, (*groupEnd).samplePosition) == eventPosition)
                ++groupEnd;
            dispatchTimestampGroup(it, groupEnd, eventPosition);
            it = groupEnd;
        }
        if (renderLimit > renderPosition)
            renderSegment(renderPosition, renderLimit - renderPosition);
    }

    // One timestamp's events, reordered so the synth can act on them. Under VST3
    // equal timestamps follow the host's parameter-queue order, not the file's:
    // Bank Select can trail its Program Change, Data Entry can precede its RPN,
    // and a reset SysEx can land after controllers meant to follow it. Audio
    // thread; groups beyond the scratch size go in buffer order.
    struct GroupEvent {
        const juce::uint8* data;
        int numBytes;
        int index;          // buffer order, final tiebreak
        juce::uint8 kind;   // GroupKind, in the .cpp
        juce::uint8 channel;
        juce::uint8 cc;
        juce::uint8 value;
        juce::int16 unit;   // RPN ordering keys, see orderChannelRpn
        juce::int16 round;
        juce::int16 subTier;
        juce::int16 ccRank;
    };
    static constexpr int kMaxGroupEvents{2048};
    std::array<GroupEvent, kMaxGroupEvents> groupScratch;
    std::array<int, kMaxGroupEvents> rpnScratch;
    void dispatchTimestampGroup(juce::MidiBufferIterator begin,
                                juce::MidiBufferIterator end,
                                int eventPosition);
    void dispatchGroupEvent(const GroupEvent& event, int eventPosition);
    void orderChannelRpn(int midiCh, int count);

    // RPN selection as FluidSynth tracks it, to recognise bend-range writes.
    // Audio thread.
    int rpnMsb[16];
    int rpnLsb[16];
    bool nrpnActive[16];
    int dataMsb[16];
    int dataLsb[16];
    void noteControllerForBendRange(int channel, int controller, int value);
    void resetRpnTracking(int channel);
    // Last MIDI-set bend range (MSB << 7 | LSB; -1 never). Re-asserted after a
    // reset SysEx: VST3 hosts do not resend unchanged RPNs.
    std::atomic<int> engineBendRange[16];
    // Host bend compensation: forced range, and a multiplier for hosts that
    // shrink bends on import. Both default off.
    std::atomic<int> bendRangeOverride{0}; // semitones; 0 follows MIDI
    std::atomic<bool> bendRangeOverrideDirty{false};
    std::atomic<int> bendScale{1};
    std::atomic<int> vibratoScale[16];
    int appliedVibratoScale[16]{};
    void applyVibratoScaleFromAudioThread();
    void applyBendRangeOverride(int channel);
    void applyBendRangeChangeFromAudioThread();
    // Re-sends a remembered range via RPN, cents included.
    void reassertBendRange(int channel);
    // Last MIDI-set CC11; -1 never. Re-asserted after CC121 and reset SysEx: VST3
    // hosts never resend an unchanged CC11, so attenuated echo channels played at
    // full level on replay.
    std::atomic<int> engineExpression[16];
    void noteControllerForExpression(int channel, int controller, int value);
    void reassertExpression(int channel);
    // Interpolation choice (7th-order, linear, none). FluidSynth stores it per
    // channel and a reset SysEx restores 4th-order, so it is re-asserted after
    // every reset.
    static int interpolationForChoice(int choice);
    std::atomic<int> interpolationMethod{FLUID_INTERP_LINEAR};
    std::atomic<bool> interpolationDirty{false};
    void applyInterpolationMethod();
    void applyInterpolationChangeFromAudioThread();

    // Mirrors a channel's program into progChN without re-applying it. Message thread.
    void syncProgParam(int ch, int preset);

    struct AppliedProgram {
        int rawBank{-1};
        int preset{-1};
    };

    // The only function that changes a channel's program. Exact-bank callers pass
    // a raw engine bank (font offset included); MIDI and progChN callers keep the
    // current Bank Select. Audio-thread callers also queue a state/UI sync.
    bool applyProgramToEngine(int midiCh,
                              int rawBank,
                              int preset,
                              bool retainCurrentBank,
                              bool queueStateSync,
                              AppliedProgram* applied = nullptr);
    void syncAppliedProgramOnMessageThread(int midiCh,
                                           const AppliedProgram& applied);
    void recordProgramApplyFailure(int midiCh);
    // Undoes the basic-channel change CC124-127 make. Audio thread.
    void restoreSixteenChannelLayout(int controller);
    // Message thread only: FluidSynth takes its API lock.
    int loadedFontBankOffset() const;

    // Dense 16-channel material must never steal voices.
    static constexpr int maximumPolyphony{512};
    static constexpr int kNumChannels{16};
    std::atomic<int> diagnosticChorusSend[kNumChannels]{};
    std::atomic<int> diagnosticModulation[kNumChannels]{};
    static constexpr int kNumMixerCcs{2}; // CC7, CC10 (ccIndexOrder)

    // Audio-thread program change (MIDI PC or progChN), captured for
    // handleAsyncUpdate. Never intercept a CC for this: host chase/reset sprays
    // would reset every channel's program.
    void applyProgramChangeFromAudioThread(int midiCh, int program);
    // Program captured on the audio thread, consumed in handleAsyncUpdate.
    std::atomic<int> midiBank[kNumChannels];
    std::atomic<int> midiPreset[kNumChannels];
    std::atomic<unsigned int> midiProgramDirtyMask{0}; // bit per channel
    // Sticky per-channel failure bits for paths that cannot return an error.
    std::atomic<unsigned int> programApplyFailureMask{0};

    // Last engine program per channel (raw bank + preset), re-asserted right
    // after a reset SysEx.
    std::atomic<int> engineBank[kNumChannels];
    std::atomic<int> enginePreset[kNumChannels];

    // Mixer CC values captured on the audio thread (-1 none pending); written to
    // the ValueTree in handleAsyncUpdate.
    std::atomic<int> midiCcValue[kNumChannels][kNumMixerCcs];
    std::atomic<unsigned int> midiCcDirtyMask{0}; // bit per channel
    // Last mixer CC values sent to the synth; reset restoration reads only these.
    std::atomic<int> engineCc[kNumChannels][kNumMixerCcs];
    // Reverb targets come from any thread; smoothers and applied state are
    // audio-thread only.
    std::atomic<bool> reverbEnabledTarget{false};
    std::atomic<float> reverbTarget[numReverbParams];
    bool reverbEnabledApplied{false};
    bool reverbEverApplied{false};
    float reverbApplied[numReverbParams]{};
    juce::SmoothedValue<float> reverbSmoother[numReverbParams];
    // Profile/Custom reconciliation pending for the message thread; -1 none.
    std::atomic<int> pendingReverbProfile{-1};
    std::atomic<bool> pendingReverbCustom{false};
    // Mute/solo changed off the message thread; refresh the rows' silenced look.
    std::atomic<bool> pendingMuteSoloSync{false};
    // Suppress only this thread's own profile writes, leaving automation on
    // another thread free to select Custom. The pointer also isolates instances.
    static thread_local const FluidSynthModel* applyingReverbProfile;
    // Audio thread, once per block before rendering.
    void applyReverbFromAudioThread(int numSamples);
    // Renders dry audio and mixes the effects bus on top.
    void renderWithEffects(float* const* outputs, int numSamples);
    // Preallocated effects bus.
    AudioBuffer<float> effectsScratch;
    // Rebound only when prepareToPlay resizes scratch.
    std::array<float*, 64> effectOutputs{};
    std::array<float*, 32> dryOutputs{};
    // Message thread: reseed engine and smoothers from the parameters.
    void resetReverbToParameters();

    // Mute/solo from any thread; the derived mask is read per note-on.
    std::atomic<unsigned int> muteMask{0};
    std::atomic<unsigned int> soloMask{0};
    std::atomic<unsigned int> silencedMask{0};
    static unsigned int deriveSilencedMask(unsigned int mutes, unsigned int solos);
    // Recomputes silencedMask and sends all-notes-off to newly silenced channels.
    // Audio-thread safe.
    void refreshSilencedMask();
    // Master trim from any thread; smoothed on the audio thread.
    std::atomic<float> outputLevelGain{1.0f};
    juce::SmoothedValue<float> outputLevelSmoother;
    // Input trace for the conformance suite; diagnostics only.
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
    // Allocation-free parser for "<prefix><1..16>" IDs (may run on the audio thread).
    enum class ChannelParamKind { none, volume, pan, mute, solo };
    static int channelSuffixOf(const String& parameterID,
                               const char* prefix,
                               int prefixLength);
    static ChannelParamKind parseChannelParam(const String& parameterID,
                                              int& chZeroBased);
    static int ccToIndex(int cc); // -1 if not a mixer CC


    static const map<fluid_midi_control_change, String> ccToChannelProperty;
    static const map<String, fluid_midi_control_change> channelPropertyToCc;

    void refreshBanks();
    void createSynth();
    void reloadFontFromState();

    AudioProcessorValueTreeState& valueTreeState;
    // Set while rolling back path/bookmark after a rejected load, so the rollback
    // is not treated as a new load.
    bool suppressFontStateReload{false};
    bool deferFontSelectionRollback{false};
    void loadFontFromSelectionBookmark();
    void restoreActiveFontSelection();

    // Declaration order matters: the synth is destroyed before its settings.
    unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings;
    unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth;

    // Engine render rate: the host rate, or host / oversampleFactor above the ceiling.
    float currentSampleRate;
    float hostSampleRate{44100.0f};
    std::atomic<bool> sampleRateSupported{true};

    bool unloadAndLoadFont(const String& absPath);
    static bool riffContainerOverrunsFile(const juce::File& src);
    void publishFontLoadResult(bool success,
                               const String& requestedPath,
                               const String& message,
                               bool repaired);

    // Writes a repaired copy of a malformed DLS to a temp file kept for the font's
    // lifetime; invalid File if no repair was needed.
    juce::File writeRepairedTempCopy(const juce::File& src);
    void clearRepairedTemp();
    juce::File repairedTempFile;

    std::atomic<int> sfont_id;
    // Channel being edited. Message thread writes, audio thread reads.
    std::atomic<unsigned int> channel;

    // Stereo scratch for mono output buses; FluidSynth renders stereo only.
    AudioBuffer<float> stereoScratch;

    // Above FluidSynth's 96 kHz ceiling the engine runs at an integer fraction of
    // the host rate and is interpolated back up. 1 = unused.
    int oversampleFactor{1};
    AudioBuffer<float> oversampleFifo;
    // Rendered but unconsumed samples carried to the next block.
    int oversampleFifoFill{0};
    // Host frames covered by rendered engine samples but not yet output. This
    // includes fractional input frames already held by the interpolator.
    juce::int64 oversampleRenderAhead{0};
    juce::LagrangeInterpolator oversampleInterpolators[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidSynthModel)
};
