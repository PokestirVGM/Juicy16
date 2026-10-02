#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginProcessor.h"
#include "GuiConstants.h"

// Standalone transport. It inherits the editor's existing palette and controls.
class MidiPlayerComponent final : public juce::Component,
                                  public juce::FileDragAndDropTarget,
                                  public juce::SettableTooltipClient,
                                  private juce::Timer {
public:
    static constexpr int preferredHeight = 144;

    explicit MidiPlayerComponent(JuicySFAudioProcessor&);
    ~MidiPlayerComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void togglePlayback();
    void showLoadResult(const juce::String& error);

private:
    class TimeField final : public juce::Label {
    public:
        TimeField(MidiPlayerComponent&, bool isStart);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        bool keyPressed(const juce::KeyPress&) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    private:
        MidiPlayerComponent& owner;
        bool start;
    };
    class LoopHandle final : public juce::Slider {
    public:
        LoopHandle(MidiPlayerComponent&, bool isStart);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        bool keyPressed(const juce::KeyPress&) override;
    private:
        MidiPlayerComponent& owner;
        bool start;
        int dragScreenX = 0;
        double dragSeconds = 0.0;
    };
    void timerCallback() override;
    void refreshStatus();
    void chooseFile();
    void loadFile(const juce::File&);
    static juce::String formatTime(double, bool showTenths = false);
    static juce::String formatTimeInput(double);
    static bool parseTimeInput(const juce::String&, double&);
    void commitTimeInput(bool isStart);
    void previewLoopMarker(bool isStart, double seconds);
    void commitLoopMarker(bool isStart, double seconds);
    void positionLoopHandles();

    JuicySFAudioProcessor& processor;
    juce::TextButton loadButton{"Load MIDI"}, playButton{"Play"}, stopButton{"Stop"};
    juce::TextButton resetLoopButton{"Reset"};
    juce::ToggleButton loopButton{"Loop"};
    juce::ComboBox speedBox, loopModeBox;
    juce::Slider positionSlider;
    juce::Label fileLabel, detailsLabel, guidanceLabel, timeLabel, loopRangeLabel, speedLabel, bpmLabel;
    juce::Label loopStartCaption, loopEndCaption;
    TimeField loopStartTime, loopEndTime;
    LoopHandle startHandle, endHandle;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String errorMessage;
    juce::Rectangle<int> timelineBounds, timelineTrackBounds;
    double durationSeconds = 0.0, loopStartSeconds = 0.0, loopEndSeconds = 0.0;
    int transportDividerX = 0, rangeDividerX = 0;
    bool draggingFile = false;
    bool scrubbingSlider = false;
    bool looping = false;
    bool sectionMode = false;
    bool draggingLoopMarker = false;
    juce::uint64 fileRevision = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayerComponent)
};
