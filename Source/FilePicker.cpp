#include "FilePicker.h"
#include "Theme.h"
#include "Util.h"

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

    // Folder outline on a 24x24 grid.
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

    // Scale the 24-unit grid, not the path bounds, so the stroke keeps the design weight.
    const auto bounds{getLocalBounds().toFloat()};
    const float box{juce::jmin(static_cast<float>(GuiConstants::folderIconSize),
                               juce::jmin(bounds.getWidth(), bounds.getHeight()))};
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
#if JUCE_MAC || JUCE_IOS
, bookmarkCreationOptions{kCFURLBookmarkCreationWithSecurityScope}
#endif
{
    // Rounded edges would add transparency.
    setOpaque (true);

    fileChooser.setName("Sound bank file");
    fileChooser.setTitle("Sound bank file");
    fileChooser.setDescription("Selected DLS, SF2, or SF3 bank file");
    fileChooser.setHelpText("Choose a DLS, SF2, or SF3 bank for all 16 MIDI channels.");

    setDisplayedFilePath(valueTreeState.state.getChildWithName("soundFont").getProperty("path", ""));

    addAndMakeVisible (fileChooser);
    fileChooser.addListener (this);
    // Setting it after construction makes FilenameComponent rebuild the browse button.
    fileChooser.setLookAndFeel(&folderIconLookAndFeel);
    valueTreeState.state.addListener(this);

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

// Required by setOpaque(true).
void FilePicker::paint(Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(Juicy16::headerBackgroundColourId));
}

void FilePicker::filenameComponentChanged (FilenameComponent*) {
    // Path first, so the bookmark handler's fallback reads the new path.
    {
        Value value{valueTreeState.state.getChildWithName("soundFont").getPropertyAsValue("path", nullptr)};
        value.setValue(fileChooser.getCurrentFile().getFullPathName());
    }
#if JUCE_MAC || JUCE_IOS
    CFUniquePtr<CFStringRef> fileExtensionCF{fileChooser.getCurrentFile().getFullPathName().toCFString()};
    CFUniquePtr<CFURLRef> cfURL{CFURLCreateWithFileSystemPath(nullptr, fileExtensionCF.get(), CFURLPathStyle::kCFURLPOSIXPathStyle, false)};
    CFErrorRef cfError = nullptr;

    // Logs a harmless "open(/var/db/DetachedSignatures)" error.
    CFUniquePtr<CFDataRef> cfData{CFURLCreateBookmarkData(nullptr, cfURL.get(), bookmarkCreationOptions, nullptr, nullptr, &cfError)};

    if (cfData) {
        const UInt8 * cfDataBytePtr{CFDataGetBytePtr(cfData.get())};
        CFIndex cfDataByteLength{CFDataGetLength(cfData.get())};
        Value value{valueTreeState.state.getChildWithName("soundFont").getPropertyAsValue("bookmark", nullptr)};
        var bookmarkVar{static_cast<const void*>(cfDataBytePtr), static_cast<size_t>(cfDataByteLength)};
        value.setValue(bookmarkVar);
    } else {
        // Clear the previous file's bookmark so the model falls back to the path.
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
