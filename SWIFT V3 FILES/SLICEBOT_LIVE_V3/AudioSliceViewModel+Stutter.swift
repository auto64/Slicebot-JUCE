//
//  AudioSliceViewModel+Stutter.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by [Your Name] on [Today's Date].
//  This file integrates the modular stutter effect. It contains the full stutter effect
//  implementation, adds two new methods to AudioSliceViewModel for applying and previewing stutter,
//  includes a new undo function, and defines the StutterOverlayView with updated gesture handling and visible highlight.

import Foundation
import AVFoundation
import SwiftUI

// MARK: - StutterEffect Module
public struct StutterEffect {
    public var stutterCount: Int
    public var volumeReductionStep: Double
    public var pitchShiftSemitones: Double
    public var audioURL: URL
    public var lastExportPrefix: String
    public var stutterTruncateEnabled: Bool

    public init(audioURL: URL,
                stutterCount: Int = 4,
                volumeReductionStep: Double = 0.2,
                pitchShiftSemitones: Double = 1.0,
                lastExportPrefix: String = "export",
                stutterTruncateEnabled: Bool = false) {
        self.audioURL = audioURL
        self.stutterCount = stutterCount
        self.volumeReductionStep = volumeReductionStep
        self.pitchShiftSemitones = pitchShiftSemitones
        self.lastExportPrefix = lastExportPrefix
        self.stutterTruncateEnabled = stutterTruncateEnabled
    }
    
    // Temporary folder for stutter files.
    private var tempFolder: URL {
        let folder = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("StutterEffect")
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true, attributes: nil)
        return folder
    }
    
    /// Applies the full stutter effect and writes the result to a WAV file.
    /// - Parameter startFraction: A value between 0 and 1 indicating the start position.
    /// - Returns: URL of the processed stutter file.
    public func applyStutter(startFraction: CGFloat) async throws -> URL {
        let asset = AVURLAsset(url: audioURL)
        let durationSec = CMTimeGetSeconds(asset.duration)
        guard durationSec > 0 else {
            throw StutterError.invalidDuration
        }
        let segmentDurationSec = durationSec / Double(stutterCount)
        let requestedStart = Double(startFraction) * durationSec
        let startTimeSec = min(requestedStart, durationSec - segmentDurationSec)
        let timeRange = CMTimeRange(start: CMTime(seconds: startTimeSec, preferredTimescale: 6000),
                                    duration: CMTime(seconds: segmentDurationSec, preferredTimescale: 6000))
        
        guard let baseBuffer = await AudioProcessor.readPCMBuffer(asset: asset, timeRange: timeRange) else {
            throw StutterError.failedToReadBuffer
        }
        
        var processedBuffers: [AVAudioPCMBuffer] = []
        for i in 0..<stutterCount {
            guard let duplicateBuffer = duplicatePCMBuffer(baseBuffer) else {
                throw StutterError.failedToDuplicateBuffer(sliceIndex: i)
            }
            let volumeMultiplier = max(0.0, 1.0 - volumeReductionStep * Double(i))
            applyVolume(to: duplicateBuffer, multiplier: Float(volumeMultiplier))
            // Use a divisor of 8.0 for the main stutter effect.
            let pitchFactor = pow(2.0, Float(i) * Float(pitchShiftSemitones) / 8.0)
            guard let pitchedBuffer = pitchShiftBuffer(duplicateBuffer, pitchFactor: pitchFactor) else {
                throw StutterError.failedToPitchShift(sliceIndex: i)
            }
            
            var finalBuffer = pitchedBuffer
            if stutterTruncateEnabled {
                let fraction = 1.0 - (Double(i) / Double(stutterCount))
                let originalFrames = Int(pitchedBuffer.frameLength)
                let newFrameCount = Int(Double(originalFrames) * fraction)
                guard let truncatedBuffer = AVAudioPCMBuffer(pcmFormat: pitchedBuffer.format, frameCapacity: pitchedBuffer.frameCapacity) else {
                    throw StutterError.failedToDuplicateBuffer(sliceIndex: i)
                }
                truncatedBuffer.frameLength = pitchedBuffer.frameLength
                if let src = pitchedBuffer.int16ChannelData?[0], let dst = truncatedBuffer.int16ChannelData?[0] {
                    let bytesToCopy = newFrameCount * MemoryLayout<Int16>.size
                    memcpy(dst, src, bytesToCopy)
                    let silenceStart = newFrameCount
                    let silenceLength = originalFrames - newFrameCount
                    memset(dst + silenceStart, 0, silenceLength * MemoryLayout<Int16>.size)
                }
                finalBuffer = truncatedBuffer
            }
            // Apply boundary fade to reduce clicks.
            applyBoundaryFade(buffer: finalBuffer, fadeDuration: 0.005)
            processedBuffers.append(finalBuffer)
        }
        
        guard let concatenatedBuffer = concatenateBuffers(processedBuffers) else {
            throw StutterError.failedToConcatenateBuffers
        }
        
        let sampleRate = baseBuffer.format.sampleRate
        let targetFrameCount = AVAudioFrameCount(durationSec * sampleRate)
        let finalBuffer = AudioProcessor.finalTrim(buffer: concatenatedBuffer, targetFrameCount: targetFrameCount)
        
        // Generate a unique file name for each stutter effect.
        let uniqueID = UUID().uuidString.prefix(8)
        let stutterURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_stutter_\(uniqueID).wav")
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: sampleRate,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        do {
            let outFile = try AVAudioFile(forWriting: stutterURL,
                                          settings: settings,
                                          commonFormat: .pcmFormatInt16,
                                          interleaved: true)
            try outFile.write(from: finalBuffer)
        } catch {
            throw StutterError.failedToWriteFile(error)
        }
        return stutterURL
    }
    
    /// Previews the stutter effect without updating the main preview chain.
    /// - Parameter startFraction: A value between 0 and 1 indicating the start position.
    /// - Returns: URL of the preview stutter file.
    public func previewStutter(startFraction: CGFloat) async throws -> URL {
        let asset = AVURLAsset(url: audioURL)
        let durationSec = CMTimeGetSeconds(asset.duration)
        guard durationSec > 0 else {
            throw StutterError.invalidDuration
        }
        let segmentDurationSec = durationSec / Double(stutterCount)
        let requestedStart = Double(startFraction) * durationSec
        let startTimeSec = min(requestedStart, durationSec - segmentDurationSec)
        let timeRange = CMTimeRange(start: CMTime(seconds: startTimeSec, preferredTimescale: 6000),
                                    duration: CMTime(seconds: segmentDurationSec, preferredTimescale: 6000))
        
        guard let baseBuffer = await AudioProcessor.readPCMBuffer(asset: asset, timeRange: timeRange) else {
            throw StutterError.failedToReadBuffer
        }
        
        var processedBuffers: [AVAudioPCMBuffer] = []
        for i in 0..<stutterCount {
            guard let duplicateBuffer = duplicatePCMBuffer(baseBuffer) else {
                throw StutterError.failedToDuplicateBuffer(sliceIndex: i)
            }
            let volumeMultiplier = max(0.0, 1.0 - volumeReductionStep * Double(i))
            applyVolume(to: duplicateBuffer, multiplier: Float(volumeMultiplier))
            let pitchFactor = pow(2.0, Float(i) * Float(pitchShiftSemitones) / 12.0)
            guard let pitchedBuffer = pitchShiftBuffer(duplicateBuffer, pitchFactor: pitchFactor) else {
                throw StutterError.failedToPitchShift(sliceIndex: i)
            }
            
            var finalBuffer = pitchedBuffer
            if stutterTruncateEnabled {
                let fraction = 1.0 - (Double(i) / Double(stutterCount))
                let originalFrames = Int(pitchedBuffer.frameLength)
                let newFrameCount = Int(Double(originalFrames) * fraction)
                guard let truncatedBuffer = AVAudioPCMBuffer(pcmFormat: pitchedBuffer.format, frameCapacity: pitchedBuffer.frameCapacity) else {
                    throw StutterError.failedToDuplicateBuffer(sliceIndex: i)
                }
                truncatedBuffer.frameLength = pitchedBuffer.frameLength
                if let src = pitchedBuffer.int16ChannelData?[0], let dst = truncatedBuffer.int16ChannelData?[0] {
                    let bytesToCopy = newFrameCount * MemoryLayout<Int16>.size
                    memcpy(dst, src, bytesToCopy)
                    let silenceStart = newFrameCount
                    let silenceLength = originalFrames - newFrameCount
                    memset(dst + silenceStart, 0, silenceLength * MemoryLayout<Int16>.size)
                }
                finalBuffer = truncatedBuffer
            }
            applyBoundaryFade(buffer: finalBuffer, fadeDuration: 0.005)
            processedBuffers.append(finalBuffer)
        }
        
        guard let concatenatedBuffer = concatenateBuffers(processedBuffers) else {
            throw StutterError.failedToConcatenateBuffers
        }
        
        let sampleRate = baseBuffer.format.sampleRate
        let targetFrameCount = AVAudioFrameCount(durationSec * sampleRate)
        let finalBuffer = AudioProcessor.finalTrim(buffer: concatenatedBuffer, targetFrameCount: targetFrameCount)
        
        let uniqueID = UUID().uuidString.prefix(8)
        let previewURL = tempFolder.appendingPathComponent("\(lastExportPrefix)_stutter_preview_\(uniqueID).wav")
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: sampleRate,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        do {
            let outFile = try AVAudioFile(forWriting: previewURL,
                                          settings: settings,
                                          commonFormat: .pcmFormatInt16,
                                          interleaved: true)
            try outFile.write(from: finalBuffer)
        } catch {
            throw StutterError.failedToWriteFile(error)
        }
        return previewURL
    }
    
    public enum StutterError: Error {
        case invalidDuration
        case failedToReadBuffer
        case failedToDuplicateBuffer(sliceIndex: Int)
        case failedToPitchShift(sliceIndex: Int)
        case failedToConcatenateBuffers
        case failedToWriteFile(Error)
    }
    
    // MARK: - Helper Functions
    private func duplicatePCMBuffer(_ buffer: AVAudioPCMBuffer) -> AVAudioPCMBuffer? {
        guard let format = AVAudioFormat(commonFormat: buffer.format.commonFormat,
                                         sampleRate: buffer.format.sampleRate,
                                         channels: buffer.format.channelCount,
                                         interleaved: buffer.format.isInterleaved) else { return nil }
        guard let newBuffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: buffer.frameCapacity) else { return nil }
        newBuffer.frameLength = buffer.frameLength
        if let src = buffer.int16ChannelData?[0],
           let dst = newBuffer.int16ChannelData?[0] {
            memcpy(dst, src, Int(buffer.frameLength) * MemoryLayout<Int16>.size)
        }
        return newBuffer
    }
    
    private func applyVolume(to buffer: AVAudioPCMBuffer, multiplier: Float) {
        guard let data = buffer.int16ChannelData?[0] else { return }
        let frameCount = Int(buffer.frameLength)
        for i in 0..<frameCount {
            let sample = Float(data[i]) * multiplier
            let clamped = max(min(sample, 32767), -32768)
            data[i] = Int16(clamped)
        }
    }
    
    private func pitchShiftBuffer(_ buffer: AVAudioPCMBuffer, pitchFactor: Float) -> AVAudioPCMBuffer? {
        let originalFrameCount = Int(buffer.frameLength)
        guard let srcData = buffer.int16ChannelData?[0] else { return nil }
        guard let newBuffer = AVAudioPCMBuffer(pcmFormat: buffer.format, frameCapacity: buffer.frameCapacity) else { return nil }
        newBuffer.frameLength = buffer.frameLength
        guard let dstData = newBuffer.int16ChannelData?[0] else { return nil }
        for i in 0..<originalFrameCount {
            let srcIndex = Float(i) / pitchFactor
            let indexInt = Int(floor(srcIndex))
            let indexNext = min(indexInt + 1, originalFrameCount - 1)
            let frac = srcIndex - Float(indexInt)
            let sample1 = Float(srcData[indexInt])
            let sample2 = Float(srcData[indexNext])
            let interpolated = sample1 * (1 - frac) + sample2 * frac
            let clamped = max(min(interpolated, 32767), -32768)
            dstData[i] = Int16(clamped)
        }
        return newBuffer
    }
    
    private func concatenateBuffers(_ buffers: [AVAudioPCMBuffer]) -> AVAudioPCMBuffer? {
        guard let firstBuffer = buffers.first else { return nil }
        let totalFrames = buffers.reduce(0) { $0 + $1.frameLength }
        guard let concatenatedBuffer = AVAudioPCMBuffer(pcmFormat: firstBuffer.format, frameCapacity: totalFrames) else { return nil }
        concatenatedBuffer.frameLength = totalFrames
        guard let dst = concatenatedBuffer.int16ChannelData?[0] else { return nil }
        var currentFrame: Int = 0
        for buffer in buffers {
            let frameCount = Int(buffer.frameLength)
            if let src = buffer.int16ChannelData?[0] {
                memcpy(dst + currentFrame, src, frameCount * MemoryLayout<Int16>.size)
            }
            currentFrame += frameCount
        }
        return concatenatedBuffer
    }
    
    // MARK: - Boundary Fade Helper
    /// Applies a short linear fade-in and fade-out to the given audio buffer in-place.
    /// - Parameters:
    ///   - buffer: The audio buffer to fade.
    ///   - fadeDuration: Duration of the fade in seconds.
    private func applyBoundaryFade(buffer: AVAudioPCMBuffer, fadeDuration: Double) {
        let sampleRate = buffer.format.sampleRate
        let fadeSamples = Int(sampleRate * fadeDuration)
        let totalFrames = Int(buffer.frameLength)
        guard fadeSamples < totalFrames / 2, let data = buffer.int16ChannelData?[0] else { return }
        for i in 0..<fadeSamples {
            let multiplier = Float(i) / Float(fadeSamples)
            data[i] = Int16(Float(data[i]) * multiplier)
        }
        for i in (totalFrames - fadeSamples)..<totalFrames {
            let multiplier = Float(totalFrames - i) / Float(fadeSamples)
            data[i] = Int16(Float(data[i]) * multiplier)
        }
    }
}

// MARK: - AudioSliceViewModel Stutter Extension
extension AudioSliceViewModel {
    /// Applies the stutter effect to the preview snippet at the given index.
    func applyStutter(to index: Int, startFraction: CGFloat) async {
        guard index < previewSnippetURLs.count else {
            await MainActor.run { self.statusText = "Invalid slice index for stutter." }
            return
        }
        // Save current state for undo.
        stutterUndoBackup[index] = previewSnippetURLs[index]
        do {
            let effect = StutterEffect(audioURL: previewSnippetURLs[index],
                                       stutterCount: stutterCount,
                                       volumeReductionStep: stutterVolumeReductionStep,
                                       pitchShiftSemitones: stutterPitchShiftSemitones,
                                       lastExportPrefix: lastExportPrefix,
                                       stutterTruncateEnabled: stutterTruncateEnabled)
            let stutterURL = try await effect.applyStutter(startFraction: startFraction)
            await MainActor.run {
                self.previewSnippetURLs[index] = stutterURL
                self.statusText = "Stutter effect applied with \(stutterCount) slices."
                self.refreshTokens[index] = UUID()
                self.isStutterModeEnabled = false
            }
            await updatePreviewChain()
        } catch {
            await MainActor.run { self.statusText = "Error applying stutter: \(error)" }
        }
    }
    
    /// Previews the stutter effect on the specified slice.
    func previewStutter(for index: Int, startFraction: CGFloat) async {
        guard index < previewSnippetURLs.count else { return }
        do {
            let effect = StutterEffect(audioURL: previewSnippetURLs[index],
                                       stutterCount: stutterCount,
                                       volumeReductionStep: stutterVolumeReductionStep,
                                       pitchShiftSemitones: stutterPitchShiftSemitones,
                                       lastExportPrefix: lastExportPrefix,
                                       stutterTruncateEnabled: stutterTruncateEnabled)
            let previewURL = try await effect.previewStutter(startFraction: startFraction)
            let previewPlayer = try AVAudioPlayer(contentsOf: previewURL)
            previewPlayer.play()
        } catch {
            await MainActor.run { self.statusText = "Error during stutter preview: \(error)" }
        }
    }
    
    /// Undoes the last stutter effect applied to the specified slice.
    func undoStutter(for index: Int) {
        guard let backupURL = stutterUndoBackup[index] else {
            self.statusText = "No stutter undo available."
            return
        }
        previewSnippetURLs[index] = backupURL
        Task { await updatePreviewChain() }
        stutterUndoBackup[index] = nil
        statusText = "Stutter effect undone."
    }
}

// MARK: - StutterOverlayView
struct StutterOverlayView: View {
    let stutterMode: Bool
    let stutterCount: Int
    var onStutterCommit: ((CGFloat, CGFloat) -> Void)?
    var onStutterPreview: ((CGFloat, CGFloat) -> Void)?
    @Binding var highlightX: CGFloat?
    
    var body: some View {
        GeometryReader { geometry in
            let viewWidth = geometry.size.width
            let rectWidth = viewWidth / CGFloat(stutterCount)
            let currentX = highlightX ?? (viewWidth - rectWidth) / 2.0
            ZStack(alignment: .leading) {
                Color.clear
                    .contentShape(Rectangle())
                    .gesture(
                        DragGesture(minimumDistance: 0)
                            .onChanged { value in
                                let newX = value.location.x - rectWidth / 2
                                highlightX = clamp(newX, lower: 0, upper: viewWidth - rectWidth)
                            }
                            .onEnded { _ in
                                onStutterCommit?(highlightX ?? currentX, viewWidth)
                            }
                    )
                    .simultaneousGesture(
                        TapGesture()
                            .onEnded {
                                onStutterCommit?(highlightX ?? currentX, viewWidth)
                            }
                    )
                Rectangle()
                    .fill(Color.blue.opacity(0.6))
                    .frame(width: rectWidth, height: geometry.size.height)
                    .offset(x: currentX)
            }
        }
    }
    
    private func clamp(_ x: CGFloat, lower: CGFloat, upper: CGFloat) -> CGFloat {
        return min(max(x, lower), upper)
    }
}

struct StutterOverlayView_Previews: PreviewProvider {
    @State static var highlight: CGFloat? = nil
    static var previews: some View {
        StutterOverlayView(stutterMode: true, stutterCount: 4, onStutterCommit: { x, width in
            print("Commit at \(x) in width \(width)")
        }, onStutterPreview: { x, width in
            print("Preview at \(x) in width \(width)")
        }, highlightX: $highlight)
        .frame(height: 100)
        .padding()
    }
}
