#include "MainComponent.h"

//==============================================================================
// MeteringSource - wraps an AudioSource and captures peak levels
//==============================================================================
class MeteringSource : public juce::AudioSource
{
public:
    MeteringSource(juce::AudioSource* source) : innerSource(source) {}

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        innerSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void releaseResources() override
    {
        innerSource->releaseResources();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        innerSource->getNextAudioBlock(info);

        int numCh = info.buffer->getNumChannels();
        if (numCh > maxChannels)
            numCh = maxChannels;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float peak = info.buffer->getMagnitude(ch, info.startSample, info.numSamples);
            float expected;
            do {
                expected = peaks[ch].load(std::memory_order_relaxed);
            } while (peak > expected &&
                     !peaks[ch].compare_exchange_weak(expected, peak,
                                                      std::memory_order_relaxed,
                                                      std::memory_order_relaxed));
        }
        numActiveChannels.store(numCh, std::memory_order_relaxed);
    }

    float getPeakAndReset(int ch)
    {
        if (ch < 0 || ch >= maxChannels) return 0.0f;
        return peaks[ch].exchange(0.0f, std::memory_order_relaxed);
    }

    int getNumChannels() const { return numActiveChannels.load(std::memory_order_relaxed); }

private:
    static constexpr int maxChannels = 32;
    juce::AudioSource* innerSource = nullptr;
    std::atomic<float> peaks[32] = {};
    std::atomic<int> numActiveChannels{2};
};

// File-scope metering source pointer, managed by MainComponent lifetime
static std::unique_ptr<MeteringSource> s_meteringSource;

//==============================================================================
// SplitterBar - custom resizer between waveform and peak meter
//==============================================================================
class SplitterBar : public juce::Component
{
public:
    SplitterBar(MainComponent& o) : owner(o)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff555555));
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStartWidth = owner.peakMeterWidth;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        owner.peakMeterWidth = juce::jlimit(30, 300, dragStartWidth - e.getDistanceFromDragStartX());
        owner.resized();
    }

private:
    MainComponent& owner;
    int dragStartWidth = 80;
};

//==============================================================================
// MainComponent
//==============================================================================
MainComponent::MainComponent()
{
    // Audio format registration
    formatManager.registerBasicFormats();

    // Listen to transport changes
    transportSource.addChangeListener(this);

    // Volume slider
    volumeSlider.setRange(0.0, 100.0, 1.0);
    volumeSlider.setValue(100.0, juce::dontSendNotification);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff555555));
    volumeSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff888888));
    volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffcccccc));
    volumeSlider.onValueChange = [this]()
    {
        volume = (int)volumeSlider.getValue();
        transportSource.setGain((float)volume / 100.0f);
        volumeSlider.setTooltip("Volume: " + juce::String(volume));
    };
    addAndMakeVisible(volumeSlider);
    volumeSlider.setPopupDisplayEnabled(true, false, this);
    volumeSlider.setTooltip("Volume: 100");

    // FFT blend slider
    fftSlider.setRange(0.0, 1.0, 0.01);
    fftSlider.setValue(0.0, juce::dontSendNotification);
    fftSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    fftSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fftSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff555555));
    fftSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff888888));
    fftSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffcccccc));
    fftSlider.onValueChange = [this]()
    {
        waveformComponent.setFFTBlend((float)fftSlider.getValue());
    };
    addAndMakeVisible(fftSlider);
    fftSlider.setPopupDisplayEnabled(true, false, this);
    fftSlider.setTooltip(juce::String::fromUTF8("Waveform \xe2\x86\x94 Spectrum"));

    // Status labels
    statusLeftLabel.setText("0kHz/0bit/0ch", juce::dontSendNotification);
    statusLeftLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    statusLeftLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
    statusLeftLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(statusLeftLabel);

    statusRightLabel.setText("00:00:00/00:00:00", juce::dontSendNotification);
    statusRightLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    statusRightLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
    statusRightLabel.setJustificationType(juce::Justification::centredRight);
    statusRightLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(statusRightLabel);

    statusLoudnessLabel.setText("", juce::dontSendNotification);
    statusLoudnessLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    statusLoudnessLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
    statusLoudnessLabel.setJustificationType(juce::Justification::centredLeft);
    statusLoudnessLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(statusLoudnessLabel);

    // Sub-components
    addAndMakeVisible(menuBar);
    menuBar.setModel(this);

    addAndMakeVisible(waveformComponent);
    addAndMakeVisible(peakMeterComponent);
    addAndMakeVisible(timeScaleComponent);

    // Waveform callbacks
    waveformComponent.onSeek = [this](double fraction)
    {
        if (audioLoaded)
        {
            double lengthSec = transportSource.getLengthInSeconds();
            transportSource.setPosition(fraction * lengthSec);
            stoppedAtEnd = false;
        }
    };

    waveformComponent.onLoopSelected = [this](double startFrac, double endFrac)
    {
        waveformComponent.setLoopRange(startFrac, endFrac);
    };

    waveformComponent.onLoopCleared = [this]()
    {
        waveformComponent.clearLoop();
    };

    waveformComponent.onLoopExportRequested = [this]()
    {
        exportLoopToWav();
    };

    // Resizer bar between waveform and peak meter (custom SplitterBar)
    resizerBar = std::make_unique<SplitterBar>(*this);
    addAndMakeVisible(resizerBar.get());

    // Load saved settings
    loadSettings();

    // Apply loaded volume
    volumeSlider.setValue((double)volume, juce::dontSendNotification);
    transportSource.setGain((float)volume / 100.0f);

    // Start timer
    startTimer(30);

    // Set size
    setSize(400, 177);
    setWantsKeyboardFocus(true);

    // Defer audio device init to background thread so window appears in <50ms
    std::thread([this] {
        deviceManager.initialiseWithDefaultDevices(0, 2);
        juce::MessageManager::callAsync([this] {
            if (!audioDeviceInitialized)
            {
                s_meteringSource = std::make_unique<MeteringSource>(&transportSource);
                sourcePlayer.setSource(s_meteringSource.get());
                deviceManager.addAudioCallback(&sourcePlayer);
                audioDeviceInitialized = true;
            }
        });
    }).detach();
}

MainComponent::~MainComponent()
{
    saveSettings();
    stopTimer();
    transportSource.setSource(nullptr);

    if (audioDeviceInitialized)
    {
        sourcePlayer.setSource(nullptr);
        deviceManager.removeAudioCallback(&sourcePlayer);
        s_meteringSource.reset();
    }
}

//==============================================================================
void MainComponent::initAudioDevice()
{
    if (audioDeviceInitialized)
        return;

    // May be called from loadAudioFile if user loads before bg thread finishes
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        deviceManager.initialiseWithDefaultDevices(0, 2);

    s_meteringSource = std::make_unique<MeteringSource>(&transportSource);
    sourcePlayer.setSource(s_meteringSource.get());
    deviceManager.addAudioCallback(&sourcePlayer);
    audioDeviceInitialized = true;
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff333333));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    // Menu bar at top (24px) with sliders on the right side
    auto menuArea = bounds.removeFromTop(24);

    // Right side of menu bar: "Vol" label area + volumeSlider + "FFT" label area + fftSlider
    // Total right area: ~200px
    int sliderAreaWidth = 200;
    auto sliderBarArea = menuArea.removeFromRight(sliderAreaWidth);

    // Menu bar gets the rest
    menuBar.setBounds(menuArea);

    // Layout sliders inside sliderBarArea
    // "Vol" (20px) + volumeSlider (80px) + gap (4px) + "FFT" (0px) + fftSlider (80px)
    // We skip text labels to save space and rely on tooltips
    auto volArea = sliderBarArea.removeFromLeft(90);
    sliderBarArea.removeFromLeft(4); // gap
    auto fftArea = sliderBarArea.removeFromLeft(90);

    volumeSlider.setBounds(volArea.reduced(0, 3));
    fftSlider.setBounds(fftArea.reduced(0, 3));

    // Status bar at bottom (22px) - 3 sections
    auto statusArea = bounds.removeFromBottom(22);
    statusLeftLabel.setBounds(statusArea.removeFromLeft(150));
    statusLoudnessLabel.setBounds(statusArea.removeFromLeft(220));
    statusRightLabel.setBounds(statusArea);

    // Time scale
    auto timeScaleArea = bounds.removeFromBottom(30);
    timeScaleComponent.setBounds(timeScaleArea);

    // Main area: waveform | splitter | peakMeter
    auto mainArea = bounds;

    // Peak meter on the right
    auto peakArea = mainArea.removeFromRight(peakMeterWidth);
    peakMeterComponent.setBounds(peakArea);

    // Resizer bar
    auto splitterArea = mainArea.removeFromRight(splitterWidth);
    resizerBar->setBounds(splitterArea);

    // Waveform fills the rest
    waveformComponent.setBounds(mainArea);
}

//==============================================================================
// MenuBarModel
//==============================================================================
juce::String MainComponent::tr(const char* en, const char* jaUtf8) const
{
    return useJapanese ? juce::String::fromUTF8(jaUtf8) : juce::String(en);
}

juce::StringArray MainComponent::getMenuBarNames()
{
    juce::StringArray names;
    names.add(tr("File", "\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab"));
    names.add(tr("Settings", "\xe8\xa8\xad\xe5\xae\x9a"));
    names.add(tr("Help", "\xe3\x83\x98\xe3\x83\xab\xe3\x83\x97"));
    return names;
}

juce::PopupMenu MainComponent::getMenuForIndex(int menuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)
    {
        menu.addItem(idOpenFile, tr("Open", "\xe9\x96\x8b\xe3\x81\x8f"));
        menu.addSeparator();
        menu.addItem(idExit, tr("Exit", "\xe7\xb5\x82\xe4\xba\x86"));
    }
    else if (menuIndex == 1)
    {
        menu.addItem(idAutoPlay, tr("Auto Play", "\xe8\x87\xaa\xe5\x8b\x95\xe5\x86\x8d\xe7\x94\x9f"), true, autoPlay);
        menu.addItem(idAlwaysOnTop, tr("Always on Top", "\xe5\xb8\xb8\xe3\x81\xab\xe6\x9c\x80\xe5\x89\x8d\xe9\x9d\xa2"), true, alwaysOnTop);
        menu.addItem(idSingleInstance, tr("Single Instance", "\xe3\x82\xb7\xe3\x83\xb3\xe3\x82\xb0\xe3\x83\xab\xe3\x82\xa4\xe3\x83\xb3\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\xb3\xe3\x82\xb9"), true, singleInstance);
        menu.addItem(idLoadToMemory, tr("Load to Memory", "\xe9\x9f\xb3\xe5\xa3\xb0\xe3\x82\x92\xe3\x83\xa1\xe3\x83\xa2\xe3\x83\xaa\xe3\x81\xab\xe5\xb1\x95\xe9\x96\x8b"), true, loadToMemory);
        menu.addItem(idShowAllChannels, tr("Show All Channels", "\xe5\x85\xa8\xe3\x83\x81\xe3\x83\xa3\xe3\x83\x8d\xe3\x83\xab\xe8\xa1\xa8\xe7\xa4\xba"), true, showAllChannels);
        menu.addSeparator();

        juce::PopupMenu tempMenu;
        tempMenu.addItem(idClearTemp, tr("Clear All Temp Files", "\xe4\xb8\x80\xe6\x99\x82\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe3\x82\x92\xe3\x81\x99\xe3\x81\xb9\xe3\x81\xa6\xe5\x89\x8a\xe9\x99\xa4"));

        juce::PopupMenu sizeMenu;
        sizeMenu.addItem(idTempSize500MB, "500MB", true, tempMaxSize == 524288000LL);
        sizeMenu.addItem(idTempSize1GB, "1GB", true, tempMaxSize == 1073741824LL);
        sizeMenu.addItem(idTempSize2GB, "2GB", true, tempMaxSize == 2147483648LL);
        sizeMenu.addItem(idTempSize5GB, "5GB", true, tempMaxSize == 5368709120LL);

        tempMenu.addSubMenu(tr("Max Temp Size", "\xe4\xb8\x80\xe6\x99\x82\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe6\x9c\x80\xe5\xa4\xa7\xe3\x82\xb5\xe3\x82\xa4\xe3\x82\xba"), sizeMenu);
        menu.addSubMenu(tr("Temp File Management", "\xe4\xb8\x80\xe6\x99\x82\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe7\xae\xa1\xe7\x90\x86"), tempMenu);

        // Language submenu
        menu.addSeparator();
        juce::PopupMenu langMenu;
        langMenu.addItem(idLangEnglish, "English", true, !useJapanese);
        langMenu.addItem(idLangJapanese, "Japanese", true, useJapanese);
        menu.addSubMenu("Language", langMenu);
    }
    else if (menuIndex == 2)
    {
        menu.addItem(idAbout, tr("About this Player", "\xe3\x81\x93\xe3\x81\xae\xe3\x83\x97\xe3\x83\xac\xe3\x82\xa4\xe3\x83\xa4\xe3\x83\xbc\xe3\x81\xab\xe3\x81\xa4\xe3\x81\x84\xe3\x81\xa6"));
    }

    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/)
{
    switch (menuItemID)
    {
        case idOpenFile:
        {
            auto chooser = std::make_shared<juce::FileChooser>(
                tr("Open Audio File", "\xe9\x9f\xb3\xe5\xa3\xb0\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe3\x82\x92\xe9\x96\x8b\xe3\x81\x8f"),
                juce::File{},
                formatManager.getWildcardForAllFormats());

            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this, chooser](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                    loadAudioFile(result);
            });
            break;
        }

        case idExit:
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            break;

        case idAutoPlay:
            autoPlay = !autoPlay;
            break;

        case idAlwaysOnTop:
            alwaysOnTop = !alwaysOnTop;
            setAlwaysOnTopState(alwaysOnTop);
            break;

        case idSingleInstance:
            singleInstance = !singleInstance;
            break;

        case idLoadToMemory:
            loadToMemory = !loadToMemory;
            break;

        case idShowAllChannels:
            showAllChannels = !showAllChannels;
            waveformComponent.setShowAllChannels(showAllChannels);
            break;

        case idClearTemp:
            clearTempFiles();
            break;

        case idTempSize500MB:
            tempMaxSize = 524288000LL;   // 500 * 1024 * 1024
            break;
        case idTempSize1GB:
            tempMaxSize = 1073741824LL;  // 1GB
            break;
        case idTempSize2GB:
            tempMaxSize = 2147483648LL;  // 2GB
            break;
        case idTempSize5GB:
            tempMaxSize = 5368709120LL;  // 5GB
            break;

        case idAbout:
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "pbAudioPlayer",
                "pbAudioPlayer v1.0.0\nBuilt with JUCE 8");
            break;
        }

        case idLangEnglish:
            useJapanese = false;
            menuBar.setModel(nullptr);
            menuBar.setModel(this);
            menuBar.repaint();
            break;

        case idLangJapanese:
            useJapanese = true;
            menuBar.setModel(nullptr);
            menuBar.setModel(this);
            menuBar.repaint();
            break;

        default:
            break;
    }
}

//==============================================================================
// Audio file loading
//==============================================================================
void MainComponent::loadAudioFile(const juce::File& file)
{
    // Ensure audio device is initialized before loading
    if (!audioDeviceInitialized)
        initAudioDevice();

    // Stop current playback and clear loop
    stop();
    waveformComponent.clearLoop();

    // Release previous source
    transportSource.setSource(nullptr);
    readerSource.reset();
    currentReader.reset();

    auto* reader = formatManager.createReaderFor(file);
    if (reader == nullptr)
        return;

    currentFile = file;

    // Always start with disk-streaming reader (instant playback)
    currentReader.reset(reader);

    // Create reader source and connect to transport
    readerSource = std::make_unique<juce::AudioFormatReaderSource>(currentReader.get(), false);
    transportSource.setSource(readerSource.get(), 0, nullptr,
                              currentReader->sampleRate,
                              (int)currentReader->numChannels);

    // Extract audio info (lightweight, instant)
    fileSampleRate = (int)currentReader->sampleRate;
    fileNumChannels = (int)currentReader->numChannels;
    fileBitDepth = detectBitDepth(file);
    audioLoaded = true;

    peakMeterComponent.setChannelCount(fileNumChannels);
    currentPeakLevels.resize((size_t)fileNumChannels, 0.0f);

    double durationSec = transportSource.getLengthInSeconds();
    timeScaleComponent.setDuration(durationSec);
    waveformComponent.setTotalLengthSeconds(durationSec);

    // Update status labels (instant)
    juce::String srStr = juce::String(fileSampleRate / 1000.0, 1) + "kHz";
    statusLeftLabel.setText(srStr + "/" + fileBitDepth + "/" + juce::String(fileNumChannels) + "ch",
                            juce::dontSendNotification);
    statusRightLabel.setText("00:00:00/" + formatTime(durationSec), juce::dontSendNotification);
    statusLoudnessLabel.setText("...", juce::dontSendNotification);

    // Window title
    if (auto* tlw = getTopLevelComponent())
        if (auto* dw = dynamic_cast<juce::DocumentWindow*>(tlw))
            dw->setName("pbAudioPlayer - " + file.getFileName());

    // AUTO-PLAY IMMEDIATELY (before heavy processing)
    if (autoPlay)
        play();

    // === Heavy processing on background thread ===
    auto bgFile = currentFile;
    auto bgSampleRate = fileSampleRate;
    auto bgNumChannels = fileNumChannels;
    auto bgBitsPerSample = currentReader->bitsPerSample;
    bool bgLoadToMemory = loadToMemory;

    std::thread([this, bgFile, bgSampleRate, bgNumChannels, bgBitsPerSample, bgLoadToMemory]()
    {
        // Open a separate reader for background work (doesn't interfere with playback)
        juce::AudioFormatManager bgFmtMgr;
        bgFmtMgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> bgReader(bgFmtMgr.createReaderFor(bgFile));
        if (bgReader == nullptr) return;

        auto totalSamples = bgReader->lengthInSamples;

        // Build mono + multi-channel samples
        std::vector<float> bgMono;
        bgMono.reserve((size_t)totalSamples);
        std::vector<std::vector<float>> bgAllCh((size_t)bgNumChannels);
        for (int ch = 0; ch < bgNumChannels; ++ch)
            bgAllCh[(size_t)ch].reserve((size_t)totalSamples);

        juce::AudioBuffer<float> readBuf(bgNumChannels, 65536);
        juce::int64 remaining = totalSamples;
        juce::int64 pos = 0;

        while (remaining > 0)
        {
            int block = (int)juce::jmin(remaining, (juce::int64)65536);
            bgReader->read(&readBuf, 0, block, pos, true, true);

            for (int i = 0; i < block; ++i)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < bgNumChannels; ++ch)
                {
                    float s = readBuf.getSample(ch, i);
                    sum += s;
                    bgAllCh[(size_t)ch].push_back(s);
                }
                bgMono.push_back(sum / (float)bgNumChannels);
            }
            pos += block;
            remaining -= block;
        }

        bgReader.reset(); // close file handle

        // Post waveform data to UI thread
        auto monoPtr = std::make_shared<std::vector<float>>(std::move(bgMono));
        auto allChPtr = std::make_shared<std::vector<std::vector<float>>>(std::move(bgAllCh));

        juce::MessageManager::callAsync([this, monoPtr, allChPtr, bgSampleRate]()
        {
            if (!audioLoaded) return;
            monoSamples = std::move(*monoPtr);
            allChannelSamples = std::move(*allChPtr);

            waveformComponent.setSamples(monoSamples);
            waveformComponent.setAllChannelSamples(allChannelSamples);

            // Compute loudness
            computeLoudness();
            juce::String loudStr;
            loudStr += juce::String::formatted("I:%.1f", loudnessI);
            loudStr += juce::String::formatted(" M:%.1f", loudnessM);
            loudStr += juce::String::formatted(" S:%.1f", loudnessS);
            loudStr += juce::String::formatted(" LRA:%.1f", loudnessLRA);
            statusLoudnessLabel.setText(loudStr, juce::dontSendNotification);

            // Compute spectrogram
            waveformComponent.computeSpectrogram(bgSampleRate);
        });

        // If loadToMemory, also prepare memory source and swap
        if (bgLoadToMemory)
        {
            juce::AudioFormatManager bgFmtMgr2;
            bgFmtMgr2.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> bgReader2(bgFmtMgr2.createReaderFor(bgFile));
            if (bgReader2 == nullptr) return;

            juce::AudioBuffer<float> buf((int)bgReader2->numChannels, (int)bgReader2->lengthInSamples);
            bgReader2->read(&buf, 0, (int)bgReader2->lengthInSamples, 0, true, true);
            auto numSamp = bgReader2->lengthInSamples;
            bgReader2.reset();

            juce::WavAudioFormat wavFmt;
            juce::MemoryBlock memBlock;
            {
                auto memOut = std::make_unique<juce::MemoryOutputStream>(memBlock, false);
                auto* writer = wavFmt.createWriterFor(memOut.get(), (double)bgSampleRate,
                    (unsigned int)bgNumChannels, (int)bgBitsPerSample, {}, 0);
                if (writer == nullptr) return;
                memOut.release();
                writer->writeFromAudioSampleBuffer(buf, 0, (int)numSamp);
                delete writer;
            }

            auto memBlockPtr = std::make_shared<juce::MemoryBlock>(std::move(memBlock));

            juce::MessageManager::callAsync([this, memBlockPtr, bgSampleRate, bgNumChannels]()
            {
                if (!audioLoaded) return;
                double curPos = transportSource.getCurrentPosition();
                bool wasPlaying = isPlaying;

                transportSource.stop();
                transportSource.setSource(nullptr);
                readerSource.reset();
                currentReader.reset();

                juce::WavAudioFormat wavFmt2;
                auto memIn = std::make_unique<juce::MemoryInputStream>(*memBlockPtr, false);
                auto* memReader = wavFmt2.createReaderFor(memIn.release(), true);
                if (memReader == nullptr) return;

                currentReader.reset(memReader);
                readerSource = std::make_unique<juce::AudioFormatReaderSource>(currentReader.get(), false);
                transportSource.setSource(readerSource.get(), 0, nullptr, (double)bgSampleRate, bgNumChannels);
                transportSource.setPosition(curPos);

                if (wasPlaying) play();
            });
        }
    }).detach();
}

//==============================================================================
// Loudness computation (EBU R128 simplified)
//==============================================================================
void MainComponent::computeLoudness()
{
    if (monoSamples.empty() || fileSampleRate <= 0)
    {
        loudnessI = -70.0f;
        loudnessM = -70.0f;
        loudnessS = -70.0f;
        loudnessLRA = 0.0f;
        return;
    }

    const size_t totalSamples = monoSamples.size();

    // Helper lambda: get sample with looping for short files
    auto getSample = [&](size_t index) -> float
    {
        return monoSamples[index % totalSamples];
    };

    // Helper lambda: compute RMS (linear) for a range with looping
    auto computeRMS = [&](size_t start, size_t count) -> double
    {
        double sumSq = 0.0;
        for (size_t i = 0; i < count; ++i)
        {
            float s = getSample(start + i);
            sumSq += (double)s * (double)s;
        }
        return sumSq / (double)count;
    };

    // Block size for 400ms
    size_t block400ms = (size_t)((double)fileSampleRate * 0.4);
    if (block400ms < 1) block400ms = 1;

    // Block size for 3s
    size_t block3s = (size_t)((double)fileSampleRate * 3.0);
    if (block3s < 1) block3s = 1;

    // --- Momentary (M): RMS of last 400ms (use end of file) ---
    {
        size_t windowSize = block400ms;
        // If file is shorter than 400ms, loop samples to fill the window
        size_t startPos = (totalSamples >= windowSize) ? (totalSamples - windowSize) : 0;
        size_t count = (totalSamples >= windowSize) ? windowSize : windowSize; // always use full window
        // For short files, we loop
        double meanSq = 0.0;
        if (totalSamples >= windowSize)
        {
            meanSq = computeRMS(startPos, windowSize);
        }
        else
        {
            // Loop samples to fill 400ms
            double sumSq = 0.0;
            for (size_t i = 0; i < windowSize; ++i)
            {
                float s = getSample(i);
                sumSq += (double)s * (double)s;
            }
            meanSq = sumSq / (double)windowSize;
        }
        if (meanSq > 0.0)
            loudnessM = (float)(-0.691 + 10.0 * std::log10(meanSq));
        else
            loudnessM = -70.0f;
    }

    // --- Short-term (S): RMS of last 3s ---
    {
        size_t windowSize = block3s;
        double meanSq = 0.0;
        if (totalSamples >= windowSize)
        {
            meanSq = computeRMS(totalSamples - windowSize, windowSize);
        }
        else
        {
            // Loop samples to fill 3s
            double sumSq = 0.0;
            for (size_t i = 0; i < windowSize; ++i)
            {
                float s = getSample(i);
                sumSq += (double)s * (double)s;
            }
            meanSq = sumSq / (double)windowSize;
        }
        if (meanSq > 0.0)
            loudnessS = (float)(-0.691 + 10.0 * std::log10(meanSq));
        else
            loudnessS = -70.0f;
    }

    // --- Integrated (I): Gated measurement over entire file ---
    {
        // Compute RMS in 400ms blocks (non-overlapping)
        size_t effectiveSamples = totalSamples;
        // For very short files, loop to at least one block
        if (effectiveSamples < block400ms)
            effectiveSamples = block400ms;

        std::vector<double> blockMeanSq;
        size_t numBlocks = effectiveSamples / block400ms;
        if (numBlocks < 1) numBlocks = 1;

        blockMeanSq.reserve(numBlocks);
        for (size_t b = 0; b < numBlocks; ++b)
        {
            size_t start = b * block400ms;
            double sumSq = 0.0;
            for (size_t i = 0; i < block400ms; ++i)
            {
                float s = getSample(start + i);
                sumSq += (double)s * (double)s;
            }
            blockMeanSq.push_back(sumSq / (double)block400ms);
        }

        // Absolute gate at -70 LUFS
        double absGateThreshold = std::pow(10.0, (-70.0 + 0.691) / 10.0);

        // First pass: compute ungated mean (above absolute gate)
        double ungatedSum = 0.0;
        int ungatedCount = 0;
        for (auto ms : blockMeanSq)
        {
            if (ms > absGateThreshold)
            {
                ungatedSum += ms;
                ungatedCount++;
            }
        }

        if (ungatedCount == 0)
        {
            loudnessI = -70.0f;
        }
        else
        {
            double ungatedMean = ungatedSum / (double)ungatedCount;
            double ungatedLUFS = -0.691 + 10.0 * std::log10(ungatedMean);

            // Relative gate at -10 LUFS from ungated mean
            double relGateThreshold = std::pow(10.0, (ungatedLUFS - 10.0 + 0.691) / 10.0);

            // Second pass: compute gated mean (above both gates)
            double gatedSum = 0.0;
            int gatedCount = 0;
            for (auto ms : blockMeanSq)
            {
                if (ms > absGateThreshold && ms > relGateThreshold)
                {
                    gatedSum += ms;
                    gatedCount++;
                }
            }

            if (gatedCount > 0)
            {
                double gatedMean = gatedSum / (double)gatedCount;
                loudnessI = (float)(-0.691 + 10.0 * std::log10(gatedMean));
            }
            else
            {
                loudnessI = -70.0f;
            }
        }
    }

    // --- LRA: Loudness Range (10th to 95th percentile of short-term blocks) ---
    {
        // 3s blocks with 75% overlap (step = 0.75s)
        size_t stepSize = (size_t)((double)fileSampleRate * 0.75);
        if (stepSize < 1) stepSize = 1;

        size_t effectiveSamples = totalSamples;
        if (effectiveSamples < block3s)
            effectiveSamples = block3s;

        std::vector<double> stLoudness;
        size_t numBlocks = 0;
        if (effectiveSamples >= block3s)
            numBlocks = (effectiveSamples - block3s) / stepSize + 1;
        if (numBlocks < 1)
            numBlocks = 1;

        // Absolute gate threshold for LRA (-70 LUFS)
        double absGateThreshold = std::pow(10.0, (-70.0 + 0.691) / 10.0);

        // Compute short-term loudness values
        std::vector<double> stMeanSq;
        stMeanSq.reserve(numBlocks);
        for (size_t b = 0; b < numBlocks; ++b)
        {
            size_t start = b * stepSize;
            double sumSq = 0.0;
            for (size_t i = 0; i < block3s; ++i)
            {
                float s = getSample(start + i);
                sumSq += (double)s * (double)s;
            }
            double ms = sumSq / (double)block3s;
            stMeanSq.push_back(ms);
        }

        // Apply absolute gate
        std::vector<double> ungated;
        double ungatedSum = 0.0;
        for (auto ms : stMeanSq)
        {
            if (ms > absGateThreshold)
            {
                ungated.push_back(ms);
                ungatedSum += ms;
            }
        }

        if (ungated.size() < 2)
        {
            loudnessLRA = 0.0f;
        }
        else
        {
            // Relative gate at -20 LUFS from ungated mean (EBU R128 uses -20 for LRA)
            double ungatedMean = ungatedSum / (double)ungated.size();
            double ungatedLUFS = -0.691 + 10.0 * std::log10(ungatedMean);
            double relGateThreshold = std::pow(10.0, (ungatedLUFS - 20.0 + 0.691) / 10.0);

            // Apply relative gate and convert to LUFS
            std::vector<double> gatedLUFS;
            for (auto ms : ungated)
            {
                if (ms > relGateThreshold)
                {
                    double lufs = -0.691 + 10.0 * std::log10(ms);
                    gatedLUFS.push_back(lufs);
                }
            }

            if (gatedLUFS.size() < 2)
            {
                loudnessLRA = 0.0f;
            }
            else
            {
                std::sort(gatedLUFS.begin(), gatedLUFS.end());

                // 10th percentile
                size_t idx10 = (size_t)((double)gatedLUFS.size() * 0.10);
                if (idx10 >= gatedLUFS.size()) idx10 = gatedLUFS.size() - 1;

                // 95th percentile
                size_t idx95 = (size_t)((double)gatedLUFS.size() * 0.95);
                if (idx95 >= gatedLUFS.size()) idx95 = gatedLUFS.size() - 1;

                loudnessLRA = (float)(gatedLUFS[idx95] - gatedLUFS[idx10]);
                if (loudnessLRA < 0.0f)
                    loudnessLRA = 0.0f;
            }
        }
    }
}

//==============================================================================
// Playback control
//==============================================================================
void MainComponent::play()
{
    if (!audioLoaded) return;

    // If loop is active and position is outside loop range, seek to loop start
    if (waveformComponent.isLoopActive())
    {
        double totalLength = transportSource.getLengthInSeconds();
        double loopStart = waveformComponent.getLoopStartFraction() * totalLength;
        double loopEnd = waveformComponent.getLoopEndFraction() * totalLength;
        double currentPos = transportSource.getCurrentPosition();

        if (currentPos < loopStart || currentPos > loopEnd)
            transportSource.setPosition(loopStart);
    }

    transportSource.start();
    isPlaying = true;
    stoppedAtEnd = false;
}

void MainComponent::pause()
{
    transportSource.stop();
    isPlaying = false;
}

void MainComponent::stop()
{
    transportSource.stop();
    transportSource.setPosition(0.0);
    isPlaying = false;
    stoppedAtEnd = false;
}

void MainComponent::stopAtEnd()
{
    transportSource.stop();
    isPlaying = false;
    stoppedAtEnd = true;
    waveformComponent.setStoppedAtEnd(true);
}

//==============================================================================
// Timer
//==============================================================================
void MainComponent::timerCallback()
{
    if (!audioLoaded)
        return;

    double currentPos = transportSource.getCurrentPosition();
    double totalLength = transportSource.getLengthInSeconds();

    // Update position display
    updatePositionDisplay();

    // Check end of stream
    if (isPlaying && currentPos >= totalLength - 0.01)
    {
        if (waveformComponent.isLoopActive())
        {
            // Loop back
            double loopStart = waveformComponent.getLoopStartFraction() * totalLength;
            transportSource.setPosition(loopStart);
        }
        else
        {
            stopAtEnd();
        }
    }

    // Handle loop
    if (isPlaying && waveformComponent.isLoopActive())
    {
        double loopEnd = waveformComponent.getLoopEndFraction() * totalLength;
        if (currentPos >= loopEnd)
        {
            double loopStart = waveformComponent.getLoopStartFraction() * totalLength;
            transportSource.setPosition(loopStart);
        }
    }

    // Update peak levels
    updatePeakLevels();

    // Update waveform position
    double fraction = (totalLength > 0.0) ? (currentPos / totalLength) : 0.0;
    waveformComponent.setPlaybackPosition(fraction);
    waveformComponent.setCurrentTimeSeconds(currentPos);
}

//==============================================================================
void MainComponent::updatePositionDisplay()
{
    double currentPos = transportSource.getCurrentPosition();
    double totalLength = transportSource.getLengthInSeconds();

    juce::String curStr = formatTime(currentPos);
    juce::String totStr = formatTime(totalLength);
    statusRightLabel.setText(curStr + "/" + totStr, juce::dontSendNotification);
}

void MainComponent::updatePeakLevels()
{
    if (s_meteringSource == nullptr) return;

    int numCh = s_meteringSource->getNumChannels();
    if ((int)currentPeakLevels.size() != numCh)
        currentPeakLevels.resize((size_t)numCh, 0.0f);

    for (int ch = 0; ch < numCh; ++ch)
        currentPeakLevels[(size_t)ch] = s_meteringSource->getPeakAndReset(ch);

    peakMeterComponent.setPeakLevels(currentPeakLevels);
}

//==============================================================================
// Key handling
//==============================================================================
bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        if (key.getModifiers().isCtrlDown())
        {
            if (waveformComponent.isLoopActive())
            {
                // Ctrl+Space with loop: seek to loop start, do NOT play. If playing, pause first.
                if (isPlaying)
                    pause();
                double loopStart = waveformComponent.getLoopStartFraction() * transportSource.getLengthInSeconds();
                transportSource.setPosition(loopStart);
            }
            else
            {
                stop();
            }
        }
        else
        {
            if (isPlaying)
                pause();
            else
            {
                if (stoppedAtEnd)
                {
                    transportSource.setPosition(0.0);
                    stoppedAtEnd = false;
                    waveformComponent.setStoppedAtEnd(false);
                }
                play();
            }
        }
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        waveformComponent.clearLoop();
        return true;
    }

    return false;
}

//==============================================================================
// File drag and drop
//==============================================================================
bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
    {
        if (formatManager.findFormatForFileExtension(juce::File(f).getFileExtension()) != nullptr)
            return true;
    }
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    if (files.size() > 0)
    {
        juce::File f(files[0]);
        if (f.existsAsFile())
            loadAudioFile(f);
    }
}

//==============================================================================
// ChangeListener
//==============================================================================
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &transportSource)
    {
        // Transport state changed
    }
}

//==============================================================================
// Always on top
//==============================================================================
void MainComponent::setAlwaysOnTopState(bool onTop)
{
    alwaysOnTop = onTop;
    if (auto* tlw = getTopLevelComponent())
        if (auto* dw = dynamic_cast<juce::DocumentWindow*>(tlw))
            dw->setAlwaysOnTop(onTop);
}

//==============================================================================
// Time formatting
//==============================================================================
juce::String MainComponent::formatTime(double seconds) const
{
    if (seconds < 0.0) seconds = 0.0;
    int totalSec = (int)seconds;
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    return juce::String::formatted("%02d:%02d:%02d", h, m, s);
}

//==============================================================================
// Bit depth detection
//==============================================================================
juce::String MainComponent::detectBitDepth(const juce::File& file) const
{
    auto ext = file.getFileExtension().toLowerCase();

    if (ext == ".wav" || ext == ".wave")
    {
        // Read WAV fmt chunk for bit depth info
        juce::FileInputStream fis(file);
        if (fis.openedOk() && fis.getTotalLength() >= 44)
        {
            // Skip RIFF header (12 bytes)
            fis.setPosition(12);

            while (!fis.isExhausted())
            {
                char chunkId[5] = {};
                fis.read(chunkId, 4);
                auto chunkSize = (juce::uint32)fis.readInt();

                if (juce::String(chunkId, 4) == "fmt ")
                {
                    auto audioFormat = (juce::uint16)fis.readShort();
                    fis.readShort(); // numChannels
                    fis.readInt();   // sampleRate
                    fis.readInt();   // byteRate
                    fis.readShort(); // blockAlign
                    auto bitsPerSample = (juce::uint16)fis.readShort();

                    if (audioFormat == 3) // IEEE float
                        return juce::String(bitsPerSample) + "bit float";
                    else
                        return juce::String(bitsPerSample) + "bit";
                }

                fis.setPosition(fis.getPosition() + chunkSize);
            }
        }
    }
    else if (ext == ".flac")
    {
        return "FLAC";
    }
    else if (ext == ".aiff" || ext == ".aif")
    {
        if (currentReader != nullptr)
            return juce::String((int)currentReader->bitsPerSample) + "bit";
    }

    // Default
    return "16bit";
}

//==============================================================================
// Window bounds save/restore
//==============================================================================
void MainComponent::saveWindowBounds(juce::Rectangle<int> bounds)
{
    savedWindowBounds = bounds;
}

juce::Rectangle<int> MainComponent::getSavedWindowBounds() const
{
    return savedWindowBounds;
}

//==============================================================================
// Settings
//==============================================================================
juce::File MainComponent::getSettingsFile() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
               .getParentDirectory()
               .getChildFile("settings.xml");
}

void MainComponent::loadSettings()
{
    auto settingsFile = getSettingsFile();
    if (!settingsFile.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(settingsFile);
    if (xml == nullptr)
        return;

    autoPlay = xml->getBoolAttribute("AutoPlay", true);
    alwaysOnTop = xml->getBoolAttribute("AlwaysOnTop", true);
    singleInstance = xml->getBoolAttribute("SingleInstance", true);
    loadToMemory = xml->getBoolAttribute("LoadToMemory", false);
    showAllChannels = xml->getBoolAttribute("ShowAllChannels", false);
    useJapanese = xml->getBoolAttribute("UseJapanese", false);
    volume = xml->getIntAttribute("Volume", 100);
    tempMaxSize = xml->getStringAttribute("TempFolderMaxSize", "1073741824").getLargeIntValue();
    peakMeterWidth = xml->getIntAttribute("PeakMeterWidth", 80);

    // FFT blend slider
    double savedFftBlend = xml->getDoubleAttribute("FFTBlend", 0.0);
    fftSlider.setValue(savedFftBlend, juce::dontSendNotification);
    waveformComponent.setFFTBlend((float)savedFftBlend);

    int w = xml->getIntAttribute("WindowWidth", 400);
    int h = xml->getIntAttribute("WindowHeight", 177);
    int x = xml->getIntAttribute("WindowX", -1);
    int y = xml->getIntAttribute("WindowY", -1);
    setSize(w, h);
    savedWindowBounds = juce::Rectangle<int>(x, y, w, h);

    // Apply showAllChannels to waveform component
    waveformComponent.setShowAllChannels(showAllChannels);

    // Apply always-on-top after a short delay (window may not exist yet)
    juce::Timer::callAfterDelay(100, [this]()
    {
        setAlwaysOnTopState(alwaysOnTop);
    });
}

void MainComponent::saveSettings()
{
    auto xml = std::make_unique<juce::XmlElement>("Settings");

    xml->setAttribute("AutoPlay", autoPlay);
    xml->setAttribute("AlwaysOnTop", alwaysOnTop);
    xml->setAttribute("SingleInstance", singleInstance);
    xml->setAttribute("LoadToMemory", loadToMemory);
    xml->setAttribute("ShowAllChannels", showAllChannels);
    xml->setAttribute("UseJapanese", useJapanese);
    xml->setAttribute("Volume", volume);
    xml->setAttribute("TempFolderMaxSize", juce::String(tempMaxSize));
    xml->setAttribute("PeakMeterWidth", peakMeterWidth);
    xml->setAttribute("FFTBlend", fftSlider.getValue());
    xml->setAttribute("WindowX", savedWindowBounds.getX());
    xml->setAttribute("WindowY", savedWindowBounds.getY());
    xml->setAttribute("WindowWidth", savedWindowBounds.getWidth());
    xml->setAttribute("WindowHeight", savedWindowBounds.getHeight());

    xml->writeTo(getSettingsFile());
}

//==============================================================================
// Temp file / loop export
//==============================================================================
juce::File MainComponent::getTempFolder() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
               .getParentDirectory()
               .getChildFile("Split_Temp_Data");
}

void MainComponent::clearTempFiles()
{
    auto folder = getTempFolder();
    if (folder.isDirectory())
        folder.deleteRecursively();
}

void MainComponent::limitTempFolderSize(const juce::File& folder, int64_t maxBytes)
{
    if (!folder.isDirectory())
        return;

    auto files = folder.findChildFiles(juce::File::findFiles, false);

    // Sort by creation time (oldest first)
    files.sort();

    int64_t totalSize = 0;
    for (auto& f : files)
        totalSize += f.getSize();

    int idx = 0;
    while (totalSize > maxBytes && idx < files.size())
    {
        totalSize -= files[idx].getSize();
        files[idx].deleteFile();
        idx++;
    }
}

void MainComponent::exportLoopToWav()
{
    if (!audioLoaded || !waveformComponent.isLoopActive())
        return;

    auto folder = getTempFolder();
    folder.createDirectory();
    limitTempFolderSize(folder, tempMaxSize);

    double totalLength = transportSource.getLengthInSeconds();
    double loopStartSec = waveformComponent.getLoopStartFraction() * totalLength;
    double loopEndSec = waveformComponent.getLoopEndFraction() * totalLength;

    juce::int64 startSample = (juce::int64)(loopStartSec * fileSampleRate);
    juce::int64 endSample = (juce::int64)(loopEndSec * fileSampleRate);
    juce::int64 numSamples = endSample - startSample;

    if (numSamples <= 0) return;

    // Create a reader for the file
    std::unique_ptr<juce::AudioFormatReader> exportReader(formatManager.createReaderFor(currentFile));
    if (exportReader == nullptr)
        return;

    // Generate unique filename
    auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    auto outputFile = folder.getChildFile(currentFile.getFileNameWithoutExtension()
                                          + "_" + timestamp + ".wav");

    // Create WAV writer
    juce::WavAudioFormat wavFormat;
    auto outStream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());
    if (outStream == nullptr) return;

    auto* writer = wavFormat.createWriterFor(outStream.get(),
                                             exportReader->sampleRate,
                                             (unsigned int)exportReader->numChannels,
                                             (int)exportReader->bitsPerSample,
                                             {}, 0);
    if (writer == nullptr) return;
    outStream.release(); // writer takes ownership

    // Read and write
    const int blockSize = 65536;
    juce::AudioBuffer<float> buffer((int)exportReader->numChannels, blockSize);
    juce::int64 remaining = numSamples;
    juce::int64 readPos = startSample;

    while (remaining > 0)
    {
        int toRead = (int)juce::jmin(remaining, (juce::int64)blockSize);
        exportReader->read(&buffer, 0, toRead, readPos, true, true);
        writer->writeFromAudioSampleBuffer(buffer, 0, toRead);
        readPos += toRead;
        remaining -= toRead;
    }

    delete writer;

    // Start OS-level drag with the exported file
    juce::StringArray filesToDrag;
    filesToDrag.add(outputFile.getFullPathName());
    performExternalDragDropOfFiles(filesToDrag, false, this, nullptr);
}
