// The only application TU that includes the VST3 SDK. It links no SDK symbols,
// so it is safe in the shared AU/Standalone code.

#include "VST3Multitimbral.h"
#include "Vst3Units.h"

// Program names read by the wrapper's IUnitInfo on both VST3 objects.
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
using Steinberg::kResultOk;
constexpr Vst::ProgramListID kJuicyProgramListId = juicysf::vst3units::kProgramListId;

//==============================================================================
JuicyVST3Extensions::JuicyVST3Extensions() = default;

JuicyVST3Extensions::~JuicyVST3Extensions()
{
    if (unitHandler != nullptr)
        static_cast<Vst::IUnitHandler*> (static_cast<void*> (unitHandler))->release();
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
