//
// See VST3Multitimbral.h for the design. This TU is the only place VST3 SDK
// headers are included. It deliberately links against NO SDK symbols: the
// ClassName##_iid TUIDs are header-inline (funknown.h without INIT_CLASS_IID),
// iidEqual is SMTG_ALWAYS_INLINE, and our COM object implements FUnknown by
// hand — so this file is safe to compile into the shared code used by the
// AU/Standalone targets, which don't link the VST3 wrapper.
//

#include "VST3Multitimbral.h"
#include "Vst3Units.h"

#include <atomic>
#include <cstring>

// Shared program-name store (see Vst3Units.h) — read by both our IUnitInfo
// implementation below and the vendored wrapper's controller-side IUnitInfo.
namespace juicysf::vst3units {
    namespace {
        juce::CriticalSection namesLock;
        juce::StringArray programNames;
    }
    void setProgramNames (const juce::StringArray& names) {
        const juce::ScopedLock sl (namesLock);
        programNames = names;
    }
    juce::String programNameForIndex (int index) {
        {
            const juce::ScopedLock sl (namesLock);
            if (index >= 0 && index < programNames.size() && programNames[index].isNotEmpty())
                return programNames[index];
        }
        return "Program " + juce::String (index);
    }
}

#include <pluginterfaces/vst/ivstunits.h>
#include <pluginterfaces/vst/ivstcomponent.h>

namespace Vst = Steinberg::Vst;
using Steinberg::tresult;
using Steinberg::kResultOk;
using Steinberg::kResultTrue;
using Steinberg::kResultFalse;
using Steinberg::kNoInterface;
using Steinberg::kNotImplemented;
using Steinberg::kInvalidArgument;

namespace {

constexpr int kNumMidiChannels = juicysf::vst3units::kNumMidiChannels;
constexpr Vst::ProgramListID kJuicyProgramListId = juicysf::vst3units::kProgramListId;

static Steinberg::Vst::UnitID unitIdForChannel (int chZeroBased)
{
    const auto id = juicysf::vst3units::unitIdForChannel (chZeroBased);
    jassert (id != Vst::kRootUnitId); // hash collision with the root would break routing
    return id;
}

static void copyToString128 (Vst::String128 dst, const juce::String& s)
{
    // TChar is char16_t; JUCE's CharPointer_UTF16::CharType is int16 — same size.
    s.copyToUTF16 (reinterpret_cast<juce::CharPointer_UTF16::CharType*> (dst),
                   sizeof (Vst::String128));
}

} // namespace

//==============================================================================
class JuicyVST3Extensions::UnitInfoImpl final : public Vst::IUnitInfo
{
public:
    UnitInfoImpl() = default;

    //==============================================================================
    // FUnknown, by hand (no SDK .cpp needed)
    tresult PLUGIN_API queryInterface (const Steinberg::TUID iid, void** obj) override
    {
        if (Steinberg::FUnknownPrivate::iidEqual (iid, Steinberg::FUnknown_iid)
            || Steinberg::FUnknownPrivate::iidEqual (iid, Vst::IUnitInfo_iid))
        {
            addRef();
            *obj = static_cast<Vst::IUnitInfo*> (this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override   { return (Steinberg::uint32) ++refCount; }

    Steinberg::uint32 PLUGIN_API release() override
    {
        const auto r = --refCount;
        if (r == 0)
            delete this;
        return (Steinberg::uint32) r;
    }

    //==============================================================================
    // IUnitInfo
    Steinberg::int32 PLUGIN_API getUnitCount() override
    {
        return 1 + kNumMidiChannels;
    }

    tresult PLUGIN_API getUnitInfo (Steinberg::int32 unitIndex, Vst::UnitInfo& info) override
    {
        if (unitIndex == 0)
        {
            info.id = Vst::kRootUnitId;
            info.parentUnitId = Vst::kNoParentUnitId;
            info.programListId = Vst::kNoProgramListId;
            copyToString128 (info.name, "Root");
            return kResultTrue;
        }
        const int ch = unitIndex - 1;
        if (ch < 0 || ch >= kNumMidiChannels)
            return kResultFalse;
        info.id = unitIdForChannel (ch);
        info.parentUnitId = Vst::kRootUnitId;
        info.programListId = kJuicyProgramListId; // one shared list for all channels
        copyToString128 (info.name, "Ch " + juce::String (ch + 1));
        return kResultTrue;
    }

    Steinberg::int32 PLUGIN_API getProgramListCount() override { return 1; }

    tresult PLUGIN_API getProgramListInfo (Steinberg::int32 listIndex, Vst::ProgramListInfo& info) override
    {
        if (listIndex != 0)
            return kResultFalse;
        info.id = kJuicyProgramListId;
        info.programCount = 128; // GM program numbers; progChN params are 0..127
        copyToString128 (info.name, "Programs");
        return kResultTrue;
    }

    tresult PLUGIN_API getProgramName (Vst::ProgramListID listId, Steinberg::int32 programIndex,
                                       Vst::String128 name) override
    {
        if (listId != kJuicyProgramListId || programIndex < 0 || programIndex >= 128)
            return kResultFalse;
        copyToString128 (name, juicysf::vst3units::programNameForIndex (programIndex));
        return kResultTrue;
    }

    tresult PLUGIN_API getProgramInfo (Vst::ProgramListID, Steinberg::int32,
                                       Vst::CString, Vst::String128) override
    {
        return kResultFalse; // no extra program attributes
    }

    tresult PLUGIN_API hasProgramPitchNames (Vst::ProgramListID, Steinberg::int32) override
    {
        return kResultFalse;
    }

    tresult PLUGIN_API getProgramPitchName (Vst::ProgramListID, Steinberg::int32,
                                            Steinberg::int16, Vst::String128) override
    {
        return kResultFalse;
    }

    Vst::UnitID PLUGIN_API getSelectedUnit() override { return selectedUnit.load(); }

    tresult PLUGIN_API selectUnit (Vst::UnitID unitId) override
    {
        selectedUnit.store (unitId);
        return kResultTrue;
    }

    tresult PLUGIN_API getUnitByBus (Vst::MediaType type, Vst::BusDirection dir,
                                     Steinberg::int32 busIndex, Steinberg::int32 channel,
                                     Vst::UnitID& unitId) override
    {
        // the per-MIDI-channel association hosts use to route program changes
        if (type == Vst::MediaTypes::kEvent && dir == Vst::BusDirections::kInput
            && busIndex == 0 && channel >= 0 && channel < kNumMidiChannels)
        {
            unitId = unitIdForChannel (channel);
            return kResultTrue;
        }
        return kResultFalse;
    }

    tresult PLUGIN_API setUnitProgramData (Steinberg::int32, Steinberg::int32,
                                           Steinberg::IBStream*) override
    {
        return kNotImplemented;
    }

private:
    std::atomic<int> refCount{1}; // creator holds the initial reference
    std::atomic<Vst::UnitID> selectedUnit{Vst::kRootUnitId};

    ~UnitInfoImpl() = default; // COM: delete only via release()
};

//==============================================================================
JuicyVST3Extensions::JuicyVST3Extensions()
    : unitInfo (new UnitInfoImpl())
{
}

JuicyVST3Extensions::~JuicyVST3Extensions()
{
    if (unitHandler != nullptr)
        static_cast<Vst::IUnitHandler*> (static_cast<void*> (unitHandler))->release();
    unitInfo->release();
}

int32_t JuicyVST3Extensions::queryIEditController (const Steinberg::TUID iid, void** obj)
{
    // Respond ONLY to IUnitInfo — never FUnknown, which would hijack the
    // controller's identity. This intentionally shadows the wrapper's own
    // IUnitInfo (a debug-build jassertfalse in the wrapper's extractResult
    // flags the shadowing; that is expected and harmless).
    if (Steinberg::FUnknownPrivate::iidEqual (iid, Vst::IUnitInfo_iid))
    {
        unitInfo->addRef();
        *obj = static_cast<Vst::IUnitInfo*> (unitInfo);
        return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
}

int32_t JuicyVST3Extensions::queryIAudioProcessor (const Steinberg::TUID iid, void** obj)
{
    // identical shadow on the component object — see header comment
    return queryIEditController (iid, obj);
}

void JuicyVST3Extensions::setIComponentHandler (Steinberg::FUnknown* handler)
{
    if (unitHandler != nullptr)
    {
        static_cast<Vst::IUnitHandler*> (static_cast<void*> (unitHandler))->release();
        unitHandler = nullptr;
    }
    if (handler != nullptr)
    {
        void* out = nullptr;
        if (handler->queryInterface (Vst::IUnitHandler_iid, &out) == kResultOk && out != nullptr)
            unitHandler = static_cast<Steinberg::FUnknown*> (out); // holds the queryInterface ref
    }
}

void JuicyVST3Extensions::setProgramNames (const juce::StringArray& names)
{
    juicysf::vst3units::setProgramNames (names);
    if (unitHandler != nullptr)
        static_cast<Vst::IUnitHandler*> (static_cast<void*> (unitHandler))
            ->notifyProgramListChange (kJuicyProgramListId, Vst::kAllProgramInvalid);
}
