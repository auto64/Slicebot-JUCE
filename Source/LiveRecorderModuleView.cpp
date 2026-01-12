#include "LiveRecorderModuleView.h"
#include "AudioEngine.h"

static constexpr int kModuleW = 120;
static constexpr int kModuleH = 120;
static constexpr double kMinSeconds = 25.0;

// =====================================================
// CONSTRUCTION
// =====================================================

LiveRecorderModuleView::LiveRecorderModuleView (AudioEngine& engine,
                                                int index)
    : audioEngine (engine),
      recorderIndex (index)
{
    setSize (kModuleW, kModuleH);

    // ================= MIDI ARM =================
    addAndMakeVisible (midiArmButton);
    midiArmButton.setClickingTogglesState (true);
    midiArmButton.setLookAndFeel (&flatTiles);

    // ================= INPUT SELECT =================
    addAndMakeVisible (channelBox);
    channelBox.addListener (this);

    // ================= CONTROL BUTTONS =================
    addAndMakeVisible (monitorButton);
    addAndMakeVisible (linkButton);
    addAndMakeVisible (sliceButton);
    addAndMakeVisible (clearButton);

    monitorButton.setClickingTogglesState (true);
    linkButton   .setClickingTogglesState (true);
    sliceButton  .setClickingTogglesState (true);

    sliceButton.setToggleState (true, juce::dontSendNotification);
    sliceButton.setButtonText (""); // tick drawn by LookAndFeel
    clearButton.setButtonText ("X");

    monitorButton.addListener (this);
    linkButton   .addListener (this);
    sliceButton  .addListener (this);
    clearButton  .addListener (this);

    monitorButton.setLookAndFeel (&flatTiles);
    linkButton   .setLookAndFeel (&flatTiles);
    sliceButton  .setLookAndFeel (&flatTiles);
    clearButton  .setLookAndFeel (&flatTiles);

    // ================= RECORD BUTTON =================
    addAndMakeVisible (timeCounter);
    timeCounter.addListener (this);
    timeCounter.setLookAndFeel (&flatTiles);
    timeCounter.setButtonText ("00:00");
    timeCounter.setName ("RECORD_IDLE");

    startTimerHz (8); // slower, visible flash
}

LiveRecorderModuleView::~LiveRecorderModuleView()
{
    midiArmButton.setLookAndFeel (nullptr);
    monitorButton.setLookAndFeel (nullptr);
    linkButton   .setLookAndFeel (nullptr);
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

    const int meterY = kModuleH - 16;

    g.setColour (juce::Colours::black);
    g.fillRect (11, meterY, 98, 10);

    g.setColour (juce::Colours::green);
    g.fillRect (11, meterY,
                int (98 * juce::jlimit (0.0f, 1.0f, rms)),
                10);
}

// =====================================================
// LAYOUT
// =====================================================

void LiveRecorderModuleView::resized()
{
    midiArmButton.setBounds (11, 6, 98, 18);
    channelBox   .setBounds (11, 30, 98, 18);

    monitorButton.setBounds (11, 54, 19, 19);
    linkButton   .setBounds (30, 54, 19, 19);
    sliceButton  .setBounds (11, 73, 19, 19);
    clearButton  .setBounds (30, 73, 19, 19);

    timeCounter  .setBounds (49, 54, 60, 38);
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
        selectedId = channelBox.getItemId (0);
        for (int i = 1; i < channelBox.getNumItems(); ++i)
            selectedId = juce::jmin (selectedId,
                                     channelBox.getItemId (i));
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
    if (b == &monitorButton)
    {
        audioEngine.setRecorderMonitoringEnabled (
            recorderIndex,
            monitorButton.getToggleState());
        return;
    }

    if (b == &linkButton)
    {
        audioEngine.setRecorderLatchEnabled (
            recorderIndex,
            linkButton.getToggleState());
        return;
    }

    if (b == &clearButton)
    {
        showClearWarning();
        return;
    }

    if (b != &timeCounter || stopDialogOpen)
        return;

    if (! isRecording)
    {
        audioEngine.armRecorder (recorderIndex);
        isRecording = true;
        timeCounter.setName ("RECORD_ORANGE_ON");
        return;
    }

    const double secs =
        audioEngine.getRecorderCurrentPassSeconds (recorderIndex);

    if (secs < kMinSeconds)
    {
        showUnderMinWarning();
        return;
    }

    audioEngine.confirmStopRecorder (recorderIndex);
    isRecording = false;
    lastRecordedSeconds = secs;
    timeCounter.setName ("RECORD_STOPPED");
}

// =====================================================
// WARNINGS (UNCHANGED)
// =====================================================

void LiveRecorderModuleView::showUnderMinWarning()
{
    stopDialogOpen = true;

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

void LiveRecorderModuleView::showClearWarning()
{
    juce::AlertWindow::showOkCancelBox (
        juce::AlertWindow::WarningIcon,
        "Clear Recording",
        "This will permanently clear the recording.",
        "Clear",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create (
            [this] (int r)
            {
                if (r == 1)
                {
                    audioEngine.clearRecorder (recorderIndex);
                    isRecording = false;
                    lastRecordedSeconds = 0.0;
                    timeCounter.setButtonText ("00:00");
                    timeCounter.setName ("RECORD_IDLE");
                }
            }));
}

// =====================================================
// TIMER (ONLY SECTION THAT CHANGED MEANINGFULLY)
// =====================================================

void LiveRecorderModuleView::timerCallback()
{
    refreshInputChannels();

    rms  = audioEngine.getInputRMS();
    peak = audioEngine.getInputPeak();

    flashPhase = 1.0 - flashPhase; // binary toggle

    if (isRecording)
    {
        const double secs =
            audioEngine.getRecorderCurrentPassSeconds (recorderIndex);

        timeCounter.setButtonText (
            juce::String::formatted ("%02d:%02d",
                                     int (secs) / 60,
                                     int (secs) % 60));

        if (secs < kMinSeconds)
            timeCounter.setName (flashPhase > 0.5
                                 ? "RECORD_ORANGE_ON"
                                 : "RECORD_ORANGE_OFF");
        else
            timeCounter.setName (flashPhase > 0.5
                                 ? "RECORD_RED_ON"
                                 : "RECORD_RED_OFF");
    }
    else
    {
        timeCounter.setButtonText (
            juce::String::formatted ("%02d:%02d",
                                     int (lastRecordedSeconds) / 60,
                                     int (lastRecordedSeconds) % 60));
    }

    repaint();
}
