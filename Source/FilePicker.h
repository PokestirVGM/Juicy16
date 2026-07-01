//
// Created by Alex Birch on 03/10/2017.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"

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
// so it doesn't affect the rest of the plugin's UI.
class FilePickerLookAndFeel : public LookAndFeel_V4
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
        const int buttonWidth{filenameComp.getHeight()};
        browseButton->setSize(buttonWidth, filenameComp.getHeight());
        browseButton->setTopRightPosition(filenameComp.getWidth(), 0);
        filenameBox->setBounds(0, 0, browseButton->getX(), filenameComp.getHeight());
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
    ~FilePicker();

    void resized() override;
    void paint (Graphics& g) override;

    void setDisplayedFilePath(const String&);
    

    virtual void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                           const Identifier& property) override;
    inline virtual void valueTreeChildAdded (ValueTree& parentTree,
                                             ValueTree& childWhichHasBeenAdded) override {};
    inline virtual void valueTreeChildRemoved (ValueTree& parentTree,
                                               ValueTree& childWhichHasBeenRemoved,
                                               int indexFromWhichChildWasRemoved) override {};
    inline virtual void valueTreeChildOrderChanged (ValueTree& parentTreeWhoseChildrenHaveMoved,
                                                    int oldIndex, int newIndex) override {};
    inline virtual void valueTreeParentChanged (ValueTree& treeWhoseParentHasChanged) override {};
    inline virtual void valueTreeRedirected (ValueTree& treeWhichHasBeenChanged) override {};
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
