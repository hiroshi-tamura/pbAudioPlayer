#include <JuceHeader.h>
#include "MainComponent.h"

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, MainComponent* mc)
        : DocumentWindow(name, juce::Colour(0xff333333), DocumentWindow::allButtons),
          mainComponent(mc)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(mc, true);
        setResizable(true, false);
        centreWithSize(400, 177);

        // Restore saved window bounds if available
        auto saved = mc->getSavedWindowBounds();
        if (saved.getWidth() > 0 && saved.getHeight() > 0)
            setBounds(saved);

        setVisible(true);
    }

    void closeButtonPressed() override
    {
        // Save window bounds before quitting
        if (auto* mc = getMainComponent())
            mc->saveWindowBounds(getBounds());

        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainComponent* getMainComponent() { return mainComponent; }

private:
    MainComponent* mainComponent;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class pbAudioPlayerApplication : public juce::JUCEApplication
{
public:
    pbAudioPlayerApplication() = default;

    const juce::String getApplicationName() override { return "pbAudioPlayer"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        auto* mc = new MainComponent();
        mainWindow.reset(new MainWindow("pbAudioPlayer", mc));

        // Load file from command line AFTER window is shown
        auto path = commandLine.unquoted().trim();
        if (path.isNotEmpty())
        {
            juce::File f(path);
            if (f.existsAsFile())
            {
                juce::MessageManager::callAsync([mc, f]() {
                    mc->loadAudioFile(f);
                });
            }
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        auto path = commandLine.unquoted().trim();
        if (mainWindow != nullptr)
        {
            if (path.isNotEmpty())
            {
                juce::File f(path);
                if (f.existsAsFile())
                {
                    if (auto* mc = mainWindow->getMainComponent())
                        mc->loadAudioFile(f);
                }
            }
            mainWindow->toFront(true);
        }
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(pbAudioPlayerApplication)
