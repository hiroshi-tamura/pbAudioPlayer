#pragma once
#include <JuceHeader.h>

//==============================================================================
// PluginEditorWindow - floating window that hosts a plugin's editor GUI
//==============================================================================
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& name,
                       juce::AudioProcessorEditor* editor,
                       std::function<void()> onCloseCallback)
        : juce::DocumentWindow(name,
                               juce::Colour(0xff2a2a2a),
                               juce::DocumentWindow::closeButton),
          closeCallback(std::move(onCloseCallback))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
    }

private:
    std::function<void()> closeCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

//==============================================================================
// PluginSlot - one slot in the chain
//==============================================================================
class PluginSlot
{
public:
    PluginSlot() = default;
    ~PluginSlot() { closeEditorWindow(); }

    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
    juce::PluginDescription pluginDescription;
    bool bypassed = false;
    std::unique_ptr<PluginEditorWindow> editorWindow;

    bool hasPlugin() const { return pluginInstance != nullptr; }

    juce::String getPluginName() const
    {
        return pluginInstance ? pluginInstance->getName() : juce::String();
    }

    void closeEditorWindow() { editorWindow.reset(); }

    void removePlugin()
    {
        closeEditorWindow();
        pluginInstance.reset();
        pluginDescription = juce::PluginDescription();
        bypassed = false;
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        if (pluginInstance)
        {
            pluginInstance->setPlayConfigDetails(2, 2, sampleRate, samplesPerBlock);
            pluginInstance->prepareToPlay(sampleRate, samplesPerBlock);
        }
    }

    void releaseResources()
    {
        if (pluginInstance)
            pluginInstance->releaseResources();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        if (pluginInstance && !bypassed)
            pluginInstance->processBlock(buffer, midi);
    }

    std::unique_ptr<juce::XmlElement> saveState() const
    {
        auto slotXml = std::make_unique<juce::XmlElement>("PluginSlot");
        slotXml->setAttribute("bypassed", bypassed);

        if (pluginInstance)
        {
            if (auto descElement = pluginDescription.createXml())
                slotXml->addChildElement(descElement.release());

            juce::MemoryBlock stateData;
            pluginInstance->getStateInformation(stateData);
            auto stateXml = std::make_unique<juce::XmlElement>("PluginState");
            stateXml->setAttribute("data", stateData.toBase64Encoding());
            slotXml->addChildElement(stateXml.release());
        }

        return slotXml;
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSlot)
};

//==============================================================================
// PluginSlotComponent
//==============================================================================
class PluginChainComponent;

class PluginSlotComponent : public juce::Component
{
public:
    PluginSlotComponent(PluginChainComponent& owner, int slotIndex, bool isPre);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void updateFromSlot();

    int slotIndex = 0;
    bool isPreSlot = true;

private:
    PluginChainComponent& ownerChain;
    juce::TextButton bypassButton;
    juce::TextButton removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSlotComponent)
};

//==============================================================================
// PluginChainComponent - Pre/Post plugin chain panel
//==============================================================================
class PluginChainComponent : public juce::Component
{
public:
    PluginChainComponent(juce::KnownPluginList& knownPlugins,
                         juce::AudioPluginFormatManager& formatManager,
                         bool useJapanese);
    ~PluginChainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Audio processing (separate Pre/Post)
    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processPreBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void processPostBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void releaseResources();

    // Expand / collapse
    bool isExpanded() const { return expanded; }
    void setExpanded(bool shouldExpand);

    // Slot management (isPre distinguishes Pre vs Post)
    void addEmptySlot(bool isPre);
    void removeSlot(int index, bool isPre);
    void toggleBypass(int index, bool isPre);
    void showPluginMenu(int slotIndex, bool isPre);
    void openPluginEditor(int slotIndex, bool isPre);
    void loadPluginIntoSlot(int slotIndex, bool isPre, const juce::PluginDescription& desc);

    // Access slots
    PluginSlot* getSlot(int index, bool isPre) const;

    // State persistence
    std::unique_ptr<juce::XmlElement> saveState() const;
    void loadState(const juce::XmlElement& xml);

    // Preset management (single file)
    void savePresetAs();
    void overwritePreset();
    void deletePreset();
    void loadPresetByIndex(int index);

    // Callback when chain changes
    std::function<void()> onChainChanged;

    // Localization helper
    juce::String tr(const char* en, const char* ja) const
    {
        return useJapanese ? juce::String::fromUTF8(ja) : juce::String(en);
    }

    static constexpr int slotHeight = 28;

private:
    juce::KnownPluginList& knownPluginList;
    juce::AudioPluginFormatManager& pluginFormatManager;
    bool useJapanese = false;
    bool expanded = false;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Pre/Post slots
    juce::OwnedArray<PluginSlot> preSlots;
    juce::OwnedArray<PluginSlot> postSlots;
    juce::OwnedArray<PluginSlotComponent> preSlotComponents;
    juce::OwnedArray<PluginSlotComponent> postSlotComponents;

    // Scroll support
    juce::Viewport viewport;

    // Custom container that draws a separator line between Pre/Post
    struct SlotContainerComponent : public juce::Component
    {
        int separatorY = 0;
        void paint(juce::Graphics& g) override
        {
            if (separatorY > 0)
            {
                g.setColour(juce::Colour(0xff666666));
                g.drawHorizontalLine(separatorY, 8.0f, (float)getWidth() - 8.0f);
            }
        }
    };
    SlotContainerComponent slotContainer;
    int separatorY = 0;

    // Section labels and add buttons (inside slotContainer)
    juce::Label preSectionLabel;
    juce::Label postSectionLabel;
    juce::TextButton addPreSlotButton;
    juce::TextButton addPostSlotButton;

    // Preset UI
    juce::ComboBox presetCombo;
    juce::TextButton presetOverwriteBtn;
    juce::TextButton presetSaveAsBtn;
    juce::TextButton presetDeleteBtn;

    // Single-file preset storage
    struct PresetEntry
    {
        juce::String name;
        std::unique_ptr<juce::XmlElement> xml;
    };
    std::vector<PresetEntry> presetEntries;
    int currentPresetIndex = -1;

    juce::File getPresetFile() const;
    void loadPresetsFromFile();
    void savePresetsToFile();
    void refreshPresetCombo();
    void rebuildSlotComponents();
    void layoutSlots();
    void notifyChainChanged();

    // Helpers for loading slots from XML
    void loadSlotsFromXml(const juce::XmlElement& sectionXml, juce::OwnedArray<PluginSlot>& targetSlots);
    static void saveSlotsToXml(juce::XmlElement& parent, const juce::OwnedArray<PluginSlot>& srcSlots);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainComponent)
};
