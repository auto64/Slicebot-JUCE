#include "PreviewChainPlayer.h"

PreviewChainPlayer::PreviewChainPlayer (juce::AudioDeviceManager& deviceManagerToUse)
    : deviceManager (deviceManagerToUse)
{
    formatManager.registerBasicFormats();
    transportSource.setLooping (isLoopEnabled);
}

PreviewChainPlayer::~PreviewChainPlayer()
{
    stopPlayback();
}

bool PreviewChainPlayer::startPlayback (const juce::File& previewChainFile)
{
    return startPlayback (previewChainFile, isLoopEnabled);
}

bool PreviewChainPlayer::startPlayback (const juce::File& previewChainFile, bool shouldLoop)
{
    if (! previewChainFile.existsAsFile())
        return false;

    stopPlayback();

    isLoopEnabled = shouldLoop;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (previewChainFile));
    if (reader == nullptr)
        return false;

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    readerSource->setLooping (isLoopEnabled);
    transportSource.setSource (readerSource.get(), 0, nullptr, readerSource->getAudioFormatReader()->sampleRate);
    transportSource.setLooping (isLoopEnabled);

    sourcePlayer.setSource (&transportSource);
    deviceManager.addAudioCallback (&sourcePlayer);
    transportSource.start();
    isPlayingFlag = true;

    return true;
}

void PreviewChainPlayer::stopPlayback()
{
    if (! isPlayingFlag && readerSource == nullptr)
        return;

    transportSource.stop();
    transportSource.setSource (nullptr);
    sourcePlayer.setSource (nullptr);
    deviceManager.removeAudioCallback (&sourcePlayer);
    readerSource.reset();
    isPlayingFlag = false;
}

void PreviewChainPlayer::setLooping (bool shouldLoop)
{
    isLoopEnabled = shouldLoop;
    transportSource.setLooping (isLoopEnabled);
    if (readerSource != nullptr)
        readerSource->setLooping (isLoopEnabled);
}

bool PreviewChainPlayer::isLooping() const
{
    return isLoopEnabled;
}

bool PreviewChainPlayer::isPlaying() const
{
    return isPlayingFlag;
}
