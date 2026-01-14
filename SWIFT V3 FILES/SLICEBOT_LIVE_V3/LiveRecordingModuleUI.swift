//
//  LiveRecordingModuleUI.swift
//  SLICEBOT_LIVE_V3
//
//  Created by [Your Name] on [Today's Date].
//  This file contains only the SwiftUI user interface components for the live recording module.
//  It has been split from the core logic to allow for a clean separation, and no visual changes have been made.
//  The placeholder modules now exactly match the original appearance with no extra white padding.
//

import SwiftUI
import AVFoundation

// MARK: - Color Extension for Hex Values
extension Color {
    init(hex: String) {
        let hex = hex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        var int: UInt64 = 0
        Scanner(string: hex).scanHexInt64(&int)
        let a, r, g, b: UInt64
        switch hex.count {
        case 3:
            (a, r, g, b) = (255, (int >> 8) * 17, (int >> 4 & 0xF) * 17, (int & 0xF) * 17)
        case 6:
            (a, r, g, b) = (255, int >> 16, int >> 8 & 0xFF, int & 0xFF)
        case 8:
            (a, r, g, b) = (int >> 24, int >> 16 & 0xFF, int >> 8 & 0xFF, int & 0xFF)
        default:
            (a, r, g, b) = (255, 0, 0, 0)
        }
        self.init(.sRGB,
                  red: Double(r) / 255,
                  green: Double(g) / 255,
                  blue: Double(b) / 255,
                  opacity: Double(a) / 255)
    }
}

extension Color {
    static var liveModuleBackground: Color { Color(hex: "464646") }
    static var emptyModuleBackground: Color { Color(hex: "808080") }
    static var dropdownBorder: Color { Color(hex: "252525") }
    static var dropdownText: Color { Color(hex: "CCCCCC") }
}

// MARK: - DownArrowTriangle
struct DownArrowTriangle: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.minX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
        path.closeSubpath()
        return path
    }
}

// MARK: - AudioInputDropdown
struct AudioInputDropdown: View {
    var placeholder: String = "Select"
    var options: [String] = []
    @Binding var isExpanded: Bool
    @Binding var selectedOption: String

    var body: some View {
        ZStack(alignment: .topLeading) {
            Button(action: {
                withAnimation { isExpanded.toggle() }
            }) {
                HStack(spacing: 2) {
                    Text(selectedOption.isEmpty ? placeholder : selectedOption)
                        .font(.system(size: 10))
                        .lineLimit(1)
                        .truncationMode(.tail)
                        .foregroundColor(Color.dropdownText)
                        .padding(.leading, 4)
                    Spacer()
                    DownArrowTriangle()
                        .fill(Color.dropdownText)
                        .frame(width: 6, height: 6)
                        .padding(.trailing, 4)
                }
                .frame(width: 103, height: 18)
                .background(Color(hex: "2B2B2B"))
                .overlay(Rectangle().stroke(Color.dropdownBorder, lineWidth: 2))
            }
            .buttonStyle(PlainButtonStyle())
            .overlay(
                Group {
                    if isExpanded {
                        ScrollView {
                            VStack(spacing: 0) {
                                ForEach(options, id: \.self) { option in
                                    Button(action: {
                                        selectedOption = option
                                        withAnimation { isExpanded = false }
                                    }) {
                                        Text(option)
                                            .font(.system(size: 10))
                                            .lineLimit(1)
                                            .truncationMode(.tail)
                                            .foregroundColor(Color.dropdownText)
                                            .frame(width: 100, height: 18)
                                            .background(Color(hex: "2B2B2B"))
                                            .overlay(Rectangle().stroke(Color.dropdownBorder, lineWidth: 2))
                                    }
                                    .buttonStyle(PlainButtonStyle())
                                }
                            }
                        }
                        .frame(width: 100, height: 18 * min(CGFloat(options.count), 10))
                        .background(Color(hex: "2B2B2B"))
                        .overlay(Rectangle().stroke(Color.dropdownBorder, lineWidth: 2))
                        .offset(y: 18)
                        .zIndex(10)
                    }
                },
                alignment: .topLeading
            )
        }
    }
}

// MARK: - CustomSliderView (Volume Meter)
struct CustomSliderView: View {
    var inputLevel: CGFloat
    var body: some View {
        ZStack(alignment: .bottom) {
            Rectangle()
                .fill(Color.gray.opacity(0.3))
                .frame(width: 10, height: 104)
            Rectangle()
                .fill(Color.green)
                .frame(width: 10, height: 104 * inputLevel)
        }
        .rotationEffect(.degrees(90))
        .frame(width: 98, height: 10)
    }
}

// MARK: - FourToggleButtons (2×2 Grid)
struct FourToggleButtons: View {
    @Binding var activeTickbox: Bool
    @Binding var linkButton: Bool
    @Binding var isMonitoring: Bool
    var onToggleMonitoring: () -> Void
    var deleteAudioAction: () -> Void
    var deleteModuleAction: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                Button(action: { onToggleMonitoring() }) {
                    ZStack {
                        (isMonitoring ? Color.blue : Color.gray)
                        Text("I")
                            .font(.system(size: 10, weight: .bold))
                            .foregroundColor(.white)
                    }
                    .frame(width: 19, height: 19)
                }
                .buttonStyle(PlainButtonStyle())
                Button(action: { linkButton.toggle() }) {
                    ZStack {
                        (linkButton ? Color.purple : Color.gray)
                        Text("L")
                            .font(.system(size: 10, weight: .bold))
                            .foregroundColor(.white)
                    }
                    .frame(width: 19, height: 19)
                }
                .buttonStyle(PlainButtonStyle())
            }
            HStack(spacing: 0) {
                Button(action: { activeTickbox.toggle() }) {
                    ZStack {
                        (activeTickbox ? Color.green : Color.gray)
                        Text("✓")
                            .font(.system(size: 10, weight: .bold))
                            .foregroundColor(.white)
                    }
                    .frame(width: 19, height: 19)
                }
                .buttonStyle(PlainButtonStyle())
                Button(action: {}) {
                    ZStack {
                        Color.red
                        Text("X")
                            .font(.system(size: 10, weight: .bold))
                            .foregroundColor(.white)
                    }
                    .frame(width: 19, height: 19)
                }
                .buttonStyle(PlainButtonStyle())
                .simultaneousGesture(
                    TapGesture(count: 2).onEnded { deleteModuleAction() }
                )
                .onTapGesture { deleteAudioAction() }
            }
        }
        .frame(width: 38, height: 38)
    }
}

// MARK: - RecordButton
struct RecordButton: View {
    @Binding var isRecording: Bool
    var elapsedTime: TimeInterval
    var onStop: () -> Void
    let maxRecordingTime: TimeInterval = 600.0

    @State private var showAlert: Bool = false

    var formattedTime: String {
        let minutes = Int(elapsedTime) / 60
        let seconds = Int(elapsedTime) % 60
        return String(format: "%02d:%02d", minutes, seconds)
    }

    var backgroundColor: Color {
        if isRecording {
            return elapsedTime < 25 ? Color.orange : Color.red
        } else {
            return Color.gray
        }
    }

    var body: some View {
        Button(action: {
            if isRecording {
                if elapsedTime < 25 {
                    SoundPlayer.shared.playCowbell()
                    showAlert = true
                } else {
                    isRecording = false
                }
            } else {
                isRecording = true
            }
        }) {
            ZStack {
                Rectangle()
                    .fill(backgroundColor)
                    .frame(width: 66, height: 38)
                if isRecording {
                    Text(formattedTime)
                        .font(.system(size: 18))
                        .foregroundColor(.white)
                        .frame(width: 66, height: 38)
                        .multilineTextAlignment(.center)
                } else {
                    Circle()
                        .fill(Color.red)
                        .frame(width: 18, height: 18)
                }
            }
        }
        .buttonStyle(PlainButtonStyle())
        .alert(isPresented: $showAlert) {
            Alert(
                title: Text("Minimum Recording Length Not Reached"),
                message: Text("The minimum live record time is 25 seconds."),
                primaryButton: .default(Text("OK"), action: {
                    showAlert = false
                }),
                secondaryButton: .destructive(Text("STOP"), action: {
                    onStop()
                    showAlert = false
                })
            )
        }
    }
}

// MARK: - DummyModuleView (Placeholder)
// This view exactly replicates the original empty buffer slot appearance.
struct DummyModuleView: View {
    var body: some View {
        Button(action: {
            // Dummy module action.
        }) {
            ZStack {
                Rectangle()
                    .fill(Color.emptyModuleBackground)
                    .frame(width: 120, height: 120)
                    .overlay(
                        Rectangle()
                            .stroke(Color(NSColor.separatorColor), lineWidth: 1)
                            .padding(10)
                    )
                Image(systemName: "plus")
                    .font(.system(size: 36))
                    .foregroundColor(.white)
            }
        }
        .buttonStyle(PlainButtonStyle())
    }
}

// MARK: - LiveRecordingModuleView
struct LiveRecordingModuleView: View {
    var moduleNumber: Int
    @StateObject var recorder: LiveRecorder
    @StateObject var playback = LivePlayback()
    
    @State private var inputDevices: [AudioInputDevice] = []
    @State private var availableChannels: [String] = []
    @State private var selectedChannel: String = "Channel 1"
    
    @State private var selectedInputDeviceName: String = ""
    @State private var isAudioInputExpanded: Bool = false
    @State private var isChannelExpanded: Bool = false
    
    // Closure to delete this module.
    var onDeleteModule: (() -> Void)?
    
    // Inject the shared AudioSliceViewModel for other settings.
    @EnvironmentObject var viewModel: AudioSliceViewModel
    
    var body: some View {
        ZStack(alignment: .topLeading) {
            Rectangle()
                .fill(Color(hex: "464646"))
                .frame(width: 120, height: 120)
            
            AudioInputDropdown(placeholder: "Audio Input",
                               options: inputDevices.map { $0.name },
                               isExpanded: $isAudioInputExpanded,
                               selectedOption: $selectedInputDeviceName)
                .offset(x: 11, y: 8)
                .zIndex(3)
                .onChange(of: isAudioInputExpanded) { newValue in
                    if newValue { isChannelExpanded = false }
                }
            AudioInputDropdown(placeholder: "Channel",
                               options: availableChannels,
                               isExpanded: $isChannelExpanded,
                               selectedOption: .constant(selectedChannel))
                .offset(x: 11, y: 32)
                .zIndex(2)
                .onChange(of: isChannelExpanded) { newValue in
                    if newValue { isAudioInputExpanded = false }
                }
            
            VStack(spacing: 8) {
                HStack(spacing: 0) {
                    FourToggleButtons(activeTickbox: $recorder.activeTickbox,
                                      linkButton: $recorder.latchTickbox,
                                      isMonitoring: $recorder.isMonitoring,
                                      onToggleMonitoring: {
                                          recorder.toggleMonitoring()
                                      },
                                      deleteAudioAction: {
                                          recorder.resetRecording()
                                      },
                                      deleteModuleAction: { onDeleteModule?() })
                    RecordButton(isRecording: $recorder.isRecording,
                                 elapsedTime: recorder.elapsedTime,
                                 onStop: {
                                     recorder.resetRecording()
                                 })
                        .environmentObject(viewModel)
                }
                .onChange(of: recorder.isRecording) { newValue in
                    if newValue {
                        if recorder.latchTickbox {
                            for rec in LiveRecorder.allRecorders where rec.latchTickbox {
                                rec.startRecording()
                            }
                        } else {
                            recorder.startRecording()
                        }
                    } else {
                        if recorder.latchTickbox {
                            for rec in LiveRecorder.allRecorders where rec.latchTickbox {
                                rec.stopRecording()
                            }
                        } else {
                            recorder.stopRecording()
                        }
                    }
                }
                CustomSliderView(inputLevel: recorder.inputLevel)
                    .frame(width: 60, height: 10)
            }
            .overlay(Text(viewModel.bpm).hidden())
            .offset(x: 11, y: 56)
            .onAppear {
                let devices = getAudioInputDevices()
                self.inputDevices = devices
                if let storedName = UserDefaults.standard.string(forKey: "LiveInputDeviceName"),
                   let device = devices.first(where: { $0.name == storedName }) {
                    selectedInputDeviceName = device.name
                    recorder.selectedDevice = device
                    updateChannels(for: device)
                } else if let first = devices.first {
                    selectedInputDeviceName = first.name
                    recorder.selectedDevice = first
                    updateChannels(for: first)
                    UserDefaults.standard.set(first.name, forKey: "LiveInputDeviceName")
                }
                if let storedChannelIndex = UserDefaults.standard.value(forKey: "LiveDesiredChannelIndex") as? Int {
                    recorder.desiredChannelIndex = storedChannelIndex
                    selectedChannel = "Channel \(storedChannelIndex + 1)"
                } else {
                    recorder.desiredChannelIndex = 0
                    selectedChannel = "Channel 1"
                    UserDefaults.standard.set(0, forKey: "LiveDesiredChannelIndex")
                }
                recorder.activeTickbox = true
            }
            .onChange(of: selectedInputDeviceName) { newValue in
                UserDefaults.standard.set(newValue, forKey: "LiveInputDeviceName")
                let devices = getAudioInputDevices()
                if let device = devices.first(where: { $0.name == newValue }) {
                    recorder.selectedDevice = device
                    updateChannels(for: device)
                    for rec in LiveRecorder.allRecorders {
                        rec.selectedDevice = device
                    }
                }
            }
        }
        .frame(width: 120, height: 120)
        .contentShape(Rectangle())
        .zIndex(100)
    }
    
    func updateChannels(for device: AudioInputDevice) {
        let count = Int(device.channelCount)
        if count > 0 {
            availableChannels = (1...count).map { "Channel \($0)" }
            selectedChannel = availableChannels.first ?? "Channel 1"
        } else {
            availableChannels = ["Channel 1"]
            selectedChannel = "Channel 1"
        }
    }
}

// MARK: - LiveRecordingContainerView
struct LiveRecordingContainerView: View {
    @EnvironmentObject var viewModel: AudioSliceViewModel
    @State private var module1Enabled: Bool = false
    @State private var module2Enabled: Bool = false
    @State private var module3Enabled: Bool = false
    @State private var module4Enabled: Bool = false

    var body: some View {
        HStack(spacing: 3) {
            DummyModuleView()
            if module1Enabled {
                LiveRecordingModuleView(moduleNumber: 1,
                                        recorder: LiveRecorder(moduleNumber: 1, fileURL: fileURLForModule(1))) {
                    module1Enabled = false
                    checkForLiveModules()
                }
            } else {
                placeholderModule {
                    module1Enabled = true
                    viewModel.sourceMode = .live
                    print("Module 1 added, switching sourceMode to live")
                }
            }
            if module2Enabled {
                LiveRecordingModuleView(moduleNumber: 2,
                                        recorder: LiveRecorder(moduleNumber: 2, fileURL: fileURLForModule(2))) {
                    module2Enabled = false
                    checkForLiveModules()
                }
            } else {
                placeholderModule {
                    module2Enabled = true
                    viewModel.sourceMode = .live
                    print("Module 2 added, switching sourceMode to live")
                }
            }
            if module3Enabled {
                LiveRecordingModuleView(moduleNumber: 3,
                                        recorder: LiveRecorder(moduleNumber: 3, fileURL: fileURLForModule(3))) {
                    module3Enabled = false
                    checkForLiveModules()
                }
            } else {
                placeholderModule {
                    module3Enabled = true
                    viewModel.sourceMode = .live
                    print("Module 3 added, switching sourceMode to live")
                }
            }
            if module4Enabled {
                LiveRecordingModuleView(moduleNumber: 4,
                                        recorder: LiveRecorder(moduleNumber: 4, fileURL: fileURLForModule(4))) {
                    module4Enabled = false
                    checkForLiveModules()
                }
            } else {
                placeholderModule {
                    module4Enabled = true
                    viewModel.sourceMode = .live
                    print("Module 4 added, switching sourceMode to live")
                }
            }
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - Helper Functions for LiveRecordingModuleUI
func fileURLForModule(_ moduleNumber: Int) -> URL {
    let fm = FileManager.default
    let supportDir = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
    let liveDir = supportDir.appendingPathComponent("LiveRecordings")
    try? fm.createDirectory(at: liveDir, withIntermediateDirectories: true, attributes: nil)
    return liveDir.appendingPathComponent("live_module\(moduleNumber).wav")
}

func checkForLiveModules() {
    // Implementation to check the state of live modules if needed.
}

func placeholderModule(action: @escaping () -> Void) -> some View {
    Button(action: action) {
        ZStack {
            Rectangle()
                .fill(Color.emptyModuleBackground)
                .frame(width: 120, height: 120)
                .overlay(
                    Rectangle()
                        .stroke(Color(NSColor.separatorColor), lineWidth: 1)
                        .padding(10)
                )
            Image(systemName: "plus")
                .font(.system(size: 36))
                .foregroundColor(.white)
        }
    }
    .buttonStyle(PlainButtonStyle())
}
