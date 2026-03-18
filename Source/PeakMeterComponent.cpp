#include "PeakMeterComponent.h"

const std::array<PeakMeterComponent::DbSegment, 6> PeakMeterComponent::dbSegments = {{
    { -60, -48, juce::Colour(0x00, 0x22, 0x11) },
    { -48, -24, juce::Colour(0x00, 0x66, 0x22) },
    { -24, -12, juce::Colour(0x00, 0xcc, 0x44) },
    { -12,  -6, juce::Colour(0xff, 0xcc, 0x00) },
    {  -6,  -3, juce::Colour(0xff, 0x66, 0x00) },
    {  -3,   0, juce::Colour(0xff, 0x00, 0x40) }
}};

const std::array<int, 7> PeakMeterComponent::dbMarks = {{ 0, -3, -6, -12, -24, -48, -60 }};

PeakMeterComponent::PeakMeterComponent()
{
    peakLevels.resize(channelCount, 0.0f);
    peakHoldLevels.resize(channelCount, 0.0f);
    peakHoldTimes.resize(channelCount, 0.0);
}

void PeakMeterComponent::resized()
{
}

void PeakMeterComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0x22, 0x22, 0x22));

    if (channelCount <= 0)
        return;

    float channelWidth = bounds.getWidth() / (float)channelCount;
    float canvasHeight = bounds.getHeight();

    auto dbToNormalized = [](float db) -> float
    {
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    auto linearToDb = [](float linear) -> float
    {
        if (linear <= 0.0f)
            return -60.0f;
        float db = 20.0f * std::log10(linear);
        return juce::jmax(db, -60.0f);
    };

    // Draw each channel
    for (int ch = 0; ch < channelCount; ++ch)
    {
        float x = (float)ch * channelWidth;

        // 1. Draw dB segment rectangles (full height of each segment), opacity 0.8
        for (auto& seg : dbSegments)
        {
            float normBottom = dbToNormalized((float)seg.minDb);
            float normTop = dbToNormalized((float)seg.maxDb);

            float yTop = canvasHeight * (1.0f - normTop);
            float yBottom = canvasHeight * (1.0f - normBottom);
            float segHeight = yBottom - yTop;

            g.setColour(seg.colour.withAlpha(0.8f));
            g.fillRect(x, yTop, channelWidth, segHeight);
        }

        // 2. Draw semi-transparent black mask from top down to cover above peak level
        float level = (ch < (int)peakLevels.size()) ? peakLevels[(size_t)ch] : 0.0f;
        float db = linearToDb(level);
        float normalizedLevel = dbToNormalized(db);
        float maskHeight = (1.0f - normalizedLevel) * canvasHeight;

        g.setColour(juce::Colour(0, 0, 0).withAlpha((juce::uint8)180));
        g.fillRect(x, 0.0f, channelWidth, maskHeight);

        // 3. Draw peak hold bar (yellow, 2px)
        float holdLevel = (ch < (int)peakHoldLevels.size()) ? peakHoldLevels[(size_t)ch] : 0.0f;
        float holdDb = linearToDb(holdLevel);
        float holdNorm = dbToNormalized(holdDb);
        float holdY = canvasHeight * (1.0f - holdNorm);

        g.setColour(juce::Colours::yellow);
        g.fillRect(x, holdY - 1.0f, channelWidth, 2.0f);

        // 4. Draw dB value text at bottom of each channel bar
        juce::String dbText;
        if (db <= -60.0f)
            dbText = juce::String::fromUTF8("-\xe2\x88\x9e");
        else
            dbText = juce::String(db, 1);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f).boldened());
        g.drawText(dbText, juce::Rectangle<float>(x, canvasHeight - 18.0f, channelWidth, 16.0f),
                   juce::Justification::centred, false);
    }

    // Draw dB scale lines
    for (auto mark : dbMarks)
    {
        float norm = dbToNormalized((float)mark);
        float y = canvasHeight * (1.0f - norm);

        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawHorizontalLine((int)y, bounds.getX(), bounds.getRight());

        if (bounds.getWidth() > 40.0f)
        {
            juce::String label = juce::String(mark);
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.setFont(juce::Font(9.0f));
            g.drawText(label, juce::Rectangle<float>(bounds.getRight() - 28.0f, y - 6.0f, 26.0f, 12.0f),
                       juce::Justification::centredRight, false);
        }
    }

    // Draw channel divider lines
    for (int ch = 1; ch < channelCount; ++ch)
    {
        float x = (float)ch * channelWidth;
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawVerticalLine((int)x, bounds.getY(), bounds.getBottom());
    }
}

void PeakMeterComponent::setPeakLevels(const std::vector<float>& levels)
{
    double now = getTimeNow();

    for (int ch = 0; ch < channelCount && ch < (int)levels.size(); ++ch)
    {
        peakLevels[(size_t)ch] = levels[(size_t)ch];

        if (levels[(size_t)ch] > peakHoldLevels[(size_t)ch])
        {
            peakHoldLevels[(size_t)ch] = levels[(size_t)ch];
            peakHoldTimes[(size_t)ch] = now;
        }
        else if (now - peakHoldTimes[(size_t)ch] > 1.0)
        {
            peakHoldLevels[(size_t)ch] -= 0.01f;
            if (peakHoldLevels[(size_t)ch] < 0.0f)
                peakHoldLevels[(size_t)ch] = 0.0f;
        }
    }

    repaint();
}

void PeakMeterComponent::setChannelCount(int count)
{
    channelCount = count;
    peakLevels.assign((size_t)count, 0.0f);
    peakHoldLevels.assign((size_t)count, 0.0f);
    peakHoldTimes.assign((size_t)count, 0.0);
    repaint();
}

void PeakMeterComponent::resetLevels()
{
    std::fill(peakLevels.begin(), peakLevels.end(), 0.0f);
    std::fill(peakHoldLevels.begin(), peakHoldLevels.end(), 0.0f);
    std::fill(peakHoldTimes.begin(), peakHoldTimes.end(), 0.0);
    repaint();
}

double PeakMeterComponent::getTimeNow() const
{
    return juce::Time::getMillisecondCounterHiRes() / 1000.0;
}
