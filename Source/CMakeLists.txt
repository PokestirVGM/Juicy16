set(_juicysf_plugin_sources
    ChannelListComponent.cpp
    FilePicker.cpp
    FluidSynthModel.cpp
    MixerPanelComponent.cpp
    PluginEditor.cpp
    PluginProcessor.cpp
    SurjectiveMidiKeyboardComponent.cpp
    Theme.cpp
    VST3Multitimbral.cpp)

target_sources(JuicySFPlugin PRIVATE ${_juicysf_plugin_sources})

# Absolute paths, exported to the parent scope: JuicySFPlugin is created in the
# top-level directory, so its source-file properties must be set there.
set(_juicysf_absolute "")
foreach (_source IN LISTS _juicysf_plugin_sources)
  list(APPEND _juicysf_absolute "${CMAKE_CURRENT_SOURCE_DIR}/${_source}")
endforeach ()
set(JUICYSF_FIRST_PARTY_SOURCES "${_juicysf_absolute}" PARENT_SCOPE)
