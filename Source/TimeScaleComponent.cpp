#include "TimeScaleComponent.h"

TimeScaleComponent::TimeScaleComponent()
{
}

void TimeScaleComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    float width = (float)bounds.getWidth();
    float height = (float)bounds.getHeight();

    g.fillAll(juce::Colour(0xff222222));

    if (durationSeconds <= 0.0)
        return;

    double pixelsPerSecond = width / durationSeconds;

    const int scales[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 1800, 3600 };
    int majorScale = scales[12];
    for (int i = 0; i < 13; ++i)
    {
        if (scales[i] * pixelsPerSecond > 50.0)
        {
            majorScale = scales[i];
            break;
        }
    }

    int minorScale = majorScale / 5;
    float majorTickHeight = height * 0.3f;
    float minorTickHeight = majorTickHeight * 0.5f;

    g.setFont(juce::Font(10.0f));

    // Draw minor ticks
    if (minorScale > 0 && minorScale * pixelsPerSecond > 5.0)
    {
        g.setColour(juce::Colour(255, 255, 255).withAlpha((juce::uint8)128));
        for (double t = 0.0; t <= durationSeconds; t += minorScale)
        {
            float x = (float)(t * pixelsPerSecond);
            if (x > width)
                break;
            g.drawLine(x, 0.0f, x, minorTickHeight, 0.5f);
        }
    }

    // Draw major ticks and labels
    g.setColour(juce::Colours::white);
    for (double t = 0.0; t <= durationSeconds; t += majorScale)
    {
        float x = (float)(t * pixelsPerSecond);
        if (x > width)
            break;

        g.drawVerticalLine((int)x, 0.0f, majorTickHeight);

        int totalSeconds = (int)t;
        juce::String label;
        if (totalSeconds >= 3600)
        {
            int h = totalSeconds / 3600;
            int m = (totalSeconds % 3600) / 60;
            int s = totalSeconds % 60;
            label = juce::String(h) + ":"
                  + juce::String(m).paddedLeft('0', 2) + ":"
                  + juce::String(s).paddedLeft('0', 2);
        }
        else
        {
            int m = totalSeconds / 60;
            int s = totalSeconds % 60;
            label = juce::String(m).paddedLeft('0', 2) + ":"
                  + juce::String(s).paddedLeft('0', 2);
        }

        g.drawText(label, (int)x + 2, (int)majorTickHeight, 60, (int)(height - majorTickHeight),
                    juce::Justification::centredLeft, false);
    }
}

void TimeScaleComponent::setDuration(double seconds)
{
    durationSeconds = seconds;
    repaint();
}
