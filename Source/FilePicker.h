#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"
#include "Theme.h"
#include "GuiConstants.h"
#include <functional>

#if JUCE_MAC || JUCE_IOS
  #include <CoreFoundation/CFURL.h>
#endif

// Folder glyph instead of FilenameComponent's "..." browse text.
class FolderIconButton : public Button
{
public:
    explicit FolderIconButton(const String& buttonName) : Button(buttonName) {}

private:
    void paintButton(Graphics& g, bool isMouseOverButton, bool isButtonDown) override;
};

// Scoped to the file picker. Derives from the plugin LookAndFeel so the
// control keeps the palette.
class FilePickerLookAndFeel : public Juicy16::PluginLookAndFeel
{
public:
    Button* createFilenameComponentBrowseButton(const String& text) override {
        return new FolderIconButton(text);
    }

    // Compact square browse button instead of the fixed 80px text button.
    void layoutFilenameComponent(FilenameComponent& filenameComp, juce::ComboBox* filenameBox, Button* browseButton) override {
        if (browseButton == nullptr || filenameBox == nullptr)
            return;
        const int buttonWidth{GuiConstants::folderIconSize + GuiConstants::innerPadding / 2};
        browseButton->setSize(buttonWidth, filenameComp.getHeight());
        browseButton->setTopRightPosition(filenameComp.getWidth(), 0);
        // Keep a gap so the folder does not read as part of the field.
        filenameBox->setBounds(0, 0,
                               browseButton->getX() - GuiConstants::innerPadding,
                               filenameComp.getHeight());
    }
};

// FilenameComponent recreates its browse button whenever the palette changes.
// Reapply the standalone chooser callback after JUCE installs its default one.
class FilePickerFilenameComponent final : public FilenameComponent
{
public:
    using FilenameComponent::FilenameComponent;
    void setBrowseCallback(std::function<void()> callback) {
        browseCallback = std::move(callback);
        lookAndFeelChanged();
    }
    void lookAndFeelChanged() override {
        FilenameComponent::lookAndFeelChanged();
        for (auto* child : getChildren()) {
            if (auto* button = dynamic_cast<Button*>(child)) {
                const String name{browseCallback ? "Load sound bank and MIDI files" : "Load sound bank"};
                button->setName(name);
                button->setTitle(name);
                button->setTooltip(browseCallback
                    ? "Select a sound bank and a MIDI file together, or either file on its own."
                    : "Select a DLS, SF2, or SF3 sound bank.");
                if (browseCallback)
                    button->onClick = [this] { browseCallback(); };
            }
        }
    }
private:
    std::function<void()> browseCallback;
};

class FilePicker: public Component,
                  public ValueTree::Listener,
                  public juce::AsyncUpdater,
                  private FilenameComponentListener
{
public:
    FilePicker(
        AudioProcessorValueTreeState& valueTreeState, FluidSynthModel& model
    );
    ~FilePicker() override;

    void resized() override;
    void paint (Graphics& g) override;
    void lookAndFeelChanged() override;

    void setDisplayedFilePath(const String&);
    void setMidiFileLoader(std::function<bool(const File&, String&)> loader,
                           std::function<void(const String&)> resultHandler);
    bool loadSelectedFiles(const juce::Array<File>& files, String& error);

    void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                   const Identifier& property) override;
    void valueTreeChildAdded (ValueTree&, ValueTree&) override {}
    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override {}
    void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    void valueTreeParentChanged (ValueTree&) override {}
    void valueTreeRedirected (ValueTree&) override {}
private:
    void handleAsyncUpdate() override;
    // Declared before fileChooser so it outlives it.
    FilePickerLookAndFeel folderIconLookAndFeel;
    FilePickerFilenameComponent fileChooser;
    std::unique_ptr<juce::FileChooser> combinedChooser;
    std::function<bool(const File&, String&)> midiFileLoader;
    std::function<void(const String&)> loadResultHandler;

    AudioProcessorValueTreeState& valueTreeState;
    FluidSynthModel& model;

    String currentPath;

#if JUCE_MAC || JUCE_IOS
    CFURLBookmarkCreationOptions bookmarkCreationOptions;
#endif

    void filenameComponentChanged (FilenameComponent*) override;
    void chooseFiles();

    bool shouldChangeDisplayedFilePath(const String &path);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilePicker)
};
