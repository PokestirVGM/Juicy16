//
// Created by Alex Birch on 03/10/2017.
//

#include "FilePicker.h"
#include "MyColours.h"
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

    // square icon area, inset from the button bounds
    const float side{juce::jmin(static_cast<float>(getWidth()), static_cast<float>(getHeight()))};
    juce::Rectangle<float> b{juce::Rectangle<float>{side, side}
        .withCentre(getLocalBounds().toFloat().getCentre())
        .reduced(side * 0.22f)};

    const float tabWidth{b.getWidth() * 0.5f};
    const float tabHeight{b.getHeight() * 0.22f};
    const float bodyTop{b.getY() + tabHeight};

    // single-outline folder silhouette (tab flush with the top-left corner,
    // stepping down to the body): unfilled line art, not a filled icon.
    juce::Path folder;
    folder.startNewSubPath(b.getX(), b.getBottom());
    folder.lineTo(b.getX(), b.getY());
    folder.lineTo(b.getX() + tabWidth, b.getY());
    folder.lineTo(b.getX() + tabWidth, bodyTop);
    folder.lineTo(b.getRight(), bodyTop);
    folder.lineTo(b.getRight(), b.getBottom());
    folder.closeSubPath();

    g.setColour(colour);
    g.strokePath(folder, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

FilePicker::FilePicker(
    AudioProcessorValueTreeState& valueTreeState
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
, valueTreeState{valueTreeState}
// , fluidSynthModel{fluidSynthModel}
// , currentPath{}
#if JUCE_MAC || JUCE_IOS
, bookmarkCreationOptions{kCFURLBookmarkCreationWithSecurityScope}
#endif
{
    // faster (rounded edges introduce transparency)
    setOpaque (true);

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
    g.fillAll(MyColours::getUIColourIfAvailable(LookAndFeel_V4::ColourScheme::UIColour::windowBackground, juce::Colours::lightgrey));
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
    }
#endif
}

void FilePicker::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                               const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")) {
    // if (&treeWhosePropertyHasChanged == &valueTree) {
        if (property == StringRef("path")) {
            String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
            DEBUG_PRINT(soundFontPath);
            setDisplayedFilePath(soundFontPath);
            // if (soundFontPath.isNotEmpty()) {
            //     loadFont(soundFontPath);
            // }
        }
    }
}

void FilePicker::setDisplayedFilePath(const String& path) {
     if (!shouldChangeDisplayedFilePath(path)) {
         return;
     }
    // currentPath = path;
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
