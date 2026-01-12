#pragma once

#include <JuceHeader.h>

class BackgroundWorker
{
public:
    BackgroundWorker();

    void enqueue (std::function<void()> job);

private:
    juce::ThreadPool threadPool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackgroundWorker)
};
