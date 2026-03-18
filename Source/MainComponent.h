#pragma once
#include <JuceHeader.h>
#include "WaveformComponent.h"
#include "PeakMeterComponent.h"
#include "TimeScaleComponent.h"

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::Timer,
                      public juce::FileDragAndDropTarget,
                      public juce::ChangeListener,
                      public juce::DragAndDropContainer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // Timer
    void timerCallback() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // ChangeListener
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Public API
    void loadAudioFile(const juce::File& file);
    void setAlwaysOnTopState(bool onTop);

    // For SplitterBar access
    int peakMeterWidth = 80;

    // Settings access for Main.cpp
    bool isSingleInstance() const { return singleInstance; }

    // Window size save/restore
    void saveWindowBounds(juce::Rectangle<int> bounds);
    juce::Rectangle<int> getSavedWindowBounds() const;

private:
    // Audio system
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::AudioFormatReader> currentReader;
    bool audioDeviceInitialized = false;

    // Sub-components
    juce::MenuBarComponent menuBar;
    WaveformComponent waveformComponent;
    PeakMeterComponent peakMeterComponent;
    TimeScaleComponent timeScaleComponent;
    juce::Slider volumeSlider;
    juce::Label statusLeftLabel;
    juce::Label statusRightLabel;
    std::unique_ptr<juce::Component> resizerBar;

    // Audio data
    std::vector<float> monoSamples;
    int fileSampleRate = 0;
    int fileNumChannels = 0;
    juce::String fileBitDepth;
    juce::File currentFile;
    bool audioLoaded = false;

    // Playback state
    bool isPlaying = false;
    bool stoppedAtEnd = false;

    // Settings
    bool autoPlay = true;
    bool alwaysOnTop = true;
    bool singleInstance = true;
    bool loadToMemory = false;
    int volume = 100;
    int64_t tempMaxSize = 1073741824; // 1GB

    // Window bounds from settings
    juce::Rectangle<int> savedWindowBounds;

    // Peak meter data
    std::vector<float> currentPeakLevels;

    // Methods
    void play();
    void pause();
    void stop();
    void stopAtEnd();
    void initAudioDevice();

    void loadSettings();
    void saveSettings();
    juce::File getSettingsFile() const;

    void updatePositionDisplay();
    void updatePeakLevels();

    juce::String formatTime(double seconds) const;
    juce::String detectBitDepth(const juce::File& file) const;

    void exportLoopToWav();
    void clearTempFiles();
    void limitTempFolderSize(const juce::File& folder, int64_t maxBytes);
    juce::File getTempFolder() const;

    // Menu item IDs
    enum MenuIDs
    {
        idOpenFile = 1,
        idExit,
        idAutoPlay,
        idAlwaysOnTop,
        idSingleInstance,
        idLoadToMemory,
        idClearTemp,
        idTempSize500MB,
        idTempSize1GB,
        idTempSize2GB,
        idTempSize5GB,
        idAbout
    };

    static constexpr int splitterWidth = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
