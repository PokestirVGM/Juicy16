//
// Created by Alex Birch on 17/09/2017.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "ChannelListComponent.h"
#include "FluidSynthModel.h"

using namespace std;

class TablesComponent : public Component
{
public:
    TablesComponent(
        AudioProcessorValueTreeState& state,
        FluidSynthModel& model
    );

    void resized() override;

private:
    ChannelListComponent channelList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TablesComponent)
};
