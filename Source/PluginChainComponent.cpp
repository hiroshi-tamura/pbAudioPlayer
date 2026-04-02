#include "PluginChainComponent.h"

//==============================================================================
// PluginSlotComponent
//==============================================================================
PluginSlotComponent::PluginSlotComponent(PluginChainComponent& owner, int index, bool isPre)
    : ownerChain(owner), slotIndex(index), isPreSlot(isPre)
{
    bypassButton.setButtonText("B");
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555555));
    bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    bypassButton.onClick = [this]() { ownerChain.toggleBypass(slotIndex, isPreSlot); };
    addAndMakeVisible(bypassButton);

    removeButton.setButtonText("x");
    removeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555555));
    removeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    removeButton.onClick = [this]() { ownerChain.removeSlot(slotIndex, isPreSlot); };
    addAndMakeVisible(removeButton);

    updateFromSlot();
}

void PluginSlotComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRoundedRectangle(bounds.toFloat().reduced(1.0f), 3.0f);
    g.setColour(juce::Colour(0xff505050));
    g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), 3.0f, 1.0f);

    auto* slot = ownerChain.getSlot(slotIndex, isPreSlot);
    if (!slot) return;

    auto numArea = bounds.removeFromLeft(22);
    g.setColour(juce::Colour(0xffaaaaaa));
    g.setFont(juce::Font(11.0f));
    g.drawText(juce::String(slotIndex + 1), numArea, juce::Justification::centred);

    auto nameArea = bounds.withTrimmedRight(50);
    if (slot->hasPlugin())
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f));
        g.drawText(slot->getPluginName(), nameArea, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour(juce::Colour(0xff888888));
        g.setFont(juce::Font(12.0f).italicised());
        g.drawText(ownerChain.tr("(Empty)", "(\xe7\xa9\xba)"), nameArea, juce::Justification::centredLeft, true);
    }
}

void PluginSlotComponent::resized()
{
    auto bounds = getLocalBounds().reduced(1);
    removeButton.setBounds(bounds.removeFromRight(24).reduced(2));
    bypassButton.setBounds(bounds.removeFromRight(24).reduced(2));
}

void PluginSlotComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown())
        ownerChain.showPluginMenu(slotIndex, isPreSlot);
}

void PluginSlotComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    auto* slot = ownerChain.getSlot(slotIndex, isPreSlot);
    if (slot && slot->hasPlugin())
        ownerChain.openPluginEditor(slotIndex, isPreSlot);
}

void PluginSlotComponent::updateFromSlot()
{
    auto* slot = ownerChain.getSlot(slotIndex, isPreSlot);
    if (!slot) return;

    if (slot->bypassed)
    {
        bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdd8800));
        bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }
    else
    {
        bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555555));
        bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcccccc));
    }
    repaint();
}

//==============================================================================
// PluginChainComponent
//==============================================================================
PluginChainComponent::PluginChainComponent(juce::KnownPluginList& knownPlugins,
                                           juce::AudioPluginFormatManager& fmtManager,
                                           bool japaneseMode)
    : knownPluginList(knownPlugins),
      pluginFormatManager(fmtManager),
      useJapanese(japaneseMode)
{
    viewport.setViewedComponent(&slotContainer, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    // Add slot buttons (inside slotContainer)
    auto styleAddBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    // Section labels
    preSectionLabel.setText(tr("Pre", "Pre"), juce::dontSendNotification);
    preSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff88aaff));
    preSectionLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff333344));
    preSectionLabel.setFont(juce::Font(11.0f).boldened());
    preSectionLabel.setJustificationType(juce::Justification::centredLeft);
    slotContainer.addAndMakeVisible(preSectionLabel);

    postSectionLabel.setText(tr("Post", "Post"), juce::dontSendNotification);
    postSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffaa88));
    postSectionLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff443333));
    postSectionLabel.setFont(juce::Font(11.0f).boldened());
    postSectionLabel.setJustificationType(juce::Justification::centredLeft);
    slotContainer.addAndMakeVisible(postSectionLabel);

    addPreSlotButton.setButtonText("+");
    addPreSlotButton.onClick = [this]() { addEmptySlot(true); };
    styleAddBtn(addPreSlotButton);
    slotContainer.addAndMakeVisible(addPreSlotButton);

    addPostSlotButton.setButtonText("+");
    addPostSlotButton.onClick = [this]() { addEmptySlot(false); };
    styleAddBtn(addPostSlotButton);
    slotContainer.addAndMakeVisible(addPostSlotButton);

    // Preset UI - stored in a single file
    presetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff3a3a3a));
    presetCombo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    presetCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff555555));
    presetCombo.setTextWhenNothingSelected(tr("-- Preset --", "-- \xe3\x83\x97\xe3\x83\xaa\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88 --"));
    presetCombo.onChange = [this]()
    {
        int idx = presetCombo.getSelectedItemIndex();
        if (idx >= 0)
            loadPresetByIndex(idx);
    };
    addAndMakeVisible(presetCombo);

    auto styleIconBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3d3d5c));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    presetOverwriteBtn.setButtonText(juce::String::fromUTF8("\xe2\x86\x91")); // ↑
    presetOverwriteBtn.setTooltip(tr("Overwrite", "\xe4\xb8\x8a\xe6\x9b\xb8\xe3\x81\x8d"));
    presetOverwriteBtn.onClick = [this]() { overwritePreset(); };
    presetOverwriteBtn.setEnabled(false);
    styleIconBtn(presetOverwriteBtn);
    addAndMakeVisible(presetOverwriteBtn);

    presetSaveAsBtn.setButtonText(juce::String::fromUTF8("\xe2\x86\x93")); // ↓
    presetSaveAsBtn.setTooltip(tr("Save As New", "\xe6\x96\xb0\xe8\xa6\x8f\xe4\xbf\x9d\xe5\xad\x98"));
    presetSaveAsBtn.onClick = [this]() { savePresetAs(); };
    styleIconBtn(presetSaveAsBtn);
    addAndMakeVisible(presetSaveAsBtn);

    presetDeleteBtn.setButtonText(juce::String::fromUTF8("\xc3\x97")); // ×
    presetDeleteBtn.setTooltip(tr("Delete", "\xe5\x89\x8a\xe9\x99\xa4"));
    presetDeleteBtn.onClick = [this]() { deletePreset(); };
    presetDeleteBtn.setEnabled(false);
    styleIconBtn(presetDeleteBtn);
    addAndMakeVisible(presetDeleteBtn);

    loadPresetsFromFile();
    refreshPresetCombo();
}

PluginChainComponent::~PluginChainComponent()
{
    for (auto* s : preSlots)  s->closeEditorWindow();
    for (auto* s : postSlots) s->closeEditorWindow();
    preSlotComponents.clear();
    postSlotComponents.clear();
    preSlots.clear();
    postSlots.clear();
}

void PluginChainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));

    // Title row
    auto titleArea = getLocalBounds().removeFromTop(22);
    g.setColour(juce::Colour(0xff333333));
    g.fillRect(titleArea);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f).boldened());
    g.drawText(tr("Plugin Chain", "\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe3\x83\x81\xe3\x82\xa7\xe3\x83\xbc\xe3\x83\xb3"),
               titleArea.reduced(6, 0), juce::Justification::centredLeft);

    // Preset row bg
    auto presetBg = getLocalBounds().withTop(22).withHeight(24);
    g.setColour(juce::Colour(0xff2e2e2e));
    g.fillRect(presetBg);
}

void PluginChainComponent::resized()
{
    auto bounds = getLocalBounds();

    // Title (22px)
    bounds.removeFromTop(22);

    // Preset row (24px)
    auto presetRow = bounds.removeFromTop(24).reduced(2, 1);
    int iconBtnW = 24;
    presetDeleteBtn.setBounds(presetRow.removeFromRight(iconBtnW).reduced(1, 0));
    presetSaveAsBtn.setBounds(presetRow.removeFromRight(iconBtnW).reduced(1, 0));
    presetOverwriteBtn.setBounds(presetRow.removeFromRight(iconBtnW).reduced(1, 0));
    presetCombo.setBounds(presetRow.reduced(1, 0));

    // Viewport fills the rest
    viewport.setBounds(bounds);
    layoutSlots();
}

//==============================================================================
// Audio Processing
//==============================================================================
void PluginChainComponent::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    for (auto* s : preSlots)  s->prepareToPlay(sampleRate, samplesPerBlock);
    for (auto* s : postSlots) s->prepareToPlay(sampleRate, samplesPerBlock);
}

void PluginChainComponent::processPreBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (auto* s : preSlots) s->processBlock(buffer, midi);
}

void PluginChainComponent::processPostBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (auto* s : postSlots) s->processBlock(buffer, midi);
}

void PluginChainComponent::releaseResources()
{
    for (auto* s : preSlots)  s->releaseResources();
    for (auto* s : postSlots) s->releaseResources();
}

//==============================================================================
// Expand / Collapse
//==============================================================================
void PluginChainComponent::setExpanded(bool shouldExpand)
{
    if (expanded == shouldExpand) return;
    expanded = shouldExpand;
    setVisible(expanded);
}

//==============================================================================
// Slot Management
//==============================================================================
void PluginChainComponent::addEmptySlot(bool isPre)
{
    auto& slots = isPre ? preSlots : postSlots;
    slots.add(new PluginSlot());
    rebuildSlotComponents();
    notifyChainChanged();
}

void PluginChainComponent::removeSlot(int index, bool isPre)
{
    auto& slots = isPre ? preSlots : postSlots;
    if (index < 0 || index >= slots.size()) return;
    slots[index]->closeEditorWindow();
    slots[index]->releaseResources();
    slots.remove(index);
    rebuildSlotComponents();
    notifyChainChanged();
}

void PluginChainComponent::toggleBypass(int index, bool isPre)
{
    if (auto* slot = getSlot(index, isPre))
    {
        slot->bypassed = !slot->bypassed;
        auto& comps = isPre ? preSlotComponents : postSlotComponents;
        if (index < comps.size())
            comps[index]->updateFromSlot();
    }
}

PluginSlot* PluginChainComponent::getSlot(int index, bool isPre) const
{
    auto& slots = isPre ? preSlots : postSlots;
    return (index >= 0 && index < slots.size()) ? slots[index] : nullptr;
}

//==============================================================================
// Plugin Menu (right-click)
//==============================================================================
void PluginChainComponent::showPluginMenu(int slotIndex, bool isPre)
{
    juce::PopupMenu menu;
    auto pluginTypes = knownPluginList.getTypes();

    if (pluginTypes.isEmpty())
    {
        menu.addItem(1, tr("No plugins available", "\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe3\x81\x8c\xe3\x81\x82\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x9b\xe3\x82\x93"), false);
        menu.showMenuAsync(juce::PopupMenu::Options());
        return;
    }

    std::map<juce::String, std::vector<int>> manufacturerMap;
    for (int i = 0; i < pluginTypes.size(); ++i)
        manufacturerMap[pluginTypes.getReference(i).manufacturerName].push_back(i);

    int menuId = 100;
    std::map<int, int> menuIdToPluginIndex;
    for (auto& [manufacturer, indices] : manufacturerMap)
    {
        juce::PopupMenu subMenu;
        for (int idx : indices)
        {
            subMenu.addItem(menuId, pluginTypes.getReference(idx).name);
            menuIdToPluginIndex[menuId] = idx;
            ++menuId;
        }
        menu.addSubMenu(manufacturer, subMenu);
    }

    auto* slot = getSlot(slotIndex, isPre);
    if (slot && slot->hasPlugin())
    {
        menu.addSeparator();
        menu.addItem(1, tr("Remove Plugin", "\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe3\x82\x92\xe5\x89\x8a\xe9\x99\xa4"));
    }

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, slotIndex, isPre, menuIdToPluginIndex](int result)
        {
            if (result == 0) return;
            if (result == 1)
            {
                if (auto* s = getSlot(slotIndex, isPre))
                {
                    s->removePlugin();
                    auto& comps = isPre ? preSlotComponents : postSlotComponents;
                    if (slotIndex < comps.size())
                        comps[slotIndex]->updateFromSlot();
                    notifyChainChanged();
                }
                return;
            }
            auto it = menuIdToPluginIndex.find(result);
            if (it != menuIdToPluginIndex.end())
            {
                auto types = knownPluginList.getTypes();
                if (it->second < types.size())
                    loadPluginIntoSlot(slotIndex, isPre, types.getReference(it->second));
            }
        });
}

//==============================================================================
// Load Plugin
//==============================================================================
void PluginChainComponent::loadPluginIntoSlot(int slotIndex, bool isPre, const juce::PluginDescription& desc)
{
    auto* slot = getSlot(slotIndex, isPre);
    if (!slot) return;

    slot->removePlugin();
    juce::String errorMsg;
    auto instance = pluginFormatManager.createPluginInstance(desc, currentSampleRate, currentBlockSize, errorMsg);
    if (instance)
    {
        slot->pluginInstance = std::move(instance);
        slot->pluginDescription = desc;
        slot->prepareToPlay(currentSampleRate, currentBlockSize);
        auto& comps = isPre ? preSlotComponents : postSlotComponents;
        if (slotIndex < comps.size())
            comps[slotIndex]->updateFromSlot();
        notifyChainChanged();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            tr("Plugin Load Error", "\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe8\xaa\xad\xe8\xbe\xbc\xe3\x82\xa8\xe3\x83\xa9\xe3\x83\xbc"),
            tr("Failed to load plugin: ", "\xe8\xaa\xad\xe8\xbe\xbc\xe5\xa4\xb1\xe6\x95\x97: ") + errorMsg);
    }
}

//==============================================================================
// Plugin Editor
//==============================================================================
void PluginChainComponent::openPluginEditor(int slotIndex, bool isPre)
{
    auto* slot = getSlot(slotIndex, isPre);
    if (!slot || !slot->hasPlugin()) return;

    if (slot->editorWindow)
    {
        slot->editorWindow->toFront(true);
        return;
    }

    auto* editor = slot->pluginInstance->createEditor();
    if (!editor)
        editor = new juce::GenericAudioProcessorEditor(*slot->pluginInstance);

    juce::String section = isPre ? "Pre" : "Post";
    juce::String windowTitle = slot->getPluginName() + " [" + section + " " + juce::String(slotIndex + 1) + "]";

    slot->editorWindow = std::make_unique<PluginEditorWindow>(
        windowTitle, editor,
        [this, slotIndex, isPre]()
        {
            juce::MessageManager::callAsync([this, slotIndex, isPre]()
            {
                if (auto* s = getSlot(slotIndex, isPre))
                    s->closeEditorWindow();
            });
        });
}

//==============================================================================
// State Persistence
//==============================================================================
void PluginChainComponent::saveSlotsToXml(juce::XmlElement& parent, const juce::OwnedArray<PluginSlot>& srcSlots)
{
    for (auto* slot : srcSlots)
        if (auto slotXml = slot->saveState())
            parent.addChildElement(slotXml.release());
}

void PluginChainComponent::loadSlotsFromXml(const juce::XmlElement& sectionXml, juce::OwnedArray<PluginSlot>& targetSlots)
{
    for (auto* slotXml : sectionXml.getChildWithTagNameIterator("PluginSlot"))
    {
        auto* newSlot = targetSlots.add(new PluginSlot());
        newSlot->bypassed = slotXml->getBoolAttribute("bypassed", false);

        auto* descXml = slotXml->getChildByName("plugin");
        if (descXml)
        {
            juce::PluginDescription desc;
            if (desc.loadFromXml(*descXml))
            {
                juce::String errorMsg;
                auto instance = pluginFormatManager.createPluginInstance(desc, currentSampleRate, currentBlockSize, errorMsg);
                if (instance)
                {
                    newSlot->pluginInstance = std::move(instance);
                    newSlot->pluginDescription = desc;

                    if (auto* stateXml = slotXml->getChildByName("PluginState"))
                    {
                        juce::MemoryBlock stateData;
                        stateData.fromBase64Encoding(stateXml->getStringAttribute("data"));
                        newSlot->pluginInstance->setStateInformation(stateData.getData(), (int)stateData.getSize());
                    }
                    newSlot->prepareToPlay(currentSampleRate, currentBlockSize);
                }
            }
        }
    }
}

std::unique_ptr<juce::XmlElement> PluginChainComponent::saveState() const
{
    auto chainXml = std::make_unique<juce::XmlElement>("PluginChain");
    chainXml->setAttribute("expanded", expanded);

    auto* preXml = chainXml->createNewChildElement("Pre");
    saveSlotsToXml(*preXml, preSlots);

    auto* postXml = chainXml->createNewChildElement("Post");
    saveSlotsToXml(*postXml, postSlots);

    return chainXml;
}

void PluginChainComponent::loadState(const juce::XmlElement& xml)
{
    if (xml.getTagName() != "PluginChain") return;

    for (auto* s : preSlots)  { s->closeEditorWindow(); s->releaseResources(); }
    for (auto* s : postSlots) { s->closeEditorWindow(); s->releaseResources(); }
    preSlotComponents.clear();
    postSlotComponents.clear();
    preSlots.clear();
    postSlots.clear();

    if (auto* preXml = xml.getChildByName("Pre"))
        loadSlotsFromXml(*preXml, preSlots);
    if (auto* postXml = xml.getChildByName("Post"))
        loadSlotsFromXml(*postXml, postSlots);

    rebuildSlotComponents();

    bool wasExpanded = xml.getBoolAttribute("expanded", false);
    setExpanded(wasExpanded);
}

//==============================================================================
// Internal helpers
//==============================================================================
void PluginChainComponent::rebuildSlotComponents()
{
    preSlotComponents.clear();
    postSlotComponents.clear();

    for (int i = 0; i < preSlots.size(); ++i)
    {
        auto* comp = new PluginSlotComponent(*this, i, true);
        slotContainer.addAndMakeVisible(comp);
        preSlotComponents.add(comp);
    }

    for (int i = 0; i < postSlots.size(); ++i)
    {
        auto* comp = new PluginSlotComponent(*this, i, false);
        slotContainer.addAndMakeVisible(comp);
        postSlotComponents.add(comp);
    }

    layoutSlots();
    repaint();
}

void PluginChainComponent::layoutSlots()
{
    int containerWidth = viewport.getWidth() > 0 ? viewport.getWidth() : getWidth();
    int sectionHeaderH = 20;
    int addBtnH = 22;
    int addBtnW = addBtnH; // square button
    int separatorH = 8;    // gap between Pre and Post sections (includes divider line)
    int y = 0;

    // === Pre Section Header ===
    preSectionLabel.setBounds(0, y, containerWidth, sectionHeaderH);
    y += sectionHeaderH;

    for (int i = 0; i < preSlotComponents.size(); ++i)
    {
        preSlotComponents[i]->setBounds(0, y, containerWidth, slotHeight);
        y += slotHeight;
    }
    addPreSlotButton.setBounds(containerWidth - addBtnW - 4, y, addBtnW, addBtnH);
    y += addBtnH;

    // Store separator Y for paint (both local and container)
    separatorY = y + separatorH / 2;
    slotContainer.separatorY = separatorY;
    y += separatorH;

    // === Post Section Header ===
    postSectionLabel.setBounds(0, y, containerWidth, sectionHeaderH);
    y += sectionHeaderH;

    for (int i = 0; i < postSlotComponents.size(); ++i)
    {
        postSlotComponents[i]->setBounds(0, y, containerWidth, slotHeight);
        y += slotHeight;
    }
    addPostSlotButton.setBounds(containerWidth - addBtnW - 4, y, addBtnW, addBtnH);
    y += addBtnH;

    slotContainer.setBounds(0, 0, containerWidth, juce::jmax(y, 1));
    slotContainer.repaint();
}

void PluginChainComponent::notifyChainChanged()
{
    if (onChainChanged)
        onChainChanged();
}

//==============================================================================
// Preset Management (single file: PluginPresets.xml)
//==============================================================================
juce::File PluginChainComponent::getPresetFile() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
               .getParentDirectory().getChildFile("PluginPresets.xml");
}

void PluginChainComponent::loadPresetsFromFile()
{
    presetEntries.clear();
    auto file = getPresetFile();
    if (!file.existsAsFile()) return;

    auto rootXml = juce::parseXML(file);
    if (!rootXml || rootXml->getTagName() != "PluginPresets") return;

    for (auto* child : rootXml->getChildWithTagNameIterator("Preset"))
    {
        PresetEntry entry;
        entry.name = child->getStringAttribute("name");
        entry.xml = std::make_unique<juce::XmlElement>(*child);
        presetEntries.push_back(std::move(entry));
    }
}

void PluginChainComponent::savePresetsToFile()
{
    juce::XmlElement root("PluginPresets");
    for (auto& entry : presetEntries)
    {
        if (entry.xml)
            root.addChildElement(new juce::XmlElement(*entry.xml));
    }
    root.writeTo(getPresetFile());
}

void PluginChainComponent::refreshPresetCombo()
{
    presetCombo.clear(juce::dontSendNotification);
    for (int i = 0; i < (int)presetEntries.size(); ++i)
        presetCombo.addItem(presetEntries[i].name, i + 1);

    if (currentPresetIndex >= 0 && currentPresetIndex < (int)presetEntries.size())
        presetCombo.setSelectedItemIndex(currentPresetIndex, juce::dontSendNotification);
}

void PluginChainComponent::loadPresetByIndex(int index)
{
    if (index < 0 || index >= (int)presetEntries.size()) return;

    auto* presetXml = presetEntries[index].xml.get();
    if (!presetXml) return;

    // The Preset element contains a PluginChain child
    if (auto* chainXml = presetXml->getChildByName("PluginChain"))
    {
        loadState(*chainXml);
        currentPresetIndex = index;
        presetOverwriteBtn.setEnabled(true);
        presetDeleteBtn.setEnabled(true);
        notifyChainChanged();
    }
}

void PluginChainComponent::savePresetAs()
{
    auto* aw = new juce::AlertWindow(
        tr("Save Preset", "\xe3\x83\x97\xe3\x83\xaa\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88\xe4\xbf\x9d\xe5\xad\x98"),
        tr("Enter preset name:", "\xe3\x83\x97\xe3\x83\xaa\xe3\x82\xbb\xe3\x83\x83\xe3\x83\x88\xe5\x90\x8d\xe3\x82\x92\xe5\x85\xa5\xe5\x8a\x9b:"),
        juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor("name", "", tr("Name", "\xe5\x90\x8d\xe5\x89\x8d"));
    aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton(tr("Cancel", "\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xb3\xe3\x82\xbb\xe3\x83\xab"), 0);

    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, aw](int result)
        {
            if (result == 1)
            {
                auto name = aw->getTextEditorContents("name").trim();
                if (name.isNotEmpty())
                {
                    PresetEntry entry;
                    entry.name = name;

                    auto presetXml = std::make_unique<juce::XmlElement>("Preset");
                    presetXml->setAttribute("name", name);
                    if (auto chainState = saveState())
                        presetXml->addChildElement(chainState.release());
                    entry.xml = std::move(presetXml);

                    presetEntries.push_back(std::move(entry));
                    currentPresetIndex = (int)presetEntries.size() - 1;
                    savePresetsToFile();
                    refreshPresetCombo();
                    presetOverwriteBtn.setEnabled(true);
                    presetDeleteBtn.setEnabled(true);
                }
            }
            delete aw;
        }), true);
}

void PluginChainComponent::overwritePreset()
{
    if (currentPresetIndex < 0 || currentPresetIndex >= (int)presetEntries.size()) return;

    auto& entry = presetEntries[currentPresetIndex];
    auto presetXml = std::make_unique<juce::XmlElement>("Preset");
    presetXml->setAttribute("name", entry.name);
    if (auto chainState = saveState())
        presetXml->addChildElement(chainState.release());
    entry.xml = std::move(presetXml);

    savePresetsToFile();
}

void PluginChainComponent::deletePreset()
{
    if (currentPresetIndex < 0 || currentPresetIndex >= (int)presetEntries.size()) return;

    presetEntries.erase(presetEntries.begin() + currentPresetIndex);
    currentPresetIndex = -1;
    presetOverwriteBtn.setEnabled(false);
    presetDeleteBtn.setEnabled(false);
    savePresetsToFile();
    refreshPresetCombo();
}
