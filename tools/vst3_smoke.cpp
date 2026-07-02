//
// Minimal VST3 host: loads the built JuicySF Rack.vst3, instantiates component +
// controller like a DAW, and verifies the per-channel unit machinery:
//   1. controller exposes IUnitInfo (our shadow, not JUCE's single-root version)
//   2. 17 units (root + Ch 1..16), each channel unit with the shared program list
//   3. getUnitByBus maps MIDI channel N -> unit N
//   4. the progChN parameters' unitIds (assigned by JUCE from parameter groups)
//      match the unit IDs our IUnitInfo reports — the invariant Cubase needs
//
// Build:
//   clang++ -std=c++17 tools/vst3_smoke.cpp -I "$SDK" -framework CoreFoundation -o /tmp/vst3_smoke
//   (SDK = JUCE's bundled VST3_SDK; interfaces are header-only, nothing links)
// Run:
//   /tmp/vst3_smoke "build/JuicySFPlugin_artefacts/Debug/VST3/JuicySF Rack.vst3"
//
#include <cstdio>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>

#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstunits.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>

using namespace Steinberg;

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  PASS  %s\n", name); \
    else { printf("  FAIL  %s\n", name); ++failures; } \
} while (0)

// same formula as the plugin + JUCE wrapper: juce::String("chUnitN").hashCode()
// (juce String hashCode: h = 31*h + char)
static int32 juceHashCode (const std::string& s) {
    int32 h = 0;
    for (char c : s) h = 31 * h + (int32) c;
    return h;
}
static Vst::UnitID expectedUnitId (int chZero) {
    return juceHashCode ("chUnit" + std::to_string (chZero + 1)) & 0x7fffffff;
}

static std::string toUtf8 (const Vst::TChar* s) {
    std::string out;
    for (int i = 0; s[i] != 0 && i < 128; ++i)
        out += (char) (s[i] < 128 ? s[i] : '?');
    return out;
}

int main (int argc, char** argv) {
    if (argc < 2) { printf ("usage: vst3_smoke <bundle.vst3>\n"); return 2; }
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

    // ---- the actual Stage-2 verification ----
    Vst::IUnitInfo* units = nullptr;
    controller->queryInterface (Vst::IUnitInfo_iid, (void**) &units);
    CHECK (units != nullptr, "controller exposes IUnitInfo");
    if (units) {
        const auto count = units->getUnitCount();
        printf ("  unit count = %d\n", count);
        CHECK (count == 17, "17 units (root + 16 channels) => OUR shadow, not JUCE's");

        Vst::UnitInfo info{};
        CHECK (units->getUnitInfo (0, info) == kResultTrue && info.id == Vst::kRootUnitId, "root unit at index 0");
        bool unitsOk = true, listOk = true;
        for (int ch = 0; ch < 16; ++ch) {
            if (units->getUnitInfo (ch + 1, info) != kResultTrue) { unitsOk = false; break; }
            if (info.id != expectedUnitId (ch)) { unitsOk = false; break; }
            if (info.programListId == Vst::kNoProgramListId) { listOk = false; break; }
        }
        CHECK (unitsOk, "channel units have expected hash-derived IDs");
        CHECK (listOk, "every channel unit references a program list");

        Vst::ProgramListInfo pli{};
        CHECK (units->getProgramListCount() == 1 && units->getProgramListInfo (0, pli) == kResultTrue
               && pli.programCount == 128, "one shared program list with 128 programs");

        Vst::String128 name{};
        CHECK (units->getProgramName (pli.id, 0, name) == kResultTrue, "getProgramName(0)");
        printf ("  program 0 name: \"%s\"\n", toUtf8 (name).c_str());

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
        int paramsInChannelUnits = 0, flaggedParams = 0;
        const auto n = controller->getParameterCount();
        for (int32 i = 0; i < n; ++i) {
            Vst::ParameterInfo pi{};
            if (controller->getParameterInfo (i, pi) != kResultOk) continue;
            for (int ch = 0; ch < 16; ++ch)
                if (pi.unitId == expectedUnitId (ch)) {
                    ++paramsInChannelUnits;
                    if ((pi.flags & Vst::ParameterInfo::kIsProgramChange) != 0)
                        ++flaggedParams;
                    break;
                }
        }
        printf ("  params inside channel units: %d (kIsProgramChange on %d)\n",
                paramsInChannelUnits, flaggedParams);
        CHECK (paramsInChannelUnits == 16, "exactly the 16 progChN params live in the channel units");
        CHECK (flaggedParams == 16, "all 16 channel program params carry kIsProgramChange");

        units->release();
    }

    // ---- component-side IUnitInfo (Cubase interrogates the COMPONENT, not the
    // controller, for unit structure; both must present our 17-unit view) ----
    {
        Vst::IUnitInfo* compUnits = nullptr;
        component->queryInterface (Vst::IUnitInfo_iid, (void**) &compUnits);
        CHECK (compUnits != nullptr, "component exposes IUnitInfo");
        if (compUnits) {
            CHECK (compUnits->getUnitCount() == 17, "component-side: 17 units (our shadow)");
            Vst::UnitID uid = -1;
            CHECK (compUnits->getUnitByBus (Vst::MediaTypes::kEvent, Vst::BusDirections::kInput, 0, 5, uid) == kResultTrue
                   && uid == expectedUnitId (5), "component-side: getUnitByBus channel->unit");
            Vst::ProgramListInfo pli{};
            CHECK (compUnits->getProgramListCount() == 1 && compUnits->getProgramListInfo (0, pli) == kResultTrue,
                   "component-side: program list present");
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

            bool pcMapped = true;
            for (int16 ch = 0; ch < 16; ++ch) {
                Vst::ParamID id = Vst::kNoParamId;
                if (mapping->getMidiControllerAssignment (0, ch, Vst::kCtrlProgramChange, id) != kResultTrue
                    || id != progParamIds[ch] || id == Vst::kNoParamId) { pcMapped = false; break; }
            }
            CHECK (pcMapped, "PC (ctrl 130) on channel N maps to that channel's progChN param");

            Vst::ParamID ccId = Vst::kNoParamId;
            CHECK (mapping->getMidiControllerAssignment (0, 3, 7, ccId) == kResultTrue && ccId != Vst::kNoParamId,
                   "CC table still intact (CC7 mapped)");
            Vst::ParamID oobId = Vst::kNoParamId;
            CHECK (mapping->getMidiControllerAssignment (0, 3, 131, oobId) != kResultTrue,
                   "out-of-table controllers rejected (no OOB read)");
            mapping->release();
        }
    }

    controller->terminate();
    controller->release();
    component->terminate();
    component->release();
    factory->release();

    printf ("== vst3_smoke: %d failures ==\n", failures);
    return failures == 0 ? 0 : 1;
}
