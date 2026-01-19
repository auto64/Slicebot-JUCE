#include "AudioEngine.h"
#include "AppProperties.h"

namespace
{
    constexpr const char* kVirtualInIdentifier = "virtual:slicebot-sync-in";
    constexpr const char* kVirtualInName = "SliceBot Sync In";
    constexpr const char* kVirtualOutIdentifier = "virtual:slicebot-sync-out";
    constexpr const char* kVirtualOutName = "SliceBot Sync Out";
    constexpr double kMinMidiBpm = 20.0;
    constexpr double kMaxMidiBpm = 300.0;
    constexpr double kPreferredSampleRate = 44100.0;
    enum ExternalTransportCommand
    {
        kExternalTransportNone = 0,
        kExternalTransportStart = 1,
        kExternalTransportStop = 2
    };

    juce::File findSoundFile (const juce::String& name)
    {
        auto cwd = juce::File::getCurrentWorkingDirectory();
        for (int i = 0; i < 6; ++i)
        {
            auto candidate = cwd.getChildFile ("SWIFT V3 FILES")
                                .getChildFile ("SLICEBOT_LIVE_V3")
                                .getChildFile (name);
            if (candidate.existsAsFile())
                return candidate;
            cwd = cwd.getParentDirectory();
        }
        return {};
    }

    bool loadSoundFile (juce::AudioFormatManager& manager,
                        const juce::File& file,
                        juce::AudioBuffer<float>& buffer)
    {
        if (! file.existsAsFile())
            return false;

        auto reader = std::unique_ptr<juce::AudioFormatReader> (
            manager.createReaderFor (file));
        if (! reader)
            return false;

        buffer.setSize (static_cast<int> (reader->numChannels),
                        static_cast<int> (reader->lengthInSamples));
        reader->read (&buffer,
                      0,
                      static_cast<int> (reader->lengthInSamples),
                      0,
                      true,
                      true);
        return true;
    }
}

// =====================================================
// CONSTRUCTION / DESTRUCTION
// =====================================================

AudioEngine::AudioEngine()
{
    auto& props = AppProperties::get().properties();

    if (auto* settings = props.getUserSettings())
    {
        if (auto xml = settings->getXmlValue ("audioDeviceState"))
            deviceManager.initialise (0, 0, xml.get(), true);
        else
            deviceManager.initialiseWithDefaultDevices (2, 2);
    }
    else
    {
        deviceManager.initialiseWithDefaultDevices (2, 2);
    }

    enforceSampleRate (kPreferredSampleRate);
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    closeMidiInputDevice();
    closeMidiOutputDevice();

    auto& props = AppProperties::get().properties();

    if (auto* settings = props.getUserSettings())
    {
        if (auto xml = deviceManager.createStateXml())
            settings->setValue ("audioDeviceState", xml.get());
    }

    deviceManager.removeAudioCallback (this);
}

// =====================================================
// LIFECYCLE
// =====================================================

void AudioEngine::start() {}
void AudioEngine::stop()  {}

void AudioEngine::restoreState()
{
    auto& props = AppProperties::get().properties();
    if (auto* settings = props.getUserSettings())
    {
#if JUCE_MAC
        const bool defaultVirtualPorts = true;
#else
        const bool defaultVirtualPorts = false;
#endif
        midiSyncMode = static_cast<MidiSyncMode> (
            settings->getIntValue ("midiSyncMode", static_cast<int> (MidiSyncMode::off)));
        midiSyncInputDeviceIdentifier = settings->getValue ("midiSyncInputDevice", "");
        midiSyncOutputDeviceIdentifier = settings->getValue ("midiSyncOutputDevice", "");
        midiVirtualPortsEnabled = settings->getBoolValue ("midiVirtualPortsEnabled", defaultVirtualPorts);
        midiSyncBpm = settings->getDoubleValue ("midiSyncBpm", midiSyncBpm);
        transportMasterRecorderIndex = -1;
        externalTransportPlaying.store (false);
        lastExternalClockMs.store (0.0);
        pendingExternalTransportCommand.store (kExternalTransportNone);

        for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
        {
            const juce::String prefix = "recorder_" + juce::String (index) + "_";
            const int inputChannel = settings->getIntValue (prefix + "inputChannel", -1);
            const bool includeEnabled = settings->getBoolValue (prefix + "includeInGeneration", true);
            const bool recordArmEnabled = settings->getBoolValue (prefix + "recordArmEnabled", true);
            const bool locked = settings->getBoolValue (prefix + "locked", false);
            const bool midiInEnabled = settings->getBoolValue (
                prefix + "midiInEnabled",
                settings->getBoolValue (prefix + "midiArmEnabled", false));
            const bool midiOutEnabled = settings->getBoolValue (prefix + "midiOutEnabled", false);
            const float gainDb = juce::jlimit (
                kRecorderMinGainDb,
                kRecorderMaxGainDb,
                static_cast<float> (settings->getDoubleValue (prefix + "inputGainDb", 0.0)));

            recorderPhysicalChannels[index] = inputChannel;
            recorderIncludeInGeneration[index] = includeEnabled;
            recorderMonitoringEnabled[index] = false;
            recorderLatchEnabled[index] = false;
            recorderMidiInEnabled[index] = midiInEnabled;
            if (midiOutEnabled && transportMasterRecorderIndex < 0)
            {
                recorderMidiOutEnabled[index] = true;
                transportMasterRecorderIndex = index;
            }
            else
            {
                recorderMidiOutEnabled[index] = false;
            }
            recorderRecordArmEnabled[index] = recordArmEnabled;
            recorderLocked[index] = locked;
            recorderInputGainDb[index] = gainDb;

            recordingBus.setRecorderMonitoringEnabled (index, false);
            recordingBus.setRecorderLatchEnabled (index, false);
            recordingBus.setRecorderRecordArmEnabled (index, recordArmEnabled);
            recordingBus.setRecorderInputGainDb (index, gainDb);
        }
    }

    updateMidiClockState();
    updateMidiInputState();
}

void AudioEngine::saveState()
{
    auto& props = AppProperties::get().properties();
    if (auto* settings = props.getUserSettings())
    {
        settings->setValue ("midiSyncMode", static_cast<int> (midiSyncMode));
        settings->setValue ("midiSyncInputDevice", midiSyncInputDeviceIdentifier);
        settings->setValue ("midiSyncOutputDevice", midiSyncOutputDeviceIdentifier);
        settings->setValue ("midiVirtualPortsEnabled", midiVirtualPortsEnabled);
        settings->setValue ("midiSyncBpm", midiSyncBpm);

        for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
        {
            const juce::String prefix = "recorder_" + juce::String (index) + "_";
            settings->setValue (prefix + "inputChannel", recorderPhysicalChannels[index]);
            settings->setValue (prefix + "includeInGeneration", recorderIncludeInGeneration[index]);
            settings->setValue (prefix + "monitoringEnabled", false);
            settings->setValue (prefix + "latchEnabled", false);
            settings->setValue (prefix + "midiInEnabled", recorderMidiInEnabled[index]);
            settings->setValue (prefix + "midiOutEnabled", recorderMidiOutEnabled[index]);
            settings->setValue (prefix + "recordArmEnabled", recorderRecordArmEnabled[index]);
            settings->setValue (prefix + "locked", recorderLocked[index]);
            settings->setValue (prefix + "inputGainDb", recorderInputGainDb[index]);
        }
    }
}

// =====================================================
// DEVICE ACCESS
// =====================================================

juce::AudioDeviceManager& AudioEngine::getDeviceManager()
{
    return deviceManager;
}

void AudioEngine::enforceSampleRate (double targetSampleRate)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (! device)
        return;

    const auto availableRates = device->getAvailableSampleRates();
    if (! availableRates.contains (targetSampleRate))
        return;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    if (juce::approximatelyEqual (setup.sampleRate, targetSampleRate))
        return;

    setup.sampleRate = targetSampleRate;
    deviceManager.setAudioDeviceSetup (setup, true);
}

// =====================================================
// INPUT CHANNEL INFO (UI)
// =====================================================

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getInputChannelNames();

    return {};
}

juce::Array<AudioEngine::ActiveInputChannel>
AudioEngine::getActiveInputChannels() const
{
    juce::Array<ActiveInputChannel> result;

    auto* device = deviceManager.getCurrentAudioDevice();
    if (! device)
        return result;

    const auto names = device->getInputChannelNames();
    const auto mask  = device->getActiveInputChannels();

    for (int i = 0; i < names.size(); ++i)
    {
        if (mask[i])
            result.add ({ names[i], i });
    }

    return result;
}

// =====================================================
// MIDI SYNC
// =====================================================

void AudioEngine::setMidiSyncMode (MidiSyncMode mode)
{
    midiSyncMode = mode;
    updateMidiClockState();
    updateMidiInputState();
}

AudioEngine::MidiSyncMode AudioEngine::getMidiSyncMode() const
{
    return midiSyncMode;
}

void AudioEngine::setMidiSyncInputDeviceIdentifier (const juce::String& identifier)
{
    midiSyncInputDeviceIdentifier = identifier;
    updateMidiInputState();
}

void AudioEngine::setMidiSyncOutputDeviceIdentifier (const juce::String& identifier)
{
    midiSyncOutputDeviceIdentifier = identifier;
    updateMidiClockState();
}

juce::String AudioEngine::getMidiSyncInputDeviceIdentifier() const
{
    return midiSyncInputDeviceIdentifier;
}

juce::String AudioEngine::getMidiSyncOutputDeviceIdentifier() const
{
    return midiSyncOutputDeviceIdentifier;
}

void AudioEngine::setMidiVirtualPortsEnabled (bool enabled)
{
    midiVirtualPortsEnabled = enabled;
    updateMidiClockState();
    updateMidiInputState();
}

bool AudioEngine::getMidiVirtualPortsEnabled() const
{
    return midiVirtualPortsEnabled;
}

void AudioEngine::setMidiSyncBpm (double bpm)
{
    midiSyncBpm = juce::jlimit (kMinMidiBpm, kMaxMidiBpm, bpm);
    updateMidiClockState();
}

double AudioEngine::getMidiSyncBpm() const
{
    return midiSyncBpm;
}

void AudioEngine::setRecorderMidiInEnabled (int index, bool enabled)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    recorderMidiInEnabled[index] = enabled;
}

void AudioEngine::setRecorderMidiOutEnabled (int index, bool enabled)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    if (enabled)
    {
        for (int i = 0; i < RecordingBus::kNumRecorders; ++i)
            recorderMidiOutEnabled[i] = (i == index);
        transportMasterRecorderIndex = index;
        updateMidiClockState();
        return;
    }

    recorderMidiOutEnabled[index] = false;
    if (transportMasterRecorderIndex == index)
        transportMasterRecorderIndex = -1;
    updateMidiClockState();
}

bool AudioEngine::isRecorderMidiInEnabled (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderMidiInEnabled[index];
}

bool AudioEngine::isRecorderMidiOutEnabled (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderMidiOutEnabled[index];
}

int AudioEngine::getTransportMasterRecorderIndex() const
{
    return transportMasterRecorderIndex;
}

void AudioEngine::hiResTimerCallback()
{
    if (! midiClockRunning)
        return;

    if (auto* output = getActiveMidiOutput())
        output->sendMessageNow (juce::MidiMessage::midiClock());
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*,
                                             const juce::MidiMessage& message)
{
    if (midiSyncMode != MidiSyncMode::receive)
        return;

    if (message.isMidiClock())
    {
        lastExternalClockMs.store (juce::Time::getMillisecondCounterHiRes());
        return;
    }

    if (message.isMidiStart() || message.isMidiContinue())
    {
        externalTransportPlaying.store (true);
        pendingExternalTransportCommand.store (kExternalTransportStart);
        triggerAsyncUpdate();
        return;
    }

    if (message.isMidiStop())
    {
        externalTransportPlaying.store (false);
        pendingExternalTransportCommand.store (kExternalTransportStop);
        triggerAsyncUpdate();
        return;
    }
}

void AudioEngine::handleAsyncUpdate()
{
    const int command = pendingExternalTransportCommand.exchange (kExternalTransportNone);
    if (command == kExternalTransportStart)
    {
        applyExternalTransportStart();
        return;
    }

    if (command == kExternalTransportStop)
        applyExternalTransportStop();
}

void AudioEngine::updateMidiClockState()
{
    const bool shouldSend = midiSyncMode == MidiSyncMode::send
                            && transportMasterRecorderIndex >= 0
                            && ! midiSyncOutputDeviceIdentifier.isEmpty();

    if (! shouldSend)
    {
        if (midiClockRunning)
        {
            sendMidiStop();
            midiClockRunning = false;
        }
        stopTimer();
        closeMidiOutputDevice();
        return;
    }

    openMidiOutputDevice();
    if (getActiveMidiOutput() == nullptr)
        return;

    const double bpm = juce::jlimit (kMinMidiBpm, kMaxMidiBpm, midiSyncBpm);
    const double ticksPerSecond = (bpm / 60.0) * 24.0;
    const int intervalUs = ticksPerSecond > 0.0
                               ? static_cast<int> (std::round (1.0e6 / ticksPerSecond))
                               : 0;

    if (intervalUs > 0)
        startTimer (intervalUs);

    if (! midiClockRunning)
    {
        sendMidiStart();
        midiClockRunning = true;
    }
}

void AudioEngine::updateMidiInputState()
{
    const bool shouldReceive = midiSyncMode == MidiSyncMode::receive
                               && ! midiSyncInputDeviceIdentifier.isEmpty();
    const bool virtualInputSelected =
        midiSyncInputDeviceIdentifier == kVirtualInIdentifier;

    if (! shouldReceive || (virtualInputSelected && ! midiVirtualPortsEnabled))
    {
        closeMidiInputDevice();
        return;
    }

    openMidiInputDevice();
}

void AudioEngine::openMidiInputDevice()
{
    if (activeMidiInputIdentifier == midiSyncInputDeviceIdentifier
        && midiInput != nullptr)
    {
        return;
    }

    closeMidiInputDevice();
    activeMidiInputIdentifier = midiSyncInputDeviceIdentifier;

    if (activeMidiInputIdentifier.isEmpty())
        return;

    if (activeMidiInputIdentifier == kVirtualInIdentifier)
    {
        if (midiVirtualPortsEnabled)
            midiInput = juce::MidiInput::createNewDevice (kVirtualInName, this);
    }
    else
    {
        midiInput = juce::MidiInput::openDevice (activeMidiInputIdentifier, this);
    }

    if (midiInput != nullptr)
        midiInput->start();
}

void AudioEngine::closeMidiInputDevice()
{
    if (midiInput != nullptr)
        midiInput->stop();

    midiInput.reset();
}

void AudioEngine::openMidiOutputDevice()
{
    if (activeMidiOutputIdentifier == midiSyncOutputDeviceIdentifier
        && getActiveMidiOutput() != nullptr)
    {
        return;
    }

    closeMidiOutputDevice();
    activeMidiOutputIdentifier = midiSyncOutputDeviceIdentifier;

    if (activeMidiOutputIdentifier.isEmpty())
        return;

    if (activeMidiOutputIdentifier == kVirtualOutIdentifier)
    {
        if (midiVirtualPortsEnabled)
        {
            midiVirtualOutput = juce::MidiOutput::createNewDevice (kVirtualOutName);
        }
        return;
    }

    midiOutput = juce::MidiOutput::openDevice (activeMidiOutputIdentifier);
}

void AudioEngine::closeMidiOutputDevice()
{
    midiOutput.reset();
    if (! midiVirtualPortsEnabled)
        midiVirtualOutput.reset();
}

juce::MidiOutput* AudioEngine::getActiveMidiOutput() const
{
    if (activeMidiOutputIdentifier == kVirtualOutIdentifier)
        return midiVirtualOutput.get();

    return midiOutput.get();
}

void AudioEngine::sendMidiStart()
{
    if (auto* output = getActiveMidiOutput())
        output->sendMessageNow (juce::MidiMessage::midiStart());
}

void AudioEngine::sendMidiStop()
{
    if (auto* output = getActiveMidiOutput())
        output->sendMessageNow (juce::MidiMessage::midiStop());
}

void AudioEngine::applyExternalTransportStart()
{
    if (midiSyncMode != MidiSyncMode::receive)
        return;

    if (! hasAnyRecorderMidiInEnabled())
        return;

    bool shouldUseLatchGroup = false;
    bool anyRecordArmEnabled = false;

    for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
    {
        if (! recorderMidiInEnabled[index])
            continue;

        if (recordingBus.isRecorderLatchEnabled (index))
            shouldUseLatchGroup = true;

        if (recordingBus.isRecorderRecordArmEnabled (index))
            anyRecordArmEnabled = true;
    }

    if (anyRecordArmEnabled)
    {
        if (shouldUseLatchGroup)
        {
            armLatchedRecorders();
            return;
        }

        for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
        {
            if (! recorderMidiInEnabled[index])
                continue;

            if (recordingBus.isRecorderRecordArmEnabled (index))
                armRecorder (index);
        }
        return;
    }

    if (shouldUseLatchGroup)
    {
        startLatchedPlayback();
        return;
    }

    for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
    {
        if (! recorderMidiInEnabled[index])
            continue;

        startPlayback (index);
    }
}

void AudioEngine::applyExternalTransportStop()
{
    if (midiSyncMode != MidiSyncMode::receive)
        return;

    if (! hasAnyRecorderMidiInEnabled())
        return;

    bool shouldUseLatchGroup = false;
    bool anyRecording = false;
    bool anyPlaying = false;

    for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
    {
        if (! recorderMidiInEnabled[index])
            continue;

        if (recordingBus.isRecorderLatchEnabled (index))
            shouldUseLatchGroup = true;

        if (recordingBus.isRecorderArmed (index))
            anyRecording = true;

        if (recordingBus.isRecorderPlaying (index))
            anyPlaying = true;
    }

    if (anyRecording)
    {
        if (shouldUseLatchGroup)
        {
            stopLatchedRecorders();
            return;
        }

        for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
        {
            if (! recorderMidiInEnabled[index])
                continue;

            confirmStopRecorder (index);
        }
        return;
    }

    if (! anyPlaying)
        return;

    if (shouldUseLatchGroup)
    {
        stopLatchedPlayback();
        return;
    }

    for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
    {
        if (! recorderMidiInEnabled[index])
            continue;

        stopPlayback (index);
    }
}

bool AudioEngine::hasAnyRecorderMidiInEnabled() const
{
    for (bool enabled : recorderMidiInEnabled)
    {
        if (enabled)
            return true;
    }
    return false;
}

// =====================================================
// RECORDER CONTROL
// =====================================================

void AudioEngine::armRecorder (int index)
{
    recordingBus.armRecorder (index);
}

RecordingModule::StopResult
AudioEngine::confirmStopRecorder (int index)
{
    return recordingBus.confirmStopRecorder (index);
}

void AudioEngine::cancelStopRecorder (int index)
{
    recordingBus.cancelStopRecorder (index);
}

void AudioEngine::clearRecorder (int index)
{
    recordingBus.clearRecorder (index);
    const auto file = RecordingModule::getRecorderFile (index);
    if (file.existsAsFile())
        file.deleteFile();

    recorderPhysicalChannels[index] = -1;
    recorderMonitoringEnabled[index] = false;
    recorderLatchEnabled[index] = false;
    recorderIncludeInGeneration[index] = true;
    recorderMidiInEnabled[index] = false;
    recorderMidiOutEnabled[index] = false;
    recorderRecordArmEnabled[index] = true;
    recorderLocked[index] = false;
    recorderInputGainDb[index] = 0.0f;

    if (transportMasterRecorderIndex == index)
    {
        transportMasterRecorderIndex = -1;
        updateMidiClockState();
    }

    recordingBus.setRecorderMonitoringEnabled (index, false);
    recordingBus.setRecorderLatchEnabled (index, false);
    recordingBus.setRecorderRecordArmEnabled (index, true);
    recordingBus.setRecorderInputGainDb (index, 0.0f);

    saveState();
}

bool AudioEngine::startPlayback (int index)
{
    return recordingBus.startPlayback (index);
}

void AudioEngine::stopPlayback (int index)
{
    recordingBus.stopPlayback (index);
}

void AudioEngine::setRecorderMonitoringEnabled (int index, bool enabled)
{
    recordingBus.setRecorderMonitoringEnabled (index, enabled);
    if (index >= 0 && index < RecordingBus::kNumRecorders)
        recorderMonitoringEnabled[index] = enabled;
}

void AudioEngine::setRecorderInputChannel (int index, int physicalChannel)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    recorderPhysicalChannels[index] = physicalChannel;
}

void AudioEngine::setRecorderLatchEnabled (int index, bool enabled)
{
    recordingBus.setRecorderLatchEnabled (index, enabled);
    if (index >= 0 && index < RecordingBus::kNumRecorders)
        recorderLatchEnabled[index] = enabled;
}

void AudioEngine::setRecorderIncludeInGenerationEnabled (int index, bool enabled)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    recorderIncludeInGeneration[index] = enabled;
}

void AudioEngine::setRecorderRecordArmEnabled (int index, bool enabled)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    recorderRecordArmEnabled[index] = enabled;
    recordingBus.setRecorderRecordArmEnabled (index, enabled);
}

void AudioEngine::setRecorderLocked (int index, bool locked)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    recorderLocked[index] = locked;
}

void AudioEngine::setRecorderInputGainDb (int index, float gainDb)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    const float clamped = juce::jlimit (kRecorderMinGainDb,
                                        kRecorderMaxGainDb,
                                        gainDb);
    recorderInputGainDb[index] = clamped;
    recordingBus.setRecorderInputGainDb (index, clamped);
}

bool AudioEngine::hasLatchedRecorders() const
{
    return recordingBus.hasLatchedRecorders();
}

void AudioEngine::armLatchedRecorders()
{
    recordingBus.armLatchedRecorders();
}

RecordingModule::StopResult AudioEngine::stopLatchedRecorders()
{
    return recordingBus.stopLatchedRecorders();
}

bool AudioEngine::startLatchedPlayback()
{
    return recordingBus.startLatchedPlayback();
}

void AudioEngine::stopLatchedPlayback()
{
    recordingBus.stopLatchedPlayback();
}

int AudioEngine::getRecorderInputChannel (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return -1;

    return recorderPhysicalChannels[index];
}

bool AudioEngine::isRecorderMonitoringEnabled (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderMonitoringEnabled[index];
}

bool AudioEngine::isRecorderLatchEnabled (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderLatchEnabled[index];
}

bool AudioEngine::isRecorderIncludeInGenerationEnabled (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderIncludeInGeneration[index];
}

bool AudioEngine::isRecorderRecordArmEnabled (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderRecordArmEnabled[index];
}

bool AudioEngine::isRecorderLocked (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderLocked[index];
}

bool AudioEngine::isRecorderArmed (int index) const
{
    return recordingBus.isRecorderArmed (index);
}

bool AudioEngine::isRecorderPlaying (int index) const
{
    return recordingBus.isRecorderPlaying (index);
}

float AudioEngine::getRecorderInputGainDb (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return 0.0f;

    return recorderInputGainDb[index];
}

float AudioEngine::getRecorderRms (int index) const
{
    return recordingBus.getRecorderRms (index);
}

float AudioEngine::getRecorderPeak (int index) const
{
    return recordingBus.getRecorderPeak (index);
}

double AudioEngine::getRecorderPlaybackProgress (int index) const
{
    return recordingBus.getRecorderPlaybackProgress (index);
}

void AudioEngine::seekRecorderPlayback (int index, double progress)
{
    recordingBus.seekRecorderPlayback (index, progress);
}

double AudioEngine::getRecorderRecordStartMs (int index) const
{
    return recordingBus.getRecorderRecordStartMs (index);
}

int AudioEngine::getRecorderTotalSamples (int index) const
{
    return recordingBus.getRecorderTotalSamples (index);
}

int AudioEngine::getRecorderMaxSamples (int index) const
{
    return recordingBus.getRecorderMaxSamples (index);
}

// =====================================================
// TIMING
// =====================================================

double AudioEngine::getRecorderCurrentPassSeconds (int index) const
{
    return recordingBus.getRecorderCurrentPassSeconds (index);
}

// =====================================================
// METERS
// =====================================================

float AudioEngine::getInputRMS() const
{
    return inputRMS.load();
}

float AudioEngine::getInputPeak() const
{
    return inputPeak.load();
}

void AudioEngine::playUiSound (UiSound sound)
{
    currentSound.store (sound);
    const int length = (sound == UiSound::Cowbell)
                           ? cowbellBuffer.getNumSamples()
                           : bleepBuffer.getNumSamples();
    soundLength.store (length);
    if (length == 0)
        return;

    soundPosition.store (0);
}

// =====================================================
// JUCE CALLBACKS
// =====================================================

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    recordingBus.prepare (device->getCurrentSampleRate(),
                          device->getCurrentBufferSizeSamples());

    if (soundFormatManager.getNumKnownFormats() == 0)
        soundFormatManager.registerBasicFormats();

    if (bleepBuffer.getNumSamples() == 0)
    {
        const auto file = findSoundFile ("bleep.wav");
        loadSoundFile (soundFormatManager, file, bleepBuffer);
    }

    if (cowbellBuffer.getNumSamples() == 0)
    {
        const auto file = findSoundFile ("cowbell.wav");
        loadSoundFile (soundFormatManager, file, cowbellBuffer);
    }

    // DEFAULT INPUT ASSIGNMENT (RESTORED)
    const auto activeMask = device->getActiveInputChannels();

    int firstActive = -1;
    const int highest = activeMask.getHighestBit() + 1;

    for (int i = 0; i < highest; ++i)
    {
        if (activeMask[i])
        {
            firstActive = i;
            break;
        }
    }

    if (firstActive >= 0)
    {
        for (int r = 0; r < RecordingBus::kNumRecorders; ++r)
        {
            if (recorderPhysicalChannels[r] < 0)
                recorderPhysicalChannels[r] = firstActive;
        }
    }
}

void AudioEngine::audioDeviceStopped() {}

// =====================================================
// AUDIO CALLBACK
// =====================================================

void AudioEngine::audioDeviceIOCallbackWithContext (
    const float* const* input,
    int numInputChannels,
    float* const* output,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (! device)
        return;

    // -------------------------------------------------
    // PHYSICAL → BUFFER MAP
    // -------------------------------------------------

    const auto activeMask = device->getActiveInputChannels();

    int physicalToBuffer[64];
    int bufferIndex = 0;

    const int highest =
        juce::jmin (64, activeMask.getHighestBit() + 1);

    for (int phys = 0; phys < highest; ++phys)
    {
        if (activeMask[phys])
            physicalToBuffer[phys] = bufferIndex++;
        else
            physicalToBuffer[phys] = -1;
    }

    for (int r = 0; r < RecordingBus::kNumRecorders; ++r)
    {
        const int phys = recorderPhysicalChannels[r];
        int buf = -1;

        if (phys >= 0 && phys < highest)
            buf = physicalToBuffer[phys];

        recordingBus.setRecorderInputBufferIndex (r, buf);
    }

    // -------------------------------------------------
    // PROCESS
    // -------------------------------------------------

    recordingBus.processAudioBlock (
        input,
        numInputChannels,
        output,
        numOutputChannels,
        numSamples);

    const int currentPos = soundPosition.load();
    const int length = soundLength.load();
    if (length > 0 && currentPos < length)
    {
        const auto sound = currentSound.load();
        const auto& buffer = (sound == UiSound::Cowbell) ? cowbellBuffer : bleepBuffer;
        const int available = buffer.getNumSamples() - currentPos;
        const int toCopy = juce::jmin (available, numSamples);
        if (toCopy > 0)
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
            {
                const int srcChannel = juce::jmin (ch, buffer.getNumChannels() - 1);
                juce::FloatVectorOperations::add (
                    output[ch],
                    buffer.getReadPointer (srcChannel, currentPos),
                    toCopy);
            }
        }

        soundPosition.store (currentPos + toCopy);
    }

    // -------------------------------------------------
    // METERS
    // -------------------------------------------------

    float rms  = 0.0f;
    float peak = 0.0f;

    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        const float* src = input[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = std::abs (src[i]);
            peak = juce::jmax (peak, v);
            rms += v * v;
        }
    }

    if (numInputChannels > 0 && numSamples > 0)
        rms = std::sqrt (rms / (numSamples * numInputChannels));

    inputRMS.store (rms);
    inputPeak.store (peak);
}
