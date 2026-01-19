#include "MainComponent.h"
#include "MainTabView.h"
#include "GlobalTabView.h"
#include "AudioCacheStore.h"
#include "MutationOrchestrator.h"
#include "PreviewChainOrchestrator.h"
#include "RecordingModule.h"
#include "SliceContextState.h"
#include <cmath>
#include <array>
#include <optional>
#include <vector>
#include "AppProperties.h"

namespace
{
    constexpr const char* kVirtualOutIdentifier = "virtual:slicebot-sync-out";
    constexpr const char* kVirtualInIdentifier = "virtual:slicebot-sync-in";
    constexpr const char* kVirtualOutName = "SliceBot Sync Out";
    constexpr const char* kVirtualInName = "SliceBot Sync In";

    struct ExportDialogResult
    {
        juce::String exportPrefix;
        bool generateIndividual = true;
        bool generateChain = true;
    };

    class ExportOptionsComponent final : public juce::Component
    {
    public:
        ExportOptionsComponent (const juce::String& initialPrefix,
                                bool initialIndividual,
                                bool initialChain)
        {
            prefixLabel.setText ("Prefix:", juce::dontSendNotification);
            prefixLabel.setColour (juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible (prefixLabel);

            prefixEditor.setText (initialPrefix, juce::dontSendNotification);
            addAndMakeVisible (prefixEditor);

            individualToggle.setButtonText ("Generate Individual Samples");
            individualToggle.setToggleState (initialIndividual, juce::dontSendNotification);
            addAndMakeVisible (individualToggle);

            chainToggle.setButtonText ("Generate Sample Chain");
            chainToggle.setToggleState (initialChain, juce::dontSendNotification);
            addAndMakeVisible (chainToggle);

            okButton.setButtonText ("OK");
            cancelButton.setButtonText ("Cancel");
            addAndMakeVisible (okButton);
            addAndMakeVisible (cancelButton);

            okButton.onClick = [this]()
            {
                accepted = true;
                if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (1);
            };
            cancelButton.onClick = [this]()
            {
                accepted = false;
                if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (0);
            };

            setSize (320, 140);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced (12);
            auto row = bounds.removeFromTop (24);
            prefixLabel.setBounds (row.removeFromLeft (60));
            prefixEditor.setBounds (row);

            bounds.removeFromTop (8);
            individualToggle.setBounds (bounds.removeFromTop (24));
            chainToggle.setBounds (bounds.removeFromTop (24));

            bounds.removeFromTop (8);
            auto buttonRow = bounds.removeFromTop (24);
            okButton.setBounds (buttonRow.removeFromLeft (80));
            buttonRow.removeFromLeft (8);
            cancelButton.setBounds (buttonRow.removeFromLeft (80));
        }

        bool wasAccepted() const { return accepted; }

        juce::String getPrefix() const
        {
            return prefixEditor.getText().trim();
        }

        bool shouldGenerateIndividual() const { return individualToggle.getToggleState(); }
        bool shouldGenerateChain() const { return chainToggle.getToggleState(); }

    private:
        juce::Label prefixLabel;
        juce::TextEditor prefixEditor;
        juce::ToggleButton individualToggle;
        juce::ToggleButton chainToggle;
        juce::TextButton okButton;
        juce::TextButton cancelButton;
        bool accepted = false;
    };

    std::optional<ExportDialogResult> promptExportOptions()
    {
        auto* settings = AppProperties::get().properties().getUserSettings();
        const juce::String lastPrefix =
            settings != nullptr ? settings->getValue ("LastExportPrefix", "export") : "export";
        const bool lastGenerateIndividual =
            settings != nullptr ? settings->getBoolValue ("LastGenerateIndividual", true) : true;
        const bool lastGenerateChain =
            settings != nullptr ? settings->getBoolValue ("LastGenerateChain", true) : true;
        juce::String prefix = lastPrefix.trim();
        if (prefix.isEmpty())
            prefix = "export";

        return ExportDialogResult { prefix,
                                    lastGenerateIndividual,
                                    lastGenerateChain };
    }

    class LiveModulePlaceholder final : public juce::Component
    {
    public:
        void setClickHandler (std::function<void()> handler)
        {
            onClick = std::move (handler);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff5a5a5a));
            auto bounds = getLocalBounds().toFloat();
            g.setColour (juce::Colours::grey);
            g.drawRect (bounds.reduced (10.0f), 1.0f);

            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (36.0f)));
            g.drawFittedText ("+", getLocalBounds(), juce::Justification::centred, 1);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (onClick)
                onClick();
        }

    private:
        std::function<void()> onClick;
    };

    class LiveModuleSlot final : public juce::Component
    {
    public:
        LiveModuleSlot (AudioEngine& engineToUse, int moduleIndexToUse)
            : audioEngine (engineToUse),
              moduleIndex (moduleIndexToUse)
        {
            addAndMakeVisible (placeholder);
            placeholder.setClickHandler ([this]()
            {
                if (placeholderClickHandler)
                    placeholderClickHandler();
            });
        }

        void setEnabled (bool shouldEnable)
        {
            if (isEnabled == shouldEnable)
                return;

            isEnabled = shouldEnable;
            if (isEnabled)
            {
                recorderView = std::make_unique<LiveRecorderModuleView> (audioEngine, moduleIndex);
                recorderView->setDeleteModuleHandler ([this]()
                {
                    if (deleteModuleHandler)
                        deleteModuleHandler();
                    else
                        setEnabled (false);
                });
                addAndMakeVisible (*recorderView);
            }
            else
            {
                if (recorderView != nullptr)
                    removeChildComponent (recorderView.get());
                recorderView.reset();
            }

            placeholder.setVisible (! isEnabled);
            resized();
        }

        bool isModuleEnabled() const
        {
            return isEnabled;
        }

        void setPlaceholderClickHandler (std::function<void()> handler)
        {
            placeholderClickHandler = std::move (handler);
        }

        void setDeleteModuleHandler (std::function<void()> handler)
        {
            deleteModuleHandler = std::move (handler);
        }

        int getModuleIndex() const
        {
            return moduleIndex;
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            placeholder.setBounds (bounds);
            if (recorderView != nullptr)
                recorderView->setBounds (bounds);
        }

    private:
        AudioEngine& audioEngine;
        int moduleIndex = 0;
        bool isEnabled = false;
        LiveModulePlaceholder placeholder;
        std::unique_ptr<LiveRecorderModuleView> recorderView;
        std::function<void()> placeholderClickHandler;
        std::function<void()> deleteModuleHandler;
    };

    class SliceContextOverlay final : public juce::Component
    {
    public:
        struct IconSource
        {
            juce::String name;
            juce::File file;
        };

        enum class Action
        {
            lock,
            remove,
            regen,
            swap,
            duplicate,
            reverse
        };

        SliceContextOverlay()
        {
            setWantsKeyboardFocus (true);
            setVisible (false);
            setInterceptsMouseClicks (false, false);
        }

        void setActionHandler (std::function<void(Action, int)> handler)
        {
            actionHandler = std::move (handler);
        }

        void setIconSources (const std::array<IconSource, 6>& sources)
        {
            for (size_t index = 0; index < iconDrawables.size(); ++index)
            {
                iconDrawables[index].reset();
                iconDrawables[index] = createDrawableFromBinaryData (sources[index].name);

                if (iconDrawables[index] == nullptr)
                    iconDrawables[index] = createDrawableFromFile (sources[index].file);

                if (iconDrawables[index] != nullptr)
                {
                    iconDrawables[index]->replaceColour (juce::Colours::black, juce::Colours::white);
                    iconDrawables[index]->replaceColour (juce::Colour (0xff000000), juce::Colours::white);
                }
            }
            repaint();
        }

        void setDismissHandler (std::function<void()> handler)
        {
            dismissHandler = std::move (handler);
        }

        void showForCell (int indexToUse, juce::Rectangle<int> cellBoundsToUse)
        {
            targetIndex = indexToUse;
            targetBounds = cellBoundsToUse;
            setVisible (true);
            setInterceptsMouseClicks (true, true);
            toFront (false);
            grabKeyboardFocus();
            repaint();
        }

        void hide()
        {
            if (! isVisible())
                return;

            setVisible (false);
            setInterceptsMouseClicks (false, false);
            targetIndex = -1;
            targetBounds = {};
        }

        bool isShowing() const
        {
            return isVisible() && targetIndex >= 0;
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
            {
                dismiss();
                return true;
            }
            return false;
        }

        void mouseDown (const juce::MouseEvent& event) override
        {
            if (! isShowing())
                return;

            const auto position = event.getPosition();
            if (! targetBounds.contains (position))
            {
                dismiss();
                return;
            }

            const int index = getActionIndexForPosition (position);

            if (actionHandler != nullptr && targetIndex >= 0)
                actionHandler (actionFromIndex (index), targetIndex);
        }

        void mouseMove (const juce::MouseEvent& event) override
        {
            if (! isShowing())
                return;

            const auto position = event.getPosition();
            const int nextIndex = targetBounds.contains (position)
                                      ? getActionIndexForPosition (position)
                                      : -1;
            if (nextIndex != hoveredActionIndex)
            {
                hoveredActionIndex = nextIndex;
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hoveredActionIndex != -1)
            {
                hoveredActionIndex = -1;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            if (! isShowing())
                return;

            const auto bounds = targetBounds;
            g.setColour (juce::Colour (0x802b2b2b));
            g.fillRect (bounds);

            g.setColour (juce::Colour (0xffcfcfcf));
            g.drawRect (bounds, 1);

            const int cols = 3;
            const int rows = 2;
            const int cellW = bounds.getWidth() / cols;
            const int cellH = bounds.getHeight() / rows;
            for (int row = 0; row < rows; ++row)
            {
                for (int col = 0; col < cols; ++col)
                {
                    const int actionIndex = row * cols + col;
                    juce::Rectangle<int> cell (bounds.getX() + col * cellW,
                                               bounds.getY() + row * cellH,
                                               cellW,
                                               cellH);
                    g.setColour (juce::Colour (0xff3d3d3d));
                    if (actionIndex == hoveredActionIndex)
                        g.setColour (juce::Colour (0xff3d3d3d).interpolatedWith (juce::Colours::white, 0.2f));
                    g.fillRect (cell);
                    g.setColour (juce::Colour (0xff3d3d3d));
                    g.drawRect (cell, 1);
                    const auto iconBounds = cell.reduced (10);
                    const auto& icon = iconDrawables[static_cast<size_t> (actionIndex)];
                    if (icon != nullptr)
                        icon->drawWithin (g,
                                          iconBounds.toFloat(),
                                          juce::RectanglePlacement::centred,
                                          1.0f);
                }
            }
        }

    private:
        void dismiss()
        {
            hide();
            if (dismissHandler != nullptr)
                dismissHandler();
        }

        static Action actionFromIndex (int index)
        {
            switch (index)
            {
                case 0: return Action::lock;
                case 1: return Action::remove;
                case 2: return Action::regen;
                case 3: return Action::swap;
                case 4: return Action::duplicate;
                case 5: return Action::reverse;
                default: return Action::lock;
            }
        }

        int getActionIndexForPosition (juce::Point<int> position) const
        {
            const auto localBounds = targetBounds;
            const int cols = 3;
            const int rows = 2;
            const int cellW = localBounds.getWidth() / cols;
            const int cellH = localBounds.getHeight() / rows;
            const int col = juce::jlimit (0, cols - 1, (position.x - localBounds.getX()) / cellW);
            const int row = juce::jlimit (0, rows - 1, (position.y - localBounds.getY()) / cellH);
            return row * cols + col;
        }

        static std::unique_ptr<juce::Drawable> createDrawableFromFile (const juce::File& file)
        {
            if (! file.existsAsFile())
                return nullptr;

            if (file.hasFileExtension ("svg"))
            {
                std::unique_ptr<juce::XmlElement> svgXml (juce::XmlDocument::parse (file));
                if (svgXml != nullptr)
                    return juce::Drawable::createFromSVG (*svgXml);
                return nullptr;
            }

            auto image = juce::ImageCache::getFromFile (file);
            if (image.isValid())
                return std::make_unique<juce::DrawableImage> (image);
            return nullptr;
        }

        static std::unique_ptr<juce::Drawable> createDrawableFromBinaryData (const juce::String& fileName)
        {
            juce::String resourceName = fileName.replaceCharacter ('.', '_');
            resourceName = resourceName.replaceCharacter ('-', '_');

            int dataSize = 0;
            const void* data = BinaryData::getNamedResource (resourceName.toRawUTF8(), dataSize);
            if (data == nullptr || dataSize <= 0)
                return nullptr;

            juce::MemoryInputStream stream (data, static_cast<size_t> (dataSize), false);
            if (fileName.endsWithIgnoreCase (".svg"))
            {
                const juce::String svgText = stream.readString();
                juce::XmlDocument svgDocument (svgText);
                std::unique_ptr<juce::XmlElement> svgXml (svgDocument.getDocumentElement());
                if (svgXml != nullptr)
                    return juce::Drawable::createFromSVG (*svgXml);
                return nullptr;
            }

            auto image = juce::ImageFileFormat::loadFrom (stream);
            if (image.isValid())
                return std::make_unique<juce::DrawableImage> (image);
            return nullptr;
        }

        std::function<void(Action, int)> actionHandler;
        std::function<void()> dismissHandler;
        int targetIndex = -1;
        int hoveredActionIndex = -1;
        juce::Rectangle<int> targetBounds;
        std::array<std::unique_ptr<juce::Drawable>, 6> iconDrawables;
    };

    class LiveModuleContainer final : public juce::Component
    {
    public:
        explicit LiveModuleContainer (AudioEngine& engineToUse)
            : audioEngine (engineToUse)
        {
            for (int index = 0; index < 4; ++index)
            {
                auto slot = std::make_unique<LiveModuleSlot> (audioEngine, index);
                slot->setPlaceholderClickHandler ([this, slotPtr = slot.get()]()
                {
                    setSlotEnabled (*slotPtr, true);
                    if (moduleEnabledCallback)
                        moduleEnabledCallback();
                });
                slot->setDeleteModuleHandler ([this, slotPtr = slot.get()]()
                {
                    setSlotEnabled (*slotPtr, false);
                });
                addAndMakeVisible (*slot);
                slots.add (std::move (slot));
            }

            restoreSlotState();
        }

        void setModuleEnabledCallback (std::function<void()> handler)
        {
            moduleEnabledCallback = std::move (handler);
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            const int spacing = 3;
            const int slotWidth =
                (bounds.getWidth() - spacing * 3) / 4;
            const int slotHeight = bounds.getHeight();
            auto startX = bounds.getX();
            auto y = bounds.getY();

            for (int index = 0; index < slots.size(); ++index)
            {
                slots[index]->setBounds (startX, y, slotWidth, slotHeight);
                startX += slotWidth + spacing;
            }
        }

    private:
        juce::String slotKey (int index) const
        {
            return "liveModuleEnabled_" + juce::String (index);
        }

        void persistSlotState (int index, bool enabled)
        {
            auto& props = AppProperties::get().properties();
            if (auto* settings = props.getUserSettings())
            {
                settings->setValue (slotKey (index), enabled);
                props.saveIfNeeded();
            }
        }

        void restoreSlotState()
        {
            auto& props = AppProperties::get().properties();
            auto* settings = props.getUserSettings();

            for (auto* slot : slots)
            {
                const int index = slot->getModuleIndex();
                const bool storedEnabled = settings != nullptr
                                               ? settings->getBoolValue (slotKey (index), false)
                                               : false;
                const auto recorderFile = RecordingModule::getRecorderFile (index);
                const bool shouldEnable = storedEnabled || recorderFile.existsAsFile();
                if (shouldEnable)
                    setSlotEnabled (*slot, true);
            }
        }

        void setSlotEnabled (LiveModuleSlot& slot, bool enabled)
        {
            slot.setEnabled (enabled);
            persistSlotState (slot.getModuleIndex(), enabled);
        }

        AudioEngine& audioEngine;
        juce::OwnedArray<LiveModuleSlot> slots;
        std::function<void()> moduleEnabledCallback;
    };

    class FocusPreviewArea final : public juce::Component,
                                   private juce::ChangeListener
    {
    public:
        static constexpr double kTargetSampleRate = 44100.0;

        FocusPreviewArea()
            : thumbnail (512, formatManager, thumbnailCache)
        {
            formatManager.registerBasicFormats();
            thumbnail.addChangeListener (this);
        }

        ~FocusPreviewArea() override
        {
            thumbnail.removeChangeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::darkgrey);

            if (thumbnail.getTotalLength() > 0.0)
            {
                g.setColour (juce::Colours::lightgrey);
                const double effectiveLength = displayLengthSeconds > 0.0
                                                   ? juce::jmin (displayLengthSeconds, thumbnail.getTotalLength())
                                                   : thumbnail.getTotalLength();
                thumbnail.drawChannels (g, getLocalBounds().reduced (6), 0.0, effectiveLength, 1.0f);
                return;
            }

            g.setColour (juce::Colours::grey);
            g.drawFittedText ("NO SLICE SELECTED",
                              getLocalBounds().reduced (6),
                              juce::Justification::centred,
                              1);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (onClick != nullptr)
                onClick();
        }

        void setClickHandler (std::function<void()> handler)
        {
            onClick = std::move (handler);
        }

        void setSourceFile (const juce::File& file, double durationSeconds = 0.0)
        {
            currentFile = file;
            thumbnail.clear();
            displayLengthSeconds = durationSeconds;

            if (currentFile.existsAsFile())
                thumbnail.setSource (new juce::FileInputSource (currentFile));

            repaint();
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            repaint();
        }

        juce::AudioFormatManager formatManager;
        juce::AudioThumbnailCache thumbnailCache { 8 };
        juce::AudioThumbnail thumbnail;
        juce::File currentFile;
        double displayLengthSeconds = 0.0;
        std::function<void()> onClick;
    };

    class GreyPlaceholder final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::darkgrey);
        }
    };

    class GridCell final : public juce::Component,
                           private juce::ChangeListener
    {
    public:
        GridCell (int indexToDraw,
                  juce::AudioFormatManager& formatManager,
                  juce::AudioThumbnailCache& thumbnailCache,
                  std::function<void(int)> clickHandler)
            : index (indexToDraw),
              thumbnail (64, formatManager, thumbnailCache),
              onClick (std::move (clickHandler))
        {
            thumbnail.addChangeListener (this);
        }

        ~GridCell() override
        {
            thumbnail.removeChangeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::darkgrey);
            g.setColour (juce::Colours::grey);
            g.drawRect (getLocalBounds(), 1);

            if (thumbnail.getTotalLength() > 0.0)
            {
                g.setColour (juce::Colours::lightgrey);
                thumbnail.drawChannels (g, getLocalBounds().reduced (4), 0.0, thumbnail.getTotalLength(), 1.0f);
                return;
            }

            g.setColour (juce::Colours::grey);
            g.drawFittedText ("EMPTY",
                              getLocalBounds().reduced (4),
                              juce::Justification::centred,
                              1);
        }

        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu())
            {
                suppressClick = true;
                if (onRightClick != nullptr)
                    onRightClick (index);
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (suppressClick)
            {
                suppressClick = false;
                return;
            }

            if (onClick != nullptr)
                onClick (index);
        }

        void setClickHandler (std::function<void(int)> handler)
        {
            onClick = std::move (handler);
        }

        void setRightClickHandler (std::function<void(int)> handler)
        {
            onRightClick = std::move (handler);
        }

        void setSourceFile (const juce::File& file)
        {
            currentFile = file;
            thumbnail.clear();

            if (currentFile.existsAsFile())
                thumbnail.setSource (new juce::FileInputSource (currentFile));

            repaint();
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            repaint();
        }

        int index = 0;
        juce::AudioThumbnail thumbnail;
        juce::File currentFile;
        std::function<void(int)> onClick;
        std::function<void(int)> onRightClick;
        bool suppressClick = false;
    };

    class PreviewGrid final : public juce::Component
    {
    public:
        PreviewGrid()
        {
            formatManager.registerBasicFormats();
            for (int index = 0; index < totalCells; ++index)
            {
                auto cell = std::make_unique<GridCell> (index, formatManager, thumbnailCache, nullptr);
                addAndMakeVisible (*cell);
                cells.add (std::move (cell));
            }
        }

        void setCellClickHandler (std::function<void(int)> handler)
        {
            for (auto* cell : cells)
                cell->setClickHandler (handler);
        }

        void setCellRightClickHandler (std::function<void(int)> handler)
        {
            for (auto* cell : cells)
                cell->setRightClickHandler (handler);
        }

        void setSliceFiles (const std::vector<juce::File>& files)
        {
            thumbnailCache.clear();
            for (int index = 0; index < totalCells; ++index)
            {
                if (index < static_cast<int> (files.size()))
                    cells[index]->setSourceFile (files[static_cast<std::size_t> (index)]);
                else
                    cells[index]->setSourceFile (juce::File());
            }
        }

        juce::Rectangle<int> getCellBounds (int index) const
        {
            if (index < 0 || index >= cells.size())
                return {};

            return cells[index]->getBounds();
        }

        void resized() override
        {
            for (int row = 0; row < rows; ++row)
            {
                for (int col = 0; col < columns; ++col)
                {
                    const int index = row * columns + col;
                    const int x = col * (cellW + spacing);
                    const int y = row * (cellH + spacing);
                    cells[index]->setBounds (x, y, cellW, cellH);
                }
            }
        }

    private:
        static constexpr int columns = 4;
        static constexpr int rows = 4;
        static constexpr int totalCells = columns * rows;
        static constexpr int cellW = 150;
        static constexpr int cellH = 64;
        static constexpr int spacing = 3;

        juce::AudioFormatManager formatManager;
        juce::AudioThumbnailCache thumbnailCache { 32 };
        juce::OwnedArray<GridCell> cells;
    };

    class ActionBar final : public juce::Component
    {
    public:
        ActionBar()
        {
            configureButton (sliceAllButton, "SLICE ALL");
            configureButton (modAllButton, "MOD ALL");
            configureButton (jumbleAllButton, "JUMBLE ALL");
            configureButton (resliceAllButton, "RESLICE ALL");
            configureButton (exportButton, "EXPORT");
            configureButton (lockButton, "🔒");
            configureButton (loopButton, "LOOP");

            loopButton.setClickingTogglesState (true);

            buttons.add (&sliceAllButton);
            buttons.add (&modAllButton);
            buttons.add (&jumbleAllButton);
            buttons.add (&resliceAllButton);
            buttons.add (&exportButton);
            buttons.add (&lockButton);
            buttons.add (&loopButton);

            for (auto* button : buttons)
            {
                button->setLookAndFeel (&compactLookAndFeel);
                addAndMakeVisible (*button);
            }
        }

        ~ActionBar() override
        {
            for (auto* button : buttons)
                button->setLookAndFeel (nullptr);
        }

        void setSliceAllHandler (std::function<void()> handler)
        {
            sliceAllButton.onClick = std::move (handler);
        }

        void setExportHandler (std::function<void()> handler)
        {
            exportButton.onClick = std::move (handler);
        }

        void setLoopHandler (std::function<void(bool)> handler)
        {
            loopButton.onClick = [this, handler = std::move (handler)]()
            {
                handler (loopButton.getToggleState());
            };
        }

        void setLoopState (bool isEnabled)
        {
            loopButton.setToggleState (isEnabled, juce::dontSendNotification);
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            const int spacing = 5;
            layoutButtons (bounds, spacing);
        }

    private:
        class CompactLookAndFeel final : public juce::LookAndFeel_V4
        {
        public:
            juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
            {
                return juce::Font (juce::FontOptions ("Helvetica",
                                                      juce::jmin (11.0f, buttonHeight * 0.5f),
                                                      juce::Font::plain));
            }

            void drawButtonBackground (juce::Graphics& g,
                                       juce::Button& button,
                                       const juce::Colour&,
                                       bool,
                                       bool) override
            {
                const auto bounds = button.getLocalBounds().toFloat();
                const auto baseColour = button.findColour (button.getToggleState()
                                                               ? juce::TextButton::buttonOnColourId
                                                               : juce::TextButton::buttonColourId);
                g.setColour (baseColour);
                g.fillRect (bounds);
                g.setColour (juce::Colour (0xff333333));
                g.drawRect (bounds, 1.0f);
            }

            void drawButtonText (juce::Graphics& g,
                                 juce::TextButton& button,
                                 bool,
                                 bool) override
            {
                g.setFont (getTextButtonFont (button, button.getHeight()));
                g.setColour (button.findColour (button.getToggleState()
                                                    ? juce::TextButton::textColourOnId
                                                    : juce::TextButton::textColourOffId));
                const auto textBounds = button.getLocalBounds().reduced (2, 1);
                g.drawFittedText (button.getButtonText(),
                                  textBounds,
                                  juce::Justification::centred,
                                  1);
            }
        };

        void layoutButtons (juce::Rectangle<int> bounds, int spacing)
        {
            const int buttonCount = buttons.size();
            if (buttonCount == 0)
                return;

            const int availableWidth = bounds.getWidth() - spacing * (buttonCount - 1);
            int totalBestWidth = 0;
            for (auto* button : buttons)
                totalBestWidth += button->getBestWidthForHeight (bounds.getHeight());

            const float scale = totalBestWidth > 0
                                    ? juce::jmin (1.0f, static_cast<float> (availableWidth) / static_cast<float> (totalBestWidth))
                                    : 1.0f;

            for (int index = 0; index < buttonCount; ++index)
            {
                auto* button = buttons[index];
                const int bestWidth = button->getBestWidthForHeight (bounds.getHeight());
                int width = static_cast<int> (std::floor (bestWidth * scale));
                if (index == buttonCount - 1)
                    width = bounds.getWidth();
                button->setBounds (bounds.removeFromLeft (width));
                if (index < buttonCount - 1)
                    bounds.removeFromLeft (spacing);
            }
        }

        void configureButton (juce::TextButton& button, const juce::String& text)
        {
            button.setButtonText (text);
            button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff5a5a5a));
            button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4fa3f7));
            button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcfcfcf));
            button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        }

        CompactLookAndFeel compactLookAndFeel;
        juce::TextButton sliceAllButton;
        juce::TextButton modAllButton;
        juce::TextButton jumbleAllButton;
        juce::TextButton resliceAllButton;
        juce::TextButton exportButton;
        juce::TextButton lockButton;
        juce::TextButton loopButton;
        juce::Array<juce::TextButton*> buttons;
    };

    class StatusArea final : public juce::Component
    {
    public:
        StatusArea()
        {
            statusLabel.setText ("PREVIEW GENERATED.", juce::dontSendNotification);
            statusLabel.setJustificationType (juce::Justification::centred);
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcfcfcf));
            addAndMakeVisible (statusLabel);
        }

        void setStatusText (const juce::String& text)
        {
            statusLabel.setText (text, juce::dontSendNotification);
        }

        void setProgress (float progress)
        {
            progressValue = juce::jlimit (0.0f, 1.0f, progress);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff444444));
            g.setColour (juce::Colour (0xffd9534f));
            const float width = static_cast<float> (getWidth());
            const float progressWidth = width * progressValue;
            g.drawLine (0.0f, 0.0f, progressWidth, 0.0f, 1.0f);
        }

        void resized() override
        {
            statusLabel.setBounds (0, 4, getWidth(), getHeight() - 4);
        }

    private:
        juce::Label statusLabel;
        float progressValue = 0.0f;
    };

    class PersistentFrame final : public juce::Component,
                                  private juce::ChangeListener
    {
    public:
        PersistentFrame (juce::TabbedComponent& tabsToTrack,
                         SliceStateStore& stateStoreToUse,
                         AudioEngine& audioEngineToUse,
                         PreviewChainPlayer& previewPlayerToUse)
            : tabs (tabsToTrack),
              stateStore (stateStoreToUse),
              audioEngine (audioEngineToUse),
              previewPlayer (previewPlayerToUse)
        {
            addAndMakeVisible (focusPlaceholder);
            addAndMakeVisible (grid);
            addChildComponent (contextOverlay);
            actionBar = std::make_unique<ActionBar>();
            statusArea = std::make_unique<StatusArea>();
            addAndMakeVisible (*actionBar);
            addAndMakeVisible (*statusArea);
            tabs.getTabbedButtonBar().addChangeListener (this);

            // Settings bypass path: hide frame while Settings tab is active.
            setVisible (tabs.getCurrentTabName() != "SETTINGS");

            if (auto* bar = dynamic_cast<ActionBar*> (actionBar.get()))
            {
                bar->setLoopState (previewPlayer.isLooping());
                bar->setLoopHandler ([this, bar] (bool isLooping)
                {
                    if (stateStore.isCaching())
                    {
                        setStatusText ("Cannot loop during caching.");
                        previewPlayer.setLooping (false);
                        bar->setLoopState (false);
                        return;
                    }

                    if (isLooping)
                    {
                        PreviewChainOrchestrator previewChain (stateStore);
                        if (! previewChain.rebuildLoopChainWithVolume())
                        {
                            setStatusText ("Preview loop failed.");
                            previewPlayer.setLooping (false);
                            bar->setLoopState (false);
                            return;
                        }

                        const auto snapshot = stateStore.getSnapshot();
                        if (! snapshot.previewChainURL.existsAsFile())
                        {
                            setStatusText ("No preview chain available.");
                            previewPlayer.setLooping (false);
                            bar->setLoopState (false);
                            return;
                        }
                        previewPlayer.setLooping (true);
                        if (! previewPlayer.startPlayback (snapshot.previewChainURL, true))
                        {
                            setStatusText ("Preview loop failed.");
                            previewPlayer.setLooping (false);
                            bar->setLoopState (false);
                            return;
                        }

                        setStatusText ("Preview looping.");
                        return;
                    }

                    previewPlayer.setLooping (false);
                    previewPlayer.stopPlayback();
                    setStatusText ("Preview loop stopped.");
                });
                bar->setSliceAllHandler ([this]()
                {
                    if (stateStore.isCaching())
                    {
                        setStatusText ("Cannot slice during caching.");
                        return;
                    }

                    MutationOrchestrator orchestrator (stateStore, &audioEngine);
                    setStatusText ("Slicing...");

                    if (! orchestrator.requestSliceAll())
                    {
                        setStatusText ("Slice all failed.");
                        return;
                    }

                    const auto snapshot = stateStore.getSnapshot();
                    if (! snapshot.previewSnippetURLs.empty())
                    {
                        focusedSliceIndex = 0;
                        double durationSeconds = 0.0;
                        if (! snapshot.sliceInfos.empty())
                        {
                            durationSeconds = static_cast<double> (snapshot.sliceInfos.front().snippetFrameCount)
                                              / FocusPreviewArea::kTargetSampleRate;
                        }
                        focusPlaceholder.setSourceFile (snapshot.previewSnippetURLs.front(), durationSeconds);
                        grid.setSliceFiles (snapshot.previewSnippetURLs);
                    }

                    setStatusText ("Slice all complete.");
                });
                bar->setExportHandler ([this]()
                {
                    const auto options = promptExportOptions();
                    if (! options.has_value())
                    {
                        setStatusText ("Export cancelled.");
                        return;
                    }

                    if (! options->generateIndividual && ! options->generateChain)
                    {
                        setStatusText ("No export options selected.");
                        return;
                    }

                    auto* settings = AppProperties::get().properties().getUserSettings();
                    const juce::String lastDirectory =
                        settings != nullptr ? settings->getValue ("LastExportDirectory") : juce::String();
                    const juce::File defaultDirectory = lastDirectory.isNotEmpty()
                                                            ? juce::File (lastDirectory)
                                                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

                    exportChooser = std::make_unique<juce::FileChooser> ("Select Export Folder",
                                                                         defaultDirectory,
                                                                         "*");
                    const int flags = juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectDirectories;
                    exportChooser->launchAsync (flags, [this, options] (const juce::FileChooser& chooser)
                    {
                        const juce::File exportDirectory = chooser.getResult();
                        if (! exportDirectory.exists())
                        {
                            setStatusText ("No export directory selected.");
                            return;
                        }

                        auto* settingsInner = AppProperties::get().properties().getUserSettings();
                        if (settingsInner != nullptr)
                        {
                            settingsInner->setValue ("LastExportDirectory", exportDirectory.getFullPathName());
                            settingsInner->setValue ("LastExportPrefix", options->exportPrefix);
                            settingsInner->setValue ("LastGenerateIndividual", options->generateIndividual);
                            settingsInner->setValue ("LastGenerateChain", options->generateChain);
                            AppProperties::get().properties().saveIfNeeded();
                        }

                        SliceStateStore::ExportSettings exportSettings;
                        exportSettings.exportDirectory = exportDirectory;
                        exportSettings.exportPrefix = options->exportPrefix;
                        exportSettings.generateIndividual = options->generateIndividual;
                        exportSettings.generateChain = options->generateChain;

                        MutationOrchestrator orchestrator (stateStore, &audioEngine);
                        bool exportOk = false;

                        if (options->generateIndividual)
                            exportOk |= orchestrator.requestExportSlices (exportSettings);

                        if (options->generateChain)
                            exportOk |= orchestrator.requestExportFullChainWithVolume (exportSettings);

                        setStatusText (exportOk ? "Export complete." : "Export failed.");
                        exportChooser.reset();
                    });
                });
            }

            focusPlaceholder.setClickHandler ([this]()
            {
                playFocusedSlice();
            });

            grid.setCellClickHandler ([this] (int index)
            {
                focusedSliceIndex = index;
                contextOverlay.hide();
                const auto snapshot = stateStore.getSnapshot();
                if (index >= 0 && index < static_cast<int> (snapshot.previewSnippetURLs.size()))
                {
                    double durationSeconds = 0.0;
                    if (index < static_cast<int> (snapshot.sliceInfos.size()))
                    {
                        durationSeconds =
                            static_cast<double> (snapshot.sliceInfos[static_cast<std::size_t> (index)].snippetFrameCount)
                            / FocusPreviewArea::kTargetSampleRate;
                    }
                    focusPlaceholder.setSourceFile (snapshot.previewSnippetURLs[static_cast<std::size_t> (index)],
                                                    durationSeconds);
                }
                playSliceAtIndex (index);
            });

            grid.setCellRightClickHandler ([this] (int index)
            {
                const auto bounds = grid.getCellBounds (index);
                if (bounds.isEmpty())
                    return;

                contextOverlay.setBounds (grid.getBounds());
                contextOverlay.showForCell (index, bounds);
            });

            auto resolveIconFile = [] (const juce::String& fileName)
            {
                const auto workingDir = juce::File::getCurrentWorkingDirectory();
                const auto appDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                                        .getParentDirectory();
                const std::array<juce::File, 6> roots = {
                    workingDir.getChildFile ("Assets"),
                    workingDir.getChildFile ("Source").getChildFile ("Assets"),
                    appDir.getChildFile ("Assets"),
                    appDir.getChildFile ("Resources"),
                    appDir.getChildFile ("Resources").getChildFile ("Assets"),
                    appDir.getChildFile ("..").getChildFile ("Assets")
                };

                for (const auto& root : roots)
                {
                    const auto candidate = root.getChildFile (fileName);
                    if (candidate.existsAsFile())
                        return candidate;
                }

                return juce::File();
            };

            contextOverlay.setIconSources ({
                SliceContextOverlay::IconSource { "lock.svg", resolveIconFile ("lock.svg") },
                SliceContextOverlay::IconSource { "delete.svg", resolveIconFile ("delete.svg") },
                SliceContextOverlay::IconSource { "regen.svg", resolveIconFile ("regen.svg") },
                SliceContextOverlay::IconSource { "swap.svg", resolveIconFile ("swap.svg") },
                SliceContextOverlay::IconSource { "duplicate.svg", resolveIconFile ("duplicate.svg") },
                SliceContextOverlay::IconSource { "reverse.svg", resolveIconFile ("reverse.svg") }
            });

            contextOverlay.setActionHandler ([this] (SliceContextOverlay::Action action, int index)
            {
                juce::String actionLabel;
                switch (action)
                {
                    case SliceContextOverlay::Action::lock: actionLabel = "Lock"; break;
                    case SliceContextOverlay::Action::remove: actionLabel = "Delete"; break;
                    case SliceContextOverlay::Action::regen: actionLabel = "Regen"; break;
                    case SliceContextOverlay::Action::swap: actionLabel = "Swap"; break;
                    case SliceContextOverlay::Action::duplicate: actionLabel = "Duplicate"; break;
                    case SliceContextOverlay::Action::reverse: actionLabel = "Reverse"; break;
                    default: actionLabel = "Action"; break;
                }
                setStatusText (actionLabel + " selected on slice " + juce::String (index + 1) + ".");
                contextOverlay.hide();
            });

            contextOverlay.setDismissHandler ([this]()
            {
                setStatusText ("Context menu dismissed.");
            });
        }

        ~PersistentFrame() override
        {
            tabs.getTabbedButtonBar().removeChangeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff7a7a7a));
        }

        void resized() override
        {
            const int focusW = 609;
            const int focusH = 96;
            const int gridW = 609;
            const int gridH = 4 * 64 + 3 * 3;
            const int spacing = 6;
            const int actionBarH = 28;
            const int statusH = 24;

            int y = 5;
            focusPlaceholder.setBounds (0, y, focusW, focusH);
            y += focusH + spacing;
            grid.setBounds (0, y, gridW, gridH);
            contextOverlay.setBounds (grid.getBounds());
            y += gridH + spacing;

            if (actionBar != nullptr)
                actionBar->setBounds (0, y, gridW, actionBarH);
            y += actionBarH + 8;
            if (statusArea != nullptr)
                statusArea->setBounds (0, y, gridW, statusH);
        }

        void setStatusText (const juce::String& text)
        {
            if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
                status->setStatusText (text);
        }

        void setProgress (float progress)
        {
            if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
                status->setProgress (progress);
        }

    private:
        void playFocusedSlice()
        {
            if (focusedSliceIndex < 0)
            {
                setStatusText ("No focused slice selected.");
                return;
            }

            playSliceAtIndex (focusedSliceIndex);
        }

        void playSliceAtIndex (int index)
        {
            const auto snapshot = stateStore.getSnapshot();
            if (index < 0 || index >= static_cast<int> (snapshot.previewSnippetURLs.size()))
            {
                setStatusText ("No preview slice available.");
                return;
            }

            const auto& snippetFile = snapshot.previewSnippetURLs[static_cast<std::size_t> (index)];
            if (! snippetFile.existsAsFile())
            {
                setStatusText ("Preview slice missing.");
                return;
            }

            if (previewPlayer.isLooping())
            {
                previewPlayer.setLooping (false);
                previewPlayer.stopPlayback();
                if (auto* bar = dynamic_cast<ActionBar*> (actionBar.get()))
                    bar->setLoopState (false);
            }

            if (! previewPlayer.startPlayback (snippetFile, false))
            {
                setStatusText ("Preview slice playback failed.");
                return;
            }

            setStatusText ("Preview slice playing.");
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            // Settings bypass path: hide frame while Settings tab is active.
            setVisible (tabs.getCurrentTabName() != "SETTINGS");
        }

        juce::TabbedComponent& tabs;
        SliceStateStore& stateStore;
        AudioEngine& audioEngine;
        PreviewChainPlayer& previewPlayer;
        FocusPreviewArea focusPlaceholder;
        PreviewGrid grid;
        SliceContextOverlay contextOverlay;
        std::unique_ptr<juce::Component> actionBar;
        std::unique_ptr<juce::Component> statusArea;
        std::unique_ptr<juce::FileChooser> exportChooser;
        SliceContextState sliceContextState;
        int focusedSliceIndex = -1;
    };

    class TabHeaderContainer final : public juce::Component,
                                     private juce::ChangeListener
    {
    public:
        TabHeaderContainer (juce::TabbedComponent& tabsToTrack,
                            SliceStateStore& stateStoreToUse,
                            MainTabView& mainTabViewToUse)
            : tabs (tabsToTrack),
              mainHeader (mainTabViewToUse),
              stateStore (stateStoreToUse),
              globalHeader (stateStoreToUse)
        {
            addAndMakeVisible (mainHeader);
            addAndMakeVisible (globalHeader);
            addAndMakeVisible (localHeader);
            addAndMakeVisible (liveHeader);

            tabs.getTabbedButtonBar().addChangeListener (this);
            updateVisibleHeader();
        }

        ~TabHeaderContainer() override
        {
            tabs.getTabbedButtonBar().removeChangeListener (this);
        }

        void setLiveContent (juce::Component* content)
        {
            liveContent = content;
            if (liveContent != nullptr)
                liveHeader.addAndMakeVisible (*liveContent);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::grey);
        }

        void resized() override
        {
            const auto bounds = getLocalBounds();
            auto paddedBounds = bounds;
            const int headerTopPadding = 16;
            const int headerBottomPadding = 10;
            paddedBounds.removeFromTop (headerTopPadding);
            paddedBounds.removeFromBottom (headerBottomPadding);

            mainHeader.setBounds (paddedBounds);
            globalHeader.setBounds (paddedBounds);
            localHeader.setBounds (paddedBounds);
            liveHeader.setBounds (bounds);
            if (liveContent != nullptr)
                liveContent->setBounds (liveHeader.getLocalBounds());
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            updateVisibleHeader();
        }

        void updateVisibleHeader()
        {
            const auto currentTab = tabs.getCurrentTabName();

            // Header region ownership (per-tab visibility).
            const bool showMain = currentTab == "MAIN";
            const bool showGlobal = currentTab == "GLOBAL";
            const bool showLocal = currentTab == "LOCAL";
            const bool showLive = currentTab == "LIVE";

            // Settings bypass path: hide header while Settings tab is active.
            setVisible (currentTab != "SETTINGS");

            // Centralized header visibility toggles.
            mainHeader.setVisible (showMain);
            globalHeader.setVisible (showGlobal);
            localHeader.setVisible (showLocal);
            liveHeader.setVisible (showLive);

            if (showGlobal)
                globalHeader.applySettingsSnapshot (stateStore.getSnapshot());
        }

        juce::TabbedComponent& tabs;
        MainTabView& mainHeader;
        SliceStateStore& stateStore;
        GlobalTabView globalHeader;
        GreyPlaceholder localHeader;
        juce::Component liveHeader;
        juce::Component* liveContent = nullptr;
    };

    class ContentArea final : public juce::Component,
                              private juce::ChangeListener
    {
    public:
        ContentArea (juce::TabbedComponent& tabsToTrack,
                     AudioEngine& audioEngineToUse,
                     SettingsView& settingsToUse,
                     SliceStateStore& stateStoreToUse,
                     PreviewChainPlayer& previewPlayerToUse,
                     juce::Component* liveContent)
            : tabs (tabsToTrack),
              audioEngine (audioEngineToUse),
              settingsView (settingsToUse),
              persistentFrame (tabsToTrack, stateStoreToUse, audioEngineToUse, previewPlayerToUse),
              mainTabView (stateStoreToUse),
              headerContainer (tabsToTrack, stateStoreToUse, mainTabView)
        {
            headerContainer.setLiveContent (liveContent);
            addAndMakeVisible (headerContainer);
            addAndMakeVisible (persistentFrame);
            addAndMakeVisible (settingsView);

            mainTabView.setStatusTextCallback ([this] (const juce::String& text)
            {
                persistentFrame.setStatusText (text);
            });
            mainTabView.setProgressCallback ([this] (float progress)
            {
                persistentFrame.setProgress (progress);
            });
            mainTabView.setBpmChangedCallback ([this] (double bpm)
            {
                audioEngine.setMidiSyncBpm (bpm);
                audioEngine.saveState();
            });
            audioEngine.setMidiSyncBpm (stateStoreToUse.getSnapshot().bpm);

            if (auto* liveContainer = dynamic_cast<LiveModuleContainer*> (liveContent))
            {
                liveContainer->setModuleEnabledCallback ([this]()
                {
                    mainTabView.setLiveModeSelected (true);
                });
            }

            tabs.getTabbedButtonBar().addChangeListener (this);
            updateVisibleContent();
        }

        ~ContentArea() override
        {
            tabs.getTabbedButtonBar().removeChangeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff7a7a7a));
        }

        void resized() override
        {
            const int headerH = 150;

            const int focusH = 96;
            const int gridH = 4 * 64 + 3 * 3;
            const int spacing = 6;
            const int actionBarH = 28;
            const int statusH = 24;
            const int frameH = focusH + spacing + gridH + spacing + actionBarH + 8 + statusH;

            const int headerTopPadding = 0;
            const int headerBottomPadding = 0;

            headerContainer.setBounds (
                0,
                headerTopPadding,
                609,
                headerH - headerTopPadding + headerBottomPadding
            );

            persistentFrame.setBounds (
                0,
                headerH,
                609,
                frameH
            );

            settingsView.setBounds (getLocalBounds());
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            updateVisibleContent();
        }

        void updateVisibleContent()
        {
            const auto currentTab = tabs.getCurrentTabName();
            const bool isSettings = currentTab == "SETTINGS";
            settingsView.setVisible (isSettings);
            headerContainer.setVisible (! isSettings);
            persistentFrame.setVisible (! isSettings);
        }

        juce::TabbedComponent& tabs;
        AudioEngine& audioEngine;
        SettingsView& settingsView;
        PersistentFrame persistentFrame;
        MainTabView mainTabView;
        TabHeaderContainer headerContainer;
    };
}

// =======================
// SETTINGS VIEW
// =======================

SettingsView::SettingsView (AudioEngine& engine)
    : audioEngine (engine)
{
    deviceSelector = std::unique_ptr<juce::AudioDeviceSelectorComponent> (
        new juce::AudioDeviceSelectorComponent (
            audioEngine.getDeviceManager(),
            0, 256,   // allow inputs
            0, 256,   // allow outputs
            false,
            false,
            false,
            false));

    syncModeBox.addItem ("Off", 1);
    syncModeBox.addItem ("Receive", 2);
    syncModeBox.addItem ("Send", 3);

    syncModeBox.onChange = [this]()
    {
        updateSyncModeSetting();
    };
    syncInputBox.onChange = [this]()
    {
        updateSyncInputSetting();
    };
    syncOutputBox.onChange = [this]()
    {
        updateSyncOutputSetting();
    };
    virtualPortsToggle.onClick = [this]()
    {
        updateVirtualPortsSetting();
    };

    addAndMakeVisible (*deviceSelector);
    addAndMakeVisible (midiSectionLabel);
    addAndMakeVisible (syncModeLabel);
    addAndMakeVisible (syncModeBox);
    addAndMakeVisible (syncInputLabel);
    addAndMakeVisible (syncInputBox);
    addAndMakeVisible (syncOutputLabel);
    addAndMakeVisible (syncOutputBox);
    addAndMakeVisible (virtualPortsToggle);

    refreshMidiDeviceLists();
    applyMidiSettings();
}

void SettingsView::resized()
{
    auto bounds = getLocalBounds().reduced (20);
    const int deviceHeight = juce::jmin (320, bounds.getHeight());
    auto deviceArea = bounds.removeFromTop (deviceHeight);
    deviceSelector->setBounds (deviceArea);

    bounds.removeFromTop (10);
    midiSectionLabel.setBounds (bounds.removeFromTop (24));

    auto row = bounds.removeFromTop (24);
    syncModeLabel.setBounds (row.removeFromLeft (140));
    syncModeBox.setBounds (row);

    bounds.removeFromTop (6);
    row = bounds.removeFromTop (24);
    syncInputLabel.setBounds (row.removeFromLeft (140));
    syncInputBox.setBounds (row);

    bounds.removeFromTop (6);
    row = bounds.removeFromTop (24);
    syncOutputLabel.setBounds (row.removeFromLeft (140));
    syncOutputBox.setBounds (row);

    bounds.removeFromTop (6);
    virtualPortsToggle.setBounds (bounds.removeFromTop (24));
}

void SettingsView::refreshMidiDeviceLists()
{
    midiInputDevices = juce::MidiInput::getAvailableDevices();
    midiOutputDevices = juce::MidiOutput::getAvailableDevices();

    if (audioEngine.getMidiVirtualPortsEnabled())
    {
        midiInputDevices.add ({ kVirtualInName, kVirtualInIdentifier });
        midiOutputDevices.add ({ kVirtualOutName, kVirtualOutIdentifier });
    }

    syncInputBox.clear (juce::dontSendNotification);
    syncOutputBox.clear (juce::dontSendNotification);

    syncInputBox.addItem ("None", 1);
    syncOutputBox.addItem ("None", 1);

    int inputId = 2;
    for (const auto& device : midiInputDevices)
        syncInputBox.addItem (device.name, inputId++);

    int outputId = 2;
    for (const auto& device : midiOutputDevices)
        syncOutputBox.addItem (device.name, outputId++);
}

void SettingsView::applyMidiSettings()
{
    const auto mode = audioEngine.getMidiSyncMode();
    if (mode == AudioEngine::MidiSyncMode::receive)
        syncModeBox.setSelectedId (2, juce::dontSendNotification);
    else if (mode == AudioEngine::MidiSyncMode::send)
        syncModeBox.setSelectedId (3, juce::dontSendNotification);
    else
        syncModeBox.setSelectedId (1, juce::dontSendNotification);

    syncInputBox.setSelectedId (1, juce::dontSendNotification);
    const auto inputIdentifier = audioEngine.getMidiSyncInputDeviceIdentifier();
    for (int index = 0; index < midiInputDevices.size(); ++index)
    {
        if (midiInputDevices.getReference (index).identifier == inputIdentifier)
        {
            syncInputBox.setSelectedId (index + 2, juce::dontSendNotification);
            break;
        }
    }

    syncOutputBox.setSelectedId (1, juce::dontSendNotification);
    const auto outputIdentifier = audioEngine.getMidiSyncOutputDeviceIdentifier();
    for (int index = 0; index < midiOutputDevices.size(); ++index)
    {
        if (midiOutputDevices.getReference (index).identifier == outputIdentifier)
        {
            syncOutputBox.setSelectedId (index + 2, juce::dontSendNotification);
            break;
        }
    }

    virtualPortsToggle.setToggleState (audioEngine.getMidiVirtualPortsEnabled(), juce::dontSendNotification);
}

void SettingsView::updateSyncModeSetting()
{
    const int selected = syncModeBox.getSelectedId();
    AudioEngine::MidiSyncMode mode = AudioEngine::MidiSyncMode::off;
    if (selected == 2)
        mode = AudioEngine::MidiSyncMode::receive;
    else if (selected == 3)
        mode = AudioEngine::MidiSyncMode::send;

    audioEngine.setMidiSyncMode (mode);
    audioEngine.saveState();
}

void SettingsView::updateSyncInputSetting()
{
    const int selected = syncInputBox.getSelectedId();
    if (selected <= 1)
    {
        audioEngine.setMidiSyncInputDeviceIdentifier ({});
    }
    else
    {
        const int index = selected - 2;
        if (index >= 0 && index < midiInputDevices.size())
            audioEngine.setMidiSyncInputDeviceIdentifier (midiInputDevices.getReference (index).identifier);
    }

    audioEngine.saveState();
}

void SettingsView::updateSyncOutputSetting()
{
    const int selected = syncOutputBox.getSelectedId();
    if (selected <= 1)
    {
        audioEngine.setMidiSyncOutputDeviceIdentifier ({});
    }
    else
    {
        const int index = selected - 2;
        if (index >= 0 && index < midiOutputDevices.size())
            audioEngine.setMidiSyncOutputDeviceIdentifier (midiOutputDevices.getReference (index).identifier);
    }

    audioEngine.saveState();
}

void SettingsView::updateVirtualPortsSetting()
{
    audioEngine.setMidiVirtualPortsEnabled (virtualPortsToggle.getToggleState());
    if (! audioEngine.getMidiVirtualPortsEnabled())
    {
        if (audioEngine.getMidiSyncInputDeviceIdentifier() == kVirtualInIdentifier)
            audioEngine.setMidiSyncInputDeviceIdentifier ({});
        if (audioEngine.getMidiSyncOutputDeviceIdentifier() == kVirtualOutIdentifier)
            audioEngine.setMidiSyncOutputDeviceIdentifier ({});
    }
    refreshMidiDeviceLists();
    applyMidiSettings();
    audioEngine.saveState();
}

// =======================
// MAIN COMPONENT
// =======================

MainComponent::MainComponent (AudioEngine& engine)
    : audioEngine (engine),
      settingsView (engine),
      previewChainPlayer (engine.getDeviceManager())
{
    liveModuleContainer = std::make_unique<LiveModuleContainer> (engine);

    tabs.addTab ("MAIN",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("GLOBAL",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("LOCAL",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("LIVE",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("SETTINGS",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    addAndMakeVisible (tabs);

    auto* contentArea = new ContentArea (tabs,
                                         audioEngine,
                                         settingsView,
                                         stateStore,
                                         previewChainPlayer,
                                         liveModuleContainer.get());
    contentArea->setComponentID ("contentArea");
    addAndMakeVisible (contentArea);
}

void MainComponent::visibilityChanged()
{
    if (! isVisible())
        return;

    stateStore.setCacheData (AudioCacheStore::load());
}

void MainComponent::resized()
{
    const int tabStripH = 25;

    tabs.setTabBarDepth (tabStripH);
    tabs.setBounds (0, 0, getWidth(), tabStripH);

    if (auto* contentArea = findChildWithID ("contentArea"))
    {
        contentArea->setBounds (0, tabStripH, getWidth(), getHeight() - tabStripH);
    }
}
