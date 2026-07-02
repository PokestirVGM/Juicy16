// JuicySF Rack: vendored replacement for JUCE's juce_audio_plugin_client_VST3.mm.
// Identical role to the stock file (Objective-C++ shim that includes the VST3
// wrapper implementation); the quoted include below resolves to the patched
// wrapper copy sitting next to this file. See the "JUICYSF RACK PATCH" comment
// in that file for the single functional change (kIsProgramChange on progChN).
#include "juce_audio_plugin_client_VST3.cpp"
