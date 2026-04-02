#pragma once
#include <JuceHeader.h>

//==============================================================================
// PluginScannerWindow - VST3 plugin scanner dialog
//==============================================================================
class PluginScannerWindow : public juce::DocumentWindow
{
public:
    PluginScannerWindow(juce::KnownPluginList& knownPlugins,
                        juce::AudioPluginFormatManager& formatManager,
                        bool useJapanese);
    ~PluginScannerWindow() override;

    void closeButtonPressed() override;

    static void show(juce::KnownPluginList& list,
                     juce::AudioPluginFormatManager& fmtMgr,
                     bool useJapanese);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScannerWindow)
};

//==============================================================================
// PluginScannerContent - main content component for the scanner window
//==============================================================================
class PluginScannerContent : public juce::Component,
                             public juce::TableListBoxModel,
                             private juce::Timer
{
public:
    PluginScannerContent(juce::KnownPluginList& knownPlugins,
                         juce::AudioPluginFormatManager& formatManager,
                         bool useJapanese);
    ~PluginScannerContent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;

private:
    // Timer
    void timerCallback() override;

    // Helpers
    juce::String tr(const char* en, const char* ja) const;
    juce::File getCacheFile() const;
    void loadCache();
    void saveCache();
    void updatePluginListFromKnown();
    void startScan();
    void clearPlugins();
    void addSearchFolder();
    void removeSearchFolder();

    // References
    juce::KnownPluginList& knownPluginList;
    juce::AudioPluginFormatManager& pluginFormatManager;
    bool useJapanese;

    // Search paths
    juce::StringArray searchPaths;

    // UI components
    juce::Label folderLabel;
    juce::ListBox folderListBox;
    juce::TextButton addFolderButton;
    juce::TextButton removeFolderButton;
    juce::TextButton rescanButton;
    juce::TextButton clearButton;
    juce::Label pluginCountLabel;
    juce::ProgressBar progressBar;
    juce::Label progressTextLabel;
    juce::TableListBox pluginTable;
    juce::TextButton closeButton;

    // Progress
    double scanProgress = 0.0;
    juce::String scanStatusText;

    // Scanning thread
    class ScanThread;
    std::unique_ptr<ScanThread> scanThread;

    // Sorted plugin descriptors for table display
    struct PluginEntry
    {
        juce::String name;
        juce::String manufacturer;
        juce::String category;
    };
    std::vector<PluginEntry> pluginEntries;

    // Folder list model
    class FolderListModel : public juce::ListBoxModel
    {
    public:
        FolderListModel(juce::StringArray& paths, bool jp) : searchPaths(paths), useJapanese(jp) {}
        int getNumRows() override { return searchPaths.size(); }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    private:
        juce::StringArray& searchPaths;
        bool useJapanese;
    };
    std::unique_ptr<FolderListModel> folderModel;

    // Column IDs
    enum ColumnIds
    {
        colName = 1,
        colManufacturer = 2,
        colCategory = 3
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScannerContent)
};
