//
//  SoundPlayerAndPlayback.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025 and updated by [Your Name] on [Today's Date].
//  This file merges the functionality of SoundPlayer (audio feedback) and the playback-related
//  extension for AudioSliceViewModel (snippet and preview playback) into one file.
//  All references to SoundPlayer and AudioSliceViewModel playback methods remain valid.

import Foundation
import AVFoundation
import SwiftUI

// MARK: - SoundPlayer

class SoundPlayer: NSObject, AVAudioPlayerDelegate {
    static let shared = SoundPlayer()
    private var player: AVAudioPlayer?
    
    private override init() {
        super.init()
    }
    
    func playBleep() {
        guard let url = Bundle.main.url(forResource: "bleep", withExtension: "wav") else {
            print("Bleep sound not found in bundle")
            return
        }
        do {
            player = try AVAudioPlayer(contentsOf: url)
            player?.delegate = self
            player?.prepareToPlay()
            player?.play()
        } catch {
            print("Error playing bleep sound: \(error)")
        }
    }
    
    // New: Play cowbell.wav when an error occurs.
    func playCowbell() {
        guard let url = Bundle.main.url(forResource: "cowbell", withExtension: "wav") else {
            print("Cowbell sound not found in bundle")
            return
        }
        do {
            player = try AVAudioPlayer(contentsOf: url)
            player?.delegate = self
            player?.prepareToPlay()
            player?.play()
        } catch {
            print("Error playing cowbell sound: \(error)")
        }
    }
    
    func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        print("Sound finished playing: \(flag)")
    }
}

// MARK: - AudioSliceViewModel Playback Extension

extension AudioSliceViewModel {
    
    // MARK: - Playback & Interaction
    
    func playSnippetOnce(at index: Int) {
        guard index < previewSnippetURLs.count else { return }
        // Set playingSnippetIndex so that the corresponding grid cell auto-plays
        playingSnippetIndex = index
        let snippetURL = previewSnippetURLs[index]
        do {
            oneShotPlayer = try AVAudioPlayer(contentsOf: snippetURL)
            oneShotPlayer?.play()
        } catch {
            print("Error playing snippet: \(error)")
        }
    }
    
    func togglePlayback() {
        if isPlaying {
            previewPlayer?.pause()
            isPlaying = false
            preparePreviewPlayer()
        } else {
            preparePreviewPlayer()
            previewPlayer?.play()
            isPlaying = true
        }
    }
    
    func preparePreviewPlayer() {
        guard let url = previewChainURL else { return }
        let item = AVPlayerItem(url: url)
        let queuePlayer = AVQueuePlayer()
        previewLooper = AVPlayerLooper(player: queuePlayer, templateItem: item)
        previewPlayer = queuePlayer
    }
}
