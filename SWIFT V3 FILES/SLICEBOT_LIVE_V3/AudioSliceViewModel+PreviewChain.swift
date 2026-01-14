//
//  AudioSliceViewModel+PreviewChain.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025.
//  Updated by [Your Name] on [Today's Date].
//  This extension contains methods to update the preview chain and looping chain
//  (i.e. writing the concatenated preview snippets to disk and preparing the player).
//

import SwiftUI
import AVFoundation
import AppKit

extension AudioSliceViewModel {
    
    func updatePreviewChain() async {
        if isCaching {
            await MainActor.run { self.statusText = "Cannot update preview chain during caching." }
            return
        }
        guard !previewSnippetURLs.isEmpty,
              let chainBuffer = AudioProcessor.concatenateWavFiles(urls: previewSnippetURLs) else {
            await MainActor.run {
                self.statusText = "Failed to update preview chain."
            }
            return
        }
        let previewURL = tempPreviewFolder.appendingPathComponent("preview_chain.wav")
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: self.sampleRate,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        do {
            let chainFile = try AVAudioFile(forWriting: previewURL,
                                            settings: settings,
                                            commonFormat: .pcmFormatInt16,
                                            interleaved: true)
            try chainFile.write(from: chainBuffer)
            await MainActor.run {
                self.previewChainURL = previewURL
                self.statusText = "Preview chain updated."
            }
            preparePreviewPlayer()
        } catch {
            await MainActor.run { self.statusText = "Error writing preview chain file: \(error)" }
        }
    }
    
    func updateLoopingChainWithVolume() async {
        if isCaching {
            await MainActor.run { self.statusText = "Cannot update looping chain during caching." }
            return
        }
        guard !previewSnippetURLs.isEmpty else {
            await MainActor.run { self.statusText = "No preview snippets to update looping chain." }
            return
        }
        var adjustedSnippetURLs: [URL] = []
        let tempFolder = tempPreviewFolder
        for (index, snippetURL) in previewSnippetURLs.enumerated() {
            let settings = sliceVolumeSettings[index] ?? (volume: 0.75, isMuted: false)
            let volumeValue = settings.volume
            let isMuted = settings.isMuted
            let multiplier: Float = isMuted ? 0.0 : Float(VolumeControl.dbToLinear(VolumeControl.sliderValueToDb(volumeValue)))
            let adjustedURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_adjusted_\(index).wav")
            let success = AudioProcessor.exportSnippetWithVolume(from: snippetURL,
                                                                  volumeMultiplier: multiplier,
                                                                  outputURL: adjustedURL)
            if !success {
                await MainActor.run {
                    self.statusText = "Error processing slice \(index + 1) for looping chain update."
                }
                return
            }
            adjustedSnippetURLs.append(adjustedURL)
        }
        
        guard let chainBuffer = AudioProcessor.concatenateWavFiles(urls: adjustedSnippetURLs) else {
            await MainActor.run { self.statusText = "Failed to concatenate volume-adjusted snippets for looping chain." }
            return
        }
        
        let settingsDict: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: self.sampleRate,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        
        let loopChainURL = tempPreviewFolder.appendingPathComponent("loop_chain.wav")
        
        if FileManager.default.fileExists(atPath: loopChainURL.path) {
            do {
                try FileManager.default.removeItem(at: loopChainURL)
            } catch {
                await MainActor.run { self.statusText = "Error removing existing looping chain file: \(error)" }
                return
            }
        }
        
        do {
            let chainFile = try AVAudioFile(forWriting: loopChainURL,
                                            settings: settingsDict,
                                            commonFormat: .pcmFormatInt16,
                                            interleaved: true)
            try chainFile.write(from: chainBuffer)
            await MainActor.run {
                self.previewChainURL = loopChainURL
                self.statusText = "Looping sample chain updated."
            }
            preparePreviewPlayer()
        } catch {
            await MainActor.run { self.statusText = "Error writing looping chain file: \(error)" }
        }
    }
}
