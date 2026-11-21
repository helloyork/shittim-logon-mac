// shittim-logon / macos / settings / PeriodBar.swift
//
// config/src/periodbar.h: the day as a bar, on 场景 (every period in colour) and on a
// period's own page (that one only). 00:00 at the left, 24:00 at the right, one stretch
// per period -- the one that crosses midnight in the same colour at both ends -- the
// minute the clock is at now, and a handle on every boundary.
//
// A boundary can be dragged. It is a period's start, and moving it moves the end of the
// period before it and nothing else; it stops at its neighbours (scenes::ClampStart), so
// a drag never reorders the ring. The file is written once, when the handle is let go,
// and `held` tells the page where it is meanwhile so the rows can say so. Clicking a
// stretch opens that period. Not ported: the tip over a stretch too narrow for its name.

import SwiftUI

/// PeriodColour: by the period's place in the file, so it does not change when a
/// boundary moves. Six hues, then repeating; a light set and a dark one.
func periodColour(_ i: Int, dark: Bool) -> (fill: Color, text: Color) {
    let light: [(UInt32, UInt32)] = [(0x9FE1CB, 0x085041), (0xCECBF6, 0x3C3489), (0xF5C4B3, 0x712B13),
                                     (0xFAC775, 0x633806), (0xB5D4F4, 0x0C447C), (0xF4C0D1, 0x72243E)]
    let night: [(UInt32, UInt32)] = [(0x0F6E56, 0xE1F5EE), (0x534AB7, 0xEEEDFE), (0x993C1D, 0xFAECE7),
                                     (0x854F0B, 0xFAEEDA), (0x185FA5, 0xE6F1FB), (0x993556, 0xFBEAF0)]
    let c = (dark ? night : light)[((i % 6) + 6) % 6]
    return (rgb(c.0), rgb(c.1))
}
/// PeriodMuted: every period but the one a page is about.
func periodMuted(dark: Bool) -> (fill: Color, text: Color) {
    dark ? (rgb(0x4A4945), rgb(0xD3D1C7)) : (rgb(0xE3E1DA), rgb(0x5F5E5A))
}
func rgb(_ v: UInt32) -> Color {
    Color(.sRGB, red: Double((v >> 16) & 0xFF) / 255, green: Double((v >> 8) & 0xFF) / 255, blue: Double(v & 0xFF) / 255)
}

/// A boundary being dragged: whose start, and where it is now.
struct HeldBoundary: Equatable {
    var index: Int
    var minute: Int
}

struct PeriodBar: View {
    @EnvironmentObject var store: Store
    @Environment(\.colorScheme) private var scheme
    var focus = -1
    @Binding var held: HeldBoundary?
    let open: (Int) -> Void

    static let barTop: CGFloat = 22, barH: CGFloat = 30, height: CGFloat = barTop + barH + 22
    static let grabSlop: CGFloat = 8
    static let day = 1440

    @State private var atRightEdge = false
    @State private var pressedAt = -1
    @State private var pressX: CGFloat = 0
    @State private var moved = false
    @State private var pressing = false
    @State private var over: CGPoint? = nil

    // The file's periods as starts, in file order, with the held one where the drag has it.
    private var starts: [Int] {
        let _ = store.revision
        var s = (0..<store.periodCount).map { store.s.periodStart($0) }
        if let h = held, h.index < s.count { s[h.index] = h.minute }
        return s
    }
    private var names: [String] { (0..<store.periodCount).map { store.s.periodLabel($0) } }

    /// PeriodSpanMinutes: from a start to the next one round the ring.
    private static func span(_ s: [Int], _ i: Int) -> Int {
        if s.count < 2 { return day }
        var best = day
        for j in s.indices where j != i {
            let d = ((s[j] - s[i]) % day + day) % day
            if d > 0 && d < best { best = d }
        }
        return best
    }
    /// Pieces: one stretch, or two for a period that crosses midnight.
    private static func pieces(_ s: [Int], _ i: Int) -> [(Int, Int)] {
        let start = s[i], n = span(s, i)
        if n >= day { return [(0, day)] }
        if start + n > day { return [(start, day), (0, start + n - day)] }
        return [(start, start + n)]
    }
    /// PeriodAt: the period whose stretch holds `minute`.
    private static func periodAt(_ s: [Int], _ minute: Int) -> Int {
        guard !s.isEmpty else { return -1 }
        for i in s.indices {
            let d = ((minute - s[i]) % day + day) % day
            if d < span(s, i) { return i }
        }
        return 0
    }

    var body: some View {
        GeometryReader { geo in
            let w = max(1, geo.size.width)
            Canvas { ctx, size in draw(ctx, size: size) }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { v in drag(v, w: w) }.onEnded { _ in release() })
                .onContinuousHover { phase in
                    if case .active(let p) = phase { over = p } else { over = nil }
                }
        }
        .frame(height: Self.height)
    }

    private func x(_ minute: Int, _ w: CGFloat) -> CGFloat { w * CGFloat(minute) / CGFloat(Self.day) }
    private func minute(at x: CGFloat, _ w: CGFloat) -> CGFloat { min(CGFloat(Self.day), max(0, x / w * CGFloat(Self.day))) }
    private func inBar(_ y: CGFloat) -> Bool { y >= Self.barTop - 6 && y <= Self.barTop + Self.barH + 6 }

    /// BoundaryNear: the boundary within reach of `x`, or -1. One period has none.
    private func boundaryNear(_ x0: CGFloat, _ w: CGFloat) -> Int {
        let s = starts
        if s.count < 2 { return -1 }
        var best = -1, bestD = Self.grabSlop
        for i in s.indices {
            var d = abs(x(s[i], w) - x0)
            if s[i] == 0 { d = min(d, abs(x(Self.day, w) - x0)) }   // 00:00 is both ends
            if d <= bestD { bestD = d; best = i }
        }
        return best
    }

    // ---- the gesture: OnPress, OnDrag, OnRelease, OnClick

    private func drag(_ v: DragGesture.Value, w: CGFloat) {
        if !pressing {
            pressing = true
            moved = false
            pressX = v.startLocation.x
            pressedAt = -1
            let b = inBar(v.startLocation.y) ? boundaryNear(v.startLocation.x, w) : -1
            if b >= 0 {
                held = HeldBoundary(index: b, minute: store.s.periodStart(b))
                atRightEdge = store.s.periodStart(b) == 0 && v.startLocation.x > w / 2
            } else {
                pressedAt = Self.periodAt(starts, Int(minute(at: v.startLocation.x, w)) % Self.day)
            }
        }
        guard let h = held else {
            if abs(v.location.x - pressX) > 4 { moved = true }
            return
        }
        let m = minute(at: v.location.x, w)
        let to = store.s.clampStart(Int(m.rounded()), at: h.index)
        atRightEdge = to == 0 && m > CGFloat(Self.day / 2)
        if to != h.minute { held = HeldBoundary(index: h.index, minute: to); moved = true }
    }

    private func release() {
        pressing = false
        if let h = held {
            held = nil
            if h.minute != store.s.periodStart(h.index) { store.edit { $0.setPeriodStart(h.minute, at: h.index) } }
            return
        }
        if !moved && pressedAt >= 0 { open(pressedAt) }
        pressedAt = -1
    }

    // ---- Paint

    private func draw(_ ctx: GraphicsContext, size: CGSize) {
        let s = starts, labels = names, w = max(1, size.width), dark = scheme == .dark
        guard !s.isEmpty else { return }
        let top = Self.barTop, bottom = top + Self.barH
        let caption = Font.caption
        let hoverAt: Int = {
            guard let p = over, held == nil, inBar(p.y), boundaryNear(p.x, w) < 0 else { return -1 }
            return Self.periodAt(s, Int(minute(at: p.x, w)) % Self.day)
        }()

        // The stretches, each piece with the name where it fits (6 pt to spare either side).
        for i in s.indices {
            var ink = (focus < 0 || focus == i) ? periodColour(i, dark: dark) : periodMuted(dark: dark)
            if hoverAt == i { ink.fill = ink.fill.opacity(0.82) }
            for (a, b) in Self.pieces(s, i) {
                let gap: CGFloat = 1.5
                let r = CGRect(x: x(a, w) + (a > 0 ? gap : 0), y: top,
                               width: x(b, w) - x(a, w) - (a > 0 ? gap : 0) - (b < Self.day ? gap : 0), height: Self.barH)
                guard r.width > 0 else { continue }
                ctx.fill(Path(roundedRect: r, cornerRadius: 4), with: .color(ink.fill))
                let t = ctx.resolve(Text(labels[i]).font(caption).foregroundColor(ink.text))
                let m = t.measure(in: CGSize(width: 1000, height: 100))
                if m.width + 12 <= r.width { ctx.draw(t, at: CGPoint(x: r.midX, y: r.midY)) }
            }
        }

        // The handles, always drawn, larger under the pointer and while held; in the text
        // colour with a ring of the page's own background (periodbar.h says why).
        if s.count >= 2 {
            let nearest: Int = { guard let p = over, held == nil, inBar(p.y) else { return -1 }; return boundaryNear(p.x, w) }()
            let primary: Color = dark ? .white : .black
            for i in s.indices {
                let isHeld = held?.index == i
                let hot = isHeld || nearest == i
                var hx = x(s[i], w)
                if isHeld && atRightEdge { hx = x(Self.day, w) }
                let hw: CGFloat = hot ? 6 : 4, hh: CGFloat = hot ? Self.barH + 8 : Self.barH - 10
                let r = CGRect(x: hx - hw / 2, y: (top + bottom) / 2 - hh / 2, width: hw, height: hh)
                ctx.fill(Path(roundedRect: r.insetBy(dx: -1.5, dy: -1.5), cornerRadius: hw / 2 + 1.5),
                         with: .color(dark ? rgb(0x202020) : .white))
                ctx.fill(Path(roundedRect: r, cornerRadius: hw / 2), with: .color(primary.opacity(hot ? 0.9 : 0.5)))
            }
        }

        // The times under the boundaries, each skipped where it would run into the one
        // before it; 00:00 and 24:00 at the ends when no boundary is there already.
        var marks: [(x: CGFloat, s: String, strong: Bool)] = []
        var zero = false
        if s.count >= 2 {
            for i in s.indices {
                let isHeld = held?.index == i
                var mx = x(s[i], w)
                if isHeld && atRightEdge { mx = x(Self.day, w) }
                zero = zero || s[i] == 0
                marks.append((mx, SLSettings.clockText(s[i]), isHeld))
            }
        }
        marks.sort { $0.x < $1.x }
        if !zero { marks.insert((x(0, w), "00:00", false), at: 0); marks.append((x(Self.day, w), "24:00", false)) }
        var lastRight: CGFloat = -1e9
        for m in marks {
            let t = ctx.resolve(Text(m.s).font(caption).foregroundColor(m.strong ? .primary : .secondary))
            let tw = t.measure(in: CGSize(width: 200, height: 40)).width
            let x0 = max(0, min(w - tw, m.x - tw / 2))
            if x0 < lastRight + 6 && !m.strong { continue }
            ctx.draw(t, at: CGPoint(x: x0, y: bottom + 3), anchor: .topLeading)
            lastRight = x0 + tw
        }

        // Now: a mark above the bar, and the time beside it.
        let now = store.s.minuteNow()
        let nx = x(now, w)
        ctx.fill(Path(ellipseIn: CGRect(x: nx - 3, y: top - 8, width: 6, height: 6)), with: .color(.primary))
        let t = ctx.resolve(Text(T("现在 %s", SLSettings.clockText(now))).font(caption).foregroundColor(.secondary))
        let tw = t.measure(in: CGSize(width: 300, height: 40)).width
        ctx.draw(t, at: CGPoint(x: max(0, min(w - tw, nx - tw / 2)), y: 0), anchor: .topLeading)
    }
}
