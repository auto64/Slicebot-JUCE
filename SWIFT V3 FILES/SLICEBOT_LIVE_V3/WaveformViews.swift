//
//  WaveformViews.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025.
//  Updated by [Your Name] on [Today's Date] to add temporary playhead on mouse move when SHIFT or COMMAND are held in the slice view.
//  This file contains the mouse tracking views, waveform drawing, and the waveform player view with the updated behavior.
//
import SwiftUI
import AVFoundation

// MARK: - Mouse Tracking with Modifier Flags

class MouseTrackingNSView: NSView {
    var alwaysTrack: Bool = false
    // Closure providing x-coordinate and current modifier flags.
    var onMouseMove: ((CGFloat?, NSEvent.ModifierFlags) -> Void)?
    var onShiftClick: ((CGFloat) -> Void)?
    var onCommandClick: ((CGFloat) -> Void)?
    var onRegularClick: (() -> Void)?

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas {
            removeTrackingArea(area)
        }
        let options: NSTrackingArea.Options = [.mouseMoved, .activeInActiveApp, .inVisibleRect]
        let trackingArea = NSTrackingArea(rect: bounds, options: options, owner: self, userInfo: nil)
        addTrackingArea(trackingArea)
    }

    override func mouseMoved(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        onMouseMove?(location.x, event.modifierFlags)
    }

    override func mouseDown(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        if event.modifierFlags.contains(.command) {
            onCommandClick?(location.x)
        } else if event.modifierFlags.contains(.shift) {
            onShiftClick?(location.x)
        } else {
            onRegularClick?()
        }
    }
}

struct MouseTrackingRepresentable: NSViewRepresentable {
    var alwaysTrack: Bool = false
    var onMouseMove: ((CGFloat?, NSEvent.ModifierFlags) -> Void)?
    var onShiftClick: ((CGFloat) -> Void)?
    var onCommandClick: ((CGFloat) -> Void)?
    var onRegularClick: (() -> Void)?

    func makeNSView(context: Context) -> MouseTrackingNSView {
        let view = MouseTrackingNSView()
        view.alwaysTrack = alwaysTrack
        view.onMouseMove = onMouseMove
        view.onShiftClick = onShiftClick
        view.onCommandClick = onCommandClick
        view.onRegularClick = onRegularClick
        return view
    }

    func updateNSView(_ nsView: MouseTrackingNSView, context: Context) {
        nsView.alwaysTrack = alwaysTrack
        nsView.onMouseMove = onMouseMove
        nsView.onShiftClick = onShiftClick
        nsView.onCommandClick = onCommandClick
        nsView.onRegularClick = onRegularClick
    }
}

// MARK: - Waveform View

struct WaveformView: View {
    let audioURL: URL
    let effectiveVolume: CGFloat  // Scales the drawn amplitude
    @State private var segments: [(min: Float, max: Float)] = []

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                Color(white: 0.7)
                if segments.isEmpty {
                    Rectangle()
                        .fill(Color.gray.opacity(0.3))
                        .onAppear {
                            AudioProcessor.loadWaveform(for: audioURL) { loadedSegments in
                                DispatchQueue.main.async {
                                    self.segments = loadedSegments
                                }
                            }
                        }
                } else {
                    let width = geometry.size.width
                    let height = geometry.size.height
                    let midY = height / 2
                    let segmentCount = segments.count
                    let step = width / CGFloat(max(segmentCount - 1, 1))
                    Canvas { context, _ in
                        for index in 0..<segmentCount {
                            let seg = segments[index]
                            let clampedMax = CGFloat(min(max(seg.max, -1.0), 1.0))
                            let clampedMin = CGFloat(min(max(seg.min, -1.0), 1.0))
                            let scaleFactor: CGFloat = 0.9
                            let scaledMax = clampedMax * effectiveVolume
                            let scaledMin = clampedMin * effectiveVolume
                            let yTop = midY - (scaledMax * midY * scaleFactor)
                            let yBottom = midY - (scaledMin * midY * scaleFactor)
                            context.stroke(Path { path in
                                path.move(to: CGPoint(x: CGFloat(index) * step, y: yTop))
                                path.addLine(to: CGPoint(x: CGFloat(index) * step, y: yBottom))
                            }, with: .color(Color.black), lineWidth: 2)
                        }
                    }
                    .frame(height: geometry.size.height)
                }
            }
        }
    }
}

// MARK: - Waveform Player View

struct WaveformPlayerView: View {
    let audioURL: URL
    var onShiftClickAction: ((CGFloat, CGFloat) -> Void)? = nil
    var onRegularClickAction: (() -> Void)? = nil
    @Binding var volume: CGFloat
    @Binding var isMuted: Bool

    @State private var playProgress: Double = 0.0
    @State private var isPlaying: Bool = false
    @State private var player: AVAudioPlayer?
    
    // State for temporary playhead on modifier keys.
    @State private var tempPlayheadX: CGFloat = 0
    @State private var showTempPlayhead: Bool = false

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
        GeometryReader { geometry in
            ZStack(alignment: .leading) {
                WaveformView(audioURL: audioURL, effectiveVolume: effectiveVolume)
                    .id(audioURL)
                // Playback playhead (from actual audio progress).
                if isPlaying {
                    Rectangle()
                        .fill(Color.white)
                        .frame(width: 2)
                        .offset(x: CGFloat(playProgress) * geometry.size.width)
                }
                // Temporary playhead shown when SHIFT or COMMAND are held.
                if showTempPlayhead {
                    Rectangle()
                        .fill(Color.white)
                        .frame(width: 2)
                        .offset(x: tempPlayheadX)
                }
            }
            .overlay(
                MouseTrackingRepresentable(
                    alwaysTrack: false,
                    onMouseMove: { x, modifiers in
                        // When SHIFT or COMMAND are held, update temporary playhead position.
                        if let x = x, (modifiers.contains(.shift) || modifiers.contains(.command)) {
                            tempPlayheadX = x
                            showTempPlayhead = true
                        } else {
                            showTempPlayhead = false
                        }
                    },
                    onShiftClick: { x in
                        onShiftClickAction?(x, geometry.size.width)
                    },
                    onCommandClick: { x in
                        startPlayback(from: x, viewWidth: geometry.size.width)
                    },
                    onRegularClick: {
                        onRegularClickAction?()
                        startPlayback()
                    }
                )
                .frame(width: geometry.size.width, height: geometry.size.height)
                .background(Color.clear)
            )
        }
        .onReceive(timer) { _ in
            if let player = self.player, player.isPlaying {
                playProgress = player.currentTime / player.duration
            } else {
                isPlaying = false
                playProgress = 0.0
            }
        }
    }
    
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
        } catch {
            print("Error playing snippet: \(error)")
        }
    }
}
