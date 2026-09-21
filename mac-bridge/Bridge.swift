// CoreS3 Bridge —— 两个方向的事都在这儿：
//
//   ① 音乐（Mac → 设备）：每 2 秒跑一次 `media-control get`（封装 MediaRemote 私有框架），
//      拿到 title / artist / album / playing / elapsedTime / duration / artworkData，
//      文本元数据一包写进 META；封面压成 160x160 JPEG 后分包写进 ART。
//      控制方向（播放/暂停、上下曲）不经过这里 —— 设备直接发 HID 媒体键给 Mac。
//
//   ② 记事本（设备 → Mac）：订阅 NOTE_CHR_OUT，把设备推来的速记按分包协议拼起来，
//      追加写进 Obsidian 的 face3notes.md，然后往 NOTE_CHR_ACK 写一个字节回执。
//      设备那边等不到回执就会把内容留在队列里，等下次连上再补发。
//
// ⚠️ 首次运行必须在「系统设置 → 隐私与安全性 → 蓝牙」里允许本程序，
//    否则 CoreBluetooth 不会回调，日志里只会停在这一行：[bridge] scanning…

import Foundation
import CoreBluetooth
import AppKit

// 必须和固件 config.h 里的 UUID 完全一致
let SVC_UUID  = CBUUID(string: "c3a10001-5f4e-4a3b-9d2c-1e0f8a7b6d5c")
let META_UUID = CBUUID(string: "c3a10002-5f4e-4a3b-9d2c-1e0f8a7b6d5c")
let ART_UUID  = CBUUID(string: "c3a10003-5f4e-4a3b-9d2c-1e0f8a7b6d5c")

// 记事本（v0.6.0）：设备 notify、Mac 订阅
let NOTE_SVC   = CBUUID(string: "c3a10011-5f4e-4a3b-9d2c-1e0f8a7b6d5c")
let NOTE_OUT   = CBUUID(string: "c3a10012-5f4e-4a3b-9d2c-1e0f8a7b6d5c")   // NOTIFY
let NOTE_ACK   = CBUUID(string: "c3a10013-5f4e-4a3b-9d2c-1e0f8a7b6d5c")   // WRITE

let TARGET_NAME = "CoreS3"
let POLL_SEC: TimeInterval = 2.0
let MEDIA_CONTROL = "/opt/homebrew/bin/media-control"

// 速记落点。默认写到用户 Documents 下的 face3notes.md；
// 想直接落进自己的 Obsidian vault，用环境变量指过去，例如：
//   export CORES3_NOTES_FILE="$HOME/Documents/Obsidian_Vault/MyVault/face3notes.md"
// （LaunchAgent 自启时在 plist 的 EnvironmentVariables 里设同一变量即可。）
let NOTES_FILE: String = {
    if let e = ProcessInfo.processInfo.environment["CORES3_NOTES_FILE"], !e.isEmpty { return e }
    return NSString("~/Documents/face3notes.md").expandingTildeInPath
}()

// ─── 日志 ───────────────────────────────────────────────────────────────
// 必须用 `open MusicBridge.app` 启动（走 LaunchServices 才能弹蓝牙授权窗）；
// 那样 stdout 会丢，所以同时写一份到文件。
let LOG_PATH = "/tmp/cores3-bridge.log"
func log(_ m: String) {
    print(m)
    let line = m + "\n"
    guard let d = line.data(using: .utf8) else { return }
    let fm = FileManager.default
    if fm.fileExists(atPath: LOG_PATH) {
        if let fh = FileHandle(forWritingAtPath: LOG_PATH) {
            fh.seekToEndOfFile(); fh.write(d); try? fh.close()
        }
    } else {
        fm.createFile(atPath: LOG_PATH, contents: d, attributes: nil)
    }
}

final class Bridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var cm: CBCentralManager!
    var peri: CBPeripheral?
    var metaCh: CBCharacteristic?
    var artCh: CBCharacteristic?
    var lastMeta = ""
    var lastArtKey = ""

    // 记事本
    var noteOutCh: CBCharacteristic?
    var noteAckCh: CBCharacteristic?
    var noteBuf = Data()
    var noteTotal = 0

    func start() { cm = CBCentralManager(delegate: self, queue: nil) }

    // ─── 中央管理器 ───────────────────────────────────────────────────────
    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn:
            log("[bridge] scanning… （找不到 CoreS3 就检查它在不在广播）")
            c.scanForPeripherals(withServices: nil, options: nil)
        case .unauthorized:
            log("[bridge] ✗ 蓝牙未授权：系统设置 → 隐私与安全性 → 蓝牙 → 勾选本程序")
        case .poweredOff:
            log("[bridge] ✗ 蓝牙已关闭")
        default:
            log("[bridge] bt state=\(c.state.rawValue)")
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        guard (p.name ?? "").contains(TARGET_NAME) else { return }
        log("[bridge] found \(p.name ?? "?") rssi=\(rssi)")
        peri = p
        p.delegate = self
        c.stopScan()
        c.connect(p, options: nil)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        log("[bridge] connected ✅")
        p.discoverServices([SVC_UUID, NOTE_SVC])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        log("[bridge] ✗ connect failed: \(error?.localizedDescription ?? "?")")
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { c.scanForPeripherals(withServices: nil, options: nil) }
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        log("[bridge] disconnected, rescan…")
        metaCh = nil; artCh = nil; lastMeta = ""; lastArtKey = ""
        noteOutCh = nil; noteAckCh = nil; noteBuf = Data(); noteTotal = 0
        c.scanForPeripherals(withServices: nil, options: nil)
    }

    // ─── 外设 ─────────────────────────────────────────────────────────────
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        if let e = error { log("[bridge] svc err \(e.localizedDescription)"); return }
        guard let ss = p.services else { return }
        for s in ss {
            if s.uuid == SVC_UUID {
                log("[bridge] music service found")
                p.discoverCharacteristics([META_UUID, ART_UUID], for: s)
            } else if s.uuid == NOTE_SVC {
                log("[bridge] note service found")
                p.discoverCharacteristics([NOTE_OUT, NOTE_ACK], for: s)
            }
        }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for c in s.characteristics ?? [] {
            if c.uuid == META_UUID { metaCh = c; log("[bridge] META ready") }
            if c.uuid == ART_UUID  { artCh  = c; log("[bridge] ART ready") }
            if c.uuid == NOTE_OUT  {
                noteOutCh = c
                log("[bridge] NOTE OUT ready, 订阅中…")
                p.setNotifyValue(true, for: c)          // ← 关键：不订阅就收不到 notify
            }
            if c.uuid == NOTE_ACK  {
                noteAckCh = c; log("[bridge] NOTE ACK ready")
                syncDeviceClock(p)          // v0.6.2：连上顺手给设备对时
            }
        }
    }

    // ─── 设备 → Mac：记事本速记 ──────────────────────────────────────────
    // 分包协议（跟封面那条对称、方向相反）：
    //   首包 [0]=0xA5 + [1..2]=总长(u16 LE)；续包 [0]=0x5A。
    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        guard c.uuid == NOTE_OUT, let d = c.value, d.count > 0 else { return }
        let tag = d[0]
        if tag == 0xA5 {
            guard d.count >= 3 else { return }
            noteTotal = Int(d[1]) | (Int(d[2]) << 8)
            noteBuf = Data(d.dropFirst(3))
        } else if tag == 0x5A {
            noteBuf.append(d.dropFirst())
        } else {
            return
        }
        if noteBuf.count >= noteTotal {
            let text = String(data: noteBuf.prefix(noteTotal), encoding: .utf8) ?? ""
            noteBuf = Data(); noteTotal = 0
            appendNote(text)
            if let ack = noteAckCh {
                p.writeValue(Data([0x01]), for: ack, type: .withResponse)
            }
        }
    }

    // 设备 → Mac：顺手对一次时（v0.6.2）。
    // CoreS3 的 RTC 没有后备电池，掉电 / 重烧会归零；不对时，离线写的笔记
    // 就烙不上正确的书写时间。复用 NOTE_ACK 这条 WRITE 通道，不新增特征。
    func syncDeviceClock(_ p: CBPeripheral) {
        guard let ch = noteAckCh else { return }
        let c = Calendar.current.dateComponents([.year, .month, .day, .hour, .minute, .second], from: Date())
        let y = (c.year ?? 2000) - 2000
        let b: [UInt8] = [0x02,
                          UInt8(clamping: y), UInt8(clamping: c.month ?? 1), UInt8(clamping: c.day ?? 1),
                          UInt8(clamping: c.hour ?? 0), UInt8(clamping: c.minute ?? 0), UInt8(clamping: c.second ?? 0)]
        p.writeValue(Data(b), for: ch, type: .withResponse)
        log(String(format: "[bridge] 对时 → 设备 %04d-%02d-%02d %02d:%02d:%02d",
                   c.year ?? 0, c.month ?? 0, c.day ?? 0, c.hour ?? 0, c.minute ?? 0, c.second ?? 0))
    }

    // 追加写进 face3notes.md：同一天的都归到同一个 ## 日期 小标题下面，
    // 内容里除第一行外的行缩进两格，保持 markdown 列表的可读性。
    //
    // 时间来源（v0.6.2）：优先用设备烙在正文前的书写时间 "[YYYY-MM-DD HH:MM] "，
    // 没有这个前缀（老固件 / 设备 RTC 归零）才退回 Mac 当前时间（= 送达时刻）。
    // → 离线补发的笔记会归回它自己那天的 ## 小标题下面，而不是送达那天。
    func appendNote(_ text: String) {
        var day = "", hm = "", body = text
        let n = text.count
        if n >= 19 {
            let at = { (i: Int) -> Character in text[text.index(text.startIndex, offsetBy: i)] }
            if at(0) == "[" && at(5) == "-" && at(8) == "-" && at(11) == " "
                && at(14) == ":" && at(17) == "]" && at(18) == " " {
                day  = String(text[text.index(text.startIndex, offsetBy: 1)..<text.index(text.startIndex, offsetBy: 11)])
                hm   = String(text[text.index(text.startIndex, offsetBy: 12)..<text.index(text.startIndex, offsetBy: 17)])
                body = String(text[text.index(text.startIndex, offsetBy: 19)...])
            }
        }
        if day.isEmpty {
            let df = DateFormatter()
            df.locale = Locale(identifier: "en_US_POSIX")
            df.dateFormat = "yyyy-MM-dd"; day = df.string(from: Date())
            df.dateFormat = "HH:mm";      hm  = df.string(from: Date())
        }

        let fm = FileManager.default
        if !fm.fileExists(atPath: NOTES_FILE) {
            try? "# Face3 速记\n".write(toFile: NOTES_FILE, atomically: true, encoding: .utf8)
        }
        var out = (try? String(contentsOfFile: NOTES_FILE, encoding: .utf8)) ?? ""

        let lines = body.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        let head = lines.first ?? ""
        var entry = "- \(hm) \(head)\n"
        for l in lines.dropFirst() { entry += "  \(l)\n" }

        if let r = out.range(of: "## \(day)") {
            // 已有这一天的段落 → 插到段落末尾（补发的旧笔记回自己那天）
            var at = out.endIndex
            if let nl = out[r.upperBound...].range(of: "\n## ") { at = nl.lowerBound }
            out.insert(contentsOf: entry, at: at)
        } else {
            // 新的一天 → 按日期顺序插进去，别直接追加到文件末尾：
            // 补发的旧笔记会在文件底部冒出一个比今天更早的标题，看着很乱。
            var insertAt = out.endIndex
            var from = out.startIndex
            while let r = out.range(of: "\n## ", range: from..<out.endIndex) {
                let after = r.upperBound
                if let nl = out[after...].range(of: "\n"), String(out[after..<nl.lowerBound]) > day {
                    insertAt = r.lowerBound
                    break
                }
                from = after
            }
            out.insert(contentsOf: "\n## \(day)\n\n" + entry, at: insertAt)
        }
        try? out.write(toFile: NOTES_FILE, atomically: true, encoding: .utf8)
        log("[note] → \(day) \(hm) \(head)")
    }

    // ─── 轮询 ─────────────────────────────────────────────────────────────
    func tick() {
        guard let p = peri, p.state == .connected, metaCh != nil else { return }
        guard let np = nowPlaying() else { return }

        let title  = np["title"]  as? String ?? ""
        let artist = np["artist"] as? String ?? ""
        let album  = np["album"]  as? String ?? ""
        let playing = (np["playing"] as? Bool) ?? false
        let pos = Int(((np["elapsedTime"] as? Double) ?? 0) * 1000)
        let dur = Int(((np["duration"]   as? Double) ?? 0) * 1000)

        // 标题里不能出现分隔符，否则设备端会解析错位
        let meta = [title, artist, album]
            .map { $0.replacingOccurrences(of: "|", with: "/") }
            .joined(separator: "|") + "|\(playing ? 1 : 0)|\(pos)|\(dur)"

        if meta != lastMeta, let d = meta.data(using: .utf8), let mc = metaCh {
            lastMeta = meta
            p.writeValue(d, for: mc, type: .withResponse)
            log("[bridge] → \(title) / \(artist) [\(playing ? "PLAY" : "PAUSE")]")
        }

        let key = "\(title)|\(artist)"
        if key != lastArtKey,
           let b64 = np["artworkData"] as? String,
           let raw = Data(base64Encoded: b64),
           let img = NSImage(data: raw),
           let jpg = jpegData(img) {
            lastArtKey = key
            pushArt(jpg, p)
        }
    }

    func nowPlaying() -> [String: Any]? {
        let t = Process()
        t.executableURL = URL(fileURLWithPath: MEDIA_CONTROL)
        t.arguments = ["get"]
        let pipe = Pipe()
        t.standardOutput = pipe
        t.standardError = Pipe()
        do { try t.run() } catch { return nil }
        let d = pipe.fileHandleForReading.readDataToEndOfFile()
        t.waitUntilExit()
        guard d.count > 4 else { return nil }         // 没在播时输出 "null"
        return try? JSONSerialization.jsonObject(with: d) as? [String: Any]
    }

    func jpegData(_ img: NSImage) -> Data? {
        let sz = NSSize(width: 160, height: 160)
        let out = NSImage(size: sz)
        out.lockFocus()
        img.draw(in: NSRect(origin: .zero, size: sz), from: .zero,
                 operation: .copy, fraction: 1.0)
        out.unlockFocus()
        guard let tiff = out.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff) else { return nil }
        return rep.representation(using: .jpeg, properties: [.compressionFactor: 0.6])
    }

    // 分包协议：首包 [0]=0xA5 + [1..4]=总长(u32 LE)，续包 [0]=0x5A
    func pushArt(_ data: Data, _ p: CBPeripheral) {
        guard let ac = artCh else { return }
        let maxLen = p.maximumWriteValueLength(for: .withResponse)
        let total = UInt32(data.count)
        var off = 0
        var first = true
        while off < data.count {
            let hdr = first ? 5 : 1
            let chunk = min(maxLen - hdr, data.count - off)
            var pkt = Data()
            if first {
                pkt.append(0xA5)
                pkt.append(UInt8(total & 0xFF))
                pkt.append(UInt8((total >> 8) & 0xFF))
                pkt.append(UInt8((total >> 16) & 0xFF))
                pkt.append(UInt8((total >> 24) & 0xFF))
            } else {
                pkt.append(0x5A)
            }
            pkt.append(data.subdata(in: off ..< (off + chunk)))
            p.writeValue(pkt, for: ac, type: .withResponse)
            off += chunk
            first = false
        }
        log("[bridge] → artwork \(data.count) bytes")
    }
}

setvbuf(stdout, nil, _IONBF, 0)
log("[bridge] CoreS3 Music Bridge 启动")
let b = Bridge()
b.start()
Timer.scheduledTimer(withTimeInterval: POLL_SEC, repeats: true) { _ in b.tick() }
RunLoop.main.run()
