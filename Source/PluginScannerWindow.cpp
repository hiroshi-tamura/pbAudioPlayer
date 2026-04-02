#include "PluginScannerWindow.h"

//==============================================================================
// ScanThread - background thread for scanning plugins
//==============================================================================
class PluginScannerContent::ScanThread : public juce::Thread
{
public:
    ScanThread(PluginScannerContent& owner,
               juce::KnownPluginList& knownPlugins,
               juce::AudioPluginFormatManager& formatManager,
               const juce::StringArray& paths)
        : juce::Thread("PluginScanThread"),
          owner(owner),
          knownPluginList(knownPlugins),
          pluginFormatManager(formatManager),
          searchPaths(paths)
    {
    }

    void run() override
    {
        juce::FileSearchPath fileSearchPath;
        for (auto& p : searchPaths)
            fileSearchPath.add(juce::File(p));

        // We only scan VST3 format
        juce::AudioPluginFormat* vst3Format = nullptr;
        for (int i = 0; i < pluginFormatManager.getNumFormats(); ++i)
        {
            if (pluginFormatManager.getFormat(i)->getName() == "VST3")
            {
                vst3Format = pluginFormatManager.getFormat(i);
                break;
            }
        }

        if (vst3Format == nullptr)
            return;

        juce::PluginDirectoryScanner scanner(
            knownPluginList,
            *vst3Format,
            fileSearchPath,
            true,  // recursive
            juce::File()  // dead mans pedal file (none)
        );

        juce::String pluginBeingScanned;
        while (!threadShouldExit())
        {
            if (!scanner.scanNextFile(true, pluginBeingScanned))
                break;

            float prog = scanner.getProgress();
            juce::String status = pluginBeingScanned;

            // Post progress to the message thread
            juce::MessageManager::callAsync([this, prog, status]()
            {
                owner.scanProgress = static_cast<double>(prog);
                owner.scanStatusText = status;
            });
        }

        // Signal completion on the message thread
        juce::MessageManager::callAsync([this]()
        {
            owner.scanProgress = 1.0;
            owner.scanStatusText = juce::String();
            owner.updatePluginListFromKnown();
            owner.saveCache();
        });
    }

private:
    PluginScannerContent& owner;
    juce::KnownPluginList& knownPluginList;
    juce::AudioPluginFormatManager& pluginFormatManager;
    juce::StringArray searchPaths;
};

//==============================================================================
// FolderListModel
//==============================================================================
void PluginScannerContent::FolderListModel::paintListBoxItem(
    int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= searchPaths.size())
        return;

    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff555555));
    else
        g.fillAll(juce::Colour(0xff2a2a2a));

    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(searchPaths[rowNumber], 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

//==============================================================================
// PluginScannerWindow
//==============================================================================
static std::unique_ptr<PluginScannerWindow> s_scannerWindow;

PluginScannerWindow::PluginScannerWindow(juce::KnownPluginList& knownPlugins,
                                         juce::AudioPluginFormatManager& formatManager,
                                         bool useJapanese)
    : juce::DocumentWindow(
          useJapanese ? juce::String::fromUTF8("\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe3\x82\xb9\xe3\x82\xad\xe3\x83\xa3\xe3\x83\x8a\xe3\x83\xbc")
                      : juce::String("Plugin Scanner"),
          juce::Colour(0xff333333),
          juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(false);
    setContentOwned(new PluginScannerContent(knownPlugins, formatManager, useJapanese), true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    toFront(true);
}

PluginScannerWindow::~PluginScannerWindow()
{
}

void PluginScannerWindow::closeButtonPressed()
{
    s_scannerWindow.reset();
}

void PluginScannerWindow::show(juce::KnownPluginList& list,
                               juce::AudioPluginFormatManager& fmtMgr,
                               bool useJapanese)
{
    if (s_scannerWindow != nullptr)
    {
        s_scannerWindow->toFront(true);
        return;
    }

    s_scannerWindow = std::make_unique<PluginScannerWindow>(list, fmtMgr, useJapanese);
}

//==============================================================================
// PluginScannerContent
//==============================================================================
PluginScannerContent::PluginScannerContent(juce::KnownPluginList& knownPlugins,
                                           juce::AudioPluginFormatManager& formatManager,
                                           bool useJapanese_)
    : knownPluginList(knownPlugins),
      pluginFormatManager(formatManager),
      useJapanese(useJapanese_),
      progressBar(scanProgress)
{
    // Default search paths
#if JUCE_WINDOWS
    searchPaths.add("C:\\Program Files\\Common Files\\VST3");
#elif JUCE_MAC
    searchPaths.add("~/Library/Audio/Plug-Ins/VST3");
    searchPaths.add("/Library/Audio/Plug-Ins/VST3");
#else
    searchPaths.add("~/.vst3");
    searchPaths.add("/usr/lib/vst3");
#endif

    // Load cache (overrides defaults if cache exists)
    loadCache();

    // Folder label
    folderLabel.setText(tr("Search Folders", "\xe6\xa4\x9c\xe7\xb4\xa2\xe3\x83\x95\xe3\x82\xa9\xe3\x83\xab\xe3\x83\x80\xe3\x83\xbc"),
                        juce::dontSendNotification);
    folderLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    folderLabel.setFont(juce::Font(15.0f));
    addAndMakeVisible(folderLabel);

    // Folder list
    folderModel = std::make_unique<FolderListModel>(searchPaths, useJapanese);
    folderListBox.setModel(folderModel.get());
    folderListBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    folderListBox.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff555555));
    folderListBox.setOutlineThickness(1);
    folderListBox.setRowHeight(22);
    addAndMakeVisible(folderListBox);

    // Add folder button
    addFolderButton.setButtonText(tr("Add", "\xe8\xbf\xbd\xe5\x8a\xa0"));
    addFolderButton.onClick = [this]() { addSearchFolder(); };
    addAndMakeVisible(addFolderButton);

    // Remove folder button
    removeFolderButton.setButtonText(tr("Remove", "\xe5\x89\x8a\xe9\x99\xa4"));
    removeFolderButton.onClick = [this]() { removeSearchFolder(); };
    addAndMakeVisible(removeFolderButton);

    // Rescan button
    rescanButton.setButtonText(tr("Rescan", "\xe5\x86\x8d\xe3\x82\xb9\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xb3"));
    rescanButton.onClick = [this]() { startScan(); };
    addAndMakeVisible(rescanButton);

    // Clear button
    clearButton.setButtonText(tr("Clear", "\xe3\x82\xaf\xe3\x83\xaa\xe3\x82\xa2"));
    clearButton.onClick = [this]() { clearPlugins(); };
    addAndMakeVisible(clearButton);

    // Plugin count label
    pluginCountLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    pluginCountLabel.setFont(juce::Font(14.0f));
    pluginCountLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pluginCountLabel);

    // Progress bar
    progressBar.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff2a2a2a));
    progressBar.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff4488cc));
    addAndMakeVisible(progressBar);

    // Progress text label
    progressTextLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    progressTextLabel.setFont(juce::Font(12.0f));
    progressTextLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(progressTextLabel);

    // Plugin table
    pluginTable.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff2a2a2a));
    pluginTable.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff555555));
    pluginTable.setOutlineThickness(1);
    pluginTable.setRowHeight(22);
    pluginTable.setHeaderHeight(24);
    pluginTable.getHeader().setStretchToFitActive(true);

    pluginTable.getHeader().addColumn(
        tr("Plugin Name", "\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe5\x90\x8d"),
        colName, 250, 100, -1,
        juce::TableHeaderComponent::defaultFlags);

    pluginTable.getHeader().addColumn(
        tr("Manufacturer", "\xe3\x83\xa1\xe3\x83\xbc\xe3\x82\xab\xe3\x83\xbc"),
        colManufacturer, 180, 80, -1,
        juce::TableHeaderComponent::defaultFlags);

    pluginTable.getHeader().addColumn(
        tr("Category", "\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe3\x83\xbc"),
        colCategory, 120, 60, -1,
        juce::TableHeaderComponent::defaultFlags);

    pluginTable.setModel(this);
    addAndMakeVisible(pluginTable);

    // Close button
    closeButton.setButtonText(tr("Close", "\xe9\x96\x89\xe3\x81\x98\xe3\x82\x8b"));
    closeButton.onClick = [this]()
    {
        if (auto* window = findParentComponentOfClass<PluginScannerWindow>())
            window->closeButtonPressed();
    };
    addAndMakeVisible(closeButton);

    // Style buttons with dark theme
    auto styleButton = [](juce::TextButton& btn)
    {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555555));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    styleButton(addFolderButton);
    styleButton(removeFolderButton);
    styleButton(rescanButton);
    styleButton(clearButton);
    styleButton(closeButton);

    // Populate from known plugins
    updatePluginListFromKnown();

    setSize(680, 560);

    // Start timer for progress updates
    startTimerHz(15);
}

PluginScannerContent::~PluginScannerContent()
{
    stopTimer();

    if (scanThread != nullptr)
    {
        scanThread->signalThreadShouldExit();
        scanThread->waitForThreadToExit(5000);
        scanThread.reset();
    }
}

void PluginScannerContent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff333333));
}

void PluginScannerContent::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Folder label
    folderLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    // Folder list + buttons
    auto folderArea = area.removeFromTop(80);
    auto folderButtons = folderArea.removeFromRight(80);
    folderListBox.setBounds(folderArea);

    addFolderButton.setBounds(folderButtons.removeFromTop(36).reduced(2));
    removeFolderButton.setBounds(folderButtons.removeFromTop(36).reduced(2));

    area.removeFromTop(8);

    // Rescan / Clear / Count row
    auto controlRow = area.removeFromTop(28);
    rescanButton.setBounds(controlRow.removeFromLeft(90));
    controlRow.removeFromLeft(6);
    clearButton.setBounds(controlRow.removeFromLeft(90));
    pluginCountLabel.setBounds(controlRow);

    area.removeFromTop(6);

    // Progress bar
    progressBar.setBounds(area.removeFromTop(20));
    area.removeFromTop(2);
    progressTextLabel.setBounds(area.removeFromTop(18));

    area.removeFromTop(6);

    // Close button at bottom
    auto bottomRow = area.removeFromBottom(30);
    closeButton.setBounds(bottomRow.removeFromRight(90));

    area.removeFromBottom(4);

    // Plugin table takes the rest
    pluginTable.setBounds(area);
}

//==============================================================================
// TableListBoxModel
//==============================================================================
int PluginScannerContent::getNumRows()
{
    return static_cast<int>(pluginEntries.size());
}

void PluginScannerContent::paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused(width);
    juce::ignoreUnused(height);

    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff4466aa));
    else if (rowNumber % 2 == 0)
        g.fillAll(juce::Colour(0xff2a2a2a));
    else
        g.fillAll(juce::Colour(0xff303030));
}

void PluginScannerContent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    juce::ignoreUnused(rowIsSelected);

    if (rowNumber < 0 || rowNumber >= static_cast<int>(pluginEntries.size()))
        return;

    g.setColour(juce::Colours::white);
    g.setFont(13.0f);

    const auto& entry = pluginEntries[static_cast<size_t>(rowNumber)];
    juce::String text;

    switch (columnId)
    {
        case colName:         text = entry.name; break;
        case colManufacturer: text = entry.manufacturer; break;
        case colCategory:     text = entry.category; break;
        default: break;
    }

    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void PluginScannerContent::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    std::sort(pluginEntries.begin(), pluginEntries.end(),
              [newSortColumnId, isForwards](const PluginEntry& a, const PluginEntry& b)
    {
        juce::String va, vb;
        switch (newSortColumnId)
        {
            case colName:         va = a.name;         vb = b.name; break;
            case colManufacturer: va = a.manufacturer;  vb = b.manufacturer; break;
            case colCategory:     va = a.category;      vb = b.category; break;
            default: return false;
        }

        int result = va.compareIgnoreCase(vb);
        return isForwards ? (result < 0) : (result > 0);
    });

    pluginTable.updateContent();
    pluginTable.repaint();
}

//==============================================================================
// Timer
//==============================================================================
void PluginScannerContent::timerCallback()
{
    // Update progress text
    if (scanStatusText.isNotEmpty())
    {
        juce::String label = tr("Scanning: ", "\xe3\x82\xb9\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xb3\xe4\xb8\xad: ");
        progressTextLabel.setText(label + scanStatusText, juce::dontSendNotification);
    }
    else if (scanProgress >= 1.0)
    {
        progressTextLabel.setText(tr("Scan complete.", "\xe3\x82\xb9\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xb3\xe5\xae\x8c\xe4\xba\x86\xe3\x80\x82"),
                                  juce::dontSendNotification);
    }

    // Update plugin count label
    juce::String countText = tr("Found: ", "\xe6\xa4\x9c\xe5\x87\xba: ")
        + juce::String(static_cast<int>(pluginEntries.size()))
        + tr(" plugins", " \xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3");
    pluginCountLabel.setText(countText, juce::dontSendNotification);

    // Enable/disable buttons based on scan state
    bool scanning = (scanThread != nullptr && scanThread->isThreadRunning());
    rescanButton.setEnabled(!scanning);
    clearButton.setEnabled(!scanning);
    addFolderButton.setEnabled(!scanning);
    removeFolderButton.setEnabled(!scanning);
}

//==============================================================================
// Helpers
//==============================================================================
juce::String PluginScannerContent::tr(const char* en, const char* ja) const
{
    return useJapanese ? juce::String::fromUTF8(ja) : juce::String(en);
}

juce::File PluginScannerContent::getCacheFile() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
               .getParentDirectory()
               .getChildFile("plugincache.xml");
}

void PluginScannerContent::loadCache()
{
    auto cacheFile = getCacheFile();

    if (!cacheFile.existsAsFile())
        return;

    auto xml = juce::parseXML(cacheFile);

    if (xml == nullptr || xml->getTagName() != "PluginCache")
        return;

    // Load search paths
    if (auto* pathsElem = xml->getChildByName("SearchPaths"))
    {
        searchPaths.clear();

        for (auto* pathElem : pathsElem->getChildIterator())
        {
            if (pathElem->getTagName() == "Path")
            {
                juce::String path = pathElem->getAllSubText().trim();
                if (path.isNotEmpty())
                    searchPaths.add(path);
            }
        }
    }

    // Load known plugins
    if (auto* pluginsElem = xml->getChildByName("KnownPlugins"))
    {
        // KnownPluginList expects its own XML element
        // The first child should be the actual KnownPluginList XML
        if (pluginsElem->getNumChildElements() > 0)
        {
            auto* listXml = pluginsElem->getFirstChildElement();
            knownPluginList.recreateFromXml(*listXml);
        }
    }
}

void PluginScannerContent::saveCache()
{
    auto cacheFile = getCacheFile();

    juce::XmlElement root("PluginCache");

    // Save search paths
    auto* pathsElem = root.createNewChildElement("SearchPaths");
    for (auto& p : searchPaths)
    {
        auto* pathElem = pathsElem->createNewChildElement("Path");
        pathElem->addTextElement(p);
    }

    // Save known plugins
    auto* pluginsElem = root.createNewChildElement("KnownPlugins");
    if (auto knownXml = knownPluginList.createXml())
        pluginsElem->addChildElement(knownXml.release());

    root.writeTo(cacheFile);
}

void PluginScannerContent::updatePluginListFromKnown()
{
    pluginEntries.clear();

    auto types = knownPluginList.getTypes();

    for (auto& desc : types)
    {
        PluginEntry entry;
        entry.name = desc.name;
        entry.manufacturer = desc.manufacturerName;
        entry.category = desc.category;
        pluginEntries.push_back(entry);
    }

    // Sort by name by default
    std::sort(pluginEntries.begin(), pluginEntries.end(),
              [](const PluginEntry& a, const PluginEntry& b)
    {
        return a.name.compareIgnoreCase(b.name) < 0;
    });

    pluginTable.updateContent();
    pluginTable.repaint();
}

void PluginScannerContent::startScan()
{
    // Stop any previous scan
    if (scanThread != nullptr)
    {
        scanThread->signalThreadShouldExit();
        scanThread->waitForThreadToExit(5000);
        scanThread.reset();
    }

    scanProgress = 0.0;
    scanStatusText = tr("Starting scan...", "\xe3\x82\xb9\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xb3\xe9\x96\x8b\xe5\xa7\x8b...");
    progressTextLabel.setText(scanStatusText, juce::dontSendNotification);

    scanThread = std::make_unique<ScanThread>(*this, knownPluginList, pluginFormatManager, searchPaths);
    scanThread->startThread();
}

void PluginScannerContent::clearPlugins()
{
    knownPluginList.clear();
    updatePluginListFromKnown();
    saveCache();
    scanProgress = 0.0;
    scanStatusText = juce::String();
    progressTextLabel.setText(tr("Plugin list cleared.", "\xe3\x83\x97\xe3\x83\xa9\xe3\x82\xb0\xe3\x82\xa4\xe3\x83\xb3\xe3\x83\xaa\xe3\x82\xb9\xe3\x83\x88\xe3\x82\x92\xe3\x82\xaf\xe3\x83\xaa\xe3\x82\xa2\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f\xe3\x80\x82"),
                              juce::dontSendNotification);
}

void PluginScannerContent::addSearchFolder()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        tr("Select VST3 Folder", "VST3\xe3\x83\x95\xe3\x82\xa9\xe3\x83\xab\xe3\x83\x80\xe3\x83\xbc\xe3\x82\x92\xe9\x81\xb8\xe6\x8a\x9e"),
        juce::File(),
        "*",
        true);

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, chooser](const juce::FileChooser& fc)
    {
        auto result = fc.getResult();
        if (result.isDirectory())
        {
            juce::String path = result.getFullPathName();
            if (!searchPaths.contains(path))
            {
                searchPaths.add(path);
                folderListBox.updateContent();
                folderListBox.repaint();
                saveCache();
            }
        }
    });
}

void PluginScannerContent::removeSearchFolder()
{
    int selectedRow = folderListBox.getSelectedRow();

    if (selectedRow >= 0 && selectedRow < searchPaths.size())
    {
        searchPaths.remove(selectedRow);
        folderListBox.updateContent();
        folderListBox.repaint();
        saveCache();
    }
}
