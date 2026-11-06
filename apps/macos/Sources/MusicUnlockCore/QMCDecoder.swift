// Adapted from qmc.ts / qmc_key.ts, Copyright (c) 2019-2021 MengYX (MIT).
import Foundation

enum QMCDecoder {
    static func decode(_ input: [UInt8]) throws -> [UInt8] {
        guard input.count >= 8 else { throw DecodeError.invalid("QMC 文件过短") }
        let tail = Array(input.suffix(4))
        if tail == Array("STag".utf8) {
            throw DecodeError.unsupported("此 QQ 音乐文件未内嵌解密密钥，目前无法离线转换")
        }
        var key: [UInt8]?
        var audioSize = input.count
        if tail == Array("QTag".utf8) {
            let length = Int(Array(input[(input.count - 8)..<(input.count - 4)]).uint32BE())
            guard length > 0, length <= input.count - 8 else { throw DecodeError.invalid("QMC QTag 长度无效") }
            audioSize -= length + 8
            let suffix = Array(input[audioSize..<(input.count - 8)])
            guard let comma = suffix.firstIndex(of: 0x2c) else { throw DecodeError.invalid("QMC QTag 密钥无效") }
            key = try deriveKey(Array(suffix.prefix(comma)))
        } else {
            let length = Int(tail.uint32LE())
            if length < 0x300 {
                guard length > 0, length <= input.count - 4 else { throw DecodeError.invalid("QMC 内嵌密钥长度无效") }
                audioSize -= length + 4
                key = try deriveKey(Array(input[audioSize..<(input.count - 4)]))
            }
        }
        guard audioSize > 0 else { throw DecodeError.invalid("QMC 文件不包含音频") }
        var audio = Array(input.prefix(audioSize))
        if let key {
            if key.count > 300 { try QMCCipher.applyRC4(&audio, key: key) }
            else { try QMCCipher.applyMap(&audio, key: key) }
        } else {
            QMCCipher.applyStatic(&audio)
        }
        return audio
    }

    static func deriveKey(_ raw: [UInt8]) throws -> [UInt8] {
        guard let encoded = String(bytes: raw, encoding: .utf8),
              let decoded = Data(base64Encoded: encoded), decoded.count >= 16 else {
            throw DecodeError.invalid("QMC 内嵌密钥无效；新版外置 EKey 格式暂不支持")
        }
        let bytes = [UInt8](decoded)
        var teaKey = [UInt8](repeating: 0, count: 16)
        for i in 0..<8 {
            teaKey[i * 2] = UInt8(Int(abs(tan(106 + Double(i) * 0.1)) * 100) & 0xff)
            teaKey[i * 2 + 1] = bytes[i]
        }
        return Array(bytes.prefix(8)) + (try TencentTEA.decrypt(Array(bytes.dropFirst(8)), key: teaKey))
    }
}

// TEA block logic is also derived from Go Authors' golang.org/x/crypto/tea (BSD-3-Clause).
enum TencentTEA {
    static func block(_ bytes: [UInt8], key: [UInt8]) -> [UInt8] {
        var v0 = Array(bytes[0..<4]).uint32BE()
        var v1 = Array(bytes[4..<8]).uint32BE()
        let k = stride(from: 0, to: 16, by: 4).map { Array(key[$0..<($0 + 4)]).uint32BE() }
        var sum: UInt32 = 0x9e3779b9 &* 16
        for _ in 0..<16 {
            v1 = v1 &- (((v0 << 4) &+ k[2]) ^ (v0 &+ sum) ^ ((v0 >> 5) &+ k[3]))
            v0 = v0 &- (((v1 << 4) &+ k[0]) ^ (v1 &+ sum) ^ ((v1 >> 5) &+ k[1]))
            sum = sum &- 0x9e3779b9
        }
        return [v0, v1].flatMap { value in [24, 16, 8, 0].map { UInt8(truncatingIfNeeded: value >> $0) } }
    }

    static func decrypt(_ bytes: [UInt8], key: [UInt8]) throws -> [UInt8] {
        guard bytes.count >= 16, bytes.count % 8 == 0, key.count == 16 else {
            throw DecodeError.invalid("QMC TEA 数据长度无效")
        }
        var plain = block(Array(bytes.prefix(8)), key: key)
        let padding = Int(plain[0] & 7)
        let length = bytes.count - 1 - padding - 2 - 7
        guard length >= 0 else { throw DecodeError.invalid("QMC TEA 填充无效") }
        var previous = [UInt8](repeating: 0, count: 8)
        var current = Array(bytes.prefix(8))
        var inputIndex = 8
        var index = 1 + padding
        func nextByte() throws -> UInt8 {
            if index == 8 {
                guard inputIndex <= bytes.count - 8 else { throw DecodeError.invalid("QMC TEA 数据被截断") }
                previous = current
                current = Array(bytes[inputIndex..<(inputIndex + 8)])
                plain = block(zip(plain, current).map { $0 ^ $1 }, key: key)
                inputIndex += 8
                index = 0
            }
            defer { index += 1 }
            return plain[index] ^ previous[index]
        }
        _ = try nextByte()
        _ = try nextByte()
        var result: [UInt8] = []
        result.reserveCapacity(length)
        for _ in 0..<length { result.append(try nextByte()) }
        for _ in 0..<7 {
            guard try nextByte() == 0 else { throw DecodeError.invalid("QMC TEA 密钥校验失败") }
        }
        return result
    }
}
