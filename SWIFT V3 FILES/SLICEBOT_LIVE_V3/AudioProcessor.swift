//
//  AudioProcessor.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025.
//  Updated by [Your Name] on [Today's Date].
//  This file contains low-level audio processing routines used throughout the app.
//  It handles waveform loading, PCM buffer reading, snippet export (with fade, normalization,
//  reversal, and merging), and concatenation of WAV files.
//  NOTE: For uncompressed files (WAV/AIFF in 16-bit or 24-bit), exportSnippet now uses
//  the UncompressedAudioProcessor module to convert only the desired slice into our
//  internal standard (16-bit PCM, 44.1 kHz, mono).

import Foundation
import AVFoundation
import AudioToolbox

// MARK: - Public Extension for UncompressedAudioProcessor

extension UncompressedAudioProcessor {
    public static func isUncompressedFormat(url: URL) -> Bool {
        let ext = url.pathExtension.lowercased()
        // Consider WAV, AIFF and AIF as uncompressed formats.
        return ext == "wav" || ext == "aiff" || ext == "aif"
    }
}

struct AudioProcessor {
    
    // MARK: - Helper: Check Target Format
    
    private static func isTargetFormat(_ format: AVAudioFormat) -> Bool {
        return format.commonFormat == .pcmFormatInt16 &&
               abs(format.sampleRate - 44100.0) < 0.1 &&
               format.channelCount == 1 &&
               format.isInterleaved == true
    }
    
    // MARK: - Waveform Loading
    
    static func loadWaveform(for audioURL: URL, completion: @escaping ([(min: Float, max: Float)]) -> Void) {
        let asset = AVURLAsset(url: audioURL)
        guard let track = asset.tracks(withMediaType: .audio).first else {
            completion([])
            return
        }
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: 44100.0,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        guard let reader = try? AVAssetReader(asset: asset) else {
            completion([])
            return
        }
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: settings)
        reader.add(output)
        reader.startReading()
        
        var sampleData: [Float] = []
        while let sampleBuffer = output.copyNextSampleBuffer() {
            if let blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) {
                let length = CMBlockBufferGetDataLength(blockBuffer)
                var data = Data(count: length)
                data.withUnsafeMutableBytes { ptr in
                    if let baseAddress = ptr.baseAddress {
                        CMBlockBufferCopyDataBytes(blockBuffer, atOffset: 0, dataLength: length, destination: baseAddress)
                    }
                }
                let count = length / MemoryLayout<Int16>.size
                data.withUnsafeBytes { ptr in
                    let samplesPtr = ptr.bindMemory(to: Int16.self)
                    let samplesBuffer = UnsafeBufferPointer(start: samplesPtr.baseAddress!, count: count)
                    for sample in samplesBuffer {
                        sampleData.append(Float(sample) / 32767.0)
                    }
                }
                CMSampleBufferInvalidate(sampleBuffer)
            }
        }
        
        let desiredSegments = 1000
        let chunkSize = max(1, sampleData.count / desiredSegments)
        var newSegments: [(min: Float, max: Float)] = []
        for i in stride(from: 0, to: sampleData.count, by: chunkSize) {
            let chunk = sampleData[i..<min(i + chunkSize, sampleData.count)]
            let minVal = chunk.min() ?? 0.0
            let maxVal = chunk.max() ?? 0.0
            newSegments.append((min: minVal, max: maxVal))
        }
        completion(newSegments)
    }
    
    // MARK: - PCM Buffer Reading
    
    static func readPCMBuffer(asset: AVURLAsset, timeRange: CMTimeRange) async -> AVAudioPCMBuffer? {
        do {
            _ = try await asset.load(.tracks)
        } catch {
            print("Error loading tracks: \(error)")
            return nil
        }
        guard let track = asset.tracks(withMediaType: .audio).first else {
            print("No audio track found.")
            return nil
        }
        
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: 44100.0,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        
        guard let reader = try? AVAssetReader(asset: asset) else {
            print("Failed to create AVAssetReader.")
            return nil
        }
        let output = AVAssetReaderTrackOutput(track: track, outputSettings: settings)
        reader.timeRange = timeRange
        reader.add(output)
        if !reader.startReading() {
            print("Error starting PCM reader: \(reader.error?.localizedDescription ?? "Unknown error")")
            return nil
        }
        
        let sampleRate = 44100.0
        let durationSec = CMTimeGetSeconds(timeRange.duration)
        let totalFrames = AVAudioFrameCount(durationSec * sampleRate)
        guard totalFrames > 0 else {
            print("Invalid timeRange: duration \(durationSec) produces totalFrames = \(totalFrames)")
            return nil
        }
        
        var effectiveChannels: UInt32 = 1
        var rawData = Data()
        if let firstSampleBuffer = output.copyNextSampleBuffer() {
            if let formatDesc = CMSampleBufferGetFormatDescription(firstSampleBuffer),
               let asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc) {
                effectiveChannels = asbd.pointee.mChannelsPerFrame
            }
            if let blockBuffer = CMSampleBufferGetDataBuffer(firstSampleBuffer) {
                let length = CMBlockBufferGetDataLength(blockBuffer)
                var data = Data(count: length)
                data.withUnsafeMutableBytes { ptr in
                    if let baseAddress = ptr.baseAddress {
                        CMBlockBufferCopyDataBytes(blockBuffer, atOffset: 0, dataLength: length, destination: baseAddress)
                    }
                }
                rawData.append(data)
            }
            CMSampleBufferInvalidate(firstSampleBuffer)
        }
        
        while let sampleBuffer = output.copyNextSampleBuffer() {
            if let blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) {
                let length = CMBlockBufferGetDataLength(blockBuffer)
                var data = Data(count: length)
                data.withUnsafeMutableBytes { ptr in
                    if let baseAddress = ptr.baseAddress {
                        CMBlockBufferCopyDataBytes(blockBuffer, atOffset: 0, dataLength: length, destination: baseAddress)
                    }
                }
                rawData.append(data)
            }
            CMSampleBufferInvalidate(sampleBuffer)
        }
        
        if effectiveChannels > 1 {
            let framesInMultiChannel = rawData.count / (2 * Int(effectiveChannels))
            let targetFormat = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                             sampleRate: sampleRate,
                                             channels: 1,
                                             interleaved: true)!
            guard let monoBuffer = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: AVAudioFrameCount(framesInMultiChannel)) else {
                print("Failed to create mono AVAudioPCMBuffer.")
                return nil
            }
            monoBuffer.frameLength = AVAudioFrameCount(framesInMultiChannel)
            rawData.withUnsafeBytes { ptr in
                let src = ptr.bindMemory(to: Int16.self)
                if let dst = monoBuffer.int16ChannelData?[0] {
                    for frame in 0..<framesInMultiChannel {
                        var sum = 0
                        for channel in 0..<Int(effectiveChannels) {
                            sum += Int(src[frame * Int(effectiveChannels) + channel])
                        }
                        let average = sum / Int(effectiveChannels)
                        dst[frame] = Int16(average)
                    }
                }
            }
            return monoBuffer
        } else {
            let sampleCount = rawData.count / 2
            let format = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                       sampleRate: sampleRate,
                                       channels: 1,
                                       interleaved: true)!
            guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(sampleCount)) else {
                print("Failed to create AVAudioPCMBuffer.")
                return nil
            }
            buffer.frameLength = AVAudioFrameCount(sampleCount)
            rawData.withUnsafeBytes { ptr in
                if let dest = buffer.int16ChannelData?[0] {
                    memcpy(dest, ptr.baseAddress, rawData.count)
                }
            }
            if buffer.frameLength == 0 {
                print("readPCMBuffer: resulted in zero frames.")
            }
            return buffer
        }
    }
    
    // MARK: - Export Snippet
    
    static func exportSnippet(asset: AVURLAsset,
                              startTime: CMTime,
                              duration: CMTime,
                              outputURL: URL,
                              applyFade: Bool,
                              normalize: Bool,
                              reverse: Bool,
                              pachinkoReverse: Bool) async -> Bool {
        let timeRange = CMTimeRange(start: startTime, duration: duration)
        var buffer: AVAudioPCMBuffer?
        
        // For uncompressed formats, force conversion.
        let ext = asset.url.pathExtension.lowercased()
        if ext == "wav" || ext == "aiff" || ext == "aif" {
            do {
                buffer = try await UncompressedAudioProcessor.loadAndConvertAudio(url: asset.url,
                                                                                  startTime: startTime,
                                                                                  duration: duration,
                                                                                  targetBitDepth: 16,
                                                                                  targetSampleRate: 44100.0,
                                                                                  targetChannels: 1,
                                                                                  throttleDelay: nil)
            } catch {
                print("Error in uncompressed conversion for file \(asset.url.path): \(error)")
                return false
            }
        } else {
            buffer = await readPCMBuffer(asset: asset, timeRange: timeRange)
        }
        
        guard let buf = buffer else {
            print("Failed to obtain PCM buffer for exportSnippet from file \(asset.url.path)")
            return false
        }
        
        if normalize {
            normalizeBuffer(buf)
        }
        if reverse {
            reverseBuffer(buf)
        } else if pachinkoReverse, Bool.random() {
            reverseBuffer(buf)
        }
        
        // Instead of applying fade before trimming, we now trim first...
        let targetFrames = AVAudioFrameCount(round(CMTimeGetSeconds(duration) * 44100.0))
        let finalBuffer = finalTrim(buffer: buf, targetFrameCount: targetFrames)
        // ...and then apply fade to the final trimmed buffer.
        if applyFade {
            applyFadeToBuffer(buffer: finalBuffer, sampleRate: 44100.0)
        }
        
        if FileManager.default.fileExists(atPath: outputURL.path) {
            try? FileManager.default.removeItem(at: outputURL)
        }
        
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: 44100.0,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        
        do {
            let outFile = try AVAudioFile(forWriting: outputURL,
                                          settings: settings,
                                          commonFormat: .pcmFormatInt16,
                                          interleaved: true)
            try outFile.write(from: finalBuffer)
            return true
        } catch {
            print("Error writing output file \(outputURL.path): \(error)")
            return false
        }
    }
    
    // MARK: - Final Trim
    
    static func finalTrim(buffer: AVAudioPCMBuffer, targetFrameCount: AVAudioFrameCount) -> AVAudioPCMBuffer {
        let format = buffer.format
        guard let trimmed = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: targetFrameCount) else {
            return buffer
        }
        trimmed.frameLength = targetFrameCount
        if let src = buffer.int16ChannelData?[0],
           let dst = trimmed.int16ChannelData?[0] {
            let available = Int(buffer.frameLength)
            let target = Int(targetFrameCount)
            let copyCount = min(available, target)
            memcpy(dst, src, copyCount * MemoryLayout<Int16>.size)
            if target > available {
                memset(dst + copyCount, 0, (target - available) * MemoryLayout<Int16>.size)
            }
        }
        return trimmed
    }
    
    // MARK: - Apply Fade to Buffer (Updated with Sine Curve)
    
    static func applyFadeToBuffer(buffer: AVAudioPCMBuffer, sampleRate: Double) {
        // Using a default fade duration of 0.01 seconds.
        let fadeDurationSeconds = 0.01
        let desiredSamples = Int(sampleRate * fadeDurationSeconds)
        let fadeSamples = min(desiredSamples, Int(buffer.frameLength) / 2)
        let total = Int(buffer.frameLength)
        guard let data = buffer.int16ChannelData?[0] else { return }
        // Apply fade-in using a sine curve.
        for i in 0..<min(fadeSamples, total) {
            let multiplier = sin((Float(i) / Float(fadeSamples)) * (Float.pi / 2))
            data[i] = Int16(Float(data[i]) * multiplier)
        }
        // Apply fade-out using a sine curve.
        for i in max(0, total - fadeSamples)..<total {
            let multiplier = sin((Float(total - i) / Float(fadeSamples)) * (Float.pi / 2))
            data[i] = Int16(Float(data[i]) * multiplier)
        }
    }
    
    // MARK: - Normalize Buffer
    
    static func normalizeBuffer(_ buffer: AVAudioPCMBuffer) {
        guard let data = buffer.int16ChannelData?[0] else { return }
        let total = Int(buffer.frameLength)
        var maxSample: Float = 0.0
        for i in 0..<total {
            maxSample = max(maxSample, abs(Float(data[i])))
        }
        if maxSample == 0 { return }
        let factor = 32767.0 / maxSample
        for i in 0..<total {
            data[i] = Int16(round(Float(data[i]) * factor))
        }
    }
    
    // MARK: - Reverse Buffer
    
    static func reverseBuffer(_ buffer: AVAudioPCMBuffer) {
        guard let data = buffer.int16ChannelData?[0] else { return }
        let total = Int(buffer.frameLength)
        for i in 0..<total / 2 {
            let j = total - 1 - i
            let temp = data[i]
            data[i] = data[j]
            data[j] = temp
        }
    }
    
    // MARK: - Merge Two Snippets (Improved for 16-bit files)
    
    static func mergeTwoSnippetsInt16(url1: URL,
                                      url2: URL,
                                      mergeMode: LayeringMergeMode,
                                      tempFolder: URL) -> URL? {
        do {
            let file1 = try AVAudioFile(forReading: url1)
            let file2 = try AVAudioFile(forReading: url2)
            let settings: [String: Any] = [
                AVFormatIDKey: kAudioFormatLinearPCM,
                AVSampleRateKey: 44100.0,
                AVNumberOfChannelsKey: 1,
                AVLinearPCMBitDepthKey: 16,
                AVLinearPCMIsFloatKey: false,
                AVLinearPCMIsBigEndianKey: false
            ]
            let targetFormat = AVAudioFormat(settings: settings)!
            let frameCount1 = AVAudioFrameCount(file1.length)
            let frameCount2 = AVAudioFrameCount(file2.length)
            let minFrames = min(frameCount1, frameCount2)
            
            let buffer1: AVAudioPCMBuffer
            if isTargetFormat(file1.processingFormat) {
                guard let buf = AVAudioPCMBuffer(pcmFormat: file1.processingFormat, frameCapacity: frameCount1) else { return nil }
                try file1.read(into: buf)
                buffer1 = buf
            } else {
                guard let buf = AVAudioPCMBuffer(pcmFormat: file1.processingFormat, frameCapacity: frameCount1) else { return nil }
                try file1.read(into: buf)
                let converter = AVAudioConverter(from: file1.processingFormat, to: targetFormat)!
                guard let converted = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: minFrames) else { return nil }
                var error: NSError? = nil
                converter.convert(to: converted, error: &error) { _, status in
                    status.pointee = .haveData
                    return buf
                }
                if error != nil { return nil }
                converted.frameLength = minFrames
                buffer1 = converted
            }
            
            let buffer2: AVAudioPCMBuffer
            if isTargetFormat(file2.processingFormat) {
                guard let buf = AVAudioPCMBuffer(pcmFormat: file2.processingFormat, frameCapacity: frameCount2) else { return nil }
                try file2.read(into: buf)
                buffer2 = buf
            } else {
                guard let buf = AVAudioPCMBuffer(pcmFormat: file2.processingFormat, frameCapacity: frameCount2) else { return nil }
                try file2.read(into: buf)
                let converter = AVAudioConverter(from: file2.processingFormat, to: targetFormat)!
                guard let converted = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: minFrames) else { return nil }
                var error: NSError? = nil
                converter.convert(to: converted, error: &error) { _, status in
                    status.pointee = .haveData
                    return buf
                }
                if error != nil { return nil }
                converted.frameLength = minFrames
                buffer2 = converted
            }
            
            guard let data1 = buffer1.int16ChannelData?[0],
                  let data2 = buffer2.int16ChannelData?[0],
                  let mergedBuffer = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: minFrames),
                  let mergedData = mergedBuffer.int16ChannelData?[0] else { return nil }
            mergedBuffer.frameLength = minFrames
            let L = Int(minFrames)
            
            let modeToUse: LayeringMergeMode = {
                if mergeMode == .pachinko {
                    return [LayeringMergeMode.none, .crossfade, .crossfadeReverse, .fiftyFifty, .quarterCuts].randomElement()!
                } else {
                    return mergeMode
                }
            }()
            
            switch modeToUse {
            case .fiftyFifty:
                let half = L / 2
                let fadeLength = min(5, half/2)
                for i in 0..<L {
                    if i < half - fadeLength/2 {
                        mergedData[i] = data1[i]
                    } else if i < half + fadeLength/2 {
                        let t = Double(i - (half - fadeLength/2)) / Double(fadeLength)
                        mergedData[i] = Int16(Double(data1[i]) * (1 - t) + Double(data2[i]) * t)
                    } else {
                        mergedData[i] = data2[i]
                    }
                }
            case .quarterCuts:
                let quarter = L / 4
                let fadeLength = min(5, quarter/2)
                for i in 0..<L {
                    if i < quarter - fadeLength/2 {
                        mergedData[i] = data1[i]
                    } else if i < quarter + fadeLength/2 {
                        let t = Double(i - (quarter - fadeLength/2)) / Double(fadeLength)
                        mergedData[i] = Int16(Double(data1[i]) * (1 - t) + Double(data2[i]) * t)
                    } else if i < 2 * quarter - fadeLength/2 {
                        mergedData[i] = data2[i]
                    } else if i < 2 * quarter + fadeLength/2 {
                        let t = Double(i - (2 * quarter - fadeLength/2)) / Double(fadeLength)
                        mergedData[i] = Int16(Double(data2[i]) * (1 - t) + Double(data1[i]) * t)
                    } else if i < 3 * quarter - fadeLength/2 {
                        mergedData[i] = data1[i]
                    } else if i < 3 * quarter + fadeLength/2 {
                        let t = Double(i - (3 * quarter - fadeLength/2)) / Double(fadeLength)
                        mergedData[i] = Int16(Double(data1[i]) * (1 - t) + Double(data2[i]) * t)
                    } else {
                        mergedData[i] = data2[i]
                    }
                }
            case .crossfade:
                for i in 0..<L {
                    let x = Double(i) / Double(max(1, L - 1))
                    let vol1 = pow(sin((Double.pi / 2) * (1 - x)), 2)
                    let vol2 = pow(sin((Double.pi / 2) * x), 2)
                    let sample1 = Double(data1[i])
                    let sample2 = Double(data2[i])
                    let mergedSample = sample1 * vol1 + sample2 * vol2
                    mergedData[i] = Int16(min(max(mergedSample, -32767), 32767))
                }
            case .crossfadeReverse:
                for i in 0..<L {
                    let x = Double(i) / Double(max(1, L - 1))
                    let vol1 = pow(sin((Double.pi / 2) * (1 - x)), 2)
                    let vol2 = pow(sin((Double.pi / 2) * x), 2)
                    let s1 = Double(data1[i])
                    let s2 = Double(data2[L - 1 - i])
                    let mergedSample = s1 * vol1 + s2 * vol2
                    mergedData[i] = Int16(min(max(mergedSample, -32767), 32767))
                }
            case .none, .pachinko:
                for i in 0..<L {
                    let sum = Double(data1[i]) + Double(data2[i])
                    mergedData[i] = Int16(min(max(sum, -32767), 32767))
                }
            }
            
            let mergedURL = tempFolder.appendingPathComponent("layered_\(UUID().uuidString).wav")
            let outFile = try AVAudioFile(forWriting: mergedURL,
                                          settings: settings,
                                          commonFormat: .pcmFormatInt16,
                                          interleaved: true)
            try outFile.write(from: mergedBuffer)
            return mergedURL
        } catch {
            print("Merge error: \(error)")
            return nil
        }
    }
    
    // MARK: - Concatenate WAV Files
    
    static func concatenateWavFiles(urls: [URL]) -> AVAudioPCMBuffer? {
        let outputFormat = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                         sampleRate: 44100,
                                         channels: 1,
                                         interleaved: true)!
        var totalFrames: AVAudioFrameCount = 0
        var buffers: [AVAudioPCMBuffer] = []
        for url in urls {
            do {
                let file = try AVAudioFile(forReading: url)
                let frameCount = AVAudioFrameCount(file.length)
                totalFrames += frameCount
                guard let buffer = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: frameCount) else { continue }
                try file.read(into: buffer)
                if file.processingFormat != outputFormat {
                    let converter = AVAudioConverter(from: file.processingFormat, to: outputFormat)!
                    guard let converted = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: frameCount) else { continue }
                    var error: NSError? = nil
                    let inputBlock: AVAudioConverterInputBlock = { _, status in
                        status.pointee = .haveData
                        return buffer
                    }
                    converter.convert(to: converted, error: &error, withInputFrom: inputBlock)
                    if error != nil { continue }
                    converted.frameLength = frameCount
                    buffers.append(converted)
                } else {
                    buffers.append(buffer)
                }
            } catch {
                continue
            }
        }
        
        guard let firstBuffer = buffers.first else { return nil }
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
}
