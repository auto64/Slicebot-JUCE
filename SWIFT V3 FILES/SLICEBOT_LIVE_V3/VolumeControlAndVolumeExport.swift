//
//  VolumeControlAndVolumeExport.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by [Your Name] on [Today's Date].
//  This file merges the functionalities of VolumeControl.swift and AudioProcessor+VolumeExport.swift.
//  It centralizes volume conversion helper functions and provides export routines with per-slice volume adjustments.

import SwiftUI
import Foundation
import AVFoundation

// MARK: - VolumeControl

public struct VolumeControl {
    
    /// Converts a normalized slider value (0 to 1) to a decibel value.
    ///
    /// For slider values in the range [0, 0.75], this function maps linearly to a range of [-40 dB, 0 dB].
    /// For slider values above 0.75, a steeper slope is applied.
    ///
    /// - Parameter v: A normalized slider value.
    /// - Returns: The corresponding decibel value.
    public static func sliderValueToDb(_ v: CGFloat) -> CGFloat {
        if v <= 0.75 {
            return (40 / 0.75) * v - 40
        } else {
            return 32 * v - 24
        }
    }
    
    /// Converts a decibel value to a linear amplitude multiplier.
    ///
    /// - Parameter db: A decibel value.
    /// - Returns: A linear multiplier computed as 10^(db/20).
    public static func dbToLinear(_ db: CGFloat) -> CGFloat {
        return pow(10, db / 20)
    }
    
    /// Determines the next available export number given a filename prefix and a directory.
    ///
    /// - Parameters:
    ///   - prefix: The filename prefix.
    ///   - directory: The URL of the directory where exported files are stored.
    /// - Returns: The next available number (an integer) to avoid naming conflicts.
    public static func nextAvailableExportNumber(prefix: String, in directory: URL) -> Int {
        let fm = FileManager.default
        var maxNumber = 0
        if let contents = try? fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil, options: []) {
            for url in contents {
                let fileName = url.lastPathComponent
                if fileName.hasPrefix("\(prefix)_") {
                    var numberPortion = fileName.replacingOccurrences(of: "\(prefix)_", with: "")
                    if let underscoreRange = numberPortion.range(of: "_chain") {
                        numberPortion = String(numberPortion[..<underscoreRange.lowerBound])
                    }
                    if let dotIndex = numberPortion.firstIndex(of: ".") {
                        numberPortion = String(numberPortion[..<dotIndex])
                    }
                    if let number = Int(numberPortion) {
                        maxNumber = max(maxNumber, number)
                    }
                }
            }
        }
        return maxNumber + 1
    }
}

// MARK: - AudioProcessor Volume Export Extension

extension AudioProcessor {
    
    /// Applies the volume multiplier to the given PCM buffer.
    /// - Parameters:
    ///   - buffer: The audio buffer to adjust.
    ///   - multiplier: The volume multiplier (0.0 for mute, 1.0 for no change, etc.).
    static func adjustBufferVolume(buffer: AVAudioPCMBuffer, multiplier: Float) {
        guard let channelData = buffer.int16ChannelData?[0] else { return }
        let frameLength = Int(buffer.frameLength)
        // Process each sample: multiply and clamp to valid Int16 range.
        for i in 0..<frameLength {
            // Multiply the current sample by the multiplier.
            let sample = Float(channelData[i]) * multiplier
            // Clamp the result between -32768 and 32767.
            let clampedSample = max(min(sample, 32767), -32768)
            channelData[i] = Int16(clampedSample)
        }
    }
    
    /// Exports an individual snippet file with volume adjustments baked in.
    ///
    /// This function reads an existing WAV file at `snippetURL`, applies a volume multiplier
    /// to each sample, and writes the adjusted audio to `outputURL` using 16-bit PCM format.
    ///
    /// - Parameters:
    ///   - snippetURL: The URL of the source snippet file.
    ///   - volumeMultiplier: The multiplier to apply to each sample (0.0 for mute).
    ///   - outputURL: The destination URL where the adjusted file will be written.
    /// - Returns: `true` if export succeeds; otherwise, `false`.
    static func exportSnippetWithVolume(from snippetURL: URL, volumeMultiplier: Float, outputURL: URL) -> Bool {
        // Define the audio format to use for reading and writing.
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: 44100.0,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        
        // Open the source file for reading.
        guard let inFile = try? AVAudioFile(forReading: snippetURL, commonFormat: .pcmFormatInt16, interleaved: true) else {
            print("Error: Unable to open snippet file at \(snippetURL.path) for reading.")
            return false
        }
        
        // Prepare a buffer to hold the entire file.
        let frameCount = AVAudioFrameCount(inFile.length)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: inFile.processingFormat, frameCapacity: frameCount) else {
            print("Error: Unable to create buffer with capacity \(frameCount).")
            return false
        }
        
        do {
            try inFile.read(into: buffer)
        } catch {
            print("Error reading audio data from \(snippetURL.path): \(error)")
            return false
        }
        
        // Apply the volume multiplier to the buffer.
        adjustBufferVolume(buffer: buffer, multiplier: volumeMultiplier)
        
        // Remove existing file at outputURL if needed.
        if FileManager.default.fileExists(atPath: outputURL.path) {
            do {
                try FileManager.default.removeItem(at: outputURL)
            } catch {
                print("Error removing existing file at \(outputURL.path): \(error)")
                return false
            }
        }
        
        // Write the adjusted buffer to a new file.
        do {
            let outFile = try AVAudioFile(forWriting: outputURL,
                                          settings: settings,
                                          commonFormat: .pcmFormatInt16,
                                          interleaved: true)
            try outFile.write(from: buffer)
            print("Successfully exported volume-adjusted snippet to \(outputURL.path)")
            return true
        } catch {
            print("Error writing adjusted audio file to \(outputURL.path): \(error)")
            return false
        }
    }
}
