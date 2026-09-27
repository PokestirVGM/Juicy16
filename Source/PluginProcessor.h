#pragma once

#if JUCE_MAC || JUCE_IOS
  #include <Foundation/NSString.h>
  #include <Foundation/NSArray.h>
#endif
#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"
#include "VST3Multitimbral.h"
#include <list>

using namespace std;

//==============================================================================
class JuicySFAudioProcessor  : public AudioProcessor
{
public:
    //==============================================================================
    JuicySFAudioProcessor();
    ~JuicySFAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override;

    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool supportsDoublePrecisionProcessing() const override;

    // VST3 per-channel units and program list; inert elsewhere.
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &vst3Extensions; }

    FluidSynthModel& getFluidSynthModel();

    MidiKeyboardState keyboardState;

private:
    static constexpr int currentStateVersion{10};
    void initialiseSynth();

    AudioProcessorValueTreeState valueTreeState;

    FluidSynthModel fluidSynthModel;
    JuicyVST3Extensions vst3Extensions;

    AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static BusesProperties getBusesProperties();

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessor)
};
