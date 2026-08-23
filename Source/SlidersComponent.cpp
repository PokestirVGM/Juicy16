//
//  SlidersComponent.cpp
//  juicysfplugin - Shared Code
//
//  Created by Alex Birch on 29/06/2019.
//  Copyright © 2019 Birchlabs. All rights reserved.
//

#include "SlidersComponent.h"
#include "FluidSynthModel.h"
#include "MidiConstants.h"
#include "GuiConstants.h"
#include "Util.h"
using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

namespace {
constexpr int kChannelSliders{2};
constexpr int kMasterSliders{1};
constexpr int kGroupXMargin{8};
constexpr int kGroupXPadding{8};
constexpr int kGroupYPadding{9};
constexpr int kSliderXMargin{3};
constexpr int kLabelHeight{25};
constexpr int kSliderWidth{34};

int groupWidth(int sliders)
{
    return sliders * kSliderWidth + (sliders - 1) * kSliderXMargin + 2 * kGroupXPadding;
}
} // namespace

std::function<void()> SlidersComponent::makeSliderListener(Slider& slider, int controller) {
    return [this, controller, &slider]{
        fluidSynthModel.setControllerValue(controller, juce::roundToInt(slider.getValue()));
    };
}

SlidersComponent::~SlidersComponent()
{
}

const int SlidersComponent::getDesiredWidth() {
    return groupWidth(kChannelSliders) + groupWidth(kMasterSliders) + kGroupXMargin;
}

void SlidersComponent::resized() {
    Rectangle<int> r{getLocalBounds()};
    Rectangle<int> rChannel{r.removeFromLeft(groupWidth(kChannelSliders))};
    Rectangle<int> rMaster{r.removeFromLeft(groupWidth(kMasterSliders) + kGroupXMargin)
                               .withTrimmedLeft(kGroupXMargin)};
    channelGroup.setBounds(rChannel);
    masterGroup.setBounds(rMaster);

    rChannel.reduce(kGroupXPadding, kGroupYPadding);
    rMaster.reduce(kGroupXPadding, kGroupYPadding);
    volumeSlider.setBounds(rChannel.removeFromLeft(kSliderWidth).withTrimmedTop(kLabelHeight));
    panSlider.setBounds(rChannel.removeFromLeft(kSliderWidth + kSliderXMargin)
                            .withTrimmedTop(kLabelHeight)
                            .withTrimmedLeft(kSliderXMargin));
    outputLevelSlider.setBounds(rMaster.removeFromLeft(kSliderWidth).withTrimmedTop(kLabelHeight));
}

void SlidersComponent::acceptMidiControlEvent(int controller, int value) {
    switch(controller) {
        case static_cast<int>(VOLUME_MSB): // MIDI CC 7 Channel Volume
            volumeSlider.setValue(value, NotificationType::dontSendNotification);
            break;
        case static_cast<int>(PAN_MSB): // MIDI CC 10 Pan
            panSlider.setValue(value, NotificationType::dontSendNotification);
            break;
        default:
            break;
    }
}

SlidersComponent::SlidersComponent(
    AudioProcessorValueTreeState& state,
    FluidSynthModel& model)
: fluidSynthModel{model}
, channelGroup{"channelGroup", "Channel"}
, masterGroup{"masterGroup", "Master"}
{
    const Slider::SliderStyle style{Slider::SliderStyle::LinearVertical};

    const auto describeSlider = [](Slider& slider,
                                   const String& name,
                                   const String& help) {
        slider.setName(name);
        slider.setTitle(name);
        slider.setDescription(name);
        slider.setHelpText(help);
        slider.setTooltip(help);
        // JUCE sliders decline keyboard focus by default, which would leave
        // parameter editing mouse-only. Focused sliders handle arrow keys.
        slider.setWantsKeyboardFocus(true);
    };

    describeSlider(
        volumeSlider, "Channel volume (CC7)",
        "Volume for the selected MIDI channel. Default 100. Incoming CC7 on that "
        "channel replaces this value.");
    describeSlider(
        panSlider, "Pan (CC10)",
        "Pan for the selected MIDI channel. 64 is centre, 0 is hard left, 127 is "
        "hard right. Incoming CC10 on that channel replaces this value.");
    describeSlider(
        outputLevelSlider, "Output level",
        "Master output trim for the whole plugin, in decibels. Not a MIDI "
        "controller: nothing in a MIDI file changes it.");

    channelGroup.setName("Selected channel mixer controls");
    channelGroup.setTitle("Selected channel mixer controls");
    masterGroup.setName("Master output");
    masterGroup.setTitle("Master output");

    const auto configure = [&](Slider& slider, double minimum, double maximum,
                               double interval) {
        slider.setSliderStyle(style);
        slider.setRange(minimum, maximum, interval);
        slider.setTextBoxStyle(Slider::TextBoxBelow, true,
                               slider.getTextBoxWidth(), slider.getTextBoxHeight());
        addAndMakeVisible(slider);
    };

    configure(volumeSlider, MidiConstants::midiMinValue, MidiConstants::midiMaxValue, 1);
    volumeSlider.onDragEnd = makeSliderListener(volumeSlider, static_cast<int>(VOLUME_MSB));
    volumeSliderAttachment = make_unique<SliderAttachment>(state, "volume", volumeSlider);

    configure(panSlider, MidiConstants::midiMinValue, MidiConstants::midiMaxValue, 1);
    panSlider.onDragEnd = makeSliderListener(panSlider, static_cast<int>(PAN_MSB));
    panSliderAttachment = make_unique<SliderAttachment>(state, "pan", panSlider);

    configure(outputLevelSlider, GuiConstants::outputLevelMinDb,
              GuiConstants::outputLevelMaxDb, 0.1);
    outputLevelSlider.setTextValueSuffix(" dB");
    outputLevelSliderAttachment =
        make_unique<SliderAttachment>(state, "outputLevel", outputLevelSlider);

    const auto attachLabel = [this](Label& label, const String& text, Slider& slider) {
        label.setText(text, NotificationType::dontSendNotification);
        label.setJustificationType(Justification::centredBottom);
        label.attachToComponent(&slider, false);
        addAndMakeVisible(label);
    };
    attachLabel(volumeLabel, "Vol", volumeSlider);
    attachLabel(panLabel, "Pan", panSlider);
    attachLabel(outputLevelLabel, "Out", outputLevelSlider);

    addAndMakeVisible(channelGroup);
    addAndMakeVisible(masterGroup);
}
