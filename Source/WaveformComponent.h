#pragma once
#include <JuceHeader.h>

class WaveformComponent : public juce::Component
{
public:
    WaveformComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Mono samples (for single-channel or mixdown display)
    void setSamples(const std::vector<float>& newSamples);

    // Multi-channel samples (for all-channel display)
    void setAllChannelSamples(const std::vector<std::vector<float>>& channels);
    void setShowAllChannels(bool show);

    // Spectrogram
    void setFFTBlend(float blend); // 0.0 = waveform only, 1.0 = spectrogram only
    void computeSpectrogram(int sampleRate); // call after setSamples

    void setPlaybackPosition(double fraction);
    void setTotalLengthSeconds(double seconds);
    void setCurrentTimeSeconds(double seconds);
    void setStoppedAtEnd(bool stopped);

    // Loop
    void setLoopRange(double startFraction, double endFraction);
    void clearLoop();
    bool isLoopActive() const { return loopActive; }
    double getLoopStartFraction() const { return loopStartFraction; }
    double getLoopEndFraction() const { return loopEndFraction; }

    // Time display
    void toggleTimeFormat();
    bool isShowingHHMMSS() const { return showTimeInHHMMSS; }

    // Callbacks
    std::function<void(double)> onSeek;
    std::function<void(double, double)> onLoopSelected;
    std::function<void()> onLoopCleared;
    std::function<void()> onLoopExportRequested;
    std::function<void()> onTimeFormatToggled;

private:
    // Waveform data
    std::vector<float> samples; // mono mixdown
    std::vector<std::vector<float>> allChannelSamples;
    bool showAllChannels = false;

    // Spectrogram
    float fftBlend = 0.0f;
    juce::Image spectrogramImage;
    bool spectrogramReady = false;
    static constexpr int fftOrder = 11; // 2048 samples
    static constexpr int fftSize = 1 << fftOrder;

    // Playback state
    double playbackFraction = 0.0;
    double totalSeconds = 0.0;
    double currentSeconds = 0.0;
    bool stoppedAtEnd = false;

    // Loop state
    bool loopActive = false;
    double loopStartFraction = 0.0;
    double loopEndFraction = 0.0;

    // Drag state
    bool isDraggingSeek = false;
    bool isDraggingLoop = false;
    double loopDragStartX = 0.0;
    double loopDragCurrentX = 0.0;

    bool showTimeInHHMMSS = true;

    // dB scale width
    static constexpr int dbScaleWidth = 30;

    // Drawing helpers
    void drawDbScale(juce::Graphics& g, int height, int yOffset = 0);
    void drawWaveformMono(juce::Graphics& g, int x, int w, int h);
    void drawWaveformAllChannels(juce::Graphics& g, int x, int w, int h);
    void drawSpectrogram(juce::Graphics& g, int x, int w, int h);
    juce::Colour spectrogramColour(float magnitude) const; // jet colormap

    juce::String formatTime(double seconds, bool hhmmss) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};
