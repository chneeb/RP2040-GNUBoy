# RP2040-GNUBoy

Game Boy emulator for the Raspberry Pi Pico (RP2040) and Pico 2 (RP2350). The ROM is
compiled in (`assets/rom.c`, `rom_gb[]`) and read in place from flash; there is no SD
card support or ROM selector.

## Display

Waveshare ResTouch Pico 2.8 LCD — **320×240** pixels, ST7789 driver.

- Key file: `sys/pico/video.cpp`
- `DISPLAY_WIDTH 320`, `DISPLAY_HEIGHT 240` defined locally in `video.cpp`
- SCALE mode enabled (`pixel_sesquialteral`, 1.5x): GB screen (160×144) → 240×216, centered at offset (40, 12)
- Driver: `sys/pico/st7789.cpp`, PIO SPI on SCK 10 / MOSI 11, CS 9, DC 8, BL 13, RST 15.
  The 320×240 case sets `ROW_ORDER | COL_ORDER` in MADCTL (rotated 180° for this board).
- The touch controller (CS 16) and SD card (CS 22) share SCK/MOSI with the LCD. Both CS
  are driven high in the display init, or they take LCD traffic as commands (a confused
  touch controller stops signalling touches, which breaks uf2loader's touch-at-reset).

## Controller

NES Mini Classic clone via I2C0 at 100 kHz:
- SDA: GPIO4, SCL: GPIO5 (GPIO26/27 are taken by the Pico Audio I2S DAC; same pins as
  rp2040-ili9341-infones on the ResTouch)
- I2C address: `0x52`
- Key file: `sys/pico/input.cpp`
- A CardKB keyboard (tiny_agi, `0x5F`) can be chained on the same bus. A reset mid-transfer
  can leave a device holding SDA low, so `nunchuck_init()` first clocks SCL until SDA is
  released and sends a STOP (`i2c_bus_recover()`); all transfers time out after 10 ms.
- The GPIO buttons (pins 2, 15-21) collide with the ResTouch's LCD reset, touch and SDIO
  lines and are off unless built with `INPUT_GPIO_BUTTONS=1`.
- `-DNUNCHUCK_DEBUG=ON` (CMake option) prints the raw controller bytes over USB serial.

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
- Pressing Left on the NES Mini controller sometimes also triggers Up (parked). The byte
  decoding and key bindings were checked; the same controller works in infoNES. Next step:
  a `NUNCHUCK_DEBUG` build to look at the raw bytes.

## Build

```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico2   # or pico
make
```

Output: `rp2040gnuboy.uf2`. Builds with GCC 14.

## uf2loader

Runs under `~/Source/uf2loader` (ResTouch port): copy the UF2 to `pico2-apps/` on the SD
card. GNUBoy doesn't write to flash, so the RP2350 partition offset doesn't matter here.

---

## Planned: Dual display backend (ST7789 / PicoDVI)

Goal: switchable output between the current ST7789 LCD and HDMI via PicoDVI on a Waveshare RP2350-PiZero, without removing the ST7789 backend.

### File structure (planned)

```
sys/pico/
  video_st7789.cpp     ← current video.cpp renamed
  video_dvi.cpp        ← new PicoDVI backend
  video.hpp            ← unchanged (shared interface)
  dvi_config.h         ← new: board pin config + timing
3rd-party/
  PicoDVI/             ← git submodule (Wren6991/PicoDVI)
```

### CMake switch

Root `CMakeLists.txt`: `option(USE_DVI "Build with PicoDVI output (RP2350-PiZero)" OFF)`

DVI build also requires `-DPICO_PLATFORM=rp2350` at cmake invocation.

### PicoDVI pin config

PicoDVI already includes `waveshare_rp2040_pizero` in `common_dvi_pin_configs.h` — pin-compatible with the RP2350-PiZero (same form factor):
- TMDS: GPIO 26, 24, 22
- Clock: GPIO 28

### Scaling

GB (160×144) → **3× scale → 480×432**, centered in 640×480 with black borders (80px left/right, 24px top/bottom). Integer scale, no cropping.

### Architecture

- Core 0: GB emulator (unchanged)
- Core 1: dedicated to DVI TMDS encoding via `dvi_scanbuf_main_16bpp()` (launched from `vid_init()`)
- `vid_end()` feeds 480 scanlines into DVI queue (each GB row repeated 3×, with blank rows for margins)
- System clock: ~252 MHz (requires voltage bump via `vreg_set_voltage`)

### Open issues before implementing

1. **GPIO conflict**: the NES Mini controller has moved to I2C0 on GPIO4/5, off the DVI TMDS pins (26/24/22/28). Check GPIO4/5 against the PiZero's other uses (SD, USB host) before reusing them.
2. **Submodule location**: decide `3rd-party/PicoDVI/` vs `sys/pico/PicoDVI/`.
3. **RP2350-PiZero clock stability**: verify stable operation at ~252 MHz with voltage bump.
4. **`PICO_BOARD` value**: confirm correct board identifier for RP2350-PiZero in Pico SDK.
