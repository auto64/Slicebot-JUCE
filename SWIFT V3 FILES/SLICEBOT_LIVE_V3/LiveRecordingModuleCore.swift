//
//  LiveRecordingModuleCore.swift
//  SLICEBOT_LIVE_V3
//
//  Created by [Your Name] on [Today's Date].
//  This file contains the complete core logic for the live recording module.
//  It includes device discovery, the LiveRecorder and LivePlayback classes,
//  and all helper functions related to audio capture and processing.
//

import AVFoundation
import CoreAudio
import AudioToolbox
import Foundation

// MARK: - AudioInputDevice and Helpers
struct AudioInputDevice: Identifiable, Equatable, Hashable {
    var id: AudioObjectID
    var name: String
    var channelCount: UInt32
}

func numberOfInputChannels(for deviceID: AudioObjectID) -> UInt32? {
    var propertyAddress = AudioObjectPropertyAddress(
        mSelector: kAudioDevicePropertyStreamConfiguration,
        mScope: kAudioObjectPropertyScopeInput,
        mElement: kAudioObjectPropertyElementMain
    )
    var dataSize: UInt32 = 0
    let status = AudioObjectGetPropertyDataSize(deviceID, &propertyAddress, 0, nil, &dataSize)
    if status != noErr { return nil }
    let allocation = UnsafeMutableRawPointer.allocate(byteCount: Int(dataSize), alignment: MemoryLayout<AudioBufferList>.alignment)
    defer { allocation.deallocate() }
    let status2 = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nil, &dataSize, allocation)
    if status2 != noErr { return nil }
    let bufferList = UnsafeMutableAudioBufferListPointer(allocation.assumingMemoryBound(to: AudioBufferList.self))
    let channels = bufferList.reduce(0) { $0 + $1.mNumberChannels }
    return channels
}

func getAudioInputDevices() -> [AudioInputDevice] {
    var deviceIDs: [AudioObjectID] = []
    var dataSize: UInt32 = 0
    var propertyAddress = AudioObjectPropertyAddress(
        mSelector: kAudioHardwarePropertyDevices,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: kAudioObjectPropertyElementMain
    )
    let status = AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &propertyAddress, 0, nil, &dataSize)
    if status != noErr { return [] }
    let deviceCount = Int(dataSize) / MemoryLayout<AudioObjectID>.size
    deviceIDs = Array(repeating: 0, count: deviceCount)
    let status2 = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &propertyAddress, 0, nil, &dataSize, &deviceIDs)
    if status2 != noErr { return [] }
    
    var inputDevices: [AudioInputDevice] = []
    for deviceID in deviceIDs {
        var propertyAddressStreams = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: kAudioObjectPropertyScopeInput,
            mElement: 0
        )
        var streamsSize: UInt32 = 0
        let statusStreams = AudioObjectGetPropertyDataSize(deviceID, &propertyAddressStreams, 0, nil, &streamsSize)
        if statusStreams != noErr || streamsSize == 0 { continue }
        var deviceName: CFString = "" as CFString
        var nameSize = UInt32(MemoryLayout<CFString>.size)
        var propertyAddressName = AudioObjectPropertyAddress(
            mSelector: kAudioObjectPropertyName,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        let statusName = AudioObjectGetPropertyData(deviceID, &propertyAddressName, 0, nil, &nameSize, &deviceName)
        if statusName != noErr { continue }
        let channels = numberOfInputChannels(for: deviceID) ?? 0
        inputDevices.append(AudioInputDevice(id: deviceID, name: deviceName as String, channelCount: channels))
    }
    return inputDevices
}

// MARK: - LiveRecorder
class LiveRecorder: ObservableObject {
    var audioUnit: AudioUnit?
    var audioFile: AVAudioFile?
    @Published var isRecording = false
    @Published var elapsedTime: TimeInterval = 0 {
        didSet {
            if isRecording && elapsedTime >= 600 {
                stopRecording()
            }
        }
    }
    let fileURL: URL
    let fileWriteQueue = DispatchQueue(label: "com.project.liveRecorderFileWriteQueue")
    
    @Published var selectedDevice: AudioInputDevice?
    @Published var desiredChannelIndex: Int = 0
    @Published var inputLevel: CGFloat = 0.0
    
    @Published var activeTickbox: Bool = false { didSet { saveMetadata() } }
    @Published var latchTickbox: Bool = false { didSet { saveMetadata() } }
    
    @Published var isMonitoring: Bool = false
    var audioEngine: AVAudioEngine?
    
    let moduleNumber: Int
    static var allRecorders = [LiveRecorder]()
    
    init(moduleNumber: Int, fileURL: URL) {
        self.moduleNumber = moduleNumber
        self.fileURL = fileURL
        LiveRecorder.allRecorders.append(self)
    }
    
    deinit {
        if let index = LiveRecorder.allRecorders.firstIndex(where: { $0 === self }) {
            LiveRecorder.allRecorders.remove(at: index)
        }
    }
    
    var fullChannelCount: Int {
        if let device = selectedDevice {
            return Int(device.channelCount)
        }
        return 1
    }
    
    // MARK: - Toggle Monitoring
    func toggleMonitoring() {
        if isMonitoring {
            audioEngine?.stop()
            audioEngine = nil
            isMonitoring = false
        } else {
            let engine = AVAudioEngine()
            let input = engine.inputNode
            let format = input.inputFormat(forBus: 0)
            engine.connect(input, to: engine.mainMixerNode, format: format)
            do {
                try engine.start()
                audioEngine = engine
                isMonitoring = true
            } catch {
                print("Error starting input monitoring: \(error)")
            }
        }
    }
    
    // MARK: - Recording Callback
    let recordingCallback: AURenderCallback = { (inRefCon, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, ioData) -> OSStatus in
        let recorder = Unmanaged<LiveRecorder>.fromOpaque(inRefCon).takeUnretainedValue()
        let totalBytes = Int(inNumberFrames) * recorder.fullChannelCount * MemoryLayout<Int16>.size
        let audioBufferListPtr = UnsafeMutablePointer<AudioBufferList>.allocate(capacity: 1)
        audioBufferListPtr.pointee.mNumberBuffers = 1
        audioBufferListPtr.pointee.mBuffers.mNumberChannels = UInt32(recorder.fullChannelCount)
        audioBufferListPtr.pointee.mBuffers.mDataByteSize = UInt32(totalBytes)
        audioBufferListPtr.pointee.mBuffers.mData = malloc(totalBytes)
        defer {
            free(audioBufferListPtr.pointee.mBuffers.mData)
            audioBufferListPtr.deallocate()
        }
        
        guard let au = recorder.audioUnit else { return -1 }
        let status = AudioUnitRender(au, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, audioBufferListPtr)
        if status != noErr { return status }
        
        let totalChannels = Int(audioBufferListPtr.pointee.mBuffers.mNumberChannels)
        var monoSamples = [Int16](repeating: 0, count: Int(inNumberFrames))
        let interleavedPtr = audioBufferListPtr.pointee.mBuffers.mData!.assumingMemoryBound(to: Int16.self)
        for frame in 0..<Int(inNumberFrames) {
            let index = frame * totalChannels + recorder.desiredChannelIndex
            monoSamples[frame] = interleavedPtr[index]
        }
        
        var monoDesc = AudioStreamBasicDescription(
            mSampleRate: 44100.0,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
            mBytesPerPacket: 2,
            mFramesPerPacket: 1,
            mBytesPerFrame: 2,
            mChannelsPerFrame: 1,
            mBitsPerChannel: 16,
            mReserved: 0)
        guard let monoFormat = AVAudioFormat(streamDescription: &monoDesc) else { return -1 }
        guard let pcmBuffer = AVAudioPCMBuffer(pcmFormat: monoFormat, frameCapacity: inNumberFrames) else { return -1 }
        pcmBuffer.frameLength = inNumberFrames
        
        monoSamples.withUnsafeBytes { ptr in
            if let dest = pcmBuffer.int16ChannelData?[0] {
                memcpy(dest, ptr.baseAddress!, monoSamples.count * MemoryLayout<Int16>.size)
            }
        }
        
        let sumSquares = monoSamples.reduce(0.0) { $0 + Double(Int($1) * Int($1)) }
        let rms = sqrt(sumSquares / Double(monoSamples.count))
        let normalizedLevel = CGFloat(rms / 32768.0)
        DispatchQueue.main.async {
            recorder.inputLevel = normalizedLevel
            if recorder.elapsedTime >= 600 {
                recorder.stopRecording()
            }
        }
        
        recorder.fileWriteQueue.async {
            do {
                try recorder.audioFile?.write(from: pcmBuffer)
            } catch {
                print("Error writing audio buffer: \(error)")
            }
        }
        
        DispatchQueue.main.async {
            let segmentDuration = Double(inNumberFrames) / 44100.0
            recorder.elapsedTime += segmentDuration
        }
        
        return noErr
    }
    
    // MARK: - Start Recording
    func startRecording() {
        var sampleRate: Double = 44100.0
        if let device = selectedDevice {
            var deviceSampleRate: Double = 44100.0
            var dataSize = UInt32(MemoryLayout.size(ofValue: deviceSampleRate))
            var propertyAddress = AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyNominalSampleRate,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain)
            let status = AudioObjectGetPropertyData(device.id, &propertyAddress, 0, nil, &dataSize, &deviceSampleRate)
            if status == noErr {
                sampleRate = deviceSampleRate
            } else {
                print("Error getting sample rate from device: \(status)")
            }
        }
        
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: sampleRate,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false
        ]
        if FileManager.default.fileExists(atPath: fileURL.path) {
            try? FileManager.default.removeItem(at: fileURL)
        }
        do {
            audioFile = try AVAudioFile(forWriting: fileURL, settings: settings, commonFormat: .pcmFormatInt16, interleaved: true)
        } catch {
            print("Error creating audio file: \(error)")
            return
        }
        
        var desc = AudioComponentDescription(
            componentType: kAudioUnitType_Output,
            componentSubType: kAudioUnitSubType_HALOutput,
            componentManufacturer: kAudioUnitManufacturer_Apple,
            componentFlags: 0,
            componentFlagsMask: 0)
        guard let comp = AudioComponentFindNext(nil, &desc) else {
            print("Error: Audio component not found")
            return
        }
        var au: AudioUnit?
        let status = AudioComponentInstanceNew(comp, &au)
        if status != noErr || au == nil {
            print("Error creating audio unit: \(status)")
            return
        }
        audioUnit = au
        
        var enableIO: UInt32 = 1
        AudioUnitSetProperty(audioUnit!, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, UInt32(MemoryLayout<UInt32>.size))
        var disableIO: UInt32 = 0
        AudioUnitSetProperty(audioUnit!, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &disableIO, UInt32(MemoryLayout<UInt32>.size))
        
        if let device = selectedDevice {
            var deviceID = device.id
            AudioUnitSetProperty(audioUnit!, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &deviceID, UInt32(MemoryLayout<AudioObjectID>.size))
        }
        
        var streamFormat = AudioStreamBasicDescription(
            mSampleRate: sampleRate,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
            mBytesPerPacket: UInt32(fullChannelCount * 2),
            mFramesPerPacket: 1,
            mBytesPerFrame: UInt32(fullChannelCount * 2),
            mChannelsPerFrame: UInt32(fullChannelCount),
            mBitsPerChannel: 16,
            mReserved: 0)
        AudioUnitSetProperty(audioUnit!, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &streamFormat, UInt32(MemoryLayout<AudioStreamBasicDescription>.size))
        
        var callbackStruct = AURenderCallbackStruct(
            inputProc: recordingCallback,
            inputProcRefCon: Unmanaged.passUnretained(self).toOpaque())
        AudioUnitSetProperty(audioUnit!, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callbackStruct, UInt32(MemoryLayout<AURenderCallbackStruct>.size))
        
        let initStatus = AudioUnitInitialize(audioUnit!)
        if initStatus != noErr {
            print("Error initializing audio unit: \(initStatus)")
            return
        }
        let startStatus = AudioOutputUnitStart(audioUnit!)
        if startStatus != noErr {
            print("Error starting audio unit: \(startStatus)")
            return
        }
        
        DispatchQueue.main.async {
            self.isRecording = true
            self.elapsedTime = 0
        }
    }
    
    func stopRecording() {
        if let au = audioUnit {
            AudioOutputUnitStop(au)
            AudioUnitUninitialize(au)
            AudioComponentInstanceDispose(au)
            audioUnit = nil
        }
        DispatchQueue.main.async {
            self.isRecording = false
        }
        audioFile = nil
        saveMetadata()
    }
    
    func resetRecording() {
        stopRecording()
        DispatchQueue.main.async { self.elapsedTime = 0 }
        try? FileManager.default.removeItem(at: fileURL)
    }
    
    // MARK: - Metadata Saving for Live Modules
    func saveMetadata() {
        let fm = FileManager.default
        guard let supportDir = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else { return }
        let liveDir = supportDir.appendingPathComponent("LiveRecordings")
        if !fm.fileExists(atPath: liveDir.path) {
            try? fm.createDirectory(at: liveDir, withIntermediateDirectories: true, attributes: nil)
        }
        let metadataURL = liveDir.appendingPathComponent("liveModules.json")
        var liveModuleInfos: [LiveModuleInfo] = []
        if fm.fileExists(atPath: metadataURL.path),
           let data = try? Data(contentsOf: metadataURL),
           let existing = try? JSONDecoder().decode([LiveModuleInfo].self, from: data) {
            liveModuleInfos = existing
        }
        let newInfo = LiveModuleInfo(moduleNumber: moduleNumber,
                                     filePath: fileURL.path,
                                     elapsedTime: elapsedTime,
                                     isActive: activeTickbox,
                                     isLinked: latchTickbox)
        if let index = liveModuleInfos.firstIndex(where: { $0.moduleNumber == moduleNumber }) {
            liveModuleInfos[index] = newInfo
        } else {
            liveModuleInfos.append(newInfo)
        }
        liveModuleInfos.sort { $0.moduleNumber < $1.moduleNumber }
        if let data = try? JSONEncoder().encode(liveModuleInfos) {
            try? data.write(to: metadataURL)
        }
    }
}

// MARK: - LivePlayback
class LivePlayback: ObservableObject {
    var audioPlayer: AVAudioPlayer?
    
    func togglePlayback(fileURL: URL) {
        if let player = audioPlayer, player.isPlaying {
            player.pause()
        } else {
            do {
                audioPlayer = try AVAudioPlayer(contentsOf: fileURL)
                audioPlayer?.play()
            } catch {
                print("Playback error: \(error)")
            }
        }
    }
}
