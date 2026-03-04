# RP2040-GNUBoy

Game Boy emulator for the Raspberry Pi Pico (RP2040).

## Display

Waveshare ResTouch Pico 2.8 LCD — **320×240** pixels, ST7789 driver.

- Key file: `sys/pico/video.cpp`
- `DISPLAY_WIDTH 320`, `DISPLAY_HEIGHT 240` defined locally in `video.cpp`
- SCALE mode enabled (`pixel_sesquialteral`, 1.5x): GB screen (160×144) → 240×216, centered at offset (40, 12)

## Controller

NES Mini Classic clone via I2C1:
- SDA: GPIO26, SCL: GPIO27
- I2C address: `0x52`
- Key file: `sys/pico/input.cpp`

### Init sequence (order matters)
1. `{0xF0, 0x55}`
2. `{0xFB, 0x00}`
3. `{0xFE, 0x03}` — required for this clone

### Read protocol
- Write `0x00` to request data
- Wait 200µs
- Read **8 bytes** (not 6 — this clone uses 8-bit axis format)

### Button mapping (active low, bytes 6 and 7)
| Button | Byte | Mask |
|--------|------|------|
| Up     | 7    | 0x01 |
| Down   | 6    | 0x40 |
| Left   | 7    | 0x02 |
| Right  | 6    | 0x80 |
| B      | 7    | 0x40 |
| A      | 7    | 0x10 |
| Select | 6    | 0x10 |
| Start  | 6    | 0x04 |

## Emulator loop

Key file: `src/emu.c`

- Frame limiter added for Pico in `emu_run()` — targets 16743µs per frame (~59.73fps, original GB rate). Without this, some games run too fast.
- Some GB games disable the LCD during pause screens, which can cause `R_LY` to stop advancing and the scanline wait loops in `emu_run()` to hang. This is a known game-specific compatibility issue.
- Nunchuck polling is rate-limited to 16ms intervals in `update_nunchuck()` to prevent over-polling when the emulator loop runs faster than 60Hz.

## Known issues

- A small number of original GB games cannot be unpaused — the game disables the LCD during its pause screen, causing the emulator loop to stall. GBC games are unaffected.

## Build

```bash
mkdir build && cd build
cmake ..
make
```

Output: `rp2040gnuboy.uf2`
