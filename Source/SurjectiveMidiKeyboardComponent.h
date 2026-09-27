#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <map>

using namespace std;

/*
 * Forked from JUCE/modules/juce_audio_utils/gui/juce_SurjectiveMidiKeyboardComponent.h,
 * which had the following license:
 */
/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2017 - ROLI Ltd.

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 5 End-User License
   Agreement and JUCE 5 Privacy Policy (both updated and effective as of the
   27th April 2017).

   End User License Agreement: www.juce.com/juce-5-licence
   Privacy Policy: www.juce.com/juce-5-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/
//==============================================================================
// Clickable piano keyboard, forked from JUCE's MidiKeyboardComponent so several
// QWERTY keys can map to the same note.
namespace juce
{

class SurjectiveMidiKeyboardComponent  : public Component,
                                         public MidiKeyboardStateListener,
                                         public ChangeBroadcaster,
                                         private Timer
{
public:
    //==============================================================================
    // The direction of the keyboard.
    enum Orientation
    {
        horizontalKeyboard,
        verticalKeyboardFacingLeft,
        verticalKeyboardFacingRight,
    };

    // Creates a SurjectiveMidiKeyboardComponent.
    SurjectiveMidiKeyboardComponent (MidiKeyboardState& state,
            Orientation orientation);

    ~SurjectiveMidiKeyboardComponent() override;

    //==============================================================================
    // Changes the velocity used in midi note-on messages that are triggered by clicking on
    // the component.
    void setVelocity (float velocity, bool useMousePositionForVelocity);

    // Changes the midi channel number that will be used for events triggered by clicking
    // on the component.
    void setMidiChannel (int midiChannelNumber);

    // Returns the midi channel that the keyboard is using for midi messages.
    int getMidiChannel() const noexcept                             { return midiChannel; }

    // Sets a mask to indicate which incoming midi channels should be represented by key
    // movements.
    void setMidiChannelsToDisplay (int midiChannelMask);

    // Returns the current set of midi channels represented by the component.
    int getMidiChannelsToDisplay() const noexcept                   { return midiInChannelMask; }

    //==============================================================================
    // Changes the width used to draw the white keys.
    void setKeyWidth (float widthInPixels);

    // Returns the width that was set by setKeyWidth().
    float getKeyWidth() const noexcept                              { return keyWidth; }

    // Changes the keyboard's current direction.
    void setOrientation (Orientation newOrientation);

    // Returns the keyboard's current direction.
    Orientation getOrientation() const noexcept                     { return orientation; }

    // Sets the range of midi notes that the keyboard will be limited to.
    void setAvailableRange (int lowestNote,
            int highestNote);

    // Returns the first note in the available range.
    int getRangeStart() const noexcept                              { return rangeStart; }

    // Returns the last note in the available range.
    int getRangeEnd() const noexcept                                { return rangeEnd; }

    // If the keyboard extends beyond the size of the component, this will scroll it to
    // show the given key at the start.
    void setLowestVisibleKey (int noteNumber);

    // Returns the number of the first key shown in the component.
    int getLowestVisibleKey() const noexcept                        { return (int) firstKey; }

    // Sets the length of the black notes as a proportion of the white note length.
    void setBlackNoteLengthProportion (float ratio) noexcept;

    // Returns the length of the black notes as a proportion of the white note length.
    float getBlackNoteLengthProportion() const noexcept             { return blackNoteLengthRatio; }

    // Returns the absolute length of the black notes.
    int getBlackNoteLength() const noexcept;

    // If set to true, then scroll buttons will appear at either end of the keyboard if
    // there are too many notes to fit them all in the component at once.
    void setScrollButtonsVisible (bool canScroll);

    //==============================================================================
    // A set of colour IDs to use to change the colour of various aspects of the keyboard.
    enum ColourIds
    {
        whiteNoteColourId               = 0x1005000,
        blackNoteColourId               = 0x1005001,
        keySeparatorLineColourId        = 0x1005002,
        mouseOverKeyOverlayColourId     = 0x1005003,  /**< This colour will be overlaid on the normal note colour. */
        keyDownOverlayColourId          = 0x1005004,  /**< This colour will be overlaid on the normal note colour. */
        textLabelColourId               = 0x1005005,
        // JUCE moved these IDs when it introduced KeyboardComponentBase.
        // Use the current IDs so this keyboard receives the registered theme.
        upDownButtonBackgroundColourId  = MidiKeyboardComponent::upDownButtonBackgroundColourId,
        upDownButtonArrowColourId       = MidiKeyboardComponent::upDownButtonArrowColourId,
        shadowColourId                  = MidiKeyboardComponent::shadowColourId
    };

    // Returns the position within the component of the left-hand edge of a key.
    int getKeyStartPosition (int midiNoteNumber) const;

    // Returns the total width needed to fit all the keys in the available range.
    int getTotalKeyboardWidth() const noexcept;

    // Returns the key at a given coordinate.
    int getNoteAtPosition (juce::Point<int> position);

    //==============================================================================
    // Deletes all key-mappings.
    void clearKeyMappings();

    // Maps a key-press to a given note.
    void setKeyPressForNote (const KeyPress& key,
            int midiNoteOffsetFromC);

    // Removes any key-mappings for a given note.
    void removeKeyPressForNote (int midiNoteOffsetFromC);

    // Changes the base note above which key-press-triggered notes are played.
    void setKeyPressBaseOctave (int newOctaveNumber);

    // This sets the octave number which is shown as the octave number for middle C.
    void setOctaveForMiddleC (int octaveNumForMiddleC);

    // This returns the value set by setOctaveForMiddleC().
    int getOctaveForMiddleC() const noexcept            { return octaveNumForMiddleC; }

    //==============================================================================
    void paint (Graphics&) override;
    void resized() override;
    void mouseMove (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseEnter (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
    void timerCallback() override;
    bool keyStateChanged (bool isKeyDown) override;
    bool keyPressed (const KeyPress&) override;
    void focusLost (FocusChangeType) override;
    void handleNoteOn (MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void colourChanged() override;

protected:
    //==============================================================================
    // Draws a white note in the given rectangle.
    virtual void drawWhiteNote (int midiNoteNumber,
            Graphics& g,
            int x, int y, int w, int h,
            bool isDown, bool isOver,
            const Colour& lineColour,
            const Colour& textColour);

    // Draws a black note in the given rectangle.
    virtual void drawBlackNote (int midiNoteNumber,
            Graphics& g,
            int x, int y, int w, int h,
            bool isDown, bool isOver,
            const Colour& noteFillColour);

    // Allows text to be drawn on the white notes.
    virtual String getWhiteNoteText (const int midiNoteNumber);

    // Draws the up and down buttons that change the base note.
    virtual void drawUpDownButton (Graphics& g, int w, int h,
            const bool isMouseOver,
            const bool isButtonPressed,
            const bool movesOctavesUp);

    // Callback when the mouse is clicked on a key.
    virtual bool mouseDownOnKey (int midiNoteNumber, const MouseEvent& e);

    // Callback when the mouse is dragged from one key onto another.
    virtual void mouseDraggedToKey (int midiNoteNumber, const MouseEvent& e);

    // Callback when the mouse is released from a key.
    virtual void mouseUpOnKey (int midiNoteNumber, const MouseEvent& e);

    // Calculates the position of a given midi-note.
    virtual void getKeyPosition (int midiNoteNumber, float keyWidth,
            int& x, int& w) const;

    // Returns the rectangle for a given key if within the displayable range
    Rectangle<int> getRectangleForKey (int midiNoteNumber) const;


private:
    //==============================================================================
    friend class SurjectiveMidiKeyboardUpDownButton;

    MidiKeyboardState& state;
    float blackNoteLengthRatio;
    int xOffset;
    float keyWidth;
    Orientation orientation;

    int midiChannel, midiInChannelMask;
    float velocity;

    Array<int> mouseOverNotes, mouseDownNotes;
    BigInteger keysPressed, keysCurrentlyDrawnDown;
    bool shouldCheckState;

    int rangeStart, rangeEnd;
    float firstKey;
    bool canScroll, useMousePositionForVelocity, shouldCheckMousePos;
    std::unique_ptr<Button> scrollDown, scrollUp;

    typedef multimap<int, KeyPress> DegreeToAscii;
    DegreeToAscii degreeToAsciis;

    int keyMappingOctave, octaveNumForMiddleC;

    static const uint8 whiteNotes[];
    static const uint8 blackNotes[];

    void getKeyPos (int midiNoteNumber, int& x, int& w) const;
    int xyToNote (juce::Point<int>, float& mousePositionVelocity);
    int remappedXYToNote (juce::Point<int>, float& mousePositionVelocity) const;
    void resetAnyKeysInUse();
    void updateNoteUnderMouse (juce::Point<int>, bool isDown, int fingerNum);
    void updateNoteUnderMouse (const MouseEvent&, bool isDown);
    void repaintNote (int midiNoteNumber);
    void setLowestVisibleKeyFloat (float noteNumber);
    void bindKeysToMidiKeyboard();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SurjectiveMidiKeyboardComponent)
};
}
