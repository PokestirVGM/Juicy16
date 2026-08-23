//
// Minimal VST3 host: loads the built Juicy16.vst3, instantiates component +
// controller like a DAW, and verifies the per-channel unit machinery:
//   1. controller exposes the patched wrapper's IUnitInfo before connection
//   2. 17 units (root + Ch 1..16), each channel unit with the shared program list
//   3. getUnitByBus maps MIDI channel N -> unit N
//   4. the progChN parameters' unitIds (assigned by JUCE from parameter groups)
//      match the unit IDs our IUnitInfo reports — the invariant Cubase needs
//
// Build:
//   clang++ -std=c++17 tools/vst3_smoke.cpp -I "$SDK" -framework CoreFoundation -o /tmp/vst3_smoke
//   (SDK = JUCE's bundled VST3_SDK; interfaces are header-only, nothing links)
// Run:
//   /tmp/vst3_smoke "build/JuicySFPlugin_artefacts/Debug/VST3/Juicy16.vst3"
//
#include <cstdio>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>

#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstunits.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/vstspeaker.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>

using namespace Steinberg;

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  PASS  %s\n", name); \
    else { printf("  FAIL  %s\n", name); ++failures; } \
} while (0)

// Exact IDs are a Beta 1 compatibility surface, not values to regenerate from
// whatever hashing behavior a future framework version happens to use.
static constexpr Vst::UnitID expectedUnitIds[16]{
    0x2B6251C8, 0x2B6251C9, 0x2B6251CA, 0x2B6251CB,
    0x2B6251CC, 0x2B6251CD, 0x2B6251CE, 0x2B6251CF,
    0x2B6251D0, 0x40E7E768, 0x40E7E769, 0x40E7E76A,
    0x40E7E76B, 0x40E7E76C, 0x40E7E76D, 0x40E7E76E
};
static constexpr Vst::ParamID expectedProgramParamIds[16]{
    0x6D8E6EB2, 0x6D8E6EB3, 0x6D8E6EB4, 0x6D8E6EB5,
    0x6D8E6EB6, 0x6D8E6EB7, 0x6D8E6EB8, 0x6D8E6EB9,
    0x6D8E6EBA, 0x443F67BE, 0x443F67BF, 0x443F67C0,
    0x443F67C1, 0x443F67C2, 0x443F67C3, 0x443F67C4
};
// JUCE derives a VST3 ParamID by hashing the parameter's string id, so these
// change whenever a parameter is renamed - which is exactly why they are frozen
// here. Order: bank, preset, outputLevel, then volCh1..16, panCh1..16,
// muteCh1..16, soloCh1..16. All of these live in the ROOT unit; only the 16
// progChN parameters live in the channel units.
// reverbOn. Named because the timing scenarios below have to bypass the reverb
// to measure the dry path they are actually about.
static constexpr Vst::ParamID kReverbOnParamId = 0x703BC751;
static constexpr Vst::ParamID expectedRootParamIds[73]{
    0x002E063C, 0x4594E2DF, 0x4DCA0B03,
    // reverbOn, reverbProfile, reverbSize, reverbDamp, reverbWidth, reverbLevel
    0x703BC751, 0x5D46E777, 0x506904F3,
    0x506213D2, 0x3CEFA714, 0x3C5314D2,
    // volCh1..volCh16
    0x4FAA2A99, 0x4FAA2A9A, 0x4FAA2A9B, 0x4FAA2A9C,
    0x4FAA2A9D, 0x4FAA2A9E, 0x4FAA2A9F, 0x4FAA2AA0,
    0x4FAA2AA1, 0x259B28B7, 0x259B28B8, 0x259B28B9,
    0x259B28BA, 0x259B28BB, 0x259B28BC, 0x259B28BD,
    // panCh1..panCh16
    0x44A8B68F, 0x44A8B690, 0x44A8B691, 0x44A8B692,
    0x44A8B693, 0x44A8B694, 0x44A8B695, 0x44A8B696,
    0x44A8B697, 0x506E1B81, 0x506E1B82, 0x506E1B83,
    0x506E1B84, 0x506E1B85, 0x506E1B86, 0x506E1B87,
    // muteCh1..muteCh16
    0x543FD393, 0x543FD394, 0x543FD395, 0x543FD396,
    0x543FD397, 0x543FD398, 0x543FD399, 0x543FD39A,
    0x543FD39B, 0x33BA9EFD, 0x33BA9EFE, 0x33BA9EFF,
    0x33BA9F00, 0x33BA9F01, 0x33BA9F02, 0x33BA9F03,
    // soloCh1..soloCh16
    0x06FBF30D, 0x06FBF30E, 0x06FBF30F, 0x06FBF310,
    0x06FBF311, 0x06FBF312, 0x06FBF313, 0x06FBF314,
    0x06FBF315, 0x58826EC3, 0x58826EC4, 0x58826EC5,
    0x58826EC6, 0x58826EC7, 0x58826EC8, 0x58826EC9
};
static constexpr int numRootParamIds =
    (int) (sizeof (expectedRootParamIds) / sizeof (expectedRootParamIds[0]));
static Vst::UnitID expectedUnitId (int chZero) {
    return expectedUnitIds[chZero];
}

static std::string toUtf8 (const Vst::TChar* s) {
    std::string out;
    for (int i = 0; s[i] != 0 && i < 128; ++i)
        out += (char) (s[i] < 128 ? s[i] : '?');
    return out;
}

class ReadOnlyMemoryStream final : public IBStream {
public:
    explicit ReadOnlyMemoryStream(std::vector<std::uint8_t> bytesIn)
        : bytes(std::move(bytesIn)) {}

    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown_iid)
            || FUnknownPrivate::iidEqual(iid, IBStream_iid)) {
            addRef();
            *object = static_cast<IBStream*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override {
        const auto remaining{--references};
        if (remaining == 0) delete this;
        return remaining;
    }
    tresult PLUGIN_API read(void* buffer, int32 requested, int32* readCount) override {
        if (requested < 0 || buffer == nullptr) return kInvalidArgument;
        const auto available{bytes.size() - std::min(position, bytes.size())};
        const auto amount{std::min<std::size_t>(static_cast<std::size_t>(requested), available)};
        std::memcpy(buffer, bytes.data() + position, amount);
        position += amount;
        if (readCount != nullptr) *readCount = static_cast<int32>(amount);
        return kResultTrue;
    }
    tresult PLUGIN_API write(void*, int32, int32*) override { return kNotImplemented; }
    tresult PLUGIN_API seek(int64 offset, int32 mode, int64* result) override {
        int64 origin{};
        if (mode == kIBSeekCur) origin = static_cast<int64>(position);
        else if (mode == kIBSeekEnd) origin = static_cast<int64>(bytes.size());
        else if (mode != kIBSeekSet) return kInvalidArgument;
        const auto next{origin + offset};
        if (next < 0 || next > static_cast<int64>(bytes.size())) return kInvalidArgument;
        position = static_cast<std::size_t>(next);
        if (result != nullptr) *result = next;
        return kResultTrue;
    }
    tresult PLUGIN_API tell(int64* current) override {
        if (current == nullptr) return kInvalidArgument;
        *current = static_cast<int64>(position);
        return kResultTrue;
    }

private:
    std::atomic<uint32> references{1};
    std::vector<std::uint8_t> bytes;
    std::size_t position{};
};

class WriteMemoryStream final : public IBStream {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown_iid)
            || FUnknownPrivate::iidEqual(iid, IBStream_iid)) {
            addRef();
            *object = static_cast<IBStream*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override {
        const auto remaining{--references};
        if (remaining == 0) delete this;
        return remaining;
    }
    tresult PLUGIN_API read(void* buffer, int32 requested, int32* readCount) override {
        if (requested < 0 || buffer == nullptr) return kInvalidArgument;
        const auto available{bytes.size() - std::min(position, bytes.size())};
        const auto amount{std::min<std::size_t>(static_cast<std::size_t>(requested), available)};
        std::memcpy(buffer, bytes.data() + position, amount);
        position += amount;
        if (readCount != nullptr) *readCount = static_cast<int32>(amount);
        return kResultTrue;
    }
    tresult PLUGIN_API write(void* buffer, int32 requested, int32* written) override {
        if (requested < 0 || (requested > 0 && buffer == nullptr)) return kInvalidArgument;
        const auto amount{static_cast<std::size_t>(requested)};
        if (position + amount > bytes.size()) bytes.resize(position + amount);
        if (amount > 0) std::memcpy(bytes.data() + position, buffer, amount);
        position += amount;
        if (written != nullptr) *written = requested;
        return kResultTrue;
    }
    tresult PLUGIN_API seek(int64 offset, int32 mode, int64* result) override {
        int64 origin{};
        if (mode == kIBSeekCur) origin = static_cast<int64>(position);
        else if (mode == kIBSeekEnd) origin = static_cast<int64>(bytes.size());
        else if (mode != kIBSeekSet) return kInvalidArgument;
        const auto next{origin + offset};
        if (next < 0) return kInvalidArgument;
        position = static_cast<std::size_t>(next);
        if (position > bytes.size()) bytes.resize(position);
        if (result != nullptr) *result = next;
        return kResultTrue;
    }
    tresult PLUGIN_API tell(int64* current) override {
        if (current == nullptr) return kInvalidArgument;
        *current = static_cast<int64>(position);
        return kResultTrue;
    }

    const std::vector<std::uint8_t>& data() const { return bytes; }

private:
    std::atomic<uint32> references{1};
    std::vector<std::uint8_t> bytes;
    std::size_t position{};
};

class ParameterQueue final : public Vst::IParamValueQueue {
public:
    explicit ParameterQueue(Vst::ParamID parameterId) : id(parameterId) {}

    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown_iid)
            || FUnknownPrivate::iidEqual(iid, Vst::IParamValueQueue_iid)) {
            addRef();
            *object = static_cast<Vst::IParamValueQueue*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override {
        const auto remaining{--references};
        if (remaining == 0) delete this;
        return remaining;
    }
    Vst::ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points.size()); }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset,
                                Vst::ParamValue& value) override {
        if (index < 0 || index >= static_cast<int32>(points.size())) return kInvalidArgument;
        sampleOffset = points[static_cast<std::size_t>(index)].first;
        value = points[static_cast<std::size_t>(index)].second;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32 sampleOffset, Vst::ParamValue value,
                                int32& index) override {
        if (sampleOffset < 0 || value < 0.0 || value > 1.0) return kInvalidArgument;
        points.emplace_back(sampleOffset, value);
        index = static_cast<int32>(points.size() - 1);
        return kResultTrue;
    }

private:
    std::atomic<uint32> references{1};
    Vst::ParamID id;
    std::vector<std::pair<int32, Vst::ParamValue>> points;
};

class ParameterChanges final : public Vst::IParameterChanges {
public:
    ~ParameterChanges() {
        for (auto* queue : queues) queue->release();
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown_iid)
            || FUnknownPrivate::iidEqual(iid, Vst::IParameterChanges_iid)) {
            addRef();
            *object = static_cast<Vst::IParameterChanges*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override { return --references; }
    int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(queues.size()); }
    Vst::IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        return index >= 0 && index < static_cast<int32>(queues.size())
            ? queues[static_cast<std::size_t>(index)] : nullptr;
    }
    Vst::IParamValueQueue* PLUGIN_API addParameterData(const Vst::ParamID& id,
                                                       int32& index) override {
        for (std::size_t i = 0; i < queues.size(); ++i) {
            if (queues[i]->getParameterId() == id) {
                index = static_cast<int32>(i);
                return queues[i];
            }
        }
        auto* queue = new ParameterQueue(id);
        queues.push_back(queue);
        index = static_cast<int32>(queues.size() - 1);
        return queue;
    }

    bool addPoint(Vst::ParamID id, int32 sampleOffset, Vst::ParamValue value) {
        int32 queueIndex{-1};
        auto* queue{addParameterData(id, queueIndex)};
        int32 pointIndex{-1};
        return queue != nullptr
            && queue->addPoint(sampleOffset, value, pointIndex) == kResultTrue;
    }

private:
    std::atomic<uint32> references{1};
    std::vector<ParameterQueue*> queues;
};

class EventList final : public Vst::IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown_iid)
            || FUnknownPrivate::iidEqual(iid, Vst::IEventList_iid)) {
            addRef();
            *object = static_cast<Vst::IEventList*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override { return --references; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events.size()); }
    tresult PLUGIN_API getEvent(int32 index, Vst::Event& event) override {
        if (index < 0 || index >= static_cast<int32>(events.size())) return kInvalidArgument;
        event = events[static_cast<std::size_t>(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Vst::Event& event) override {
        events.push_back(event);
        return kResultTrue;
    }

    void addNoteOn(int channel, int pitch, int velocity, int sampleOffset) {
        Vst::Event event{};
        event.busIndex = 0;
        event.sampleOffset = sampleOffset;
        event.type = Vst::Event::kNoteOnEvent;
        event.noteOn.channel = static_cast<int16>(channel);
        event.noteOn.pitch = static_cast<int16>(pitch);
        event.noteOn.velocity = static_cast<float>(velocity) / 127.0f;
        event.noteOn.noteId = channel * 128 + pitch;
        events.push_back(event);
    }

    void addSysEx(const std::vector<std::uint8_t>& bytes, int sampleOffset) {
        sysExStorage.push_back(bytes);
        const auto& stored{sysExStorage.back()};
        Vst::Event event{};
        event.busIndex = 0;
        event.sampleOffset = sampleOffset;
        event.type = Vst::Event::kDataEvent;
        event.data.type = Vst::DataEvent::kMidiSysEx;
        event.data.size = static_cast<uint32>(stored.size());
        event.data.bytes = stored.data();
        events.push_back(event);
    }

private:
    std::atomic<uint32> references{1};
    std::vector<Vst::Event> events;
    std::deque<std::vector<std::uint8_t>> sysExStorage;
};

class HostUnitHandler final : public Vst::IComponentHandler, public Vst::IUnitHandler {
public:
    struct Edit {
        Vst::ParamID id{Vst::kNoParamId};
        Vst::ParamValue value{};
    };

    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override {
        if (FUnknownPrivate::iidEqual(iid, FUnknown_iid)
            || FUnknownPrivate::iidEqual(iid, Vst::IComponentHandler_iid)) {
            addRef();
            *object = static_cast<Vst::IComponentHandler*>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, Vst::IUnitHandler_iid)) {
            addRef();
            *object = static_cast<Vst::IUnitHandler*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override {
        const auto remaining{--references};
        if (remaining == 0) delete this;
        return remaining;
    }
    tresult PLUGIN_API beginEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) override {
        edits.push_back({id, value});
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
    tresult PLUGIN_API notifyUnitSelection(Vst::UnitID) override { return kResultOk; }
    tresult PLUGIN_API notifyProgramListChange(Vst::ProgramListID listId,
                                               int32 programIndex) override {
        ++programListNotifications;
        lastListId = listId;
        lastProgramIndex = programIndex;
        return kResultOk;
    }

    int programListNotifications{};
    Vst::ProgramListID lastListId{Vst::kNoProgramListId};
    int32 lastProgramIndex{};
    std::vector<Edit> edits;

    void clearEdits() { edits.clear(); }

private:
    std::atomic<uint32> references{1};
};

static std::vector<std::uint8_t> makeXmlState(const std::string& fontPath) {
    const std::string xml{"<MYPLUGINSETTINGS stateVersion=\"2\"><soundFont path=\""
                          + fontPath + "\" bookmark=\"\"/></MYPLUGINSETTINGS>"};
    std::vector<std::uint8_t> result;
    const auto appendLittleEndian32 = [&result](std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte)
            result.push_back(static_cast<std::uint8_t>((value >> (byte * 8)) & 0xff));
    };
    appendLittleEndian32(0x21324356);
    appendLittleEndian32(static_cast<std::uint32_t>(xml.size()));
    result.insert(result.end(), xml.begin(), xml.end());
    result.push_back(0);
    return result;
}

static std::string xmlFromBinaryState(const std::vector<std::uint8_t>& state) {
    if (state.size() < 9) return {};
    const auto readLittleEndian32 = [&state](std::size_t offset) {
        std::uint32_t value{};
        for (int byte = 0; byte < 4; ++byte)
            value |= static_cast<std::uint32_t>(state[offset + static_cast<std::size_t>(byte)])
                << (byte * 8);
        return value;
    };
    if (readLittleEndian32(0) != 0x21324356) return {};
    const auto length{static_cast<std::size_t>(readLittleEndian32(4))};
    if (length == 0 || length > state.size() - 8) return {};
    return {reinterpret_cast<const char*>(state.data() + 8), length};
}

static bool elementAttribute(const std::string& xml,
                             const std::string& elementStart,
                             const std::string& attribute,
                             std::string& value) {
    const auto start{xml.find(elementStart)};
    if (start == std::string::npos) return false;
    const auto end{xml.find('>', start)};
    if (end == std::string::npos) return false;
    const auto marker{" " + attribute + "=\""};
    const auto attributeStart{xml.find(marker, start)};
    if (attributeStart == std::string::npos || attributeStart >= end) return false;
    const auto valueStart{attributeStart + marker.size()};
    const auto valueEnd{xml.find('"', valueStart)};
    if (valueEnd == std::string::npos || valueEnd > end) return false;
    value = xml.substr(valueStart, valueEnd - valueStart);
    return true;
}

static bool channelAttribute(const std::string& xml,
                             int channel,
                             const std::string& attribute,
                             int& value) {
    std::size_t cursor{};
    while ((cursor = xml.find("<ch ", cursor)) != std::string::npos) {
        const auto end{xml.find('>', cursor)};
        if (end == std::string::npos) return false;
        std::string number;
        const auto element{xml.substr(cursor, end - cursor + 1)};
        if (elementAttribute(element, "<ch ", "num", number)
            && std::stoi(number) == channel) {
            std::string attributeValue;
            if (!elementAttribute(element, "<ch ", attribute, attributeValue)) return false;
            value = std::stoi(attributeValue);
            return true;
        }
        cursor = end + 1;
    }
    return false;
}

static double audioMagnitude(const std::array<float, 512>& left,
                             const std::array<float, 512>& right,
                             int start,
                             int length) {
    double result{};
    for (int sample = start; sample < start + length; ++sample)
        result += std::abs(left[static_cast<std::size_t>(sample)])
            + std::abs(right[static_cast<std::size_t>(sample)]);
    return result;
}

// Normalized cross-correlation of the same segment in two renderings. Identical
// synth state gives ~1.0; a different instrument on the same note drops well
// below that, because the programs differ in spectrum rather than fundamental.
static double waveformCorrelation(const std::array<float, 512>& leftA,
                                  const std::array<float, 512>& rightA,
                                  const std::array<float, 512>& leftB,
                                  const std::array<float, 512>& rightB,
                                  int start,
                                  int length) {
    double dot{}, energyA{}, energyB{};
    for (int sample = start; sample < start + length; ++sample) {
        const auto index{static_cast<std::size_t>(sample)};
        for (const auto& pair : {std::pair<double, double>{leftA[index], leftB[index]},
                                 std::pair<double, double>{rightA[index], rightB[index]}}) {
            dot += pair.first * pair.second;
            energyA += pair.first * pair.first;
            energyB += pair.second * pair.second;
        }
    }
    return dot / std::sqrt(energyA * energyB + 1.0e-30);
}

struct ProgramFixtureEvent {
    std::string type;
    int channel{-1};
    int sample{-1};
    int data1{-1};
    int data2{-1};
};

struct ProgramFixtureScenario {
    std::string name;
    std::string source;
    std::vector<ProgramFixtureEvent> events;
    std::array<int, 16> expectedBanks{};
    std::array<int, 16> expectedPrograms{};
    bool restartTransport{};
};

static bool loadProgramFixture(const std::string& path,
                               std::vector<ProgramFixtureScenario>& scenarios,
                               std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "cannot open fixture: " + path;
        return false;
    }

    std::string line;
    int lineNumber{};
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty() || line[0] == '#' || line.rfind("scenario,", 0) == 0)
            continue;
        std::vector<std::string> fields;
        std::stringstream row(line);
        std::string field;
        while (std::getline(row, field, ',')) fields.push_back(field);
        if (fields.size() != 7) {
            error = "fixture line " + std::to_string(lineNumber) + " must have 7 fields";
            return false;
        }
        ProgramFixtureEvent event;
        try {
            event.type = fields[2];
            event.channel = std::stoi(fields[3]);
            event.sample = std::stoi(fields[4]);
            event.data1 = std::stoi(fields[5]);
            event.data2 = std::stoi(fields[6]);
        } catch (...) {
            error = "fixture line " + std::to_string(lineNumber) + " has invalid integers";
            return false;
        }
        if ((fields[1] != "mapping" && fields[1] != "unit")
            || event.sample < 0 || event.sample >= 512
            || (event.type != "reset" && event.type != "transport"
                && (event.channel < 1 || event.channel > 16))) {
            error = "fixture line " + std::to_string(lineNumber) + " is out of range";
            return false;
        }

        auto scenario = std::find_if(
            scenarios.begin(), scenarios.end(), [&](const auto& candidate) {
                return candidate.name == fields[0];
            });
        if (scenario == scenarios.end()) {
            ProgramFixtureScenario added;
            added.name = fields[0];
            added.source = fields[1];
            added.expectedBanks.fill(-1);
            added.expectedPrograms.fill(-1);
            scenarios.push_back(std::move(added));
            scenario = std::prev(scenarios.end());
        }
        if (scenario->source != fields[1]) {
            error = "fixture scenario " + fields[0] + " mixes parameter sources";
            return false;
        }
        if (event.type == "checkpoint") {
            scenario->expectedBanks[static_cast<std::size_t>(event.channel - 1)] = event.data1;
            scenario->expectedPrograms[static_cast<std::size_t>(event.channel - 1)] = event.data2;
        } else if (event.type == "transport") {
            if (event.channel != 0 || event.sample != 0
                || event.data1 != 0 || event.data2 != 0
                || scenario->restartTransport) {
                error = "fixture line " + std::to_string(lineNumber)
                    + " has an invalid or duplicate transport restart";
                return false;
            }
            scenario->restartTransport = true;
        } else {
            scenario->events.push_back(event);
        }
    }

    bool sawMapping{};
    bool sawUnit{};
    bool sawTransportRestart{};
    std::array<bool, 4> resetKinds{};
    bool fixtureComplete{scenarios.size() >= 3};
    for (const auto& scenario : scenarios) {
        sawMapping = sawMapping || scenario.source == "mapping";
        sawUnit = sawUnit || scenario.source == "unit";
        sawTransportRestart = sawTransportRestart || scenario.restartTransport;
        std::array<int, 16> programCounts{};
        int notes{};
        for (const auto& event : scenario.events) {
            if (event.type == "reset" && event.data1 >= 1 && event.data1 <= 3)
                resetKinds[static_cast<std::size_t>(event.data1)] = true;
            else if (event.type == "program"
                     && event.data1 >= 0 && event.data1 <= 127)
                ++programCounts[static_cast<std::size_t>(event.channel - 1)];
            else if (event.type == "note"
                     && event.data1 >= 0 && event.data1 <= 127
                     && event.data2 >= 1 && event.data2 <= 127)
                ++notes;
            else if (event.type != "cc"
                     || event.data1 < 0 || event.data1 > 127
                     || event.data2 < 0 || event.data2 > 127)
                fixtureComplete = false;
        }
        for (int channel = 0; channel < 16; ++channel)
            fixtureComplete = fixtureComplete
                && programCounts[static_cast<std::size_t>(channel)] > 0
                && scenario.expectedBanks[static_cast<std::size_t>(channel)] >= 0
                && scenario.expectedPrograms[static_cast<std::size_t>(channel)] >= 0;
        fixtureComplete = fixtureComplete && notes > 0;
    }
    fixtureComplete = fixtureComplete && sawMapping && sawUnit
        && sawTransportRestart
        && resetKinds[1] && resetKinds[2] && resetKinds[3];
    if (!fixtureComplete)
        error = "fixture must cover mapping/unit routes, all channels, notes, checkpoints, transport restart, and GM/GS/XG resets";
    return fixtureComplete;
}

static std::vector<std::uint8_t> resetSysEx(int resetKind) {
    // JUCE's VST3 bridge requires kMidiSysEx data to include MIDI's 0xf0/0xf7
    // framing, then removes it before constructing the internal MidiMessage.
    if (resetKind == 1)
        return {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
    if (resetKind == 2)
        return {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7};
    if (resetKind == 3)
        return {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7};
    return {};
}

int main (int argc, char** argv) {
    if (argc < 2) {
        printf ("usage: vst3_smoke <bundle.vst3> [font.dls] [program-fixture.csv]\n");
        return 2;
    }
    const std::string bundlePath = argv[1];

    // find the binary inside the bundle
    std::string binName = bundlePath.substr (bundlePath.find_last_of ('/') + 1);
    binName = binName.substr (0, binName.size() - 5); // strip ".vst3"
    const std::string dylibPath = bundlePath + "/Contents/MacOS/" + binName;

    void* handle = dlopen (dylibPath.c_str(), RTLD_NOW);
    CHECK (handle != nullptr, "dlopen plugin binary");
    if (!handle) { printf ("  dlerror: %s\n", dlerror()); return 1; }

    // macOS VST3 entry: bundleEntry(CFBundleRef)
    using BundleEntryFn = bool (*) (CFBundleRef);
    auto* bundleEntry = (BundleEntryFn) dlsym (handle, "bundleEntry");
    CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) bundlePath.c_str(),
                                                            (CFIndex) bundlePath.size(), true);
    CFBundleRef bundle = CFBundleCreate (nullptr, url);
    CFRelease (url);
    CHECK (bundleEntry != nullptr, "bundleEntry symbol");
    if (bundleEntry) CHECK (bundleEntry (bundle), "bundleEntry(bundle)");

    using GetFactoryFn = IPluginFactory* (*) ();
    auto* getFactory = (GetFactoryFn) dlsym (handle, "GetPluginFactory");
    CHECK (getFactory != nullptr, "GetPluginFactory symbol");
    if (!getFactory) return 1;
    IPluginFactory* factory = getFactory();
    CHECK (factory != nullptr, "factory");

    // find the audio effect class
    PClassInfo ci{};
    TUID effectCid{};
    bool found = false;
    for (int32 i = 0; i < factory->countClasses(); ++i) {
        factory->getClassInfo (i, &ci);
        if (strcmp (ci.category, kVstAudioEffectClass) == 0) {
            memcpy (effectCid, ci.cid, sizeof (TUID));
            found = true;
            break;
        }
    }
    CHECK (found, "kVstAudioEffectClass found");

    Vst::IComponent* component = nullptr;
    factory->createInstance (effectCid, Vst::IComponent_iid, (void**) &component);
    CHECK (component != nullptr, "component created");
    if (!component) return 1;
    CHECK (component->initialize (nullptr) == kResultOk, "component->initialize");

    TUID controllerCid{};
    CHECK (component->getControllerClassId (controllerCid) == kResultOk, "getControllerClassId");
    Vst::IEditController* controller = nullptr;
    factory->createInstance (controllerCid, Vst::IEditController_iid, (void**) &controller);
    CHECK (controller != nullptr, "controller created");
    if (!controller) return 1;
    CHECK (controller->initialize (nullptr) == kResultOk, "controller->initialize");

    Vst::UnitID earlyUnitIds[16]{};
    Vst::UnitID earlyBusUnitIds[16]{};
    Vst::ProgramListID earlyProgramListId = Vst::kNoProgramListId;
    Vst::ParamID unitProgramParamIds[16]{};
    Vst::ParamID mappedProgramParamIds[16]{};

    // ---- PRE-CONNECTION probe (Cubase's order): hosts may interrogate the unit
    // structure immediately after initialize, BEFORE the component/controller
    // connection that hands JUCE's controller its AudioProcessor. Whatever is
    // answered here is what a caching host believes forever.
    {
        Vst::IUnitInfo* early = nullptr;
        controller->queryInterface (Vst::IUnitInfo_iid, (void**) &early);
        if (early == nullptr)
            printf ("  pre-connect: controller has NO IUnitInfo\n");
        else {
            printf ("  pre-connect: unit count = %d, programListCount = %d\n",
                    early->getUnitCount(), early->getProgramListCount());
            CHECK (early->getUnitCount() == 17,
                   "PRE-CONNECT unit count is 17 (host cache sees the real structure)");
            Vst::ProgramListInfo earlyList{};
            CHECK (early->getProgramListCount() == 1
                       && early->getProgramListInfo (0, earlyList) == kResultTrue
                       && earlyList.programCount == 128,
                   "PRE-CONNECT shared 128-entry program list is available");
            earlyProgramListId = earlyList.id;
            bool earlyUnitsOk = true;
            for (int ch = 0; ch < 16; ++ch) {
                Vst::UnitInfo info{};
                Vst::UnitID uid = -1;
                earlyUnitsOk = earlyUnitsOk
                    && early->getUnitInfo (ch + 1, info) == kResultTrue
                    && info.id == expectedUnitId (ch)
                    && info.programListId == earlyList.id
                    && early->getUnitByBus (
                           Vst::MediaTypes::kEvent, Vst::BusDirections::kInput,
                           0, static_cast<int32> (ch), uid) == kResultTrue
                    && uid == expectedUnitId (ch);
                earlyUnitIds[ch] = info.id;
                earlyBusUnitIds[ch] = uid;
            }
            CHECK (earlyUnitsOk,
                   "PRE-CONNECT all 16 channel units and bus mappings are stable");
            Vst::UnitInfo invalidUnit{};
            Vst::ProgramListInfo invalidList{};
            Vst::String128 invalidName{};
            Vst::UnitID invalidBusUnit = -1;
            CHECK (early->getUnitInfo (-1, invalidUnit) != kResultTrue
                       && early->getUnitInfo (17, invalidUnit) != kResultTrue,
                   "PRE-CONNECT invalid unit indices are rejected");
            CHECK (early->getProgramListInfo (-1, invalidList) != kResultTrue
                       && early->getProgramListInfo (1, invalidList) != kResultTrue,
                   "PRE-CONNECT invalid program-list indices are rejected");
            CHECK (early->getProgramName (earlyList.id, -1, invalidName) != kResultTrue
                       && early->getProgramName (earlyList.id, 128, invalidName) != kResultTrue
                       && early->getProgramName (earlyList.id + 1, 0, invalidName) != kResultTrue,
                   "PRE-CONNECT invalid programs and list IDs are rejected");
            CHECK (early->getUnitByBus (Vst::MediaTypes::kAudio, Vst::BusDirections::kInput,
                                        0, 0, invalidBusUnit) != kResultTrue
                       && early->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kOutput,
                                               0, 0, invalidBusUnit) != kResultTrue
                       && early->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput,
                                               1, 0, invalidBusUnit) != kResultTrue
                       && early->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput,
                                               0, -1, invalidBusUnit) != kResultTrue
                       && early->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput,
                                               0, 16, invalidBusUnit) != kResultTrue,
                   "PRE-CONNECT invalid bus queries are rejected");
            early->release();
        }
    }

    // Connect component <-> controller like a real host: JUCE's controller only
    // learns its AudioProcessor instance (and thus consults the plugin's
    // VST3ClientExtensions in queryInterface) once the connection points are wired.
    {
        Vst::IConnectionPoint* compCP = nullptr;
        Vst::IConnectionPoint* ctrlCP = nullptr;
        component->queryInterface (Vst::IConnectionPoint_iid, (void**) &compCP);
        controller->queryInterface (Vst::IConnectionPoint_iid, (void**) &ctrlCP);
        CHECK (compCP != nullptr && ctrlCP != nullptr, "connection points available");
        if (compCP && ctrlCP) {
            CHECK (compCP->connect (ctrlCP) == kResultOk, "component->connect(controller)");
            CHECK (ctrlCP->connect (compCP) == kResultOk, "controller->connect(component)");
        }
        if (compCP) compCP->release();
        if (ctrlCP) ctrlCP->release();
    }

    auto* hostHandler = new HostUnitHandler();
    CHECK (controller->setComponentHandler(hostHandler) == kResultTrue,
           "host component/unit handler installed");
    const bool testProgramNameRefresh{argc >= 3};
    if (testProgramNameRefresh) {
        auto* state = new ReadOnlyMemoryStream(makeXmlState(argv[2]));
        CHECK (component->setState(state) == kResultTrue,
               "component accepts a state that loads the program-name fixture");
        state->release();
    }

    // ---- the actual Stage-2 verification ----
    Vst::IUnitInfo* units = nullptr;
    controller->queryInterface (Vst::IUnitInfo_iid, (void**) &units);
    CHECK (units != nullptr, "controller exposes IUnitInfo");
    if (units) {
        const auto count = units->getUnitCount();
        printf ("  unit count = %d\n", count);
        CHECK (count == 17, "17 units (root + 16 channels) from the patched wrapper");

        Vst::UnitInfo info{};
        CHECK (units->getUnitInfo (0, info) == kResultTrue && info.id == Vst::kRootUnitId, "root unit at index 0");
        bool unitsOk = true, listOk = true, queryOrderStable = true;
        for (int ch = 0; ch < 16; ++ch) {
            if (units->getUnitInfo (ch + 1, info) != kResultTrue) { unitsOk = false; break; }
            if (info.id != expectedUnitId (ch)) { unitsOk = false; break; }
            if (info.programListId == Vst::kNoProgramListId) { listOk = false; break; }
            Vst::UnitID uid = -1;
            queryOrderStable = queryOrderStable
                && info.id == earlyUnitIds[ch]
                && info.programListId == earlyProgramListId
                && units->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput,
                                        0, ch, uid) == kResultTrue
                && uid == earlyBusUnitIds[ch];
        }
        CHECK (unitsOk, "channel units have expected hash-derived IDs");
        CHECK (listOk, "every channel unit references a program list");
        CHECK (queryOrderStable, "unit/list/bus answers are identical before and after connection");

        Vst::ProgramListInfo pli{};
        CHECK (units->getProgramListCount() == 1 && units->getProgramListInfo (0, pli) == kResultTrue
               && pli.id == 0x50524F47 && pli.programCount == 128,
               "frozen PROG list ID exposes 128 programs");

        Vst::String128 name{};
        CHECK (units->getProgramName (pli.id, 0, name) == kResultTrue, "getProgramName(0)");
        const auto programZeroName{toUtf8(name)};
        printf ("  program 0 name: \"%s\"\n", programZeroName.c_str());
        if (testProgramNameRefresh) {
            CHECK (hostHandler->programListNotifications > 0
                       && hostHandler->lastListId == pli.id
                       && hostHandler->lastProgramIndex == Vst::kAllProgramInvalid,
                   "font load notifies the host that the shared program list changed");
            CHECK (!programZeroName.empty() && programZeroName != "Program 0",
                   "font load refreshes the program name visible through IUnitInfo");
        }

        bool busOk = true;
        for (int ch = 0; ch < 16; ++ch) {
            Vst::UnitID uid = -1;
            if (units->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput, 0, ch, uid) != kResultTrue
                || uid != expectedUnitId (ch)) { busOk = false; break; }
        }
        CHECK (busOk, "getUnitByBus maps MIDI channel N -> unit N");

        // parameter unitIds must land inside our units (the Cubase invariant),
        // and each must carry kIsProgramChange (the vendored wrapper patch) so
        // hosts treat it as the unit's program selector.
        int paramsInChannelUnits = 0, flaggedParams = 0, steppedParams = 0;
        bool frozenProgramIds = true;
        bool frozenRootIds = true;
        bool rootParamsInRootUnit = true;
        bool foundRootIds[numRootParamIds]{};
        const auto n = controller->getParameterCount();
        for (int32 i = 0; i < n; ++i) {
            Vst::ParameterInfo pi{};
            if (controller->getParameterInfo (i, pi) != kResultOk) continue;
            for (int root = 0; root < numRootParamIds; ++root)
                if (pi.id == expectedRootParamIds[root]) {
                    foundRootIds[root] = true;
                    // The wrapper serves a fixed 17-unit structure that hosts
                    // cache before the component connection exists. A parameter
                    // announcing any other unit would point at a unit the host
                    // was never told about.
                    if (pi.unitId != Vst::kRootUnitId)
                        rootParamsInRootUnit = false;
                }
            for (int ch = 0; ch < 16; ++ch)
                if (pi.unitId == expectedUnitId (ch)) {
                    ++paramsInChannelUnits;
                    unitProgramParamIds[ch] = pi.id;
                    if (pi.id != expectedProgramParamIds[ch])
                        frozenProgramIds = false;
                    if ((pi.flags & Vst::ParameterInfo::kIsProgramChange) != 0)
                        ++flaggedParams;
                    if (pi.stepCount == 127)
                        ++steppedParams;
                    break;
                }
        }
        for (const bool found : foundRootIds)
            frozenRootIds = frozenRootIds && found;
        printf ("  params inside channel units: %d (kIsProgramChange on %d, stepCount 127 on %d)\n",
                paramsInChannelUnits, flaggedParams, steppedParams);
        CHECK (paramsInChannelUnits == 16, "exactly the 16 progChN params live in the channel units");
        CHECK (flaggedParams == 16, "all 16 channel program params carry kIsProgramChange");
        // Cubase requires a discrete 128-step program selector: stepCount MUST be
        // programCount-1 (127), not 0 (continuous). Regression-guard for isDiscrete().
        CHECK (steppedParams == 16, "all 16 channel program params report stepCount 127");
        CHECK (frozenProgramIds, "all 16 Beta 1 VST3 program ParamIDs are unchanged");
        CHECK (frozenRootIds, "all 73 Beta 1 root-unit VST3 ParamIDs are present");
        CHECK (rootParamsInRootUnit,
               "every non-program parameter reports the root unit, so no parameter names a unit outside the frozen 17");

        units->release();
    }

    // ---- component-side IUnitInfo (Cubase interrogates the COMPONENT, not the
    // controller, for unit structure; both must present our 17-unit view) ----
    {
        Vst::IUnitInfo* compUnits = nullptr;
        component->queryInterface (Vst::IUnitInfo_iid, (void**) &compUnits);
        CHECK (compUnits != nullptr, "component exposes IUnitInfo");
        if (compUnits) {
            CHECK (compUnits->getUnitCount() == 17, "component-side: 17 units from the patched wrapper");
            Vst::UnitID uid = -1;
            CHECK (compUnits->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput, 0, 5, uid) == kResultTrue
                   && uid == expectedUnitId (5), "component-side: getUnitByBus channel->unit");
            Vst::ProgramListInfo pli{};
            CHECK (compUnits->getProgramListCount() == 1 && compUnits->getProgramListInfo (0, pli) == kResultTrue,
                   "component-side: program list present");
            Vst::UnitInfo invalidUnit{};
            Vst::UnitID invalidBusUnit = -1;
            CHECK (compUnits->getUnitInfo (17, invalidUnit) != kResultTrue
                       && compUnits->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput,
                                                  0, 16, invalidBusUnit) != kResultTrue,
                   "component-side: invalid unit and channel rejected");
            compUnits->release();
        }
    }

    // ---- IMidiMapping: the path Cubase/FL actually use to route Program Change ----
    {
        Vst::IMidiMapping* mapping = nullptr;
        controller->queryInterface (Vst::IMidiMapping_iid, (void**) &mapping);
        CHECK (mapping != nullptr, "controller exposes IMidiMapping");
        if (mapping) {
            // collect the ParamIDs of the 16 progChN params (identified by unitId)
            Vst::ParamID progParamIds[16];
            for (auto& id : progParamIds) id = Vst::kNoParamId;
            const auto n = controller->getParameterCount();
            for (int32 i = 0; i < n; ++i) {
                Vst::ParameterInfo pi{};
                if (controller->getParameterInfo (i, pi) != kResultOk) continue;
                for (int ch = 0; ch < 16; ++ch)
                    if (pi.unitId == expectedUnitId (ch)) { progParamIds[ch] = pi.id; break; }
            }
            std::copy(std::begin(progParamIds), std::end(progParamIds),
                      std::begin(mappedProgramParamIds));

            bool pcMapped = true;
            for (int16 ch = 0; ch < 16; ++ch) {
                Vst::ParamID id = Vst::kNoParamId;
                if (mapping->getMidiControllerAssignment (0, ch, Vst::kCtrlProgramChange, id) != kResultTrue
                    || id != progParamIds[ch] || id != expectedProgramParamIds[ch]
                    || id == Vst::kNoParamId) { pcMapped = false; break; }
            }
            CHECK (pcMapped, "PC (ctrl 130) on channel N maps to that channel's progChN param");

            Vst::ParamID ccId = Vst::kNoParamId;
            CHECK (mapping->getMidiControllerAssignment (0, 3, 7, ccId) == kResultTrue && ccId != Vst::kNoParamId,
                   "CC table still intact (CC7 mapped)");
            Vst::ParamID oobId = Vst::kNoParamId;
            CHECK (mapping->getMidiControllerAssignment (0, 3, 131, oobId) != kResultTrue
                       && mapping->getMidiControllerAssignment (0, -1, Vst::kCtrlProgramChange, oobId) != kResultTrue
                       && mapping->getMidiControllerAssignment (0, 16, Vst::kCtrlProgramChange, oobId) != kResultTrue,
                   "out-of-table controllers and channels are rejected (no OOB read)");
            mapping->release();
        }
    }

    // ---- actual VST3 processing: independently acquire the program ParamIDs
    // through IMidiMapping (FL Studio-style) and unit program parameters
    // (Cubase-style), feed them through IAudioProcessor, and require both routes
    // to converge on engine-backed channel state without cross-channel changes.
    Vst::IAudioProcessor* audioProcessor = nullptr;
    component->queryInterface(Vst::IAudioProcessor_iid,
                              reinterpret_cast<void**>(&audioProcessor));
    CHECK(audioProcessor != nullptr, "component exposes IAudioProcessor");
    bool processingReady{audioProcessor != nullptr};
    if (audioProcessor != nullptr) {
        Vst::SpeakerArrangement outputArrangement{Vst::SpeakerArr::kStereo};
        processingReady = processingReady
            && audioProcessor->setBusArrangements(nullptr, 0, &outputArrangement, 1)
                == kResultTrue
            && component->activateBus(Vst::kAudio, Vst::kOutput, 0, true)
                == kResultTrue
            && component->activateBus(Vst::kEvent, Vst::kInput, 0, true)
                == kResultTrue;
        Vst::ProcessSetup setup{};
        setup.processMode = Vst::kOffline;
        setup.symbolicSampleSize = Vst::kSample32;
        setup.maxSamplesPerBlock = 512;
        setup.sampleRate = 48000.0;
        processingReady = processingReady
            && audioProcessor->setupProcessing(setup) == kResultTrue
            && component->setActive(true) == kResultTrue
            && audioProcessor->setProcessing(true) == kResultTrue;
    }
    CHECK(processingReady, "VST3 component configured for stereo event/audio processing");

    std::vector<ProgramFixtureScenario> fixtureScenarios;
    std::string fixtureError;
    const bool fixtureLoaded{argc >= 4
        && loadProgramFixture(argv[3], fixtureScenarios, fixtureError)};
    CHECK(fixtureLoaded, "checked-in VST3 multichannel/reset fixture is valid");
    if (!fixtureLoaded && argc >= 4)
        std::printf("  fixture error: %s\n", fixtureError.c_str());

    Vst::IMidiMapping* processingMapping = nullptr;
    controller->queryInterface(Vst::IMidiMapping_iid,
                               reinterpret_cast<void**>(&processingMapping));

    Vst::TSamples nextProjectSample{};
    const auto runProgramRoute = [&](const ProgramFixtureScenario& scenario,
                                     const Vst::ParamID (&paramIds)[16],
                                     bool requireInitialSilence) {
        bool inputIdsValid{true};
        std::array<int, 16> initialPrograms{};
        bool initialStateOk{true};
        {
            WriteMemoryStream initialState;
            const auto initialXml{component->getState(&initialState) == kResultTrue
                ? xmlFromBinaryState(initialState.data()) : std::string{}};
            for (int channel = 0; channel < 16; ++channel)
                initialStateOk = initialStateOk
                    && channelAttribute(initialXml, channel, "preset",
                                        initialPrograms[static_cast<std::size_t>(channel)]);
        }
        const auto initialStateName{"pre-scenario state is readable for " + scenario.name};
        CHECK(initialStateOk, initialStateName.c_str());

        if (scenario.restartTransport) {
            Vst::ProcessContext stoppedContext{};
            stoppedContext.sampleRate = 48000.0;
            stoppedContext.projectTimeSamples = nextProjectSample;
            Vst::ProcessData stopped{};
            stopped.processMode = Vst::kOffline;
            stopped.symbolicSampleSize = Vst::kSample32;
            stopped.processContext = &stoppedContext;
            CHECK(processingReady && audioProcessor->process(stopped) == kResultTrue,
                  "transport stop is observed before the restart scenario");
            nextProjectSample = 0;
        }
        hostHandler->clearEdits();
        ParameterChanges inputChanges;
        EventList inputEvents;
        int firstNoteSample{512};
        for (const auto& event : scenario.events) {
            if (event.type == "program") {
                const int channel{event.channel - 1};
                inputIdsValid = inputIdsValid
                    && paramIds[channel] == expectedProgramParamIds[channel]
                    && inputChanges.addPoint(
                        paramIds[channel], event.sample,
                        static_cast<double>(event.data1) / 127.0);
            } else if (event.type == "cc") {
                Vst::ParamID ccParam{Vst::kNoParamId};
                inputIdsValid = inputIdsValid && processingMapping != nullptr
                    && processingMapping->getMidiControllerAssignment(
                        0, static_cast<int16>(event.channel - 1),
                        static_cast<Vst::CtrlNumber>(event.data1), ccParam) == kResultTrue
                    && ccParam != Vst::kNoParamId
                    && inputChanges.addPoint(
                        ccParam, event.sample,
                        static_cast<double>(event.data2) / 127.0);
            } else if (event.type == "note") {
                firstNoteSample = std::min(firstNoteSample, event.sample);
                inputEvents.addNoteOn(
                    event.channel - 1, event.data1, event.data2, event.sample);
            } else if (event.type == "reset") {
                const auto bytes{resetSysEx(event.data1)};
                inputIdsValid = inputIdsValid && !bytes.empty();
                if (!bytes.empty()) inputEvents.addSysEx(bytes, event.sample);
            }
        }
        const auto inputName{"fixture queues are valid for scenario " + scenario.name};
        CHECK(inputIdsValid, inputName.c_str());

        std::array<float, 512> left{};
        std::array<float, 512> right{};
        float* outputChannels[]{left.data(), right.data()};
        Vst::AudioBusBuffers output{};
        output.numChannels = 2;
        output.channelBuffers32 = outputChannels;
        Vst::ProcessData processData{};
        processData.processMode = Vst::kOffline;
        processData.symbolicSampleSize = Vst::kSample32;
        processData.numSamples = 512;
        processData.numOutputs = 1;
        processData.outputs = &output;
        // Bypass the reverb for this scenario. It measures the DRY path -
        // sample-accurate event placement and exact silence before the first
        // event - and a reverb tail crossing a block boundary is correct
        // behaviour that would make the silence assertion meaningless. The
        // reverb's own behaviour is asserted in the engine suite.
        inputChanges.addPoint(kReverbOnParamId, 0, 0.0);
        processData.inputParameterChanges = &inputChanges;
        processData.inputEvents = &inputEvents;
        Vst::ProcessContext playingContext{};
        playingContext.state = Vst::ProcessContext::kPlaying;
        playingContext.sampleRate = 48000.0;
        playingContext.projectTimeSamples = nextProjectSample;
        processData.processContext = &playingContext;
        const bool processSucceeded{
            processingReady && audioProcessor->process(processData) == kResultTrue};
        nextProjectSample += processData.numSamples;
        CHECK(processSucceeded, "VST3 block with all-channel programs and notes processes");
        CHECK(firstNoteSample < 512
                  && audioMagnitude(left, right, firstNoteSample, 512 - firstNoteSample) > 0.001,
              "processed VST3 Program Change fixture produces audio");
        if (requireInitialSilence)
            CHECK(firstNoteSample > 0
                      && audioMagnitude(left, right, 0, firstNoteSample) == 0.0,
                  "VST3 note audio begins only after the first timestamped event");

        // Async program-state mirroring is deliberately message-thread-only.
        // Pump the native run loop, then flush wrapper-to-host parameter changes.
        for (int iteration = 0; iteration < 8; ++iteration)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);

        Vst::ProcessData flush{};
        flush.processMode = Vst::kOffline;
        flush.symbolicSampleSize = Vst::kSample32;
        const bool flushSucceeded{
            processingReady && audioProcessor->process(flush) == kResultTrue};
        CHECK(flushSucceeded, "message-thread program mirrors complete before the next VST3 block");

        std::array<int, 16> observedPrograms{};
        bool observedValuesOk{true};
        int observedProgramQueues{};
        for (const auto& edit : hostHandler->edits) {
            for (int channel = 0; channel < 16; ++channel) {
                if (edit.id != expectedProgramParamIds[channel])
                    continue;
                ++observedProgramQueues;
                ++observedPrograms[static_cast<std::size_t>(channel)];
                observedValuesOk = observedValuesOk
                    && std::abs(edit.value
                        - static_cast<double>(scenario.expectedPrograms[static_cast<std::size_t>(channel)])
                            / 127.0) < 1.0e-6;
            }
        }
        int expectedProgramQueues{};
        for (int channel = 0; channel < 16; ++channel) {
            const bool changed{initialPrograms[static_cast<std::size_t>(channel)]
                != scenario.expectedPrograms[static_cast<std::size_t>(channel)]};
            expectedProgramQueues += changed ? 1 : 0;
            observedValuesOk = observedValuesOk
                && observedPrograms[static_cast<std::size_t>(channel)] == (changed ? 1 : 0);
        }
        std::printf("  observed host program edits: %d/%d changed channels (total host edits: %zu)\n",
                    observedProgramQueues, expectedProgramQueues, hostHandler->edits.size());
        CHECK(observedProgramQueues == expectedProgramQueues && observedValuesOk,
              "host observes one final progChN edit per changed channel and suppresses duplicates");

        WriteMemoryStream state;
        const bool stateWritten{component->getState(&state) == kResultTrue};
        const auto xml{xmlFromBinaryState(state.data())};
        bool stateOk{stateWritten && !xml.empty()};
        std::string paramsElement;
        for (int channel = 0; channel < 16; ++channel) {
            int bank{-1};
            int preset{-1};
            std::string normalized;
            const bool hasBank{channelAttribute(xml, channel, "bank", bank)};
            const bool hasPreset{channelAttribute(xml, channel, "preset", preset)};
            const bool hasNormalized{elementAttribute(
                xml, "<params ", "progCh" + std::to_string(channel + 1), normalized)};
            const bool channelOk{hasBank && hasPreset && hasNormalized
                && bank == scenario.expectedBanks[static_cast<std::size_t>(channel)]
                && preset == scenario.expectedPrograms[static_cast<std::size_t>(channel)]
                && std::abs(std::stod(normalized)
                    - static_cast<double>(scenario.expectedPrograms[static_cast<std::size_t>(channel)])
                        / 127.0) < 1.0e-6};
            if (!channelOk)
                std::printf("  state mismatch %s ch%d: bank=%d/%d preset=%d/%d normalized=%s\n",
                            scenario.name.c_str(), channel + 1,
                            bank, scenario.expectedBanks[static_cast<std::size_t>(channel)],
                            preset, scenario.expectedPrograms[static_cast<std::size_t>(channel)],
                            normalized.c_str());
            stateOk = stateOk && channelOk;
        }
        CHECK(stateOk,
              "all 16 VST3 programs converge across parameters and serialized channel state");

        // Prove that every state-verified channel is not merely carrying the
        // right numbers, but is independently connected to a sounding engine
        // program. Before each audition, send All Sound Off to every channel so
        // a voice left by the preceding channel cannot satisfy the next check.
        bool everyChannelAudible{stateOk && processingMapping != nullptr};
        for (int channel = 0; channel < 16 && everyChannelAudible; ++channel) {
            ParameterChanges auditionChanges;
            bool auditionInputOk{true};
            for (int silencedChannel = 0; silencedChannel < 16; ++silencedChannel) {
                Vst::ParamID allSoundOff{Vst::kNoParamId};
                auditionInputOk = auditionInputOk
                    && processingMapping->getMidiControllerAssignment(
                        0,
                        static_cast<int16>(silencedChannel),
                        Vst::kCtrlAllSoundsOff,
                        allSoundOff) == kResultTrue
                    && allSoundOff != Vst::kNoParamId
                    && auditionChanges.addPoint(allSoundOff, 0, 0.0);
            }

            EventList auditionEvents;
            auditionEvents.addNoteOn(
                channel, channel == 9 ? 36 : 60, 100, 8);
            std::array<float, 512> auditionLeft{};
            std::array<float, 512> auditionRight{};
            float* auditionChannels[]{auditionLeft.data(), auditionRight.data()};
            Vst::AudioBusBuffers auditionOutput{};
            auditionOutput.numChannels = 2;
            auditionOutput.channelBuffers32 = auditionChannels;
            Vst::ProcessContext auditionContext{};
            auditionContext.state = Vst::ProcessContext::kPlaying;
            auditionContext.sampleRate = 48000.0;
            auditionContext.projectTimeSamples = nextProjectSample;
            Vst::ProcessData audition{};
            audition.processMode = Vst::kOffline;
            audition.symbolicSampleSize = Vst::kSample32;
            audition.numSamples = 512;
            audition.numOutputs = 1;
            audition.outputs = &auditionOutput;
            audition.inputParameterChanges = &auditionChanges;
            audition.inputEvents = &auditionEvents;
            audition.processContext = &auditionContext;
            const bool auditionProcessed{auditionInputOk
                && audioProcessor->process(audition) == kResultTrue};
            nextProjectSample += audition.numSamples;
            const bool channelAudible{auditionProcessed
                && audioMagnitude(auditionLeft, auditionRight, 8, 504) > 0.001};
            if (!channelAudible)
                std::printf("  silent VST3 audition %s ch%d bank=%d program=%d\n",
                            scenario.name.c_str(), channel + 1,
                            scenario.expectedBanks[static_cast<std::size_t>(channel)],
                            scenario.expectedPrograms[static_cast<std::size_t>(channel)]);
            everyChannelAudible = everyChannelAudible && channelAudible;
        }
        CHECK(everyChannelAudible,
              "every checkpointed VST3 channel independently sounds its state-verified program");
        return processSucceeded && observedProgramQueues == expectedProgramQueues
            && observedValuesOk && stateOk && everyChannelAudible;
    };

    if (processingReady && testProgramNameRefresh && fixtureLoaded) {
        bool mappingPassed{};
        bool unitPassed{};
        bool allScenariosPassed{true};
        for (std::size_t index = 0; index < fixtureScenarios.size(); ++index) {
            const auto& scenario{fixtureScenarios[index]};
            const bool passed{scenario.source == "mapping"
                ? runProgramRoute(scenario, mappedProgramParamIds, index == 0)
                : runProgramRoute(scenario, unitProgramParamIds, index == 0)};
            mappingPassed = mappingPassed || (scenario.source == "mapping" && passed);
            unitPassed = unitPassed || (scenario.source == "unit" && passed);
            allScenariosPassed = allScenariosPassed && passed;
        }
        CHECK(mappingPassed,
              "IMidiMapping fixture route changes all 16 programs independently");
        CHECK(unitPassed,
              "unit/program-parameter fixture route changes all 16 programs independently");
        CHECK(allScenariosPassed,
              "GM/GS/XG reset, Bank Select, transport restart, mid-song, and note checkpoints all pass");
    }

    // ---- in-block timbre transition ----
    // The fixture scenarios above prove state and host edits. This proves the
    // sample position in the audio itself: a progChN automation point must take
    // effect exactly at its own sample, not at block start and not late. Each
    // trial runs on a fresh component so the synth state at the note is identical
    // and the renderings can be compared directly.
    if (processingReady && testProgramNameRefresh) {
        constexpr int pianoProgram{0};
        constexpr int organProgram{19};
        constexpr int notePosition{256};
        constexpr int tail{512 - notePosition};

        struct AutomationPoint { int sample; int program; };
        const auto renderIsolatedBlock =
            [&](std::initializer_list<AutomationPoint> points,
                std::array<float, 512>& left,
                std::array<float, 512>& right) {
                Vst::IComponent* isolated{nullptr};
                factory->createInstance(effectCid, Vst::IComponent_iid,
                                        reinterpret_cast<void**>(&isolated));
                if (isolated == nullptr)
                    return false;
                bool ok{isolated->initialize(nullptr) == kResultOk};
                if (ok) {
                    auto* bankState = new ReadOnlyMemoryStream(makeXmlState(argv[2]));
                    ok = isolated->setState(bankState) == kResultTrue;
                    bankState->release();
                }
                Vst::IAudioProcessor* isolatedProcessor{nullptr};
                if (ok)
                    isolated->queryInterface(Vst::IAudioProcessor_iid,
                                             reinterpret_cast<void**>(&isolatedProcessor));
                ok = ok && isolatedProcessor != nullptr;
                if (ok) {
                    Vst::SpeakerArrangement arrangement{Vst::SpeakerArr::kStereo};
                    Vst::ProcessSetup setup{};
                    setup.processMode = Vst::kOffline;
                    setup.symbolicSampleSize = Vst::kSample32;
                    setup.maxSamplesPerBlock = 512;
                    setup.sampleRate = 48000.0;
                    ok = isolatedProcessor->setBusArrangements(nullptr, 0, &arrangement, 1)
                            == kResultTrue
                        && isolated->activateBus(Vst::kAudio, Vst::kOutput, 0, true) == kResultTrue
                        && isolated->activateBus(Vst::kEvent, Vst::kInput, 0, true) == kResultTrue
                        && isolatedProcessor->setupProcessing(setup) == kResultTrue
                        && isolated->setActive(true) == kResultTrue
                        && isolatedProcessor->setProcessing(true) == kResultTrue;
                }
                if (ok) {
                    ParameterChanges changes;
                    for (const auto& point : points)
                        ok = ok && changes.addPoint(
                            expectedProgramParamIds[0], point.sample,
                            static_cast<double>(point.program) / 127.0);
                    EventList events;
                    events.addNoteOn(0, 60, 100, notePosition);

                    left.fill(0.0f);
                    right.fill(0.0f);
                    float* channels[]{left.data(), right.data()};
                    Vst::AudioBusBuffers output{};
                    output.numChannels = 2;
                    output.channelBuffers32 = channels;
                    Vst::ProcessContext context{};
                    context.state = Vst::ProcessContext::kPlaying;
                    context.sampleRate = 48000.0;
                    Vst::ProcessData data{};
                    data.processMode = Vst::kOffline;
                    data.symbolicSampleSize = Vst::kSample32;
                    data.numSamples = 512;
                    data.numOutputs = 1;
                    data.outputs = &output;
                    data.inputParameterChanges = &changes;
                    data.inputEvents = &events;
                    data.processContext = &context;
                    ok = ok && isolatedProcessor->process(data) == kResultTrue;
                    isolatedProcessor->setProcessing(false);
                    isolated->setActive(false);
                }
                if (isolatedProcessor != nullptr)
                    isolatedProcessor->release();
                isolated->terminate();
                isolated->release();
                return ok;
            };

        std::array<float, 512> pianoLeft{}, pianoRight{};
        std::array<float, 512> organLeft{}, organRight{};
        std::array<float, 512> switchedBeforeLeft{}, switchedBeforeRight{};
        std::array<float, 512> switchedAfterLeft{}, switchedAfterRight{};

        bool rendered{renderIsolatedBlock({{0, pianoProgram}}, pianoLeft, pianoRight)};
        rendered = rendered
            && renderIsolatedBlock({{0, organProgram}}, organLeft, organRight);
        // Second point one sample before the note: the note must sound the new
        // program, which only happens if the point keeps its own timestamp.
        rendered = rendered
            && renderIsolatedBlock({{0, pianoProgram}, {notePosition - 1, organProgram}},
                                   switchedBeforeLeft, switchedBeforeRight);
        // Second point one sample after the note: the note must still sound the
        // old program, which only happens if the point is not hoisted to block
        // start — JUCE's ordinary parameter collapse would do exactly that.
        rendered = rendered
            && renderIsolatedBlock({{0, pianoProgram}, {notePosition + 1, organProgram}},
                                   switchedAfterLeft, switchedAfterRight);

        CHECK(rendered, "isolated in-block automation trials render");
        if (rendered) {
            const double differentPrograms{waveformCorrelation(
                pianoLeft, pianoRight, organLeft, organRight, notePosition, tail)};
            const double switchedIsNew{waveformCorrelation(
                organLeft, organRight, switchedBeforeLeft, switchedBeforeRight,
                notePosition, tail)};
            const double switchedIsOld{waveformCorrelation(
                pianoLeft, pianoRight, switchedAfterLeft, switchedAfterRight,
                notePosition, tail)};
            std::printf("  in-block automation correlations: different %.4f,"
                        " point before note %.4f, point after note %.4f\n",
                        differentPrograms, switchedIsNew, switchedIsOld);
            CHECK(audioMagnitude(organLeft, organRight, notePosition, tail) > 0.001
                      && differentPrograms < 0.9,
                  "the two trial programs are audibly different on the same note");
            CHECK(switchedIsNew > 0.999,
                  "a progChN point one sample before a note sounds the new program at that sample");
            CHECK(switchedIsOld > 0.999,
                  "a progChN point one sample after a note leaves that note on the old program");
        }
    }

    // ---- editor view: size contract and host-provided scaling ----
    // No window is created, so this covers what a host can ask before attaching:
    // that a view exists, reports a usable size, constrains a nonsense size, and
    // accepts a content scale factor. Screen-reader and real-window behaviour
    // stay manual.
    {
        IPlugView* view{controller->createView(Vst::ViewType::kEditor)};
        CHECK(view != nullptr, "controller offers an editor view");
        if (view != nullptr) {
            CHECK(view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue,
                  "the editor view supports the host platform type");

            ViewRect defaultSize{};
            const bool defaultSizeOk{view->getSize(&defaultSize) == kResultTrue
                && defaultSize.getWidth() > 0 && defaultSize.getHeight() > 0};
            CHECK(defaultSizeOk, "the editor view reports a non-empty default size");
            std::printf("  editor default size: %d x %d\n",
                        defaultSize.getWidth(), defaultSize.getHeight());

            // A host that asks for an absurd size must be given the constrained
            // one back, not have the request accepted verbatim.
            ViewRect tiny{0, 0, 1, 1};
            ViewRect huge{0, 0, 20000, 20000};
            const bool tinyHandled{view->checkSizeConstraint(&tiny) == kResultTrue};
            const bool hugeHandled{view->checkSizeConstraint(&huge) == kResultTrue};
            const bool constrained{tinyHandled && hugeHandled
                && tiny.getWidth() > 1 && tiny.getHeight() > 1
                && huge.getWidth() < 20000 && huge.getHeight() < 20000
                && tiny.getWidth() <= huge.getWidth()
                && tiny.getHeight() <= huge.getHeight()};
            CHECK(constrained,
                  "the editor view constrains undersized and oversized host requests");
            std::printf("  editor constrained bounds: %d x %d minimum, %d x %d maximum\n",
                        tiny.getWidth(), tiny.getHeight(),
                        huge.getWidth(), huge.getHeight());

            IPlugViewContentScaleSupport* scaling{nullptr};
            view->queryInterface(IPlugViewContentScaleSupport_iid,
                                 reinterpret_cast<void**>(&scaling));
            CHECK(scaling != nullptr,
                  "the editor view exposes IPlugViewContentScaleSupport for host scaling");
            if (scaling != nullptr) {
                // JUCE answers kResultFalse on macOS on purpose: the window
                // server applies the backing scale factor, so there is nothing
                // for the plugin to do. Windows and Linux are where a host
                // actually drives this. Assert the platform's own answer rather
                // than a portable one, and require it to be consistent.
                const tresult firstAnswer{scaling->setContentScaleFactor(1.0f)};
                bool consistentAnswer{true};
                for (const float factor : {1.25f, 2.0f, 1.0f})
                    consistentAnswer = consistentAnswer
                        && scaling->setContentScaleFactor(factor) == firstAnswer;
               #if defined(__APPLE__)
                const bool expectedAnswer{firstAnswer == kResultFalse};
               #else
                const bool expectedAnswer{firstAnswer == kResultTrue};
               #endif
                // The reported size is in logical units either way, so a scale
                // change must not move it; a host that resized here would be
                // fighting the plugin.
                ViewRect afterScaling{};
                const bool sizeStable{view->getSize(&afterScaling) == kResultTrue
                    && afterScaling.getWidth() == defaultSize.getWidth()
                    && afterScaling.getHeight() == defaultSize.getHeight()};
                std::printf("  host content scale factor answer: %d (%s)\n",
                            static_cast<int>(firstAnswer),
                            firstAnswer == kResultTrue ? "applied by the plugin"
                                                       : "declined; the OS scales");
                CHECK(consistentAnswer && expectedAnswer && sizeStable,
                      "host content scale factors get this platform's documented answer"
                      " and leave the logical size unchanged");
                scaling->release();
            }
            view->release();
        }
    }

    if (processingMapping != nullptr)
        processingMapping->release();

    if (audioProcessor != nullptr && processingReady) {
        audioProcessor->setProcessing(false);
        component->setActive(false);
    }
    if (audioProcessor != nullptr)
        audioProcessor->release();

    controller->setComponentHandler(nullptr);
    hostHandler->release();
    controller->terminate();
    controller->release();
    component->terminate();
    component->release();

    // Recreate the pair twice and repeat Cubase's early-query/connection order.
    // This catches lifecycle state leaking from the first instance.
    bool repeatedLifecycleOk = true;
    for (int iteration = 0; iteration < 2; ++iteration) {
        Vst::IComponent* repeatedComponent = nullptr;
        Vst::IEditController* repeatedController = nullptr;
        factory->createInstance (effectCid, Vst::IComponent_iid, (void**) &repeatedComponent);
        repeatedLifecycleOk = repeatedLifecycleOk && repeatedComponent != nullptr;
        if (repeatedComponent == nullptr)
            continue;
        repeatedLifecycleOk = repeatedLifecycleOk && repeatedComponent->initialize (nullptr) == kResultOk;
        TUID repeatedControllerCid{};
        repeatedLifecycleOk = repeatedLifecycleOk
            && repeatedComponent->getControllerClassId (repeatedControllerCid) == kResultOk;
        factory->createInstance (repeatedControllerCid, Vst::IEditController_iid,
                                 (void**) &repeatedController);
        repeatedLifecycleOk = repeatedLifecycleOk && repeatedController != nullptr;
        if (repeatedController != nullptr) {
            repeatedLifecycleOk = repeatedLifecycleOk && repeatedController->initialize (nullptr) == kResultOk;
            Vst::IUnitInfo* repeatedEarly = nullptr;
            repeatedController->queryInterface (Vst::IUnitInfo_iid, (void**) &repeatedEarly);
            repeatedLifecycleOk = repeatedLifecycleOk && repeatedEarly != nullptr
                && repeatedEarly->getUnitCount() == 17
                && repeatedEarly->getProgramListCount() == 1;
            if (repeatedEarly != nullptr)
                repeatedEarly->release();

            Vst::IConnectionPoint* repeatedCompCP = nullptr;
            Vst::IConnectionPoint* repeatedCtrlCP = nullptr;
            repeatedComponent->queryInterface (Vst::IConnectionPoint_iid, (void**) &repeatedCompCP);
            repeatedController->queryInterface (Vst::IConnectionPoint_iid, (void**) &repeatedCtrlCP);
            repeatedLifecycleOk = repeatedLifecycleOk && repeatedCompCP != nullptr && repeatedCtrlCP != nullptr;
            if (repeatedCompCP != nullptr && repeatedCtrlCP != nullptr) {
                repeatedLifecycleOk = repeatedLifecycleOk
                    && repeatedCompCP->connect (repeatedCtrlCP) == kResultOk
                    && repeatedCtrlCP->connect (repeatedCompCP) == kResultOk;
                repeatedCompCP->disconnect (repeatedCtrlCP);
                repeatedCtrlCP->disconnect (repeatedCompCP);
            }
            if (repeatedCompCP != nullptr) repeatedCompCP->release();
            if (repeatedCtrlCP != nullptr) repeatedCtrlCP->release();
            repeatedController->terminate();
            repeatedController->release();
        }
        repeatedComponent->terminate();
        repeatedComponent->release();
    }
    CHECK (repeatedLifecycleOk, "component/controller early-query lifecycle is repeatable");

    factory->release();

    printf ("== vst3_smoke: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
