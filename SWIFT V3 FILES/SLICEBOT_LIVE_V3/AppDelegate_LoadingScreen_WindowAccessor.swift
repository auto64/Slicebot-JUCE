//
//  AppDelegate_LoadingScreen_WindowAccessor.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 3/3/2025 and updated by [Your Name] on [Today's Date].
//  This file merges AppDelegate (which configures the window and clears live recording buffers),
//  LoadingScreenView (the 500×500 splash screen with progress), and WindowAccessor (a helper to access the NSWindow)
//  into one file. All existing references to these types remain valid.

import SwiftUI
import AppKit

// MARK: - AppDelegate

class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow?
    
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Clear only the live recording audio buffer files on startup.
        clearLiveRecordingBuffers()
        
        // Delay briefly to allow the window to be created, then capture and configure it.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            if let mainWindow = NSApplication.shared.windows.first {
                self.window = mainWindow
                // Configure window for splash screen: borderless, 500×500, centered.
                mainWindow.styleMask = [.borderless]
                mainWindow.setContentSize(NSSize(width: 620, height: 615))
                mainWindow.center()
            }
        }
    }
    
    /// Call this method to update the window for the main UI.
    func updateWindowForMainUI() {
        if let window = self.window {
            window.styleMask = [.titled, .closable, .miniaturizable, .resizable]
            window.setContentSize(NSSize(width: 620, height: 615))
            window.center()
        }
    }
    
    /// Clears only the live recording buffer files (live_module*.wav) from the LiveRecordings folder,
    /// leaving any metadata (e.g. liveModules.json) intact.
    private func clearLiveRecordingBuffers() {
        let fileManager = FileManager.default
        if let appSupportDir = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            let liveRecordingsDir = appSupportDir.appendingPathComponent("LiveRecordings")
            if fileManager.fileExists(atPath: liveRecordingsDir.path) {
                do {
                    let contents = try fileManager.contentsOfDirectory(at: liveRecordingsDir, includingPropertiesForKeys: nil, options: [])
                    for fileURL in contents {
                        let filename = fileURL.lastPathComponent
                        // Remove only the audio buffer files; preserve other files like metadata.
                        if filename.hasPrefix("live_module") && filename.hasSuffix(".wav") {
                            try fileManager.removeItem(at: fileURL)
                        }
                    }
                    print("Successfully cleared live recording buffers.")
                } catch {
                    print("Error clearing live recording buffers: \(error)")
                }
            } else {
                // Create the directory if it doesn't exist.
                do {
                    try fileManager.createDirectory(at: liveRecordingsDir, withIntermediateDirectories: true, attributes: nil)
                    print("LiveRecordings directory created.")
                } catch {
                    print("Error creating LiveRecordings directory: \(error)")
                }
            }
        }
    }
}

// MARK: - LoadingScreenView

struct LoadingScreenView: View {
    /// A progress value between 0.0 and 1.0. The red bar’s width equals progress × 500.
    var progress: Double

    var body: some View {
        ZStack(alignment: .bottomLeading) {
            // Display the background image (ensure your asset "LOADSCREEN" is 500×500).
            Image("LOADSCREEN")
                .resizable()
                .frame(width: 620, height: 615)
            // A red progress bar (10px high) at the bottom.
            Rectangle()
                .fill(Color.red)
                .frame(width: CGFloat(progress) * 620, height: 10)
        }
        .frame(width: 620, height: 615)
    }
}

struct LoadingScreenView_Previews: PreviewProvider {
    static var previews: some View {
        LoadingScreenView(progress: 0.5)
    }
}

// MARK: - WindowAccessor

struct WindowAccessor: NSViewRepresentable {
    var callback: (NSWindow?) -> Void

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            self.callback(view.window)
        }
        return view
    }
    
    func updateNSView(_ nsView: NSView, context: Context) {}
}
