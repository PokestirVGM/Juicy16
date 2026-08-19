Legacy Projucer output
======================

The current project is built exclusively through the root CMakeLists.txt and the installed, exactly pinned JUCE package. Source files still include JuceHeader.h for compatibility aliases. The obsolete generated include_juce_* translation units were removed because CMake never compiled them and they misleadingly advertised unsupported formats.

Do not regenerate those legacy translation units or treat AppConfig.h as the canonical release manifest. CMake project/plugin metadata is authoritative. The retained fallback values are kept aligned only to avoid misleading source audits and should eventually disappear when source files use normal JUCE headers directly.
