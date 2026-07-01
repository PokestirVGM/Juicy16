//
// Created by Alex Birch on 17/09/2017.
//

#include "TablesComponent.h"

using namespace std;

TablesComponent::TablesComponent(
    AudioProcessorValueTreeState& valueTreeState,
    FluidSynthModel& fluidSynthModel
)
: valueTreeState{valueTreeState}
, channelList{valueTreeState, fluidSynthModel}
{
    channelList.setWantsKeyboardFocus(false);
    addAndMakeVisible(channelList);
}

void TablesComponent::resized() {
    channelList.setBounds(getLocalBounds());
}
