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
    const int ch{parameterID.substring(6).getIntValue() - 1};
    return (ch >= 0 && ch < kNumChannels) ? ch : -1;
}

FluidSynthModel::FluidSynthModel(
    AudioProcessorValueTreeState& valueTreeState
    )
: valueTreeState{valueTreeState}
, settings{nullptr, nullptr}
, synth{nullptr, nullptr}
, currentSampleRate{44100}
, sfont_id{-1}
, channel{0}
{
    for (int i = 0; i < kNumChannels; i++) {
        midiBank[i].store(0, std::memory_order_relaxed);
        midiPreset[i].store(0, std::memory_order_relaxed);
        for (int c = 0; c < kNumSoundCcs; c++)
            midiCcValue[i][c].store(-1, std::memory_order_relaxed);
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
#if JUCE_DEBUG
    fluid_settings_setint(settings.get(), "synth.verbose", 1);
#endif

    fluid_settings_setnum(settings.get(), "synth.sample-rate", currentSampleRate);
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
    juce::ScopedValueSetter<bool> guard{loadingChannel, true};
    if (auto* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter(progParamId(ch)))})
        *p = preset;
}

void FluidSynthModel::saveParamToChannel(const String& parameterID, int value) {
    if (loadingChannel)
        return;
    ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
        .getChildWithProperty("num", static_cast<int>(channel.load(std::memory_order_relaxed)))};
    if (chNode.isValid())
        chNode.setProperty(parameterID, value, nullptr);
}

void FluidSynthModel::parameterChanged(const String& parameterID, float newValue) {
    // While loadingChannel is set, the params are being written to MIRROR state the
    // engine already has (channel switch, MIDI program-change sync, dropdown pick):
    // re-sending it to the synth is at best redundant and at worst applies an
    // invalid intermediate program (bank set before preset), and saving it back
    // would clobber the tree we just read. Skip entirely.
    if (loadingChannel)
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
        int bankOffset{fluid_synth_get_bank_offset(synth.get(), sfont_id)};
        const unsigned int ch{channel.load(std::memory_order_relaxed)};
        fluid_synth_program_select(
            synth.get(),
            ch,
            sfont_id,
            static_cast<unsigned int>(bankOffset + bank),
            static_cast<unsigned int>(preset));
        // persist both bank and preset to the channel currently being edited.
        // Host automation can invoke us on the audio thread, where ValueTree
        // writes are unsafe — defer those through the same atomics the MIDI
        // program-change path uses.
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            saveParamToChannel("bank", bank);
            saveParamToChannel("preset", preset);
        } else {
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
        fluid_synth_cc(
            synth.get(),
            ch,
            controllerNumber,
            value);
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
    loadSelectedChannel(sel);
}

void FluidSynthModel::setChannelProgram(int chan, int bank, int preset) {
    if (sfont_id != -1) {
        int bankOffset{fluid_synth_get_bank_offset(synth.get(), sfont_id)};
        fluid_synth_program_select(
            synth.get(),
            static_cast<unsigned int>(chan),
            sfont_id,
            static_cast<unsigned int>(bankOffset + bank),
            static_cast<unsigned int>(preset));
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
        juce::ScopedValueSetter<bool> guard{loadingChannel, true};
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
        int bankOffset{sfont_id == -1 ? 0 : fluid_synth_get_bank_offset(synth.get(), sfont_id)};
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
                juce::ScopedValueSetter<bool> guard{loadingChannel, true};
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
                juce::ScopedValueSetter<bool> guard{loadingChannel, true};
                if (auto* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter(paramID))})
                    *p = value;
            }
        }
    }
}

void FluidSynthModel::loadSelectedChannel(int newChannel) {
    channel.store(static_cast<unsigned int>(newChannel), std::memory_order_relaxed);
    ValueTree chNode{valueTreeState.state.getChildWithName("channelPrograms")
        .getChildWithProperty("num", newChannel)};
    if (!chNode.isValid())
        return;
    // push the saved values into the params; this drives the sliders and channel
    // dropdowns to display the newly-selected channel. The guard makes
    // parameterChanged skip both the engine re-send and the save-back — the engine
    // already holds these values for this channel.
    juce::ScopedValueSetter<bool> guard{loadingChannel, true};
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
        loadSelectedChannel(newChannel);
        return;
    }
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")) {
#if JUCE_MAC || JUCE_IOS
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
    fluid_synth_cc(
        synth.get(),
        channel.load(std::memory_order_relaxed),
        controller,
        value);
}

void FluidSynthModel::unloadAndLoadFont(const String &absPath) {
    // in the base case, there is no font loaded
    if (fluid_synth_sfcount(synth.get()) > 0) {
        // if -1 is returned, that indicates failure
        // not really sure how to handle "fail to unload"
        fluid_synth_sfunload(synth.get(), sfont_id, 1);
        sfont_id = -1;
    }
    // the previous font is unloaded; its repaired temp copy (if any) is no longer needed
    clearRepairedTemp();
    loadFont(absPath);
}

void FluidSynthModel::loadFont(const String &absPath) {
    if (!absPath.isEmpty()) {
        String pathToLoad{absPath};
        // Malformed-but-playable DLS files (e.g. Awave Studio exports with bad RIFF
        // sizes) are rejected by FluidSynth's strict DLS parser, which reads the file
        // directly and ignores sfloader callbacks. So repair a copy to a temp file
        // and load that instead. Well-formed files (and all SF2/SF3) load as-is.
        juce::File repaired{writeRepairedTempCopy(juce::File{absPath})};
        if (repaired.existsAsFile()) {
            repairedTempFile = repaired; // keep alive for the loaded font's lifetime
            pathToLoad = repaired.getFullPathName();
        }
        sfont_id = fluid_synth_sfload(synth.get(), pathToLoad.toStdString().c_str(), 1);
        // if -1 is returned, that indicates failure
    }
    // refresh regardless of success, if only to clear the table
    refreshBanks();
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
    fluid_sfont_t* sfont{
        sfont_id == -1
        ? nullptr
        : fluid_synth_get_sfont_by_id(synth.get(), sfont_id)
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
    if (sfont_id != -1) {
        int bankOffset{fluid_synth_get_bank_offset(synth.get(), sfont_id)};
        ValueTree firstBank{banks.getChild(0)};
        int fallbackBank{firstBank.isValid() ? static_cast<int>(firstBank.getProperty("num")) : 0};
        ValueTree firstPreset{firstBank.isValid() ? firstBank.getChild(0) : ValueTree{}};
        int fallbackPreset{firstPreset.isValid() ? static_cast<int>(firstPreset.getProperty("num")) : 0};

        ValueTree chPrograms{valueTreeState.state.getChildWithName("channelPrograms")};
        for (int i = 0; i < chPrograms.getNumChildren(); i++) {
            ValueTree ch{chPrograms.getChild(i)};
            int chNum{ch.getProperty("num")};

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
                static_cast<unsigned int>(chNum),
                sfont_id,
                static_cast<unsigned int>(bankOffset + rawBank),
                static_cast<unsigned int>(rawPreset));
            // mirror into the display and the channel's program parameter
            ch.setProperty("bank", rawBank, nullptr);
            ch.setProperty("preset", rawPreset, nullptr);
            if (chNum >= 0 && chNum < kNumChannels)
                syncProgParam(chNum, rawPreset);
            // re-apply this channel's saved envelope/filter sliders (64 = neutral)
            for (const auto& [paramID, cc] : paramToController) {
                fluid_synth_cc(
                    synth.get(),
                    static_cast<unsigned int>(chNum),
                    static_cast<int>(cc),
                    static_cast<int>(ch.getProperty(paramID, defaultParamValue(paramID))));
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
    if (sampleRate <= 0.0f || sampleRate == currentSampleRate)
        return;
    currentSampleRate = sampleRate;
    if (settings)
        fluid_settings_setnum(settings.get(), "synth.sample-rate", sampleRate);
    if (synth) {
        // The synth is created (at the default rate) in the constructor, before the
        // host tells us the real rate via prepareToPlay. Without this the synth would
        // render at 44.1 kHz regardless of the host — wrong pitch and resampling on
        // 48/96 kHz projects. set_sample_rate is deprecated (FluidSynth prefers
        // creating the synth at the target rate) but it's the only in-place option,
        // and prepareToPlay calls us before any audio, so DSP state is fresh.
#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        fluid_synth_set_sample_rate(synth.get(), sampleRate);
#if defined(__clang__)
 #pragma clang diagnostic pop
#endif
    }
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    MidiBuffer processedMidi;
    int time;
    MidiMessage m;

    for (MidiBuffer::Iterator i{midiMessages}; i.getNextEvent(m, time);) {
        DEBUG_PRINT(m.getDescription());
        
        const unsigned int midiCh{static_cast<unsigned int>(m.getChannel() - 1)}; // JUCE: 1-16, FluidSynth: 0-15
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
                idx >= 0 && midiCh < static_cast<unsigned int>(kNumChannels)) {
                midiCcValue[midiCh][idx].store(m.getControllerValue(), std::memory_order_relaxed);
                midiCcDirtyMask.fetch_or(1u << midiCh, std::memory_order_release);
                triggerAsyncUpdate();
            }
        } else if (m.isProgramChange()) {
            // MIDI from the DAW is authoritative: apply the program change to this
            // channel, then capture the resulting program so the message thread can
            // update the channel list / params (see handleAsyncUpdate).
            int result{fluid_synth_program_change(
                synth.get(),
                midiCh,
                m.getProgramChangeNumber())};
            if (result == FLUID_OK) {
                int sf, bank, preset;
                if (fluid_synth_get_program(synth.get(), static_cast<int>(midiCh), &sf, &bank, &preset) == FLUID_OK) {
                    midiBank[midiCh].store(bank, std::memory_order_relaxed);
                    midiPreset[midiCh].store(preset, std::memory_order_relaxed);
                    midiProgramDirtyMask.fetch_or(1u << midiCh, std::memory_order_release);
                    triggerAsyncUpdate();
                }
            }
        } else if (m.isPitchWheel()) {
            fluid_synth_pitch_bend(
                synth.get(),
                midiCh,
                m.getPitchWheelValue());
        } else if (m.isChannelPressure()) {
            fluid_synth_channel_pressure(
                synth.get(),
                midiCh,
                m.getChannelPressureValue());
        } else if (m.isAftertouch()) {
            fluid_synth_key_pressure(
                synth.get(),
                midiCh,
                m.getNoteNumber(),
                m.getAfterTouchValue());
//        } else if (m.isMetaEvent()) {
//            fluid_midi_event_t *midi_event{new_fluid_midi_event()};
//            fluid_midi_event_set_type(midi_event, static_cast<int>(MIDI_SYSTEM_RESET));
//            fluid_synth_handle_midi_event(synth.get(), midi_event);
//            delete_fluid_midi_event(midi_event);
        } else if (m.isSysEx()) {
            fluid_synth_sysex(
                synth.get(),
                reinterpret_cast<const char*>(m.getSysExData()),
                m.getSysExDataSize(),
                nullptr, // no response pointer because we have no interest in handling response currently
                nullptr, // no response_len pointer because we have no interest in handling response currently
                nullptr, // no handled pointer because we have no interest in handling response currently
                static_cast<int>(false));
        }
    }

    const int numSamples{buffer.getNumSamples()};
    const int numChannels{buffer.getNumChannels()};

    if (numChannels >= 2) {
        fluid_synth_process(
            synth.get(),
            numSamples,
            0,
            nullptr,
            2, // FluidSynth renders dry audio in stereo pairs; extra host channels stay cleared
            const_cast<float**>(buffer.getArrayOfWritePointers()));
    } else if (numChannels == 1) {
        // FluidSynth can only render stereo pairs; passing a single buffer fails
        // silently. Render into the stereo scratch and downmix so mono hosts/devices
        // still get audio.
        if (stereoScratch.getNumSamples() < numSamples)
            stereoScratch.setSize(2, numSamples, false, false, true); // rare: host exceeded prepareToPlay block size
        stereoScratch.clear(0, 0, numSamples);
        stereoScratch.clear(1, 0, numSamples);
        fluid_synth_process(
            synth.get(),
            numSamples,
            0,
            nullptr,
            2,
            const_cast<float**>(stereoScratch.getArrayOfWritePointers()));
        buffer.copyFrom(0, 0, stereoScratch, 0, 0, numSamples);
        buffer.addFrom(0, 0, stereoScratch, 1, 0, numSamples);
        buffer.applyGain(0, 0, numSamples, 0.5f); // equal-power-ish L+R average
    }
}
