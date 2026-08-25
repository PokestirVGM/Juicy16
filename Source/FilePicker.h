//
// Created by Alex Birch on 03/10/2017.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"
#include "Theme.h"
#include "GuiConstants.h"

#if JUCE_MAC || JUCE_IOS
//   #include <CoreFoundation/CoreFoundation.h>
  #include <CoreFoundation/CFURL.h>
#endif

// Draws a plain folder glyph instead of FilenameComponent's default "..." browse
// button text.
class FolderIconButton : public Button
{
public:
    explicit FolderIconButton(const String& buttonName) : Button(buttonName) {}

private:
    void paintButton(Graphics& g, bool isMouseOverButton, bool isButtonDown) override;
};

// Scoped to just the FilePicker's FilenameComponent (via Component::setLookAndFeel),
// so it doesn't affect the rest of the plugin's UI. Derives from the plugin's
// own LookAndFeel, not LookAndFeel_V4: overriding the browse button must not
// also opt this one control out of the palette.
class FilePickerLookAndFeel : public Juicy16::PluginLookAndFeel
{
public:
    Button* createFilenameComponentBrowseButton(const String& text) override {
        return new FolderIconButton(text);
    }

    // The base implementation sizes the browse button to a fixed 80px (sized for
    // "..." text) unless it's a TextButton, which FolderIconButton isn't. Make it
    // a compact square icon button instead.
    void layoutFilenameComponent(FilenameComponent& filenameComp, juce::ComboBox* filenameBox, Button* browseButton) override {
        if (browseButton == nullptr || filenameBox == nullptr)
            return;
        // Just enough box for the glyph plus a small even inset, so the space the
        // eye reads either side of the folder is this inset plus the gap below,
        // rather than whatever a square the height of the field left over.
        const int buttonWidth{GuiConstants::folderIconSize + GuiConstants::innerPadding / 2};
        browseButton->setSize(buttonWidth, filenameComp.getHeight());
        browseButton->setTopRightPosition(filenameComp.getWidth(), 0);
        // Leave a gap between the field's border and the folder. Butted right up
        // against it the folder read as part of the field, and the cog that used
        // to sit beside it made the spacing look uneven from the other side too.
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
        // FluidSynthModel& fluidSynthModel
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
    // declared before fileChooser so it outlives it: members destruct in reverse
    // declaration order, and fileChooser must not hold a dangling LookAndFeel*.
    FilePickerLookAndFeel folderIconLookAndFeel;
    FilenameComponent fileChooser;

    AudioProcessorValueTreeState& valueTreeState;
    // FluidSynthModel& fluidSynthModel;

    String currentPath;

#if JUCE_MAC || JUCE_IOS
    CFURLBookmarkCreationOptions bookmarkCreationOptions;
#endif

    void filenameComponentChanged (FilenameComponent*) override;

    bool shouldChangeDisplayedFilePath(const String &path);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilePicker)
};
