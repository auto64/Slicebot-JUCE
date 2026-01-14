//
//  AudioSliceViewModel+Caching.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025.
//  This extension handles directory and file selection as well as caching of audio files.
//  It enumerates the chosen folder and stores metadata (file URL and duration) for each supported file.
//  NOTE: Supported file types include compressed formats (mp3, m4a) and uncompressed formats (wav, aiff, aif).
//  For uncompressed files, conversion to the standard internal format (16-bit PCM, 44.1 kHz, mono)
//  is not done here but deferred until slicing/export time via the new UncompressedAudioHandler module.

import SwiftUI
import AVFoundation

extension AudioSliceViewModel {
    // MARK: - Directory & File Selection, Cache Management

    /// Selects a directory containing audio files and recaches its contents.
    func selectDirectory() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            // Attempt to start accessing the security-scoped resource.
            guard url.startAccessingSecurityScopedResource() else {
                statusText = "Failed to access security-scoped resource."
                return
            }
            // Save a bookmark for persistent access.
            do {
                let bookmarkData = try url.bookmarkData(options: .withSecurityScope, includingResourceValuesForKeys: nil, relativeTo: nil)
                UserDefaults.standard.set(bookmarkData, forKey: "LastInputDirectoryBookmark")
            } catch {
                print("Error saving bookmark: \(error)")
            }
            // Set the directoryURL and continue.
            directoryURL = url
            statusText = "Input: \(url.path)"
            fileCache.removeAll()
            recacheDirectory()
        }
    }
    
    /// Selects a single audio file.
    func selectSingleFile() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            singleFileURL = url
            statusText = "Selected file: \(url.path)"
        }
    }
    
    /// Loads the cached audio file metadata from disk.
    func loadCache() {
        // First, try to restore the directoryURL from a saved security-scoped bookmark.
        if let bookmarkData = UserDefaults.standard.data(forKey: "LastInputDirectoryBookmark") {
            var isStale = false
            do {
                let bookmarkedURL = try URL(resolvingBookmarkData: bookmarkData,
                                            options: .withSecurityScope,
                                            relativeTo: nil,
                                            bookmarkDataIsStale: &isStale)
                if isStale {
                    print("Bookmark data is stale.")
                }
                guard bookmarkedURL.startAccessingSecurityScopedResource() else {
                    print("Failed to start accessing security scoped resource.")
                    return
                }
                directoryURL = bookmarkedURL
                print("Restored directoryURL from bookmark: \(bookmarkedURL.path)")
            } catch {
                print("Error resolving bookmark: \(error)")
            }
        }
        
        if let url = cacheFileURL() {
            print("Attempting to load cache from: \(url.path)")
            if let data = try? Data(contentsOf: url) {
                let decoder = JSONDecoder()
                if let loaded = try? decoder.decode(CacheData.self, from: data) {
                    let fm = FileManager.default
                    if fm.fileExists(atPath: loaded.sourceDirectory) {
                        DispatchQueue.main.async {
                            self.directoryURL = URL(fileURLWithPath: loaded.sourceDirectory)
                            // When loading the cache, we mark all files as candidates.
                            // (The candidate flag will be recalculated during recaching if needed.)
                            self.fileCache = loaded.files.map {
                                AudioFileMetadata(fileURL: URL(fileURLWithPath: $0.path), duration: $0.duration, isCandidate: true)
                            }
                            self.statusText = "Loaded cache of \(self.fileCache.count) files."
                        }
                        print("Cache loaded successfully.")
                        return
                    } else {
                        DispatchQueue.main.async {
                            self.statusText = "Cached source not available; please recache."
                        }
                        print("Cached source directory does not exist: \(loaded.sourceDirectory)")
                    }
                } else {
                    print("Error decoding cache data from \(url.path)")
                }
            } else {
                print("No cache data found at \(url.path)")
            }
        } else {
            print("Cache file URL is nil.")
        }
    }
    
    /// Saves the current file cache to disk.
    func saveCache() {
        guard let dir = directoryURL else {
            print("Error: directoryURL is nil, cache not saved.")
            return
        }
        let cached = fileCache.map { CachedFile(path: $0.fileURL.path, duration: $0.duration) }
        let data = CacheData(sourceDirectory: dir.path, files: cached)
        if let url = cacheFileURL() {
            do {
                let encoder = JSONEncoder()
                encoder.outputFormatting = .prettyPrinted
                let json = try encoder.encode(data)
                try json.write(to: url)
                print("Cache successfully written to \(url.path)")
            } catch {
                print("Error saving cache: \(error)")
            }
        } else {
            print("Failed to generate cache file URL; cache not saved.")
        }
    }
    
    /// Generates the URL for the cache file in the Application Support directory.
    func cacheFileURL() -> URL? {
        let fm = FileManager.default
        guard let supportDir = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
            print("Error: Application Support directory not found.")
            return nil
        }
        let appFolder = supportDir.appendingPathComponent("AudioSnippetGenerator")
        do {
            try fm.createDirectory(at: appFolder, withIntermediateDirectories: true, attributes: nil)
            print("Cache folder confirmed at: \(appFolder.path)")
        } catch {
            print("Error creating cache folder: \(error)")
        }
        let fileURL = appFolder.appendingPathComponent(cacheFileName)
        print("Cache file will be written to: \(fileURL.path)")
        return fileURL
    }
    
    // MARK: - Recaching Directory
    
    /// Recaches the contents of the selected directory by enumerating files and storing metadata.
    /// For each file that has a supported extension (mp3, wav, m4a, aiff, aif), it loads its duration.
    /// Note: For uncompressed formats (wav, aiff) the conversion is deferred until a slice is needed.
    func recacheDirectory() {
        guard let dir = directoryURL else {
            statusText = "Please select an input directory first."
            return
        }
        // Ensure we have security-scoped access.
        guard dir.startAccessingSecurityScopedResource() else {
            statusText = "Cannot access the input directory."
            return
        }
        // Check if directory is accessible.
        guard FileManager.default.fileExists(atPath: dir.path) else {
            statusText = "Input directory not available. Please reselect the folder."
            return
        }
        
        // Set caching flag, reset progress, and clear fileCache on the main actor.
        Task { @MainActor in
            self.isCaching = true
            self.progress = 0.0
            self.fileCache = []
        }
        
        statusText = "Recaching input directory..."
        recacheTask?.cancel()
        recacheTask = Task {
            let fm = FileManager.default
            let keys: [URLResourceKey] = [.isRegularFileKey]
            var total = 0
            if let enumerator = fm.enumerator(at: dir, includingPropertiesForKeys: keys) {
                for _ in enumerator { total += 1 }
            }
            guard total > 0 else {
                await MainActor.run {
                    self.statusText = "No files found in directory."
                    self.isCaching = false
                }
                return
            }
            guard let enumerator2 = fm.enumerator(at: dir, includingPropertiesForKeys: keys) else {
                await MainActor.run {
                    self.statusText = "Failed to enumerate directory."
                    self.isCaching = false
                }
                return
            }
            let supported = ["mp3", "wav", "m4a", "aiff", "aif"]
            var count = 0
            var processed = 0
            var localCache: [AudioFileMetadata] = []
            while let fileURL = enumerator2.nextObject() as? URL {
                if Task.isCancelled { break }
                processed += 1
                // Every 100 files (or on the last file), update progress and append batch to fileCache.
                if processed % 100 == 0 || processed == total {
                    let percent = (Double(processed) / Double(total)) * 100.0
                    await MainActor.run {
                        self.statusText = "Recaching: \(processed) of \(total) files processed (\(Int(percent))%)."
                        self.progress = percent
                        self.fileCache.append(contentsOf: localCache)
                    }
                    localCache.removeAll()
                }
                if !supported.contains(fileURL.pathExtension.lowercased()) { continue }
                let asset = AVURLAsset(url: fileURL)
                do {
                    let duration = try await asset.load(.duration)
                    let seconds = CMTimeGetSeconds(duration)
                    if seconds.isNaN || seconds <= 0 { continue }
                    // Calculate minimum duration required (32 beats based on current BPM)
                    let minDuration: Double
                    if let bpmVal = self.computedBPM, bpmVal > 0 {
                        minDuration = (60.0 / bpmVal) * 32.0
                    } else {
                        // Default to 128 BPM if BPM is not set or invalid
                        minDuration = (60.0 / 128.0) * 32.0
                    }
                    let candidate = seconds >= minDuration
                    localCache.append(AudioFileMetadata(fileURL: fileURL, duration: seconds, isCandidate: candidate))
                    count += 1
                } catch {
                    continue
                }
            }
            // Append any remaining items.
            await MainActor.run {
                self.fileCache.append(contentsOf: localCache)
                if Task.isCancelled {
                    self.statusText = "Recache cancelled. Cached \(self.fileCache.count) files so far."
                } else {
                    self.statusText = "Recached \(count) audio files out of \(processed) processed."
                    SoundPlayer.shared.playBleep()
                    self.saveCache()
                }
                self.progress = 100.0
                self.isCaching = false
            }
            recacheTask = nil
        }
    }
    
    /// Stops the recaching process.
    func stopCaching() {
        recacheTask?.cancel()
    }
}
