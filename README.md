# fw2-moonhop

An original vector-style moon landing game for the FreeWili 2 DISPLAY CPU.
Glowing cyan lines on black, a jagged procedurally generated mountain range,
and four flat pads worth 1x, 2X, 4X and 5X (the 5X pad is the narrowest and
sits up on the peaks).

## Play

| Input | Action |
| --- | --- |
| Tilt the board left/right | Rotate the lander (BMI323 accelerometer) |
| Hold GRAY | Thrust |
| Press RED | Emergency thrust: 2.5x force, 4x fuel burn, cuts out after 2 s |
| GREEN | Start / confirm |
| NAV LEFT/RIGHT | Rotate (also the steering if no IMU is found); NAV CENTER zeroes it |
| Hold PAGE 5 s | About screen (game pauses) |
| Hold HOME 5 s | Leave the app |

Red is a burst on purpose: **holding red for 6 s powers the FreeWili 2 off**
(the power controller does that, not the app). Tap it.

On first start, hold the board the way you will play and press GREEN, then tilt
it right and press GREEN; the game learns your steering direction from that.

- **Safe landing:** both feet on a flat pad, within ±10° of upright, slow
  vertical and horizontal speed. Scores 100 × the pad multiplier, adds fuel,
  shows `LANDED!` and moves to a new terrain after 2 s.
- **Crash:** rocks, too fast or too tilted. Vector debris, lose 300 fuel,
  try again on the same terrain. Game over when fuel reaches 0.
- Flying off the left or right edge wraps to the other side.
- Sound: low-passed noise rumble for thrust (louder for emergency) and an
  explosion burst, kept well below the 0.5 W speaker limit; the speaker path
  powers down when silent. LEDs glow with thrust and flash red on a crash.

## Getting started

```
git clone --recurse-submodules https://github.com/rquinn-intrepid/fw2-moonhop.git
cd fw2-moonhop
```

Needs the Pico SDK 2.3.0 toolchain under `~/.pico-sdk`, Python 3, CMake and
Ninja (see `wilibsp/README.md`); for `tools\dev test` on Windows, MSYS2's
mingw64 GCC.

| Command | What it does |
| --- | --- |
| `tools\dev build` | Build `build/moonhop.uf2` |
| `tools\dev run` | Build, load into the board over USB and start it (gone after a reboot) |
| `tools\dev install` | Copy the UF2 to the SD card's `/apps/` so it shows in the board's app list |
| `tools\dev test` | Host unit tests for the game rules |
| `tools\dev screenshot -o shot.png` | Any other wilibsp `fw` verb works the same way |

`wilibsp/` is a submodule pinned to the BSP commit this app was tested against.
