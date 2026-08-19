//
// See VST3Multitimbral.h for the design. This TU is the only application source
// that includes the VST3 SDK. It links against no SDK implementation symbols, so
// it remains safe in the shared code used by AU and Standalone targets.
//

#include "VST3Multitimbral.h"
#include "Vst3Units.h"

// Shared program-name store (see Vst3Units.h) read by the vendored wrapper's
// component-side and controller-side IUnitInfo implementations.
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
