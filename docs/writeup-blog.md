# LVGL on the Pimoroni Presto: a 480×480 touch UI that fits entirely in on-chip RAM

*Draft blog post — technical tutorial angle. Companion piece to a shorter
TikTok/video script (see writeup-tiktok.md).*

---

The [Pimoroni Presto](https://shop.pimoroni.com/products/presto) is a lovely
bit of hardware: a 480×480 IPS capacitive touchscreen, an RP2350B, WiFi, a
piezo buzzer and seven RGB LEDs on the back. Out of the box it runs
MicroPython, and for most projects that's the right answer.

I wanted more headroom. The Presto is going into a drone ground station as an
always-on control panel — live environmental readings, a WiFi-pairing QR code,
and an emergency land button — so I wanted a real widget toolkit, proper touch
handling, and every drop of performance the silicon has. That means C, the
pico-sdk, and [LVGL](https://lvgl.io).

The nudge to actually try it came from **DrJonEA**, who showed LVGL running on
Pimoroni's Interstate 75 — same RP2350 family. If it runs there, it should fly
on the Presto. It does: ~60fps, and — the part I think is worth writing up —
**the entire display stack fits in the RP2350's 520KB of on-chip SRAM**. No
PSRAM setup, no external framebuffer gymnastics.

The trick is running the panel at **240×240**.

## Why 240×240 is the sweet spot

A full 480×480 RGB565 framebuffer is 450KB. Double-buffer it — which you want,
because the panel is scanned out continuously — and you're at 900KB, well past
the 520KB of SRAM. That's why full-res setups on this board lean on the 8MB
PSRAM, with the bandwidth and cache-timing caveats that come with it.

But Pimoroni's ST7701 driver has a half-resolution mode, and it costs you
almost nothing: the scanout hardware **pixel-doubles in hardware**. Each
logical line is sent to the panel twice, and each pixel is held for two dot
clocks, so every logical pixel becomes a crisp 2×2 block on the physical
480×480 grid. Text stays sharp (it's integer scaling, not blur), QR codes
scan fine, and your memory bill drops 4×:

| Buffer                          | Size      |
|---------------------------------|-----------|
| Front buffer (LVGL composits into this) | 112.5 KB |
| Back buffer (scanout DMA target)        | 112.5 KB |
| 2 × LVGL stripe draw buffers (240×60)   | 56.3 KB  |
| LVGL heap (widgets, styles)             | 48 KB    |
| **Total UI stack**                      | **~330 KB** |

That leaves ~190KB of SRAM for your application, lwIP, and stacks. Everything
is a plain `static` array — no allocator drama, no PSRAM init, no linker
script changes.

## The architecture: Pimoroni drivers underneath, LVGL on top

The key insight is that you don't port LVGL *to the hardware* — you port it to
**Pimoroni's existing drivers**, which already know how to drive this panel's
rather exotic scanout (a PIO-generated RGB timing signal feeding an 18-bit
parallel bus, with DMA streaming lines from the framebuffer).

```
core 1                          core 0
┌─────────────────────┐         ┌──────────────────────────────┐
│ ST7701 driver        │         │ LVGL                          │
│  PIO1 timing SM      │         │  renders dirty areas into     │
│  PIO1 parallel SM    │         │  240×60 stripe buffers        │
│  2× DMA channels     │         │       │ flush_cb              │
│  scanout ISRs        │         │       ▼                       │
│        ▲             │         │  byte-swap + copy into        │
│   back buffer ◄──────┼─────────┼── front buffer, then          │
│   (DMA source)       │ update()│  presto->update(&gfx)         │
└─────────────────────┘         └──────────────────────────────┘
```

Two decisions matter:

**1. Give the display its own core.** The ST7701 scanout is fed by interrupt
handlers that must run continuously (one keeps the PIO timing state machine's
FIFO full; one advances the DMA to the next line). Mirroring what the shipping
MicroPython firmware does, `init()` runs on core 1 so those ISRs live there,
and core 1 then just idles — the interrupts do the work. Core 0 belongs to
LVGL and your application. Nothing you do in application code can starve the
panel.

**2. Let LVGL render partial, not direct.** LVGL doesn't need to own a full
framebuffer. In `LV_DISPLAY_RENDER_MODE_PARTIAL` it renders just the dirty
regions into small stripe buffers, and your flush callback copies them into
place. Two 240×60 stripes let it render the next stripe while you copy the
previous one.

## The display glue, in ~40 lines

The whole LVGL↔driver bridge is a flush callback. The only subtlety is byte
order: PicoGraphics and the ST7701 store RGB565 **byte-swapped** (big-endian),
and LVGL 9 dropped the old `LV_COLOR_16_SWAP` config — the modern way is
`lv_draw_sw_rgb565_swap()` in the flush:

```cpp
static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);

    // LVGL renders native RGB565; the panel wants it byte-swapped
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)(w * h));

    // Composite the stripe into the front buffer
    const uint16_t* src = (const uint16_t*)px_map;
    for (int32_t row = 0; row < h; row++) {
        memcpy(front + (area->y1 + row) * 240 + area->x1, src, w * 2);
        src += w;
    }

    // Last stripe of this frame? Hand the frame to the driver.
    if (lv_display_flush_is_last(disp)) {
        presto->update(&gfx);   // copies front -> back, racing the beam
    }
    lv_display_flush_ready(disp);
}
```

`presto->update()` is doing something quietly clever: it copies the front
buffer into the live scanout buffer *while chasing the beam position*, so you
get tear-free updates without ever blocking on vsync. You inherit that for
free by building on the driver instead of around it.

Setup is the standard LVGL 9 dance:

```cpp
lv_init();
lv_tick_set_cb([] { return to_ms_since_boot(get_absolute_time()); });

lv_display_t* disp = lv_display_create(240, 240);
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
lv_display_set_buffers(disp, stripe_a, stripe_b, sizeof(stripe_a),
                       LV_DISPLAY_RENDER_MODE_PARTIAL);
lv_display_set_flush_cb(disp, flush_cb);
```

## Touch: port the protocol, not the driver

The Presto's FT6236 capacitive touch controller has no C++ driver in the
Pimoroni repos — but it does have a beautifully readable MicroPython one
(`touch.py` in the presto repo), which is effectively protocol documentation:

- I²C1, SDA on GPIO 30, SCL on 31, address `0x48`
- An interrupt line on GPIO 32 that's **held low while a touch is active** —
  so you only need to hit the I²C bus when it's low (or to catch a release)
- Write register `0x00`, read 15 bytes: touch count at byte 2, then 6-byte
  records — `x = ((d[0]&0x0F)<<8)|d[1]`, `y = ((d[2]&0x0F)<<8)|d[3]`, event
  bits in the top of `d[0]`
- Coordinates arrive in 480-space: shift right once and you're in your
  240×240 logical space, matching the display exactly

That becomes a ~60-line poll function feeding a standard LVGL pointer device:

```cpp
static void indev_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    touch_poll();                       // INT-gated I2C read
    data->point.x = touch_x;            // already in 240-space
    data->point.y = touch_y;
    data->state = touch_down ? LV_INDEV_STATE_PRESSED
                             : LV_INDEV_STATE_RELEASED;
}
```

Gating on the INT line matters more than it looks: the panel polls touch from
LVGL's indev timer (~30Hz), and without the gate you'd be doing 30 pointless
I²C transactions a second forever.

## The rest of the board comes along for the ride

Because we're on the pico-sdk with Pimoroni's library ecosystem, the other
peripherals are one include away: the seven WS2812 ambient LEDs reuse
Pimoroni's `plasma` driver (on PIO0 — PIO1 belongs to the display), the piezo
is a PWM slice, the backlight is a single driver call. All of it addressable
from LVGL event callbacks — press a button, beep the buzzer, pulse the LEDs.

The main loop ends up almost insultingly simple:

```cpp
while (true) {
    uint32_t wait = lv_timer_handler();   // render + timers + touch
    peripherals_task();                   // beep timeouts, LED fades
    sleep_ms(wait > 10 ? 10 : wait);
}
```

## Results

- **~60fps** UI on a 480×480 panel, entirely from on-chip SRAM
- Full LVGL 9.2 widget set — the WiFi-pairing screen is literally the built-in
  `lv_qrcode` widget, zero extra code
- Touch, buzzer, RGB LEDs and backlight all live under one event model
- ~330KB for the whole display stack, leaving comfortable room for lwIP and a
  REST client (that's the next post)

One honest footnote: the bring-up was *not* smooth — the panel stayed black
for an evening, and the reason turned out to be genuinely interesting (and
fixable, and now upstreamed to Pimoroni). That debugging story — PIO register
forensics, a frozen core, and a one-function fix — deserves its own write-up.

*Code: [github.com/zimchaa/skyfiscreen] — Phase 1 commit has the minimal port;
later commits build the full panel. Thanks again to DrJonEA for the spark.*
