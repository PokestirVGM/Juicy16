//
// Created by Alex Birch on 10/09/2017.
//

#include <iostream>
#include <iterator>
#include <cstring>
#include <fluidsynth.h>
#include "FluidSynthModel.h"
#include "MidiConstants.h"
#include "Util.h"

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

const map<fluid_midi_control_change, String> FluidSynthModel::controllerToParam{
    {SOUND_CTRL2, "filterResonance"}, // MIDI CC 71 Timbre/Harmonic Intensity (filter resonance)
    {SOUND_CTRL3, "release"}, // MIDI CC 72 Release time
    {SOUND_CTRL4, "attack"}, // MIDI CC 73 Attack time
    {SOUND_CTRL5, "filterCutOff"}, // MIDI CC 74 Brightness (cutoff frequency, FILTERFC)
    {SOUND_CTRL6, "decay"}, // MIDI CC 75 Decay Time
    {SOUND_CTRL10, "sustain"}}; // MIDI CC 79 undefined

const map<String, fluid_midi_control_change> FluidSynthModel::paramToController{[]{
    map<String, fluid_midi_control_change> map;
    transform(
        controllerToParam.begin(),
        controllerToParam.end(),
        inserter(map, map.begin()),
        [](const pair<fluid_midi_control_change, String>& pair) {
            return make_pair(pair.second, pair.first);
        });
    return map;
}()};

// fixed index order for the audio-thread CC capture arrays
const fluid_midi_control_change FluidSynthModel::ccIndexOrder[FluidSynthModel::kNumSoundCcs]{
    SOUND_CTRL2, SOUND_CTRL3, SOUND_CTRL4, SOUND_CTRL5, SOUND_CTRL6, SOUND_CTRL10};
thread_local bool FluidSynthModel::mirroringParameters{false};

int FluidSynthModel::ccToIndex(int cc) {
    for (int i = 0; i < kNumSoundCcs; ++i)
        if (static_cast<int>(ccIndexOrder[i]) == cc)
            return i;
    return -1;
}

int FluidSynthModel::defaultParamValue(const String& parameterID) {
    // Sound controllers (attack/decay/sustain/release/cutoff/resonance) are neutral
    // at 64 per the MIDI convention for CC70-79 (and FluidSynth initialises every
    // channel's CC71-79 to 64). bank/preset default to 0.
    return programChangeParams.contains(parameterID) ? 0 : 64;
}

String FluidSynthModel::progParamId(int chZeroBased) {
    return "progCh" + String(chZeroBased + 1);
}

int FluidSynthModel::progParamChannel(const String& parameterID) {
    if (!parameterID.startsWith("progCh"))
        return -1;
    // This parser runs from parameterChanged, which hosts may call on the audio
    // thread. Avoid substring()/numeric String conversion and their temporary
    // allocation for the fixed identifiers progCh1..progCh16.
    const int length{parameterID.length()};
    int oneBased{0};
    if (length == 7 && parameterID[6] >= '1' && parameterID[6] <= '9') {
        oneBased = static_cast<int>(parameterID[6] - '0');
    } else if (length == 8 && parameterID[6] == '1'
               && parameterID[7] >= '0' && parameterID[7] <= '6') {
        oneBased = 10 + static_cast<int>(parameterID[7] - '0');
    }
    return oneBased > 0 ? oneBased - 1 : -1;
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
        lastChannelPressureValue[i].store(-1, std::memory_order_relaxed);
        lastChannelPressureSample[i].store(-1, std::memory_order_relaxed);
        for (int value = 0; value < 128; ++value) {
            lastCcValue[i][value].store(-1, std::memory_order_relaxed);
            lastCcSample[i][value].store(-1, std::memory_order_relaxed);
            lastKeyPressureValue[i][value].store(-1, std::memory_order_relaxed);
            lastKeyPressureSample[i][value].store(-1, std::memory_order_relaxed);
        }
        for (int c = 0; c < kNumSoundCcs; c++) {
            midiCcValue[i][c].store(-1, std::memory_order_relaxed);
            engineCc[i][c].store(64, std::memory_order_relaxed);
        }
    }
    valueTreeState.addParameterListener("bank", this);
    valueTreeState.addParameterListener("preset", this);
    for (const auto &[param, controller]: paramToController) {
        valueTreeState.addParameterListener(param, this);
    }
    for (int ch = 0; ch < kNumChannels; ch++) {
        valueTreeState.addParameterListener(progParamId(ch), this);
    }
    valueTreeState.state.addListener(this);
}

FluidSynthModel::~FluidSynthModel() {
    cancelPendingUpdate();
    clearRepairedTemp();
    for (int ch = 0; ch < kNumChannels; ch++) {
        valueTreeState.removeParameterListener(progParamId(ch), this);
    }
    for (const auto &[param, controller]: paramToController) {
        valueTreeState.removeParameterListener(param, this);
    }
    valueTreeState.removeParameterListener("bank", this);
    valueTreeState.removeParameterListener("preset", this);
    valueTreeState.state.removeListener(this);
}

void FluidSynthModel::initialise() {
    // deactivate all audio drivers in fluidsynth to avoid FL Studio deadlock when initialising CoreAudio
    // after all: we only use fluidsynth to render blocks of audio. it doesn't output to audio driver.
    const char *DRV[] {NULL};
    fluid_audio_driver_register(DRV);
    
    settings = { new_fluid_settings(), delete_fluid_settings };
    
    // https://sourceforge.net/p/fluidsynth/wiki/FluidSettings/
    fluid_settings_setnum(settings.get(), "synth.sample-rate", currentSampleRate);
    // Explicitly retain FluidSynth's API serialization. UI-driven bank/program
    // changes are rare but may overlap host rendering; this prevents concurrent
    // FluidSynth API calls from corrupting its internal state.
    fluid_settings_setint(settings.get(), "synth.threadsafe-api", 1);
    createSynth();
}

void FluidSynthModel::createSynth() {
    synth = { new_fluid_synth(settings.get()), delete_fluid_synth };

    // Gold-standard playback fidelity:
    // - 7th-order ("highest") sample interpolation. FluidSynth defaults to 4th-order,
    //   which audibly rolls off the top octave; 7th-order preserves the high end for
    //   both SF2 and DLS.
    fluid_synth_set_interp_method(synth.get(), -1, FLUID_INTERP_HIGHEST);
    // - Generous polyphony so dense 16-channel multitimbral material never steals
    //   voices (voice stealing cuts note tails / harmonics).
    fluid_synth_set_polyphony(synth.get(), 512);

    // Output gain. FluidSynth's default is 0.2 (headroom for dense polyphony);
    // the old value here was 2.0, which clips hard on 16-channel material —
    // especially in the standalone, where the DAC clamps at 0 dBFS. 1.0 is still
    // 5x FluidSynth's default and comfortably loud without guaranteed clipping.
    fluid_synth_set_gain(synth.get(), 1.0);

    // Sound-controller modulators (CC 71/72/73/74/75/79 -> filter + volume envelope).
    //
    // MIDI (GS/XG convention) and FluidSynth itself treat sound controllers as
    // "64 = no change": fluid_channel_init_ctrl() initialises CC70-79 to 64 on every
    // channel. The old mods here were UNIPOLAR-from-zero, so a channel sitting at
    // the spec-neutral 64 — or any MIDI file innocently sending CC74=64 — had its
    // cutoff dropped ~an octave and its envelope times multiplied ~300x. Measured
    // on a real SF2: −18.7 dB body level, −18.4 dB above 4 kHz, and notes still
    // ringing 0.5 s after note-off. That was the plugin's "missing high end".
    //
    // These are BIPOLAR (64 = neutral, matching the MIDI convention), linear, with
    // amounts spanning a musically useful range in each direction:
    //   CC71 -> FILTERQ        ±960 cB   (FLUID_PEAK_ATTENUATION, full spec range)
    //   CC72 -> VOLENVRELEASE  ±12000 tc (~÷1000..×1000; generator clamps at spec limits)
    //   CC73 -> VOLENVATTACK   ±12000 tc
    //   CC74 -> FILTERFC       ±2400 cents (up = brighter — the old unipolar amount
    //                          was negative, so the "Cut" slider worked inverted)
    //   CC75 -> VOLENVDECAY    ±12000 tc
    //   CC79 -> VOLENVSUSTAIN  ∓1000 cB  (negative amount so slider up = louder sustain;
    //                          the generator clamps to 0..1000 cB attenuation)
    struct SoundCtrlMod { fluid_midi_control_change cc; int gen; float amount; };
    const SoundCtrlMod mods[] {
        { SOUND_CTRL2,  GEN_FILTERQ,        FLUID_PEAK_ATTENUATION }, // CC 71 resonance
        { SOUND_CTRL3,  GEN_VOLENVRELEASE,  12000.0f },               // CC 72 release
        { SOUND_CTRL4,  GEN_VOLENVATTACK,   12000.0f },               // CC 73 attack
        { SOUND_CTRL5,  GEN_FILTERFC,       2400.0f },                // CC 74 brightness
        { SOUND_CTRL6,  GEN_VOLENVDECAY,    12000.0f },               // CC 75 decay
        { SOUND_CTRL10, GEN_VOLENVSUSTAIN,  -1000.0f },               // CC 79 sustain level
    };
    for (const auto& sm : mods) {
        unique_ptr<fluid_mod_t, decltype(&delete_fluid_mod)> mod{new_fluid_mod(), delete_fluid_mod};
        fluid_mod_set_source1(mod.get(),
                              static_cast<int>(sm.cc),
                              FLUID_MOD_CC
                              | FLUID_MOD_BIPOLAR
                              | FLUID_MOD_LINEAR
                              | FLUID_MOD_POSITIVE);
        fluid_mod_set_source2(mod.get(), 0, 0);
        fluid_mod_set_dest(mod.get(), sm.gen);
        fluid_mod_set_amount(mod.get(), sm.amount);
        fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    }
    // NOTE: no CC zeroing here — FluidSynth's per-channel init value of 64 is
    // exactly our neutral point now.
}

void FluidSynthModel::prepareToPlay(double sampleRate, int samplesPerBlock) {
    setSampleRate(static_cast<float>(sampleRate));
    // pre-allocate the mono-downmix scratch off the audio thread
    stereoScratch.setSize(2, jmax(64, samplesPerBlock), false, false, true);
}

const StringArray FluidSynthModel::programChangeParams{"bank", "preset"};
const StringArray FluidSynthModel::perChannelParams{
    "bank", "preset", "attack", "decay", "sustain", "release", "filterCutOff", "filterResonance"};

void FluidSynthModel::syncProgParam(int ch, int preset) {
    juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
    if (auto* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter(progParamId(ch)))})
        *p = preset;
}

void FluidSynthModel::saveParamToChannel(const String& parameterID, int value) {
    if (mirroringParameters)
        return;
    ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
        .getChildWithProperty("num", static_cast<int>(channel.load(std::memory_order_relaxed)))};
    if (chNode.isValid())
        chNode.setProperty(parameterID, value, nullptr);
}

void FluidSynthModel::parameterChanged(const String& parameterID, float /*newValue*/) {
    // While loadingChannel is set, the params are being written to MIRROR state the
    // engine already has (channel switch, MIDI program-change sync, dropdown pick):
    // re-sending it to the synth is at best redundant and at worst applies an
    // invalid intermediate program (bank set before preset), and saving it back
    // would clobber the tree we just read. Skip entirely.
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
        if (fluid_synth_program_change(synth.get(), progCh, program) == FLUID_OK) {
            int sf, bank, preset;
            if (fluid_synth_get_program(synth.get(), progCh, &sf, &bank, &preset) == FLUID_OK) {
                midiBank[progCh].store(bank, std::memory_order_relaxed);
                midiPreset[progCh].store(preset, std::memory_order_relaxed);
                engineBank[progCh].store(bank, std::memory_order_relaxed);
                enginePreset[progCh].store(preset, std::memory_order_relaxed);
                midiProgramDirtyMask.fetch_or(1u << progCh, std::memory_order_release);
                triggerAsyncUpdate();
            }
        }
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
            fluid_synth_program_select(
                synth.get(),
                static_cast<int>(ch),
                fontId,
                bankOffset + bank,
                preset);
            engineBank[ch].store(bankOffset + bank, std::memory_order_relaxed);
            enginePreset[ch].store(preset, std::memory_order_relaxed);
        }
        // persist both bank and preset to the channel currently being edited.
        // Host automation can invoke us on the audio thread, where ValueTree
        // writes are unsafe — defer those through the same atomics the MIDI
        // program-change path uses.
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            saveParamToChannel("bank", bank);
            saveParamToChannel("preset", preset);
        } else if (ch < static_cast<unsigned int>(kNumChannels)) {
            midiBank[ch].store(bankOffset + bank, std::memory_order_relaxed);
            midiPreset[ch].store(preset, std::memory_order_relaxed);
            midiProgramDirtyMask.fetch_or(1u << ch, std::memory_order_release);
            triggerAsyncUpdate();
        }
    } else if (
        // https://stackoverflow.com/a/55482091/5257399
        auto it{paramToController.find(parameterID)};
        it != end(paramToController)) {
        RangedAudioParameter *param{valueTreeState.getParameter(parameterID)};
        jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
        AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
        int value{castParam->get()};
        int controllerNumber{static_cast<int>(it->second)};

        const unsigned int ch{channel.load(std::memory_order_relaxed)};
        if (ch >= static_cast<unsigned int>(kNumChannels))
            return;
        fluid_synth_cc(synth.get(), static_cast<int>(ch), controllerNumber, value);
        if (const int idx{ccToIndex(controllerNumber)}; idx >= 0)
            engineCc[ch][idx].store(value, std::memory_order_relaxed);
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            saveParamToChannel(parameterID, value);
        } else if (int idx{ccToIndex(controllerNumber)}; idx >= 0) {
            // audio-thread automation: defer the tree write to handleAsyncUpdate
            midiCcValue[ch][idx].store(value, std::memory_order_relaxed);
            midiCcDirtyMask.fetch_or(1u << ch, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }
}

void FluidSynthModel::syncToSelectedChannel() {
    int sel{static_cast<int>(valueTreeState.state.getChildWithName("uiState")
        .getProperty("selectedChannel", 1)) - 1};
    loadSelectedChannel(juce::jlimit(0, kNumChannels - 1, sel));
}

void FluidSynthModel::setChannelProgram(int chan, int bank, int preset) {
    if (chan < 0 || chan >= kNumChannels
        || bank < MidiConstants::midiMinValue || bank > 128
        || preset < MidiConstants::midiMinValue || preset > MidiConstants::midiMaxValue)
        return;
    const int fontId{sfont_id.load(std::memory_order_acquire)};
    if (fontId != -1) {
        int bankOffset{fluid_synth_get_bank_offset(synth.get(), fontId)};
        fluid_synth_program_select(
            synth.get(),
            chan,
            fontId,
            bankOffset + bank,
            preset);
        if (chan >= 0 && chan < kNumChannels) {
            engineBank[chan].store(bankOffset + bank, std::memory_order_relaxed);
            enginePreset[chan].store(preset, std::memory_order_relaxed);
        }
    }
    // persist to the channel node (drives the dropdown display + saved state)
    ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
        .getChildWithProperty("num", chan)};
    if (chNode.isValid()) {
        chNode.setProperty("bank", bank, nullptr);
        chNode.setProperty("preset", preset, nullptr);
    }
    // mirror into the channel's program parameter for hosts
    if (chan >= 0 && chan < kNumChannels)
        syncProgParam(chan, preset);
    // if this is the channel the user is currently viewing, keep the global
    // bank/preset params (host program interface + slider sync) consistent.
    // suppress save-back so parameterChanged doesn't re-write the node.
    if (chan == static_cast<int>(channel)) {
        juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
        if (auto* bankParam{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("bank"))})
            *bankParam = bank;
        if (auto* presetParam{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("preset"))})
            *presetParam = preset;
    }
}

void FluidSynthModel::handleAsyncUpdate() {
    // consume per-channel program changes and sound-CC changes captured on the
    // audio thread; all ValueTree/parameter writes happen here, on the message thread.
    const unsigned int pcMask{midiProgramDirtyMask.exchange(0, std::memory_order_acquire)};
    const unsigned int ccMask{midiCcDirtyMask.exchange(0, std::memory_order_acquire)};
    if (pcMask == 0 && ccMask == 0)
        return;
    const int selected{static_cast<int>(channel.load(std::memory_order_relaxed))};
    ValueTree chPrograms{valueTreeState.state.getChildWithName("channelPrograms")};

    if (pcMask != 0) {
        const int fontId{sfont_id.load(std::memory_order_acquire)};
        int bankOffset{fontId == -1 ? 0 : fluid_synth_get_bank_offset(synth.get(), fontId)};
        AudioParameterInt* bankParam{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("bank"))};
        AudioParameterInt* presetParam{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("preset"))};

        for (int ch = 0; ch < kNumChannels; ch++) {
            if ((pcMask & (1u << ch)) == 0)
                continue;
            int bank{midiBank[ch].load(std::memory_order_relaxed) - bankOffset};
            int preset{midiPreset[ch].load(std::memory_order_relaxed)};

            // update the channel list display
            ValueTree chNode{chPrograms.getChildWithProperty("num", ch)};
            if (chNode.isValid()) {
                chNode.setProperty("bank", bank, nullptr);
                chNode.setProperty("preset", preset, nullptr);
            }
            // keep the channel's program parameter (host automation lane / VST3
            // unit program) mirroring the engine
            syncProgParam(ch, preset);
            // if this is the channel the user is viewing, move the dropdown highlight
            if (ch == selected) {
                juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
                if (bankParam)   *bankParam = bank;
                if (presetParam) *presetParam = preset;
            }
        }
    }

    for (int ch = 0; ch < kNumChannels && ccMask != 0; ch++) {
        if ((ccMask & (1u << ch)) == 0)
            continue;
        ValueTree chNode{chPrograms.getChildWithProperty("num", ch)};
        for (int idx = 0; idx < kNumSoundCcs; idx++) {
            const int value{midiCcValue[ch][idx].exchange(-1, std::memory_order_relaxed)};
            if (value < 0)
                continue; // nothing pending for this CC
            const String& paramID{controllerToParam.at(ccIndexOrder[idx])};
            if (chNode.isValid())
                chNode.setProperty(paramID, value, nullptr);
            // if this is the channel the user is viewing, move its slider too
            if (ch == selected) {
                juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
                if (auto* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter(paramID))})
                    *p = value;
            }
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
    // push the saved values into the params; this drives the sliders and channel
    // dropdowns to display the newly-selected channel. The guard makes
    // parameterChanged skip both the engine re-send and the save-back — the engine
    // already holds these values for this channel.
    juce::ScopedValueSetter<bool> guard{mirroringParameters, true};
    for (const String& p : perChannelParams) {
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
            if (bookmark.getBinaryData()->getSize() > 0) {
                CFUniquePtr<CFDataRef> data{CFDataCreate(
                    NULL,
                    static_cast<const UInt8 *>(bookmark.getBinaryData()->getData()),
                    static_cast<CFIndex>(bookmark.getBinaryData()->getSize()))};
                CFUniquePtr<CFURLRef> cfURL{CFURLCreateByResolvingBookmarkData(NULL, data.get(), kCFURLBookmarkResolutionWithSecurityScope, NULL, NULL, NULL, &cfError)};
                if (cfURL) {
                    CFUniquePtr<CFStringRef> cfPath {CFURLCopyFileSystemPath(cfURL.get(), CFURLPathStyle::kCFURLPOSIXPathStyle)};
                    StringRef path {String::fromCFString(cfPath.get())};
                    if (path.isNotEmpty()) {
                        CFURLStartAccessingSecurityScopedResource(cfURL.get());
                        unloadAndLoadFont(path);
                        CFURLStopAccessingSecurityScopedResource(cfURL.get());
                        loadedViaBookmark = true;
                    }
                }
            }
            if (cfError != nullptr)
                CFRelease(cfError);
            if (!loadedViaBookmark) {
                String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
                if (soundFontPath.isNotEmpty()) {
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

bool FluidSynthModel::getChannelProgram(int channelToRead, int& bank, int& preset) const {
    if (channelToRead < 0 || channelToRead >= kNumChannels)
        return false;
    int soundFontId{-1};
    return fluid_synth_get_program(
        synth.get(), channelToRead, &soundFontId, &bank, &preset) == FLUID_OK;
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

String FluidSynthModel::getLoadedFontPath() const {
    return valueTreeState.state.getChildWithName("soundFont")
        .getProperty("loadedPath", "").toString();
}

bool FluidSynthModel::unloadAndLoadFont(const String& absPath) {
    const juce::File requested{absPath};
    if (absPath.isEmpty() || !requested.existsAsFile()) {
        publishFontLoadResult(false, absPath, "The selected bank file is missing or unreadable.", false);
        return false;
    }

    String pathToLoad{absPath};
    // Build a repaired candidate without disturbing the active bank. The candidate
    // temp file becomes owned by the model only after FluidSynth accepts it.
    juce::File repaired{writeRepairedTempCopy(requested)};
    if (repaired.existsAsFile())
        pathToLoad = repaired.getFullPathName();

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
                ? "FluidSynth could not load this SF2, SF3, or DLS bank."
                : "The selected bank contains no playable presets.",
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

juce::File FluidSynthModel::writeRepairedTempCopy(const juce::File& src) {
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
            bankMap[bankNum].appendChild({ "preset", {
                { "num", fluid_preset_get_num(preset) },
                { "name", String{fluid_preset_get_name(preset)} }
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
            if (!exists) {
                rawBank = fallbackBank;
                rawPreset = fallbackPreset;
            }
            // apply it to the synth so this channel actually plays its saved instrument
            fluid_synth_program_select(
                synth.get(),
                chNum,
                fontId,
                bankOffset + rawBank,
                rawPreset);
            engineBank[chNum].store(bankOffset + rawBank, std::memory_order_relaxed);
            enginePreset[chNum].store(rawPreset, std::memory_order_relaxed);
            // mirror into the display and the channel's program parameter
            ch.setProperty("bank", rawBank, nullptr);
            ch.setProperty("preset", rawPreset, nullptr);
            syncProgParam(chNum, rawPreset);
            // re-apply this channel's saved envelope/filter sliders (64 = neutral)
            for (const auto& [paramID, cc] : paramToController) {
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
    if (sampleRate <= 0.0f || std::abs(sampleRate - currentSampleRate) < 0.01f)
        return;
    currentSampleRate = sampleRate;
    if (!settings)
        return;

    // FluidSynth 2.4+ deliberately rejects fluid_synth_set_sample_rate(). Recreate
    // the synth at the host rate before playback, then restore the bank and all
    // per-channel state through the normal font-load path.
    synth.reset();
    sfont_id.store(-1, std::memory_order_release);
    clearRepairedTemp();
    fluid_settings_setnum(settings.get(), "synth.sample-rate", sampleRate);
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
    if (midiCh < 0 || midiCh >= kNumChannels)
        return;
    // MIDI from the DAW is authoritative: apply the program change to this
    // channel, then capture the resulting program so the message thread can
    // update the channel list / params (see handleAsyncUpdate).
    if (fluid_synth_program_change(synth.get(), midiCh, program) == FLUID_OK) {
        int sf, bank, preset;
        if (fluid_synth_get_program(synth.get(), midiCh, &sf, &bank, &preset) == FLUID_OK) {
            midiBank[midiCh].store(bank, std::memory_order_relaxed);
            midiPreset[midiCh].store(preset, std::memory_order_relaxed);
            engineBank[midiCh].store(bank, std::memory_order_relaxed);
            enginePreset[midiCh].store(preset, std::memory_order_relaxed);
            midiProgramDirtyMask.fetch_or(1u << midiCh, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }
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

void FluidSynthModel::dispatchMidiEvent(const MidiMessage& m, int samplePosition) {
#if JUICYSF_TRACE_MIDI
        DEBUG_PRINT(m.getDescription());
#endif

        if (m.isSysEx()) {
            fluid_synth_sysex(
                synth.get(),
                reinterpret_cast<const char*>(m.getSysExData()),
                m.getSysExDataSize(),
                nullptr,
                nullptr,
                nullptr,
                static_cast<int>(false));

            if (isSystemResetSysex(m.getSysExData(), m.getSysExDataSize())) {
                const int fontId{sfont_id.load(std::memory_order_acquire)};
                if (fontId != -1) {
                    for (int ch = 0; ch < kNumChannels; ch++) {
                        fluid_synth_program_select(
                            synth.get(),
                            ch,
                            fontId,
                            engineBank[ch].load(std::memory_order_relaxed),
                            enginePreset[ch].load(std::memory_order_relaxed));
                        for (int idx = 0; idx < kNumSoundCcs; ++idx)
                            fluid_synth_cc(
                                synth.get(), ch, static_cast<int>(ccIndexOrder[idx]),
                                engineCc[ch][idx].load(std::memory_order_relaxed));
                    }
                }
            }
            return;
        }

        const int channelIndex{m.getChannel() - 1}; // JUCE: 1-16, FluidSynth: 0-15
        if (channelIndex < 0 || channelIndex >= kNumChannels)
            return;
        const int midiCh{channelIndex};
        if (m.isNoteOn()) {
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
            fluid_synth_pitch_bend(
                synth.get(),
                midiCh,
                m.getPitchWheelValue());
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

void FluidSynthModel::renderSamples(AudioBuffer<float>& buffer, int startSample, int numSamples) {
    if (numSamples <= 0)
        return;

    const int numChannels{buffer.getNumChannels()};
    if (numChannels >= 2) {
        float* outputs[] { buffer.getWritePointer(0, startSample),
                           buffer.getWritePointer(1, startSample) };
        fluid_synth_process(synth.get(), numSamples, 0, nullptr, 2, outputs);
        return;
    }

    if (numChannels == 1) {
        const int scratchCapacity{stereoScratch.getNumSamples()};
        jassert(scratchCapacity > 0);
        for (int rendered = 0; rendered < numSamples;) {
            const int chunk{juce::jmin(numSamples - rendered, scratchCapacity)};
            stereoScratch.clear(0, 0, chunk);
            stereoScratch.clear(1, 0, chunk);
            fluid_synth_process(
                synth.get(), chunk, 0, nullptr, 2,
                const_cast<float**>(stereoScratch.getArrayOfWritePointers()));
            buffer.copyFrom(0, startSample + rendered, stereoScratch, 0, 0, chunk);
            buffer.addFrom(0, startSample + rendered, stereoScratch, 1, 0, chunk);
            buffer.applyGain(0, startSample + rendered, chunk, 0.5f);
            rendered += chunk;
        }
    }
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    const int numSamples{buffer.getNumSamples()};
    int renderPosition{0};

    // MidiBuffer is timestamp ordered. Render the audio before each event, apply all
    // events at that timestamp in buffer order, then continue. This preserves Bank
    // Select -> Program Change -> Note ordering and avoids quantising every event to
    // the start of the host block.
    for (const auto metadata : midiMessages) {
        const int eventPosition{juce::jlimit(0, numSamples, metadata.samplePosition)};
        if (eventPosition > renderPosition) {
            renderSamples(buffer, renderPosition, eventPosition - renderPosition);
            renderPosition = eventPosition;
        }
        dispatchMidiEvent(metadata.getMessage(), eventPosition);
    }

    renderSamples(buffer, renderPosition, numSamples - renderPosition);
}
