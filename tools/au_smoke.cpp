//
// Minimal Audio Unit host: loads the built Juicy16.component in-process and
// drives it the way a host does, so AU-specific behaviour is covered by an
// automated test rather than only by `auval`.
//
// The AU is registered from its own bundle with AudioComponentRegister and the
// factory function named in its Info.plist, so nothing is installed into
// ~/Library/Audio/Plug-Ins/Components and an already-installed copy can never be
// tested by mistake.
//
// Covers: component metadata, instantiation and lifecycle, stream format,
// per-channel Program Change delivery through MusicDeviceMIDIEvent, per-channel
// audio isolation, the progChN parameter mirror, and ClassInfo state save and
// restore.
//
// Build:
//   clang++ -std=c++17 tools/au_smoke.cpp -framework AudioToolbox \
//       -framework CoreFoundation -o /tmp/au_smoke
// Run:
//   /tmp/au_smoke "build/JuicySFPlugin_artefacts/Debug/AU/Juicy16.component" [bank]
//
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

std::string toUtf8(CFStringRef string)
{
    if (string == nullptr)
        return {};
    const CFIndex bytes{CFStringGetMaximumSizeForEncoding(
        CFStringGetLength(string), kCFStringEncodingUTF8) + 1};
    std::string out(static_cast<size_t>(bytes), '\0');
    if (!CFStringGetCString(string, out.data(), bytes, kCFStringEncodingUTF8))
        return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

OSType fourCC(CFStringRef string)
{
    const auto text{toUtf8(string)};
    if (text.size() != 4)
        return 0;
    return static_cast<OSType>(
        (static_cast<unsigned char>(text[0]) << 24)
        | (static_cast<unsigned char>(text[1]) << 16)
        | (static_cast<unsigned char>(text[2]) << 8)
        | static_cast<unsigned char>(text[3]));
}

// JUCE's binary plugin state: a little-endian magic number, the XML length, the
// single-line XML, and a terminating zero. See AudioProcessor::copyXmlToBinary.
std::vector<uint8_t> juceStateForBank(const std::string& bankPath)
{
    std::string xml{
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<MYPLUGINSETTINGS stateVersion=\"2\">"
        "<uiState width=\"850\" height=\"650\" selectedChannel=\"1\"/>"
        "<soundFont path=\""};
    for (const char character : bankPath) {
        switch (character) {
            case '&': xml += "&amp;"; break;
            case '<': xml += "&lt;"; break;
            case '>': xml += "&gt;"; break;
            case '"': xml += "&quot;"; break;
            default: xml += character; break;
        }
    }
    xml += "\" bookmark=\"\"/></MYPLUGINSETTINGS>";

    std::vector<uint8_t> state(8 + xml.size() + 1, 0);
    const uint32_t magic{0x21324356};
    const uint32_t length{static_cast<uint32_t>(xml.size())};
    std::memcpy(state.data(), &magic, 4);
    std::memcpy(state.data() + 4, &length, 4);
    std::memcpy(state.data() + 8, xml.data(), xml.size());
    return state;
}

// JUCE delivers parameter and state updates on the message thread, so a host
// that never runs its loop sees none of them. Pumping the run loop here is what
// a real host does between render calls.
void pumpMessageLoop(double seconds = 0.05)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
}

struct RenderBuffers {
    explicit RenderBuffers(UInt32 frames)
        : left(frames), right(frames)
    {
        list.mNumberBuffers = 2;
        for (int channel = 0; channel < 2; ++channel) {
            list.mBuffers[channel].mNumberChannels = 1;
            list.mBuffers[channel].mDataByteSize = frames * sizeof(float);
        }
        list.mBuffers[0].mData = left.data();
        list.mBuffers[1].mData = right.data();
    }

    std::vector<float> left, right;
    AudioBufferList list{};
    // AudioBufferList declares one trailing buffer; the second lives here.
    AudioBuffer secondBuffer{};
};

double renderMagnitude(AudioUnit unit, UInt32 frames, double& sampleTime)
{
    RenderBuffers buffers{frames};
    AudioUnitRenderActionFlags flags{0};
    AudioTimeStamp timeStamp{};
    timeStamp.mFlags = kAudioTimeStampSampleTimeValid;
    timeStamp.mSampleTime = sampleTime;
    if (AudioUnitRender(unit, &flags, &timeStamp, 0, frames, &buffers.list) != noErr)
        return -1.0;
    sampleTime += frames;

    double sum{0.0};
    for (UInt32 frame = 0; frame < frames; ++frame)
        sum += std::abs(buffers.left[frame]) + std::abs(buffers.right[frame]);
    return sum;
}

bool sendMidi(AudioUnit unit, UInt32 status, UInt32 data1, UInt32 data2, UInt32 offset)
{
    return MusicDeviceMIDIEvent(unit, status, data1, data2, offset) == noErr;
}

void allSoundOff(AudioUnit unit)
{
    for (int channel = 0; channel < 16; ++channel)
        sendMidi(unit, 0xb0u | static_cast<UInt32>(channel), 120, 0, 0);
}

std::vector<AudioUnitParameterID> parameterList(AudioUnit unit)
{
    UInt32 size{0};
    if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList,
                                 kAudioUnitScope_Global, 0, &size, nullptr) != noErr)
        return {};
    std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
    if (ids.empty())
        return {};
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList,
                             kAudioUnitScope_Global, 0, ids.data(), &size) != noErr)
        return {};
    return ids;
}

std::string parameterName(AudioUnit unit, AudioUnitParameterID identifier)
{
    AudioUnitParameterInfo info{};
    UInt32 size{sizeof(info)};
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo,
                             kAudioUnitScope_Global, identifier, &info, &size) != noErr)
        return {};
    if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0
        && info.cfNameString != nullptr) {
        const auto name{toUtf8(info.cfNameString)};
        if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0)
            CFRelease(info.cfNameString);
        return name;
    }
    return std::string{info.name};
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: JuicySFAUSmoke <Juicy16.component> [bank]\n");
        return 2;
    }

    const std::string bundlePath{argv[1]};
    const std::string bankPath{argc > 2 ? argv[2] : std::string{}};
    std::printf("== Juicy16 AU host smoke ==\n  bundle: %s\n", bundlePath.c_str());

    CFURLRef bundleUrl{CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(bundlePath.c_str()),
        static_cast<CFIndex>(bundlePath.size()), true)};
    CFBundleRef bundle{bundleUrl != nullptr
        ? CFBundleCreate(kCFAllocatorDefault, bundleUrl) : nullptr};
    if (bundleUrl != nullptr)
        CFRelease(bundleUrl);
    check(bundle != nullptr, "the AU bundle opens");
    if (bundle == nullptr)
        return 1;

    // Read the component description from the bundle's own Info.plist rather
    // than hard-coding it, so the metadata the host would see is what is tested.
    auto* components{static_cast<CFArrayRef>(
        CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("AudioComponents")))};
    const bool haveComponents{components != nullptr
        && CFGetTypeID(components) == CFArrayGetTypeID()
        && CFArrayGetCount(components) == 1};
    check(haveComponents, "Info.plist declares exactly one AudioComponent");
    if (!haveComponents)
        return 1;

    auto* entry{static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(components, 0))};
    const auto stringValue{[entry](CFStringRef key) {
        return static_cast<CFStringRef>(CFDictionaryGetValue(entry, key));
    }};
    AudioComponentDescription description{};
    description.componentType = fourCC(stringValue(CFSTR("type")));
    description.componentSubType = fourCC(stringValue(CFSTR("subtype")));
    description.componentManufacturer = fourCC(stringValue(CFSTR("manufacturer")));
    CFStringRef factoryName{stringValue(CFSTR("factoryFunction"))};
    CFStringRef componentName{stringValue(CFSTR("name"))};

    check(description.componentType == 'aumu'
              && description.componentSubType == 'Jc16'
              && description.componentManufacturer == 'Pkst',
          "the AU declares the frozen music-device type, subtype, and manufacturer");

    const bool loaded{CFBundleLoadExecutable(bundle) != false};
    check(loaded, "the AU executable loads");
    auto factory{reinterpret_cast<AudioComponentFactoryFunction>(
        factoryName != nullptr
            ? CFBundleGetFunctionPointerForName(bundle, factoryName) : nullptr)};
    check(factory != nullptr, "the factory function named in Info.plist is exported");
    if (factory == nullptr)
        return 1;

    AudioComponent component{AudioComponentRegister(
        &description, componentName != nullptr ? componentName : CFSTR("Juicy16"),
        1, factory)};
    check(component != nullptr, "the component registers from its own bundle");
    if (component == nullptr)
        return 1;

    constexpr double sampleRate{48000.0};
    constexpr UInt32 frames{1024};

    const auto openUnit{[&](AudioUnit& unit) {
        if (AudioComponentInstanceNew(component, &unit) != noErr)
            return false;
        AudioStreamBasicDescription format{};
        format.mSampleRate = sampleRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked
                            | kAudioFormatFlagIsNonInterleaved;
        format.mFramesPerPacket = 1;
        format.mChannelsPerFrame = 2;
        format.mBitsPerChannel = 32;
        format.mBytesPerFrame = 4;
        format.mBytesPerPacket = 4;
        if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 0, &format, sizeof(format))
            != noErr)
            return false;
        UInt32 maximumFrames{frames};
        if (AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                 kAudioUnitScope_Global, 0, &maximumFrames,
                                 sizeof(maximumFrames)) != noErr)
            return false;
        return AudioUnitInitialize(unit) == noErr;
    }};

    AudioUnit unit{nullptr};
    check(openUnit(unit), "the AU instantiates, accepts 48 kHz stereo, and initializes");
    if (unit == nullptr)
        return 1;
    pumpMessageLoop();

    const auto parameters{parameterList(unit)};
    std::vector<AudioUnitParameterID> programParameters(16, 0);
    int namedProgramParameters{0};
    for (const auto identifier : parameters) {
        const auto name{parameterName(unit, identifier)};
        static const std::string prefix{"program for MIDI channel "};
        if (name.rfind(prefix, 0) != 0)
            continue;
        const int channel{std::atoi(name.c_str() + prefix.size())};
        if (channel >= 1 && channel <= 16) {
            programParameters[static_cast<size_t>(channel - 1)] = identifier;
            ++namedProgramParameters;
        }
    }
    // 3 global (bank, preset, outputLevel) + 64 per-channel mixer + 16 program.
    check(parameters.size() == 83 && namedProgramParameters == 16,
          "the AU publishes 83 parameters including one program parameter per MIDI channel");

    // Every channel's mixer controls are host-visible in AU too, by name, which
    // is what a host's automation list enumerates.
    int namedMixerParameters{0};
    for (const auto identifier : parameters) {
        const auto name{parameterName(unit, identifier)};
        for (const char* prefix : {"volume (CC7) for MIDI channel ",
                                   "pan (CC10) for MIDI channel ",
                                   "mute MIDI channel ",
                                   "solo MIDI channel "}) {
            const std::string expected{prefix};
            if (name.rfind(expected, 0) != 0)
                continue;
            const int channel{std::atoi(name.c_str() + expected.size())};
            if (channel >= 1 && channel <= 16)
                ++namedMixerParameters;
        }
    }
    check(namedMixerParameters == 64,
          "the AU publishes volume, pan, mute and solo for each of the 16 MIDI channels");

    double sampleTime{0.0};

    if (bankPath.empty()) {
        std::printf("  (no bank argument: skipping playback and state coverage)\n");
    } else {
        const auto state{juceStateForBank(bankPath)};
        CFPropertyListRef classInfo{nullptr};
        UInt32 size{sizeof(classInfo)};
        const bool readClassInfo{
            AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo,
                                 kAudioUnitScope_Global, 0, &classInfo, &size) == noErr
            && classInfo != nullptr
            && CFGetTypeID(classInfo) == CFDictionaryGetTypeID()};
        check(readClassInfo, "the AU reports ClassInfo state");

        bool bankLoaded{false};
        if (readClassInfo) {
            CFMutableDictionaryRef withBank{CFDictionaryCreateMutableCopy(
                nullptr, 0, static_cast<CFDictionaryRef>(classInfo))};
            CFDataRef data{CFDataCreate(nullptr, state.data(),
                                        static_cast<CFIndex>(state.size()))};
            CFDictionarySetValue(withBank, CFSTR("jucePluginState"), data);
            CFPropertyListRef toRestore{withBank};
            bankLoaded = AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo,
                                              kAudioUnitScope_Global, 0, &toRestore,
                                              sizeof(toRestore)) == noErr;
            CFRelease(data);
            CFRelease(withBank);
            pumpMessageLoop(0.5);
        }
        if (classInfo != nullptr)
            CFRelease(classInfo);
        check(bankLoaded, "a bank loads through AU state restoration");

        // Per-channel Program Change through the AU MIDI entry point. Each
        // channel gets its own program; the mirror parameters must follow, and a
        // change on one channel must not move another.
        for (int channel = 0; channel < 16; ++channel)
            sendMidi(unit, 0xc0u | static_cast<UInt32>(channel),
                     static_cast<UInt32>(channel), 0, 0);
        renderMagnitude(unit, frames, sampleTime);
        pumpMessageLoop(0.3);

        int matchingPrograms{0};
        for (int channel = 0; channel < 16; ++channel) {
            AudioUnitParameterValue value{-1.0f};
            if (AudioUnitGetParameter(unit, programParameters[static_cast<size_t>(channel)],
                                      kAudioUnitScope_Global, 0, &value) == noErr
                && static_cast<int>(std::lround(value)) == channel)
                ++matchingPrograms;
        }
        check(matchingPrograms == 16,
              "each channel's Program Change is mirrored onto its own program parameter and no other");

        // Channel isolation in the audio domain: with everything silenced, only
        // the channel being auditioned may produce output.
        int soundingChannels{0};
        for (int channel = 0; channel < 16; ++channel) {
            allSoundOff(unit);
            renderMagnitude(unit, frames, sampleTime);
            sendMidi(unit, 0x90u | static_cast<UInt32>(channel), 60, 100, 0);
            const double magnitude{renderMagnitude(unit, frames, sampleTime)};
            sendMidi(unit, 0x80u | static_cast<UInt32>(channel), 60, 0, 0);
            if (magnitude > 0.001)
                ++soundingChannels;
        }
        allSoundOff(unit);
        renderMagnitude(unit, frames, sampleTime);
        check(soundingChannels == 16,
              "all 16 MIDI channels sound independently through the AU");

        // Save, disturb, restore: the host contract that keeps a reopened project
        // on the instruments the user chose.
        CFPropertyListRef saved{nullptr};
        size = sizeof(saved);
        const bool savedOk{
            AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo,
                                 kAudioUnitScope_Global, 0, &saved, &size) == noErr
            && saved != nullptr};
        for (int channel = 0; channel < 16; ++channel)
            sendMidi(unit, 0xc0u | static_cast<UInt32>(channel), 0, 0, 0);
        renderMagnitude(unit, frames, sampleTime);
        pumpMessageLoop(0.3);

        bool restored{savedOk};
        if (savedOk) {
            restored = AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo,
                                            kAudioUnitScope_Global, 0, &saved,
                                            sizeof(saved)) == noErr;
            pumpMessageLoop(0.5);
            for (int channel = 0; channel < 16; ++channel) {
                AudioUnitParameterValue value{-1.0f};
                restored = restored
                    && AudioUnitGetParameter(
                           unit, programParameters[static_cast<size_t>(channel)],
                           kAudioUnitScope_Global, 0, &value) == noErr
                    && static_cast<int>(std::lround(value)) == channel;
            }
            CFRelease(saved);
        }
        check(restored,
              "AU state save and restore returns every channel to its saved program");
    }

    check(AudioUnitUninitialize(unit) == noErr
              && AudioComponentInstanceDispose(unit) == noErr,
          "the AU uninitializes and disposes cleanly");

    // A host that rescans, or a user who reopens a project, instantiates again.
    AudioUnit second{nullptr};
    const bool reopened{openUnit(second)};
    if (second != nullptr) {
        pumpMessageLoop();
        AudioUnitUninitialize(second);
        AudioComponentInstanceDispose(second);
    }
    check(reopened, "a second instance opens after the first is disposed");

    std::printf("== au_smoke: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
