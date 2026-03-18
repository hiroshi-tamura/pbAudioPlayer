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

    // Menu bar at top
    menuBar.setBounds(bounds.removeFromTop(24));

    // Status bar at bottom
    auto statusArea = bounds.removeFromBottom(22);
    statusLeftLabel.setBounds(statusArea.removeFromLeft(statusArea.getWidth() / 2));
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

    // Volume slider at top-right of waveform area
    volumeSlider.setBounds(mainArea.getRight() - 100, mainArea.getY(), 100, 16);
}

//==============================================================================
// MenuBarModel
//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    juce::StringArray names;
    names.add(juce::String::fromUTF8("\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab"));
    names.add(juce::String::fromUTF8("\xe8\xa8\xad\xe5\xae\x9a"));
    names.add(juce::String::fromUTF8("\xe3\x83\x98\xe3\x83\xab\xe3\x83\x97"));
    return names;
}

juce::PopupMenu MainComponent::getMenuForIndex(int menuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)
    {
        menu.addItem(idOpenFile, juce::String::fromUTF8("\xe9\x96\x8b\xe3\x81\x8f"));
        menu.addSeparator();
        menu.addItem(idExit, juce::String::fromUTF8("\xe7\xb5\x82\xe4\xba\x86"));
    }
    else if (menuIndex == 1)
    {
        menu.addItem(idAutoPlay, juce::String::fromUTF8("\xe8\x87\xaa\xe5\x8b\x95\xe5\x86\x8d\xe7\x94\x9f"), true, autoPlay);
        menu.addItem(idAlwaysOnTop, juce::String::fromUTF8("\xe5\xb8\xb8\xe3\x81\xab\xe6\x9c\x80\xe5\x89\x8d\xe9\x9d\xa2"), true, alwaysOnTop);
        menu.addItem(idSingleInstance, juce::String::fromUTF8("\xe3\x82\xb7\xe3\x83\xb3\xe3\x82\xb0\xe3\x83\xab\xe3\x82\xa4\xe3\x83\xb3\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\xb3\xe3\x82\xb9"), true, singleInstance);
        menu.addItem(idLoadToMemory, juce::String::fromUTF8("\xe9\x9f\xb3\xe5\xa3\xb0\xe3\x82\x92\xe3\x83\xa1\xe3\x83\xa2\xe3\x83\xaa\xe3\x81\xab\xe5\xb1\x95\xe9\x96\x8b"), true, loadToMemory);
        menu.addSeparator();

        juce::PopupMenu tempMenu;
        tempMenu.addItem(idClearTemp, juce::String::fromUTF8("\xe4\xb8\x80\xe6\x99\x82\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe3\x82\x92\xe3\x81\x99\xe3\x81\xb9\xe3\x81\xa6\xe5\x89\x8a\xe9\x99\xa4"));

        juce::PopupMenu sizeMenu;
        sizeMenu.addItem(idTempSize500MB, "500MB", true, tempMaxSize == 524288000LL);
        sizeMenu.addItem(idTempSize1GB, "1GB", true, tempMaxSize == 1073741824LL);
        sizeMenu.addItem(idTempSize2GB, "2GB", true, tempMaxSize == 2147483648LL);
        sizeMenu.addItem(idTempSize5GB, "5GB", true, tempMaxSize == 5368709120LL);

        tempMenu.addSubMenu(juce::String::fromUTF8("\xe4\xb8\x80\xe6\x99\x82\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe6\x9c\x80\xe5\xa4\xa7\xe3\x82\xb5\xe3\x82\xa4\xe3\x82\xba"), sizeMenu);
        menu.addSubMenu(juce::String::fromUTF8("\xe4\xb8\x80\xe6\x99\x82\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe7\xae\xa1\xe7\x90\x86"), tempMenu);
    }
    else if (menuIndex == 2)
    {
        menu.addItem(idAbout, juce::String::fromUTF8("\xe3\x81\x93\xe3\x81\xae\xe3\x83\x97\xe3\x83\xac\xe3\x82\xa4\xe3\x83\xa4\xe3\x83\xbc\xe3\x81\xab\xe3\x81\xa4\xe3\x81\x84\xe3\x81\xa6"));
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
                juce::String::fromUTF8("\xe9\x9f\xb3\xe5\xa3\xb0\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe3\x82\x92\xe9\x96\x8b\xe3\x81\x8f"),
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

    if (loadToMemory && reader->lengthInSamples > 0)
    {
        // Read entire file into memory
        juce::AudioBuffer<float> buffer((int)reader->numChannels,
                                        (int)reader->lengthInSamples);
        reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);

        auto sampleRate = reader->sampleRate;
        auto bitsPerSample = reader->bitsPerSample;
        auto numChannels = reader->numChannels;
        auto numSamples = reader->lengthInSamples;
        delete reader;

        // Create MemoryInputStream from buffer
        juce::WavAudioFormat wavFormat;
        juce::MemoryBlock memBlock;
        {
            auto memStream = std::make_unique<juce::MemoryOutputStream>(memBlock, false);
            auto* writer = wavFormat.createWriterFor(memStream.get(), sampleRate,
                                                     (unsigned int)numChannels,
                                                     (int)bitsPerSample, {}, 0);
            if (writer != nullptr)
            {
                memStream.release(); // writer takes ownership
                writer->writeFromAudioSampleBuffer(buffer, 0, (int)numSamples);
                delete writer;
            }
        }

        auto memInput = std::make_unique<juce::MemoryInputStream>(memBlock, true);
        auto* memReader = wavFormat.createReaderFor(memInput.release(), true);
        if (memReader == nullptr)
            return;

        currentReader.reset(memReader);
    }
    else
    {
        currentReader.reset(reader);
    }

    // Create reader source and connect to transport
    readerSource = std::make_unique<juce::AudioFormatReaderSource>(currentReader.get(), false);
    transportSource.setSource(readerSource.get(), 0, nullptr,
                              currentReader->sampleRate,
                              (int)currentReader->numChannels);

    // Extract audio info
    fileSampleRate = (int)currentReader->sampleRate;
    fileNumChannels = (int)currentReader->numChannels;
    fileBitDepth = detectBitDepth(file);
    audioLoaded = true;

    // Build mono samples for waveform
    auto totalSamples = currentReader->lengthInSamples;
    juce::AudioBuffer<float> readBuffer((int)currentReader->numChannels,
                                        (int)juce::jmin(totalSamples, (juce::int64)65536));
    monoSamples.clear();
    monoSamples.reserve((size_t)totalSamples);

    juce::int64 samplesRemaining = totalSamples;
    juce::int64 startSample = 0;

    while (samplesRemaining > 0)
    {
        int blockSize = (int)juce::jmin(samplesRemaining, (juce::int64)readBuffer.getNumSamples());
        currentReader->read(&readBuffer, 0, blockSize, startSample, true, true);

        for (int i = 0; i < blockSize; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < (int)currentReader->numChannels; ++ch)
                sum += readBuffer.getSample(ch, i);
            monoSamples.push_back(sum / (float)currentReader->numChannels);
        }

        startSample += blockSize;
        samplesRemaining -= blockSize;
    }

    // Update sub-components
    waveformComponent.setSamples(monoSamples);
    peakMeterComponent.setChannelCount(fileNumChannels);
    currentPeakLevels.resize((size_t)fileNumChannels, 0.0f);

    double durationSec = transportSource.getLengthInSeconds();
    timeScaleComponent.setDuration(durationSec);
    waveformComponent.setTotalLengthSeconds(durationSec);

    // Update status labels
    juce::String srStr = juce::String(fileSampleRate / 1000.0, 1) + "kHz";
    statusLeftLabel.setText(srStr + "/" + fileBitDepth + "/" + juce::String(fileNumChannels) + "ch",
                            juce::dontSendNotification);

    juce::String totalTimeStr = formatTime(durationSec);
    statusRightLabel.setText("00:00:00/" + totalTimeStr, juce::dontSendNotification);

    // Window title
    if (auto* tlw = getTopLevelComponent())
        if (auto* dw = dynamic_cast<juce::DocumentWindow*>(tlw))
            dw->setName("pbAudioPlayer - " + file.getFileName());

    // Auto-play
    if (autoPlay)
        play();
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
    volume = xml->getIntAttribute("Volume", 100);
    tempMaxSize = xml->getStringAttribute("TempFolderMaxSize", "1073741824").getLargeIntValue();
    peakMeterWidth = xml->getIntAttribute("PeakMeterWidth", 80);

    int w = xml->getIntAttribute("WindowWidth", 400);
    int h = xml->getIntAttribute("WindowHeight", 177);
    int x = xml->getIntAttribute("WindowX", -1);
    int y = xml->getIntAttribute("WindowY", -1);
    setSize(w, h);
    savedWindowBounds = juce::Rectangle<int>(x, y, w, h);

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
    xml->setAttribute("Volume", volume);
    xml->setAttribute("TempFolderMaxSize", juce::String(tempMaxSize));
    xml->setAttribute("PeakMeterWidth", peakMeterWidth);
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
