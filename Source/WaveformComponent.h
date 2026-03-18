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

    void setSamples(const std::vector<float>& newSamples);
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
    std::vector<float> samples;
    double playbackFraction = 0.0;
    double totalSeconds = 0.0;
    double currentSeconds = 0.0;
    bool stoppedAtEnd = false;

    bool loopActive = false;
    double loopStartFraction = 0.0;
    double loopEndFraction = 0.0;

    bool isDraggingSeek = false;
    bool isDraggingLoop = false;
    double loopDragStartX = 0.0;
    double loopDragCurrentX = 0.0;

    bool showTimeInHHMMSS = true;

    juce::String formatTime(double seconds, bool hhmmss) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};
