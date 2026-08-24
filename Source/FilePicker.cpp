//
// Created by Alex Birch on 03/10/2017.
//

#include "FilePicker.h"
#include "Theme.h"
#include "Util.h"

// #ifdef __APPLE__
//   #include <CoreFoundation/CFURL.h>
// #endif
#if JUCE_MAC || JUCE_IOS
  #include <juce_core/native/juce_CFHelpers_mac.h>
  #include <CoreFoundation/CFString.h>
  #include <CoreFoundation/CFData.h>
  #include <CoreFoundation/CFError.h>
  using juce::CFUniquePtr;
#endif

void FolderIconButton::paintButton(Graphics& g, bool isMouseOverButton, bool isButtonDown) {
    Colour colour{findColour(TextButton::textColourOffId)};
    if (isButtonDown)
        colour = colour.withAlpha(0.6f);
    else if (isMouseOverButton)
        colour = colour.withAlpha(0.8f);

    // The folder from the approved header design, traced in its own 24x24
    // coordinates so the proportions hold at any size. Corner rounds are
    // quadratics rather than true arcs; indistinguishable at a 14px icon.
    juce::Path folder;
    folder.startNewSubPath(3.0f, 7.0f);
    folder.quadraticTo(3.0f, 5.0f, 5.0f, 5.0f);   // top-left round
    folder.lineTo(9.0f, 5.0f);                     // tab top
    folder.lineTo(11.0f, 7.0f);                    // tab shoulder, sloped
    folder.lineTo(19.0f, 7.0f);                    // body top
    folder.quadraticTo(21.0f, 7.0f, 21.0f, 9.0f);  // top-right round
    folder.lineTo(21.0f, 17.0f);
    folder.quadraticTo(21.0f, 19.0f, 19.0f, 19.0f);
    folder.lineTo(5.0f, 19.0f);
    folder.quadraticTo(3.0f, 19.0f, 3.0f, 17.0f);
    folder.closeSubPath();

    // Scale the whole 24-unit grid - not the path's own bounds - to a 14px icon
    // box, so the stroke lands at the design's weight (2 units at 14/24 scale is
    // 1.17px) instead of being computed from whatever the outline happens to
    // span. Sizing from the bounds is what made the first version look heavy.
    const auto bounds{getLocalBounds().toFloat()};
    const float box{juce::jmin(14.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()))};
    const float scale{box / 24.0f};
    folder.applyTransform(
        juce::AffineTransform::scale(scale).translated(
            bounds.getCentreX() - box * 0.5f, bounds.getCentreY() - box * 0.5f));

    g.setColour(colour);
    g.strokePath(folder, juce::PathStrokeType{2.0f * scale,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded});
}

FilePicker::FilePicker(
    AudioProcessorValueTreeState& state
    // FluidSynthModel& fluidSynthModel
)
: fileChooser{
    "File",
    File(),
    true,
    false,
    false,
    "*.sf2;*.sf3;*.dls",
    String(),
    "Select a SoundFont or DLS file to load."}
, valueTreeState{state}
// , fluidSynthModel{fluidSynthModel}
// , currentPath{}
#if JUCE_MAC || JUCE_IOS
, bookmarkCreationOptions{kCFURLBookmarkCreationWithSecurityScope}
#endif
{
    // faster (rounded edges introduce transparency)
    setOpaque (true);

    fileChooser.setName("Sound bank file");
    fileChooser.setTitle("Sound bank file");
    fileChooser.setDescription("Selected DLS, SF2, or SF3 bank file");
    fileChooser.setHelpText("Choose a DLS, SF2, or SF3 bank for all 16 MIDI channels.");

    // setDisplayedFilePath(fluidSynthModel.getCurrentSoundFontAbsPath());
    setDisplayedFilePath(valueTreeState.state.getChildWithName("soundFont").getProperty("path", ""));

    addAndMakeVisible (fileChooser);
    fileChooser.addListener (this);
    // scoped LookAndFeel swaps the "..." browse button for a folder icon; assigning
    // it (rather than passing at construction) triggers FilenameComponent's
    // lookAndFeelChanged(), which recreates the browse button using it.
    fileChooser.setLookAndFeel(&folderIconLookAndFeel);
    valueTreeState.state.addListener(this);
//    valueTreeState.state.getChildWithName("soundFont").sendPropertyChangeMessage("path");

#if JUCE_MAC || JUCE_IOS
    bookmarkCreationOptions |= kCFURLBookmarkCreationSecurityScopeAllowOnlyReadAccess;
#endif
}
FilePicker::~FilePicker() {
    fileChooser.removeListener (this);
    valueTreeState.state.removeListener(this);
}

void FilePicker::resized() {
    Rectangle<int> r (getLocalBounds());
    fileChooser.setBounds (r);
}

/**
 * This is required to support setOpaque(true)
 */
void FilePicker::paint(Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(Juicy16::headerBackgroundColourId));
}

void FilePicker::filenameComponentChanged (FilenameComponent*) {
    // Set path first so the bookmark handler's path fallback reads the correct new path.
    {
        Value value{valueTreeState.state.getChildWithName("soundFont").getPropertyAsValue("path", nullptr)};
        value.setValue(fileChooser.getCurrentFile().getFullPathName());
    }
#if JUCE_MAC || JUCE_IOS
    CFUniquePtr<CFStringRef> fileExtensionCF{fileChooser.getCurrentFile().getFullPathName().toCFString()};
    CFUniquePtr<CFURLRef> cfURL{CFURLCreateWithFileSystemPath(NULL, fileExtensionCF.get(), CFURLPathStyle::kCFURLPOSIXPathStyle, false)};
    CFErrorRef cfError = nullptr;

    // CFURLCreateBookmarkData causes this error:
    // cannot open file at line 45340 of [d24547a13b]
    // os_unix.c:45340: (0) open(/var/db/DetachedSignatures) - Undefined error: 0
    CFUniquePtr<CFDataRef> cfData{CFURLCreateBookmarkData(NULL, cfURL.get(), bookmarkCreationOptions, NULL, NULL, &cfError)};

    if (cfData) {
        const UInt8 * cfDataBytePtr{CFDataGetBytePtr(cfData.get())};
        CFIndex cfDataByteLength{CFDataGetLength(cfData.get())};
        Value value{valueTreeState.state.getChildWithName("soundFont").getPropertyAsValue("bookmark", nullptr)};
        var bookmarkVar{static_cast<const void*>(cfDataBytePtr), static_cast<size_t>(cfDataByteLength)};
        value.setValue(bookmarkVar);
    } else {
        // Clear a bookmark left by the previously selected file. This triggers the
        // model's path fallback instead of waiting forever for a bookmark update.
        MemoryBlock emptyBookmark;
        Value value{valueTreeState.state.getChildWithName("soundFont").getPropertyAsValue("bookmark", nullptr)};
        value.setValue(var{std::move(emptyBookmark)});
    }
    if (cfError != nullptr)
        CFRelease(cfError);
#endif
}

void FilePicker::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                               const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")) {
        if (property == StringRef("path")) {
            String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
            setDisplayedFilePath(soundFontPath);
        } else if (property == StringRef("loadMessage")) {
            fileChooser.setTooltip(treeWhosePropertyHasChanged.getProperty("loadMessage").toString());
        }
    }
}

void FilePicker::setDisplayedFilePath(const String& path) {
     if (!shouldChangeDisplayedFilePath(path)) {
         return;
     }
    currentPath = path;
    fileChooser.setCurrentFile(File(path), true, dontSendNotification);
}

bool FilePicker::shouldChangeDisplayedFilePath(const String &path) {
    if (path.isEmpty()) {
        return false;
    }
    if (path == currentPath) {
        return false;
    }
    return true;
}
