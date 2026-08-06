Legacy Projucer output
======================

The current project is built exclusively through the root CMakeLists.txt and the installed, exactly pinned JUCE package. Source files include JuceHeader.h for compatibility aliases, but the generated include_juce_* translation units in this directory are not compiled by Source/CMakeLists.txt.

Do not add these legacy translation units to a build or treat AppConfig.h as the canonical release manifest. CMake project/plugin metadata is authoritative. The retained fallback values are kept aligned only to avoid misleading source audits and should eventually disappear when source files use normal JUCE headers directly.
