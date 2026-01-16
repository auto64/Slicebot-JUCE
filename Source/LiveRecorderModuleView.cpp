#include "LiveRecorderModuleView.h"
#include "AudioEngine.h"
#include "RecordingModule.h"
#include "RecordingBus.h"
#include <cmath>
#include <utility>

static constexpr int kModuleW = 120;
static constexpr int kModuleH = 150;
static constexpr double kMinSeconds = 25.0;
static constexpr double kMaxRecordSeconds = 600.0;
static constexpr float kMinGainDb = -60.0f;
static constexpr float kMaxGainDb = 6.0f;

// =====================================================
// CONSTRUCTION
// =====================================================

LiveRecorderModuleView::LiveRecorderModuleView (AudioEngine& engine,
                                                int index)
    : audioEngine (engine),
      recorderIndex (index)
{
    setSize (kModuleW, kModuleH);

    // ================= MIDI IN/OUT =================
    addAndMakeVisible (midiInButton);
    addAndMakeVisible (midiOutButton);
    midiInButton.setClickingTogglesState (true);
    midiOutButton.setClickingTogglesState (true);
    midiInButton.setLookAndFeel (&flatTiles);
    midiOutButton.setLookAndFeel (&flatTiles);

    // ================= INPUT SELECT =================
    addAndMakeVisible (channelBox);
    channelBox.addListener (this);

    // ================= CONTROL BUTTONS =================
    addAndMakeVisible (recordArmButton);
    addAndMakeVisible (monitorButton);
    addAndMakeVisible (linkButton);
    addAndMakeVisible (lockButton);
    addAndMakeVisible (sliceButton);
    addAndMakeVisible (clearButton);

    recordArmButton.setClickingTogglesState (true);
    monitorButton.setClickingTogglesState (true);
    linkButton   .setClickingTogglesState (true);
    lockButton   .setClickingTogglesState (true);
    sliceButton  .setClickingTogglesState (true);

    sliceButton.setToggleState (true, juce::dontSendNotification);
    sliceButton.setButtonText (""); // tick drawn by LookAndFeel
    clearButton.setButtonText ("X");

    recordArmButton.addListener (this);
    monitorButton.addListener (this);
    linkButton   .addListener (this);
    lockButton   .addListener (this);
    sliceButton  .addListener (this);
    clearButton  .addListener (this);
    midiInButton .addListener (this);
    midiOutButton.addListener (this);

    monitorButton.setLookAndFeel (&flatTiles);
    linkButton   .setLookAndFeel (&flatTiles);
    recordArmButton.setLookAndFeel (&flatTiles);
    lockButton   .setLookAndFeel (&flatTiles);
    sliceButton  .setLookAndFeel (&flatTiles);
    clearButton  .setLookAndFeel (&flatTiles);

    // ================= RECORD BUTTON =================
    addAndMakeVisible (timeCounter);
    timeCounter.addListener (this);
    timeCounter.setLookAndFeel (&flatTiles);
    timeCounter.setButtonText ("00:00");
    timeCounter.setName ("RECORD_IDLE");

    applyPersistedControlState();
    refreshInputChannels();
    startTimerHz (8); // slower, visible flash
}

LiveRecorderModuleView::~LiveRecorderModuleView()
{
    midiInButton.setLookAndFeel (nullptr);
    midiOutButton.setLookAndFeel (nullptr);
    monitorButton.setLookAndFeel (nullptr);
    linkButton   .setLookAndFeel (nullptr);
    recordArmButton.setLookAndFeel (nullptr);
    lockButton   .setLookAndFeel (nullptr);
    sliceButton  .setLookAndFeel (nullptr);
    clearButton  .setLookAndFeel (nullptr);
    timeCounter  .setLookAndFeel (nullptr);
    stopTimer();
}

// =====================================================
// PAINT (BACKGROUND + VU ONLY)
// =====================================================

void LiveRecorderModuleView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff464646));

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1),
                            8.0f,
                            1.0f);

    g.setColour (juce::Colours::black);
    g.fillRect (meterBounds);

    g.setColour (juce::Colours::green);
    g.fillRect (meterBounds.getX(),
                meterBounds.getY(),
                int (meterBounds.getWidth() * juce::jlimit (0.0f, 1.0f, rms)),
                meterBounds.getHeight());

    const float gainPos =
        juce::jlimit (0.0f, 1.0f, gainPosition);
    const int lineX = meterBounds.getX() + int (meterBounds.getWidth() * gainPos);
    g.setColour (juce::Colours::white);
    g.drawLine (float (lineX),
                float (meterBounds.getY()),
                float (lineX),
                float (meterBounds.getBottom()),
                2.0f);

    const bool recordArmEnabled =
        audioEngine.isRecorderRecordArmEnabled (recorderIndex);
    const int totalSamples = audioEngine.getRecorderTotalSamples (recorderIndex);
    const int maxSamples = audioEngine.getRecorderMaxSamples (recorderIndex);

    double progress = 0.0;
    if (recordArmEnabled)
    {
        if (maxSamples > 0)
            progress = static_cast<double> (totalSamples) / static_cast<double> (maxSamples);
    }
    else
    {
        progress = audioEngine.getRecorderPlaybackProgress (recorderIndex);
    }

    if (progressBounds.getHeight() > 0)
    {
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillRect (progressBounds);
        g.setColour (juce::Colours::white);
        g.fillRect (progressBounds.withWidth (int (progressBounds.getWidth() * progress)));
    }
}

// =====================================================
// LAYOUT
// =====================================================

void LiveRecorderModuleView::resized()
{
    const int padding = 8;
    const int gap = 4;
    const int topY = 6;
    const int rowHeight = 18;

    const int contentWidth = getWidth() - padding * 2;
    const int leftX = padding;

    channelBox.setBounds (leftX, topY, contentWidth, rowHeight);

    const int buttonRowY = topY + rowHeight + gap;
    const int smallButtonSize = rowHeight;
    const int totalButtonWidth = smallButtonSize * 6 + gap * 5;
    const int buttonStartX = leftX + (contentWidth - totalButtonWidth) / 2;

    recordArmButton.setBounds (buttonStartX, buttonRowY, smallButtonSize, smallButtonSize);
    monitorButton.setBounds (buttonStartX + (smallButtonSize + gap), buttonRowY, smallButtonSize, smallButtonSize);
    linkButton.setBounds (buttonStartX + (smallButtonSize + gap) * 2, buttonRowY, smallButtonSize, smallButtonSize);
    lockButton.setBounds (buttonStartX + (smallButtonSize + gap) * 3, buttonRowY, smallButtonSize, smallButtonSize);
    sliceButton.setBounds (buttonStartX + (smallButtonSize + gap) * 4, buttonRowY, smallButtonSize, smallButtonSize);
    clearButton.setBounds (buttonStartX + (smallButtonSize + gap) * 5, buttonRowY, smallButtonSize, smallButtonSize);

    const int midiRowY = buttonRowY + smallButtonSize + gap;
    const int midiButtonWidth = (contentWidth - gap) / 2;
    midiInButton.setBounds (leftX, midiRowY, midiButtonWidth, rowHeight);
    midiOutButton.setBounds (leftX + midiButtonWidth + gap, midiRowY, midiButtonWidth, rowHeight);

    const int bigButtonY = midiRowY + rowHeight + gap;
    const int progressHeight = 7;
    const int meterHeight = 12;
    const int meterY = getHeight() - padding - meterHeight;
    const int progressY = meterY - gap - progressHeight;
    const int bigButtonHeight = progressY - bigButtonY - gap;

    timeCounter.setBounds (leftX, bigButtonY, contentWidth, bigButtonHeight);
    progressBounds = { leftX, progressY, contentWidth, progressHeight };
    meterBounds = { leftX, meterY, contentWidth, meterHeight };
}

// =====================================================
// INPUT CHANNEL SYNC (UNCHANGED)
// =====================================================

void LiveRecorderModuleView::refreshInputChannels()
{
    const auto activeInputs = audioEngine.getActiveInputChannels();
    const int previousId    = channelBox.getSelectedId();

    channelBox.clear (juce::dontSendNotification);

    for (const auto& ch : activeInputs)
        channelBox.addItem (ch.name, ch.physicalIndex + 1);

    if (channelBox.getNumItems() == 0)
        return;

    int selectedId = 0;

    for (int i = 0; i < channelBox.getNumItems(); ++i)
    {
        const int id = channelBox.getItemId (i);
        if (id == previousId)
        {
            selectedId = id;
            break;
        }
    }

    if (selectedId == 0)
    {
        const int desiredChannel = audioEngine.getRecorderInputChannel (recorderIndex);
        if (desiredChannel >= 0)
        {
            for (int i = 0; i < channelBox.getNumItems(); ++i)
            {
                const int id = channelBox.getItemId (i);
                if (id == desiredChannel + 1)
                {
                    selectedId = id;
                    break;
                }
            }
        }

        if (selectedId == 0)
        {
            selectedId = channelBox.getItemId (0);
            for (int i = 1; i < channelBox.getNumItems(); ++i)
                selectedId = juce::jmin (selectedId,
                                         channelBox.getItemId (i));
        }
    }

    channelBox.setSelectedId (selectedId,
                              juce::dontSendNotification);

    audioEngine.setRecorderInputChannel (
        recorderIndex,
        selectedId - 1);
}

// =====================================================
// COMBO BOX CALLBACK
// =====================================================

void LiveRecorderModuleView::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &channelBox)
        return;

    if (audioEngine.isRecorderLocked (recorderIndex))
    {
        showLockedWarning();
        refreshInputChannels();
        return;
    }

    const int selectedId = channelBox.getSelectedId();
    if (selectedId <= 0)
        return;

    audioEngine.setRecorderInputChannel (
        recorderIndex,
        selectedId - 1);
}

// =====================================================
// BUTTON HANDLING (UNCHANGED)
// =====================================================

void LiveRecorderModuleView::buttonClicked (juce::Button* b)
{
    if (b == &lockButton)
    {
        audioEngine.setRecorderLocked (recorderIndex,
                                       lockButton.getToggleState());
        audioEngine.saveState();
        return;
    }

    if (b == &recordArmButton)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            applyPersistedControlState();
            return;
        }

        if (! recordArmButton.getToggleState()
            && audioEngine.isRecorderArmed (recorderIndex))
        {
            showRecordingInProgressWarning();
            applyPersistedControlState();
            return;
        }

        audioEngine.setRecorderRecordArmEnabled (
            recorderIndex,
            recordArmButton.getToggleState());
        if (recordArmButton.getToggleState())
            audioEngine.stopPlayback (recorderIndex);
        audioEngine.saveState();
        return;
    }

    if (b == &monitorButton)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            applyPersistedControlState();
            return;
        }

        audioEngine.setRecorderMonitoringEnabled (
            recorderIndex,
            monitorButton.getToggleState());
        return;
    }

    if (b == &midiInButton || b == &midiOutButton)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            applyPersistedControlState();
            return;
        }

        const bool enableMidi = b->getToggleState();
        if (b == &midiInButton)
            audioEngine.setRecorderMidiInEnabled (recorderIndex, enableMidi);
        else
            audioEngine.setRecorderMidiOutEnabled (recorderIndex, enableMidi);

        audioEngine.saveState();
        syncMidiButtonStates();
        return;
    }

    if (b == &linkButton)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            applyPersistedControlState();
            return;
        }

        audioEngine.setRecorderLatchEnabled (
            recorderIndex,
            linkButton.getToggleState());
        return;
    }

    if (b == &sliceButton)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            applyPersistedControlState();
            return;
        }

        audioEngine.setRecorderIncludeInGenerationEnabled (
            recorderIndex,
            sliceButton.getToggleState());
        return;
    }

    if (b == &clearButton)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            return;
        }

        showDeleteWarning();
        return;
    }

    if (b != &timeCounter || stopDialogOpen)
        return;

    const bool recordArmEnabled =
        audioEngine.isRecorderRecordArmEnabled (recorderIndex);
    const bool hasLatched = audioEngine.hasLatchedRecorders();

    if (! recordArmEnabled)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            return;
        }

        if (isPlaying)
        {
            if (hasLatched)
            {
                audioEngine.stopLatchedPlayback();
                for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
                    audioEngine.seekRecorderPlayback (index, 0.0);
            }
            else
            {
                audioEngine.stopPlayback (recorderIndex);
                audioEngine.seekRecorderPlayback (recorderIndex, 0.0);
            }
            return;
        }

        const bool started =
            hasLatched
                ? audioEngine.startLatchedPlayback()
                : audioEngine.startPlayback (recorderIndex);

        if (! started)
            showMissingRecordingWarning();
        return;
    }

    if (! isRecording)
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            return;
        }

        if (hasLatched)
            audioEngine.armLatchedRecorders();
        else
            audioEngine.armRecorder (recorderIndex);
        isRecording = true;
        recordingOffsetSeconds = lastRecordedSeconds;
        return;
    }

    const double secs =
        audioEngine.getRecorderCurrentPassSeconds (recorderIndex);

    if (secs < kMinSeconds)
    {
        showUnderMinWarning();
        return;
    }

    if (hasLatched)
    {
        const auto result = audioEngine.stopLatchedRecorders();
        if (result == RecordingModule::StopResult::Kept)
            audioEngine.playUiSound (AudioEngine::UiSound::Bleep);
    }
    else
    {
        const auto result = audioEngine.confirmStopRecorder (recorderIndex);
        if (result == RecordingModule::StopResult::Kept)
            audioEngine.playUiSound (AudioEngine::UiSound::Bleep);
    }
    isRecording = false;
    lastRecordedSeconds = recordingOffsetSeconds + secs;
}

// =====================================================
// WARNINGS (UNCHANGED)
// =====================================================

void LiveRecorderModuleView::showUnderMinWarning()
{
    stopDialogOpen = true;

    audioEngine.playUiSound (AudioEngine::UiSound::Cowbell);

    juce::AlertWindow::showOkCancelBox (
        juce::AlertWindow::WarningIcon,
        "Recording Too Short",
        "Minimum recording length is 25 seconds.",
        "STOP",
        "OK",
        this,
        juce::ModalCallbackFunction::create (
            [this] (int r)
            {
                stopDialogOpen = false;
                if (r == 1)
                {
                    audioEngine.confirmStopRecorder (recorderIndex);
                    isRecording = false;
                    timeCounter.setName ("RECORD_IDLE");
                }
            }));
}

void LiveRecorderModuleView::showDeleteWarning()
{
    juce::AlertWindow::showOkCancelBox (
        juce::AlertWindow::WarningIcon,
        "Delete Recorder",
        "Deleting recorder deletes temp file. Continue?",
        "Continue",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create (
            [this] (int r)
            {
                if (r != 1)
                    return;

                audioEngine.clearRecorder (recorderIndex);
                isRecording = false;
                recordingOffsetSeconds = 0.0;
                lastRecordedSeconds = 0.0;
                timeCounter.setButtonText ("00:00");
                timeCounter.setName ("RECORD_IDLE");
                applyPersistedControlState();

                if (deleteModuleHandler)
                    deleteModuleHandler();
            }));
}

void LiveRecorderModuleView::showLockedWarning()
{
    audioEngine.playUiSound (AudioEngine::UiSound::Cowbell);
    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::WarningIcon,
        "Locked",
        "This recorder is locked.");
}

void LiveRecorderModuleView::showMissingRecordingWarning()
{
    audioEngine.playUiSound (AudioEngine::UiSound::Cowbell);
    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::InfoIcon,
        "Nothing Recorded Yet",
        "No recording exists for this recorder yet.");
}

void LiveRecorderModuleView::showRecordingInProgressWarning()
{
    audioEngine.playUiSound (AudioEngine::UiSound::Cowbell);
    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::WarningIcon,
        "Recording In Progress",
        "Stop the current recording before switching to playback mode.");
}

void LiveRecorderModuleView::applyPersistedControlState()
{
    syncMidiButtonStates();
    recordArmButton.setToggleState (
        audioEngine.isRecorderRecordArmEnabled (recorderIndex),
        juce::dontSendNotification);
    monitorButton.setToggleState (
        audioEngine.isRecorderMonitoringEnabled (recorderIndex),
        juce::dontSendNotification);
    linkButton.setToggleState (
        audioEngine.isRecorderLatchEnabled (recorderIndex),
        juce::dontSendNotification);
    lockButton.setToggleState (
        audioEngine.isRecorderLocked (recorderIndex),
        juce::dontSendNotification);
    sliceButton.setToggleState (
        audioEngine.isRecorderIncludeInGenerationEnabled (recorderIndex),
        juce::dontSendNotification);
}

void LiveRecorderModuleView::syncMidiButtonStates()
{
    const bool midiInEnabled = audioEngine.isRecorderMidiInEnabled (recorderIndex);
    const bool midiOutEnabled = audioEngine.isRecorderMidiOutEnabled (recorderIndex);
    midiInButton.setToggleState (midiInEnabled, juce::dontSendNotification);
    midiOutButton.setToggleState (midiOutEnabled, juce::dontSendNotification);
}

void LiveRecorderModuleView::setDeleteModuleHandler (std::function<void()> handler)
{
    deleteModuleHandler = std::move (handler);
}

void LiveRecorderModuleView::mouseDown (const juce::MouseEvent& event)
{
    if (meterBounds.contains (event.getPosition()))
    {
        if (audioEngine.isRecorderLocked (recorderIndex))
        {
            showLockedWarning();
            return;
        }

        adjustingGain = true;
        const int x = event.getPosition().getX();
        const float progress = juce::jlimit (0.0f, 1.0f,
                                             float (x - meterBounds.getX())
                                             / float (meterBounds.getWidth()));
        const float gainDb = kMinGainDb + progress * (kMaxGainDb - kMinGainDb);
        audioEngine.setRecorderInputGainDb (recorderIndex, gainDb);
        audioEngine.saveState();
        return;
    }

    if (progressBounds.contains (event.getPosition()))
    {
        const bool recordArmEnabled =
            audioEngine.isRecorderRecordArmEnabled (recorderIndex);
        if (! recordArmEnabled)
        {
            const float progress = juce::jlimit (0.0f, 1.0f,
                                                 float (event.getPosition().getX() - progressBounds.getX())
                                                 / float (progressBounds.getWidth()));
            audioEngine.seekRecorderPlayback (recorderIndex, progress);
        }
    }
}

void LiveRecorderModuleView::mouseDrag (const juce::MouseEvent& event)
{
    if (! adjustingGain)
        return;

    if (audioEngine.isRecorderLocked (recorderIndex))
    {
        adjustingGain = false;
        return;
    }

    const int x = event.getPosition().getX();
    const float progress = juce::jlimit (0.0f, 1.0f,
                                         float (x - meterBounds.getX())
                                         / float (meterBounds.getWidth()));
    const float gainDb = kMinGainDb + progress * (kMaxGainDb - kMinGainDb);
    audioEngine.setRecorderInputGainDb (recorderIndex, gainDb);
}

void LiveRecorderModuleView::mouseUp (const juce::MouseEvent&)
{
    adjustingGain = false;
    audioEngine.saveState();
}

// =====================================================
// TIMER (ONLY SECTION THAT CHANGED MEANINGFULLY)
// =====================================================

void LiveRecorderModuleView::timerCallback()
{
    refreshInputChannels();
    syncMidiButtonStates();

    const float linearRms = audioEngine.getRecorderRms (recorderIndex);
    const float linearPeak = audioEngine.getRecorderPeak (recorderIndex);

    const float rmsDb = juce::Decibels::gainToDecibels (linearRms, kMinGainDb);
    const float peakDb = juce::Decibels::gainToDecibels (linearPeak, kMinGainDb);

    rms = juce::jlimit (0.0f, 1.0f,
                        (rmsDb - kMinGainDb) / (kMaxGainDb - kMinGainDb));
    peak = juce::jlimit (0.0f, 1.0f,
                         (peakDb - kMinGainDb) / (kMaxGainDb - kMinGainDb));

    const float gainDb = audioEngine.getRecorderInputGainDb (recorderIndex);
    gainPosition = juce::jlimit (0.0f, 1.0f,
                                 (gainDb - kMinGainDb) / (kMaxGainDb - kMinGainDb));

    const bool previouslyRecording = isRecording;
    const bool currentlyRecording = audioEngine.isRecorderArmed (recorderIndex);
    if (currentlyRecording != isRecording)
        isRecording = currentlyRecording;

    const bool recordArmEnabled =
        audioEngine.isRecorderRecordArmEnabled (recorderIndex);

    const int totalSamples = audioEngine.getRecorderTotalSamples (recorderIndex);
    const int maxSamples = audioEngine.getRecorderMaxSamples (recorderIndex);
    const double totalRecordedSeconds =
        maxSamples > 0
            ? (static_cast<double> (totalSamples) * kMaxRecordSeconds
               / static_cast<double> (maxSamples))
            : 0.0;

    if (! previouslyRecording && isRecording)
        recordingOffsetSeconds = lastRecordedSeconds;

    if (previouslyRecording && ! isRecording)
    {
        const double secs =
            audioEngine.getRecorderCurrentPassSeconds (recorderIndex);
        lastRecordedSeconds = recordingOffsetSeconds + secs;
    }

    double progress = 0.0;
    if (recordArmEnabled)
    {
        if (maxSamples > 0)
            progress = static_cast<double> (totalSamples) / static_cast<double> (maxSamples);
    }
    else
    {
        isPlaying = audioEngine.isRecorderPlaying (recorderIndex);
        if (isPlaying)
        {
            progress = audioEngine.getRecorderPlaybackProgress (recorderIndex);
            if (progress >= 1.0)
            {
                audioEngine.stopPlayback (recorderIndex);
                audioEngine.seekRecorderPlayback (recorderIndex, 0.0);
                isPlaying = false;
                progress = 0.0;
            }
        }
        else
        {
            progress = 0.0;
        }
        timeCounter.setName (isPlaying ? "PLAYING" : "PLAY_IDLE");
        timeCounter.setButtonText ("");
    }

    if (recordArmEnabled)
    {
        if (! isRecording)
        {
            recordingOffsetSeconds = 0.0;
            lastRecordedSeconds = totalRecordedSeconds;
            timeCounter.setButtonText (
                juce::String::formatted ("%02d:%02d",
                                         int (lastRecordedSeconds) / 60,
                                         int (lastRecordedSeconds) % 60));
        }

        const double startMs = audioEngine.getRecorderRecordStartMs (recorderIndex);
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double elapsedMs = nowMs - startMs;
        const double cycleMs = 1000.0;
        flashPhase = std::fmod (elapsedMs / cycleMs, 1.0);
        const bool flashOn = flashPhase < 0.5;

        if (isRecording)
        {
            const double secs =
                audioEngine.getRecorderCurrentPassSeconds (recorderIndex);
            const double totalSecs = recordingOffsetSeconds + secs;

            timeCounter.setButtonText (
                juce::String::formatted ("%02d:%02d",
                                         int (totalSecs) / 60,
                                         int (totalSecs) % 60));

            if (secs < kMinSeconds)
                timeCounter.setName (flashOn ? "RECORD_ORANGE_ON"
                                             : "RECORD_ORANGE_DIM");
            else
                timeCounter.setName (flashOn ? "RECORD_RED_ON"
                                             : "RECORD_RED_DIM");
        }
        else
        {
            timeCounter.setName ("RECORD_STOPPED");
            timeCounter.setButtonText (
                juce::String::formatted ("%02d:%02d",
                                         int (lastRecordedSeconds) / 60,
                                         int (lastRecordedSeconds) % 60));
        }
    }

    repaint();
}
