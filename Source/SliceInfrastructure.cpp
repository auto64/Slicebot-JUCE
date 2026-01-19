#include "SliceInfrastructure.h"

namespace
{
    constexpr double kTargetSampleRate = 44100.0;
    constexpr double kPreTransientOffsetSeconds = 0.005;
}

std::optional<int> refinedStart (const juce::AudioBuffer<float>& input,
                                 juce::Random& random,
                                 int maxCandidateStart,
                                 int windowFrames,
                                 bool transientDetectEnabled)
{
    if (! transientDetectEnabled)
        return std::nullopt;

    const int totalSamples = input.getNumSamples();
    if (windowFrames <= 0 || totalSamples <= 0)
        return std::nullopt;

    if (windowFrames > totalSamples)
        return std::nullopt;

    const int maxWindowStart = totalSamples - windowFrames;
    const int cappedCandidateStart = juce::jlimit (0, maxWindowStart, maxCandidateStart);
    const int windowStart = random.nextInt (cappedCandidateStart + 1);

    if (windowStart < 0 || windowStart + windowFrames > totalSamples)
        return std::nullopt;

    const float* samples = input.getReadPointer (0);
    int maxIndex = 0;
    float maxValue = 0.0f;

    for (int i = 0; i < windowFrames; ++i)
    {
        const float value = std::abs (samples[windowStart + i]);
        if (value > maxValue)
        {
            maxValue = value;
            maxIndex = i;
        }
    }

    const int transientFrame = windowStart + maxIndex;
    const int offsetFrames = static_cast<int> (std::lround (kPreTransientOffsetSeconds * kTargetSampleRate));
    const int startFrame = juce::jmax (0, transientFrame - offsetFrames);

    return startFrame;
}

juce::AudioBuffer<float> mergeSlices (const juce::AudioBuffer<float>& leftSlice,
                                      const juce::AudioBuffer<float>&,
                                      SliceStateStore::MergeMode)
{
    return leftSlice;
}

bool regenerateSliceDormant (AudioFileIO&,
                             const SliceStateStore::SliceInfo&,
                             const juce::File&)
{
    return false;
}
