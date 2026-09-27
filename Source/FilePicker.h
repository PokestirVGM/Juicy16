#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"
#include "Theme.h"
#include "GuiConstants.h"

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

class FilePicker: public Component,
                  public ValueTree::Listener,
                  private FilenameComponentListener
{
public:
    FilePicker(
        AudioProcessorValueTreeState& valueTreeState
    );
    ~FilePicker() override;

    void resized() override;
    void paint (Graphics& g) override;

    void setDisplayedFilePath(const String&);
    

    void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                   const Identifier& property) override;
    void valueTreeChildAdded (ValueTree&, ValueTree&) override {}
    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override {}
    void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    void valueTreeParentChanged (ValueTree&) override {}
    void valueTreeRedirected (ValueTree&) override {}
private:
    // Declared before fileChooser so it outlives it.
    FilePickerLookAndFeel folderIconLookAndFeel;
    FilenameComponent fileChooser;

    AudioProcessorValueTreeState& valueTreeState;

    String currentPath;

#if JUCE_MAC || JUCE_IOS
    CFURLBookmarkCreationOptions bookmarkCreationOptions;
#endif

    void filenameComponentChanged (FilenameComponent*) override;

    bool shouldChangeDisplayedFilePath(const String &path);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilePicker)
};
