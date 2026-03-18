#pragma once
#include <JuceHeader.h>

class TimeScaleComponent : public juce::Component
{
public:
    TimeScaleComponent();

    void paint(juce::Graphics& g) override;

    void setDuration(double seconds);

private:
    double durationSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeScaleComponent)
};
