//
// TEMPORARY Cubase VST3 bring-up diagnostics.
//
// Counters written from the vendored VST3 wrapper (getMidiControllerAssignment)
// and read by the status-bar monitor, so we can see — from inside a running DAW —
// whether the host uses IMidiMapping to route MIDI, and whether it ever asks
// about Program Change (controller 130). Defined in VST3Multitimbral.cpp (always
// part of the shared code, so the symbols exist for the AU/Standalone builds too,
// where they simply stay at zero because the VST3 wrapper isn't linked).
//
// Remove once Cubase routing is understood.
//

#pragma once

#include <atomic>

namespace juicysf::diag {
    extern std::atomic<int> midiMapCalls;    // total getMidiControllerAssignment calls
    extern std::atomic<int> midiMapMaxCtrl;  // highest controllerNumber the host asked about
    extern std::atomic<int> midiMapPcCalls;  // calls specifically for kCtrlProgramChange (130)

    // IUnitInfo activity: does the host read our unit/program-list structure at all?
    extern std::atomic<int> unitInfoCalls;   // any IUnitInfo method
    extern std::atomic<int> unitByBusCalls;  // getUnitByBus (the channel->unit map)
    extern std::atomic<int> programListCalls;// getProgramListInfo/getProgramName
}
