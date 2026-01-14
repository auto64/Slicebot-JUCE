//
//  AudioSliceViewModel+ExportIntegration.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by [Your Name] on [Today's Date].
//  This file integrates individual slice and chain export routines with volume adjustments.
//  It presents an export dialog with options, stores user settings, and performs the export accordingly.

import SwiftUI
import AVFoundation
import AppKit

extension AudioSliceViewModel {
    
    /// Main export function to be called from the UI.
    /// Presents an NSOpenPanel for folder selection with accessory controls for prefix and export options.
    func exportPreviewChainWithVolume() {
        if isCaching {
            statusText = "Cannot export while caching is in progress."
            return
        }
        
        // If export settings are locked and stored, use them.
        if exportSettingsLocked,
           let exportDirectory = lastExportDirectory,
           let finalPrefix = UserDefaults.standard.string(forKey: "LastExportPrefix"),
           let storedIndividual = UserDefaults.standard.value(forKey: "LastGenerateIndividual") as? Bool,
           let storedChain = UserDefaults.standard.value(forKey: "LastGenerateChain") as? Bool {
            
            generateIndividual = storedIndividual
            generateChain = storedChain
            performExportWithVolume(to: exportDirectory, withPrefix: finalPrefix)
            return
        }
        
        // Setup NSOpenPanel for folder selection with accessory view.
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Select Export Folder"
        
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
            statusText = "Export cancelled."
            return
        }
        
        guard let exportDirectory = panel.url else {
            statusText = "No export directory selected."
            return
        }
        
        let basePrefixInput = prefixField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let finalPrefix = basePrefixInput.isEmpty ? "export" : basePrefixInput
        
        // Store export settings.
        UserDefaults.standard.set(exportDirectory.path, forKey: "LastExportDirectory")
        UserDefaults.standard.set(finalPrefix, forKey: "LastExportPrefix")
        UserDefaults.standard.set(individualCheckbox.state == .on, forKey: "LastGenerateIndividual")
        UserDefaults.standard.set(chainCheckbox.state == .on, forKey: "LastGenerateChain")
        
        generateIndividual = (individualCheckbox.state == .on)
        generateChain = (chainCheckbox.state == .on)
        
        performExportWithVolume(to: exportDirectory, withPrefix: finalPrefix)
    }
    
    /// Performs export by invoking individual and/or chain export functions based on user settings.
    /// - Parameters:
    ///   - exportDirectory: The destination folder URL.
    ///   - finalPrefix: The filename prefix to be used for exported files.
    private func performExportWithVolume(to exportDirectory: URL, withPrefix finalPrefix: String) {
        // First, if individual export is enabled, export individual slices.
        if generateIndividual {
            exportIndividualSlicesWithVolume(to: exportDirectory, withPrefix: finalPrefix)
        }
        // Next, if chain export is enabled, export the sample chain.
        if generateChain {
            exportChainWithVolume(to: exportDirectory, withPrefix: finalPrefix)
        }
    }
}
