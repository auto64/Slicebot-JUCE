//
//  AudioSliceViewModel+Export.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by [Your Name] on [Today's Date].
//  This extension consolidates all export-related routines. It presents an export dialog
//  with options for file prefix and export type (individual slices and/or sample chain).
//  Volume adjustments are applied per slice. Note that any slice export now ultimately
//  uses AudioProcessor.exportSnippet which has been updated to handle uncompressed
//  formats (WAV/AIFF in 16-bit or 24-bit) by converting only the desired slice into the
//  internal standard (16-bit PCM, 44.1 kHz, mono).

import SwiftUI
import AVFoundation
import AppKit

extension AudioSliceViewModel {
    
    // MARK: - Export Entry Point
    
    /// Called when the Export button is pushed.
    /// Presents the export dialog with options and then calls the export routines for individual slices and/or sample chain.
    func exportPreviewChainVolumeChecked() {
        DispatchQueue.main.async {
            self.statusText = "Exporting slices..."
        }
        exportIntegrationPreviewChainWithVolumeInternal()
    }
    
    // MARK: - Private Helper: Present Export Panel with Options
    
    /// Presents an NSOpenPanel configured to select an export folder.
    /// The accessory view includes a prefix field and checkboxes for:
    /// "Generate Individual Samples" and "Generate Sample Chain".
    /// - Returns: A tuple containing the chosen export folder URL, file prefix, and the two export type booleans.
    private func presentExportPanel() -> (exportDirectory: URL, finalPrefix: String, generateIndividual: Bool, generateChain: Bool)? {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Select Export Folder"
        panel.canCreateDirectories = true
        panel.isAccessoryViewDisclosed = true
        
        let accessoryWidth: CGFloat = 300
        let accessoryHeight: CGFloat = 80
        let accessoryView = NSView(frame: NSRect(x: 0, y: 0, width: accessoryWidth, height: accessoryHeight))
        
        let prefixLabel = NSTextField(labelWithString: "Prefix:")
        prefixLabel.frame = NSRect(x: 0, y: 50, width: 60, height: 20)
        accessoryView.addSubview(prefixLabel)
        
        let prefixField = NSTextField(frame: NSRect(x: 70, y: 50, width: 200, height: 24))
        prefixField.stringValue = lastExportPrefix
        accessoryView.addSubview(prefixField)
        
        let individualCheckbox = NSButton(checkboxWithTitle: "Generate Individual Samples", target: nil, action: nil)
        individualCheckbox.frame = NSRect(x: 0, y: 25, width: 280, height: 20)
        individualCheckbox.state = generateIndividual ? .on : .off
        accessoryView.addSubview(individualCheckbox)
        
        let chainCheckbox = NSButton(checkboxWithTitle: "Generate Sample Chain", target: nil, action: nil)
        chainCheckbox.frame = NSRect(x: 0, y: 0, width: 280, height: 20)
        chainCheckbox.state = generateChain ? .on : .off
        accessoryView.addSubview(chainCheckbox)
        
        panel.accessoryView = accessoryView
        
        if panel.runModal() != .OK {
            self.statusText = "Export cancelled."
            return nil
        }
        guard let exportDirectory = panel.url else {
            self.statusText = "No export directory selected."
            return nil
        }
        
        let basePrefixInput = prefixField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let finalPrefix = basePrefixInput.isEmpty ? "export" : basePrefixInput
        
        UserDefaults.standard.set(exportDirectory.path, forKey: "LastExportDirectory")
        UserDefaults.standard.set(finalPrefix, forKey: "LastExportPrefix")
        UserDefaults.standard.set(individualCheckbox.state == .on, forKey: "LastGenerateIndividual")
        UserDefaults.standard.set(chainCheckbox.state == .on, forKey: "LastGenerateChain")
        
        let genIndividual = (individualCheckbox.state == .on)
        let genChain = (chainCheckbox.state == .on)
        
        return (exportDirectory, finalPrefix, genIndividual, genChain)
    }
    
    // MARK: - Volume-Adjusted Export Integration
    
    /// Presents the export dialog (with options) and then calls the export routines that process
    /// each slice with volume adjustments baked in.
    func exportIntegrationPreviewChainWithVolumeInternal() {
        guard let options = presentExportPanel() else { return }
        let exportDirectory = options.exportDirectory
        let finalPrefix = options.finalPrefix
        generateIndividual = options.generateIndividual
        generateChain = options.generateChain
        
        if generateIndividual {
            exportIndividualSlicesWithVolume(to: exportDirectory, withPrefix: finalPrefix)
        }
        if generateChain {
            exportChainWithVolume(to: exportDirectory, withPrefix: finalPrefix)
        }
    }
    
    // MARK: - Normal Export Routine (Without Volume Adjustments)
    
    /// Presents the export dialog (with options) and simply copies the existing preview chain file
    /// to the selected export folder.
    func exportPreviewChainWithoutVolume() {
        guard let chainURL = previewChainURL else {
            DispatchQueue.main.async {
                self.statusText = "No preview chain available for export."
            }
            return
        }
        
        guard let options = presentExportPanel() else { return }
        let exportDirectory = options.exportDirectory
        let finalPrefix = options.finalPrefix
        
        let startingNumber = VolumeControl.nextAvailableExportNumber(prefix: finalPrefix, in: exportDirectory)
        let destFileName = "\(finalPrefix)_\(startingNumber)_chain.wav"
        let destURL = exportDirectory.appendingPathComponent(destFileName)
        
        do {
            if FileManager.default.fileExists(atPath: destURL.path) {
                try FileManager.default.removeItem(at: destURL)
            }
            try FileManager.default.copyItem(at: chainURL, to: destURL)
            DispatchQueue.main.async {
                self.statusText = "Preview exported to \(exportDirectory.path)"
            }
        } catch {
            DispatchQueue.main.async {
                self.statusText = "Error exporting preview chain: \(error)"
            }
        }
    }
    
    // MARK: - Individual Slice Export with Volume Adjustments
    
    /// For each preview snippet, retrieves the slice's volume settings, computes the effective multiplier,
    /// and calls the helper in AudioProcessor+VolumeExport.swift to export a new file with volume adjustments.
    func exportIndividualSlicesWithVolume(to exportDirectory: URL, withPrefix finalPrefix: String) {
        let startingNumber = VolumeControl.nextAvailableExportNumber(prefix: finalPrefix, in: exportDirectory)
        var exportNumber = startingNumber
        
        for (index, snippetURL) in previewSnippetURLs.enumerated() {
            let settings = sliceVolumeSettings[index] ?? (volume: 0.75, isMuted: false)
            let volumeValue = settings.volume
            let isMuted = settings.isMuted
            
            let multiplier: Float = isMuted ? 0.0 : Float(VolumeControl.dbToLinear(VolumeControl.sliderValueToDb(volumeValue)))
            
            let destFileName = "\(finalPrefix)_\(exportNumber).wav"
            let destURL = exportDirectory.appendingPathComponent(destFileName)
            
            let success = AudioProcessor.exportSnippetWithVolume(from: snippetURL,
                                                                  volumeMultiplier: multiplier,
                                                                  outputURL: destURL)
            if !success {
                DispatchQueue.main.async {
                    self.statusText = "Error exporting slice \(index + 1)."
                }
                return
            }
            exportNumber += 1
        }
        
        DispatchQueue.main.async {
            self.statusText = "Individual slices exported to \(exportDirectory.path)"
        }
    }
    
    // MARK: - Chain Export with Volume Adjustments
    
    /// Processes each preview snippet to create a volume-adjusted version, then concatenates these adjusted snippets
    /// into a single WAV file for export.
    func exportChainWithVolume(to exportDirectory: URL, withPrefix finalPrefix: String) {
        var adjustedSnippetURLs: [URL] = []
        let tempFolder = tempPreviewFolder
        
        for (index, snippetURL) in previewSnippetURLs.enumerated() {
            let settings = sliceVolumeSettings[index] ?? (volume: 0.75, isMuted: false)
            let volumeValue = settings.volume
            let isMuted = settings.isMuted
            
            let multiplier: Float = isMuted ? 0.0 : Float(VolumeControl.dbToLinear(VolumeControl.sliderValueToDb(volumeValue)))
            
            let adjustedURL = tempFolder.appendingPathComponent("\(finalPrefix)_adjusted_\(index).wav")
            
            let success = AudioProcessor.exportSnippetWithVolume(from: snippetURL,
                                                                  volumeMultiplier: multiplier,
                                                                  outputURL: adjustedURL)
            if !success {
                DispatchQueue.main.async {
                    self.statusText = "Error processing slice \(index + 1) for chain export."
                }
                return
            }
            adjustedSnippetURLs.append(adjustedURL)
        }
        
        guard let chainBuffer = AudioProcessor.concatenateWavFiles(urls: adjustedSnippetURLs) else {
            DispatchQueue.main.async {
                self.statusText = "Failed to concatenate volume-adjusted snippets."
            }
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
        
        let startingNumber = VolumeControl.nextAvailableExportNumber(prefix: finalPrefix, in: exportDirectory)
        let destFileName = "\(finalPrefix)_\(startingNumber)_chain.wav"
        let chainDestURL = exportDirectory.appendingPathComponent(destFileName)
        
        if FileManager.default.fileExists(atPath: chainDestURL.path) {
            do {
                try FileManager.default.removeItem(at: chainDestURL)
            } catch {
                DispatchQueue.main.async {
                    self.statusText = "Error removing existing chain file: \(error)"
                }
                return
            }
        }
        
        do {
            let chainFile = try AVAudioFile(forWriting: chainDestURL,
                                            settings: settings,
                                            commonFormat: .pcmFormatInt16,
                                            interleaved: true)
            try chainFile.write(from: chainBuffer)
            DispatchQueue.main.async {
                self.statusText = "Sample chain exported to \(chainDestURL.path)"
            }
        } catch {
            DispatchQueue.main.async {
                self.statusText = "Error writing sample chain file: \(error)"
            }
        }
    }
}
