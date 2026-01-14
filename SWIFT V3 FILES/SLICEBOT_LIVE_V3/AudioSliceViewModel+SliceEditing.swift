import SwiftUI
import AVFoundation
import AppKit

// MARK: - Supporting Types and Functions

// Define LiveModuleInfo so that live-mode code compiles.
struct LiveModuleInfo: Codable, Equatable {
    var moduleNumber: Int
    var filePath: String
    var elapsedTime: TimeInterval
    var isActive: Bool
    var isLinked: Bool
}

// Define loadLiveModuleInfos so that live-mode code compiles.
private func loadLiveModuleInfos(from url: URL) -> [LiveModuleInfo]? {
    do {
        let data = try Data(contentsOf: url)
        let infos = try JSONDecoder().decode([LiveModuleInfo].self, from: data)
        return infos
    } catch {
        return nil
    }
}

// Provide a simple implementation for generateRandomSubdivisions.
private func generateRandomSubdivisions(for count: Int, allowed: [Double], targets: [Double]) -> [Double]? {
    // For simplicity, return an array of random allowed values.
    var subdivisions: [Double] = []
    for _ in 0..<count {
        if let value = allowed.randomElement() {
            subdivisions.append(value)
        } else {
            subdivisions.append(4) // fallback value
        }
    }
    return subdivisions
}

// In case these constants are not available in the current scope, define them here.
private let allowedSubdivisionsSteps: [Double] = [8, 4, 2, 1]
private let allowedTotalSteps: [Double] = [16, 32, 64, 128]
private let uniformMap: [String: Double] = [
    "1/2 bar": 8,
    "1/4 bar": 4,
    "8th note": 2,
    "16th note": 1
]

// MARK: - AudioSliceViewModel+SliceEditing Extension

extension AudioSliceViewModel {
    
    // MARK: - Shuffle Slices
    func shuffleSlices() {
        if isCaching {
            Task { @MainActor in
                self.statusText = "Cannot shuffle slices during caching."
            }
            print("[DEBUG] Shuffle aborted due to caching")
            return
        }
        
        let oldSelectedSlice: SliceInfo? = (selectedSliceIndex < sliceInfos.count ? sliceInfos[selectedSliceIndex] : nil)
        let count = previewSnippetURLs.count
        let combined: [(slice: SliceInfo, snippetURL: URL, oldIndex: Int)] = (0..<count).map { index in
            (sliceInfos[index], previewSnippetURLs[index], index)
        }.shuffled()
        
        sliceInfos = combined.map { $0.slice }
        previewSnippetURLs = combined.map { $0.snippetURL }
        
        var newVolumeSettings: [Int: (volume: CGFloat, isMuted: Bool)] = [:]
        for (newIndex, element) in combined.enumerated() {
            let oldIndex = element.oldIndex
            newVolumeSettings[newIndex] = sliceVolumeSettings[oldIndex] ?? (volume: 0.75, isMuted: false)
        }
        sliceVolumeSettings = newVolumeSettings
        
        if let oldSelected = oldSelectedSlice,
           let newIndex = sliceInfos.firstIndex(where: { $0.fileURL == oldSelected.fileURL &&
                                                         $0.startFrame == oldSelected.startFrame &&
                                                         $0.subdivisionSteps == oldSelected.subdivisionSteps }) {
            selectedSliceIndex = newIndex
        } else {
            selectedSliceIndex = 0
        }
        
        for i in previewSnippetURLs.indices {
            refreshTokens[i] = UUID()
        }
        Task {
            await updatePreviewChain()
        }
    }
    
    // MARK: - Export Slice with Retries
    private func exportSliceWithRetries(asset: AVURLAsset, startTime: CMTime, duration: CMTime, outputURL: URL) async -> Bool {
        let maxAttempts = 5
        let delayNanoseconds: UInt64 = 500_000_000 // 500ms
        print("[DEBUG] Starting exportSliceWithRetries for file: \(asset.url.path)")
        for attempt in 1...maxAttempts {
            print("[DEBUG] Attempt \(attempt) for exporting slice from file: \(asset.url.path)")
            let success = await AudioProcessor.exportSnippet(asset: asset,
                                                             startTime: startTime,
                                                             duration: duration,
                                                             outputURL: outputURL,
                                                             applyFade: fadeEnabled,
                                                             normalize: normalizeEnabled,
                                                             reverse: reverseEnabled,
                                                             pachinkoReverse: pachinkoReverseEnabled)
            if success {
                print("[DEBUG] Export succeeded for file: \(asset.url.path) on attempt \(attempt)")
                return true
            }
            await MainActor.run {
                self.statusText = "Slice export failed (attempt \(attempt) of \(maxAttempts)). Retrying..."
            }
            print("[DEBUG] Export attempt \(attempt) failed for file: \(asset.url.path)")
            await Task.sleep(delayNanoseconds)
        }
        print("[DEBUG] Export failed after \(maxAttempts) attempts for file: \(asset.url.path)")
        return false
    }
    
    // MARK: - Generate Preview Chain
    func generatePreviewChain() {
        print("[DEBUG] Starting generatePreviewChain with sourceMode: \(sourceMode.rawValue) and BPM: \(bpm)")
        if isCaching {
            Task { @MainActor in
                self.statusText = "Cannot generate preview chain during caching."
            }
            print("[DEBUG] generatePreviewChain aborted due to caching")
            return
        }
        guard let bpmValue = computedBPM else {
            self.statusText = "Invalid BPM value."
            print("[DEBUG] BPM value invalid: \(bpm)")
            return
        }
        
        var availableFiles: [AudioFileMetadata] = []
        switch sourceMode {
        case .multi:
            availableFiles = fileCache.filter { $0.isCandidate }
            print("[DEBUG] Multi-mode: Found \(availableFiles.count) candidate files")
        case .singleRandom:
            if let randomFile = fileCache.filter({ $0.isCandidate }).randomElement() {
                availableFiles = [randomFile]
                print("[DEBUG] SingleRandom mode: Using file \(randomFile.fileURL.path)")
            } else {
                self.statusText = "No file available."
                print("[DEBUG] SingleRandom mode: No candidate file available")
                return
            }
        case .singleManual:
            if let file = singleFileURL {
                let asset = AVURLAsset(url: file)
                let duration = CMTimeGetSeconds(asset.duration)
                print("[DEBUG] SingleManual mode: Selected file \(file.path) with duration \(duration) sec")
                if duration <= 0 {
                    self.statusText = "Invalid file duration."
                    print("[DEBUG] SingleManual mode: File duration invalid")
                    return
                }
                availableFiles = [AudioFileMetadata(fileURL: file, duration: duration, isCandidate: true)]
            } else {
                DispatchQueue.main.async {
                    let alert = NSAlert()
                    alert.messageText = "No file selected"
                    alert.informativeText = "Please select a file by clicking the 'Source' button."
                    alert.addButton(withTitle: "OK")
                    alert.runModal()
                    self.statusText = "Please select a file using the 'Source' button."
                }
                print("[DEBUG] SingleManual mode: No file selected")
                return
            }
        case .live:
            let fm = FileManager.default
            guard let supportDir = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
                self.statusText = "Application Support directory not found."
                print("[DEBUG] Live mode: Application Support directory not found")
                return
            }
            let liveDir = supportDir.appendingPathComponent("LiveRecordings")
            let metadataURL = liveDir.appendingPathComponent("liveModules.json")
            var liveModuleInfos: [LiveModuleInfo] = []
            if fm.fileExists(atPath: metadataURL.path),
               let infos = loadLiveModuleInfos(from: metadataURL) {
                liveModuleInfos = infos
                print("[DEBUG] Live mode: Loaded \(liveModuleInfos.count) live module infos")
            }
            let activeInfos = liveModuleInfos.filter { $0.isActive }
            availableFiles = activeInfos.compactMap { info in
                let url = URL(fileURLWithPath: info.filePath)
                let asset = AVURLAsset(url: url)
                let duration = CMTimeGetSeconds(asset.duration)
                if duration > 0 {
                    return AudioFileMetadata(fileURL: url, duration: duration, isCandidate: true)
                } else {
                    return nil
                }
            }
            print("[DEBUG] Live mode: Found \(availableFiles.count) active live recording files")
            if availableFiles.isEmpty {
                self.statusText = "No live recordings available."
                print("[DEBUG] Live mode: No live recordings available")
                return
            }
        }
        
        // Always apply the no-go zone.
        let noGoZone = computedNoGoZone ?? 0.0
        
        let countNeeded = layeringMode ? sampleCount * 2 : sampleCount
        print("[DEBUG] Need to generate \(countNeeded) slices")
        var sliceDurations: [Double] = []
        if randomSubdivisionMode {
            if layeringMode {
                let layerCount = sampleCount
                let layerTargets = allowedTotalSteps.map { $0 / 2 }
                guard let randomSubs = generateRandomSubdivisions(for: layerCount, allowed: allowedSubdivisionsSteps, targets: layerTargets) else {
                    self.statusText = "Failed to generate random subdivisions for layering."
                    print("[DEBUG] Failed to generate random subdivisions for layering")
                    return
                }
                sliceDurations = randomSubs + randomSubs
                print("[DEBUG] Random subdivisions (layering): \(sliceDurations)")
            } else {
                guard let randomSubs = generateRandomSubdivisions(for: countNeeded, allowed: allowedSubdivisionsSteps, targets: allowedTotalSteps) else {
                    self.statusText = "Failed to generate random subdivisions."
                    print("[DEBUG] Failed to generate random subdivisions for non-layering")
                    return
                }
                sliceDurations = randomSubs
                print("[DEBUG] Random subdivisions: \(sliceDurations)")
            }
        } else {
            let uniformVal = uniformMap[selectedSubdivision] ?? 4
            sliceDurations = Array(repeating: uniformVal, count: countNeeded)
            print("[DEBUG] Uniform subdivision using \(selectedSubdivision): \(uniformVal) for all \(countNeeded) slices")
        }
        
        var tempSnippetURLs: [URL] = []
        let tempFolder = tempPreviewFolder
        do {
            try FileManager.default.removeItem(at: tempFolder)
            print("[DEBUG] Removed existing temp folder at \(tempFolder.path)")
        } catch {
            print("[DEBUG] Temp folder removal error: \(error)")
        }
        do {
            try FileManager.default.createDirectory(at: tempFolder, withIntermediateDirectories: true, attributes: nil)
            print("[DEBUG] Created temp folder at \(tempFolder.path)")
        } catch {
            print("[DEBUG] Failed to create temp folder: \(error)")
        }
        
        Task {
            await MainActor.run {
                self.sliceInfos = []
                self.previewSnippetURLs = []
            }
            for i in 1...countNeeded {
                print("[DEBUG] Processing slice \(i) of \(countNeeded)")
                if isCaching {
                    await MainActor.run {
                        self.statusText = "Action cancelled: caching in progress."
                    }
                    print("[DEBUG] Slice generation aborted due to caching at slice \(i)")
                    return
                }
                let fileMeta = availableFiles.randomElement()!
                print("[DEBUG] Selected candidate file for slice \(i): \(fileMeta.fileURL.path) with duration \(fileMeta.duration) sec")
                let subdivision = sliceDurations[i - 1]
                let snippetDurationSeconds = (60.0 / bpmValue) * (subdivision / 4.0)
                let snippetFrameCount = Int(round(snippetDurationSeconds * self.sampleRate))
                print("[DEBUG] Calculated snippet duration: \(snippetDurationSeconds) sec, frame count: \(snippetFrameCount), subdivision: \(subdivision)")
                var snippetStart: Double = 0
                let asset = AVURLAsset(url: fileMeta.fileURL)
                let fileDuration = CMTimeGetSeconds(asset.duration)
                print("[DEBUG] File duration: \(fileDuration) sec")
                // Use file duration minus no-go zone.
                let maxCandidate = max(0, fileDuration - noGoZone)
                if transientDetect {
                    if let refined = await refinedStart(for: asset, snippetDuration: snippetDurationSeconds, bpm: bpmValue) {
                        snippetStart = refined
                        print("[DEBUG] Transient detection refined start for slice \(i): \(snippetStart) sec")
                    } else {
                        print("[DEBUG] Dropping slice \(i): transient detection failed for file \(fileMeta.fileURL.path)")
                        continue
                    }
                } else {
                    snippetStart = Double.random(in: 0...maxCandidate)
                    print("[DEBUG] Random snippet start for slice \(i): \(snippetStart) sec")
                }
                await MainActor.run {
                    self.sliceInfos.append(SliceInfo(fileURL: fileMeta.fileURL,
                                                      startFrame: Int(round(snippetStart * self.sampleRate)),
                                                      subdivisionSteps: subdivision,
                                                      snippetFrameCount: snippetFrameCount))
                }
                let tempURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_\(i).wav")
                let startTime = CMTime(seconds: snippetStart, preferredTimescale: 6000)
                let duration = CMTime(seconds: snippetDurationSeconds, preferredTimescale: 6000)
                print("[DEBUG] Exporting slice \(i) from file \(fileMeta.fileURL.path)")
                let exportSucceeded = await exportSliceWithRetries(asset: asset,
                                                                   startTime: startTime,
                                                                   duration: duration,
                                                                   outputURL: tempURL)
                if !exportSucceeded {
                    print("[DEBUG] Dropping slice \(i) from file: \(fileMeta.fileURL.path)")
                    await MainActor.run {
                        self.statusText = "Failed to export slice \(i) after multiple attempts. Dropped file: \(fileMeta.fileURL.lastPathComponent)"
                    }
                    continue
                }
                print("[DEBUG] Slice \(i) exported successfully to \(tempURL.path)")
                tempSnippetURLs.append(tempURL)
                print("[DEBUG] Waiting 5ms before next slice export...")
                await Task.sleep(5_000_000)
                await MainActor.run {
                    self.progress = (Double(i) / Double(countNeeded)) * 100.0
                    self.statusText = "Generating preview snippet \(i) of \(countNeeded)..."
                }
            }
            
            if layeringMode && tempSnippetURLs.count >= sampleCount * 2 {
                var layered: [URL] = []
                print("[DEBUG] Layering mode active. Merging \(sampleCount) pairs of slices.")
                for i in 0..<sampleCount {
                    let url1 = tempSnippetURLs[i]
                    let url2 = tempSnippetURLs[i + sampleCount]
                    print("[DEBUG] Merging slice pair: \(url1.lastPathComponent) + \(url2.lastPathComponent)")
                    if let merged = AudioProcessor.mergeTwoSnippetsInt16(url1: url1,
                                                                         url2: url2,
                                                                         mergeMode: mergeMode,
                                                                         tempFolder: tempFolder) {
                        layered.append(merged)
                        print("[DEBUG] Merged slice pair \(i+1) successfully into \(merged.lastPathComponent)")
                    } else {
                        print("[DEBUG] Failed to merge slice pair \(i+1)")
                    }
                }
                await MainActor.run {
                    self.previewSnippetURLs = layered
                }
            } else {
                await MainActor.run {
                    self.previewSnippetURLs = tempSnippetURLs
                }
            }
            await MainActor.run {
                for idx in previewSnippetURLs.indices {
                    self.refreshTokens[idx] = UUID()
                }
            }
            if let chainBuffer = AudioProcessor.concatenateWavFiles(urls: previewSnippetURLs) {
                let previewURL = tempFolder.appendingPathComponent("preview_chain.wav")
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
                        self.statusText = "Preview generated."
                        self.progress = 100.0
                    }
                    print("[DEBUG] Preview chain generated successfully at \(previewURL.path)")
                    await MainActor.run { self.preparePreviewPlayer() }
                } catch {
                    await MainActor.run { self.statusText = "Error writing preview chain file: \(error)" }
                    print("[DEBUG] Error writing preview chain file: \(error)")
                }
            } else {
                await MainActor.run { self.statusText = "Failed to generate preview chain." }
                print("[DEBUG] Concatenation of preview snippets failed.")
            }
            if self.pachinkoStutterEnabled {
                print("[DEBUG] Pachinko stutter enabled after regeneration. Applying stutter effect.")
                await self.applyPachinkoStutter()
            }
        }
    }
    
    // MARK: - Reslice All Snippets
    func resliceAllSnippets() async {
        print("[DEBUG] Starting resliceAllSnippets")
        if isCaching {
            await MainActor.run { self.statusText = "Cannot reslice during caching." }
            print("[DEBUG] Reslice aborted due to caching")
            return
        }
        if layeringMode {
            for index in 0..<sampleCount {
                await resliceSnippet(at: index)
            }
        } else {
            for index in previewSnippetURLs.indices {
                await resliceSnippet(at: index)
            }
        }
    }
    
    // MARK: - Reslice Single Snippet
    func resliceSnippet(at index: Int) async {
        print("[DEBUG] Starting resliceSnippet at index \(index)")
        if isCaching {
            await MainActor.run { self.statusText = "Cannot reslice snippet during caching." }
            print("[DEBUG] Reslice aborted due to caching at index \(index)")
            return
        }
        if layeringMode {
            guard let bpmValue = computedBPM else {
                await MainActor.run { self.statusText = "Invalid BPM value." }
                print("[DEBUG] Reslice layering mode aborted: invalid BPM")
                return
            }
            let leftIndex = index
            let rightIndex = index + sampleCount
            guard leftIndex < sliceInfos.count, rightIndex < sliceInfos.count else {
                print("[DEBUG] Reslice layering mode aborted: Not enough sliceInfos")
                return
            }
            let leftSlice = sliceInfos[leftIndex]
            let rightSlice = sliceInfos[rightIndex]
            let leftAsset = AVURLAsset(url: leftSlice.fileURL)
            let rightAsset = AVURLAsset(url: rightSlice.fileURL)
            let leftDuration = CMTimeGetSeconds(leftAsset.duration)
            let rightDuration = CMTimeGetSeconds(rightAsset.duration)
            let leftSnippetSeconds = Double(leftSlice.snippetFrameCount) / self.sampleRate
            let rightSnippetSeconds = Double(rightSlice.snippetFrameCount) / self.sampleRate
            // Apply no-go zone here as well.
            let noGoZone = computedNoGoZone ?? leftSnippetSeconds
            let maxLeftCandidate = max(0, leftDuration - noGoZone)
            let maxRightCandidate = max(0, rightDuration - noGoZone)
            let newLeftStart = Int.random(in: 0...Int(maxLeftCandidate))
            let newRightStart = Int.random(in: 0...Int(maxRightCandidate))
            print("[DEBUG] Reslicing layering mode: left new start \(newLeftStart), right new start \(newRightStart)")
            await MainActor.run {
                sliceInfos[leftIndex] = SliceInfo(fileURL: leftSlice.fileURL,
                                                  startFrame: newLeftStart,
                                                  subdivisionSteps: leftSlice.subdivisionSteps,
                                                  snippetFrameCount: leftSlice.snippetFrameCount)
                sliceInfos[rightIndex] = SliceInfo(fileURL: rightSlice.fileURL,
                                                   startFrame: newRightStart,
                                                   subdivisionSteps: rightSlice.subdivisionSteps,
                                                   snippetFrameCount: rightSlice.snippetFrameCount)
            }
            Task {
                let leftStartSec = Double(newLeftStart) / self.sampleRate
                let leftDurationSec = Double(leftSlice.snippetFrameCount) / self.sampleRate
                let rightStartSec = Double(newRightStart) / self.sampleRate
                let rightDurationSec = Double(rightSlice.snippetFrameCount) / self.sampleRate
                let leftStartTime = CMTime(seconds: leftStartSec, preferredTimescale: 6000)
                let leftDur = CMTime(seconds: leftDurationSec, preferredTimescale: 6000)
                let rightStartTime = CMTime(seconds: rightStartSec, preferredTimescale: 6000)
                let rightDur = CMTime(seconds: rightDurationSec, preferredTimescale: 6000)
                let tempFolder = tempPreviewFolder
                let leftTempURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_reslice_left_\(index).wav")
                let rightTempURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_reslice_right_\(index).wav")
                print("[DEBUG] Reslice layering mode: Exporting left slice to \(leftTempURL.path)")
                let leftSuccess = await exportSliceWithRetries(asset: leftAsset,
                                                               startTime: leftStartTime,
                                                               duration: leftDur,
                                                               outputURL: leftTempURL)
                print("[DEBUG] Reslice layering mode: Exporting right slice to \(rightTempURL.path)")
                let rightSuccess = await exportSliceWithRetries(asset: rightAsset,
                                                                startTime: rightStartTime,
                                                                duration: rightDur,
                                                                outputURL: rightTempURL)
                if leftSuccess && rightSuccess {
                    if let merged = AudioProcessor.mergeTwoSnippetsInt16(url1: leftTempURL,
                                                                         url2: rightTempURL,
                                                                         mergeMode: mergeMode,
                                                                         tempFolder: tempFolder) {
                        await MainActor.run {
                            previewSnippetURLs[index] = merged
                            refreshTokens[index] = UUID()
                        }
                        if let chainBuffer = AudioProcessor.concatenateWavFiles(urls: previewSnippetURLs) {
                            let previewURL = tempFolder.appendingPathComponent("preview_chain.wav")
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
                                    previewChainURL = previewURL
                                    self.statusText = "Layered slice \(index + 1) resliced and chain updated."
                                }
                                print("[DEBUG] Reslice layering mode: Merged slice \(index + 1) updated successfully")
                                await MainActor.run { self.preparePreviewPlayer() }
                            } catch {
                                await MainActor.run { self.statusText = "Error writing preview chain file: \(error)" }
                                print("[DEBUG] Reslice layering mode: Error writing chain file: \(error)")
                            }
                        }
                    }
                } else {
                    await MainActor.run { self.statusText = "Reslice failed for layered slice \(index + 1)." }
                    print("[DEBUG] Reslice layering mode: Export failed for slice pair at index \(index)")
                }
            }
        } else {
            let originalSlice = sliceInfos[index]
            let fileURL = originalSlice.fileURL
            guard let bpmValue = computedBPM else {
                await MainActor.run { self.statusText = "Invalid BPM value." }
                print("[DEBUG] Reslice aborted: Invalid BPM value")
                return
            }
            let asset = AVURLAsset(url: fileURL)
            let durationSec = CMTimeGetSeconds(asset.duration)
            guard durationSec > 0 else {
                await MainActor.run { self.statusText = "Invalid file duration." }
                print("[DEBUG] Reslice aborted: File duration invalid for \(fileURL.path)")
                return
            }
            let subdivision = uniformMap[selectedSubdivision] ?? 4
            let snippetDurationSeconds = (60.0 / bpmValue) * (subdivision / 4.0)
            let snippetFrameCount = Int(round(snippetDurationSeconds * self.sampleRate))
            var snippetStart: Double = 0
            // Apply no-go zone here as well.
            let noGoZone = computedNoGoZone ?? snippetDurationSeconds
            let maxCandidate = max(0, durationSec - noGoZone)
            if transientDetect {
                let secondsPerBeat = 60.0 / bpmValue
                let detectionDuration = 4.0 * secondsPerBeat
                if durationSec < detectionDuration {
                    await MainActor.run { self.statusText = "Transient detection: audio file too short." }
                    print("[DEBUG] Reslice aborted: File too short for transient detection for \(fileURL.path)")
                    return
                }
                if let refined = await refinedStart(for: asset, snippetDuration: snippetDurationSeconds, bpm: bpmValue) {
                    snippetStart = refined
                    print("[DEBUG] Reslice transient detection refined start: \(snippetStart) sec")
                } else {
                    snippetStart = Double.random(in: 0...maxCandidate)
                    print("[DEBUG] Reslice transient detection fallback random start: \(snippetStart) sec")
                }
            } else {
                snippetStart = Double.random(in: 0...maxCandidate)
                print("[DEBUG] Reslice random start: \(snippetStart) sec")
            }
            let tempFolder = tempPreviewFolder
            let tempURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_reslice_\(index).wav")
            let startTime = CMTime(seconds: snippetStart, preferredTimescale: 6000)
            let duration = CMTime(seconds: snippetDurationSeconds, preferredTimescale: 6000)
            print("[DEBUG] Reslicing non-layering mode for slice \(index + 1) from file \(fileURL.path)")
            let exportSucceeded = await exportSliceWithRetries(asset: asset,
                                                               startTime: startTime,
                                                               duration: duration,
                                                               outputURL: tempURL)
            if !exportSucceeded {
                await MainActor.run { self.statusText = "Reslice failed for snippet \(index + 1)." }
                print("[DEBUG] Reslice failed for slice \(index + 1) from file \(fileURL.path)")
                return
            }
            await MainActor.run {
                sliceInfos[index] = SliceInfo(fileURL: fileURL,
                                              startFrame: Int(round(snippetStart * self.sampleRate)),
                                              subdivisionSteps: subdivision,
                                              snippetFrameCount: snippetFrameCount)
                previewSnippetURLs[index] = tempURL
                refreshTokens[index] = UUID()
            }
            if let chainBuffer = AudioProcessor.concatenateWavFiles(urls: previewSnippetURLs) {
                let previewURL = tempFolder.appendingPathComponent("preview_chain.wav")
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
                        previewChainURL = previewURL
                        self.statusText = "Slice \(index + 1) updated via Start Select."
                    }
                    print("[DEBUG] Reslice non-layering mode: Slice \(index + 1) updated successfully")
                    await MainActor.run { self.preparePreviewPlayer() }
                } catch {
                    await MainActor.run { self.statusText = "Error writing preview chain file: \(error)" }
                    print("[DEBUG] Reslice non-layering mode: Error writing chain file: \(error)")
                }
            } else {
                await MainActor.run { self.statusText = "Failed to reassemble preview chain." }
                print("[DEBUG] Reslice non-layering mode: Failed to concatenate preview snippets")
            }
        }
    }
    
    // MARK: - Regenerate Single Snippet
    func regenerateSnippet(at index: Int) async {
        print("[DEBUG] Starting regenerateSnippet for slice \(index + 1)")
        if isCaching {
            await MainActor.run { self.statusText = "Cannot regenerate slice during caching." }
            print("[DEBUG] Regenerate snippet aborted due to caching at index \(index)")
            return
        }
        guard let bpmValue = computedBPM, index < sliceInfos.count else { return }
        let slice = sliceInfos[index]
        let subdivision = slice.subdivisionSteps
        let snippetSeconds = Double(slice.snippetFrameCount) / self.sampleRate
        let startTime = CMTime(seconds: Double(slice.startFrame) / self.sampleRate, preferredTimescale: 6000)
        let duration = CMTime(seconds: snippetSeconds, preferredTimescale: 6000)
        let tempFolder = tempPreviewFolder
        let tempURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_regen_\(index).wav")
        let asset = AVURLAsset(url: slice.fileURL)
        _ = await AudioProcessor.exportSnippet(asset: asset,
                                               startTime: startTime,
                                               duration: duration,
                                               outputURL: tempURL,
                                               applyFade: fadeEnabled,
                                               normalize: normalizeEnabled,
                                               reverse: reverseEnabled,
                                               pachinkoReverse: pachinkoReverseEnabled)
        await MainActor.run {
            previewSnippetURLs[index] = tempURL
            refreshTokens[index] = UUID()
            sliceInfos[index] = SliceInfo(fileURL: slice.fileURL,
                                          startFrame: slice.startFrame,
                                          subdivisionSteps: slice.subdivisionSteps,
                                          snippetFrameCount: slice.snippetFrameCount)
        }
        if let chainBuffer = AudioProcessor.concatenateWavFiles(urls: previewSnippetURLs) {
            let previewURL = tempFolder.appendingPathComponent("preview_chain.wav")
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
                    previewChainURL = previewURL
                    self.statusText = "Preview regenerated."
                    self.progress = 100.0
                }
                print("[DEBUG] Regenerate snippet for slice \(index + 1) succeeded")
                await MainActor.run { self.preparePreviewPlayer() }
            } catch {
                await MainActor.run { self.statusText = "Error writing preview chain file: \(error)" }
                print("[DEBUG] Regenerate snippet: Error writing chain file: \(error)")
            }
        }
    }
    
    // MARK: - Regenerate All Snippets
    func regenerateSnippets() async {
        print("[DEBUG] Starting regenerateSnippets")
        if isCaching {
            await MainActor.run { self.statusText = "Cannot regenerate snippets during caching." }
            print("[DEBUG] Regenerate snippets aborted due to caching")
            return
        }
        guard !sliceInfos.isEmpty else {
            await MainActor.run { self.statusText = "No previous slice data available. Please generate slices first." }
            print("[DEBUG] Regenerate snippets aborted: No sliceInfos available")
            return
        }
        guard let bpmValue = computedBPM else {
            await MainActor.run { self.statusText = "Invalid BPM value." }
            print("[DEBUG] Regenerate snippets aborted: Invalid BPM")
            return
        }
        let totalCount = sliceInfos.count
        var regeneratedURLs: [URL] = []
        let tempFolder = tempPreviewFolder
        do {
            try FileManager.default.removeItem(at: tempFolder)
            print("[DEBUG] Removed temp folder at \(tempFolder.path) for regeneration")
        } catch {
            print("[DEBUG] Error removing temp folder: \(error)")
        }
        do {
            try FileManager.default.createDirectory(at: tempFolder, withIntermediateDirectories: true, attributes: nil)
            print("[DEBUG] Created temp folder at \(tempFolder.path) for regeneration")
        } catch {
            print("[DEBUG] Failed to create temp folder: \(error)")
        }
        
        for (i, slice) in sliceInfos.enumerated() {
            print("[DEBUG] Regenerating slice \(i + 1) of \(totalCount)")
            if isCaching {
                await MainActor.run { self.statusText = "Cannot regenerate snippets during caching." }
                print("[DEBUG] Regenerate aborted due to caching during slice \(i + 1)")
                return
            }
            let asset = AVURLAsset(url: slice.fileURL)
            let newSubdivision = randomSubdivisionMode ? (allowedSubdivisionsSteps.randomElement() ?? (uniformMap[selectedSubdivision] ?? 4))
                                                        : (uniformMap[selectedSubdivision] ?? 4)
            let snippetDurationSeconds = (60.0 / bpmValue) * (newSubdivision / 4.0)
            let newFrameCount = Int(round(snippetDurationSeconds * self.sampleRate))
            let newStart = slice.startFrame // Keeping the same start frame for regeneration.
            let startTime = CMTime(seconds: Double(newStart) / self.sampleRate, preferredTimescale: 6000)
            let duration = CMTime(seconds: snippetDurationSeconds, preferredTimescale: 6000)
            let tempURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_regen_\(i + 1).wav")
            print("[DEBUG] Regenerating slice \(i + 1) from file \(slice.fileURL.path) with start \(newStart) and duration \(snippetDurationSeconds)")
            let exportSucceeded = await exportSliceWithRetries(asset: asset,
                                                               startTime: startTime,
                                                               duration: duration,
                                                               outputURL: tempURL)
            if !exportSucceeded {
                await MainActor.run { self.statusText = "Slice \(i + 1) failed to export during regeneration." }
                print("[DEBUG] Regeneration incomplete: slice \(i + 1) export failed for file \(slice.fileURL.path)")
                return
            }
            regeneratedURLs.append(tempURL)
            await MainActor.run {
                self.progress = (Double(i + 1) / Double(totalCount)) * 100.0
                self.statusText = "Regenerating preview snippet \(i + 1) of \(totalCount)..."
            }
            print("[DEBUG] Regenerated slice \(i + 1) exported to \(tempURL.path)")
        }
        if regeneratedURLs.count != sliceInfos.count {
            await MainActor.run { self.statusText = "Some slices failed to regenerate." }
            print("[DEBUG] Regeneration incomplete: only \(regeneratedURLs.count) slices regenerated out of \(sliceInfos.count)")
            return
        }
        if layeringMode && regeneratedURLs.count >= sampleCount * 2 {
            var layered: [URL] = []
            print("[DEBUG] Regeneration layering mode: Merging \(sampleCount) pairs of slices")
            for i in 0..<sampleCount {
                let url1 = regeneratedURLs[i]
                let url2 = regeneratedURLs[i + sampleCount]
                print("[DEBUG] Merging regenerated slice pair \(i + 1): \(url1.lastPathComponent) + \(url2.lastPathComponent)")
                if let merged = AudioProcessor.mergeTwoSnippetsInt16(url1: url1,
                                                                     url2: url2,
                                                                     mergeMode: mergeMode,
                                                                     tempFolder: tempFolder) {
                    layered.append(merged)
                    print("[DEBUG] Merged regenerated slice pair \(i + 1) into \(merged.lastPathComponent)")
                }
            }
            await MainActor.run {
                self.previewSnippetURLs = layered
            }
        } else {
            await MainActor.run {
                self.previewSnippetURLs = regeneratedURLs
            }
        }
        await MainActor.run {
            for idx in previewSnippetURLs.indices {
                self.refreshTokens[idx] = UUID()
            }
        }
        if let chainBuffer = AudioProcessor.concatenateWavFiles(urls: previewSnippetURLs) {
            let previewURL = tempFolder.appendingPathComponent("preview_chain.wav")
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
                    self.statusText = "Preview regenerated."
                    self.progress = 100.0
                }
                print("[DEBUG] Regenerated preview chain created at \(previewURL.path)")
                await MainActor.run { self.preparePreviewPlayer() }
            } catch {
                await MainActor.run { self.statusText = "Error writing preview chain file: \(error)" }
                print("[DEBUG] Error writing regenerated preview chain: \(error)")
            }
        } else {
            await MainActor.run { self.statusText = "Failed to generate preview chain." }
            print("[DEBUG] Regenerated preview chain concatenation failed")
        }
        if self.pachinkoStutterEnabled {
            print("[DEBUG] Pachinko stutter enabled after regeneration. Applying stutter effect.")
            await self.applyPachinkoStutter()
        }
    }
    
    // MARK: - Refined Start using Transient Detection (applies same candidate range regardless of detection)
    private func refinedStart(for asset: AVURLAsset, snippetDuration: Double, bpm: Double) async -> Double? {
        let durationSec = CMTimeGetSeconds(asset.duration)
        guard durationSec >= snippetDuration else { return nil }
        guard let twoBar = computedNoGoZone else { return nil }
        let oneBarDuration = (60.0 / bpm) * 4.0
        
        // Ensure there is enough room in the file beyond the no-go zone.
        if let requiredMin = computedBPM.flatMap({ (60.0 / $0) * 16.0 }), (durationSec - twoBar) < requiredMin {
            return nil
        }
        
        // Pick a candidate start time from 0 to (durationSec - twoBar)
        let candidateStart = Double.random(in: 0...(durationSec - twoBar))
        
        // Define a detection window (one bar long) starting at candidateStart.
        let detectionStart = CMTime(seconds: candidateStart, preferredTimescale: 6000)
        let detectionDuration = CMTime(seconds: oneBarDuration, preferredTimescale: 6000)
        let detectionRange = CMTimeRange(start: detectionStart, duration: detectionDuration)
        
        // Use the same method for both compressed and uncompressed files.
        let buffer: AVAudioPCMBuffer?
        if UncompressedAudioProcessor.isUncompressedFormat(url: asset.url) {
            do {
                buffer = try await UncompressedAudioProcessor.loadAndConvertAudio(url: asset.url,
                                                                                   startTime: detectionRange.start,
                                                                                   duration: detectionRange.duration,
                                                                                   targetBitDepth: 16,
                                                                                   targetSampleRate: 44100.0,
                                                                                   targetChannels: 1,
                                                                                   throttleDelay: nil)
            } catch {
                print("[DEBUG] Uncompressed conversion failed in refinedStart: \(error)")
                return candidateStart
            }
        } else {
            buffer = await AudioProcessor.readPCMBuffer(asset: asset, timeRange: detectionRange)
        }
        
        guard let validBuffer = buffer, let channelDataPointer = validBuffer.int16ChannelData?[0] else {
            return candidateStart
        }
        
        let frameLength = Int(validBuffer.frameLength)
        var maxIndex = 0
        var maxValue: Int32 = 0
        let samplesBuffer = UnsafeBufferPointer(start: channelDataPointer, count: frameLength)
        for (i, sampleValue) in samplesBuffer.enumerated() {
            let sampleAbs = abs(Int32(sampleValue))
            if sampleAbs > maxValue {
                maxValue = sampleAbs
                maxIndex = i
            }
        }
        
        let transientOffset = Double(maxIndex) / self.sampleRate
        let adjustedOffset = max(0, transientOffset - 0.005)
        var newStart = candidateStart + adjustedOffset
        
        if newStart + snippetDuration > durationSec {
            newStart = durationSec - snippetDuration
        }
        return newStart
    }
}
