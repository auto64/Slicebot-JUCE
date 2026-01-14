//
//  UncompressedAudioError.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by [Your Name] on [Today's Date].
//
//  This file provides a robust module for handling uncompressed audio files (WAV, AIFF)
//  in 16‑bit or 24‑bit formats with sample rates typically ranging from 44.1 kHz to 96 kHz.
//  It defines the UncompressedAudioError enum for error handling and implements functions
//  to load and convert a desired slice into our internal standard format: 16‑bit PCM, 44.1 kHz, mono.
//  In this new version, we treat all uncompressed files the same and always run them through our manual conversion
//  (and downmixing, if needed), even if they are already 16‑bit, 44.1 kHz, mono.

import Foundation
import AVFoundation

public enum UncompressedAudioError: Error, LocalizedError {
    case fileOpenFailed(String)
    case readFailed(String)
    case conversionFailed(String)
    case invalidDuration(String)
    
    public var errorDescription: String? {
        switch self {
        case .fileOpenFailed(let msg):
            return "File Open Failed: \(msg)"
        case .readFailed(let msg):
            return "Read Failed: \(msg)"
        case .conversionFailed(let msg):
            return "Conversion Failed: \(msg)"
        case .invalidDuration(let msg):
            return "Invalid Duration: \(msg)"
        }
    }
}

public struct UncompressedAudioProcessor {
    
    /// Loads and converts a slice from an uncompressed audio file into our target format.
    ///
    /// - Parameters:
    ///   - url: The URL of the source audio file.
    ///   - startTime: The starting CMTime of the slice.
    ///   - duration: The duration (CMTime) of the slice.
    ///   - targetBitDepth: The desired bit depth (default is 16).
    ///   - targetSampleRate: The desired sample rate (default is 44100.0 Hz).
    ///   - targetChannels: The desired number of channels (default is 1 for mono).
    ///   - throttleDelay: An optional delay (in seconds) between conversion chunks.
    /// - Returns: An AVAudioPCMBuffer in 16‑bit, 44.1 kHz, mono format.
    /// - Throws: An UncompressedAudioError if any step fails.
    public static func loadAndConvertAudio(url: URL,
                                           startTime: CMTime,
                                           duration: CMTime,
                                           targetBitDepth: Int = 16,
                                           targetSampleRate: Double = 44100.0,
                                           targetChannels: AVAudioChannelCount = 1,
                                           throttleDelay: TimeInterval? = nil) async throws -> AVAudioPCMBuffer {
        // Open the file.
        let audioFile: AVAudioFile
        do {
            audioFile = try AVAudioFile(forReading: url)
        } catch {
            throw UncompressedAudioError.fileOpenFailed("Unable to open file at \(url.path): \(error)")
        }
        
        // Calculate file duration (in seconds) from frame count and sample rate.
        let fileSampleRate = audioFile.processingFormat.sampleRate
        let fileDurationSec = Double(audioFile.length) / fileSampleRate
        
        // Verify that the requested slice fits within the file.
        guard startTime.seconds >= 0,
              duration.seconds > 0,
              (startTime.seconds + duration.seconds) <= fileDurationSec else {
            throw UncompressedAudioError.invalidDuration("Requested slice (start: \(startTime.seconds), duration: \(duration.seconds)) exceeds file duration (\(fileDurationSec) seconds)")
        }
        
        // Set the file's frame position.
        audioFile.framePosition = AVAudioFramePosition(startTime.seconds * fileSampleRate)
        let framesToRead = AVAudioFrameCount(duration.seconds * fileSampleRate)
        guard let nativeBuffer = AVAudioPCMBuffer(pcmFormat: audioFile.processingFormat, frameCapacity: framesToRead) else {
            throw UncompressedAudioError.readFailed("Failed to create PCM buffer with capacity \(framesToRead)")
        }
        
        do {
            try audioFile.read(into: nativeBuffer, frameCount: framesToRead)
        } catch {
            throw UncompressedAudioError.readFailed("Error reading audio data: \(error)")
        }
        
        // Throttle processing if a delay is provided.
        if let delay = throttleDelay {
            Thread.sleep(forTimeInterval: delay)
        }
        
        let channels = audioFile.processingFormat.channelCount
        var workingBuffer: AVAudioPCMBuffer
        
        // Always perform conversion—even if the file is already in target format.
        if channels > targetChannels && targetChannels == 1 {
            workingBuffer = try manualConvertAndDownmix(buffer: nativeBuffer, targetSampleRate: targetSampleRate)
        } else {
            workingBuffer = try manualConvert(buffer: nativeBuffer, targetSampleRate: targetSampleRate)
        }
        
        return workingBuffer
    }
    
    /// Manually converts a PCM buffer from Float32 to Int16.
    private static func manualConvert(buffer: AVAudioPCMBuffer, targetSampleRate: Double) throws -> AVAudioPCMBuffer {
        let frameLength = buffer.frameLength
        guard let targetFormat = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                               sampleRate: targetSampleRate,
                                               channels: buffer.format.channelCount,
                                               interleaved: true) else {
            throw UncompressedAudioError.conversionFailed("Failed to create target format for manual conversion")
        }
        guard let outBuffer = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: frameLength) else {
            throw UncompressedAudioError.conversionFailed("Unable to create output buffer for manual conversion")
        }
        outBuffer.frameLength = frameLength
        
        // If floatChannelData is available, perform conversion from Float32.
        if let floatData = buffer.floatChannelData?[0], let outData = outBuffer.int16ChannelData?[0] {
            for i in 0..<Int(frameLength) {
                let converted = floatData[i] * 32767.0
                outData[i] = Int16(max(min(converted, 32767.0), -32768.0))
            }
        }
        // Otherwise, assume the buffer already holds Int16 data and simply copy it.
        else if let int16Data = buffer.int16ChannelData?[0], let outData = outBuffer.int16ChannelData?[0] {
            for i in 0..<Int(frameLength) {
                outData[i] = int16Data[i]
            }
        } else {
            throw UncompressedAudioError.conversionFailed("Missing channel data during conversion")
        }
        return outBuffer
    }
    
    /// Manually converts and downmixes a multi-channel (Float32) buffer to mono Int16.
    private static func manualConvertAndDownmix(buffer: AVAudioPCMBuffer, targetSampleRate: Double) throws -> AVAudioPCMBuffer {
        let frameLength = buffer.frameLength
        guard let targetFormat = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                               sampleRate: targetSampleRate,
                                               channels: 1,
                                               interleaved: true) else {
            throw UncompressedAudioError.conversionFailed("Failed to create target format for conversion and downmix")
        }
        guard let outBuffer = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: frameLength) else {
            throw UncompressedAudioError.conversionFailed("Unable to create output buffer for conversion and downmix")
        }
        outBuffer.frameLength = frameLength
        
        let inputChannels = buffer.format.channelCount
        
        // If floatChannelData is available, downmix using the float values.
        if let floatDataPointers = buffer.floatChannelData, let outData = outBuffer.int16ChannelData?[0] {
            for i in 0..<Int(frameLength) {
                var sum: Float = 0.0
                for ch in 0..<Int(inputChannels) {
                    sum += floatDataPointers[ch][i]
                }
                let avg = sum / Float(inputChannels)
                let intVal = avg * 32767.0
                outData[i] = Int16(max(min(intVal, 32767.0), -32768.0))
            }
        }
        // Otherwise, use the available Int16 channel data to perform downmixing.
        else if let int16Pointers = buffer.int16ChannelData, let outData = outBuffer.int16ChannelData?[0] {
            for i in 0..<Int(frameLength) {
                var sum: Int = 0
                for ch in 0..<Int(inputChannels) {
                    sum += Int(int16Pointers[ch][i])
                }
                let avg = Double(sum) / Double(inputChannels)
                outData[i] = Int16(max(min(avg, 32767.0), -32768.0))
            }
        }
        else {
            throw UncompressedAudioError.conversionFailed("Missing channel data during conversion and downmix")
        }
        return outBuffer
    }
    
    /// Downmixes a multi‑channel AVAudioPCMBuffer to mono by averaging the channels.
    private static func downmixToMono(buffer: AVAudioPCMBuffer, sampleRate: Double) throws -> AVAudioPCMBuffer {
        guard let monoFormat = AVAudioFormat(commonFormat: .pcmFormatInt16,
                                               sampleRate: sampleRate,
                                               channels: 1,
                                               interleaved: true) else {
            throw UncompressedAudioError.conversionFailed("Failed to create mono format for downmixing")
        }
        guard let monoBuffer = AVAudioPCMBuffer(pcmFormat: monoFormat, frameCapacity: buffer.frameLength) else {
            throw UncompressedAudioError.conversionFailed("Failed to create mono buffer for downmixing")
        }
        monoBuffer.frameLength = buffer.frameLength
        let frameCountInt = Int(buffer.frameLength)
        let channelCount = buffer.format.channelCount
        guard channelCount >= 2 else {
            throw UncompressedAudioError.conversionFailed("Source buffer does not have enough channels for downmixing")
        }
        let srcChannels = buffer.int16ChannelData!
        let monoChannel = monoBuffer.int16ChannelData![0]
        for i in 0..<frameCountInt {
            let left = Float(srcChannels[0][i])
            let right = Float(srcChannels[1][i])
            let average = (left + right) / 2.0
            monoChannel[i] = Int16(max(min(average, 32767), -32768))
        }
        return monoBuffer
    }
}
