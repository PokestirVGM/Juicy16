//
// Created by Alex Birch on 10/09/2017.
//

#include <iostream>
#include <iterator>
#include <cstring>
#include <array>
#include <algorithm>
#include <fluidsynth.h>
#include "FluidSynthModel.h"
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

// Per-channel mixer controls. These are ordinary MIDI controllers that
// FluidSynth's own default modulators already implement, so Juicy16 forwards
// them and mirrors them into the UI rather than adding a modulator of its own.
// Incoming MIDI stays authoritative exactly as it is for Bank Select and Program
// Change: a value set in the editor is only a starting point, and the next
// CC7/CC10 on that channel replaces it at that event's timestamp.
// CC -> the property name it occupies in a channelPrograms/ch node. The
// host-facing parameters are per channel ("volCh1".."panCh16"); these two names
// are the tree's, and the two are bridged by mixerParamId below.
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

// fixed index order for the audio-thread CC capture arrays
const fluid_midi_control_change FluidSynthModel::ccIndexOrder[FluidSynthModel::kNumMixerCcs]{
    VOLUME_MSB, PAN_MSB};
thread_local bool FluidSynthModel::mirroringParameters{false};


int FluidSynthModel::ccToIndex(int cc) {
    for (int i = 0; i < kNumMixerCcs; ++i)
        if (static_cast<int>(ccIndexOrder[i]) == cc)
            return i;
    return -1;
}

void FluidSynthModel::setOutputLevelDb(float decibels) {
    // -inf at the bottom of the range rather than a very small gain, so a host
    // automating the parameter to minimum actually silences the plugin.
    const float gain{decibels <= GuiConstants::outputLevelMinDb
        ? 0.0f
        : juce::Decibels::decibelsToGain(decibels)};
    outputLevelGain.store(gain, std::memory_order_relaxed);
}

int FluidSynthModel::defaultParamValue(const String& parameterID) {
    // GM channel defaults, which are also FluidSynth's own channel initialisation
    // values: volume 100, pan 64 (centre). bank/preset and mute/solo default to 0.
    if (parameterID == "volume")
        return MidiConstants::defaultChannelVolume;
    if (parameterID == "pan")
        return MidiConstants::centreValue;
    return programChangeParams.contains(parameterID) ? 0 : 64;
}

// Beta 1's profiles. Naming rule, binding on every future profile: a profile may
// not be named after hardware it does not emulate. Neither of these emulates
// anything - they are designed settings over FluidSynth's own FDN reverb - so
// neither carries a console or hardware name. "Custom" is not a preset; it is
// what the selection reads as once the user has moved a control.
// Chosen by measurement against real game-rip material rather than taken from
// FluidSynth's defaults, which are roughly twice as wet as Universal. Soft is a
// much smaller room at full width: width without a long tail. Figures in
// docs/CONTROLLER_SUPPORT.md.
const FluidSynthModel::ReverbProfile FluidSynthModel::reverbProfiles[]{
    // size  damp  width level
    {"Universal", {0.45f, 0.35f, 0.85f, 0.55f}},
    // OPEN: the owner proposed "SNS" for this profile. It ships as "Soft"
    // because the naming rule is binding — a profile may not be named after
    // hardware it does not emulate, and this one is designed rather than
    // modelled. If "SNS" means SNES the name belongs to the S-DSP echo profile
    // in 10.3, which would be a real emulation. Owner decision still required.
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

String FluidSynthModel::reverbParamId(int reverbParam) {
    switch (reverbParam) {
        case reverbSize:  return "reverbSize";
        case reverbDamp:  return "reverbDamp";
        case reverbWidth: return "reverbWidth";
        case reverbLevel: return "reverbLevel";
        default: break;
    }
    jassertfalse;
    return {};
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

// Matches "<prefix><1..16>" without substring()/numeric conversion, because
// parameterChanged may run on the audio thread and neither allocates there.
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
    // Ordered by first character so a non-match costs one comparison.
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
        resetRpnTracking(i);
        for (int c = 0; c < kNumMixerCcs; c++) {
            midiCcValue[i][c].store(-1, std::memory_order_relaxed);
            // Seed from the same GM defaults the ValueTree and the parameters use,
            // not a single shared constant: volume is 100 and pan is 64, and a
            // reset SysEx re-asserts whatever is stored here.
            engineCc[i][c].store(
                defaultParamValue(ccToChannelProperty.at(ccIndexOrder[c])),
                std::memory_order_relaxed);
        }
    }
    valueTreeState.addParameterListener("bank", this);
    valueTreeState.addParameterListener("preset", this);
    valueTreeState.addParameterListener("outputLevel", this);
    // ...and seed the gain from that parameter. parameterChanged only fires on a
    // *change*, so on a fresh instance nothing ever applied the +1.5 dB default:
    // the knob read +1.5 dB while the audio ran at unity, and the trim only began
    // working once the user moved it. Measured on SEQ_ROAD_D_D: -10.40 dBFS peak
    // with the parameter at its default, the same as an explicit 0 dB.
    if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
            valueTreeState.getParameter("outputLevel"))})
        setOutputLevelDb(p->get());
    valueTreeState.addParameterListener("bendRange", this);
    valueTreeState.addParameterListener("bendScale", this);
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
    valueTreeState.removeParameterListener("reverbOn", this);
    valueTreeState.removeParameterListener("reverbProfile", this);
    for (int i = 0; i < numReverbParams; ++i)
        valueTreeState.removeParameterListener(reverbParamId(i), this);
    valueTreeState.state.removeListener(this);
}

void FluidSynthModel::initialise() {
    // deactivate all audio drivers in fluidsynth to avoid FL Studio deadlock when initialising CoreAudio
    // after all: we only use fluidsynth to render blocks of audio. it doesn't output to audio driver.
    const char *DRV[] {nullptr};
    fluid_audio_driver_register(DRV);
    
    settings = { new_fluid_settings(), delete_fluid_settings };
    
    // https://sourceforge.net/p/fluidsynth/wiki/FluidSettings/
    fluid_settings_setnum(settings.get(), "synth.sample-rate", currentSampleRate);
    // Freeze Beta 1 Bank Select semantics instead of inheriting a FluidSynth
    // default that could change between engine versions. In GS mode CC0 selects
    // the bank for the next Program Change; CC32 is delivered/stored but does
    // not alter the bank number.
    fluid_settings_setstr(settings.get(), "synth.midi-bank-select", "gs");
    // Explicitly retain FluidSynth's API serialization. UI-driven bank/program
    // changes are rare but may overlap host rendering; this prevents concurrent
    // FluidSynth API calls from corrupting its internal state.
    fluid_settings_setint(settings.get(), "synth.threadsafe-api", 1);
    // Generous polyphony so dense 16-channel multitimbral material never steals
    // voices (voice stealing cuts note tails / harmonics).
    //
    // This MUST be a setting rather than a post-construction
    // fluid_synth_set_polyphony call. new_fluid_synth sizes the rvoice event
    // queue once, as polyphony * 64, and fluid_synth_set_polyphony grows only the
    // voice array. Raising polyphony afterwards therefore left the queue sized for
    // FluidSynth's default 256 while 512 voices fed it: above ~256 sounding voices
    // it overflowed continuously, dropping rvoice events and emitting thousands of
    // "Ringbuffer full" warnings per second. Measured with tools/perf_probe.cpp.
    fluid_settings_setint(settings.get(), "synth.polyphony", maximumPolyphony);
    createSynth();
}

void FluidSynthModel::createSynth() {
    synth = { new_fluid_synth(settings.get()), delete_fluid_synth };

    // Gold-standard playback fidelity:
    // - 7th-order ("highest") sample interpolation. FluidSynth defaults to 4th-order,
    //   which audibly rolls off the top octave; 7th-order preserves the high end for
    //   both SF2 and DLS. Setting it here is not enough on its own - see
    //   applyInterpolationMethod, which a reset SysEx calls again.
    applyInterpolationMethod();
    // Polyphony comes from the settings above, before this synth existed.

    // Output gain. FluidSynth's own default, and deliberately back to it.
    //
    // This was 1.0 - five times the default - and that was the whole of the
    // "dynamics sound wrong" report: measured on a real 52 s game rip, gain 1.0
    // peaks at +7.3 dBFS with 0.39% of samples past full scale, and a four-note
    // chord on all 16 channels reaches +20 dBFS. Once anything downstream clips,
    // quiet notes rise relative to loud ones, CC7 automation stops doing anything
    // above the ceiling, and hard clipping collapses the L/R difference that
    // carries pan. At 0.2 the same rip peaks at -6.7 dBFS, which is the ordinary
    // gain-staging target, and matches the loudness of every other FluidSynth
    // based player. FluidSynth documents 0.2 as being low "to avoid the
    // saturation of the output when many notes are played", and their own attempt
    // to raise the default to 0.6 in 2.4.0 drew clipping reports.
    //
    // Users who want it louder use the outputLevel parameter, which is applied
    // after rendering with smoothing, rather than by moving this.
    fluid_synth_set_gain(synth.get(), 0.2f);

    // Chorus off, deliberately. Now that the effects bus is mixed into the
    // output (see renderSamples), leaving FluidSynth's `synth.chorus.active`
    // default alone would un-mute a chorus nobody chose, on every rip — the
    // exact class of unchosen default this phase exists to remove. Chorus stays
    // off until it has parameters of its own. CC93 still reaches the engine.
    fluid_synth_chorus_on(synth.get(), -1, 0);

    // The reverb belongs to the synth, so a (re)created synth starts from the
    // parameters rather than from FluidSynth's own defaults.
    resetReverbToParameters();

    // ...and every channel starts at the GM default reverb send, which
    // FluidSynth does not apply. See MidiConstants::defaultReverbSend.
    for (int ch = 0; ch < kNumChannels; ++ch)
        fluid_synth_cc(synth.get(), ch, static_cast<int>(EFFECTS_DEPTH1),
                       MidiConstants::defaultReverbSend);

    // A fresh synth is at the default two semitones everywhere; processBlock
    // re-applies the override, and refreshBanks the remembered per-channel ranges.
    bendRangeOverrideDirty.store(true, std::memory_order_release);

    // No modulators are installed here any more.
    //
    // Juicy16 used to add its own CC71-79 -> filter/volume-envelope modulators,
    // which no other SoundFont player applies: stock FluidSynth ignores CC71-79
    // entirely. Measured against a real SF2, the amounts were wildly out of
    // scale - CC73=127 stretched attack from 50 ms to 868 ms, CC75=127 raised the
    // note tail by 43 dB, CC72=127 left a note ringing 48 dB above neutral a
    // second after note-off, and CC71=127 attenuated the signal by 46 dB - while
    // on DLS banks they did nothing at all, because FluidSynth's native DLS
    // loader does not apply the default modulator list. Game rips commonly send
    // those controllers, so the result was material that sounded flat and
    // compressed only in this plugin. Every CC still reaches the synth; there is
    // simply no Juicy16-specific modulator listening for it.
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
    // 20 ms is long enough that an automation jump is inaudible as a step and
    // short enough that a deliberate move still feels immediate.
    outputLevelSmoother.reset(sampleRate, 0.02);
    outputLevelSmoother.setCurrentAndTargetValue(
        outputLevelGain.load(std::memory_order_relaxed));
    // The reverb settings glide over the same 20 ms, but per block rather than
    // per sample: FluidSynth takes a setting, not a signal.
    for (int i = 0; i < numReverbParams; ++i)
        reverbSmoother[i].reset(sampleRate, 0.02);
    resetReverbToParameters();
    // pre-allocate the mono-downmix scratch off the audio thread
    stereoScratch.setSize(2, jmax(64, samplesPerBlock), false, false, true);
    // ...and the effects bus the reverb is mixed from. Sized for a whole block
    // so the common path renders in one call; renderSamples chunks against this
    // capacity, so a host that ignores its own maximum block size still cannot
    // overrun it or allocate on the audio thread.
    effectsScratch.setSize(2, jmax(64, samplesPerBlock), false, false, true);
    // ...and the oversampling FIFO, which holds one block's worth of internal
    // samples plus the interpolator's read-ahead and whatever the previous block
    // left behind.
    oversampleFifo.setSize(
        2, jmax(64, samplesPerBlock / jmax(1, oversampleFactor) + 8), false, true, true);
    oversampleFifoFill = 0;
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

    const int result{retainCurrentBank
        ? fluid_synth_program_change(synth.get(), midiCh, preset)
        : fluid_synth_program_select(synth.get(), midiCh, fontId, rawBank, preset)};
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
    // While loadingChannel is set, the params are being written to MIRROR state the
    // engine already has (channel switch, MIDI program-change sync, dropdown pick):
    // re-sending it to the synth is at best redundant and at worst applies an
    // invalid intermediate program (bank set before preset), and saving it back
    // would clobber the tree we just read. Skip entirely.
    if (parameterID == "outputLevel") {
        // May arrive on the audio thread from host automation. Only stores an
        // atomic; the smoothing happens in processBlock.
        if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
                valueTreeState.getParameter(parameterID))})
            setOutputLevelDb(p->get());
        return;
    }
    if (parameterID == "bendRange" || parameterID == "bendScale") {
        // Host bend compensation. May arrive on the audio thread; store only.
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
    // Reverb. Like outputLevel, these may arrive on the audio thread from host
    // automation, so they only store an atomic; processBlock does the work.
    if (parameterID == "reverbOn") {
        if (auto* p{dynamic_cast<juce::AudioParameterBool*>(
                valueTreeState.getParameter(parameterID))})
            reverbEnabledTarget.store(p->get(), std::memory_order_relaxed);
        return;
    }
    if (parameterID == "reverbProfile") {
        if (applyingReverbProfile)
            return;
        int profile{0};
        if (auto* p{dynamic_cast<juce::AudioParameterChoice*>(
                valueTreeState.getParameter(parameterID))})
            profile = p->getIndex();
        // Selecting a profile MOVES the visible controls. Writing four
        // parameters is a message-thread job, so hand it over rather than doing
        // it wherever the host happened to call us.
        if (profile != customReverbProfileIndex()) {
            pendingReverbProfile.store(profile, std::memory_order_release);
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
        // A control moved by hand means the selection is no longer any named
        // profile - unless we are the ones moving it to apply one.
        if (!applyingReverbProfile) {
            pendingReverbCustom.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }
        return;
    }
    if (mirroringParameters)
        return;
    if (int progCh{progParamChannel(parameterID)}; progCh >= 0) {
        // Per-channel program parameter (host automation / VST3 unit program).
        // Can arrive on the audio thread, so treat it exactly like an incoming
        // MIDI program change: apply to the synth, then capture the resulting
        // program for the message thread to mirror into channelPrograms/UI.
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
            // volChN / panChN: this row's knob, host automation, and an incoming
            // CC7/CC10 on channel N all arrive here or in dispatchMidiEvent, and
            // all end at the same place - the engine, the channelPrograms node,
            // and the parameter. MIDI stays authoritative because its write
            // happens last, at the event's own timestamp.
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
                // audio-thread automation: defer the tree write to handleAsyncUpdate
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
                // Host automation on the audio thread: defer the tree write, or
                // the editor never learns that fifteen other rows just went
                // quiet. Solo is the case that matters - it changes how every
                // OTHER row should look.
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
        // Mirroring: the values are already in the tree, and the engine receives
        // them when the font loads. Writing them back would be a no-op at best.
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
    // Reverb profile reconciliation. Selecting a profile writes the four
    // parameters; editing one of them writes the selection back to Custom. Both
    // directions run here so neither happens on whichever thread the host chose,
    // and applyingReverbProfile keeps them from chasing each other.
    if (const int profile{pendingReverbProfile.exchange(-1, std::memory_order_acquire)};
        profile >= 0 && profile < customReverbProfileIndex()) {
        const juce::ScopedValueSetter<bool> guard{applyingReverbProfile, true};
        for (int i = 0; i < numReverbParams; ++i) {
            const float value{reverbProfiles[profile].values[i]};
            reverbTarget[i].store(value, std::memory_order_relaxed);
            if (auto* p{dynamic_cast<juce::AudioParameterFloat*>(
                    valueTreeState.getParameter(reverbParamId(i)))})
                *p = value;
        }
        pendingReverbCustom.store(false, std::memory_order_release);
    }
    if (pendingReverbCustom.exchange(false, std::memory_order_acquire)) {
        const juce::ScopedValueSetter<bool> guard{applyingReverbProfile, true};
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

    // consume per-channel program changes and sound-CC changes captured on the
    // audio thread; all ValueTree/parameter writes happen here, on the message thread.
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
            // Every channel has its own knob and its own parameter now, so an
            // incoming CC moves that row whether or not it is the selected one.
            // The guard stops the parameter write from being sent back to the
            // engine, which already has this value from the MIDI event itself.
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
    // Push the saved program into the shared bank/preset params so they describe
    // the newly-selected channel. Volume, pan, mute, and solo no longer follow
    // the selection: each channel owns its own parameter, and each row its own
    // control. The guard makes parameterChanged skip both the engine re-send and
    // the save-back - the engine already holds these values for this channel.
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
            // A path-only state is valid when no security bookmark was available
            // (including tests and older sessions). If a bookmark exists, wait for
            // its property update so sandbox access is established before loading.
            MemoryBlock emptyBookmark;
            const var bookmark{treeWhosePropertyHasChanged.getProperty("bookmark", emptyBookmark)};
            if (bookmark.isBinaryData() && bookmark.getBinaryData()->isEmpty()) {
                const String path{treeWhosePropertyHasChanged.getProperty("path", "")};
                if (path.isNotEmpty())
                    unloadAndLoadFont(path);
            }
        }
        if (property == StringRef("bookmark")) {
            CFErrorRef cfError = nullptr;
            MemoryBlock buffer;
            var bookmark = treeWhosePropertyHasChanged.getProperty("bookmark", buffer);
            jassert(bookmark.isBinaryData());
            bool loadedViaBookmark = false;
            String bookmarkPath;
            if (bookmark.getBinaryData()->getSize() > 0) {
                CFUniquePtr<CFDataRef> data{CFDataCreate(
                    nullptr,
                    static_cast<const UInt8 *>(bookmark.getBinaryData()->getData()),
                    static_cast<CFIndex>(bookmark.getBinaryData()->getSize()))};
                // isStale reports a bookmark that still resolves but whose target
                // has moved or been replaced. Recorded for diagnostics; the
                // resolved URL is authoritative either way.
                Boolean isStale = false;
                CFUniquePtr<CFURLRef> cfURL{CFURLCreateByResolvingBookmarkData(nullptr, data.get(), kCFURLBookmarkResolutionWithSecurityScope, nullptr, nullptr, &isStale, &cfError)};
                if (cfURL) {
                    CFUniquePtr<CFStringRef> cfPath {CFURLCopyFileSystemPath(cfURL.get(), CFURLPathStyle::kCFURLPOSIXPathStyle)};
                    // Must own the string: StringRef would only borrow a pointer
                    // into the temporary returned by fromCFString.
                    bookmarkPath = String::fromCFString(cfPath.get());
                    if (bookmarkPath.isNotEmpty()) {
                        CFURLStartAccessingSecurityScopedResource(cfURL.get());
                        // A bookmark can resolve to a file that no longer loads
                        // (replaced, truncated, permissions). Keep the result so
                        // the stored path is still tried below.
                        loadedViaBookmark = unloadAndLoadFont(bookmarkPath);
                        CFURLStopAccessingSecurityScopedResource(cfURL.get());
                    }
                }
                if (ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
                    fontState.isValid())
                    fontState.setProperty("bookmarkStale", isStale != 0, nullptr);
            }
            if (cfError != nullptr)
                CFRelease(cfError);
            if (!loadedViaBookmark) {
                String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
                if (soundFontPath.isNotEmpty() && soundFontPath != bookmarkPath) {
                    unloadAndLoadFont(soundFontPath);
                }
            }
        }
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

void FluidSynthModel::setControllerValue(int controller, int value) {
    const auto ch{channel.load(std::memory_order_relaxed)};
    if (ch >= static_cast<unsigned int>(kNumChannels)
        || !juce::isPositiveAndBelow(controller, 128)
        || !juce::isPositiveAndBelow(value, 128))
        return;
    fluid_synth_cc(synth.get(), static_cast<int>(ch), controller, value);
    if (const int idx{ccToIndex(controller)}; idx >= 0)
        engineCc[ch][idx].store(value, std::memory_order_relaxed);
}

void FluidSynthModel::setChannelControllerValue(int channelToWrite, int controller, int value) {
    if (channelToWrite < 0 || channelToWrite >= kNumChannels
        || !juce::isPositiveAndBelow(controller, 128)
        || !juce::isPositiveAndBelow(value, 128))
        return;
    fluid_synth_cc(synth.get(), channelToWrite, controller, value);
    if (const int idx{ccToIndex(controller)}; idx >= 0)
        engineCc[channelToWrite][idx].store(value, std::memory_order_relaxed);
}

unsigned int FluidSynthModel::deriveSilencedMask(unsigned int mutes, unsigned int solos) {
    // A channel sounds if it is NOT muted AND (nothing is soloed OR it is one of
    // the soloed ones). Mute always wins; solo only restricts the candidates, so
    // every press of M does something. Consequences, all intended: muting the
    // only soloed channel is silent, soloing a muted channel is silent, soloing
    // everything equals soloing nothing, and clearing the last solo restores the
    // mute picture untouched.
    constexpr unsigned int all{(1u << kNumChannels) - 1u};
    return (mutes | (solos != 0 ? ~solos : 0u)) & all;
}

void FluidSynthModel::refreshSilencedMask() {
    const unsigned int updated{deriveSilencedMask(
        muteMask.load(std::memory_order_relaxed),
        soloMask.load(std::memory_order_relaxed))};
    const unsigned int previous{silencedMask.exchange(updated, std::memory_order_release)};
    // All-notes-off, not all-sound-off: the envelopes release naturally, so
    // muting a sustained pad does not click. Note-offs are never dropped, so a
    // channel unmuted later is not left with stuck state either.
    for (int ch = 0; ch < kNumChannels; ++ch)
        if ((updated & ~previous & (1u << ch)) != 0)
            fluid_synth_all_notes_off(synth.get(), ch);
}

bool FluidSynthModel::isChannelSilenced(int channelToRead) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    return (silencedMask.load(std::memory_order_acquire) & (1u << channelToRead)) != 0;
}

unsigned int FluidSynthModel::getSilencedMask() const {
    return silencedMask.load(std::memory_order_acquire);
}

void FluidSynthModel::applyReverbFromAudioThread(int numSamples) {
    fluid_synth_t* const synthesizer{synth.get()};
    if (synthesizer == nullptr)
        return;

    // Bypass is the reverb unit switched off, not its level taken to zero: an
    // inactive unit is not processed at all, so nothing keeps computing a tail
    // that nobody can hear.
    const bool enabled{reverbEnabledTarget.load(std::memory_order_relaxed)};
    if (!reverbEverApplied || enabled != reverbEnabledApplied) {
        fluid_synth_reverb_on(synthesizer, -1, enabled ? 1 : 0);
        reverbEnabledApplied = enabled;
    }

    for (int i = 0; i < numReverbParams; ++i) {
        reverbSmoother[i].setTargetValue(reverbTarget[i].load(std::memory_order_relaxed));
        // One value per block rather than per sample: FluidSynth's reverb takes a
        // setting, not a signal, and recomputing its coefficients per sample
        // would be both impossible through this API and pointless. The smoother
        // is what stops a host's automation jump becoming one audible step.
        reverbSmoother[i].skip(numSamples);
        const float value{reverbSmoother[i].getCurrentValue()};
        // Only write a setting that actually moved. A game rip that never
        // automates reverb must not pay four coefficient recomputations a block.
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
        // Jump rather than glide: this runs when the synth is (re)created or the
        // sample rate changes, where there is no previous value to glide from.
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

bool FluidSynthModel::getChannelProgram(int channelToRead, int& bank, int& preset) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    int soundFontId{-1};
    int rawBank{0};
    if (fluid_synth_get_program(
            synth.get(), channelToRead, &soundFontId, &rawBank, &preset) != FLUID_OK)
        return false;
    // Reported in the font's own bank numbering, matching channelPrograms and the
    // bank parameter. Raw engine banks stay inside applyProgramToEngine.
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
    // The audio thread stores the raw engine bank; the offset conversion belongs
    // here, where the FluidSynth API lock is safe to take.
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

// Runtime-only; never serialised. True after a security-scoped bookmark resolved
// but reported its target as moved or replaced.
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

// Reads the voice limit back out of the settings the synth was constructed from,
// which is the property that actually sizes FluidSynth's rvoice event queue.
bool FluidSynthModel::getConfiguredPolyphony(int& configured, int& active) const {
    if (settings == nullptr || synth == nullptr)
        return false;
    if (fluid_settings_getint(settings.get(), "synth.polyphony", &configured) != FLUID_OK)
        return false;
    active = fluid_synth_get_polyphony(synth.get());
    return active > 0;
}

// CC124-127 are MIDI 1.0 channel-mode messages. FluidSynth implements them
// faithfully, which means Omni Off and Mono On assign a group of consecutive
// channels to a basic channel and DISABLE the rest: a single CC124 on channel 1
// leaves only channel 1 responding, silent and unreadable everywhere else, until
// the next reset.
//
// That is correct for a MIDI 1.0 sound module and wrong for Juicy16, whose
// product contract is exactly 16 independent channels per instance. The
// controller is still delivered to the engine above, so the "every CC0-127
// reaches FluidSynth" contract holds; only the channel layout it would have
// destroyed is put back.
//
// The restored layout is FluidSynth's own default: one basic channel at 0 in
// OMNION_POLY whose group covers every MIDI channel. `val` 0 means "to MIDI
// channel count minus 1". Audio thread; both calls take the FluidSynth API lock,
// exactly like the fluid_synth_cc above, and channel-mode messages are rare.
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

    String pathToLoad{absPath};
    // Build a repaired candidate without disturbing the active bank. The candidate
    // temp file becomes owned by the model only after FluidSynth accepts it.
    juce::File repaired{writeRepairedTempCopy(requested)};
    if (repaired.existsAsFile())
        pathToLoad = repaired.getFullPathName();

    // A RIFF container that claims to be larger than the file cannot be valid.
    // Repair fixes exactly that for banks it can rewrite; anything left over is
    // rejected here rather than handed to FluidSynth, whose parser can spend
    // minutes scanning a large malformed image and block the message thread.
    // A well-formed bank passes at any size, so legitimate large SoundFonts are
    // unaffected.
    if (!repaired.existsAsFile() && riffContainerOverrunsFile(requested)) {
        publishFontLoadResult(
            false, absPath,
            "This bank's RIFF header claims more data than the file contains, so it "
            "is truncated or corrupt. Try re-exporting or downloading it again.",
            false);
        return false;
    }

    // reset_presets=0 is deliberate: the old bank and its live channel programs
    // stay usable until the replacement is proven loadable and non-empty.
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

    // The candidate failed after the old bank had already proved usable. Restore
    // the serialised selection to that active bank as part of the transaction, so
    // saving the project (or recreating the synth at a new sample rate) cannot turn
    // a harmless rejected file choice into a broken future session. Keep the error
    // fields above so the UI can still explain what was rejected.
    const String loadedPath{fontState.getProperty("loadedPath", "").toString()};
    if (loadedPath.isNotEmpty()) {
        juce::ScopedValueSetter<bool> suppress{ suppressFontStateReload, true };
        MemoryBlock emptyBookmark;
        fontState.setProperty("path", loadedPath, nullptr);
        fontState.setProperty(
            "bookmark", fontState.getProperty("loadedBookmark", emptyBookmark), nullptr);
    }
}

// True when the file starts with a RIFF header whose declared payload extends
// past the end of the file. Non-RIFF and unreadable inputs return false, so this
// only ever adds a rejection for a container that is provably inconsistent.
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
    // Repair rewrites an in-memory image of the whole file, and the file is
    // user-selected, so the size must be bounded before any of it is read.
    // Anything larger is handed to FluidSynth unrepaired: it streams the file
    // instead of buffering it, so an oversized or hostile input costs a normal
    // parse failure rather than an allocation of that size.
    static constexpr juce::int64 maxRepairableBytes{512ll * 1024 * 1024};
    if (src.getSize() > maxRepairableBytes)
        return {};

    // header sniff: only DLS files are candidates for repair (avoid reading large
    // SF2 files into memory needlessly)
    char hdr[12] = {};
    {
        juce::FileInputStream in{src};
        if (in.failedToOpen())
            return {};
        in.read(hdr, sizeof(hdr));
    }
    if (std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "DLS ", 4) != 0)
        return {};

    juce::MemoryBlock mb;
    if (!src.loadFileAsData(mb))
        return {};
    if (!juicysf::repairDlsImage(static_cast<uint8_t*>(mb.getData()), mb.getSize()))
        return {}; // already well-formed: load the original

    juce::File tmp{juce::File::createTempFile(".dls")};
    if (!tmp.replaceWithData(mb.getData(), mb.getSize()))
        return {};
    return tmp;
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

    // After a font (re)load every channel must end up on a program that exists in
    // the new font. Each channel's saved program lives in channelPrograms (set by the
    // dropdowns, restored from plugin state, or left at the default). Apply THAT to the
    // synth so the saved/default instrument actually sounds — falling back to the first
    // available preset only when the saved one is absent from the font. (Previously this
    // read the synth's LIVE program, which right after a load is just the default, so it
    // clobbered every channel back to the first patch and lost restored assignments.)
    // Incoming MIDI program changes remain authoritative and override this at play time.
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

            // the channel's saved/intended program
            int rawBank{static_cast<int>(ch.getProperty("bank", 0))};
            int rawPreset{static_cast<int>(ch.getProperty("preset", 0))};
            bool exists{banks.getChildWithProperty("num", rawBank)
                .getChildWithProperty("num", rawPreset).isValid()};
            // A bank above the percussion bank is not a font bank at all: it is
            // FluidSynth's drum offset plus the Bank Select MSB, so no font
            // defines it and only FluidSynth can resolve it to a kit. Restore it
            // the way live MIDI produced it - select the bank, then change
            // program - which keeps the channel on the bank it was saved with and
            // lets FluidSynth substitute the sound. The generic fallback below
            // would instead move a drum channel onto the font's first melodic
            // preset, which is audible.
            const bool substituteThroughBankSelect{
                !exists && rawBank > MidiConstants::percussionBank};
            if (!exists && !substituteThroughBankSelect) {
                rawBank = fallbackBank;
                rawPreset = fallbackPreset;
            }
            // Apply through the same engine/result path used by MIDI, host
            // automation, and manual selection. State follows the program that
            // FluidSynth actually accepted.
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
            // re-apply this channel's saved envelope/filter sliders (64 = neutral)
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
            // ...and the bend range the MIDI stream last set, which a rebuilt
            // synth (sample-rate change) would otherwise have forgotten.
            reassertBendRange(chNum);
            reassertExpression(chNum);
        }
    }

    valueTreeState.state.getChildWithName("banks").sendPropertyChangeMessage("synthetic");

    // refresh the selected channel's params so the preset list highlight + sliders
    // reflect the (possibly adjusted) current program.
    syncToSelectedChannel();

    if (onBanksRefreshed)
        onBanksRefreshed();

#if JUCE_DEBUG
//    unique_ptr<XmlElement> xml{valueTreeState.state.createXml()};
//    Logger::outputDebugString(xml->createDocument("",false,false));
#endif
}

void FluidSynthModel::setSampleRate(float sampleRate) {
    if (sampleRate <= 0.0f || settings == nullptr) {
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

    // Above FluidSynth's ceiling, render at the largest integer fraction of the
    // host rate the engine accepts and interpolate each block back up: a 192 kHz
    // project renders at 96 kHz rather than falling silent. An integer factor
    // keeps the ratio an exact 1/N. Below the floor there is no such trick -
    // that direction needs decimation with an anti-alias filter, and an 8 kHz
    // floor is not a rate any host in scope runs at - so it still mutes.
    int factor{1};
    double engineRate{sampleRate};
    if (sampleRate > maximumRate) {
        factor = static_cast<int>(std::ceil(sampleRate / maximumRate));
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

    // FluidSynth 2.4+ deliberately rejects fluid_synth_set_sample_rate(). Recreate
    // the synth at the host rate before playback, then restore the bank and all
    // per-channel state through the normal font-load path.
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
    // MIDI from the DAW is authoritative: apply the program change to this
    // channel, then capture the resulting program so the message thread can
    // update the channel list / params (see handleAsyncUpdate).
    applyProgramToEngine(midiCh, 0, program, true, true);
}

bool FluidSynthModel::isSystemResetSysex(const uint8_t* d, int size) {
    // data excludes the F0/F7 framing (JUCE getSysExData).
    if (d == nullptr)
        return false;
    // Universal Non-Realtime, General MIDI subfamily: 7E <dev> 09 <01|02|03>
    // (GM1 On / GM Off / GM2 On) — FluidSynth resets channels for these.
    if (size >= 4 && d[0] == 0x7E && d[2] == 0x09)
        return true;
    // Roland GS Reset: 41 <dev> 42 12 40 00 7F 00 41
    if (size >= 9 && d[0] == 0x41 && d[2] == 0x42 && d[3] == 0x12
        && d[4] == 0x40 && d[5] == 0x00 && d[6] == 0x7F)
        return true;
    // Yamaha XG System On: 43 <dev> 4C 00 00 7E 00
    if (size >= 7 && d[0] == 0x43 && d[2] == 0x4C && d[3] == 0x00
        && d[4] == 0x00 && d[5] == 0x7E)
        return true;
    return false;
}

// `payload`/`payloadBytes` follow MidiMessage::getSysExData(): the bytes between
// the leading 0xF0 and the trailing 0xF7.
void FluidSynthModel::dispatchSysEx(const uint8_t* payload, int payloadBytes) {
    fluid_synth_sysex(
        synth.get(),
        reinterpret_cast<const char*>(payload),
        payloadBytes,
        nullptr,
        nullptr,
        nullptr,
        static_cast<int>(false));

    if (!isSystemResetSysex(payload, payloadBytes))
        return;

    // The reset returned every channel to FluidSynth's 4th-order interpolation.
    // Put the plugin's own method back first: unlike everything below it, it does
    // not depend on a font being loaded.
    applyInterpolationMethod();

    const int fontId{sfont_id.load(std::memory_order_acquire)};
    if (fontId == -1)
        return;

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
        // A GM/GS/XG reset returns the reverb send to its specified default,
        // which is 40 rather than FluidSynth's 0. Unlike volume and pan, there
        // is no plugin-owned value to restore here: the spec's default IS the
        // right answer after a reset.
        fluid_synth_cc(synth.get(), ch, static_cast<int>(EFFECTS_DEPTH1),
                       MidiConstants::defaultReverbSend);
        // The reset nulled the RPN and returned the bend range to two
        // semitones. Re-assert the range the file established, for the same
        // reason programs are re-asserted: on a replay the host's parameter
        // cache still holds the RPN and it is never sent again.
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

// Follows the RPN selection the way FluidSynth does, so a Data Entry can be
// recognised as a bend-range write and remembered. Reset All Controllers nulls
// the selection and zeroes Data Entry, as FluidSynth's channel init does.
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
}

void FluidSynthModel::applyBendRangeOverride(int ch) {
    const int forced{bendRangeOverride.load(std::memory_order_relaxed)};
    if (forced > 0 && synth != nullptr)
        fluid_synth_pitch_wheel_sens(synth.get(), ch, forced);
}

// Through the RPN rather than fluid_synth_pitch_wheel_sens, so the cents in the
// Data Entry LSB survive. Leaves the RPN null, which is where a reset put it.
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
}

// Remembers the expression the stream sets, and puts it back after a Reset
// All Controllers. The engine has already applied the reset by the time this
// runs, so the re-assert lands on the reset channel.
void FluidSynthModel::noteControllerForExpression(int ch, int cc, int value) {
    if (cc == EXPRESSION_MSB)
        engineExpression[ch].store(value, std::memory_order_relaxed);
    else if (cc == ALL_CTRL_OFF)
        reassertExpression(ch);
}

void FluidSynthModel::reassertExpression(int ch) {
    const int value{engineExpression[ch].load(std::memory_order_relaxed)};
    if (value >= 0 && synth != nullptr)
        fluid_synth_cc(synth.get(), ch, static_cast<int>(EXPRESSION_MSB), value);
}

// One call sets all 16 channels, so this is not per channel like the others.
void FluidSynthModel::applyInterpolationMethod() {
    if (synth != nullptr)
        fluid_synth_set_interp_method(synth.get(), -1, interpolationMethod);
}

void FluidSynthModel::applyBendRangeChangeFromAudioThread() {
    if (!bendRangeOverrideDirty.exchange(false, std::memory_order_acq_rel) || synth == nullptr)
        return;
    const int forced{bendRangeOverride.load(std::memory_order_relaxed)};
    for (int ch = 0; ch < kNumChannels; ++ch) {
        if (forced > 0) {
            fluid_synth_pitch_wheel_sens(synth.get(), ch, forced);
            continue;
        }
        // Back to what the file established. Whole semitones: this must not
        // disturb the live RPN selection, and the file's next RPN restores cents.
        const int range{engineBendRange[ch].load(std::memory_order_relaxed)};
        fluid_synth_pitch_wheel_sens(synth.get(), ch, range >= 0 ? range >> 7 : 2);
    }
}

namespace {
enum GroupKind : juce::uint8 {
    kindSysEx, kindBank, kindProgram, kindRpnSelect, kindRpnNull, kindData, kindOther
};

// Dispatch tier per kind: resets first, then what a Program Change needs, the
// Program Change itself, everything ordinary, and the RPN machinery last - a
// Reset All Controllers written ahead of it in the file still lands ahead.
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

// Slot 0-7 for the eight RPN-machinery controllers. Selector pairs are adjacent
// so a partner is slot ^ 1.
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
        // Straight from the buffer: MidiMessage copies any SysEx longer than
        // four bytes to the heap, and game rips carry a reset at tick 0.
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
    int count{0};
    bool plain{true};
    for (auto it = begin; it != end; ++it) {
        if (count == kMaxGroupEvents) {
            // More than the scratch holds: the host's order, nothing dropped.
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
        ++count;
    }
    if (count > 1 && !plain) {
        for (int ch = 0; ch < kNumChannels; ++ch)
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

// Puts one channel's RPN machinery - selectors, nulls, Data Entries - into
// select -> write -> deselect order without disturbing a file that already
// has it.
//
// The events split into runs of selectors and runs of Data Entries. A data run
// and the selector run before it form a unit, which is how a sequenced file
// reads: select, write, usually deselect. Within a unit the k-th write of each
// controller goes after the k-th selection and before the k-th null, which is
// exactly what survives a host that delivers each controller's queue back to
// back. A trailing selector run with no data of its own joins the unit before
// it when it completes that unit - the unit selected nothing yet, or the run
// supplies the other half of a pair whose first half is already there - and
// otherwise stays in buffer order. Nulls ahead of everything else in a unit
// stay ahead, so a defensive null before a selection is left alone.
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
                continue; // leading null: buffer order, ahead of the unit
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
        if (m.isNoteOn()) {
            // Muted, or not soloed while something else is. Drop the note-on and
            // do not record it in the trace: no note sounded. Note-offs, CCs,
            // program changes, and bend still pass through below, so the channel
            // stays in step and unmuting mid-song needs no resync.
            if ((silencedMask.load(std::memory_order_acquire) & (1u << midiCh)) != 0)
                return;
            // Fixed-size diagnostic trace: capture the already-maintained engine
            // snapshot immediately before dispatch. This lets the conformance
            // suite prove reset/Program Change ordering at the sounding note.
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
            lastCcValue[midiCh][m.getControllerNumber()].store(
                m.getControllerValue(), std::memory_order_relaxed);
            lastCcSample[midiCh][m.getControllerNumber()].store(
                samplePosition, std::memory_order_relaxed);
            fluid_synth_cc(
                synth.get(),
                midiCh,
                m.getControllerNumber(),
                m.getControllerValue());
            noteControllerForBendRange(
                midiCh, m.getControllerNumber(), m.getControllerValue());
            noteControllerForExpression(
                midiCh, m.getControllerNumber(), m.getControllerValue());
            restoreSixteenChannelLayout(m.getControllerNumber());

            // Mirror mapped sound controllers (CC71-79) into the channel's saved
            // state + sliders. We are on the audio thread: parameter and ValueTree
            // writes are NOT safe here (they'd fire UI listeners synchronously), so
            // capture into atomics and let handleAsyncUpdate apply them on the
            // message thread — same pattern as program changes below.
            if (int idx{ccToIndex(m.getControllerNumber())};
                idx >= 0) {
                engineCc[midiCh][idx].store(m.getControllerValue(), std::memory_order_relaxed);
                midiCcValue[midiCh][idx].store(m.getControllerValue(), std::memory_order_relaxed);
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

// The one place FluidSynth is asked for audio.
//
// It must be called with an EFFECTS bus: `fluid_synth_process(synth, n, 0,
// nullptr, 2, out)` renders the dry voices and DISCARDS the reverb and chorus
// buses, which is why Juicy16's reverb was inaudible before 0.6.0-alpha.1.
// `nfx=2` returns one stereo bus carrying reverb and chorus together; chorus is
// switched off in createSynth so enabling this did not un-mute an effect nobody
// chose.
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
            buffer.copyFrom(0, startSample + rendered, stereoScratch, 0, 0, chunk);
            buffer.addFrom(0, startSample + rendered, stereoScratch, 1, 0, chunk);
            buffer.applyGain(0, startSample + rendered, chunk, 0.5f);
            rendered += chunk;
        }
    }
}

// Renders `numSamples` of dry audio into `outputs` and adds the effects bus on
// top. `numSamples` must not exceed the preallocated effects scratch.
void FluidSynthModel::renderWithEffects(float* const* outputs, int numSamples) {
    jassert(numSamples <= effectsScratch.getNumSamples());
    effectsScratch.clear(0, 0, numSamples);
    effectsScratch.clear(1, 0, numSamples);
    float* effects[] { effectsScratch.getWritePointer(0),
                       effectsScratch.getWritePointer(1) };
    fluid_synth_process(synth.get(), numSamples, 2, effects, 2,
                        const_cast<float**>(outputs));
    // fluid_synth_process ADDS into its buffers rather than overwriting them, so
    // both the dry and the wet sides accumulate the same way here.
    juce::FloatVectorOperations::add(outputs[0], effects[0], numSamples);
    juce::FloatVectorOperations::add(outputs[1], effects[1], numSamples);
}

void FluidSynthModel::renderIntoFifo(int startSample, int numSamples) {
    if (numSamples <= 0)
        return;
    // fluid_synth_process mixes into its output rather than overwriting it, so
    // the region has to start clean; it still holds the previous block's audio.
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

    // What the interpolator can consume producing numSamples outputs at ratio
    // 1/N, plus the one sample it reads ahead of its fractional position.
    const int required{juce::jmin(oversampleFifo.getNumSamples(),
                                  numSamples / oversampleFactor + 2)};
    const int toRender{juce::jmax(0, required - oversampleFifoFill)};
    const int fifoBase{oversampleFifoFill};

    // Event positions divide by the same factor, so ordering is preserved and
    // timing quantises to one internal sample - 10 microseconds at 96 kHz, which
    // is the price of playing at all at a rate the engine cannot render.
    dispatchTimestampedEvents(
        midiMessages, numSamples, toRender,
        [this, fifoBase](int from, int count) { renderIntoFifo(fifoBase + from, count); },
        [this](int hostPosition) { return hostPosition / oversampleFactor; });
    oversampleFifoFill += toRender;

    float* outputs[2];
    const bool downmix{outputChannels < 2};
    if (downmix) {
        outputs[0] = stereoScratch.getWritePointer(0);
        outputs[1] = stereoScratch.getWritePointer(1);
    } else {
        outputs[0] = buffer.getWritePointer(0);
        outputs[1] = buffer.getWritePointer(1);
    }

    // The interpolator reads ahead of its fractional position, so it can consume
    // one input past the arithmetic. Produce only what the FIFO actually holds:
    // a host block larger than the prepared maximum would otherwise read past the
    // end of the buffer. Such a block ends in silence rather than a bad read, and
    // the scratch bound applies the same way to the mono downmix.
    int produce{juce::jmin(numSamples,
                           juce::jmax(0, (oversampleFifoFill - 2) * oversampleFactor))};
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

    if (downmix) {
        buffer.copyFrom(0, 0, stereoScratch, 0, 0, produce);
        buffer.addFrom(0, 0, stereoScratch, 1, 0, produce);
        buffer.applyGain(0, 0, produce, 0.5f);
    }

    // Keep whatever the interpolator did not consume: it is the head of the next
    // block, so nothing is rendered twice and no sample is silently dropped.
    consumed = juce::jlimit(0, oversampleFifoFill, consumed);
    const int remaining{oversampleFifoFill - consumed};
    if (remaining > 0 && consumed > 0)
        for (int ch = 0; ch < 2; ++ch) {
            float* data{oversampleFifo.getWritePointer(ch)};
            std::memmove(data, data + consumed, sizeof(float) * static_cast<size_t>(remaining));
        }
    oversampleFifoFill = remaining;
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    if (!sampleRateSupported.load(std::memory_order_acquire) || synth == nullptr) {
        buffer.clear();
        return;
    }

    const int numSamples{buffer.getNumSamples()};

    // Reverb settings first, so a note rendered in this block is heard through
    // the reverb this block was asked for rather than the previous one's.
    applyReverbFromAudioThread(numSamples);
    applyBendRangeChangeFromAudioThread();

    // MidiBuffer is timestamp ordered. Render the audio before each event, apply all
    // events at that timestamp in buffer order, then continue. This preserves Bank
    // Select -> Program Change -> Note ordering and avoids quantising every event to
    // the start of the host block.
    if (oversampleFactor <= 1)
        dispatchTimestampedEvents(
            midiMessages, numSamples, numSamples,
            [this, &buffer](int from, int count) { renderSamples(buffer, from, count); },
            [](int hostPosition) { return hostPosition; });
    else
        renderThroughOversampler(buffer, midiMessages, numSamples);

    // Master trim, applied once over the whole block after every segment has been
    // rendered. Smoothed so host automation cannot step the gain mid-block.
    outputLevelSmoother.setTargetValue(outputLevelGain.load(std::memory_order_relaxed));
    outputLevelSmoother.applyGain(buffer, numSamples);
}
