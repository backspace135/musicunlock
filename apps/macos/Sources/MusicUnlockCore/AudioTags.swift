import Foundation

/// Restores container metadata using standard ID3v2.3 / FLAC blocks; encoded audio frames are untouched.
enum AudioTags {
    static func restore(_ audio: Data, ext: String, metadata: AudioMetadata) throws -> Data {
        guard !metadata.title.isEmpty || !metadata.artist.isEmpty || !metadata.album.isEmpty || metadata.cover != nil else { return audio }
        if ext == "mp3" {
            // Preserve existing ID3 metadata instead of replacing its unknown frames.
            if audio.starts(with: Data("ID3".utf8)) { return audio }
            var frames = Data()
            for (id, text) in [("TIT2", metadata.title), ("TPE1", metadata.artist), ("TALB", metadata.album)] where !text.isEmpty {
                let payload = Data([1, 0xff, 0xfe]) + (text.data(using: .utf16LittleEndian) ?? Data())
                frames += Data(id.utf8) + bigEndian(payload.count) + Data([0, 0]) + payload
            }
            if let cover = metadata.cover {
                let mime = cover.starts(with: [0x89, 0x50, 0x4e, 0x47]) ? "image/png" : "image/jpeg"
                let payload = Data([0]) + Data(mime.utf8) + Data([0, 3, 0]) + cover
                frames += Data("APIC".utf8) + bigEndian(payload.count) + Data([0, 0]) + payload
            }
            guard frames.count < 1 << 28 else { throw DecodeError.invalid("ID3 标签过大") }
            let size = frames.count
            let syncSafe = Data([UInt8((size >> 21) & 127), UInt8((size >> 14) & 127), UInt8((size >> 7) & 127), UInt8(size & 127)])
            return Data([0x49, 0x44, 0x33, 3, 0, 0]) + syncSafe + frames + audio
        }
        if ext == "flac" {
            let bytes = [UInt8](audio)
            var reader = ByteReader(bytes: bytes, position: 4)
            var blocks: [(UInt8, Data)] = []
            var last = false
            while !last {
                let header = try reader.take(4)
                last = header[0] & 0x80 != 0
                let length = Int(header[1]) << 16 | Int(header[2]) << 8 | Int(header[3])
                blocks.append((header[0] & 0x7f, Data(try reader.take(length))))
            }
            if !blocks.contains(where: { $0.0 == 4 }) {
                let vendor = Data("MusicUnlock".utf8)
                let comments = [("TITLE", metadata.title), ("ARTIST", metadata.artist), ("ALBUM", metadata.album)]
                    .filter { !$0.1.isEmpty }.map { Data("\($0.0)=\($0.1)".utf8) }
                var payload = littleEndian(vendor.count) + vendor + littleEndian(comments.count)
                for comment in comments { payload += littleEndian(comment.count) + comment }
                blocks.append((4, payload))
            }
            if let cover = metadata.cover, !blocks.contains(where: { $0.0 == 6 }) {
                let mime = Data((cover.starts(with: [0x89, 0x50, 0x4e, 0x47]) ? "image/png" : "image/jpeg").utf8)
                let picture = bigEndian(3) + bigEndian(mime.count) + mime + bigEndian(0)
                    + Data(repeating: 0, count: 16) + bigEndian(cover.count) + cover
                if picture.count < 1 << 24 { blocks.append((6, picture)) }
            }
            var result = Data("fLaC".utf8)
            for (index, block) in blocks.enumerated() {
                guard block.1.count < 1 << 24 else { throw DecodeError.invalid("FLAC 标签过大") }
                let length = block.1.count
                result += Data([block.0 | (index == blocks.count - 1 ? 0x80 : 0),
                                UInt8((length >> 16) & 255), UInt8((length >> 8) & 255), UInt8(length & 255)]) + block.1
            }
            result += Data(bytes.dropFirst(reader.position))
            return result
        }
        return audio
    }

    private static func bigEndian(_ number: Int) -> Data {
        Data([24, 16, 8, 0].map { UInt8(truncatingIfNeeded: number >> $0) })
    }

    private static func littleEndian(_ number: Int) -> Data {
        Data([0, 8, 16, 24].map { UInt8(truncatingIfNeeded: number >> $0) })
    }
}
