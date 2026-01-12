#include "SliceInfrastructure.h"

std::optional<int> refinedStart (const juce::AudioBuffer<float>&,
                                 int,
                                 int,
                                 bool)
{
    return std::nullopt;
}

juce::AudioBuffer<float> mergeSlices (const juce::AudioBuffer<float>& leftSlice,
                                      const juce::AudioBuffer<float>&,
                                      MergeMode)
{
    return leftSlice;
}

bool regenerateSliceDormant (AudioFileIO&,
                             const SliceStateStore::SliceInfo&,
                             const juce::File&)
{
    return false;
}
