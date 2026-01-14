//
//  FOCUSView.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Tom Phillipson on 25/2/2025.
//  Updated by [Your Name] on [Today's Date]
//  This view displays the waveform along with playback and stutter selection. When stutter mode is enabled
//  (via a binding), a semi‑transparent highlight rectangle appears over the waveform. Moving the mouse updates
//  the highlight’s x‑position. Clicking with no modifier triggers playback, while clicking when stutter mode is active
//  commits or previews the stutter effect as defined by the provided closures.

import SwiftUI
import AVFoundation

// MARK: - Custom Button Views

struct MomentaryButton: View {
    let label: String
    let midDarkGray: Color
    let action: () -> Void
    @State private var isActive = false

    var body: some View {
        ZStack {
            (isActive ? Color.white : midDarkGray)
            Text(label)
                .font(.system(size: 10))
                .foregroundColor(isActive ? midDarkGray : Color.white)
        }
        .frame(width: 19, height: 19)
        .onTapGesture {
            isActive = true
            action()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
                isActive = false
            }
        }
    }
}

struct ModifierButton: View {
    let label: String
    let midDarkGray: Color
    @Binding var isActive: Bool

    var body: some View {
        ZStack {
            (isActive ? Color.white : midDarkGray)
            Text(label)
                .font(.system(size: 10))
                .foregroundColor(isActive ? midDarkGray : Color.white)
        }
        .frame(width: 19, height: 19)
        .onTapGesture {
            isActive.toggle()
            DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) {
                if label == "C" {
                    isActive = false
                }
            }
        }
    }
}

struct PersistentModifierButton: View {
    let label: String
    let midDarkGray: Color
    @Binding var isActive: Bool

    var body: some View {
        ZStack {
            (isActive ? Color.white : midDarkGray)
            Text(label)
                .font(.system(size: 10))
                .foregroundColor(isActive ? midDarkGray : Color.white)
        }
        .frame(width: 19, height: 19)
        .onTapGesture {
            isActive.toggle()
        }
    }
}

// MARK: - FOCUSView

struct FOCUSView: View {
    let audioURL: URL
    // Bind stutterMode so that changes in viewModel propagate automatically.
    @Binding var stutterMode: Bool
    var stutterCount: Int = 4
    var onStutterCommit: ((CGFloat, CGFloat) -> Void)? = nil
    var onStutterPreview: ((CGFloat, CGFloat) -> Void)? = nil
    var onEditSlice: ((CGFloat, CGFloat) -> Void)? = nil

    @Binding var volume: CGFloat
    @Binding var isMuted: Bool

    // Playback and mouse tracking state.
    @State private var playProgress: Double = 0.0
    @State private var isPlaying: Bool = false
    @State private var player: AVAudioPlayer?
    @State private var stutterHighlightX: CGFloat? = nil

    // Modifier state variables.
    @State private var isCutModifierActive: Bool = false
    @State private var isPreviewModifierActive: Bool = false
    @State private var lastMouseX: CGFloat = 0
    @State private var showTempPlayhead: Bool = false

    // New: Monitor for modifier key changes.
    @State private var modifierMonitor: Any? = nil

    let timer = Timer.publish(every: 0.05, on: .main, in: .common).autoconnect()

    private func sliderValueToDb(_ v: CGFloat) -> CGFloat {
        if v <= 0.75 {
            return (40 / 0.75) * v - 40
        } else {
            return 32 * v - 24
        }
    }
    
    private func dbToLinear(_ db: CGFloat) -> CGFloat {
        return pow(10, db / 20)
    }
    
    var effectiveVolume: CGFloat {
        return isMuted ? 0 : dbToLinear(sliderValueToDb(volume))
    }
    
    var body: some View {
        HStack(spacing: 0) {
            // Left column: Button row.
            VStack(spacing: 0) {
                MomentaryButton(label: "S", midDarkGray: Color(white: 0.4)) { print("S pressed") }
                MomentaryButton(label: "W", midDarkGray: Color(white: 0.4)) { print("W pressed") }
                MomentaryButton(label: "R", midDarkGray: Color(white: 0.4)) { print("R pressed") }
                ModifierButton(label: "C", midDarkGray: Color(white: 0.4), isActive: $isCutModifierActive)
                PersistentModifierButton(label: "P", midDarkGray: Color(white: 0.4), isActive: $isPreviewModifierActive)
            }
            .frame(width: 19, height: 96)
            
            // Center column: Waveform view with stutter highlight overlay.
            GeometryReader { geometry in
                let viewWidth = geometry.size.width
                let defaultX = (viewWidth - viewWidth / CGFloat(stutterCount)) / 2.0
                
                // Mouse tracking closures.
                let mouseMoveHandler: (CGFloat?, NSEvent.ModifierFlags) -> Void = { x, modifiers in
                    if let xpos = x {
                        if modifiers.contains(.shift) || modifiers.contains(.command) || isCutModifierActive || isPreviewModifierActive {
                            lastMouseX = xpos
                            showTempPlayhead = true
                        } else {
                            showTempPlayhead = false
                        }
                        if stutterMode {
                            stutterHighlightX = xpos
                        }
                    }
                }
                
                let shiftClickHandler: (CGFloat) -> Void = { x in
                    if let editAction = onEditSlice {
                        editAction(x, viewWidth)
                    } else {
                        editCurrentSlice(at: x, viewWidth: viewWidth)
                    }
                }
                
                let commandClickHandler: (CGFloat) -> Void = { x in
                    if stutterMode {
                        onStutterPreview?(x, viewWidth)
                    } else {
                        startPlayback(from: x, viewWidth: viewWidth)
                    }
                }
                
                let regularClickHandler: () -> Void = {
                    if stutterMode, let x = stutterHighlightX {
                        onStutterCommit?(x, viewWidth)
                    } else if isCutModifierActive {
                        if let editAction = onEditSlice {
                            editAction(lastMouseX, viewWidth)
                        } else {
                            editCurrentSlice(at: lastMouseX, viewWidth: viewWidth)
                        }
                        isCutModifierActive = false
                    } else if isPreviewModifierActive {
                        startPlayback(from: lastMouseX, viewWidth: viewWidth)
                    } else {
                        startPlayback()
                    }
                }
                
                ZStack(alignment: .leading) {
                    WaveformView(audioURL: audioURL, effectiveVolume: effectiveVolume)
                        .id(audioURL)
                        .frame(width: viewWidth, height: geometry.size.height)
                    
                    if stutterMode {
                        if stutterHighlightX == nil {
                            Color.clear.onAppear { stutterHighlightX = defaultX }
                        }
                        if let x = stutterHighlightX {
                            let rectWidth = viewWidth / CGFloat(stutterCount)
                            Rectangle()
                                .fill(Color.white.opacity(0.5))
                                .frame(width: rectWidth, height: geometry.size.height)
                                .offset(x: x)
                        }
                    }
                    
                    if isPlaying {
                        Rectangle()
                            .fill(Color.white)
                            .frame(width: 2, height: geometry.size.height)
                            .offset(x: CGFloat(playProgress) * viewWidth)
                    } else if showTempPlayhead {
                        Rectangle()
                            .fill(Color.white)
                            .frame(width: 2, height: geometry.size.height)
                            .offset(x: lastMouseX)
                    }
                }
                .overlay(
                    MouseTrackingRepresentable(
                        alwaysTrack: stutterMode,
                        onMouseMove: mouseMoveHandler,
                        onShiftClick: shiftClickHandler,
                        onCommandClick: commandClickHandler,
                        onRegularClick: regularClickHandler
                    )
                    .frame(width: viewWidth, height: geometry.size.height)
                    .background(Color.clear)
                )
            }
            .frame(width: 571, height: 96)
            
            // Right column: Volume slider and mute toggle.
            VStack(spacing: 0) {
                ZStack(alignment: .bottom) {
                    Rectangle()
                        .fill(Color.gray.opacity(0.3))
                        .frame(width: 19, height: 80)
                    Rectangle()
                        .fill(Color.gray.opacity(0.7))
                        .frame(width: 19, height: 80 * volume)
                }
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            let newVolume = 1.0 - min(max(value.location.y, 0), 80) / 80.0
                            volume = newVolume
                            if let p = player, !isMuted {
                                let dB = sliderValueToDb(newVolume)
                                p.volume = Float(dbToLinear(dB))
                            }
                        }
                )
                ZStack {
                    if isMuted {
                        Color.white
                    } else {
                        Color(white: 0.4)
                    }
                    Text("M")
                        .font(.system(size: 10))
                        .foregroundColor(isMuted ? Color(white: 0.4) : Color.white)
                }
                .frame(width: 19, height: 16)
                .onTapGesture {
                    isMuted.toggle()
                    if let p = player {
                        p.volume = isMuted ? 0 : Float(dbToLinear(sliderValueToDb(volume)))
                    }
                }
            }
            .frame(width: 19, height: 96)
        }
        .frame(width: 609, height: 96)
        .background(Color.gray.opacity(0.2))
        .onReceive(timer) { _ in
            if let p = player, p.isPlaying {
                playProgress = p.currentTime / p.duration
            } else {
                isPlaying = false
                playProgress = 0.0
            }
        }
        .onAppear {
            // Set up a monitor for flagsChanged events to detect Command and Shift key press and release.
            modifierMonitor = NSEvent.addLocalMonitorForEvents(matching: .flagsChanged) { event in
                let commandPressed = event.modifierFlags.contains(.command)
                if commandPressed != self.isPreviewModifierActive {
                    self.isPreviewModifierActive = commandPressed
                }
                let shiftPressed = event.modifierFlags.contains(.shift)
                if shiftPressed != self.isCutModifierActive {
                    self.isCutModifierActive = shiftPressed
                }
                return event
            }
        }
        .onDisappear {
            if let monitor = modifierMonitor {
                NSEvent.removeMonitor(monitor)
                modifierMonitor = nil
            }
        }
    }
    
    // Helper: start playback from a given position if provided.
    func startPlayback(from positionX: CGFloat? = nil, viewWidth: CGFloat? = nil) {
        do {
            player = try AVAudioPlayer(contentsOf: audioURL)
            player?.volume = isMuted ? 0 : Float(dbToLinear(sliderValueToDb(volume)))
            if let posX = positionX, let width = viewWidth, let duration = player?.duration {
                let playbackTime = (Double(posX) / width) * duration
                player?.currentTime = playbackTime
            }
            player?.play()
            isPlaying = true
            showTempPlayhead = false
        } catch {
            print("Error playing snippet: \(error)")
        }
    }
    
    func startPlayback() {
        startPlayback(from: nil, viewWidth: nil)
    }
    
    func editCurrentSlice(at x: CGFloat, viewWidth: CGFloat) {
        print("Default editCurrentSlice called at \(x) in view width \(viewWidth). No onEditSlice closure provided.")
    }
}

struct FOCUSView_Previews: PreviewProvider {
    @State static var vol: CGFloat = 0.75
    @State static var mute: Bool = false
    @State static var stutter: Bool = false
    static var previews: some View {
        let testURL = Bundle.main.url(forResource: "example", withExtension: "wav") ?? URL(fileURLWithPath: "")
        FOCUSView(audioURL: testURL,
                  stutterMode: $stutter,
                  stutterCount: 4,
                  onStutterCommit: { x, width in print("Commit at \(x) in width \(width)") },
                  onStutterPreview: { x, width in print("Preview at \(x) in width \(width)") },
                  onEditSlice: { x, width in print("Edit at \(x) in width \(width)") },
                  volume: $vol,
                  isMuted: $mute)
    }
}
