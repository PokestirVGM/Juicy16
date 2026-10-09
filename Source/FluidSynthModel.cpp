#include <iostream>
#include <iterator>
#include <cstring>
#include <array>
#include <algorithm>
#include <limits>
#include <cmath>
#include <fluidsynth.h>
#include "FluidSynthModel.h"

#if defined(__aarch64__) && defined(__clang__)
  #include <arm_neon.h>
#endif

#ifndef FLUIDSYNTH_JUICY16_VIBRATO_SCALE
#error "Rebuild the FluidSynth dependency with tools/build_macos_dependencies.sh or tools/build_windows_dependencies.ps1 (Juicy16 vibrato extension required)."
#endif
#ifndef FLUIDSYNTH_JUICY16_DLS_FULL_PAN
#error "Rebuild the FluidSynth dependency with tools/build_macos_dependencies.sh or tools/build_windows_dependencies.ps1 (Juicy16 DLS pan extension required)."
#endif
#include "MidiConstants.h"
#include "Util.h"
#include "GuiConstants.h"

#if JUCE_MAC || JUCE_IOS
  #include <CoreFoundation/CFString.h>
  #include <CoreFoundation/CFData.h>
  #include <CoreFoundation/CFURL.h>
  #include <CoreFoundation/CFError.h>
  #include <juce_core/native/juce_CFHelpers_mac.h>
  using juce::CFUniquePtr;
#endif

using namespace std;

#include "DlsRepair.h"

// Balance (CC8/CC40) is traced but never reaches the engine, as in Fruity LSD.
// FluidSynth's balance cuts up to 96 dB, survives CC121, and overwhelms pan on
// stereo-pair banks.
static bool reachesEngine(int controller) {
    return controller != BALANCE_MSB && controller != BALANCE_LSB;
}

// Mixer CCs use FluidSynth's default modulators. MIDI stays authoritative: the
// next CC7/CC10 replaces an editor value at its timestamp. Maps each CC to its
// channelPrograms property; mixerParamId bridges to volChN/panChN.
const map<fluid_midi_control_change, String> FluidSynthModel::ccToChannelProperty{
    {VOLUME_MSB, "volume"}, // MIDI CC 7 Channel Volume
    {PAN_MSB, "pan"}};      // MIDI CC 10 Pan

const map<String, fluid_midi_control_change> FluidSynthModel::channelPropertyToCc{[]{
    map<String, fluid_midi_control_change> map;
    transform(
        ccToChannelProperty.begin(),
        ccToChannelProperty.end(),
        inserter(map, map.begin()),
        [](const pair<fluid_midi_control_change, String>& pair) {
            return make_pair(pair.second, pair.first);
        });
    return map;
}()};

// Fixed index order for the audio-thread capture arrays.
const fluid_midi_control_change FluidSynthModel::ccIndexOrder[FluidSynthModel::kNumMixerCcs]{
    VOLUME_MSB, PAN_MSB};
thread_local bool FluidSynthModel::mirroringParameters{false};
thread_local const FluidSynthModel* FluidSynthModel::applyingReverbProfile{nullptr};


int FluidSynthModel::ccToIndex(int cc) {
    for (int i = 0; i < kNumMixerCcs; ++i)
        if (static_cast<int>(ccIndexOrder[i]) == cc)
            return i;
    return -1;
}

void FluidSynthModel::setOutputLevelDb(float decibels) {
    // The bottom of the range is -inf, so automation can reach silence.
    const float gain{decibels <= GuiConstants::outputLevelMinDb
        ? 0.0f
        : juce::Decibels::decibelsToGain(decibels)};
    outputLevelGain.store(gain, std::memory_order_relaxed);
}

int FluidSynthModel::defaultParamValue(const String& parameterID) {
    // GM defaults: volume 100, pan 64; everything else 0.
    if (parameterID == "volume")
        return MidiConstants::defaultChannelVolume;
    if (parameterID == "pan")
        return MidiConstants::centreValue;
    return programChangeParams.contains(parameterID) ? 0 : 64;
}

// Profiles are designed settings over FluidSynth's FDN reverb, never named after
// hardware they do not emulate. Tuned on game rips (FluidSynth's defaults are
// about twice as wet); see docs/CONTROLLER_SUPPORT.md.
const FluidSynthModel::ReverbProfile FluidSynthModel::reverbProfiles[]{
    // size  damp  width level
    {"Universal", {0.45f, 0.35f, 0.85f, 0.55f}},
    // "Soft", not the proposed "SNS": the name is reserved for a real SNES echo
    // emulation. Owner decision pending.
    {"Soft",      {0.20f, 0.60f, 1.00f, 0.55f}},
    {"Custom",    {0.45f, 0.35f, 0.85f, 0.55f}},
};

int FluidSynthModel::numReverbProfiles() {
    return static_cast<int>(sizeof(reverbProfiles) / sizeof(reverbProfiles[0]));
}

int FluidSynthModel::customReverbProfileIndex() {
    return numReverbProfiles() - 1;
}

juce::StringArray FluidSynthModel::reverbProfileNames() {
    juce::StringArray names;
    for (int i = 0; i < numReverbProfiles(); ++i)
        names.add(reverbProfiles[i].name);
    return names;
}

const String& FluidSynthModel::reverbParamId(int reverbParam) {
    // The constructor registers all four listeners before audio can run, so
    // these strings are created there and never allocated in parameterChanged.
    static const std::array<String, numReverbParams> ids{
        "reverbSize", "reverbDamp", "reverbWidth", "reverbLevel"};
    if (juce::isPositiveAndBelow(reverbParam, numReverbParams))
        return ids[static_cast<size_t>(reverbParam)];
    jassertfalse;
    static const String empty;
    return empty;
}

String FluidSynthModel::mixerParamId(int ccIndex, int chZeroBased) {
    jassert(ccIndex >= 0 && ccIndex < kNumMixerCcs);
    return (ccIndexOrder[ccIndex] == VOLUME_MSB ? "volCh" : "panCh")
        + String(chZeroBased + 1);
}

String FluidSynthModel::muteParamId(int chZeroBased) {
    return "muteCh" + String(chZeroBased + 1);
}

String FluidSynthModel::soloParamId(int chZeroBased) {
    return "soloCh" + String(chZeroBased + 1);
}

// Allocation-free, because parameterChanged may run on the audio thread.
int FluidSynthModel::channelSuffixOf(const String& parameterID,
                                     const char* prefix,
                                     int prefixLength) {
    const int length{parameterID.length()};
    if (length < prefixLength + 1 || length > prefixLength + 2
        || !parameterID.startsWith(prefix))
        return -1;
    int oneBased{0};
    if (length == prefixLength + 1
        && parameterID[prefixLength] >= '1' && parameterID[prefixLength] <= '9') {
        oneBased = static_cast<int>(parameterID[prefixLength] - '0');
    } else if (length == prefixLength + 2
               && parameterID[prefixLength] == '1'
               && parameterID[prefixLength + 1] >= '0'
               && parameterID[prefixLength + 1] <= '6') {
        oneBased = 10 + static_cast<int>(parameterID[prefixLength + 1] - '0');
    }
    return oneBased > 0 ? oneBased - 1 : -1;
}

FluidSynthModel::ChannelParamKind FluidSynthModel::parseChannelParam(
    const String& parameterID, int& chZeroBased) {
    // Ordered by first character so a mismatch costs one comparison.
    if ((chZeroBased = channelSuffixOf(parameterID, "volCh", 5)) >= 0)
        return ChannelParamKind::volume;
    if ((chZeroBased = channelSuffixOf(parameterID, "panCh", 5)) >= 0)
        return ChannelParamKind::pan;
    if ((chZeroBased = channelSuffixOf(parameterID, "muteCh", 6)) >= 0)
        return ChannelParamKind::mute;
    if ((chZeroBased = channelSuffixOf(parameterID, "soloCh", 6)) >= 0)
        return ChannelParamKind::solo;
    return ChannelParamKind::none;
}

String FluidSynthModel::progParamId(int chZeroBased) {
    return "progCh" + String(chZeroBased + 1);
}

int FluidSynthModel::progParamChannel(const String& parameterID) {
    return channelSuffixOf(parameterID, "progCh", 6);
}

FluidSynthModel::FluidSynthModel(
    AudioProcessorValueTreeState& state
    )
: valueTreeState{state}
, settings{nullptr, nullptr}
, synth{nullptr, nullptr}
, currentSampleRate{44100}
, sfont_id{-1}
, channel{0}
{
    for (int i = 0; i < kNumChannels; i++) {
        midiBank[i].store(i == 9 ? 128 : 0, std::memory_order_relaxed);
        midiPreset[i].store(0, std::memory_order_relaxed);
        engineBank[i].store(i == 9 ? 128 : 0, std::memory_order_relaxed);
        enginePreset[i].store(0, std::memory_order_relaxed);
        lastNoteOnBank[i].store(-1, std::memory_order_relaxed);
        lastNoteOnPreset[i].store(-1, std::memory_order_relaxed);
        lastNoteOnSample[i].store(-1, std::memory_order_relaxed);
        lastChannelPressureValue[i].store(-1, std::memory_order_relaxed);
        lastChannelPressureSample[i].store(-1, std::memory_order_relaxed);
        for (int value = 0; value < 128; ++value) {
            lastCcValue[i][value].store(-1, std::memory_order_relaxed);
            lastCcSample[i][value].store(-1, std::memory_order_relaxed);
            lastKeyPressureValue[i][value].store(-1, std::memory_order_relaxed);
            lastKeyPressureSample[i][value].store(-1, std::memory_order_relaxed);
        }
        engineBendRange[i].store(-1, std::memory_order_relaxed);
        engineExpression[i].store(-1, std::memory_order_relaxed);
        channelTrimGain[i].store(1.0f);
        vibratoScale[i].store(1);
        channelPeak[i].store(0.0f);
        channelMidiEvents[i].store(0);
        soundingBank[i].store(-1);
        soundingPreset[i].store(-1);
        diagnosticExpression[i].store(127);
        diagnosticBendRange[i].store(256);
        diagnosticBend[i].store(8192);
        diagnosticSustain[i].store(0);
        resetRpnTracking(i);
        for (int c = 0; c < kNumMixerCcs; c++) {
            midiCcValue[i][c].store(-1, std::memory_order_relaxed);
            // Seeded from the per-property GM defaults; a reset SysEx re-asserts these.
            engineCc[i][c].store(
                defaultParamValue(ccToChannelProperty.at(ccIndexOrder[c])),
                std::memory_order_relaxed);
        }
    }
    valueTreeState.addParameterListener("bank", this);
    valueTreeState.addParameterListener("preset", this);
    valueTreeState.addParameterListener("outputLevel", this);
    // Seed from the parameter: parameterChanged fires only on a change, so the
    // +1.5 dB default was never applied.
    if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
            valueTreeState.getParameter("outputLevel"))})
        setOutputLevelDb(p->get());
    for (const auto& id : chorusParamIds)
        valueTreeState.addParameterListener(id, this);
    valueTreeState.addParameterListener("resetPolicy", this);
    for (int ch = 0; ch < 16; ++ch)
        valueTreeState.addParameterListener("trimCh" + String(ch + 1), this);
    valueTreeState.addParameterListener("bendRange", this);
    valueTreeState.addParameterListener("bendScale", this);
    // Seeded explicitly: parameterChanged only fires on a change.
    valueTreeState.addParameterListener("interpolation", this);
    if (auto* p{valueTreeState.getRawParameterValue("interpolation")})
        interpolationMethod.store(interpolationForChoice(juce::roundToInt(p->load())));
    for (int ch = 1; ch <= 16; ++ch)
        valueTreeState.addParameterListener("vibratoScaleCh" + String(ch), this);
    valueTreeState.addParameterListener("cc1VibratoScale", this);
    valueTreeState.addParameterListener("cc1VibratoRate", this);
    valueTreeState.addParameterListener("reverbOn", this);
    valueTreeState.addParameterListener("reverbProfile", this);
    for (int i = 0; i < numReverbParams; ++i) {
        reverbTarget[i].store(reverbProfiles[0].values[i], std::memory_order_relaxed);
        valueTreeState.addParameterListener(reverbParamId(i), this);
    }
    for (int ch = 0; ch < kNumChannels; ch++) {
        valueTreeState.addParameterListener(progParamId(ch), this);
        for (int idx = 0; idx < kNumMixerCcs; idx++)
            valueTreeState.addParameterListener(mixerParamId(idx, ch), this);
        valueTreeState.addParameterListener(muteParamId(ch), this);
        valueTreeState.addParameterListener(soloParamId(ch), this);
    }
    valueTreeState.state.addListener(this);
}

FluidSynthModel::~FluidSynthModel() {
    cancelPendingUpdate();
    for (const auto& id : chorusParamIds)
        valueTreeState.removeParameterListener(id, this);
    valueTreeState.removeParameterListener("resetPolicy", this);
    for (int ch = 0; ch < 16; ++ch)
        valueTreeState.removeParameterListener("trimCh" + String(ch + 1), this);
    clearRepairedTemp();
    for (int ch = 0; ch < kNumChannels; ch++) {
        valueTreeState.removeParameterListener(progParamId(ch), this);
    }
    for (int ch = 0; ch < kNumChannels; ch++) {
        for (int idx = 0; idx < kNumMixerCcs; idx++)
            valueTreeState.removeParameterListener(mixerParamId(idx, ch), this);
        valueTreeState.removeParameterListener(muteParamId(ch), this);
        valueTreeState.removeParameterListener(soloParamId(ch), this);
    }
    valueTreeState.removeParameterListener("bank", this);
    valueTreeState.removeParameterListener("preset", this);
    valueTreeState.removeParameterListener("outputLevel", this);
    valueTreeState.removeParameterListener("bendRange", this);
    valueTreeState.removeParameterListener("bendScale", this);
    valueTreeState.removeParameterListener("interpolation", this);
    for (int ch = 1; ch <= 16; ++ch)
        valueTreeState.removeParameterListener("vibratoScaleCh" + String(ch), this);
    valueTreeState.removeParameterListener("cc1VibratoScale", this);
    valueTreeState.removeParameterListener("cc1VibratoRate", this);
    valueTreeState.removeParameterListener("reverbOn", this);
    valueTreeState.removeParameterListener("reverbProfile", this);
    for (int i = 0; i < numReverbParams; ++i)
        valueTreeState.removeParameterListener(reverbParamId(i), this);
    valueTreeState.state.removeListener(this);
}

void FluidSynthModel::initialise() {
    // No audio drivers: Juicy16 only renders blocks, and FL Studio deadlocked when
    // FluidSynth initialised CoreAudio.
    const char *DRV[] {nullptr};
    fluid_audio_driver_register(DRV);
    
    settings = { new_fluid_settings(), delete_fluid_settings };
    
    fluid_settings_setnum(settings.get(), "synth.sample-rate", currentSampleRate);
    // Pinned GS Bank Select: CC0 selects the bank for the next Program Change; CC32
    // is stored but ignored.
    fluid_settings_setstr(settings.get(), "synth.midi-bank-select", "gs");
    // UI program changes can overlap rendering; keep FluidSynth's API lock.
    fluid_settings_setint(settings.get(), "synth.threadsafe-api", 1);
    // Must be a setting, not fluid_synth_set_polyphony: the rvoice event queue is
    // sized once from it (polyphony * 64). Raising it later overflowed the queue
    // above ~256 voices.
    fluid_settings_setint(settings.get(), "synth.polyphony", maximumPolyphony);
    // 16 internal stems for per-channel trims and meters; still one stereo output
    // with identical effects on every group.
    fluid_settings_setint(settings.get(), "synth.audio-channels", 16);
    fluid_settings_setint(settings.get(), "synth.audio-groups", 16);
    fluid_settings_setint(settings.get(), "synth.effects-groups", 16);
    createSynth();
}

void FluidSynthModel::createSynth() {
    synth = { new_fluid_synth(settings.get()), delete_fluid_synth };
    std::fill(std::begin(appliedVibratoScale), std::end(appliedVibratoScale), 0);
    applyVibratoScaleFromAudioThread();
    appliedVibratoRate = 0;
    applyVibratoRateFromAudioThread();

    // Interpolation comes from the setting; a reset SysEx re-applies it.
    applyInterpolationMethod();

    // FluidSynth's default gain. At 1.0 real rips clipped (+7.3 dBFS), which flattened
    // dynamics and pan. Users raise level with outputLevel instead.
    fluid_synth_set_gain(synth.get(), 0.2f);

    // Chorus is opt-in; CC93 (default 0) and bank routing stay native.
    resetChorusToParameters();
    for (auto& send : diagnosticChorusSend) send.store(0);
    for (auto& value : diagnosticModulation) value.store(0);

    // A new synth starts from the parameters, not FluidSynth's defaults.
    resetReverbToParameters();

    // GM default reverb send, which FluidSynth does not apply.
    for (int ch = 0; ch < kNumChannels; ++ch)
        fluid_synth_cc(synth.get(), ch, static_cast<int>(EFFECTS_DEPTH1),
                       MidiConstants::defaultReverbSend);

    // A new synth is at +-2 semitones; processBlock re-applies the override.
    bendRangeOverrideDirty.store(true, std::memory_order_release);

    // No custom modulators. Juicy16's former CC71-79 modulators were far out of scale
    // on SF2 and inert on DLS; every CC still reaches the synth.
}


bool FluidSynthModel::getVoiceStateCounts(int requestedChannel,
                                          VoiceStateCounts& counts) const
{
    if (requestedChannel < 0 || requestedChannel >= kNumChannels || synth == nullptr)
        return false;

    counts = {};
    std::array<fluid_voice_t*, 513> voices{};
    fluid_synth_get_voicelist(
        synth.get(), voices.data(), static_cast<int>(voices.size() - 1), -1);
    for (auto* voice : voices) {
        if (voice == nullptr)
            break;
        if (fluid_voice_get_channel(voice) != requestedChannel)
            continue;
        ++counts.playing;
        counts.on += fluid_voice_is_on(voice) != 0 ? 1 : 0;
        counts.sustained += fluid_voice_is_sustained(voice) != 0 ? 1 : 0;
        counts.sostenuto += fluid_voice_is_sostenuto(voice) != 0 ? 1 : 0;
    }
    return true;
}

void FluidSynthModel::prepareToPlay(double sampleRate, int samplesPerBlock) {
    setSampleRate(static_cast<float>(sampleRate));
    // Rejected rates still need safe smoother initialization. A nonfinite host
    // rate would otherwise be converted to an integer ramp length inside JUCE.
    const double smoothingRate{sampleRateSupported.load() ? sampleRate : currentSampleRate};
    // 20 ms: long enough to hide steps, short enough to feel immediate.
    outputLevelSmoother.reset(smoothingRate, 0.02);
    outputLevelSmoother.setCurrentAndTargetValue(
        outputLevelGain.load(std::memory_order_relaxed));
    // Reverb glides per block: FluidSynth takes settings, not signals.
    for (int i = 0; i < numReverbParams; ++i)
        reverbSmoother[i].reset(smoothingRate, 0.02);
    resetReverbToParameters();
    // Preallocate scratch off the audio thread.
    stereoScratch.setSize(2, jmax(64, samplesPerBlock), false, false, true);
    // renderSamples chunks against this capacity, so oversized host blocks never
    // overrun or allocate.
    effectsScratch.setSize(32, jmax(64, samplesPerBlock), false, false, true);
    channelScratch.setSize(32, jmax(64, samplesPerBlock), false, false, true);
    for (int i = 0; i < 32; ++i) {
        dryOutputs[static_cast<size_t>(i)] = channelScratch.getWritePointer(i);
        // Each group's effects add into the same stereo scratch.
        const int group = i / 2, side = i % 2;
        effectOutputs[static_cast<size_t>(4 * group + side)] = effectsScratch.getWritePointer(i);
        effectOutputs[static_cast<size_t>(4 * group + 2 + side)] = effectsScratch.getWritePointer(i);
    }
    for (int ch = 0; ch < 16; ++ch) {
        channelTrimSmoother[ch].reset(currentSampleRate, 0.02);
        channelTrimSmoother[ch].setCurrentAndTargetValue(channelTrimGain[ch].load());
        channelPeak[ch].store(0.0f);
    }
    masterPeak.store(0.0f);
    outputOverload.store(false);
    // One block of internal samples, the interpolator's read-ahead and leftovers.
    oversampleFifo.setSize(
        2, jmax(64, samplesPerBlock / jmax(1, oversampleFactor) + 8), false, true, true);
    oversampleFifoFill = 0;
    oversampleRenderAhead = 0;
    for (auto& interpolator : oversampleInterpolators)
        interpolator.reset();
}

const StringArray FluidSynthModel::programChangeParams{"bank", "preset"};
const StringArray FluidSynthModel::perChannelParams{
    "bank", "preset", "volume", "pan", "mute", "solo"};

void FluidSynthModel::syncProgParam(int ch, int preset) {
    juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
    if (auto* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter(progParamId(ch)))})
        *p = preset;
}

void FluidSynthModel::recordProgramApplyFailure(int midiCh) {
    if (midiCh >= 0 && midiCh < kNumChannels)
        programApplyFailureMask.fetch_or(1u << midiCh, std::memory_order_release);
}

bool FluidSynthModel::applyProgramToEngine(int midiCh,
                                           int rawBank,
                                           int preset,
                                           bool retainCurrentBank,
                                           bool queueStateSync,
                                           AppliedProgram* applied) {
    if (midiCh < 0 || midiCh >= kNumChannels
        || preset < MidiConstants::midiMinValue
        || preset > MidiConstants::midiMaxValue
        || (!retainCurrentBank && rawBank < MidiConstants::midiMinValue)) {
        recordProgramApplyFailure(midiCh);
        return false;
    }

    const int fontId{sfont_id.load(std::memory_order_acquire)};
    if (fontId == -1) {
        recordProgramApplyFailure(midiCh);
        return false;
    }

    int result{retainCurrentBank
        ? fluid_synth_program_change(synth.get(), midiCh, preset)
        : fluid_synth_program_select(synth.get(), midiCh, fontId, rawBank, preset)};
    // A drum channel searches only the percussion bank; if it finds nothing (e.g. a
    // kit at 0:0 without the drum flag), fall back to the melodic bank.
    if (retainCurrentBank && fluid_synth_get_channel_preset(synth.get(), midiCh) == nullptr) {
        const int melodicBank{loadedFontBankOffset()};
        result = fluid_synth_program_select(synth.get(), midiCh, fontId, melodicBank, preset);
        if (result != FLUID_OK)
            result = fluid_synth_program_select(synth.get(), midiCh, fontId, melodicBank, 0);
    }
    if (result != FLUID_OK) {
        recordProgramApplyFailure(midiCh);
        return false;
    }

    int actualFont{-1};
    AppliedProgram actual;
    if (fluid_synth_get_program(
            synth.get(), midiCh, &actualFont, &actual.rawBank, &actual.preset) != FLUID_OK) {
        recordProgramApplyFailure(midiCh);
        return false;
    }

    if (auto* voicePreset = fluid_synth_get_channel_preset(synth.get(), midiCh)) {
        soundingBank[midiCh].store(fluid_preset_get_banknum(voicePreset));
        soundingPreset[midiCh].store(fluid_preset_get_num(voicePreset));
    } else {
        soundingBank[midiCh].store(-1);
        soundingPreset[midiCh].store(-1);
    }
    midiBank[midiCh].store(actual.rawBank, std::memory_order_relaxed);
    midiPreset[midiCh].store(actual.preset, std::memory_order_relaxed);
    engineBank[midiCh].store(actual.rawBank, std::memory_order_relaxed);
    enginePreset[midiCh].store(actual.preset, std::memory_order_relaxed);
    if (applied != nullptr)
        *applied = actual;

    if (queueStateSync) {
        midiProgramDirtyMask.fetch_or(1u << midiCh, std::memory_order_release);
        triggerAsyncUpdate();
    }
    return true;
}

void FluidSynthModel::syncAppliedProgramOnMessageThread(
    int midiCh, const AppliedProgram& applied) {
    if (midiCh < 0 || midiCh >= kNumChannels)
        return;

    const int fontId{sfont_id.load(std::memory_order_acquire)};
    const int bankOffset{fontId == -1
        ? 0 : fluid_synth_get_bank_offset(synth.get(), fontId)};
    const int logicalBank{applied.rawBank - bankOffset};
    ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
        .getChildWithProperty("num", midiCh)};
    if (chNode.isValid()) {
        chNode.setProperty("bank", logicalBank, nullptr);
        chNode.setProperty("preset", applied.preset, nullptr);
    }

    syncProgParam(midiCh, applied.preset);
    if (midiCh == static_cast<int>(channel.load(std::memory_order_relaxed))) {
        juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
        if (auto* bankParam{
                dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("bank"))};
            bankParam != nullptr && logicalBank >= MidiConstants::midiMinValue
            && logicalBank <= MidiConstants::maxChannelBank)
            *bankParam = logicalBank;
        if (auto* presetParam{
                dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("preset"))})
            *presetParam = applied.preset;
    }
}

void FluidSynthModel::parameterChanged(const String& parameterID, float /*newValue*/) {
    // Parameter writes that only mirror engine state (mirroringParameters) are not
    // re-sent to the synth or saved back.
    if (const int index = chorusParamIds.indexOf(parameterID); index >= 0) {
        chorusTarget[index].store(valueTreeState.getRawParameterValue(parameterID)->load(),
                                  std::memory_order_relaxed);
        return;
    }
    if (parameterID.startsWith("vibratoScaleCh")) {
        const int ch = channelSuffixOf(parameterID, "vibratoScaleCh", 14);
        if (ch >= 0 && ch < 16)
            vibratoScale[ch].store(juce::jlimit(1, 24, juce::roundToInt(
                valueTreeState.getRawParameterValue(parameterID)->load())));
        return;
    }
    if (parameterID == "cc1VibratoScale") {
        globalVibratoScale.store(juce::jlimit(1, maxVibratoScale, juce::roundToInt(
            valueTreeState.getRawParameterValue(parameterID)->load())));
        return;
    }
    if (parameterID == "cc1VibratoRate") {
        vibratoRate.store(juce::jlimit(0, numVibratoRates - 1, juce::roundToInt(
            valueTreeState.getRawParameterValue(parameterID)->load())));
        return;
    }
    if (parameterID == "resetPolicy") {
        standardMidiResets.store(valueTreeState.getRawParameterValue(parameterID)->load() > 0.5f);
        return;
    }
    if (parameterID == "interpolation") {
        // May arrive on the audio thread; processBlock applies it.
        interpolationMethod.store(interpolationForChoice(juce::roundToInt(
            valueTreeState.getRawParameterValue(parameterID)->load())));
        interpolationDirty.store(true, std::memory_order_release);
        return;
    }
    if (parameterID.startsWith("trimCh")) {
        const int ch = channelSuffixOf(parameterID, "trimCh", 6);
        if (ch >= 0 && ch < 16)
            channelTrimGain[ch].store(juce::Decibels::decibelsToGain(
                valueTreeState.getRawParameterValue(parameterID)->load()));
        return;
    }
    if (parameterID == "outputLevel") {
        // May arrive on the audio thread; store only, processBlock smooths.
        if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
                valueTreeState.getParameter(parameterID))})
            setOutputLevelDb(p->get());
        return;
    }
    if (parameterID == "bendRange" || parameterID == "bendScale") {
        // May arrive on the audio thread; store only.
        int value{0};
        if (auto* p{dynamic_cast<AudioParameterInt*>(
                valueTreeState.getParameter(parameterID))})
            value = p->get();
        if (parameterID == "bendScale") {
            bendScale.store(juce::jmax(1, value), std::memory_order_relaxed);
        } else {
            bendRangeOverride.store(juce::jmax(0, value), std::memory_order_relaxed);
            bendRangeOverrideDirty.store(true, std::memory_order_release);
        }
        return;
    }
    // May arrive on the audio thread; store only, processBlock applies.
    if (parameterID == "reverbOn") {
        if (auto* p{dynamic_cast<juce::AudioParameterBool*>(
                valueTreeState.getParameter(parameterID))})
            reverbEnabledTarget.store(p->get(), std::memory_order_relaxed);
        return;
    }
    if (parameterID == "reverbProfile") {
        if (applyingReverbProfile == this)
            return;
        int profile{0};
        if (auto* p{dynamic_cast<juce::AudioParameterChoice*>(
                valueTreeState.getParameter(parameterID))})
            profile = p->getIndex();
        // The most recent user action wins. A new profile supersedes an older
        // manual edit; choosing Custom cancels a profile not yet applied.
        pendingReverbCustom.store(false, std::memory_order_release);
        pendingReverbProfile.store(profile != customReverbProfileIndex() ? profile : -1,
                                  std::memory_order_release);
        if (profile != customReverbProfileIndex()) {
            triggerAsyncUpdate();
        }
        return;
    }
    for (int i = 0; i < numReverbParams; ++i) {
        if (parameterID != reverbParamId(i))
            continue;
        if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
                valueTreeState.getParameter(parameterID))})
            reverbTarget[i].store(p->get(), std::memory_order_relaxed);
        // A manual edit switches the selection to Custom, unless applying a profile.
        if (applyingReverbProfile != this) {
            pendingReverbProfile.store(-1, std::memory_order_release);
            pendingReverbCustom.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }
        return;
    }
    if (mirroringParameters)
        return;
    if (int progCh{progParamChannel(parameterID)}; progCh >= 0) {
        // progChN may arrive on the audio thread: treat it like a MIDI Program Change
        // and let the message thread mirror the result.
        int program{0};
        if (auto* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter(parameterID))})
            program = p->get();
        applyProgramToEngine(progCh, 0, program, true, true);
        return;
    }
    if (programChangeParams.contains(parameterID)) {
        int bank, preset;
        {
            RangedAudioParameter *param{valueTreeState.getParameter("bank")};
            jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
            AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
            bank = castParam->get();
        }
        {
            RangedAudioParameter *param{valueTreeState.getParameter("preset")};
            jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
            AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
            preset = castParam->get();
        }
        const unsigned int ch{channel.load(std::memory_order_relaxed)};
        int bankOffset{0};
        const int fontId{sfont_id.load(std::memory_order_acquire)};
        if (ch < static_cast<unsigned int>(kNumChannels) && fontId != -1) {
            bankOffset = fluid_synth_get_bank_offset(synth.get(), fontId);
            AppliedProgram applied;
            const bool onMessageThread{juce::MessageManager::existsAndIsCurrentThread()};
            if (applyProgramToEngine(
                    static_cast<int>(ch), bankOffset + bank, preset, false,
                    !onMessageThread, &applied)
                && onMessageThread)
                syncAppliedProgramOnMessageThread(static_cast<int>(ch), applied);
        }
        return;
    }
    int paramChannel{-1};
    switch (parseChannelParam(parameterID, paramChannel)) {
        case ChannelParamKind::volume:
        case ChannelParamKind::pan: {
            // Knob, automation and incoming CC7/CC10 all end at the engine, channelPrograms
            // and the parameter. MIDI wins because it writes last, at its timestamp.
            const int controllerNumber{static_cast<int>(
                parameterID.startsWith("volCh") ? VOLUME_MSB : PAN_MSB)};
            int value{0};
            if (auto* p{dynamic_cast<AudioParameterInt*>(
                    valueTreeState.getParameter(parameterID))})
                value = p->get();
            setChannelControllerValue(paramChannel, controllerNumber, value);
            const int idx{ccToIndex(controllerNumber)};
            if (idx < 0)
                return;
            if (juce::MessageManager::existsAndIsCurrentThread()) {
                ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
                    .getChildWithProperty("num", paramChannel)};
                if (chNode.isValid())
                    chNode.setProperty(
                        ccToChannelProperty.at(ccIndexOrder[idx]), value, nullptr);
            } else {
                // Audio thread: defer the tree write.
                midiCcValue[paramChannel][idx].store(value, std::memory_order_relaxed);
                midiCcDirtyMask.fetch_or(1u << paramChannel, std::memory_order_release);
                triggerAsyncUpdate();
            }
            return;
        }
        case ChannelParamKind::mute:
        case ChannelParamKind::solo: {
            const bool isMute{parameterID.startsWith("muteCh")};
            bool engaged{false};
            if (auto* p{dynamic_cast<juce::AudioParameterBool*>(
                    valueTreeState.getParameter(parameterID))})
                engaged = p->get();
            std::atomic<unsigned int>& mask{isMute ? muteMask : soloMask};
            const unsigned int bit{1u << paramChannel};
            if (engaged)
                mask.fetch_or(bit, std::memory_order_relaxed);
            else
                mask.fetch_and(~bit, std::memory_order_relaxed);
            refreshSilencedMask();
            if (juce::MessageManager::existsAndIsCurrentThread()) {
                ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
                    .getChildWithProperty("num", paramChannel)};
                if (chNode.isValid())
                    chNode.setProperty(isMute ? "mute" : "solo",
                                       engaged ? 1 : 0, nullptr);
            } else {
                // Audio thread: defer the tree write so the rows' silenced look updates.
                pendingMuteSoloSync.store(true, std::memory_order_release);
                triggerAsyncUpdate();
            }
            return;
        }
        case ChannelParamKind::none:
            break;
    }
}

void FluidSynthModel::syncMixerParamsFromState() {
    ValueTree chPrograms{valueTreeState.state.getChildWithName("channelPrograms")};
    unsigned int mutes{0};
    unsigned int solos{0};
    for (int ch = 0; ch < kNumChannels; ch++) {
        ValueTree chNode{chPrograms.getChildWithProperty("num", ch)};
        if (!chNode.isValid())
            continue;
        // Values are already in the tree; the engine gets them on font load.
        juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
        for (int idx = 0; idx < kNumMixerCcs; idx++) {
            const String& property{ccToChannelProperty.at(ccIndexOrder[idx])};
            const int value{juce::jlimit(
                MidiConstants::midiMinValue, MidiConstants::midiMaxValue,
                static_cast<int>(
                    chNode.getProperty(property, defaultParamValue(property))))};
            if (auto* p{dynamic_cast<AudioParameterInt*>(
                    valueTreeState.getParameter(mixerParamId(idx, ch)))})
                *p = value;
            engineCc[ch][idx].store(value, std::memory_order_relaxed);
        }
        const bool muted{static_cast<int>(chNode.getProperty("mute", 0)) != 0};
        const bool soloed{static_cast<int>(chNode.getProperty("solo", 0)) != 0};
        if (muted)
            mutes |= 1u << ch;
        if (soloed)
            solos |= 1u << ch;
        if (auto* p{dynamic_cast<juce::AudioParameterBool*>(
                valueTreeState.getParameter(muteParamId(ch)))})
            *p = muted;
        if (auto* p{dynamic_cast<juce::AudioParameterBool*>(
                valueTreeState.getParameter(soloParamId(ch)))})
            *p = soloed;
    }
    muteMask.store(mutes, std::memory_order_relaxed);
    soloMask.store(solos, std::memory_order_relaxed);
    refreshSilencedMask();
}

void FluidSynthModel::syncToSelectedChannel() {
    int sel{static_cast<int>(valueTreeState.state.getChildWithName("uiState")
        .getProperty("selectedChannel", 1)) - 1};
    loadSelectedChannel(juce::jlimit(0, kNumChannels - 1, sel));
}

void FluidSynthModel::finishStateRestore(bool restoreChannelRecords) {
    // A parameter need not emit a change when recalling the same project. MIDI
    // may have changed the engine since the last UI mirror, so explicitly apply
    // its saved channel records even when the bank was not reloaded.
    discardPendingStateUpdates();
    if (restoreChannelRecords)
        restoreSavedChannelState(true);
    else
        for (int ch = 0; ch < kNumChannels; ++ch)
            for (int idx = 0; idx < kNumMixerCcs; ++idx)
                setChannelControllerValue(ch, ccIndexOrder[idx], savedMixerValue(ch, idx));

    // The saved control values outrank an asynchronously selected profile.
    // Reconcile its label without allowing a delayed profile write to overwrite
    // a custom edit saved before the message thread ran.
    auto* profile = dynamic_cast<juce::AudioParameterChoice*>(
        valueTreeState.getParameter("reverbProfile"));
    if (profile != nullptr && profile->getIndex() < customReverbProfileIndex()) {
        bool matches{true};
        for (int i = 0; i < numReverbParams; ++i)
            matches = matches && std::abs(valueTreeState.getRawParameterValue(reverbParamId(i))->load()
                - reverbProfiles[profile->getIndex()].values[i]) < 1.0e-5f;
        if (!matches) {
            const juce::ScopedValueSetter<const FluidSynthModel*> guard{applyingReverbProfile, this};
            *profile = customReverbProfileIndex();
        }
    }
}

void FluidSynthModel::selectChannelForEditing(int selectedChannel) {
    valueTreeState.state.getChildWithName("uiState").setProperty(
        "selectedChannel",
        juce::jlimit(0, kNumChannels - 1, selectedChannel) + 1,
        nullptr);
}

bool FluidSynthModel::setChannelProgram(int chan, int bank, int preset) {
    if (chan < 0 || chan >= kNumChannels
        || bank < MidiConstants::midiMinValue || bank > MidiConstants::maxChannelBank
        || preset < MidiConstants::midiMinValue || preset > MidiConstants::midiMaxValue) {
        recordProgramApplyFailure(chan);
        return false;
    }
    const int fontId{sfont_id.load(std::memory_order_acquire)};
    if (fontId == -1) {
        recordProgramApplyFailure(chan);
        return false;
    }

    AppliedProgram applied;
    const bool onMessageThread{juce::MessageManager::existsAndIsCurrentThread()};
    if (!applyProgramToEngine(
            chan, fluid_synth_get_bank_offset(synth.get(), fontId) + bank, preset,
            false, !onMessageThread, &applied))
        return false;
    if (onMessageThread)
        syncAppliedProgramOnMessageThread(chan, applied);
    return true;
}

void FluidSynthModel::handleAsyncUpdate() {
    // Profile/Custom reconciliation, on the message thread; applyingReverbProfile
    // stops the two directions chasing each other.
    if (const int profile{pendingReverbProfile.exchange(-1, std::memory_order_acquire)};
        profile >= 0 && profile < customReverbProfileIndex()) {
        const juce::ScopedValueSetter<const FluidSynthModel*> guard{applyingReverbProfile, this};
        for (int i = 0; i < numReverbParams; ++i) {
            const float value{reverbProfiles[profile].values[i]};
            reverbTarget[i].store(value, std::memory_order_relaxed);
            if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
                    valueTreeState.getParameter(reverbParamId(i)))})
                *p = value;
        }
    }
    if (pendingReverbCustom.exchange(false, std::memory_order_acquire)) {
        const juce::ScopedValueSetter<const FluidSynthModel*> guard{applyingReverbProfile, this};
        if (auto* p{dynamic_cast<juce::AudioParameterChoice*>(
                valueTreeState.getParameter("reverbProfile"))};
            p != nullptr && p->getIndex() != customReverbProfileIndex())
            *p = customReverbProfileIndex();
    }

    if (pendingMuteSoloSync.exchange(false, std::memory_order_acquire)) {
        const unsigned int mutes{muteMask.load(std::memory_order_relaxed)};
        const unsigned int solos{soloMask.load(std::memory_order_relaxed)};
        ValueTree chPrograms{valueTreeState.state.getChildWithName("channelPrograms")};
        for (int ch = 0; ch < kNumChannels; ++ch) {
            ValueTree chNode{chPrograms.getChildWithProperty("num", ch)};
            if (!chNode.isValid())
                continue;
            chNode.setProperty("mute", (mutes & (1u << ch)) != 0 ? 1 : 0, nullptr);
            chNode.setProperty("solo", (solos & (1u << ch)) != 0 ? 1 : 0, nullptr);
        }
    }

    // Apply audio-thread captures to state and parameters on the message thread.
    const unsigned int pcMask{midiProgramDirtyMask.exchange(0, std::memory_order_acquire)};
    const unsigned int ccMask{midiCcDirtyMask.exchange(0, std::memory_order_acquire)};
    if (pcMask == 0 && ccMask == 0)
        return;
    ValueTree chPrograms{valueTreeState.state.getChildWithName("channelPrograms")};

    if (pcMask != 0) {
        for (int ch = 0; ch < kNumChannels; ch++) {
            if ((pcMask & (1u << ch)) == 0)
                continue;
            syncAppliedProgramOnMessageThread(
                ch,
                {midiBank[ch].load(std::memory_order_relaxed),
                 midiPreset[ch].load(std::memory_order_relaxed)});
        }
    }

    for (int ch = 0; ch < kNumChannels && ccMask != 0; ch++) {
        if ((ccMask & (1u << ch)) == 0)
            continue;
        ValueTree chNode{chPrograms.getChildWithProperty("num", ch)};
        for (int idx = 0; idx < kNumMixerCcs; idx++) {
            const int value{midiCcValue[ch][idx].exchange(-1, std::memory_order_relaxed)};
            if (value < 0)
                continue; // nothing pending for this CC
            if (chNode.isValid())
                chNode.setProperty(
                    ccToChannelProperty.at(ccIndexOrder[idx]), value, nullptr);
            // Every row has its own parameter. The guard stops the engine being re-sent a
            // value it already has.
            juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
            if (auto* p{dynamic_cast<AudioParameterInt*>(
                    valueTreeState.getParameter(mixerParamId(idx, ch)))})
                *p = value;
        }
    }
}

void FluidSynthModel::loadSelectedChannel(int newChannel) {
    newChannel = juce::jlimit(0, kNumChannels - 1, newChannel);
    channel.store(static_cast<unsigned int>(newChannel), std::memory_order_relaxed);
    ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
        .getChildWithProperty("num", newChannel)};
    if (!chNode.isValid())
        return;
    // Point the shared bank/preset params at the newly selected channel without
    // re-sending or saving back.
    juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
    for (const String& p : programChangeParams) {
        AudioParameterInt* param{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter(p))};
        if (param)
            *param = static_cast<int>(chNode.getProperty(p, defaultParamValue(p)));
    }
}

void FluidSynthModel::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                               const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() == StringRef("uiState")
        && property == StringRef("selectedChannel")) {
        int newChannel{static_cast<int>(treeWhosePropertyHasChanged.getProperty("selectedChannel", 1)) - 1};
        loadSelectedChannel(juce::jlimit(0, kNumChannels - 1, newChannel));
        return;
    }
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")) {
        if (suppressFontStateReload)
            return;
#if JUCE_MAC || JUCE_IOS
        if (property == StringRef("path")) {
            // Path-only state is valid without a bookmark; if one exists, wait for it so
            // sandbox access is in place first.
            MemoryBlock emptyBookmark;
            const var bookmark{treeWhosePropertyHasChanged.getProperty("bookmark", emptyBookmark)};
            if (bookmark.isBinaryData() && bookmark.getBinaryData()->isEmpty()) {
                const String path{treeWhosePropertyHasChanged.getProperty("path", "")};
                if (path.isNotEmpty())
                    unloadAndLoadFont(path);
            }
        }
        if (property == StringRef("bookmark"))
            loadFontFromSelectionBookmark();
#else
        if (property == StringRef("path")) {
            String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
            if (soundFontPath.isNotEmpty()) {
                unloadAndLoadFont(soundFontPath);
            }
        }
#endif
    }
}

void FluidSynthModel::restoreFontSelection(const String& path,
                                           const juce::MemoryBlock& bookmark,
                                           bool forceReload) {
    ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
    if (!fontState.isValid())
        return;
    juce::MemoryBlock emptyBookmark;
    const var previousBookmark{fontState.getProperty("bookmark", emptyBookmark)};
    const bool pathChanged{fontState.getProperty("path", "").toString() != path};
    const bool bookmarkChanged{!previousBookmark.isBinaryData()
        || *previousBookmark.getBinaryData() != bookmark};
    if (!forceReload && !pathChanged && !bookmarkChanged)
        return;
    {
        juce::ScopedValueSetter<bool> suppress{suppressFontStateReload, true};
        fontState.setProperty("path", path, nullptr);
        fontState.setProperty("bookmark", bookmark, nullptr);
    }
#if JUCE_MAC || JUCE_IOS
    loadFontFromSelectionBookmark();
#else
    if ((forceReload || pathChanged) && path.isNotEmpty())
        unloadAndLoadFont(path);
#endif
}

void FluidSynthModel::loadFontFromSelectionBookmark() {
#if JUCE_MAC || JUCE_IOS
    ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
    juce::MemoryBlock emptyBookmark;
    const var bookmark{fontState.getProperty("bookmark", emptyBookmark)};
    const String fallbackPath{fontState.getProperty("path", "").toString()};
    jassert(bookmark.isBinaryData());
    bool loaded{false}, attempted{false};
    {
        // A rejected bookmark candidate must not roll the saved fallback path
        // back to the old active bank before that fallback has been attempted.
        juce::ScopedValueSetter<bool> defer{deferFontSelectionRollback, true};
        CFErrorRef error{nullptr};
        String bookmarkPath;
        if (bookmark.isBinaryData() && !bookmark.getBinaryData()->isEmpty()) {
            CFUniquePtr<CFDataRef> data{CFDataCreate(nullptr,
                static_cast<const UInt8*>(bookmark.getBinaryData()->getData()),
                static_cast<CFIndex>(bookmark.getBinaryData()->getSize()))};
            Boolean stale{false};
            CFUniquePtr<CFURLRef> url{CFURLCreateByResolvingBookmarkData(nullptr,
                data.get(), kCFURLBookmarkResolutionWithSecurityScope, nullptr,
                nullptr, &stale, &error)};
            if (url) {
                CFUniquePtr<CFStringRef> resolved{CFURLCopyFileSystemPath(
                    url.get(), kCFURLPOSIXPathStyle)};
                bookmarkPath = String::fromCFString(resolved.get());
                if (bookmarkPath.isNotEmpty()) {
                    CFURLStartAccessingSecurityScopedResource(url.get());
                    attempted = true;
                    loaded = unloadAndLoadFont(bookmarkPath);
                    CFURLStopAccessingSecurityScopedResource(url.get());
                }
            }
            fontState.setProperty("bookmarkStale", stale != 0, nullptr);
        }
        if (error != nullptr)
            CFRelease(error);
        if (!loaded && fallbackPath.isNotEmpty() && fallbackPath != bookmarkPath) {
            attempted = true;
            loaded = unloadAndLoadFont(fallbackPath);
        }
    }
    if (attempted && !loaded)
        restoreActiveFontSelection();
#endif
}

void FluidSynthModel::setControllerValue(int controller, int value) {
    const auto ch{channel.load(std::memory_order_relaxed)};
    if (ch >= static_cast<unsigned int>(kNumChannels)
        || !juce::isPositiveAndBelow(controller, 128)
        || !juce::isPositiveAndBelow(value, 128))
        return;
    if (reachesEngine(controller)) {
        fluid_synth_cc(synth.get(), static_cast<int>(ch), controller, value);
        reapplyVibratoRateAfterController(static_cast<int>(ch), controller);
    }
    if (controller == 1 || controller == 121) diagnosticModulation[ch].store(controller == 1 ? value : 0);
    if (const int idx{ccToIndex(controller)}; idx >= 0)
        engineCc[ch][idx].store(value, std::memory_order_relaxed);
}

void FluidSynthModel::setChannelControllerValue(int channelToWrite, int controller, int value) {
    if (channelToWrite < 0 || channelToWrite >= kNumChannels
        || !juce::isPositiveAndBelow(controller, 128)
        || !juce::isPositiveAndBelow(value, 128))
        return;
    if (reachesEngine(controller)) {
        fluid_synth_cc(synth.get(), channelToWrite, controller, value);
        reapplyVibratoRateAfterController(channelToWrite, controller);
    }
    if (controller == 1 || controller == 121) diagnosticModulation[channelToWrite].store(controller == 1 ? value : 0);
    if (const int idx{ccToIndex(controller)}; idx >= 0)
        engineCc[channelToWrite][idx].store(value, std::memory_order_relaxed);
}

unsigned int FluidSynthModel::deriveSilencedMask(unsigned int mutes, unsigned int solos) {
    // Sounds if not muted and (nothing soloed or soloed). Mute always wins.
    constexpr unsigned int all{(1u << kNumChannels) - 1u};
    return (mutes | (solos != 0 ? ~solos : 0u)) & all;
}

void FluidSynthModel::refreshSilencedMask() {
    const unsigned int updated{deriveSilencedMask(
        muteMask.load(std::memory_order_relaxed),
        soloMask.load(std::memory_order_relaxed))};
    const unsigned int previous{silencedMask.exchange(updated, std::memory_order_release)};
    // Usually release envelopes without clicks. Pedals can hold note-off voices
    // indefinitely; cut those voices while preserving the file's pedal values.
    for (int ch = 0; ch < kNumChannels; ++ch)
        if ((updated & ~previous & (1u << ch)) != 0) {
            int sustain{0}, sostenuto{0};
            fluid_synth_get_cc(synth.get(), ch, 64, &sustain);
            fluid_synth_get_cc(synth.get(), ch, 66, &sostenuto);
            if (sustain >= 64 || sostenuto >= 64)
                fluid_synth_all_sounds_off(synth.get(), ch);
            else
                fluid_synth_all_notes_off(synth.get(), ch);
        }
}

bool FluidSynthModel::isChannelSilenced(int channelToRead) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    return (silencedMask.load(std::memory_order_acquire) & (1u << channelToRead)) != 0;
}

unsigned int FluidSynthModel::getSilencedMask() const {
    return silencedMask.load(std::memory_order_acquire);
}

void FluidSynthModel::resetChorusToParameters() {
    for (int i = 0; i < numChorusParams; ++i)
        chorusTarget[i].store(valueTreeState.getRawParameterValue(chorusParamIds[i])->load());
    for (int i = 0; i < 3; ++i) {
        chorusSmoother[i].reset(currentSampleRate, 0.02);
        chorusSmoother[i].setCurrentAndTargetValue(chorusTarget[chorusLevel + i].load());
    }
    chorusEverApplied = false;
    applyChorusFromAudioThread(0);
}

void FluidSynthModel::applyChorusFromAudioThread(int numSamples) {
    if (synth == nullptr) return;
    for (int i = 0; i < numChorusParams; ++i) {
        float value = chorusTarget[i].load(std::memory_order_relaxed);
        if (i >= chorusLevel && i <= chorusDepth) {
            auto& smoother = chorusSmoother[i - chorusLevel];
            if (!juce::exactlyEqual(value, smoother.getTargetValue()))
                smoother.setTargetValue(value);
            value = smoother.skip(numSamples);
        }
        if (chorusEverApplied && std::abs(value - chorusApplied[i]) < 1.0e-5f)
            continue;
        switch (i) {
            case chorusOn: fluid_synth_chorus_on(synth.get(), -1, value > 0.5f ? 1 : 0); break;
            case chorusVoices: fluid_synth_set_chorus_group_nr(synth.get(), -1, static_cast<int>(value)); break;
            case chorusLevel: fluid_synth_set_chorus_group_level(synth.get(), -1, value); break;
            case chorusRate: fluid_synth_set_chorus_group_speed(synth.get(), -1, value); break;
            case chorusDepth: fluid_synth_set_chorus_group_depth(synth.get(), -1, value); break;
            case chorusWaveform: fluid_synth_set_chorus_group_type(synth.get(), -1, static_cast<int>(value)); break;
            default: break;
        }
        chorusApplied[i] = value;
    }
    chorusEverApplied = true;
}

bool FluidSynthModel::getChorusSetting(int parameter, int group, double& value) const {
    if (synth == nullptr || group < 0 || group >= kNumChannels) return false;
    int integer{0};
    switch (parameter) {
        case chorusVoices:
            if (fluid_synth_get_chorus_group_nr(synth.get(), group, &integer) != FLUID_OK) return false;
            value = integer; return true;
        case chorusLevel: return fluid_synth_get_chorus_group_level(synth.get(), group, &value) == FLUID_OK;
        case chorusRate: return fluid_synth_get_chorus_group_speed(synth.get(), group, &value) == FLUID_OK;
        case chorusDepth: return fluid_synth_get_chorus_group_depth(synth.get(), group, &value) == FLUID_OK;
        case chorusWaveform:
            if (fluid_synth_get_chorus_group_type(synth.get(), group, &integer) != FLUID_OK) return false;
            value = integer; return true;
        default: return false;
    }
}

void FluidSynthModel::applyReverbFromAudioThread(int numSamples) {
    fluid_synth_t* const synthesizer{synth.get()};
    if (synthesizer == nullptr)
        return;

    // Bypass disables the unit, so no inaudible tail is computed.
    const bool enabled{reverbEnabledTarget.load(std::memory_order_relaxed)};
    if (!reverbEverApplied || enabled != reverbEnabledApplied) {
        fluid_synth_reverb_on(synthesizer, -1, enabled ? 1 : 0);
        reverbEnabledApplied = enabled;
    }

    for (int i = 0; i < numReverbParams; ++i) {
        const float target{reverbTarget[i].load(std::memory_order_relaxed)};
        if (!juce::exactlyEqual(target, reverbSmoother[i].getTargetValue()))
            reverbSmoother[i].setTargetValue(target);
        // One value per block; the smoother hides automation steps.
        reverbSmoother[i].skip(numSamples);
        const float value{reverbSmoother[i].getCurrentValue()};
        // Write only settings that moved.
        if (reverbEverApplied && std::abs(value - reverbApplied[i]) < 1.0e-4f)
            continue;
        switch (i) {
            case reverbSize:
                fluid_synth_set_reverb_group_roomsize(synthesizer, -1, value); break;
            case reverbDamp:
                fluid_synth_set_reverb_group_damp(synthesizer, -1, value); break;
            case reverbWidth:
                fluid_synth_set_reverb_group_width(synthesizer, -1, value); break;
            case reverbLevel:
                fluid_synth_set_reverb_group_level(synthesizer, -1, value); break;
            default: break;
        }
        reverbApplied[i] = value;
    }
    reverbEverApplied = true;
}

void FluidSynthModel::resetReverbToParameters() {
    if (auto* p{dynamic_cast<juce::AudioParameterBool*>(
            valueTreeState.getParameter("reverbOn"))})
        reverbEnabledTarget.store(p->get(), std::memory_order_relaxed);
    for (int i = 0; i < numReverbParams; ++i) {
        float value{reverbProfiles[0].values[i]};
        if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
                valueTreeState.getParameter(reverbParamId(i)))})
            value = p->get();
        reverbTarget[i].store(value, std::memory_order_relaxed);
        // Jump: a new synth or rate change has nothing to glide from.
        reverbSmoother[i].setCurrentAndTargetValue(value);
    }
    reverbEverApplied = false;
}

bool FluidSynthModel::isReverbEnabled() const {
    return reverbEnabledTarget.load(std::memory_order_relaxed);
}

bool FluidSynthModel::getReverbSetting(int reverbParam, double& value) const {
    if (synth == nullptr || reverbParam < 0 || reverbParam >= numReverbParams)
        return false;
    switch (reverbParam) {
        case reverbSize:
            return fluid_synth_get_reverb_group_roomsize(synth.get(), 0, &value) == FLUID_OK;
        case reverbDamp:
            return fluid_synth_get_reverb_group_damp(synth.get(), 0, &value) == FLUID_OK;
        case reverbWidth:
            return fluid_synth_get_reverb_group_width(synth.get(), 0, &value) == FLUID_OK;
        case reverbLevel:
            return fluid_synth_get_reverb_group_level(synth.get(), 0, &value) == FLUID_OK;
        default: break;
    }
    return false;
}

bool FluidSynthModel::getControllerValue(int channelToRead, int controller, int& value) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels
        || !juce::isPositiveAndBelow(controller, 128))
        return false;
    return fluid_synth_get_cc(synth.get(), channelToRead, controller, &value) == FLUID_OK;
}

bool FluidSynthModel::getPitchBend(int channelToRead, int& value) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    return fluid_synth_get_pitch_bend(synth.get(), channelToRead, &value) == FLUID_OK;
}

bool FluidSynthModel::getPitchWheelSensitivity(int channelToRead, int& semitones) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    return fluid_synth_get_pitch_wheel_sens(synth.get(), channelToRead, &semitones) == FLUID_OK;
}

int FluidSynthModel::loadedFontBankOffset() const {
    int offset{0};
    return getLoadedFontBankOffset(offset) ? offset : 0;
}

bool FluidSynthModel::getAppliedChannelProgram(int ch, int& bank, int& preset) const {
    if (ch < 0 || ch >= kNumChannels || sfont_id.load() < 0) return false;
    // Only completed selections are state; Bank Select alone is pending.
    bank = engineBank[ch].load() - loadedFontBankOffset();
    preset = enginePreset[ch].load();
    return true;
}

bool FluidSynthModel::getChannelProgram(int channelToRead, int& bank, int& preset) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    int soundFontId{-1};
    int rawBank{0};
    if (fluid_synth_get_program(
            synth.get(), channelToRead, &soundFontId, &rawBank, &preset) != FLUID_OK)
        return false;
    // In the font's own bank numbering, like channelPrograms.
    bank = rawBank - loadedFontBankOffset();
    return true;
}

unsigned int FluidSynthModel::getProgramApplyFailureMask() const {
    return programApplyFailureMask.load(std::memory_order_acquire);
}

bool FluidSynthModel::getLastDispatchedController(
    int channelToRead, int controller, int& value, int& sample) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels
        || !juce::isPositiveAndBelow(controller, 128))
        return false;
    value = lastCcValue[channelToRead][controller].load(std::memory_order_relaxed);
    sample = lastCcSample[channelToRead][controller].load(std::memory_order_relaxed);
    return value >= 0 && sample >= 0;
}

bool FluidSynthModel::getLastDispatchedNoteOnProgram(
    int channelToRead, int& bank, int& preset, int& sample) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    // Offset conversion here, where taking the API lock is safe.
    const int rawBank{lastNoteOnBank[channelToRead].load(std::memory_order_relaxed)};
    preset = lastNoteOnPreset[channelToRead].load(std::memory_order_relaxed);
    sample = lastNoteOnSample[channelToRead].load(std::memory_order_relaxed);
    if (rawBank < 0 || preset < 0 || sample < 0)
        return false;
    bank = rawBank - loadedFontBankOffset();
    return true;
}

bool FluidSynthModel::getLastDispatchedChannelPressure(
    int channelToRead, int& value, int& sample) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    value = lastChannelPressureValue[channelToRead].load(std::memory_order_relaxed);
    sample = lastChannelPressureSample[channelToRead].load(std::memory_order_relaxed);
    return value >= 0 && sample >= 0;
}

bool FluidSynthModel::getLastDispatchedKeyPressure(
    int channelToRead, int key, int& value, int& sample) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels
        || !juce::isPositiveAndBelow(key, 128))
        return false;
    value = lastKeyPressureValue[channelToRead][key].load(std::memory_order_relaxed);
    sample = lastKeyPressureSample[channelToRead][key].load(std::memory_order_relaxed);
    return value >= 0 && sample >= 0;
}

String FluidSynthModel::getFontLoadStatus() const {
    return valueTreeState.state.getChildWithName("soundFont")
        .getProperty("loadStatus", "idle").toString();
}

// Runtime only: the bookmark resolved but its target moved.
bool FluidSynthModel::isBookmarkStale() const {
    return static_cast<bool>(valueTreeState.state.getChildWithName("soundFont")
        .getProperty("bookmarkStale", false));
}

String FluidSynthModel::getFontLoadMessage() const {
    return valueTreeState.state.getChildWithName("soundFont")
        .getProperty("loadMessage", "").toString();
}

String FluidSynthModel::getLastAttemptedFontPath() const {
    return valueTreeState.state.getChildWithName("soundFont")
        .getProperty("lastAttemptedPath", "").toString();
}

String FluidSynthModel::getLoadedFontPath() const {
    return valueTreeState.state.getChildWithName("soundFont")
        .getProperty("loadedPath", "").toString();
}

// Read from the settings, which size the rvoice event queue.
bool FluidSynthModel::getConfiguredPolyphony(int& configured, int& active) const {
    if (settings == nullptr || synth == nullptr)
        return false;
    if (fluid_settings_getint(settings.get(), "synth.polyphony", &configured) != FLUID_OK)
        return false;
    active = fluid_synth_get_polyphony(synth.get());
    return active > 0;
}

// CC124-127 make FluidSynth disable all but a group of channels (a single CC124
// on channel 1 silences the rest). The CC still reaches the engine; this
// restores FluidSynth's default layout: one OMNION_POLY basic channel at 0
// covering all channels. Audio thread.
void FluidSynthModel::restoreSixteenChannelLayout(int controller) {
    if (controller < MidiConstants::firstChannelModeCc || synth == nullptr)
        return;
    fluid_synth_reset_basic_channel(synth.get(), -1);
    fluid_synth_set_basic_channel(
        synth.get(), 0, FLUID_CHANNEL_MODE_OMNION_POLY, 0);
}

int FluidSynthModel::getSelectedChannel() const {
    return static_cast<int>(channel.load(std::memory_order_relaxed));
}

bool FluidSynthModel::isSampleRateSupported() const {
    return sampleRateSupported.load(std::memory_order_acquire);
}

bool FluidSynthModel::getLoadedFontBankOffset(int& offset) const {
    const int fontId{sfont_id.load(std::memory_order_acquire)};
    if (fontId == -1 || synth == nullptr)
        return false;
    offset = fluid_synth_get_bank_offset(synth.get(), fontId);
    return true;
}

bool FluidSynthModel::setLoadedFontBankOffset(int offset) {
    const int fontId{sfont_id.load(std::memory_order_acquire)};
    if (fontId == -1 || synth == nullptr)
        return false;
    return fluid_synth_set_bank_offset(synth.get(), fontId, offset) == FLUID_OK;
}

bool FluidSynthModel::unloadAndLoadFont(const String& absPath) {
    const juce::File requested{absPath};
    if (absPath.isEmpty() || !requested.existsAsFile()) {
        publishFontLoadResult(
            false, absPath,
            "The selected bank file is missing or unreadable. Check that it is still "
            "in place and that you have permission to read it.",
            false);
        return false;
    }

    // Reject an incomplete or unrelated file before either backend loader opens
    // it. In particular, a rejected short bank must remain replaceable on Windows.
    bool recognisedHeader{false};
    {
        juce::FileInputStream input{requested};
        char header[12]{};
        recognisedHeader = !input.failedToOpen() && input.read(header, sizeof(header)) == sizeof(header)
            && std::memcmp(header, "RIFF", 4) == 0
            && (std::memcmp(header + 8, "sfbk", 4) == 0 || std::memcmp(header + 8, "DLS ", 4) == 0);
    }
    if (!recognisedHeader) {
        publishFontLoadResult(false, absPath,
            "This file is not a readable SF2, SF3, or DLS bank. Choose a supported bank or re-export it.", false);
        return false;
    }

    String pathToLoad{absPath};
    // Repair into a temp candidate; the model owns it only once FluidSynth accepts it.
    juce::File repaired{writeRepairedTempCopy(requested)};
    if (repaired.existsAsFile())
        pathToLoad = repaired.getFullPathName();

    // Reject RIFF containers larger than the file that repair could not fix;
    // FluidSynth can spend minutes parsing them on the message thread.
    if (!repaired.existsAsFile() && riffContainerOverrunsFile(requested)) {
        publishFontLoadResult(
            false, absPath,
            "This bank's RIFF header claims more data than the file contains, so it "
            "is truncated or corrupt. Try re-exporting or downloading it again.",
            false);
        return false;
    }

    // reset_presets=0 keeps the old bank playable until the new one is proven.
    const int candidateId{fluid_synth_sfload(
        synth.get(), pathToLoad.toRawUTF8(), 0)};
    fluid_sfont_t* candidate{candidateId == FLUID_FAILED
        ? nullptr : fluid_synth_get_sfont_by_id(synth.get(), candidateId)};
    bool hasPreset{false};
    if (candidate != nullptr) {
        fluid_sfont_iteration_start(candidate);
        hasPreset = fluid_sfont_iteration_next(candidate) != nullptr;
    }

    if (candidateId == FLUID_FAILED || !hasPreset) {
        if (candidateId != FLUID_FAILED)
            fluid_synth_sfunload(synth.get(), candidateId, 0);
        if (repaired.existsAsFile())
            repaired.deleteFile();
        publishFontLoadResult(
            false, absPath,
            candidateId == FLUID_FAILED
                ? "FluidSynth could not load this SF2, SF3, or DLS bank. It may be "
                  "corrupt or an unsupported variant; try another bank."
                : "The selected bank contains no playable presets. Choose a bank "
                  "that contains at least one instrument.",
            false);
        return false;
    }

    const int previousId{sfont_id.exchange(candidateId, std::memory_order_acq_rel)};
    if (previousId != -1 && previousId != candidateId)
        fluid_synth_sfunload(synth.get(), previousId, 0);
    clearRepairedTemp();
    repairedTempFile = repaired;
    refreshBanks();
    publishFontLoadResult(
        true, absPath,
        repaired.existsAsFile()
            ? "Bank loaded from a safe repaired temporary DLS copy."
            : "Bank loaded successfully.",
        repaired.existsAsFile());
    return true;
}

void FluidSynthModel::publishFontLoadResult(bool success,
                                            const String& requestedPath,
                                            const String& message,
                                            bool repaired) {
    ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
    if (!fontState.isValid())
        return;
    fontState.setProperty("loadStatus", success ? "loaded" : "error", nullptr);
    fontState.setProperty("loadMessage", message, nullptr);
    fontState.setProperty("lastAttemptedPath", requestedPath, nullptr);
    if (success) {
        fontState.setProperty("loadedPath", requestedPath, nullptr);
        MemoryBlock emptyBookmark;
        fontState.setProperty(
            "loadedBookmark", fontState.getProperty("bookmark", emptyBookmark), nullptr);
        fontState.setProperty("usedDlsRepair", repaired, nullptr);
        return;
    }

    if (!deferFontSelectionRollback)
        restoreActiveFontSelection();
}

void FluidSynthModel::restoreActiveFontSelection() {
    // Keep an unsuccessful selection out of saved state and rate-change reloads.
    ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
    const String loadedPath{fontState.getProperty("loadedPath", "").toString()};
    if (loadedPath.isNotEmpty()) {
        juce::ScopedValueSetter<bool> suppress{suppressFontStateReload, true};
        MemoryBlock emptyBookmark;
        fontState.setProperty("path", loadedPath, nullptr);
        fontState.setProperty(
            "bookmark", fontState.getProperty("loadedBookmark", emptyBookmark), nullptr);
    }
}

// True if a RIFF header claims more payload than the file holds.
bool FluidSynthModel::riffContainerOverrunsFile(const juce::File& src) {
    juce::uint8 header[8] = {};
    {
        juce::FileInputStream in{src};
        if (in.failedToOpen() || in.read(header, sizeof(header)) != sizeof(header))
            return false;
    }
    if (std::memcmp(header, "RIFF", 4) != 0)
        return false;
    const juce::uint32 declared{
        static_cast<juce::uint32>(header[4])
        | (static_cast<juce::uint32>(header[5]) << 8)
        | (static_cast<juce::uint32>(header[6]) << 16)
        | (static_cast<juce::uint32>(header[7]) << 24)};
    return static_cast<juce::int64>(declared) + 8 > src.getSize();
}

juce::File FluidSynthModel::writeRepairedTempCopy(const juce::File& src) {
    // Bound every allocation, including if the file grows between preflight
    // and repair. Larger banks still go to FluidSynth's streaming loader.
    static constexpr juce::int64 maxRepairableBytes{512ll * 1024 * 1024};
    const juce::int64 initialSize{src.getSize()};
    if (initialSize < 0 || initialSize > maxRepairableBytes)
        return {};
    const auto initialModified{src.getLastModificationTime()};
    {
        juce::FileInputStream input{src};
        if (!input.failedToOpen() && input.getTotalLength() == initialSize) {
            size_t headerReads{0};
            const auto scan{juicysf::dlsRepairNeeded(static_cast<size_t>(initialSize),
                [&input, &headerReads](size_t offset, uint8_t* destination, size_t count) {
                    // Pathological files may contain millions of empty chunks.
                    // Bound seeks, then use the original in-memory repair path.
                    if (++headerReads > 4096)
                        return false;
                    return input.setPosition(static_cast<juce::int64>(offset))
                        && input.read(destination, static_cast<int>(count)) == static_cast<int>(count);
                })};
            if (scan == juicysf::DlsRepairScan::notNeeded
                && src.getSize() == initialSize
                && src.getLastModificationTime() == initialModified)
                return {}; // Healthy DLS: do not copy its sample payload.
        }
    }

    // Read failure or changed file metadata is indeterminate, not proof that
    // repair is unnecessary. Reopen and re-evaluate the actual bounded bytes.
    juce::FileInputStream input{src};
    if (input.failedToOpen())
        return {};
    const juce::int64 size{input.getTotalLength()};
    if (size < 12 || size > maxRepairableBytes)
        return {};
    juce::MemoryBlock bytes{static_cast<size_t>(size)};
    size_t completed{0};
    while (completed < bytes.getSize()) {
        const int count{static_cast<int>(std::min<size_t>(65536, bytes.getSize() - completed))};
        const int received{input.read(static_cast<uint8_t*>(bytes.getData()) + completed, count)};
        if (received <= 0)
            return {};
        completed += static_cast<size_t>(received);
    }
    uint8_t extra{0};
    if (input.read(&extra, 1) != 0)
        return {}; // Grew while reading; never repair an incomplete snapshot.
    if (!juicysf::repairDlsImage(static_cast<uint8_t*>(bytes.getData()), bytes.getSize()))
        return {};

    juce::File temporary{juce::File::createTempFile(".dls")};
    if (!temporary.replaceWithData(bytes.getData(), bytes.getSize())) {
        temporary.deleteFile();
        return {};
    }
    return temporary;
}

void FluidSynthModel::clearRepairedTemp() {
    if (repairedTempFile != juce::File{}) {
        repairedTempFile.deleteFile();
        repairedTempFile = juce::File{};
    }
}

void FluidSynthModel::refreshBanks() {
    ValueTree banks{"banks"};
    const int fontId{sfont_id.load(std::memory_order_acquire)};
    fluid_sfont_t* sfont{
        fontId == -1
        ? nullptr
        : fluid_synth_get_sfont_by_id(synth.get(), fontId)
    };
    if (sfont) {
        std::map<int, ValueTree> bankMap;
        fluid_sfont_iteration_start(sfont);
        for (fluid_preset_t* preset = fluid_sfont_iteration_next(sfont);
             preset != nullptr;
             preset = fluid_sfont_iteration_next(sfont)) {
            int bankNum{fluid_preset_get_banknum(preset)};
            if (!bankMap.count(bankNum))
                bankMap[bankNum] = { "bank", { { "num", bankNum } } };
            const char* const presetName{fluid_preset_get_name(preset)};
            bankMap[bankNum].appendChild({ "preset", {
                { "num", fluid_preset_get_num(preset) },
                { "name", presetName != nullptr ? String::fromUTF8(presetName) : String{} }
            }, {} }, nullptr);
        }
        for (auto& [num, bank] : bankMap)
            banks.appendChild(bank, nullptr);
    }
    valueTreeState.state.getChildWithName("banks").copyPropertiesAndChildrenFrom(banks, nullptr);

    restoreSavedChannelState();

    valueTreeState.state.getChildWithName("banks").sendPropertyChangeMessage("synthetic");

    // Refresh the selected channel's params for the possibly adjusted program.
    syncToSelectedChannel();

    if (onBanksRefreshed)
        onBanksRefreshed();

#if JUCE_DEBUG
#endif
}

void FluidSynthModel::restoreSavedChannelState(bool preserveRequestedPrograms) {
    const int fontId{sfont_id.load(std::memory_order_acquire)};
    const ValueTree banks{valueTreeState.state.getChildWithName("banks")};
    // After a font load, apply each channel's saved program from channelPrograms,
    // falling back to the first preset if the font lacks it. Incoming MIDI still
    // overrides at play time.
    if (fontId != -1) {
        int bankOffset{fluid_synth_get_bank_offset(synth.get(), fontId)};
        ValueTree firstBank{banks.getChild(0)};
        int fallbackBank{firstBank.isValid() ? static_cast<int>(firstBank.getProperty("num")) : 0};
        ValueTree firstPreset{firstBank.isValid() ? firstBank.getChild(0) : ValueTree{}};
        int fallbackPreset{firstPreset.isValid() ? static_cast<int>(firstPreset.getProperty("num")) : 0};

        ValueTree chPrograms{valueTreeState.state.getChildWithName("channelPrograms")};
        for (int i = 0; i < chPrograms.getNumChildren(); i++) {
            ValueTree ch{chPrograms.getChild(i)};
            int chNum{ch.getProperty("num")};
            if (chNum < 0 || chNum >= kNumChannels)
                continue;

            int rawBank{static_cast<int>(ch.getProperty("bank", 0))};
            int rawPreset{static_cast<int>(ch.getProperty("preset", 0))};
            bool exists{banks.getChildWithProperty("num", rawBank)
                .getChildWithProperty("num", rawPreset).isValid()};
            // Banks above 128 are the drum offset plus Bank Select, not font banks. Restore
            // them through Bank Select + Program Change so FluidSynth substitutes the kit;
            // the generic fallback would pick a melodic preset.
            const bool substituteThroughBankSelect{
                !exists && (preserveRequestedPrograms || rawBank > MidiConstants::percussionBank)};
            if (!exists && !substituteThroughBankSelect) {
                rawBank = fallbackBank;
                rawPreset = fallbackPreset;
            }
            // Same engine path as MIDI and automation; state follows what FluidSynth accepted.
            AppliedProgram applied;
            if (substituteThroughBankSelect) {
                if (fluid_synth_bank_select(
                        synth.get(), chNum, bankOffset + rawBank) != FLUID_OK
                    || !applyProgramToEngine(chNum, 0, rawPreset, true, false, &applied))
                    continue;
            } else if (!applyProgramToEngine(
                    chNum, bankOffset + rawBank, rawPreset, false, false, &applied))
                continue;
            syncAppliedProgramOnMessageThread(chNum, applied);
            // Re-apply saved volume and pan.
            for (const auto& [paramID, cc] : channelPropertyToCc) {
                fluid_synth_cc(
                    synth.get(),
                    chNum,
                    static_cast<int>(cc),
                    static_cast<int>(ch.getProperty(paramID, defaultParamValue(paramID))));
                if (const int idx{ccToIndex(static_cast<int>(cc))}; idx >= 0)
                    engineCc[chNum][idx].store(
                        static_cast<int>(ch.getProperty(paramID, defaultParamValue(paramID))),
                        std::memory_order_relaxed);
            }
            // ...and the MIDI-set bend range, which a rebuilt synth forgot.
            reassertBendRange(chNum);
            reassertExpression(chNum);
        }
    }

    syncToSelectedChannel();
}

void FluidSynthModel::setSampleRate(float sampleRate) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0f || settings == nullptr
        || static_cast<double>(sampleRate) * 0.02 >= std::numeric_limits<int>::max()) {
        sampleRateSupported.store(false, std::memory_order_release);
        return;
    }

    double minimumRate{0.0};
    double maximumRate{0.0};
    if (fluid_settings_getnum_range(
            settings.get(), "synth.sample-rate", &minimumRate, &maximumRate) != FLUID_OK) {
        sampleRateSupported.store(false, std::memory_order_release);
        return;
    }

    // Above FluidSynth's ceiling, render at host/N and interpolate up. Below the
    // floor there is no equivalent, so it mutes.
    int factor{1};
    double engineRate{sampleRate};
    if (sampleRate > maximumRate) {
        const double requiredFactor{std::ceil(sampleRate / maximumRate)};
        if (requiredFactor > std::numeric_limits<int>::max()) {
            sampleRateSupported.store(false, std::memory_order_release);
            return;
        }
        factor = static_cast<int>(requiredFactor);
        engineRate = static_cast<double>(sampleRate) / static_cast<double>(factor);
    }
    if (engineRate < minimumRate || engineRate > maximumRate) {
        sampleRateSupported.store(false, std::memory_order_release);
        Logger::writeToLog(
            "Juicy16: unsupported host sample rate " + String(sampleRate, 1)
            + " Hz; FluidSynth supports " + String(minimumRate, 1)
            + "-" + String(maximumRate, 1) + " Hz, so audio is muted");
        return;
    }

    const bool wasSupported{
        sampleRateSupported.exchange(true, std::memory_order_acq_rel)};
    if (wasSupported && std::abs(sampleRate - hostSampleRate) < 0.01f)
        return;

    if (fluid_settings_setnum(
            settings.get(), "synth.sample-rate", engineRate) != FLUID_OK) {
        sampleRateSupported.store(false, std::memory_order_release);
        Logger::writeToLog(
            "Juicy16: FluidSynth rejected host sample rate "
            + String(sampleRate, 1) + " Hz, so audio is muted");
        return;
    }
    if (factor > 1)
        Logger::writeToLog(
            "Juicy16: host sample rate " + String(sampleRate, 1)
            + " Hz is above FluidSynth's " + String(maximumRate, 1)
            + " Hz ceiling; rendering at " + String(engineRate, 1)
            + " Hz and interpolating up");
    hostSampleRate = sampleRate;
    oversampleFactor = factor;
    currentSampleRate = static_cast<float>(engineRate);

    // FluidSynth 2.4+ rejects set_sample_rate: recreate the synth and reload.
    synth.reset();
    sfont_id.store(-1, std::memory_order_release);
    clearRepairedTemp();
    createSynth();
    reloadFontFromState();
}

void FluidSynthModel::reloadFontFromState() {
    ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
    if (!fontState.isValid()
        || fontState.getProperty("path", "").toString().isEmpty())
        return;
#if JUCE_MAC || JUCE_IOS
    valueTreePropertyChanged(fontState, Identifier{"bookmark"});
#else
    valueTreePropertyChanged(fontState, Identifier{"path"});
#endif
}

void FluidSynthModel::applyProgramChangeFromAudioThread(int midiCh, int program) {
    // MIDI is authoritative; the message thread mirrors the result.
    applyProgramToEngine(midiCh, 0, program, true, true);
}

bool FluidSynthModel::isSystemResetSysex(const uint8_t* d, int size) {
    // Data excludes F0/F7.
    if (d == nullptr)
        return false;
    // GM1 / GM2 On. GM Off is recognized by the backend but does not reset it.
    if (size >= 4 && d[0] == 0x7E && d[2] == 0x09
        && (d[3] == 0x01 || d[3] == 0x03))
        return true;
    // Roland GS Reset: 41 <dev> 42 12 40 00 7F 00 41
    if (size == 9 && d[0] == 0x41 && d[2] == 0x42 && d[3] == 0x12
        && d[4] == 0x40 && d[5] == 0x00 && d[6] == 0x7F
        && (d[7] == 0 || d[7] == 0x7F))
        return ((d[4] + d[5] + d[6] + d[7] + d[8]) & 0x7F) == 0;
    // Yamaha XG System On and factory reset, with exactly one zero data byte.
    if (size == 7 && d[0] == 0x43 && d[2] == 0x4C && d[3] == 0x00
        && d[4] == 0x00 && (d[5] == 0x7E || d[5] == 0x7F) && d[6] == 0)
        return true;
    return false;
}

// Payload excludes the F0/F7 framing.
void FluidSynthModel::dispatchSysEx(const uint8_t* payload, int payloadBytes) {
    int handled{0};
    const int result{fluid_synth_sysex(
        synth.get(),
        reinterpret_cast<const char*>(payload),
        payloadBytes,
        nullptr,
        nullptr,
        &handled,
        static_cast<int>(false))};

    if (result != FLUID_OK || handled == 0 || !isSystemResetSysex(payload, payloadBytes))
        return;
    for (auto& send : diagnosticChorusSend) send.store(0);
    for (auto& value : diagnosticModulation) value.store(0);

    // The reset restored 4th-order interpolation; re-apply ours first (no font needed).
    applyInterpolationMethod();
    for (int ch = 0; ch < kNumChannels; ++ch) reapplyVibratoRate(ch);

    const int fontId{sfont_id.load(std::memory_order_acquire)};
    if (fontId == -1)
        return;

    if (processingMidiFile || standardMidiResets.load()) {
        for (int ch = 0; ch < kNumChannels; ++ch) {
            engineExpression[ch].store(-1);
            engineBendRange[ch].store(-1);
            resetRpnTracking(ch);
            int volume{100}, pan{64};
            fluid_synth_get_cc(synth.get(), ch, 7, &volume);
            fluid_synth_get_cc(synth.get(), ch, 10, &pan);
            engineCc[ch][0].store(volume); midiCcValue[ch][0].store(volume);
            engineCc[ch][1].store(pan); midiCcValue[ch][1].store(pan);
            // Publish the reset assignment without restoring the previous song's values.
            int id{-1}, bank{0}, program{0};
            if (fluid_synth_get_program(synth.get(), ch, &id, &bank, &program) == FLUID_OK)
                applyProgramToEngine(ch, bank, program, false, true);
            fluid_synth_cc(synth.get(), ch, 91, MidiConstants::defaultReverbSend);
            applyBendRangeOverride(ch);
            diagnosticExpression[ch].store(127);
            diagnosticBendRange[ch].store(bendRangeOverride.load() > 0
                ? bendRangeOverride.load() << 7 : 256);
            diagnosticBend[ch].store(8192);
            diagnosticSustain[ch].store(0);
            diagnosticChorusSend[ch].store(0);
        }
        midiCcDirtyMask.fetch_or(0xffffu);
        triggerAsyncUpdate();
        return;
    }
    for (int ch = 0; ch < kNumChannels; ch++) {
        applyProgramToEngine(
            ch,
            engineBank[ch].load(std::memory_order_relaxed),
            enginePreset[ch].load(std::memory_order_relaxed),
            false,
            false);
        for (int idx = 0; idx < kNumMixerCcs; ++idx)
            fluid_synth_cc(
                synth.get(), ch, static_cast<int>(ccIndexOrder[idx]),
                engineCc[ch][idx].load(std::memory_order_relaxed));
        // Reset returns the reverb send to the spec default 40 (FluidSynth uses 0).
        fluid_synth_cc(synth.get(), ch, static_cast<int>(EFFECTS_DEPTH1),
                       MidiConstants::defaultReverbSend);
        // Re-assert the file's bend range: on replay the host never resends the RPN.
        resetRpnTracking(ch);
        reassertBendRange(ch);
        reassertExpression(ch);
    }
}

void FluidSynthModel::resetRpnTracking(int ch) {
    rpnMsb[ch] = 127;
    rpnLsb[ch] = 127;
    nrpnActive[ch] = false;
    dataMsb[ch] = 0;
    dataLsb[ch] = 0;
}

// Tracks RPN selection like FluidSynth to recognise bend-range writes. CC121
// nulls the selection and zeroes Data Entry.
void FluidSynthModel::noteControllerForBendRange(int ch, int cc, int value) {
    switch (cc) {
        case RPN_MSB:  rpnMsb[ch] = value; nrpnActive[ch] = false; return;
        case RPN_LSB:  rpnLsb[ch] = value; nrpnActive[ch] = false; return;
        case NRPN_MSB:
        case NRPN_LSB: nrpnActive[ch] = true; return;
        case ALL_CTRL_OFF: resetRpnTracking(ch); return;
        case DATA_ENTRY_MSB: dataMsb[ch] = value; break;
        case DATA_ENTRY_LSB: dataLsb[ch] = value; break;
        default: return;
    }
    if (nrpnActive[ch] || rpnMsb[ch] != 0 || rpnLsb[ch] != 0)
        return;
    engineBendRange[ch].store((dataMsb[ch] << 7) | dataLsb[ch], std::memory_order_relaxed);
    applyBendRangeOverride(ch); // the override outranks the file
    diagnosticBendRange[ch].store(bendRangeOverride.load() > 0
        ? bendRangeOverride.load() << 7 : (dataMsb[ch] << 7) | dataLsb[ch]);
}

void FluidSynthModel::applyBendRangeOverride(int ch) {
    const int forced{bendRangeOverride.load(std::memory_order_relaxed)};
    if (forced > 0 && synth != nullptr)
        fluid_synth_pitch_wheel_sens(synth.get(), ch, forced);
}

// Via RPN so the cents survive; leaves the RPN null, as a reset does.
void FluidSynthModel::reassertBendRange(int ch) {
    const int range{engineBendRange[ch].load(std::memory_order_relaxed)};
    if (range >= 0 && synth != nullptr) {
        fluid_synth_cc(synth.get(), ch, RPN_MSB, 0);
        fluid_synth_cc(synth.get(), ch, RPN_LSB, 0);
        fluid_synth_cc(synth.get(), ch, DATA_ENTRY_MSB, range >> 7);
        fluid_synth_cc(synth.get(), ch, DATA_ENTRY_LSB, range & 127);
        fluid_synth_cc(synth.get(), ch, RPN_MSB, 127);
        fluid_synth_cc(synth.get(), ch, RPN_LSB, 127);
    }
    applyBendRangeOverride(ch);
    diagnosticBendRange[ch].store(bendRangeOverride.load() > 0
        ? bendRangeOverride.load() << 7 : (range >= 0 ? range : 256));
    // This also runs during project recall and bank reload, which do not reset
    // live bend or pedals. Report the engine instead of assuming a MIDI reset.
    int bend{8192}, sustain{0};
    fluid_synth_get_pitch_bend(synth.get(), ch, &bend);
    fluid_synth_get_cc(synth.get(), ch, 64, &sustain);
    diagnosticBend[ch].store(bend);
    diagnosticSustain[ch].store(sustain);
}

// Remembers CC11 and re-asserts it after CC121 (already applied by the engine).
void FluidSynthModel::noteControllerForExpression(int ch, int cc, int value) {
    if (cc == EXPRESSION_MSB) {
        engineExpression[ch].store(value, std::memory_order_relaxed);
        diagnosticExpression[ch].store(value);
    } else if (cc == ALL_CTRL_OFF) {
        if (processingMidiFile || standardMidiResets.load()) {
            engineExpression[ch].store(-1);
            diagnosticExpression[ch].store(127);
        } else {
            reassertExpression(ch);
        }
        diagnosticBend[ch].store(8192);
        diagnosticSustain[ch].store(0);
    }
}

void FluidSynthModel::reassertExpression(int ch) {
    const int value{engineExpression[ch].load(std::memory_order_relaxed)};
    if (value >= 0 && synth != nullptr)
        fluid_synth_cc(synth.get(), ch, static_cast<int>(EXPRESSION_MSB), value);
    diagnosticExpression[ch].store(value >= 0 ? value : 127);
}

// Sets all 16 channels at once.
void FluidSynthModel::applyInterpolationMethod() {
    if (synth != nullptr)
        fluid_synth_set_interp_method(synth.get(), -1, interpolationMethod.load());
}

void FluidSynthModel::applyInterpolationChangeFromAudioThread() {
    if (interpolationDirty.exchange(false, std::memory_order_acq_rel))
        applyInterpolationMethod();
}

// Choice order is frozen: hosts store the index.
int FluidSynthModel::interpolationForChoice(int choice) {
    switch (choice) {
        case 0: return FLUID_INTERP_HIGHEST;
        case 2: return FLUID_INTERP_NONE;
        default: return FLUID_INTERP_LINEAR;
    }
}

void FluidSynthModel::applyVibratoScaleFromAudioThread() {
    const int global = globalVibratoScale.load(std::memory_order_relaxed);
    for (int ch = 0; ch < 16; ++ch) {
        const int scale = std::min(maxVibratoScale, global * vibratoScale[ch].load(std::memory_order_relaxed));
        if (scale != appliedVibratoScale[ch]) {
            fluid_synth_set_cc1_vibrato_scale(synth.get(), ch, static_cast<float>(scale));
            appliedVibratoScale[ch] = scale;
        }
    }
}

// Vibrato LFO rate multiplier as a channel generator offset, in cents.
float FluidSynthModel::vibratoRateOffsetCents(int choice) {
    static constexpr float multipliers[numVibratoRates]{1.0f, 1.5f, 2.0f, 2.4f, 3.0f, 4.0f};
    return 1200.0f * std::log2(multipliers[juce::jlimit(0, numVibratoRates - 1, choice)]);
}

void FluidSynthModel::applyVibratoRateFromAudioThread() {
    const int rate = vibratoRate.load(std::memory_order_relaxed);
    if (rate == appliedVibratoRate)
        return;
    appliedVibratoRate = rate;
    for (int ch = 0; ch < 16; ++ch)
        reapplyVibratoRate(ch);
}

// CC121, mode messages and reset SysEx clear FluidSynth's channel generators.
void FluidSynthModel::reapplyVibratoRate(int ch) {
    fluid_synth_set_gen(synth.get(), ch, GEN_VIBLFOFREQ,
                        vibratoRateOffsetCents(vibratoRate.load(std::memory_order_relaxed)));
}

void FluidSynthModel::reapplyVibratoRateAfterController(int ch, int controller) {
    if (controller == 121)
        reapplyVibratoRate(ch);
    else if (controller >= 124)
        for (int i = 0; i < kNumChannels; ++i) reapplyVibratoRate(i);
}

void FluidSynthModel::applyBendRangeChangeFromAudioThread() {
    if (!bendRangeOverrideDirty.exchange(false, std::memory_order_acq_rel) || synth == nullptr)
        return;
    const int forced{bendRangeOverride.load(std::memory_order_relaxed)};
    for (int ch = 0; ch < kNumChannels; ++ch) {
        const int remembered = engineBendRange[ch].load();
        diagnosticBendRange[ch].store(forced > 0 ? forced << 7
            : (remembered >= 0 ? (remembered >> 7) << 7 : 256));
        if (forced > 0) {
            fluid_synth_pitch_wheel_sens(synth.get(), ch, forced);
            continue;
        }
        // Back to the file's range, whole semitones only, without touching the live RPN
        // selection.
        const int range{engineBendRange[ch].load(std::memory_order_relaxed)};
        fluid_synth_pitch_wheel_sens(synth.get(), ch, range >= 0 ? range >> 7 : 2);
    }
}

namespace {
enum GroupKind : juce::uint8 {
    kindSysEx, kindBank, kindProgram, kindRpnSelect, kindRpnNull, kindData, kindOther
};

// Tier per kind: resets, Program Change prerequisites, Program Change, ordinary
// events, then RPN machinery.
int groupTier(juce::uint8 kind) {
    switch (kind) {
        case kindSysEx:   return 0;
        case kindBank:    return 1;
        case kindProgram: return 2;
        case kindOther:   return 3;
        default:          return 4;
    }
}

juce::uint8 classifyGroupEvent(const juce::uint8* d, int n, int& channel, int& cc, int& value) {
    channel = 0;
    cc = 0;
    value = 0;
    if (d == nullptr || n < 1)
        return kindOther;
    if (d[0] == 0xf0)
        return kindSysEx;
    const int status{d[0] & 0xf0};
    if (status < 0x80 || status == 0xf0)
        return kindOther;
    channel = d[0] & 0x0f;
    if (status == 0xc0)
        return kindProgram;
    if (status != 0xb0 || n < 3)
        return kindOther;
    cc = d[1];
    value = d[2];
    switch (cc) {
        case BANK_SELECT_MSB:
        case BANK_SELECT_LSB:
            return kindBank;
        case RPN_MSB:
        case RPN_LSB:
        case NRPN_MSB:
        case NRPN_LSB:
            return value == 127 ? kindRpnNull : kindRpnSelect;
        case DATA_ENTRY_MSB:
        case DATA_ENTRY_LSB:
        case DATA_ENTRY_INCR:
        case DATA_ENTRY_DECR:
            return kindData;
        default:
            return kindOther;
    }
}

// Slots 0-7 for RPN-machinery CCs; selector pairs are adjacent (partner = slot ^ 1).
int rpnSlot(int cc) {
    switch (cc) {
        case RPN_MSB:         return 0;
        case RPN_LSB:         return 1;
        case NRPN_MSB:        return 2;
        case NRPN_LSB:        return 3;
        case DATA_ENTRY_MSB:  return 4;
        case DATA_ENTRY_LSB:  return 5;
        case DATA_ENTRY_INCR: return 6;
        default:              return 7;
    }
}
} // namespace

void FluidSynthModel::dispatchGroupEvent(const GroupEvent& e, int eventPosition) {
    if (e.kind == kindSysEx) {
        // Straight from the buffer: MidiMessage heap-copies SysEx over four bytes.
        if (e.numBytes >= 2)
            dispatchSysEx(e.data + 1, e.numBytes - 2);
        return;
    }
    dispatchMidiEvent(
        juce::MidiMessage{e.data, e.numBytes, static_cast<double>(eventPosition)},
        eventPosition);
}

void FluidSynthModel::dispatchTimestampGroup(juce::MidiBufferIterator begin,
                                             juce::MidiBufferIterator end,
                                             int eventPosition) {
    // A lone event has no ordering dependencies. Avoid filling the sorting
    // scratch while retaining the raw SysEx path and MidiMessage validation.
    auto next = begin;
    if (next != end && ++next == end) {
        const auto m = *begin;
        GroupEvent raw{};
        raw.data = m.data;
        raw.numBytes = m.numBytes;
        raw.kind = m.numBytes > 0 && m.data[0] == 0xf0 ? kindSysEx : kindOther;
        dispatchGroupEvent(raw, eventPosition);
        return;
    }
    if (processingMidiFile || standardMidiResets.load()) {
        for (auto it = begin; it != end; ++it) {
            const auto m = *it;
            GroupEvent raw{};
            int ch{0}, cc{0}, value{0};
            raw.data = m.data;
            raw.numBytes = m.numBytes;
            raw.kind = classifyGroupEvent(m.data, m.numBytes, ch, cc, value);
            dispatchGroupEvent(raw, eventPosition);
        }
        return;
    }
    int count{0};
    bool plain{true};
    unsigned int rpnChannels{0};
    for (auto it = begin; it != end; ++it) {
        if (count == kMaxGroupEvents) {
            // Over capacity: host order, nothing dropped.
            for (auto rest = begin; rest != end; ++rest) {
                const auto m = *rest;
                GroupEvent raw{};
                int ch{0}, cc{0}, value{0};
                raw.data = m.data;
                raw.numBytes = m.numBytes;
                raw.kind = classifyGroupEvent(m.data, m.numBytes, ch, cc, value);
                dispatchGroupEvent(raw, eventPosition);
            }
            return;
        }
        const auto m = *it;
        GroupEvent& e{groupScratch[static_cast<std::size_t>(count)]};
        int ch{0}, cc{0}, value{0};
        e.data = m.data;
        e.numBytes = m.numBytes;
        e.index = count;
        e.kind = classifyGroupEvent(m.data, m.numBytes, ch, cc, value);
        e.channel = static_cast<juce::uint8>(ch);
        e.cc = static_cast<juce::uint8>(cc);
        e.value = static_cast<juce::uint8>(value);
        e.unit = e.round = e.subTier = e.ccRank = 0;
        plain = plain && e.kind == kindOther;
        if (e.kind == kindRpnSelect || e.kind == kindRpnNull || e.kind == kindData)
            rpnChannels |= 1u << ch;
        ++count;
    }
    if (count > 1 && !plain) {
        for (int ch = 0; ch < kNumChannels; ++ch)
            // Channels without RPN events keep their order.
            if ((rpnChannels & (1u << ch)) != 0)
                orderChannelRpn(ch, count);
        std::sort(groupScratch.begin(), groupScratch.begin() + count,
                  [](const GroupEvent& a, const GroupEvent& b) {
                      const int ta{groupTier(a.kind)}, tb{groupTier(b.kind)};
                      if (ta != tb)
                          return ta < tb;
                      if (ta == 4) {
                          if (a.channel != b.channel) return a.channel < b.channel;
                          if (a.unit != b.unit)       return a.unit < b.unit;
                          if (a.round != b.round)     return a.round < b.round;
                          if (a.subTier != b.subTier) return a.subTier < b.subTier;
                          if (a.ccRank != b.ccRank)   return a.ccRank < b.ccRank;
                      }
                      return a.index < b.index;
                  });
    }
    for (int i = 0; i < count; ++i)
        dispatchGroupEvent(groupScratch[static_cast<std::size_t>(i)], eventPosition);
}

// Orders one channel's RPN machinery as select -> write -> deselect, leaving
// already-ordered input alone. Each data run and its preceding selector run form
// a unit; the k-th write of a controller goes after the k-th selection and
// before the k-th null. A trailing selector run joins the previous unit only if
// it completes it. Leading nulls stay ahead.
void FluidSynthModel::orderChannelRpn(int midiCh, int count) {
    int n{0};
    for (int i = 0; i < count; ++i) {
        const GroupEvent& e{groupScratch[static_cast<std::size_t>(i)]};
        if (e.channel == midiCh
            && (e.kind == kindRpnSelect || e.kind == kindRpnNull || e.kind == kindData))
            rpnScratch[static_cast<std::size_t>(n++)] = i;
    }
    if (n < 2)
        return;
    const auto at = [this](int k) -> GroupEvent& {
        return groupScratch[static_cast<std::size_t>(rpnScratch[static_cast<std::size_t>(k)])];
    };

    int units{1};
    bool lastWasData{false};
    for (int k = 0; k < n; ++k) {
        GroupEvent& e{at(k)};
        const bool isData{e.kind == kindData};
        if (k > 0 && !isData && lastWasData)
            ++units;
        e.unit = static_cast<juce::int16>(units - 1);
        lastWasData = isData;
    }

    if (units > 1 && !lastWasData) {
        const int last{units - 1}, prev{units - 2};
        unsigned prevSelected{0}, lastSelected{0};
        for (int k = 0; k < n; ++k) {
            const GroupEvent& e{at(k)};
            if (e.kind != kindRpnSelect)
                continue;
            const unsigned bit{1u << rpnSlot(e.cc)};
            if (e.unit == prev)
                prevSelected |= bit;
            else if (e.unit == last)
                lastSelected |= bit;
        }
        bool merge{prevSelected == 0};
        if (!merge && lastSelected != 0) {
            merge = true;
            for (int slot = 0; slot < 4; ++slot) {
                if ((lastSelected & (1u << slot)) == 0)
                    continue;
                if ((prevSelected & (1u << slot)) != 0
                    || (prevSelected & (1u << (slot ^ 1))) == 0)
                    merge = false;
            }
        }
        if (merge) {
            for (int k = 0; k < n; ++k)
                if (GroupEvent& e{at(k)}; e.unit == last)
                    e.unit = static_cast<juce::int16>(prev);
            --units;
        }
    }

    for (int u = 0; u < units; ++u) {
        bool hasData{false};
        for (int k = 0; k < n; ++k)
            hasData = hasData || (at(k).unit == u && at(k).kind == kindData);
        if (!hasData)
            continue; // selectors alone stay in buffer order
        int occurrence[4][8]{};
        int rank[4][8];
        int nextRank[4]{};
        std::fill(&rank[0][0], &rank[0][0] + 32, -1);
        bool seenSelectOrData{false};
        for (int k = 0; k < n; ++k) {
            GroupEvent& e{at(k)};
            if (e.unit != u)
                continue;
            int sub{0};
            if (e.kind == kindRpnSelect) { seenSelectOrData = true; sub = 1; }
            else if (e.kind == kindData) { seenSelectOrData = true; sub = 2; }
            else sub = seenSelectOrData ? 3 : 0;
            if (sub == 0)
                continue; // leading null stays ahead
            const int slot{rpnSlot(e.cc)};
            if (rank[sub][slot] < 0)
                rank[sub][slot] = nextRank[sub]++;
            e.subTier = static_cast<juce::int16>(sub);
            e.round = static_cast<juce::int16>(occurrence[sub][slot]++);
            e.ccRank = static_cast<juce::int16>(rank[sub][slot]);
        }
    }
}

void FluidSynthModel::dispatchMidiEvent(const MidiMessage& m, int samplePosition) {
#if JUICYSF_TRACE_MIDI
        DEBUG_PRINT(m.getDescription());
#endif

        if (m.isSysEx()) {
            dispatchSysEx(m.getSysExData(), m.getSysExDataSize());
            return;
        }

        const int channelIndex{m.getChannel() - 1}; // JUCE: 1-16, FluidSynth: 0-15
        if (channelIndex < 0 || channelIndex >= kNumChannels)
            return;
        const int midiCh{channelIndex};
        channelMidiEvents[midiCh].fetch_add(1, std::memory_order_relaxed);
        if (m.isNoteOn()) {
            // Silenced: drop the note-on and skip the trace. Everything else passes, so
            // unmuting mid-song needs no resync.
            if ((silencedMask.load(std::memory_order_acquire) & (1u << midiCh)) != 0)
                return;
            // Trace the engine program at the note, for the conformance suite.
            lastNoteOnBank[midiCh].store(
                engineBank[midiCh].load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            lastNoteOnPreset[midiCh].store(
                enginePreset[midiCh].load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            lastNoteOnSample[midiCh].store(samplePosition, std::memory_order_relaxed);
            fluid_synth_noteon(
                synth.get(),
                midiCh,
                m.getNoteNumber(),
                m.getVelocity());
        } else if (m.isNoteOff()) {
            fluid_synth_noteoff(
                synth.get(),
                midiCh,
                m.getNoteNumber());
        } else if (m.isController()) {
            const int controller{m.getControllerNumber()};
            const int value{m.getControllerValue()};
            lastCcValue[midiCh][controller].store(
                value, std::memory_order_relaxed);
            lastCcSample[midiCh][controller].store(
                samplePosition, std::memory_order_relaxed);
            if (reachesEngine(controller)) {
                fluid_synth_cc(
                    synth.get(),
                    midiCh,
                    controller,
                    value);
                reapplyVibratoRateAfterController(midiCh, controller);
            }
            if (controller == 1)
                diagnosticModulation[midiCh].store(value);
            if (controller == 121)
                diagnosticModulation[midiCh].store(0);
            if (controller == 93)
                diagnosticChorusSend[midiCh].store(value);
            if (controller == 64)
                diagnosticSustain[midiCh].store(value);
            noteControllerForBendRange(
                midiCh, controller, value);
            noteControllerForExpression(
                midiCh, controller, value);
            restoreSixteenChannelLayout(controller);

            // Mirror CC7/CC10 into state via atomics; handleAsyncUpdate writes the tree on
            // the message thread.
            if (int idx{ccToIndex(controller)};
                idx >= 0) {
                engineCc[midiCh][idx].store(value, std::memory_order_relaxed);
                midiCcValue[midiCh][idx].store(value, std::memory_order_relaxed);
                midiCcDirtyMask.fetch_or(1u << midiCh, std::memory_order_release);
                triggerAsyncUpdate();
            }
        } else if (m.isProgramChange()) {
            applyProgramChangeFromAudioThread(midiCh, m.getProgramChangeNumber());
        } else if (m.isPitchWheel()) {
            int bend{m.getPitchWheelValue()};
            if (const int scale{bendScale.load(std::memory_order_relaxed)}; scale > 1)
                bend = juce::jlimit(0, 16383, 8192 + (bend - 8192) * scale);
            fluid_synth_pitch_bend(synth.get(), midiCh, bend);
            diagnosticBend[midiCh].store(bend);
        } else if (m.isChannelPressure()) {
            lastChannelPressureValue[midiCh].store(
                m.getChannelPressureValue(), std::memory_order_relaxed);
            lastChannelPressureSample[midiCh].store(samplePosition, std::memory_order_relaxed);
            fluid_synth_channel_pressure(
                synth.get(),
                midiCh,
                m.getChannelPressureValue());
        } else if (m.isAftertouch()) {
            lastKeyPressureValue[midiCh][m.getNoteNumber()].store(
                m.getAfterTouchValue(), std::memory_order_relaxed);
            lastKeyPressureSample[midiCh][m.getNoteNumber()].store(
                samplePosition, std::memory_order_relaxed);
            fluid_synth_key_pressure(
                synth.get(),
                midiCh,
                m.getNoteNumber(),
                m.getAfterTouchValue());
        }
}

// The one place FluidSynth renders. It must get effects buses: without them
// fluid_synth_process discards reverb and chorus. Each MIDI channel has its own
// dry/effects group so its trim scales everything; the host gets one stereo mix.
void FluidSynthModel::renderSamples(AudioBuffer<float>& buffer, int startSample, int numSamples) {
    if (numSamples <= 0)
        return;

    const int numChannels{buffer.getNumChannels()};
    const int scratchCapacity{effectsScratch.getNumSamples()};
    if (scratchCapacity <= 0)
        return;

    if (numChannels >= 2) {
        for (int rendered = 0; rendered < numSamples;) {
            const int chunk{juce::jmin(numSamples - rendered, scratchCapacity)};
            const int at{startSample + rendered};
            float* outputs[] { buffer.getWritePointer(0, at),
                               buffer.getWritePointer(1, at) };
            renderWithEffects(outputs, chunk);
            rendered += chunk;
        }
        return;
    }

    if (numChannels == 1) {
        const int monoCapacity{juce::jmin(scratchCapacity, stereoScratch.getNumSamples())};
        jassert(monoCapacity > 0);
        for (int rendered = 0; rendered < numSamples;) {
            const int chunk{juce::jmin(numSamples - rendered, monoCapacity)};
            stereoScratch.clear(0, 0, chunk);
            stereoScratch.clear(1, 0, chunk);
            renderWithEffects(
                const_cast<float**>(stereoScratch.getArrayOfWritePointers()), chunk);
            const float* const left{stereoScratch.getReadPointer(0)};
            const float* const right{stereoScratch.getReadPointer(1)};
            float* const mono{buffer.getWritePointer(0, startSample + rendered)};
            // Preserve copy L -> add R -> halve arithmetic in one memory pass.
            for (int i = 0; i < chunk; ++i)
                mono[i] = (left[i] + right[i]) * 0.5f;
            rendered += chunk;
        }
    }
}

// Dry audio plus effects. numSamples must fit the preallocated scratch.
void FluidSynthModel::renderWithEffects(float* const* outputs, int numSamples) {
    jassert(numSamples <= effectsScratch.getNumSamples());
    // Clear only the region FluidSynth adds into, then mark both buffers dirty since
    // cached pointers bypass AudioBuffer's cleared flag.
    effectsScratch.clear(0, numSamples);
    channelScratch.clear(0, numSamples);
    fluid_synth_process(synth.get(), numSamples, 64, effectOutputs.data(), 32, dryOutputs.data());
    effectsScratch.setNotClear();
    channelScratch.setNotClear();
    if (meterDecaySamples != numSamples || !juce::exactlyEqual(meterDecayRate, currentSampleRate)) {
        if (!juce::exactlyEqual(meterDecayRate, currentSampleRate)) {
            meterDecayCache.fill(MeterDecayEntry{});
            meterDecayCacheNext = 0;
            meterDecayRate = currentSampleRate;
        }
        meterDecaySamples = numSamples;
        const auto cached = std::find_if(meterDecayCache.begin(), meterDecayCache.end(),
            [numSamples](const auto& entry) { return entry.samples == numSamples; });
        if (cached != meterDecayCache.end()) {
            meterDecayValue = cached->value;
        } else {
            meterDecayValue = std::pow(0.1f, static_cast<float>(numSamples) / currentSampleRate);
            meterDecayCache[meterDecayCacheNext] = {numSamples, meterDecayValue};
            meterDecayCacheNext = (meterDecayCacheNext + 1) % meterDecayCache.size();
        }
    }
    const float decay{meterDecayValue};
    for (int ch = 0; ch < 16; ++ch) {
        auto& smooth = channelTrimSmoother[ch];
        const float target{channelTrimGain[ch].load(std::memory_order_relaxed)};
        // Equal targets need no tolerance check; changed targets retain JUCE's
        // original approximatelyEqual/setTargetValue behavior.
        if (!juce::exactlyEqual(target, smooth.getTargetValue()))
            smooth.setTargetValue(target);
        const float* const dryLeft = dryOutputs[static_cast<size_t>(2 * ch)];
        const float* const dryRight = dryOutputs[static_cast<size_t>(2 * ch + 1)];
        const float* const wetLeft = effectsScratch.getReadPointer(2 * ch);
        const float* const wetRight = effectsScratch.getReadPointer(2 * ch + 1);
        float peak{0.0f};
        const auto mix = [&](auto nextGain) {
            for (int i = 0; i < numSamples; ++i) {
                const float gain = nextGain();
                const float left = (dryLeft[i] + wetLeft[i]) * gain;
                const float right = (dryRight[i] + wetRight[i]) * gain;
                outputs[0][i] += left;
                outputs[1][i] += right;
                peak = juce::jmax(peak, std::abs(left), std::abs(right));
            }
        };
        // Hoist the settled case out of the loop; arithmetic and order are unchanged.
        if (smooth.isSmoothing())
            mix([&smooth] { return smooth.getNextValue(); });
        else {
#if defined(__aarch64__) && defined(__clang__)
            // Keep each add, multiply and output add separate: contraction would
            // change rounding. Channels still accumulate in their original order.
            #pragma clang fp contract(off)
            const float gain{smooth.getTargetValue()};
            const float32x4_t gains{vdupq_n_f32(gain)};
            float32x4_t peaks{vdupq_n_f32(0.0f)};
            int i{0};
            for (; i <= numSamples - 4; i += 4) {
                const float32x4_t left{vmulq_f32(
                    vaddq_f32(vld1q_f32(dryLeft + i), vld1q_f32(wetLeft + i)), gains)};
                const float32x4_t right{vmulq_f32(
                    vaddq_f32(vld1q_f32(dryRight + i), vld1q_f32(wetRight + i)), gains)};
                vst1q_f32(outputs[0] + i, vaddq_f32(vld1q_f32(outputs[0] + i), left));
                vst1q_f32(outputs[1] + i, vaddq_f32(vld1q_f32(outputs[1] + i), right));
                // JUCE jmax(peak, abs(left), abs(right)) ignores NaNs because
                // peak starts at +0 and remains numeric. Numeric-max does too.
                peaks = vmaxnmq_f32(vmaxnmq_f32(peaks, vabsq_f32(left)), vabsq_f32(right));
            }
            peak = vmaxnmvq_f32(peaks);
            for (; i < numSamples; ++i) {
                const float left{(dryLeft[i] + wetLeft[i]) * gain};
                const float right{(dryRight[i] + wetRight[i]) * gain};
                outputs[0][i] += left;
                outputs[1][i] += right;
                peak = juce::jmax(peak, std::abs(left), std::abs(right));
            }
#else
            mix([gain = smooth.getTargetValue()] { return gain; });
#endif
        }
        // Peak decays with audio time at 20 dB/s, independent of the UI.
        // Each peak is independently atomic telemetry; it publishes no other
        // data and has one rendering writer, so no cross-object fence is needed.
        channelPeak[ch].store(juce::jmax(peak,
            channelPeak[ch].load(std::memory_order_relaxed) * decay),
            std::memory_order_relaxed);
    }
}

void FluidSynthModel::renderIntoFifo(int startSample, int numSamples) {
    if (numSamples <= 0)
        return;
    // fluid_synth_process mixes in, so clear the region first.
    oversampleFifo.clear(0, startSample, numSamples);
    oversampleFifo.clear(1, startSample, numSamples);
    const int capacity{effectsScratch.getNumSamples()};
    if (capacity <= 0)
        return;
    for (int rendered = 0; rendered < numSamples;) {
        const int chunk{juce::jmin(numSamples - rendered, capacity)};
        float* outputs[] { oversampleFifo.getWritePointer(0, startSample + rendered),
                           oversampleFifo.getWritePointer(1, startSample + rendered) };
        renderWithEffects(outputs, chunk);
        rendered += chunk;
    }
}

void FluidSynthModel::renderThroughOversampler(
    AudioBuffer<float>& buffer, MidiBuffer& midiMessages, int numSamples) {
    const int outputChannels{buffer.getNumChannels()};
    if (outputChannels < 1 || numSamples <= 0)
        return;

    // Lagrange uses its past-input history, not future samples. Render only the
    // input needed by this block, including a partial input frame when needed.
    // Extra read-ahead would synthesize next-block audio before its MIDI arrives.
    const juce::int64 uncovered{juce::jmax(juce::int64{0},
        static_cast<juce::int64>(numSamples) - oversampleRenderAhead)};
    const juce::int64 requiredNew{(uncovered + oversampleFactor - 1) / oversampleFactor};
    const int toRender{static_cast<int>(juce::jmin(requiredNew,
        static_cast<juce::int64>(oversampleFifo.getNumSamples() - oversampleFifoFill)))};
    const int fifoBase{oversampleFifoFill};

    // Account for engine samples already rendered, including fractional input
    // frames held by Lagrange. Timing still quantises to one engine sample and
    // FluidSynth's own render quantum; it must not gain a FIFO-dependent delay.
    dispatchTimestampedEvents(
        midiMessages, numSamples, toRender,
        [this, fifoBase](int from, int count) { renderIntoFifo(fifoBase + from, count); },
        [this](int hostPosition) {
            return static_cast<int>(juce::jmax(juce::int64{0},
                (static_cast<juce::int64>(hostPosition) - oversampleRenderAhead)
                    / oversampleFactor));
        });
    oversampleFifoFill += toRender;
    oversampleRenderAhead += static_cast<juce::int64>(toRender) * oversampleFactor;

    float* outputs[2];
    const bool downmix{outputChannels < 2};
    if (downmix) {
        outputs[0] = stereoScratch.getWritePointer(0);
        outputs[1] = stereoScratch.getWritePointer(1);
    } else {
        outputs[0] = buffer.getWritePointer(0);
        outputs[1] = buffer.getWritePointer(1);
    }

    // Produce only what the FIFO holds; an oversized host block ends in silence
    // rather than a bad read.
    int produce{static_cast<int>(juce::jmin(
        static_cast<juce::int64>(numSamples), oversampleRenderAhead))};
    if (downmix)
        produce = juce::jmin(produce, stereoScratch.getNumSamples());
    if (produce < numSamples)
        buffer.clear(produce, numSamples - produce);
    if (produce <= 0)
        return;

    const double ratio{1.0 / oversampleFactor};
    int consumed{0};
    for (int ch = 0; ch < 2; ++ch)
        consumed = oversampleInterpolators[ch].process(
            ratio, oversampleFifo.getReadPointer(ch), outputs[ch], produce);
    oversampleRenderAhead -= produce;

    if (downmix) {
        const float* const left{stereoScratch.getReadPointer(0)};
        const float* const right{stereoScratch.getReadPointer(1)};
        float* const mono{buffer.getWritePointer(0)};
        for (int i = 0; i < produce; ++i)
            mono[i] = (left[i] + right[i]) * 0.5f;
    }

    // Carry unconsumed samples to the next block.
    jassert(consumed >= 0 && consumed <= oversampleFifoFill);
    consumed = juce::jlimit(0, oversampleFifoFill, consumed);
    const int remaining{oversampleFifoFill - consumed};
    if (remaining > 0 && consumed > 0)
        for (int ch = 0; ch < 2; ++ch) {
            float* data{oversampleFifo.getWritePointer(ch)};
            std::memmove(data, data + consumed, sizeof(float) * static_cast<size_t>(remaining));
        }
    oversampleFifoFill = remaining;
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages, bool midiFilePlayback) {
    const juce::ScopedValueSetter<bool> context{processingMidiFile, midiFilePlayback};
    if (!sampleRateSupported.load(std::memory_order_acquire) || synth == nullptr) {
        buffer.clear();
        return;
    }

    const int numSamples{buffer.getNumSamples()};

    // Reverb first, so this block's notes hear this block's settings.
    applyReverbFromAudioThread(numSamples);
    applyChorusFromAudioThread(numSamples);
    applyBendRangeChangeFromAudioThread();
    applyVibratoScaleFromAudioThread();
    applyVibratoRateFromAudioThread();
    applyInterpolationChangeFromAudioThread();

    // Render up to each event, then apply that timestamp's events. Keeps Bank Select
    // -> Program Change -> Note order without quantising to the block start.
    if (oversampleFactor <= 1 || numSamples == 0)
        dispatchTimestampedEvents(
            midiMessages, numSamples, numSamples,
            [this, &buffer](int from, int count) { renderSamples(buffer, from, count); },
            [](int hostPosition) { return hostPosition; });
    else
        renderThroughOversampler(buffer, midiMessages, numSamples);

    // Master trim over the whole block, smoothed.
    const float target{outputLevelGain.load(std::memory_order_relaxed)};
    if (!juce::exactlyEqual(target, outputLevelSmoother.getTargetValue()))
        outputLevelSmoother.setTargetValue(target);
    if (numSamples > 0 && buffer.getNumChannels() == 2 && outputLevelSmoother.isSmoothing()) {
        float* const left{buffer.getWritePointer(0)};
        float* const right{buffer.getWritePointer(1)};
        for (int i = 0; i < numSamples; ++i) {
            const float gain{outputLevelSmoother.getNextValue()};
            left[i] *= gain;
            right[i] *= gain;
        }
    } else {
        outputLevelSmoother.applyGain(buffer, numSamples);
    }
    float peak{0.0f};
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax(peak, buffer.getMagnitude(ch, 0, numSamples));
    // Compare/exchange keeps peaks if the UI reads mid-update.
    float old = masterPeak.load();
    while (old < peak && !masterPeak.compare_exchange_weak(old, peak)) {}
    if (peak > 1.0f)
        outputOverload.store(true);
}

FluidSynthModel::ChannelDiagnostics FluidSynthModel::getChannelDiagnostics(int ch) const {
    ChannelDiagnostics d;
    if (ch < 0 || ch >= 16) return d;
    d.midiEvents = channelMidiEvents[ch].load(); d.peak = channelPeak[ch].load();
    d.expression = diagnosticExpression[ch].load(); d.bendRange = diagnosticBendRange[ch].load();
    d.pitchBend = diagnosticBend[ch].load(); d.sustain = diagnosticSustain[ch].load();
    d.chorusSend = diagnosticChorusSend[ch].load();
    d.modulation = diagnosticModulation[ch].load();
    d.soundingBank = soundingBank[ch].load(); d.soundingPreset = soundingPreset[ch].load();
    return d;
}
int FluidSynthModel::rememberedExpression(int ch) const { return engineExpression[ch].load(); }
int FluidSynthModel::rememberedBendRange(int ch) const { return engineBendRange[ch].load(); }
int FluidSynthModel::savedMixerValue(int ch, int index) const { return engineCc[ch][index].load(); }
void FluidSynthModel::discardPendingStateUpdates() {
    cancelPendingUpdate();
    midiProgramDirtyMask.store(0);
    midiCcDirtyMask.store(0);
    pendingMuteSoloSync.store(false);
    pendingReverbProfile.store(-1);
    pendingReverbCustom.store(false);
}

void FluidSynthModel::restoreRememberedControllers(int ch, int expression, int range) {
    if (ch < 0 || ch >= kNumChannels) return;
    expression = juce::jlimit(-1, 127, expression);
    range = juce::jlimit(-1, 16383, range);
    diagnosticExpression[ch].store(expression >= 0 ? expression : 127);
    diagnosticBendRange[ch].store(range >= 0 ? range : (2 << 7));
    engineExpression[ch].store(juce::jlimit(-1, 127, expression));
    engineBendRange[ch].store(juce::jlimit(-1, 16383, range));
    resetRpnTracking(ch);
    if (synth != nullptr) {
        fluid_synth_cc(synth.get(), ch, 11, expression >= 0 ? expression : 127);
        if (range < 0) fluid_synth_pitch_wheel_sens(synth.get(), ch, 2);
        reassertBendRange(ch);
        reassertExpression(ch);
    }
}
float FluidSynthModel::consumeMasterPeak() { return masterPeak.exchange(0.0f); }
bool FluidSynthModel::hasOutputOverload() const { return outputOverload.load(); }
void FluidSynthModel::clearOutputOverload() { outputOverload.store(false); }
