#include "AudioEngine.h"
#include "AppProperties.h"

namespace
{
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

    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
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
        for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
        {
            const juce::String prefix = "recorder_" + juce::String (index) + "_";
            const int inputChannel = settings->getIntValue (prefix + "inputChannel", -1);
            const bool includeEnabled = settings->getBoolValue (prefix + "includeInGeneration", true);
            const bool recordArmEnabled = settings->getBoolValue (prefix + "recordArmEnabled", true);
            const bool locked = settings->getBoolValue (prefix + "locked", false);
            const float gainDb = static_cast<float> (
                settings->getDoubleValue (prefix + "inputGainDb", 0.0));

            recorderPhysicalChannels[index] = inputChannel;
            recorderIncludeInGeneration[index] = includeEnabled;
            recorderMonitoringEnabled[index] = false;
            recorderLatchEnabled[index] = false;
            recorderMidiArmEnabled[index] = false;
            recorderRecordArmEnabled[index] = recordArmEnabled;
            recorderLocked[index] = locked;
            recorderInputGainDb[index] = gainDb;

            recordingBus.setRecorderMonitoringEnabled (index, false);
            recordingBus.setRecorderLatchEnabled (index, false);
            recordingBus.setRecorderRecordArmEnabled (index, recordArmEnabled);
            recordingBus.setRecorderInputGainDb (index, gainDb);
        }
    }
}

void AudioEngine::saveState()
{
    auto& props = AppProperties::get().properties();
    if (auto* settings = props.getUserSettings())
    {
        for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
        {
            const juce::String prefix = "recorder_" + juce::String (index) + "_";
            settings->setValue (prefix + "inputChannel", recorderPhysicalChannels[index]);
            settings->setValue (prefix + "includeInGeneration", recorderIncludeInGeneration[index]);
            settings->setValue (prefix + "monitoringEnabled", false);
            settings->setValue (prefix + "latchEnabled", false);
            settings->setValue (prefix + "midiArmEnabled", false);
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
    recorderMidiArmEnabled[index] = false;
    recorderRecordArmEnabled[index] = true;
    recorderLocked[index] = false;
    recorderInputGainDb[index] = 0.0f;

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

void AudioEngine::setRecorderMidiArmEnabled (int index, bool enabled)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    recorderMidiArmEnabled[index] = enabled;
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

    const float clamped = juce::jlimit (-60.0f, 6.0f, gainDb);
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

bool AudioEngine::isRecorderMidiArmEnabled (int index) const
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return false;

    return recorderMidiArmEnabled[index];
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
