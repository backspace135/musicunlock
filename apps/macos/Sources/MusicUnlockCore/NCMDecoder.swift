// Container and key-box logic adapted from Unlock Music, Copyright (c) 2019-2021 MengYX (MIT).
import CommonCrypto
import Foundation

enum NCMDecoder {
    static let coreKey: [UInt8] = [0x68, 0x7a, 0x48, 0x52, 0x41, 0x6d, 0x73, 0x6f, 0x35, 0x6b, 0x49, 0x6e, 0x62, 0x61, 0x78, 0x57]
    static let metaKey: [UInt8] = [0x23, 0x31, 0x34, 0x6c, 0x6a, 0x6b, 0x5f, 0x21, 0x5c, 0x5d, 0x26, 0x30, 0x55, 0x3c, 0x27, 0x28]

    static func decode(_ data: Data) throws -> (bytes: [UInt8], metadata: AudioMetadata) {
        var reader = ByteReader(bytes: [UInt8](data))
        guard try reader.take(8) == Array("CTENFDAM".utf8) else {
            throw DecodeError.invalid("不是有效的 NCM 文件")
        }
        _ = try reader.take(2)
        let keyLength = try reader.little32()
        let encryptedKey = try reader.take(keyLength).map { $0 ^ 0x64 }
        let plaintextKey = try decryptAES(encryptedKey, key: coreKey)
        guard plaintextKey.count > 17,
              plaintextKey.starts(with: Array("neteasecloudmusic".utf8)) else {
            throw DecodeError.invalid("NCM 音频密钥无效")
        }
        let key = Array(plaintextKey.dropFirst(17))
        var box = Array(UInt8.min...UInt8.max)
        var j = 0
        for i in 0..<256 {
            j = (Int(box[i]) + j + Int(key[i % key.count])) & 0xff
            box.swapAt(i, j)
        }
        let mask: [UInt8] = (0..<256).map { index in
            let i = (index + 1) & 0xff
            let si = Int(box[i])
            let sj = Int(box[(i + si) & 0xff])
            return box[(si + sj) & 0xff]
        }
        var metadata = AudioMetadata()
        let metaLength = try reader.little32()
        if metaLength > 0 {
            let encrypted = try reader.take(metaLength).map { $0 ^ 0x63 }
            guard encrypted.count > 22,
                  let encoded = String(bytes: encrypted.dropFirst(22), encoding: .utf8),
                  let cipher = Data(base64Encoded: encoded) else {
                throw DecodeError.invalid("NCM 元数据无效")
            }
            let plain = try decryptAES([UInt8](cipher), key: metaKey)
            guard let separator = plain.firstIndex(of: 0x3a),
                  var json = try JSONSerialization.jsonObject(with: Data(plain.dropFirst(separator + 1))) as? [String: Any] else {
                throw DecodeError.invalid("NCM 元数据无法读取")
            }
            if let main = json["mainMusic"] as? [String: Any] { json = main }
            metadata.title = json["musicName"] as? String ?? ""
            metadata.album = json["album"] as? String ?? ""
            metadata.artist = (json["artist"] as? [[Any]] ?? []).compactMap { $0.first as? String }.joined(separator: "; ")
        }
        _ = try reader.take(5) // CRC32 + reserved byte
        let coverSpace = try reader.little32()
        let coverLength = try reader.little32()
        guard coverLength <= coverSpace else { throw DecodeError.invalid("NCM 封面长度无效") }
        let cover = try reader.take(coverSpace)
        if coverLength > 0 { metadata.cover = Data(cover.prefix(coverLength)) }
        var audio = Array(reader.bytes.dropFirst(reader.position))
        for i in audio.indices { audio[i] ^= mask[i & 0xff] }
        return (audio, metadata)
    }

    static func decryptAES(_ input: [UInt8], key: [UInt8]) throws -> [UInt8] {
        guard !input.isEmpty, input.count % kCCBlockSizeAES128 == 0 else {
            throw DecodeError.invalid("NCM AES 数据长度无效")
        }
        var output = [UInt8](repeating: 0, count: input.count + kCCBlockSizeAES128)
        var written = 0
        let capacity = output.count
        let status = output.withUnsafeMutableBytes { destination in
            input.withUnsafeBytes { source in
                key.withUnsafeBytes { keyBytes in
                    CCCrypt(CCOperation(kCCDecrypt), CCAlgorithm(kCCAlgorithmAES),
                            CCOptions(kCCOptionECBMode | kCCOptionPKCS7Padding),
                            keyBytes.baseAddress, key.count, nil, source.baseAddress, input.count,
                            destination.baseAddress, capacity, &written)
                }
            }
        }
        guard status == kCCSuccess else { throw DecodeError.invalid("NCM 密钥或元数据解密失败") }
        return Array(output.prefix(written))
    }
}
