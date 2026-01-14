//
//  AudioSliceViewModel+StutterIntegration.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by [Your Name] on [Today's Date].
//  This extension integrates the “pachinko” stutter effect routines that were previously part of processing.
//  It contains methods to apply the stutter effect and update the preview accordingly.
//
 
import SwiftUI
import AVFoundation

extension AudioSliceViewModel {
    
    func applyPachinkoStutterToSlice(at index: Int, startFraction: CGFloat) async {
        guard index < previewSnippetURLs.count else {
            await MainActor.run { self.statusText = "Invalid slice index for pachinko stutter." }
            return
        }
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
    
    func applyPachinkoStutter() async {
        let sliceCount = previewSnippetURLs.count
        for index in 0..<sliceCount {
            if Bool.random() {
                self.stutterCount = [2, 3, 4, 6, 8].randomElement()!
                self.stutterVolumeReductionStep = Double.random(in: 0.1...0.4)
                self.stutterPitchShiftSemitones = Double.random(in: 0.5...4.0)
                self.stutterTruncateEnabled = Bool.random()
                let randomStartFraction = CGFloat.random(in: 0.0...1.0)
                await applyPachinkoStutterToSlice(at: index, startFraction: randomStartFraction)
            }
        }
        await updatePreviewChain()
    }
}
