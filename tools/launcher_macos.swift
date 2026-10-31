// SPDX-License-Identifier: GPL-3.0-or-later
// macOS launcher: pick resolution, anti-aliasing, volume, widescreen and the
// DOAXBV swimsuit switch (with the DOAXBV disc it builds swimsuits from),
// remember them in private/launcher.json, then exit 0 so launcher.sh starts
// the game. Mirrors tools/launcher.cs without SMAA (D3D11 only) or the build
// page: tools/launcher-background.jpg between letterbox bars, the page tabs and
// the chosen page's settings in the bottom one. Keyboard and mouse. Exits 1 if
// the window is closed.
import AppKit

let root = URL(fileURLWithPath: CommandLine.arguments.count > 1
    ? CommandLine.arguments[1] : FileManager.default.currentDirectoryPath)
let settingsURL = root.appendingPathComponent("private/launcher.json")

let heights = [480, 720, 1080, 1440, 2160]
let samples = [1, 2, 4, 8]
let volumes = [100, 75, 50, 25, 0]
// The swimsuit switch builds DOAXBV swimsuits from the player's own DOAXBV
// disc: an .iso image or the extracted folder, remembered in
// private/doaxbv-path.txt.
let doaxbvPathURL = root.appendingPathComponent("private/doaxbv-path.txt")
var doaxbvPath: String = (try? String(contentsOf: doaxbvPathURL, encoding: .utf8))?
    .components(separatedBy: .newlines).first?.trimmingCharacters(in: .whitespaces) ?? ""

var settings: [String: Any] = [:]
if let data = try? Data(contentsOf: settingsURL),
   let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
    settings = json
}
func intSetting(_ key: String, _ fallback: Int) -> Int {
    (settings[key] as? NSNumber)?.intValue ?? fallback
}
func boolSetting(_ key: String, _ fallback: Bool = false) -> Bool {
    (settings[key] as? NSNumber)?.boolValue ?? fallback
}
let screenHeight: Int = {
    guard let screen = NSScreen.screens.first ?? NSScreen.main else { return 1080 }
    return Int(screen.frame.height * screen.backingScaleFactor)
}()

func rgb(_ r: Int, _ g: Int, _ b: Int, _ a: Int = 255) -> NSColor {
    NSColor(srgbRed: CGFloat(r) / 255, green: CGFloat(g) / 255, blue: CGFloat(b) / 255, alpha: CGFloat(a) / 255)
}
func font(_ name: String, _ size: CGFloat) -> NSFont { NSFont(name: name, size: size) ?? .boldSystemFont(ofSize: size) }

// A setting with its choices, or an action row when names is empty.
final class Row {
    let label: String
    var info: String
    let names: [String]
    var index: Int
    init(_ label: String, _ info: String, _ names: [String] = [], _ index: Int = 0) {
        self.label = label; self.info = info; self.names = names; self.index = max(0, index)
    }
}

struct Page { let name: String; let info: String; let rows: [Row] }

final class LauncherView: NSView {
    // Horizon Bar layout, as in tools/launcher.cs: letterbox bars, the settings and page tabs in the bottom one.
    static let topH: CGFloat = 44, side: CGFloat = 30, fadeY: CGFloat = 334
    static let infoY: CGFloat = 380, labelY: CGFloat = 414, valueY: CGFloat = 434, valueH: CGFloat = 30
    static let tabY: CGFloat = 494, tabH: CGFloat = 26
    static let playPage = 4, exitPage = 5

    let resolution = Row("Resolution", "Sets the render resolution. 480p opens in a window",
                         heights.map { "\($0)p" }, heights.lastIndex(where: { $0 <= intSetting("height", screenHeight) }) ?? 0)
    let msaa = Row("MSAA", "Smooths polygon edges with multisampling", samples.map { $0 == 1 ? "Off" : "\($0)x" },
                   samples.firstIndex(of: intSetting("msaa", 1)) ?? 0)
    let screen = Row("Screen", "Wider 3D view. The HUD and menus are stretched", ["4:3", "16:9"], boolSetting("widescreen") ? 1 : 0)
    let volume = Row("Volume", "Sets the master volume", volumes.map { $0 == 0 ? "Mute" : "\($0)%" },
                     volumes.firstIndex(of: intSetting("volume", 100)) ?? 0)
    let freeCamera = Row("Free Camera", "F1, \u{60} or a right-stick click flies the camera; P freezes the game",
                         ["Off", "On"], boolSetting("free_camera") ? 1 : 0)
    let swimsuits = Row("Swimsuits", "In a fight, click L3: LB / LT P1, RB / RT P2, X / B favorite",
                        ["Off", "On"], boolSetting("swimsuits", true) ? 1 : 0)
    let startFavorite = Row("Start In Favorite", "Each fighter starts in her first favorite (L3 + X / B)",
                            ["Off", "On"], boolSetting("swimsuit_start_favorite") ? 1 : 0)
    let disc = Row("DOAXBV Disc", "")
    lazy var pages = [
        Page(name: "VIDEO SETTINGS", info: "Sets resolution, anti-aliasing and screen shape", rows: [resolution, msaa, screen]),
        Page(name: "AUDIO SETTINGS", info: "Sets the master volume", rows: [volume]),
        Page(name: "FREE CAMERA", info: "Fly the camera through fights and cutscenes, freeze them and save screenshots", rows: [freeCamera]),
        Page(name: "SWIMSUITS", info: "DOAXBV swimsuits; needs your own DOAXBV disc (.iso or folder) to build them from",
             rows: [swimsuits, startFavorite, disc]),
        Page(name: "START GAME", info: "Saves these settings and starts the game", rows: []),
        Page(name: "EXIT", info: "Closes the launcher", rows: []),
    ]
    // The tab cursor picks a page; inside a page the row cursor picks a setting column.
    // Not "menu": NSView inherits menu: NSMenu? from NSResponder.
    var tab = LauncherView.playPage, row = 0, inPage = false
    var tabs: [NSRect] = []
    var installed = "INSTALLED: USA 3.0"
    var onPlay: () -> Void = {}
    let background = NSImage(contentsOf: root.appendingPathComponent("tools/launcher-background.jpg"))

    let itemFont = font("HelveticaNeue-BoldItalic", 15), menuFont = font("HelveticaNeue-BoldItalic", 19)
    let tabFont = font("HelveticaNeue-BoldItalic", 14), infoFont = font("HelveticaNeue-Italic", 14), smallFont = font("HelveticaNeue-Bold", 12)
    let titleFont = font("HelveticaNeue-BoldItalic", 25), logoFont = font("Arial-Black", 38), bigFont = font("Arial-Black", 66)

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        showDisc()
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect], owner: self, userInfo: nil))
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    var rows: [Row] { pages[tab].rows }
    var columnWidth: CGFloat { (bounds.width - 2 * LauncherView.side) / CGFloat(max(rows.count, 1)) }

    func showDisc() {
        disc.info = doaxbvPath.isEmpty ? "No DOAXBV disc chosen; choose its .iso or folder"
            : "Disc: " + (doaxbvPath as NSString).lastPathComponent
    }

    func chooseDisc() {
        let panel = NSOpenPanel()
        panel.message = "Choose your DOAXBV disc image (.iso) or its extracted folder"
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        doaxbvPath = url.path
        try? FileManager.default.createDirectory(at: doaxbvPathURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? (doaxbvPath + "\n").write(to: doaxbvPathURL, atomically: true, encoding: .utf8)
        showDisc()
    }

    var chosen: [String: Any] {
        [
            "swimsuits": swimsuits.index == 1,
            "swimsuit_start_favorite": startFavorite.index == 1,
            "height": heights[resolution.index],
            "msaa": samples[msaa.index],
            "smaa": false,
            "volume": volumes[volume.index],
            "widescreen": screen.index == 1,
            "free_camera": freeCamera.index == 1,
        ]
    }

    func change(_ delta: Int) {
        let r = rows[row]
        if !r.names.isEmpty { r.index = min(max(r.index + delta, 0), r.names.count - 1) }
    }

    func accept() {
        if !inPage {
            if tab == LauncherView.playPage { onPlay(); return }
            if tab == LauncherView.exitPage { NSApp.terminate(nil); return }
            inPage = true
            row = 0
        } else if rows[row].names.isEmpty {
            chooseDisc()
        } else {
            change(1)
        }
    }

    func back() {
        if inPage { inPage = false } else { NSApp.terminate(nil) }
    }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 123: if inPage { row = max(row - 1, 0) } else { tab = max(tab - 1, 0) }
        case 124: if inPage { row = min(row + 1, rows.count - 1) } else { tab = min(tab + 1, pages.count - 1) }
        case 126: if inPage { change(1) }
        case 125: if inPage { change(-1) } else if !rows.isEmpty { accept() }
        case 36, 76, 49: accept()
        case 53, 51: back()
        default: super.keyDown(with: event); return
        }
        needsDisplay = true
    }

    func hitTab(_ p: NSPoint) -> Int { tabs.firstIndex { $0.contains(p) } ?? -1 }
    func hitColumn(_ p: NSPoint) -> Int {
        guard p.y >= LauncherView.labelY - 6, p.y <= LauncherView.valueY + LauncherView.valueH, p.x >= LauncherView.side else { return -1 }
        let i = Int((p.x - LauncherView.side) / columnWidth)
        return i < rows.count ? i : -1
    }

    override func mouseMoved(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let m = hitTab(p), i = hitColumn(p)
        if !inPage && m >= 0 && m != tab { tab = m; needsDisplay = true }
        if inPage && i >= 0 && i != row { row = i; needsDisplay = true }
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let m = hitTab(p), i = hitColumn(p)
        defer { needsDisplay = true }
        if m >= 0 { tab = m; inPage = false; accept(); return }
        guard i >= 0 else { return }
        // From the tabs, the first click only enters the page and selects the setting.
        let entering = !inPage
        inPage = true
        row = i
        if entering { return }
        if rows[i].names.isEmpty { chooseDisc() }
        else { change(p.x < LauncherView.side + (CGFloat(i) + 0.5) * columnWidth ? -1 : 1) }
    }

    override func rightMouseDown(with event: NSEvent) { back(); needsDisplay = true }

    // Text vertically centred in rect; shadowed text gets the logo's dark drop shadow.
    func text(_ s: String, _ f: NSFont, _ color: NSColor, _ rect: NSRect, _ align: NSTextAlignment = .left,
              shadow: Bool = false, oblique: CGFloat = 0) {
        let style = NSMutableParagraphStyle()
        style.alignment = align
        var attributes: [NSAttributedString.Key: Any] = [.font: f, .foregroundColor: color, .paragraphStyle: style, .obliqueness: oblique]
        if shadow {
            let drop = NSShadow()
            drop.shadowColor = rgb(30, 22, 18, 220)
            drop.shadowOffset = NSSize(width: 2, height: -2)
            attributes[.shadow] = drop
        }
        let string = NSAttributedString(string: s, attributes: attributes)
        let height = min(rect.height, string.boundingRect(with: rect.size, options: [.usesLineFragmentOrigin]).height)
        string.draw(in: NSRect(x: rect.minX, y: rect.minY + (rect.height - height) / 2, width: rect.width, height: height))
    }

    // A triangle pointing along dx, dy from its base centred on x, y.
    func arrow(_ x: CGFloat, _ y: CGFloat, _ dx: CGFloat, _ dy: CGFloat, _ color: NSColor) {
        let path = NSBezierPath()
        path.move(to: NSPoint(x: x + 7 * dx, y: y + 7 * dy))
        path.line(to: NSPoint(x: x - 5 * dy, y: y - 5 * dx))
        path.line(to: NSPoint(x: x + 5 * dy, y: y + 5 * dx))
        path.close()
        color.setFill()
        path.fill()
    }

    func width(_ s: String, _ f: NSFont) -> CGFloat { NSAttributedString(string: s, attributes: [.font: f]).size().width }

    override func draw(_ dirtyRect: NSRect) {
        let w = bounds.width, h = bounds.height, side = LauncherView.side, topH = LauncherView.topH
        let labelY = LauncherView.labelY, valueY = LauncherView.valueY, valueH = LauncherView.valueH
        let tabY = LauncherView.tabY, tabH = LauncherView.tabH
        let sand = rgb(241, 197, 143), dark = rgb(42, 29, 21), muted = rgb(191, 178, 165), chrome = rgb(226, 228, 228)
        rgb(74, 60, 51).setFill()
        bounds.fill()
        background?.draw(in: bounds, from: .zero, operation: .copy, fraction: 1, respectFlipped: true, hints: nil)

        // Letterbox: a solid top bar, and the beach fading into the same dark under the settings.
        rgb(13, 10, 9).setFill()
        NSRect(x: 0, y: 0, width: w, height: topH).fill()
        let fadeStops: [CGFloat] = [0, 0.26, 1]
        NSGradient(colors: [rgb(13, 10, 9, 0), rgb(13, 10, 9, 230), rgb(13, 10, 9)], atLocations: fadeStops, colorSpace: .sRGB)?
            .draw(in: NSRect(x: 0, y: LauncherView.fadeY, width: w, height: h - LauncherView.fadeY), angle: 90)

        // Top bar: title and install state on the left, key prompts on the right.
        text("DEAD OR ALIVE 3  \u{00B7}  " + installed, smallFont, muted, NSRect(x: side, y: 0, width: 500, height: topH))
        var x = w - side
        for (key, label) in [("", "CHANGE"), ("Esc", "BACK"), ("Return", "SELECT")] {
            x -= width(label, smallFont)
            text(label, smallFont, muted, NSRect(x: x, y: 0, width: width(label, smallFont) + 4, height: topH))
            let pillW: CGFloat = key.isEmpty ? 26 : width(key, smallFont) + 14
            x -= pillW + 6
            let pill = NSRect(x: x, y: topH / 2 - 10, width: pillW, height: 20)
            rgb(242, 227, 207).setFill()
            NSBezierPath(roundedRect: pill, xRadius: 10, yRadius: 10).fill()
            if key.isEmpty { arrow(x + 9, topH / 2 + 2, 0, -1, dark); arrow(x + 17, topH / 2 - 2, 0, 1, dark) }
            else { text(key, smallFont, dark, pill, .center) }
            x -= 22
        }

        // Logo over the cliff.
        text("3", bigFont, chrome, NSRect(x: 0, y: 58, width: w - 44, height: 90), .right, shadow: true, oblique: 0.2)
        text("DEAD OR ALIVE", logoFont, chrome, NSRect(x: 0, y: 82, width: w - 122, height: 56), .right, shadow: true, oblique: 0.2)

        // The chosen page in the bottom bar: its information line, then its settings side by side.
        let page = pages[tab]
        text(inPage ? page.rows[row].info : page.info, infoFont, rgb(216, 202, 188),
             NSRect(x: side, y: LauncherView.infoY, width: w - 2 * side, height: 22))
        if tab == LauncherView.playPage {
            text("PRESS START BUTTON", titleFont, chrome, NSRect(x: side, y: valueY - 8, width: w - 2 * side, height: 40), shadow: true)
        }
        let colW = columnWidth
        for (i, r) in page.rows.enumerated() {
            let left = side + CGFloat(i) * colW
            let selected = inPage && i == row
            if selected {
                rgb(232, 191, 138, 45).setFill()
                NSRect(x: left, y: labelY - 6, width: colW, height: valueY + valueH + 6 - labelY).fill(using: .sourceOver)
            }
            if i > 0 {
                rgb(255, 255, 255, 36).setFill()
                NSRect(x: left, y: labelY - 2, width: 1, height: valueY + valueH + 2 - labelY).fill(using: .sourceOver)
            }
            text(r.label.uppercased(), smallFont, selected ? .white : rgb(198, 182, 166), NSRect(x: left + 12, y: labelY, width: colW - 16, height: 18))
            guard !r.names.isEmpty else {
                text("CHOOSE", itemFont, sand, NSRect(x: left + 12, y: valueY, width: colW - 16, height: valueH))
                continue
            }
            let value = r.names[r.index].uppercased()
            let arrowColor = selected ? sand : rgb(241, 197, 143, 130)
            arrow(left + 19, valueY + valueH / 2, -1, 0, arrowColor)
            arrow(left + 30 + width(value, menuFont), valueY + valueH / 2, 1, 0, arrowColor)
            text(value, menuFont, selected ? .white : rgb(232, 222, 212), NSRect(x: left + 26, y: valueY, width: colW - 30, height: valueH))
        }

        // Page tabs along the bottom, the current one underlined in sunset gold.
        rgb(255, 255, 255, 36).setFill()
        NSRect(x: side, y: tabY - 8, width: w - 2 * side, height: 1).fill(using: .sourceOver)
        var tx = side
        tabs = pages.enumerated().map { m, p in
            let tw = width(p.name, tabFont)
            let rect = NSRect(x: tx, y: tabY, width: tw, height: tabH)
            text(p.name, tabFont, m == tab ? rgb(255, 243, 228) : rgb(169, 156, 144), NSRect(x: tx, y: tabY, width: tw + 4, height: tabH))
            if m == tab {
                NSGradient(starting: rgb(247, 220, 181), ending: rgb(201, 138, 85))?.draw(in: NSRect(x: tx, y: tabY + tabH, width: tw, height: 3), angle: 0)
            }
            tx += tw + 18
            return rect
        }
    }
}

final class Launcher: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 960, height: 540),
                          styleMask: [.titled, .closable], backing: .buffered, defer: false)
    let view = LauncherView(frame: NSRect(x: 0, y: 0, width: 960, height: 540))
    var launched = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        // The installed version, from the build's record; older builds are USA 3.0.
        let build = (try? Data(contentsOf: root.appendingPathComponent("private/build.json")))
            .flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
        view.installed = "INSTALLED: " + ((build?["version"] as? String) ?? "USA 3.0").uppercased()
        view.onPlay = { [unowned self] in self.play() }
        window.title = "Dead or Alive 3"
        window.delegate = self
        window.contentView = view
        window.acceptsMouseMovedEvents = true
        window.makeFirstResponder(view)
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func play() {
        do {
            try FileManager.default.createDirectory(
                at: settingsURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            let data = try JSONSerialization.data(
                withJSONObject: view.chosen, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: settingsURL)
        } catch {
            NSAlert(error: error).runModal()
            return
        }
        launched = true
        NSApp.terminate(nil)
    }

    func windowWillClose(_ notification: Notification) {
        if !launched { exit(1) }
    }

    // AppKit exits 0 after terminate, so a Dock or Cmd-Q quit would start the game.
    func applicationWillTerminate(_ notification: Notification) {
        exit(launched ? 0 : 1)
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let launcher = Launcher()
app.delegate = launcher
app.run()
exit(launcher.launched ? 0 : 1)
