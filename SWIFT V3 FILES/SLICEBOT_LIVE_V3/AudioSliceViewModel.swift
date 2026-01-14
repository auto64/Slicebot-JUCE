//
//  AudioSliceViewModel.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025.
//

import SwiftUI
import AVFoundation

class AudioSliceViewModel: ObservableObject {
    // MARK: - Basic Properties
    let cacheFileName = "AudioCache.json"
    
    // Declare sampleRate so that extensions can reference it.
    let sampleRate: Double = 44100.0
    
    @Published var sourceMode: SourceMode = .multi
    @Published var directoryURL: URL? = nil
    @Published var singleFileURL: URL? = nil
    @Published var fileCache: [AudioFileMetadata] = []
    @Published var sliceInfos: [SliceInfo] = []
    @Published var cellUndo: [Int: SliceInfo] = [:]
    @Published var bpm: String = "128.0"
    let noteSubdivisions = ["1/2 bar", "1/4 bar", "8th note", "16th note"]
    @Published var selectedSubdivision: String = "1/4 bar"
    @Published var randomSubdivisionMode: Bool = false
    @Published var transientDetect: Bool = false
    @Published var pachinko: Bool = false
    @Published var fadeEnabled: Bool = true
    @Published var normalizeEnabled: Bool = false
    @Published var reverseEnabled: Bool = false
    @Published var pachinkoReverseEnabled: Bool = false
    let sampleCountOptions = [4, 8, 16, 128]
    var availableSampleCountOptions: [Int] {
        transientDetect ? sampleCountOptions.filter { $0 != 128 } : sampleCountOptions
    }
    @Published var sampleCount: Int = 8

    @Published var layeringMode: Bool = false
    @Published var generateIndividual: Bool = true
    @Published var generateChain: Bool = false
    @Published var mergeMode: LayeringMergeMode = .crossfade
    @Published var statusText: String = "Ready"
    @Published var progress: Double = 0.0
    @Published var previewChainURL: URL? = nil
    @Published var previewSnippetURLs: [URL] = []
    @Published var isPlaying: Bool = false
    @Published var refreshTokens: [Int: UUID] = [:]
    @Published var startSelectMode: Bool = false
    @Published var selectedSliceIndex: Int = 0
    @Published var playingSnippetIndex: Int? = nil
    @Published var exportSettingsLocked: Bool = false
    
    // Caching state – used to disable actions during caching.
    @Published var isCaching: Bool = false
    
    // MARK: - Stutter Properties (Configuration Only)
    @Published var stutterCount: Int = 4
    @Published var stutterVolumeReductionStep: Double = 0.2
    @Published var stutterPitchShiftSemitones: Double = 1.0
    @Published var isStutterModeEnabled: Bool = false
    @Published var stutterHighlightX: CGFloat? = nil
    @Published var stutterTruncateEnabled: Bool = false
    @Published var stutterUndoBackup: [Int: URL] = [:]
    
    // MARK: - Global Pachinko Stutter Toggle
    @Published var pachinkoStutterEnabled: Bool = false
    
    // MARK: - Playback Properties
    @Published var previewPlayer: AVPlayer? = nil
    @Published var previewLooper: AVPlayerLooper? = nil
    
    var recacheTask: Task<Void, Never>? = nil
    var oneShotPlayer: AVAudioPlayer? = nil
    
    // MARK: - Audio Generation Helpers
    let allowedSubdivisionsSteps: [Double] = [8, 4, 2, 1]
    let allowedTotalSteps: [Double] = [16, 32, 64, 128]
    let uniformMap: [String: Double] = [
        "1/2 bar": 8,
        "1/4 bar": 4,
        "8th note": 2,
        "16th note": 1
    ]
    
    var tempPreviewFolder: URL {
        URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("AudioSnippetPreview")
    }
    
    let commonFormat = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                     sampleRate: 44100.0,
                                     channels: 1,
                                     interleaved: true)!
    
    // MARK: - Computed Properties
    
    var computedBPM: Double? {
        Double(bpm)
    }
    
    // NEW: Required minimum duration is 32 beats.
    var requiredMinimumDuration: Double? {
        if let bpmVal = computedBPM, bpmVal > 0 {
            return (60.0 / bpmVal) * 32.0
        }
        return (60.0 / 128.0) * 32.0
    }
    
    // computedNoGoZone remains unchanged.
    var computedNoGoZone: Double? {
        if let bpmVal = computedBPM {
            let twoBar = (60.0 / bpmVal) * 8.0
            return ceil(twoBar)
        }
        return nil
    }
    
    var lastExportDirectory: URL? {
        if let path = UserDefaults.standard.string(forKey: "LastExportDirectory") {
            return URL(fileURLWithPath: path)
        }
        return nil
    }
    
    var lastExportPrefix: String {
        UserDefaults.standard.string(forKey: "LastExportPrefix") ?? "export"
    }
    
    // MARK: - Copy & Paste Functionality
    @Published var copiedSlice: (slice: SliceInfo, volume: (volume: CGFloat, isMuted: Bool))?
    
    func copySelectedSlice() {
        guard selectedSliceIndex < sliceInfos.count else { return }
        let slice = sliceInfos[selectedSliceIndex]
        let volume = sliceVolumeSettings[selectedSliceIndex] ?? (volume: 0.75, isMuted: false)
        copiedSlice = (slice: slice, volume: volume)
        statusText = "Slice copied."
    }
    
    func pasteToSelectedSlice() async {
        guard let copied = copiedSlice, selectedSliceIndex < sliceInfos.count else {
            statusText = "No slice copied or invalid selection."
            return
        }
        sliceInfos[selectedSliceIndex] = copied.slice
        sliceVolumeSettings[selectedSliceIndex] = copied.volume
        await regenerateSnippet(at: selectedSliceIndex)
        statusText = "Slice pasted."
    }
    
    // MARK: - startSelectSnippet Method (Shift‑Click Editing)
    func startSelectSnippet(at index: Int, relativeX: CGFloat, viewWidth: CGFloat) {
        let fraction = Double(relativeX / viewWidth)
        let oldSlice = sliceInfos[index]
        let snippetFrameCount = oldSlice.snippetFrameCount
        let newStartFrame = oldSlice.startFrame + Int(round(fraction * Double(snippetFrameCount)))
        let asset = AVURLAsset(url: oldSlice.fileURL)
        let fileDurationSec = CMTimeGetSeconds(asset.duration)
        let fileTotalFrames = Int(round(fileDurationSec * sampleRate))
        let adjustedStartFrame = newStartFrame > fileTotalFrames - snippetFrameCount ? max(0, fileTotalFrames - snippetFrameCount) : newStartFrame
        sliceInfos[index] = SliceInfo(fileURL: oldSlice.fileURL,
                                      startFrame: adjustedStartFrame,
                                      subdivisionSteps: oldSlice.subdivisionSteps,
                                      snippetFrameCount: snippetFrameCount)
        Task {
            let startSec = Double(adjustedStartFrame) / sampleRate
            let durationSec = Double(snippetFrameCount) / sampleRate
            let startCM = CMTime(seconds: startSec, preferredTimescale: 6000)
            let durationCM = CMTime(seconds: durationSec, preferredTimescale: 6000)
            let tempFolder = tempPreviewFolder
            let tempURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_startselect_\(index).wav")
            let taskAsset = AVURLAsset(url: oldSlice.fileURL)
            let success = await AudioProcessor.exportSnippet(asset: taskAsset,
                                                             startTime: startCM,
                                                             duration: durationCM,
                                                             outputURL: tempURL,
                                                             applyFade: fadeEnabled,
                                                             normalize: normalizeEnabled,
                                                             reverse: reverseEnabled,
                                                             pachinkoReverse: pachinkoReverseEnabled)
            if success {
                previewSnippetURLs[index] = tempURL
                refreshTokens[index] = UUID()
                if let chainBuffer = AudioProcessor.concatenateWavFiles(urls: previewSnippetURLs) {
                    let previewURL = tempFolder.appendingPathComponent("preview_chain.wav")
                    let settings: [String: Any] = [
                        AVFormatIDKey: kAudioFormatLinearPCM,
                        AVSampleRateKey: sampleRate,
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
                            previewChainURL = previewURL
                            statusText = "Slice \(index + 1) updated via Start Select."
                        }
                    } catch {
                        await MainActor.run { statusText = "Error writing preview chain file: \(error)" }
                    }
                } else {
                    await MainActor.run { statusText = "Failed to reassemble preview chain." }
                }
            } else {
                await MainActor.run { statusText = "Failed to generate snippet \(index + 1) via Start Select." }
            }
        }
    }
    
    // MARK: - Slice Volume Settings
    @Published var sliceVolumeSettings: [Int: (volume: CGFloat, isMuted: Bool)] = [:]
    
    // regenerateSnippet(at:) and other processing methods are defined in extensions.
}
