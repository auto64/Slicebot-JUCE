//
//  Slicebot.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025.
//  Updated by [Your Name] on [Today's Date] to inject the AudioSliceViewModel as an environment object.
import SwiftUI
import AppKit

@main
struct Slicebot_v6_9App: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject var viewModel = AudioSliceViewModel()
    @State private var isLoading: Bool = true
    @State private var progress: Double = 0.0

    var body: some Scene {
        WindowGroup {
            Group {
                if isLoading {
                    LoadingScreenView(progress: progress)
                        .frame(width: 620, height: 615)
                } else {
                    ContentView()
                        .environmentObject(viewModel)
                }
            }
            .onAppear {
                let timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { timer in
                    progress += 0.05 / 3.0
                    if progress >= 1.0 {
                        progress = 1.0
                        timer.invalidate()
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                            withAnimation {
                                isLoading = false
                            }
                            appDelegate.updateWindowForMainUI()
                        }
                    }
                }
                RunLoop.main.add(timer, forMode: .common)
            }
        }
    }
}
