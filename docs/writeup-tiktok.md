# TikTok / Short script — "LVGL on the Pimoroni Presto, no PSRAM required"

Target ~60s, technical-explainer tone (talking over screen captures + board
shots). Captions on everything. The bug story is deliberately *not* in this
one — it's the teaser for part 2.

---

**HOOK (0–4s)**
*Shot: finger drags through the dashboard UI, tiles animating, buttery smooth.*

> "This is LVGL running at sixty frames a second on a 480 by 480 touchscreen —
> and it fits entirely in the microcontroller's own RAM. No PSRAM. Here's the
> trick."

**THE PROBLEM (4–14s)**
*Shot: Presto board in hand; on-screen text: "480×480 × RGB565 × 2 buffers = 900KB. SRAM: 520KB".*

> "The Pimoroni Presto has a gorgeous square panel, but do the maths: a
> double-buffered full-res framebuffer needs 900K, and the RP2350 only has
> 520K of SRAM. Most people reach for the external PSRAM. You don't have to."

**THE TRICK (14–28s)**
*Shot: zoom on pixel grid / side-by-side 240 vs 480 text crop; on-screen: "240×240 → hardware pixel-doubling → 480×480".*

> "Pimoroni's ST7701 driver has a half-res mode where the *hardware* pixel-
> doubles: every line goes out twice, every pixel is held for two clocks.
> Perfect 2×2 blocks — integer scaling, so text stays sharp and QR codes still
> scan. And your whole display stack drops to about 330K. Everything fits."

**THE ARCHITECTURE (28–44s)**
*Shot: simple two-core diagram; then the flush_cb code on screen with the swap line highlighted.*

> "The port is a sandwich: Pimoroni's driver owns the panel on core one — PIO
> timing, DMA, interrupts. LVGL owns core zero, rendering dirty regions into
> two little stripe buffers. The flush callback byte-swaps — the panel wants
> big-endian RGB565 — copies the stripe in, and hands finished frames to the
> driver, which races the beam so you never tear and never wait for vsync."

**TOUCH (44–54s)**
*Shot: touch.py on screen morphing into the C version; finger pressing UI.*

> "Touch? There's no C driver for the FT6236 — but the MicroPython one is the
> documentation. Sixty lines of I²C later it's a native LVGL pointer device,
> and it only polls when the interrupt line says a finger's actually there."

**PAYOFF + TEASER (54–65s)**
*Shot: full SkyFi panel — tiles, QR page swipe, hold-to-confirm land button; freeze-frame on a black screen at the very end.*

> "Result: full widget toolkit, QR codes for free, buzzer and RGB LEDs on tap —
> on a fifty quid dev board. Shout-out to DrJonEA, whose LVGL port on the
> Interstate 75 kicked this whole thing off. …Oh, and the screen stayed
> completely black for the first six hours. That story — and the driver bug I
> found — is the next video."

**CTA / caption:**
> Full technical write-up + code linked in bio. Inspired by @DrJonEA's LVGL
> work on the Interstate 75. Part 2: the PIO bug hunt. #embedded #rp2350
> #lvgl #pimoroni #raspberrypi #maker

---

## B-roll checklist
- [ ] Smooth UI scrub (dashboard tiles + swipe to QR page)
- [ ] Board in hand, back side showing the RGB LEDs
- [ ] Maths overlay (900KB vs 520KB)
- [ ] Macro/crop showing sharp 2×2 pixel doubling on text
- [ ] flush_cb code with `lv_draw_sw_rgb565_swap` highlighted
- [ ] touch.py next to the C port
- [ ] Phone scanning the QR page
- [ ] LAND NOW hold-to-confirm (nice kinetic ending)
- [ ] 1s black-screen freeze-frame for the teaser

## Blog-to-short mapping
Each section above lifts directly from a heading in `writeup-blog.md`, so the
short and the post can ship together and cross-link.
