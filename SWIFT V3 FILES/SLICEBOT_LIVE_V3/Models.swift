//
//  Models.swift
//  SLICEBOT_REFACTORED_CLEAN
//
//  Created by Waste on 25/2/2025.
//

import Foundation

struct AudioFileMetadata {
    let fileURL: URL
    let duration: Double
    var isCandidate: Bool = true
}

struct CachedFile: Codable {
    let path: String
    let duration: Double
}

struct CacheData: Codable {
    let sourceDirectory: String
    let files: [CachedFile]
}

/// SliceInfo now stores startFrame and snippetFrameCount so that slice positions and lengths are maintained in frames.
struct SliceInfo: Codable {
    let fileURL: URL
    let startFrame: Int       // Start sample index
    let subdivisionSteps: Double
    let snippetFrameCount: Int  // Length of slice in frames
}

enum SourceMode: String, CaseIterable, Identifiable {
    case multi = "Multi-file"
    case singleRandom = "Single file (Random)"
    case singleManual = "Single file (Manual)"
    case live = "LIVE"  // NEW: LIVE option added for new recording module
    
    var id: String { self.rawValue }
}

enum LayeringMergeMode: String, CaseIterable, Identifiable {
    case none = "None"
    case crossfade = "Crossfade"
    case crossfadeReverse = "Crossfade Reverse"
    case fiftyFifty = "50/50"
    case quarterCuts = "1/4 Cuts"
    case pachinko = "Pachinko"
    
    var id: String { self.rawValue }
}
