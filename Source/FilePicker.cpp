#include "FilePicker.h"
#include "Theme.h"
#include "Util.h"
#include "MidiFilePlayer.h"

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
    AudioProcessorValueTreeState& state, FluidSynthModel& synthModel
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
, model{synthModel}
#if JUCE_MAC || JUCE_IOS
, bookmarkCreationOptions{kCFURLBookmarkCreationWithSecurityScope}
#endif
{
    // Rounded edges would add transparency.
    setOpaque (true);
    setName("Sound bank file picker");

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
    combinedChooser.reset();
    fileChooser.removeListener (this);
    valueTreeState.state.removeListener(this);
    cancelPendingUpdate();
    fileChooser.setLookAndFeel(nullptr);
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

void FilePicker::lookAndFeelChanged() {
    // The browse-button factory needs its own LookAndFeel, but its palette
    // must follow the editor rather than keeping the default accent.
    if (auto* theme = dynamic_cast<Juicy16::PluginLookAndFeel*>(&getLookAndFeel())) {
        if (folderIconLookAndFeel.getAccent() != theme->getAccent())
            folderIconLookAndFeel.setAccent(theme->getAccent());
    }
}

void FilePicker::filenameComponentChanged (FilenameComponent*) {
    const File selectedFile{fileChooser.getCurrentFile()};
    juce::MemoryBlock bookmark;
#if JUCE_MAC || JUCE_IOS
    CFUniquePtr<CFStringRef> fileExtensionCF{selectedFile.getFullPathName().toCFString()};
    CFUniquePtr<CFURLRef> cfURL{CFURLCreateWithFileSystemPath(nullptr, fileExtensionCF.get(), CFURLPathStyle::kCFURLPOSIXPathStyle, false)};
    CFErrorRef cfError = nullptr;

    // Logs a harmless "open(/var/db/DetachedSignatures)" error.
    CFUniquePtr<CFDataRef> cfData{CFURLCreateBookmarkData(nullptr, cfURL.get(), bookmarkCreationOptions, nullptr, nullptr, &cfError)};

    if (cfData) {
        const UInt8 * cfDataBytePtr{CFDataGetBytePtr(cfData.get())};
        CFIndex cfDataByteLength{CFDataGetLength(cfData.get())};
        bookmark.replaceAll(cfDataBytePtr, static_cast<size_t>(cfDataByteLength));
    }
    if (cfError != nullptr)
        CFRelease(cfError);
#endif
    // Publish the selected path and its bookmark together. A rejected bank must
    // not roll back the path before the new bookmark's fallback is attempted.
    model.restoreFontSelection(selectedFile.getFullPathName(), bookmark, true);
}

void FilePicker::setMidiFileLoader(std::function<bool(const File&, String&)> loader,
                                 std::function<void(const String&)> resultHandler) {
    midiFileLoader = std::move(loader);
    loadResultHandler = std::move(resultHandler);
    fileChooser.setBrowseCallback(midiFileLoader ? std::function<void()>{[this] { chooseFiles(); }}
                                               : std::function<void()>{});
}

void FilePicker::chooseFiles() {
    combinedChooser = std::make_unique<juce::FileChooser>(
        "Choose a sound bank and MIDI file", fileChooser.getCurrentFile(),
        "*.sf2;*.sf3;*.dls;*.mid;*.midi");
    const auto safeThis = Component::SafePointer<FilePicker>{this};
    combinedChooser->launchAsync(juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis](const juce::FileChooser& chooser) {
            if (safeThis == nullptr)
                return;
            String error;
            safeThis->loadSelectedFiles(chooser.getResults(), error);
        });
}

bool FilePicker::loadSelectedFiles(const juce::Array<File>& files, String& error) {
    error.clear();
    if (files.isEmpty())
        return true;
    const auto finish = [this, &error](bool success) {
        if (loadResultHandler)
            loadResultHandler(error);
        return success;
    };
    File bank, midi;
    for (const auto& file : files) {
        if (!file.existsAsFile()) {
            error = "The selected file could not be opened: " + file.getFileName();
            return finish(false);
        }
        if (file.hasFileExtension("sf2;sf3;dls")) {
            if (bank != File{}) {
                error = "Choose one sound bank at a time, optionally with one MIDI file.";
                return finish(false);
            }
            bank = file;
        } else if (file.hasFileExtension("mid;midi")) {
            if (!midiFileLoader) {
                error = "MIDI file playback is available only in Standalone.";
                return finish(false);
            }
            if (midi != File{}) {
                error = "Choose one MIDI file at a time, optionally with one sound bank.";
                return finish(false);
            }
            midi = file;
        } else {
            error = "Choose a DLS, SF2, or SF3 sound bank or a MID/MIDI file.";
            return finish(false);
        }
    }
    // Check the complete MIDI before changing the bank. A malformed selection
    // must not replace the working pair with half of the requested new pair.
    if (midi != File{} && !MidiFilePlayer::validateFile(midi, error))
        return finish(false);
    if (bank != File{}) {
        fileChooser.setCurrentFile(bank, true, dontSendNotification);
        filenameComponentChanged(&fileChooser);
        const auto fontState = valueTreeState.state.getChildWithName("soundFont");
        if (fontState.getProperty("loadStatus").toString() == "error") {
            error = fontState.getProperty("loadMessage").toString();
            return finish(false);
        }
    }
    if (midi != File{} && !midiFileLoader(midi, error))
        return finish(false);
    return finish(true);
}

void FilePicker::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                               const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() != StringRef("soundFont")
        || (property != StringRef("path") && property != StringRef("loadMessage")))
        return;
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        triggerAsyncUpdate();
        return;
    }
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")) {
        if (property == StringRef("path")) {
            String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
            setDisplayedFilePath(soundFontPath);
        } else if (property == StringRef("loadMessage")) {
            fileChooser.setTooltip(treeWhosePropertyHasChanged.getProperty("loadMessage").toString());
        }
    }
}

void FilePicker::handleAsyncUpdate() {
    const auto font{valueTreeState.state.getChildWithName("soundFont")};
    setDisplayedFilePath(font.getProperty("path", "").toString());
    fileChooser.setTooltip(font.getProperty("loadMessage").toString());
}

void FilePicker::setDisplayedFilePath(const String& path) {
     if (!shouldChangeDisplayedFilePath(path)) {
         return;
     }
    currentPath = path;
    fileChooser.setCurrentFile(File(path), path.isNotEmpty(), dontSendNotification);
}

bool FilePicker::shouldChangeDisplayedFilePath(const String &path) {
    if (path == currentPath) {
        return false;
    }
    return true;
}
