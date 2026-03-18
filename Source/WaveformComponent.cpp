#include "WaveformComponent.h"

WaveformComponent::WaveformComponent()
{
}

void WaveformComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    int w = bounds.getWidth();
    int h = bounds.getHeight();

    // Background
    g.fillAll(juce::Colour(0xFF444444));

    if (w <= 0 || h <= 0)
        return;

    float centerY = h * 0.5f;

    // Center line (0dB) - semi-transparent pink
    g.setColour(juce::Colour(0x80ff3399));
    g.drawHorizontalLine((int)centerY, 0.0f, (float)w);

    // -6dB reference lines (amplitude ~0.5)
    float db6Y = h * 0.25f;
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine((int)(centerY - db6Y), 0.0f, (float)w);
    g.drawHorizontalLine((int)(centerY + db6Y), 0.0f, (float)w);

    // Draw waveform
    if (!samples.empty())
    {
        int barWidth = juce::jmax(w / 1000, 1);
        int barsCount = w / barWidth;

        int numSamples = (int)samples.size();

        for (int i = 0; i < barsCount; ++i)
        {
            int startSample = (int)((int64_t)i * numSamples / barsCount);
            int endSample = (int)((int64_t)(i + 1) * numSamples / barsCount);
            startSample = juce::jlimit(0, numSamples - 1, startSample);
            endSample = juce::jlimit(startSample + 1, numSamples, endSample);

            float minVal = 0.0f;
            float maxVal = 0.0f;
            for (int s = startSample; s < endSample; ++s)
            {
                float v = samples[(size_t)s];
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }

            float barFraction = (float)i / (float)barsCount;
            float x = (float)(i * barWidth);

            // Orange before playback position, Turquoise after
            if (barFraction < playbackFraction)
                g.setColour(juce::Colour(0xFFFFA500)); // Orange
            else
                g.setColour(juce::Colours::turquoise);

            float top = centerY + maxVal * (-centerY);
            float bottom = centerY + minVal * (-centerY);
            float barH = bottom - top;
            if (barH < 1.0f) barH = 1.0f;

            g.fillRect(x, top, (float)barWidth, barH);
        }
    }

    // Loop region overlay
    if (loopActive)
    {
        float loopStartX = (float)(loopStartFraction * w);
        float loopEndX = (float)(loopEndFraction * w);

        // Semi-transparent blue overlay
        g.setColour(juce::Colour(0x460064ff));
        g.fillRect(loopStartX, 0.0f, loopEndX - loopStartX, (float)h);

        // Blue vertical lines at loop boundaries
        g.setColour(juce::Colours::blue);
        g.drawLine(loopStartX, 0.0f, loopStartX, (float)h, 2.0f);
        g.drawLine(loopEndX, 0.0f, loopEndX, (float)h, 2.0f);
    }

    // Temporary loop drag rectangle (cyan)
    if (isDraggingLoop)
    {
        float dragLeft = (float)juce::jmin(loopDragStartX, loopDragCurrentX);
        float dragRight = (float)juce::jmax(loopDragStartX, loopDragCurrentX);

        g.setColour(juce::Colour(0x3200ffff));
        g.fillRect(dragLeft, 0.0f, dragRight - dragLeft, (float)h);

        g.setColour(juce::Colours::cyan);
        g.drawLine(dragLeft, 0.0f, dragLeft, (float)h, 1.0f);
        g.drawLine(dragRight, 0.0f, dragRight, (float)h, 1.0f);
    }

    // Red seek line at current playback position
    float seekX = (float)(playbackFraction * w);
    g.setColour(juce::Colours::red);
    g.drawLine(seekX, 0.0f, seekX, (float)h, 2.0f);

    // Current time text
    {
        juce::String timeText = formatTime(currentSeconds, showTimeInHHMMSS);

        g.setFont(12.0f);
        int textW = g.getCurrentFont().getStringWidth(timeText) + 8;
        int textH = 18;
        int textY = h - textH - 4;

        // Position near seek line, but keep within bounds
        int textX = (int)seekX + 4;
        if (textX + textW > w)
            textX = (int)seekX - textW - 4;

        // Black background
        g.setColour(juce::Colours::black);
        g.fillRect(textX, textY, textW, textH);

        // Yellow text
        g.setColour(juce::Colours::yellow);
        g.drawText(timeText, textX, textY, textW, textH, juce::Justification::centred, false);
    }
}

void WaveformComponent::resized()
{
}

void WaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    int w = getWidth();
    if (w <= 0) return;

    if (e.mods.isRightButtonDown())
    {
        // Right click: start loop drag
        isDraggingLoop = true;
        loopDragStartX = (double)e.x;
        loopDragCurrentX = (double)e.x;
        repaint();
        return;
    }

    // Left click
    if (e.mods.isCtrlDown() && loopActive)
    {
        // Ctrl + Left click with active loop: export
        if (onLoopExportRequested)
            onLoopExportRequested();
        return;
    }

    // Check if clicking on time text area to toggle format
    {
        float seekX = (float)(playbackFraction * w);
        int h = getHeight();
        int textH = 18;
        int textY = h - textH - 4;

        juce::String timeText = formatTime(currentSeconds, showTimeInHHMMSS);
        juce::Font font(12.0f);
        int textW = font.getStringWidth(timeText) + 8;
        int textX = (int)seekX + 4;
        if (textX + textW > w)
            textX = (int)seekX - textW - 4;

        juce::Rectangle<int> timeRect(textX, textY, textW, textH);
        if (timeRect.contains(e.x, e.y))
        {
            toggleTimeFormat();
            if (onTimeFormatToggled)
                onTimeFormatToggled();
            repaint();
            return;
        }
    }

    // Normal left click: seek
    isDraggingSeek = true;
    double fraction = juce::jlimit(0.0, 1.0, (double)e.x / (double)w);
    if (onSeek)
        onSeek(fraction);
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& e)
{
    int w = getWidth();
    if (w <= 0) return;

    if (isDraggingLoop)
    {
        loopDragCurrentX = (double)juce::jlimit(0, w, e.x);
        repaint();
        return;
    }

    if (isDraggingSeek)
    {
        double fraction = juce::jlimit(0.0, 1.0, (double)e.x / (double)w);
        if (onSeek)
            onSeek(fraction);
    }
}

void WaveformComponent::mouseUp(const juce::MouseEvent& e)
{
    int w = getWidth();

    if (isDraggingLoop)
    {
        isDraggingLoop = false;

        if (w > 0)
        {
            double dist = std::abs(loopDragCurrentX - loopDragStartX);
            if (dist > 5.0)
            {
                double startFrac = juce::jlimit(0.0, 1.0, juce::jmin(loopDragStartX, loopDragCurrentX) / (double)w);
                double endFrac = juce::jlimit(0.0, 1.0, juce::jmax(loopDragStartX, loopDragCurrentX) / (double)w);
                if (onLoopSelected)
                    onLoopSelected(startFrac, endFrac);
            }
            else
            {
                clearLoop();
                if (onLoopCleared)
                    onLoopCleared();
            }
        }

        repaint();
        return;
    }

    isDraggingSeek = false;
}

void WaveformComponent::setSamples(const std::vector<float>& newSamples)
{
    samples = newSamples;
    repaint();
}

void WaveformComponent::setPlaybackPosition(double fraction)
{
    playbackFraction = juce::jlimit(0.0, 1.0, fraction);
    repaint();
}

void WaveformComponent::setTotalLengthSeconds(double seconds)
{
    totalSeconds = seconds;
}

void WaveformComponent::setCurrentTimeSeconds(double seconds)
{
    currentSeconds = seconds;
}

void WaveformComponent::setStoppedAtEnd(bool stopped)
{
    stoppedAtEnd = stopped;
}

void WaveformComponent::setLoopRange(double startFraction, double endFraction)
{
    loopStartFraction = juce::jlimit(0.0, 1.0, startFraction);
    loopEndFraction = juce::jlimit(0.0, 1.0, endFraction);
    loopActive = true;
    repaint();
}

void WaveformComponent::clearLoop()
{
    loopActive = false;
    loopStartFraction = 0.0;
    loopEndFraction = 0.0;
    repaint();
}

void WaveformComponent::toggleTimeFormat()
{
    showTimeInHHMMSS = !showTimeInHHMMSS;
    repaint();
}

juce::String WaveformComponent::formatTime(double seconds, bool hhmmss) const
{
    if (hhmmss)
    {
        int totalSec = (int)seconds;
        int hrs = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;
        return juce::String::formatted("%02d:%02d:%02d", hrs, mins, secs);
    }
    else
    {
        return juce::String(seconds, 6);
    }
}
