#pragma once
#include <JuceHeader.h>

class PeakMeterComponent : public juce::Component
{
public:
    PeakMeterComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setChannelCount(int count);
    void setPeakLevels(const std::vector<float>& levels);
    void resetLevels();

private:
    struct DbSegment
    {
        double minDb, maxDb;
        juce::Colour colour;
    };

    static const std::array<DbSegment, 6> dbSegments;
    static const std::array<int, 7> dbMarks;

    int channelCount = 2;
    std::vector<float> peakLevels;
    std::vector<float> peakHoldLevels;
    std::vector<double> peakHoldTimes;

    double getTimeNow() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeakMeterComponent)
};
