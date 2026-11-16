# Demo recordings

Recorded from the example app (`example/`), screen "Simulated courier", on an
Android emulator running the release build.

Every coordinate in these recordings is made up. The simulator feeds made-up
GPS readings to the real library, so what the panel shows is what the engine
really answers. No real location is read.

| File | What it shows |
|---|---|
| `android-all-scenarios.mp4` | The six situations, one after the other (2 min 25 s). |
| `android-signal-lost-vs-standing-still.mp4` | Two runs side by side (27 s). Left: the GPS goes silent and nothing tells whether the phone moved. Right: the same silence, but the phone reports that it is standing still. |
| `arriving.png` | A courier rode to the address and parked. Several readings agree. |
| `gps-jump.png` | One reading landed 340 m away (the red dot). It is not used. |
| `signal-lost.png` | 12 s without a reading: the phone could be 211 m away by now. |
| `standing-still.png` | 16 s without a reading, phone standing still: 16 m. |
| `fake-gps.png` | The courier is 150 m away; a fake-GPS app reports the door. |
| `approximate-only.png` | Only the approximate location is allowed. |

## What to look at

- The big number is an index from 0 to 100 that says how good the evidence
  is. It is not a percentage and it does not approve a delivery.
- The sentence in color says what the evidence points to: inside the radius,
  outside, or it cannot take a side.
- The tinted circle on the map is where the phone could be right now,
  according to the engine. It grows while the GPS is silent.
- The red notch on the ruler marks the index before the limits were applied,
  and the caption names the limits.

## Recording it again

The videos above were made with the emulator's own recorder, which captures
about 10 frames per second. A recording made on a real phone, or with the
screen recorder of Android Studio, is smoother.

```sh
yarn example build:android     # or: yarn example android
adb emu screenrecord start --time-limit 170 --fps 30 demo.webm
# tap the situations in the app
adb emu screenrecord stop
```
