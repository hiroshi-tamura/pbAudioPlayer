#include "WaveformComponent.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Simple in-place radix-2 FFT (forward only)
// ---------------------------------------------------------------------------
static void performFFT(float* real, float* imag, int n)
{
    // Bit-reversal permutation
    int j = 0;
    for (int i = 0; i < n - 1; ++i)
    {
        if (i < j) { std::swap(real[i], real[j]); std::swap(imag[i], imag[j]); }
        int k = n >> 1;
        while (k <= j) { j -= k; k >>= 1; }
        j += k;
    }
    // FFT butterfly
    for (int len = 2; len <= n; len <<= 1)
    {
        float angle = -6.283185307f / (float)len;
        float wReal = std::cos(angle), wImag = std::sin(angle);
        for (int i = 0; i < n; i += len)
        {
            float curReal = 1.0f, curImag = 0.0f;
            for (int jj = 0; jj < len / 2; ++jj)
            {
                float tReal = curReal * real[i + jj + len / 2] - curImag * imag[i + jj + len / 2];
                float tImag = curReal * imag[i + jj + len / 2] + curImag * real[i + jj + len / 2];
                real[i + jj + len / 2] = real[i + jj] - tReal;
                imag[i + jj + len / 2] = imag[i + jj] - tImag;
                real[i + jj] += tReal;
                imag[i + jj] += tImag;
                float newCurReal = curReal * wReal - curImag * wImag;
                curImag = curReal * wImag + curImag * wReal;
                curReal = newCurReal;
            }
        }
    }
}

// ---------------------------------------------------------------------------
WaveformComponent::WaveformComponent()
{
}

// ---------------------------------------------------------------------------
// paint
// ---------------------------------------------------------------------------
void WaveformComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    int w = bounds.getWidth();
    int h = bounds.getHeight();

    // Background
    g.fillAll(juce::Colour(0xFF444444));

    if (w <= 0 || h <= 0)
        return;

    // --- dB scale on the left ---
    if (showAllChannels && !allChannelSamples.empty())
    {
        int numCh = (int)allChannelSamples.size();
        float chH = (float)h / (float)numCh;
        for (int ch = 0; ch < numCh; ++ch)
            drawDbScale(g, (int)chH, (int)(ch * chH), ch + 1);
    }
    else
    {
        drawDbScale(g, h, 0);
    }

    // --- Waveform / Spectrogram area ---
    int wfX = dbScaleWidth;
    int wfW = w - dbScaleWidth;
    if (wfW <= 0)
        return;

    // Save current graphics state for opacity blending
    if (fftBlend <= 0.0f)
    {
        // Waveform only
        if (showAllChannels && !allChannelSamples.empty())
            drawWaveformAllChannels(g, wfX, wfW, h);
        else
            drawWaveformMono(g, wfX, wfW, h);
    }
    else if (fftBlend >= 1.0f)
    {
        // Spectrogram only
        drawSpectrogram(g, wfX, wfW, h);
    }
    else
    {
        // Blend: waveform at reduced opacity, then spectrogram on top
        g.setOpacity(1.0f - fftBlend);
        if (showAllChannels && !allChannelSamples.empty())
            drawWaveformAllChannels(g, wfX, wfW, h);
        else
            drawWaveformMono(g, wfX, wfW, h);

        g.setOpacity(fftBlend);
        drawSpectrogram(g, wfX, wfW, h);

        g.setOpacity(1.0f); // restore
    }

    // --- Overlays drawn at full opacity, within waveform area ---

    // Loop region overlay
    if (loopActive)
    {
        float loopStartX = wfX + (float)(loopStartFraction * wfW);
        float loopEndX   = wfX + (float)(loopEndFraction * wfW);

        // Semi-transparent blue overlay
        g.setColour(juce::Colour(0x460064ff));
        g.fillRect(loopStartX, 0.0f, loopEndX - loopStartX, (float)h);

        // Blue vertical lines at loop boundaries
        g.setColour(juce::Colours::blue);
        g.drawLine(loopStartX, 0.0f, loopStartX, (float)h, 2.0f);
        g.drawLine(loopEndX,   0.0f, loopEndX,   (float)h, 2.0f);
    }

    // Temporary loop drag rectangle (cyan)
    if (isDraggingLoop)
    {
        float dragLeft  = (float)juce::jmin(loopDragStartX, loopDragCurrentX);
        float dragRight = (float)juce::jmax(loopDragStartX, loopDragCurrentX);

        g.setColour(juce::Colour(0x3200ffff));
        g.fillRect(dragLeft, 0.0f, dragRight - dragLeft, (float)h);

        g.setColour(juce::Colours::cyan);
        g.drawLine(dragLeft,  0.0f, dragLeft,  (float)h, 1.0f);
        g.drawLine(dragRight, 0.0f, dragRight, (float)h, 1.0f);
    }

    // Red seek line at current playback position
    float seekX = wfX + (float)(playbackFraction * wfW);
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
        g.drawText(timeText, textX, textY, textW, textH,
                   juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------
void WaveformComponent::resized()
{
}

// ---------------------------------------------------------------------------
// drawDbScale
// ---------------------------------------------------------------------------
void WaveformComponent::drawDbScale(juce::Graphics& g, int height, int yOffset, int channelLabel)
{
    // Background
    g.setColour(juce::Colour(0xFF333333));
    g.fillRect(0, yOffset, dbScaleWidth, height);

    if (height < 20) return;

    // Channel number label at top-left
    if (channelLabel > 0)
    {
        g.setColour(juce::Colour(0xddffcc00)); // bright yellow
        g.setFont(10.0f);
        g.drawText(juce::String(channelLabel), 2, yOffset + 2, 14, 12,
                   juce::Justification::centredLeft, false);
    }

    // Clip to this channel's region
    g.saveState();
    g.reduceClipRegion(0, yOffset, dbScaleWidth, height);

    float centerY = yOffset + height * 0.5f;
    float halfH = height * 0.5f;
    const int labelH = 10;
    const int minSpacing = labelH + 3;

    struct DbMark { float db; const char* label; };
    const DbMark allMarks[] = {
        {   0.0f, "0"   },
        {  -6.0f, "-6"  },
        { -12.0f, "-12" },
        { -24.0f, "-24" },
        { -48.0f, "-48" },
        {  -3.0f, "-3"  },
        { -18.0f, "-18" },
        { -36.0f, "-36" },
    };
    const int totalMarks = 8;

    struct Drawn { float y; };
    std::vector<Drawn> drawnPositions;
    drawnPositions.push_back({ centerY });

    g.setFont(8.0f);

    for (int i = 0; i < totalMarks; ++i)
    {
        float amp = std::pow(10.0f, allMarks[i].db / 20.0f);
        float yTop = centerY - amp * halfH;

        bool fits = true;
        for (auto& d : drawnPositions)
        {
            if (std::abs(yTop - d.y) < minSpacing)
            { fits = false; break; }
        }
        if (!fits) continue;

        float yBot = centerY + amp * halfH;

        g.setColour(juce::Colour(0x60ffffff));
        g.drawHorizontalLine((int)yTop, (float)(dbScaleWidth - 6), (float)dbScaleWidth);
        g.drawHorizontalLine((int)yBot, (float)(dbScaleWidth - 6), (float)dbScaleWidth);

        g.setColour(juce::Colour(0xb0ffffff));
        g.drawText(allMarks[i].label, 0, (int)yTop - 5, dbScaleWidth - 7, labelH,
                   juce::Justification::centredRight, false);
        g.drawText(allMarks[i].label, 0, (int)yBot - 5, dbScaleWidth - 7, labelH,
                   juce::Justification::centredRight, false);

        drawnPositions.push_back({ yTop });
        drawnPositions.push_back({ yBot });
    }

    // Center: −∞
    g.setColour(juce::Colour(0x40ffffff));
    g.drawHorizontalLine((int)centerY, (float)(dbScaleWidth - 6), (float)dbScaleWidth);
    g.setColour(juce::Colour(0x80ffffff));
    g.drawText(juce::String::fromUTF8("-\xe2\x88\x9e"), 0, (int)centerY - 5, dbScaleWidth - 7, labelH,
               juce::Justification::centredRight, false);

    g.restoreState();

    // Separator line (full height, outside clip)
    g.setColour(juce::Colour(0xff555555));
    g.drawVerticalLine(dbScaleWidth - 1, (float)yOffset, (float)(yOffset + height));
}

// ---------------------------------------------------------------------------
// drawWaveformMono
// ---------------------------------------------------------------------------
void WaveformComponent::drawWaveformMono(juce::Graphics& g, int x, int w, int h)
{
    float centerY = h * 0.5f;

    // Center line (silence) - semi-transparent pink
    g.setColour(juce::Colour(0x80ff3399));
    g.drawHorizontalLine((int)centerY, (float)x, (float)(x + w));

    // -6dB reference lines (amplitude ~0.5 -> quarter height from center)
    float db6Y = h * 0.25f;
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine((int)(centerY - db6Y), (float)x, (float)(x + w));
    g.drawHorizontalLine((int)(centerY + db6Y), (float)x, (float)(x + w));

    if (samples.empty())
        return;

    int barWidth = juce::jmax(w / 1000, 1);
    int barsCount = w / barWidth;
    int numSamples = (int)samples.size();

    for (int i = 0; i < barsCount; ++i)
    {
        int startSample = (int)((int64_t)i * numSamples / barsCount);
        int endSample   = (int)((int64_t)(i + 1) * numSamples / barsCount);
        startSample = juce::jlimit(0, numSamples - 1, startSample);
        endSample   = juce::jlimit(startSample + 1, numSamples, endSample);

        float minVal = 0.0f;
        float maxVal = 0.0f;
        for (int s = startSample; s < endSample; ++s)
        {
            float v = samples[(size_t)s];
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
        }

        float barFraction = (float)i / (float)barsCount;
        float bx = (float)(x + i * barWidth);

        // Orange before playback position, Turquoise after
        if (barFraction < playbackFraction)
            g.setColour(juce::Colour(0xFFFFA500)); // Orange
        else
            g.setColour(juce::Colours::turquoise);

        float top    = centerY + maxVal * (-centerY);
        float bottom = centerY + minVal * (-centerY);
        float barH   = bottom - top;
        if (barH < 1.0f) barH = 1.0f;

        g.fillRect(bx, top, (float)barWidth, barH);
    }
}

// ---------------------------------------------------------------------------
// drawWaveformAllChannels
// ---------------------------------------------------------------------------
void WaveformComponent::drawWaveformAllChannels(juce::Graphics& g, int x, int w, int h)
{
    int numChannels = (int)allChannelSamples.size();
    if (numChannels <= 0)
        return;

    float channelHeight = (float)h / (float)numChannels;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float chTop    = ch * channelHeight;
        float chBottom = (ch + 1) * channelHeight;
        float centerY  = (chTop + chBottom) * 0.5f;
        float halfH    = channelHeight * 0.5f;

        // Separator line between channels
        if (ch > 0)
        {
            g.setColour(juce::Colours::grey);
            g.drawHorizontalLine((int)chTop, (float)x, (float)(x + w));
        }

        // Center line (silence) - semi-transparent pink
        g.setColour(juce::Colour(0x80ff3399));
        g.drawHorizontalLine((int)centerY, (float)x, (float)(x + w));

        const auto& chSamples = allChannelSamples[(size_t)ch];
        if (chSamples.empty())
            continue;

        int barWidth   = juce::jmax(w / 1000, 1);
        int barsCount  = w / barWidth;
        int numSamples = (int)chSamples.size();

        for (int i = 0; i < barsCount; ++i)
        {
            int startSample = (int)((int64_t)i * numSamples / barsCount);
            int endSample   = (int)((int64_t)(i + 1) * numSamples / barsCount);
            startSample = juce::jlimit(0, numSamples - 1, startSample);
            endSample   = juce::jlimit(startSample + 1, numSamples, endSample);

            float minVal = 0.0f;
            float maxVal = 0.0f;
            for (int s = startSample; s < endSample; ++s)
            {
                float v = chSamples[(size_t)s];
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }

            float barFraction = (float)i / (float)barsCount;
            float bx = (float)(x + i * barWidth);

            // Orange before playback position, Turquoise after
            if (barFraction < playbackFraction)
                g.setColour(juce::Colour(0xFFFFA500));
            else
                g.setColour(juce::Colours::turquoise);

            float top    = centerY + maxVal * (-halfH);
            float bottom = centerY + minVal * (-halfH);
            float barH   = bottom - top;
            if (barH < 1.0f) barH = 1.0f;

            g.fillRect(bx, top, (float)barWidth, barH);
        }
    }
}

// ---------------------------------------------------------------------------
// drawSpectrogram
// ---------------------------------------------------------------------------
void WaveformComponent::drawSpectrogram(juce::Graphics& g, int x, int w, int h)
{
    if (!spectrogramReady || spectrogramImage.isNull())
        return;

    g.drawImage(spectrogramImage, x, 0, w, h,
                0, 0, spectrogramImage.getWidth(), spectrogramImage.getHeight());
}

// ---------------------------------------------------------------------------
// computeSpectrogram
// ---------------------------------------------------------------------------
void WaveformComponent::computeSpectrogram(int sampleRate)
{
    spectrogramReady = false;

    if (samples.empty() || sampleRate <= 0)
        return;

    const int hopSize = fftSize / 4; // 512
    int numSamples = (int)samples.size();
    int numHops = (numSamples - fftSize) / hopSize + 1;
    if (numHops <= 0)
        return;

    int numBins = fftSize / 2; // 1024

    // Pre-compute Hann window
    std::vector<float> window(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[i] = 0.5f * (1.0f - std::cos(6.283185307f * (float)i / (float)(fftSize - 1)));

    // Create spectrogram image
    spectrogramImage = juce::Image(juce::Image::ARGB, numHops, numBins, true);

    std::vector<float> realBuf(fftSize);
    std::vector<float> imagBuf(fftSize);

    for (int hop = 0; hop < numHops; ++hop)
    {
        int offset = hop * hopSize;

        // Apply window and fill buffers
        for (int i = 0; i < fftSize; ++i)
        {
            int idx = offset + i;
            realBuf[i] = (idx < numSamples) ? samples[idx] * window[i] : 0.0f;
            imagBuf[i] = 0.0f;
        }

        // Perform FFT
        performFFT(realBuf.data(), imagBuf.data(), fftSize);

        // Compute magnitudes and write pixels
        for (int bin = 0; bin < numBins; ++bin)
        {
            float re = realBuf[bin];
            float im = imagBuf[bin];
            float mag = std::sqrt(re * re + im * im);

            // Convert to dB, clamp to -80..0
            float dB = (mag > 1e-10f) ? 20.0f * std::log10(mag) : -80.0f;
            if (dB < -80.0f) dB = -80.0f;
            if (dB > 0.0f)   dB = 0.0f;

            // Normalize to 0..1
            float normalized = (dB + 80.0f) / 80.0f;

            // Y axis: low freq at bottom, high at top
            int y = numBins - 1 - bin;

            spectrogramImage.setPixelAt(hop, y, spectrogramColour(normalized));
        }
    }

    spectrogramReady = true;
}

// ---------------------------------------------------------------------------
// spectrogramColour - jet colormap
// ---------------------------------------------------------------------------
juce::Colour WaveformComponent::spectrogramColour(float magnitude) const
{
    if (magnitude < 0.0f) magnitude = 0.0f;
    if (magnitude > 1.0f) magnitude = 1.0f;

    float r, g, b;
    if (magnitude < 0.25f)
    {
        r = 0.0f;
        g = 4.0f * magnitude;
        b = 1.0f;
    }
    else if (magnitude < 0.5f)
    {
        r = 0.0f;
        g = 1.0f;
        b = 1.0f - 4.0f * (magnitude - 0.25f);
    }
    else if (magnitude < 0.75f)
    {
        r = 4.0f * (magnitude - 0.5f);
        g = 1.0f;
        b = 0.0f;
    }
    else
    {
        r = 1.0f;
        g = 1.0f - 4.0f * (magnitude - 0.75f);
        b = 0.0f;
    }

    return juce::Colour::fromFloatRGBA(r, g, b, 1.0f);
}

// ---------------------------------------------------------------------------
// Mouse interaction
// ---------------------------------------------------------------------------
void WaveformComponent::mouseDown(const juce::MouseEvent& e)
{
    int w = getWidth();
    int wfW = w - dbScaleWidth;
    if (wfW <= 0) return;

    if (e.mods.isRightButtonDown())
    {
        // Right click: start loop drag
        isDraggingLoop = true;
        loopDragStartX = (double)juce::jmax(e.x, dbScaleWidth);
        loopDragCurrentX = loopDragStartX;
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
        float seekX = dbScaleWidth + (float)(playbackFraction * wfW);
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
    double fraction = juce::jlimit(0.0, 1.0, (double)(e.x - dbScaleWidth) / (double)wfW);
    if (onSeek)
        onSeek(fraction);
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& e)
{
    int w = getWidth();
    int wfW = w - dbScaleWidth;
    if (wfW <= 0) return;

    if (isDraggingLoop)
    {
        loopDragCurrentX = (double)juce::jlimit(dbScaleWidth, w, e.x);
        repaint();
        return;
    }

    if (isDraggingSeek)
    {
        double fraction = juce::jlimit(0.0, 1.0, (double)(e.x - dbScaleWidth) / (double)wfW);
        if (onSeek)
            onSeek(fraction);
    }
}

void WaveformComponent::mouseUp(const juce::MouseEvent& e)
{
    int w = getWidth();
    int wfW = w - dbScaleWidth;

    if (isDraggingLoop)
    {
        isDraggingLoop = false;

        if (wfW > 0)
        {
            double dist = std::abs(loopDragCurrentX - loopDragStartX);
            if (dist > 5.0)
            {
                double startFrac = juce::jlimit(0.0, 1.0,
                    (juce::jmin(loopDragStartX, loopDragCurrentX) - dbScaleWidth) / (double)wfW);
                double endFrac = juce::jlimit(0.0, 1.0,
                    (juce::jmax(loopDragStartX, loopDragCurrentX) - dbScaleWidth) / (double)wfW);
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

// ---------------------------------------------------------------------------
// Data setters
// ---------------------------------------------------------------------------
void WaveformComponent::setSamples(const std::vector<float>& newSamples)
{
    samples = newSamples;
    spectrogramReady = false;
    repaint();
}

void WaveformComponent::setAllChannelSamples(const std::vector<std::vector<float>>& channels)
{
    allChannelSamples = channels;
    repaint();
}

void WaveformComponent::setShowAllChannels(bool show)
{
    showAllChannels = show;
    repaint();
}

void WaveformComponent::setFFTBlend(float blend)
{
    fftBlend = juce::jlimit(0.0f, 1.0f, blend);
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
    loopEndFraction   = juce::jlimit(0.0, 1.0, endFraction);
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

// ---------------------------------------------------------------------------
// formatTime
// ---------------------------------------------------------------------------
juce::String WaveformComponent::formatTime(double seconds, bool hhmmss) const
{
    if (hhmmss)
    {
        int totalSec = (int)seconds;
        int hrs  = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;
        return juce::String::formatted("%02d:%02d:%02d", hrs, mins, secs);
    }
    else
    {
        return juce::String(seconds, 6);
    }
}
