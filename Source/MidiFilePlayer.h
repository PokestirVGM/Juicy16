#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <functional>
#include <array>
#include <memory>
#include <vector>

// Transport is runtime-only. It never adds host parameters or changes saved state.
// The standalone wrapper holds the processor callback lock during rendering;
// UI operations use the same lock only to publish prepared data or apply setup.
class MidiFilePlayer final {
public:
    struct Status {
        juce::String fileName;
        double positionSeconds{}, durationSeconds{}, speed{1.0};
        double tempoBpm{120.0}, playbackTempoBpm{120.0};
        double loopStartSeconds{}, loopEndSeconds{};
        bool playing{}, looping{};
        int trackCount{}, channelCount{};
        juce::uint64 revision{};
    };

    MidiFilePlayer(juce::AudioProcessor&, std::function<void(juce::MidiBuffer&)> applySetup);
    static bool validateFile(const juce::File&, juce::String& error);
    bool loadFile(const juce::File&, juce::String& error);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void setSpeed(double factor);
    void setLooping(bool);
    bool setLoopRange(double start, double end);
    Status getStatus() const;
    std::array<float, 256> getNoteDensity() const;

    // Audio callback only, with the processor callback lock already held.
    void appendNextBlock(juce::MidiBuffer&, int numSamples, double sampleRate);
    void prepare(int maximumBlockSize, double sampleRate);
    juce::MidiBuffer& getRenderBuffer() { return renderBuffer; }
    bool hasSong() const { return song != nullptr; }
    double getPreparedSampleRate() const { return preparedSampleRate; }

private:
    struct Event { juce::MidiMessage message; double seconds{}; juce::uint64 tick{}; };
    struct TempoPoint { double seconds{}, bpm{120.0}; };
    struct Song {
        juce::String fileName;
        std::vector<Event> events;
        std::vector<TempoPoint> tempoMap;
        double duration{};
        int tracks{}, channels{};
        size_t bufferBytes{};
        std::array<float, 256> noteDensity{};
    };
    static std::shared_ptr<Song> readSong(const juce::File&, juce::String& error);
    static juce::MidiBuffer makeSetup(const Song&, double position, bool restoreNotes);
    static void addPanic(juce::MidiBuffer&, int sample);
    void moveTo(double position, bool startPlaying);
    void locateCursor();
    void reserveBuffers();

    juce::AudioProcessor& processor;
    std::function<void(juce::MidiBuffer&)> applySetup;
    std::shared_ptr<const Song> song;
    Status status;
    size_t cursor{};
    juce::MidiBuffer loopSetup, renderBuffer;
    int maximumBlockSize{1024};
    double preparedSampleRate{48000.0};
    bool pendingEndPanic{};
    bool pendingLoopSetup{};
};
