//
//  ContentView.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025
//  Updated by [Your Name] on [Today's Date].
//  This file integrates a fourth NSTab ("Live") whose content is replaced
//  with the new LiveRecordingContainerView that displays up to three independent recording buffers.
//  The rest of the UI (Main, Global, Local tabs, BPM/Sample pickers, FocusView, preview grid, bottom buttons)
//  remains as in the original project.
//
import SwiftUI
import AVFoundation
import AppKit

// Note: WindowAccessor is assumed to be defined in its own file (WindowAccessor.swift).

// MARK: - BottomRowButtonStyle
struct BottomRowButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 12))
            .padding(.horizontal, 8)
            .frame(height: 25)
            .background(configuration.isPressed ? Color.white : Color(white: 0.4))
            .foregroundColor(configuration.isPressed ? Color(white: 0.4) : Color.white)
    }
}

// MARK: - NSTabViewWrapper with Four Tabs (Main, Global, Local, Live)
struct NSTabViewWrapper<MainContent: View, GlobalContent: View, LocalContent: View, LiveContent: View>: NSViewRepresentable {
    let mainContent: MainContent
    let globalContent: GlobalContent
    let localContent: LocalContent
    let liveContent: LiveContent

    func makeNSView(context: Context) -> NSTabView {
        let tabView = NSTabView()
        
        // Main Tab
        let mainItem = NSTabViewItem(identifier: "Main")
        mainItem.label = "Main"
        mainItem.view = NSHostingView(rootView: mainContent.padding(.horizontal, 16))
        
        // Global Tab
        let globalItem = NSTabViewItem(identifier: "Global")
        globalItem.label = "Global"
        globalItem.view = NSHostingView(rootView: globalContent.padding(16))
        
        // Local Tab
        let localItem = NSTabViewItem(identifier: "Local")
        localItem.label = "Local"
        localItem.view = NSHostingView(rootView: localContent.padding(16))
        
        // Live Tab – note the injection of the environment object here
        let liveItem = NSTabViewItem(identifier: "Live")
        liveItem.label = "Live"
        liveItem.view = NSHostingView(rootView: liveContent.padding(16))
        
        tabView.addTabViewItem(mainItem)
        tabView.addTabViewItem(globalItem)
        tabView.addTabViewItem(localItem)
        tabView.addTabViewItem(liveItem)
        
        return tabView
    }
    
    func updateNSView(_ nsView: NSTabView, context: Context) { }
}

// MARK: - MainTabControls with BPM and Sample Count Pickers
struct MainTabControls: View {
    @ObservedObject var viewModel: AudioSliceViewModel
    @FocusState private var bpmFieldIsFocused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Row 1: Source Mode Picker (including LIVE option)
            HStack {
                Picker("", selection: $viewModel.sourceMode) {
                    ForEach(SourceMode.allCases) { mode in
                        Text(mode.rawValue).tag(mode)
                    }
                }
                .pickerStyle(SegmentedPickerStyle())
                .onChange(of: viewModel.sourceMode) { _ in
                    if viewModel.sourceMode == .multi {
                        viewModel.singleFileURL = nil
                    }
                }
            }
            // Row 2: Source Button (only for non-LIVE modes)
            HStack(spacing: 20) {
                if viewModel.sourceMode != .live {
                    Button(viewModel.isCaching ? "Stop Cache" : "Source") {
                        if viewModel.isCaching {
                            viewModel.stopCaching()
                        } else {
                            if viewModel.sourceMode == .singleManual {
                                viewModel.selectSingleFile()
                            } else {
                                viewModel.selectDirectory()
                            }
                        }
                    }
                }
                // Row 3: Subdivision Picker and Random Toggle
                HStack(spacing: 4) {
                    Text("Subdiv")
                    Picker("", selection: $viewModel.selectedSubdivision) {
                        ForEach(viewModel.noteSubdivisions, id: \.self) { subdivision in
                            Text(subdivision).foregroundColor(.black)
                        }
                    }
                    .pickerStyle(SegmentedPickerStyle())
                    Toggle("random", isOn: $viewModel.randomSubdivisionMode)
                        .toggleStyle(CheckboxToggleStyle())
                }
            }
            // Row 4: BPM and Sample Count Pickers
            HStack(spacing: 20) {
                HStack(spacing: 4) {
                    Text("BPM:")
                    TextField("BPM", text: $viewModel.bpm)
                        .frame(width: 40)
                        .focused($bpmFieldIsFocused)
                        .onSubmit { bpmFieldIsFocused = false }
                }
                HStack {
                    Text("Samples:")
                    Picker("", selection: $viewModel.sampleCount) {
                        ForEach(viewModel.sampleCountOptions.filter { $0 != 128 }, id: \.self) { count in
                            Text("\(count)")
                        }
                    }
                    .pickerStyle(SegmentedPickerStyle())
                    .onChange(of: viewModel.sampleCount) { newCount in
                        viewModel.previewSnippetURLs = []
                        viewModel.sliceInfos = []
                        viewModel.refreshTokens = [:]
                    }
                }
            }
        }
        .font(.body)
        .onAppear { DispatchQueue.main.async { bpmFieldIsFocused = false } }
    }
}

// MARK: - GlobalTabControls (Unchanged)
struct GlobalTabControls: View {
    @ObservedObject var viewModel: AudioSliceViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 20) {
                Toggle("Layer", isOn: $viewModel.layeringMode)
                    .toggleStyle(CheckboxToggleStyle())
                Picker("", selection: $viewModel.mergeMode) {
                    ForEach(LayeringMergeMode.allCases) { mode in
                        if mode == .crossfade {
                            Text("x-fade").tag(mode)
                        } else if mode == .crossfadeReverse {
                            Text("x-fade reverse").tag(mode)
                        } else {
                            Text(mode.rawValue).tag(mode)
                        }
                    }
                }
                .pickerStyle(SegmentedPickerStyle())
            }
            HStack {
                Toggle("Normalize", isOn: $viewModel.normalizeEnabled)
                    .toggleStyle(CheckboxToggleStyle())
                Toggle("Reverse", isOn: $viewModel.reverseEnabled)
                    .toggleStyle(CheckboxToggleStyle())
                    .onChange(of: viewModel.reverseEnabled) { newValue in
                        if newValue { viewModel.pachinkoReverseEnabled = false }
                    }
                Toggle("Pachinko Reverse", isOn: $viewModel.pachinkoReverseEnabled)
                    .toggleStyle(CheckboxToggleStyle())
                    .onChange(of: viewModel.pachinkoReverseEnabled) { newValue in
                        if newValue { viewModel.reverseEnabled = false }
                    }
                Toggle("Pachinko Stutter", isOn: $viewModel.pachinkoStutterEnabled)
                    .toggleStyle(CheckboxToggleStyle())
                Toggle("Transient detect", isOn: $viewModel.transientDetect)
                    .toggleStyle(CheckboxToggleStyle())
            }
        }
        .font(.body)
    }
}

// MARK: - LocalTabControls (Stutter Configuration, Unchanged)
struct LocalTabControls: View {
    @ObservedObject var viewModel: AudioSliceViewModel
    
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Toggle("Stutter", isOn: $viewModel.isStutterModeEnabled)
                    .toggleStyle(CheckboxToggleStyle())
                Toggle("Truncate Each Step", isOn: $viewModel.stutterTruncateEnabled)
                    .toggleStyle(CheckboxToggleStyle())
            }
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("Count:")
                    Picker("", selection: $viewModel.stutterCount) {
                        Text("2").tag(2)
                        Text("3").tag(3)
                        Text("4").tag(4)
                        Text("6").tag(6)
                        Text("8").tag(8)
                    }
                    .pickerStyle(SegmentedPickerStyle())
                }
                HStack {
                    VStack(alignment: .leading) {
                        Text("Volume")
                        Slider(value: $viewModel.stutterVolumeReductionStep, in: 0...0.4, step: 0.05)
                    }
                    VStack(alignment: .leading) {
                        Text("Pitch")
                        Slider(value: $viewModel.stutterPitchShiftSemitones, in: 0...8, step: 0.5)
                    }
                }
            }
            .padding(8)
        }
        .padding()
    }
}

// MARK: - ContentView
struct ContentView: View {
    @StateObject var viewModel = AudioSliceViewModel()
    @State private var globalMouseMonitor: Any?
    @State private var globalKeyboardMonitor: Any?

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Configure window size and style using the WindowAccessor.
            WindowAccessor { window in
                if let window = window {
                    window.setContentSize(NSSize(width: 620, height: 615))
                    window.styleMask.remove(.resizable)
                }
            }
            // Top section: NSTabViewWrapper with four tabs (Main, Global, Local, Live)
            HStack {
                Spacer().frame(width: 5)
                NSTabViewWrapper(
                    mainContent: MainTabControls(viewModel: viewModel),
                    globalContent: GlobalTabControls(viewModel: viewModel),
                    localContent: LocalTabControls(viewModel: viewModel),
                    liveContent: LiveRecordingContainerView().environmentObject(viewModel)
                )
                .frame(height: 150)
                .padding(.trailing, 5)
            }
            .padding(.top, 16)
            .padding(.bottom, -10)
            .frame(maxWidth: .infinity)
            
            // FocusView and preview grid section
            VStack(spacing: 6) {
                if viewModel.sampleCount != 128 {
                    if viewModel.previewSnippetURLs.isEmpty {
                        Rectangle()
                            .fill(Color.gray.opacity(0.6))
                            .frame(width: 609, height: 96)
                    } else if viewModel.previewSnippetURLs.indices.contains(viewModel.selectedSliceIndex) {
                        let currentVolume = Binding<CGFloat>(
                            get: {
                                viewModel.sliceVolumeSettings[viewModel.selectedSliceIndex]?.volume ?? 0.75
                            },
                            set: { newValue in
                                let currentMute = viewModel.sliceVolumeSettings[viewModel.selectedSliceIndex]?.isMuted ?? false
                                viewModel.sliceVolumeSettings[viewModel.selectedSliceIndex] = (volume: newValue, isMuted: currentMute)
                            }
                        )
                        let currentMute = Binding<Bool>(
                            get: {
                                viewModel.sliceVolumeSettings[viewModel.selectedSliceIndex]?.isMuted ?? false
                            },
                            set: { newValue in
                                let currentVolume = viewModel.sliceVolumeSettings[viewModel.selectedSliceIndex]?.volume ?? 0.75
                                viewModel.sliceVolumeSettings[viewModel.selectedSliceIndex] = (volume: currentVolume, isMuted: newValue)
                            }
                        )
                        FOCUSView(
                            audioURL: viewModel.previewSnippetURLs[viewModel.selectedSliceIndex],
                            stutterMode: $viewModel.isStutterModeEnabled,
                            stutterCount: viewModel.stutterCount,
                            onStutterCommit: { x, width in
                                let fraction = x / width
                                Task { await viewModel.applyStutter(to: viewModel.selectedSliceIndex, startFraction: fraction) }
                            },
                            onStutterPreview: { x, width in
                                let fraction = x / width
                                Task { await viewModel.previewStutter(for: viewModel.selectedSliceIndex, startFraction: fraction) }
                            },
                            onEditSlice: { x, width in
                                Task { await viewModel.startSelectSnippet(at: viewModel.selectedSliceIndex, relativeX: x, viewWidth: width) }
                            },
                            volume: currentVolume,
                            isMuted: currentMute
                        )
                        .id(viewModel.refreshTokens[viewModel.selectedSliceIndex] ?? UUID())
                        .frame(width: 609, height: 96)
                    }
                    LazyVGrid(columns: Array(repeating: GridItem(.fixed(150), spacing: 3), count: 4), spacing: 3) {
                        ForEach(0..<16, id: \.self) { index in
                            previewCell(for: index)
                        }
                    }
                } else {
                    Rectangle()
                        .fill(Color.gray.opacity(0.6))
                        .frame(width: 609, height: 265)
                }
            }
            
            // Bottom row: Action buttons
            HStack(spacing: 3) {
                Button("SLICE ALL") { viewModel.generatePreviewChain() }
                    .buttonStyle(BottomRowButtonStyle())
                    .keyboardShortcut("a", modifiers: [.shift])
                    .disabled(viewModel.isCaching)
                Button("MOD ALL") { Task { await viewModel.regenerateSnippets() } }
                    .buttonStyle(BottomRowButtonStyle())
                    .disabled(viewModel.sliceInfos.isEmpty || viewModel.isCaching)
                    .keyboardShortcut("m", modifiers: [.shift])
                Button("JUMBLE ALL") { viewModel.shuffleSlices() }
                    .buttonStyle(BottomRowButtonStyle())
                    .keyboardShortcut("j", modifiers: [.shift])
                    .disabled(viewModel.isCaching)
                Button("RESLICE ALL") { Task { await viewModel.resliceAllSnippets() } }
                    .buttonStyle(BottomRowButtonStyle())
                    .keyboardShortcut("r", modifiers: [.shift])
                    .disabled(viewModel.isCaching)
                Button("EXPORT") {
                    viewModel.exportPreviewChainVolumeChecked()
                }
                    .buttonStyle(BottomRowButtonStyle())
                    .keyboardShortcut("e", modifiers: [.shift])
                    .disabled(viewModel.isCaching)
                Button(action: { viewModel.exportSettingsLocked.toggle() }) {
                    Image(systemName: viewModel.exportSettingsLocked ? "lock.fill" : "lock.open.fill")
                }
                    .buttonStyle(BottomRowButtonStyle())
                Button("LOOP") {
                    Task {
                        await viewModel.updateLoopingChainWithVolume()
                        viewModel.togglePlayback()
                    }
                }
                    .buttonStyle(BottomRowButtonStyle())
                    .keyboardShortcut(.space, modifiers: [])
                    .disabled(viewModel.isCaching)
            }
            .padding(.horizontal, 6)
            .padding(.top, -9)
            
            // Animated red divider.
            HStack {
                Rectangle()
                    .fill(Color.red)
                    .frame(width: CGFloat(viewModel.progress) / 100 * 609, height: 1)
                    .animation(.linear, value: viewModel.progress)
            }
            .padding(.horizontal, 3)
            .padding(.vertical, 3)
            
            Spacer()
            
            // Status and progress indicator.
            VStack(spacing: 0) {
                Text(viewModel.statusText)
                    .padding(.horizontal, 16)
                    .padding(.bottom, 8)
                    .fixedSize(horizontal: false, vertical: true)
                Group {
                    if viewModel.isCaching {
                        ProgressView(value: viewModel.progress, total: 100)
                            .progressViewStyle(LinearProgressViewStyle())
                    } else {
                        Rectangle().fill(Color.clear)
                    }
                }
                .frame(height: 16)
                .padding(.horizontal, 16)
            }
        }
        .frame(minWidth: 620, minHeight: 615)
        .onAppear {
            globalMouseMonitor = NSEvent.addLocalMonitorForEvents(matching: .leftMouseDown) { event in
                return event
            }
            globalKeyboardMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
                if event.modifierFlags.contains(.command),
                   let characters = event.charactersIgnoringModifiers {
                    if characters.lowercased() == "c" {
                        viewModel.copySelectedSlice()
                        return nil
                    } else if characters.lowercased() == "v" {
                        Task { await viewModel.pasteToSelectedSlice() }
                        return nil
                    } else if characters.lowercased() == "z" {
                        viewModel.undoStutter(for: viewModel.selectedSliceIndex)
                        return nil
                    }
                }
                return event
            }
            
            if !viewModel.noteSubdivisions.contains(viewModel.selectedSubdivision) {
                viewModel.selectedSubdivision = "1/4 bar"
            }
            viewModel.loadCache()
            if let dir = viewModel.directoryURL {
                if !FileManager.default.fileExists(atPath: dir.path) {
                    viewModel.statusText = "Selected source folder is not available. Please reselect."
                } else {
                    viewModel.statusText = "Source folder loaded. Please press 'Slice All' to generate previews."
                }
            } else {
                viewModel.statusText = "Please select a source folder using the 'Source' button."
            }
            let index = viewModel.selectedSliceIndex
            if viewModel.sliceVolumeSettings[index] == nil {
                viewModel.sliceVolumeSettings[index] = (volume: 0.75, isMuted: false)
            }
        }
    }
    
    // Helper for preview grid cells.
    func previewCell(for index: Int) -> some View {
        Group {
            if index < viewModel.previewSnippetURLs.count {
                ZStack {
                    let cellVolume = Binding<CGFloat>(
                        get: {
                            viewModel.sliceVolumeSettings[index]?.volume ?? 0.75
                        },
                        set: { newValue in
                            let currentMute = viewModel.sliceVolumeSettings[index]?.isMuted ?? false
                            viewModel.sliceVolumeSettings[index] = (volume: newValue, isMuted: currentMute)
                        }
                    )
                    let cellMute = Binding<Bool>(
                        get: {
                            viewModel.sliceVolumeSettings[index]?.isMuted ?? false
                        },
                        set: { newValue in
                            let currentVolume = viewModel.sliceVolumeSettings[index]?.volume ?? 0.75
                            viewModel.sliceVolumeSettings[index] = (volume: currentVolume, isMuted: newValue)
                        }
                    )
                    WaveformPlayerView(
                        audioURL: viewModel.previewSnippetURLs[index],
                        onShiftClickAction: { x, width in
                            viewModel.startSelectSnippet(at: index, relativeX: x, viewWidth: width)
                        },
                        onRegularClickAction: {
                            viewModel.selectedSliceIndex = index
                        },
                        volume: cellVolume,
                        isMuted: cellMute
                    )
                    .id("\(viewModel.previewSnippetURLs[index].path)_\(viewModel.refreshTokens[index] ?? UUID())")
                    .frame(width: 150, height: 64)
                    .background(Color.gray.opacity(0.2))
                    
                    if cellMute.wrappedValue {
                        Color(.darkGray)
                            .opacity(0.5)
                            .allowsHitTesting(false)
                    }
                    
                    if index == viewModel.selectedSliceIndex {
                        Color.white
                            .opacity(0.1)
                            .allowsHitTesting(false)
                    }
                }
            } else {
                Rectangle()
                    .fill(Color.gray.opacity(0.6))
                    .frame(width: 150, height: 64)
            }
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
