#include "RecordingBus.h"
#include <cmath>

// =====================================================
// CONSTRUCTION
// =====================================================

RecordingBus::RecordingBus() {}

// =====================================================
// LIFECYCLE
// =====================================================

void RecordingBus::prepare (double sampleRate, int bufferSize)
{
    this->sampleRate = sampleRate;
    this->bufferSize = bufferSize;

    // CRITICAL: ensure writers exist
    for (int i = 0; i < kNumRecorders; ++i)
    {
        recorders[i].recorder.prepareDevice (sampleRate, i);
        recorders[i].inputBuffer.setSize (1, bufferSize, false, false, true);
        recorders[i].playbackBuffer.setSize (1, bufferSize, false, false, true);
    }
}

// =====================================================
// RECORD CONTROL
// =====================================================

void RecordingBus::armRecorder (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    if (hasLatchedRecorders())
    {
        armLatchedRecorders();
    }
    else
    {
        recorders[index].armed = true;
        recorders[index].recordStartMs = juce::Time::getMillisecondCounterHiRes();
        recorders[index].recorder.arm();
    }
}

RecordingModule::StopResult
RecordingBus::confirmStopRecorder (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return RecordingModule::StopResult::Kept;

    auto& slot = recorders[index];

    if (! slot.armed)
        return RecordingModule::StopResult::Kept;

    if (hasLatchedRecorders())
    {
        return stopLatchedRecorders();
    }

    slot.armed = false;
    return slot.recorder.confirmStop();
}

void RecordingBus::cancelStopRecorder (int)
{
}

void RecordingBus::clearRecorder (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].recorder.clear();
    recorders[index].armed = false;
    recorders[index].playing = false;
    recorders[index].playbackPosition = 0;
}

// =====================================================
// STATE
// =====================================================

bool RecordingBus::hasLatchedRecorders() const
{
    for (const auto& slot : recorders)
    {
        if (slot.latchEnabled)
            return true;
    }

    return false;
}

void RecordingBus::armLatchedRecorders()
{
    const double startMs = juce::Time::getMillisecondCounterHiRes();
    for (auto& slot : recorders)
    {
        if (! slot.latchEnabled)
            continue;

        slot.armed = true;
        slot.recordStartMs = startMs;
        slot.recorder.arm();
    }
}

RecordingModule::StopResult RecordingBus::stopLatchedRecorders()
{
    RecordingModule::StopResult result = RecordingModule::StopResult::Kept;
    for (auto& slot : recorders)
    {
        if (! slot.latchEnabled)
            continue;

        slot.armed = false;
        const auto stopResult = slot.recorder.confirmStop();
        if (stopResult == RecordingModule::StopResult::DeletedTooShort)
            result = stopResult;
    }
    return result;
}

bool RecordingBus::isRecorderArmed (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return false;

    return recorders[index].armed;
}

void RecordingBus::setRecorderLatchEnabled (int index, bool enabled)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].latchEnabled = enabled;
}

bool RecordingBus::isRecorderLatchEnabled (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return false;

    return recorders[index].latchEnabled;
}

void RecordingBus::setRecorderRecordArmEnabled (int index, bool enabled)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].recordArmEnabled = enabled;
}

bool RecordingBus::isRecorderRecordArmEnabled (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return false;

    return recorders[index].recordArmEnabled;
}

bool RecordingBus::startPlayback (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return false;

    auto& slot = recorders[index];
    const int totalSamples = slot.recorder.getTotalSamples();
    if (totalSamples <= 0)
        return false;

    if (slot.playbackPosition >= totalSamples)
        slot.playbackPosition = 0;

    slot.playing = true;
    return true;
}

void RecordingBus::stopPlayback (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].playing = false;
}

bool RecordingBus::isRecorderPlaying (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return false;

    return recorders[index].playing;
}

bool RecordingBus::startLatchedPlayback()
{
    bool started = false;
    for (int i = 0; i < kNumRecorders; ++i)
    {
        if (! recorders[i].latchEnabled)
            continue;

        if (startPlayback (i))
            started = true;
    }
    return started;
}

void RecordingBus::stopLatchedPlayback()
{
    for (auto& slot : recorders)
    {
        if (! slot.latchEnabled)
            continue;

        slot.playing = false;
    }
}

double RecordingBus::getRecorderPlaybackProgress (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0.0;

    const auto& slot = recorders[index];
    const int totalSamples = slot.recorder.getTotalSamples();
    if (totalSamples <= 0)
        return 0.0;

    return static_cast<double> (slot.playbackPosition)
           / static_cast<double> (totalSamples);
}

void RecordingBus::seekRecorderPlayback (int index, double progress)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    auto& slot = recorders[index];
    const int totalSamples = slot.recorder.getTotalSamples();
    if (totalSamples <= 0)
        return;

    const auto clamped = juce::jlimit (0.0, 1.0, progress);
    slot.playbackPosition =
        static_cast<juce::int64> (clamped * static_cast<double> (totalSamples));
}

double RecordingBus::getRecorderRecordStartMs (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0.0;

    return recorders[index].recordStartMs;
}

int RecordingBus::getRecorderTotalSamples (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0;

    return recorders[index].recorder.getTotalSamples();
}

int RecordingBus::getRecorderMaxSamples (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0;

    return recorders[index].recorder.getMaxSamples();
}

void RecordingBus::setRecorderInputGainDb (int index, float gainDb)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    auto& slot = recorders[index];
    slot.inputGainDb = gainDb;
    slot.inputGainLinear = juce::Decibels::decibelsToGain (gainDb);
}

float RecordingBus::getRecorderInputGainDb (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0.0f;

    return recorders[index].inputGainDb;
}

float RecordingBus::getRecorderRms (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0.0f;

    return recorders[index].rms;
}

float RecordingBus::getRecorderPeak (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0.0f;

    return recorders[index].peak;
}

// =====================================================
// ROUTING
// =====================================================

void RecordingBus::setRecorderInputBufferIndex (int index, int bufferIndex)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].bufferIndex = bufferIndex;
}

void RecordingBus::setRecorderMonitoringEnabled (int index, bool enabled)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].monitoringEnabled = enabled;
    recorders[index].recorder.setMonitoringEnabled (enabled);
}

// =====================================================
// TIMING
// =====================================================

double RecordingBus::getRecorderCurrentPassSeconds (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0.0;

    return recorders[index].recorder.getCurrentPassSeconds();
}

// =====================================================
// AUDIO
// =====================================================

void RecordingBus::processAudioBlock (const float* const* input,
                                      int numInputChannels,
                                      float* const* output,
                                      int numOutputChannels,
                                      int numSamples)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        juce::FloatVectorOperations::clear (output[ch], numSamples);

    for (auto& slot : recorders)
    {
        slot.rms = 0.0f;
        slot.peak = 0.0f;

        const int buf = slot.bufferIndex;
        const bool hasInput = buf >= 0 && buf < numInputChannels;
        const float* src = hasInput ? input[buf] : nullptr;

        const float gain = slot.inputGainLinear;
        const bool hasGain = gain != 1.0f;

        const float* meterSrc = src;
        if (hasInput && hasGain)
        {
            auto* scratch = slot.inputBuffer.getWritePointer (0);
            for (int i = 0; i < numSamples; ++i)
                scratch[i] = src[i] * gain;
            meterSrc = scratch;
        }

        if (hasInput)
        {
            float rmsSum = 0.0f;
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = std::abs (meterSrc[i]);
                peak = juce::jmax (peak, v);
                rmsSum += v * v;
            }
            slot.peak = peak;
            if (numSamples > 0)
                slot.rms = std::sqrt (rmsSum / numSamples);
        }

        if (slot.armed && hasInput)
            slot.recorder.process (meterSrc, numSamples);

        if (slot.monitoringEnabled && slot.recordArmEnabled && hasInput)
        {
            for (int out = 0; out < numOutputChannels; ++out)
                juce::FloatVectorOperations::add (
                    output[out], meterSrc, numSamples);
        }

        if (slot.playing)
        {
            auto* playBuffer = slot.playbackBuffer.getWritePointer (0);
            const int readSamples =
                slot.recorder.readPlaybackSamples (playBuffer,
                                                   static_cast<int> (slot.playbackPosition),
                                                   numSamples);
            if (readSamples <= 0)
            {
                slot.playing = false;
            }
            else
            {
                if (readSamples < numSamples)
                    juce::FloatVectorOperations::clear (playBuffer + readSamples,
                                                        numSamples - readSamples);

                slot.playbackPosition += readSamples;
                const int totalSamples = slot.recorder.getTotalSamples();
                if (slot.playbackPosition >= totalSamples)
                    slot.playing = false;

                for (int out = 0; out < numOutputChannels; ++out)
                    juce::FloatVectorOperations::add (
                        output[out], playBuffer, numSamples);
            }
        }
    }
}
