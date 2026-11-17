# Demo recordings

Recorded from the example app (`example/`), screen "Simulated courier",
running the release build on an Android emulator and on the iOS Simulator.

Every coordinate in these recordings is made up. The simulator feeds made-up
GPS readings to the real library, so what the panel shows is what the engine
really answers. No real location is read.

There are three folders:

| Folder | Recorded on | Language of the app |
|---|---|---|
| `android/` | Android emulator | English |
| `ios/` | iOS Simulator | English |
| `ios-pt/` | iOS Simulator | Portuguese |

Each folder has the same six situations:

| File | What it shows |
|---|---|
| `all-scenarios.mp4` | The six situations, one after the other (about 2 minutes). |
| `signal-lost-vs-standing-still.mp4` | Two runs side by side (about 25 s). Left: the GPS goes silent and nothing tells whether the phone moved. Right: the same silence, but the phone reports that it is standing still. |
| `arriving.png` | A courier rode to the address and parked. Several readings agree. |
| `gps-jump.png` | One reading landed 340 m away (the red dot). It is not used. |
| `signal-lost.png` | About 13 s without a reading: the phone could be more than 200 m away by now. |
| `standing-still.png` | More than 16 s without a reading, phone standing still: 16 m. |
| `fake-gps.png` | The courier is 150 m away; a fake-GPS app reports the door. |
| `approximate-only.png` | Only the approximate location is allowed. |

`ios/language-switch.mp4` (16 s) shows the app going from English to
Portuguese while a situation plays: the switch only changes the words, and
the situation keeps going.

The answers are the same on the two platforms and in the two languages. The
engine is the same code, and the language only chooses the words on the
screen.

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

iOS Simulator (smooth, up to 60 frames per second):

```sh
xcrun simctl io booted recordVideo --codec h264 demo.mov
# tap the situations in the app, then stop with Ctrl+C
```

Android emulator (its own recorder captures about 10 frames per second; a
real phone or the recorder of Android Studio is smoother):

```sh
adb emu screenrecord start --time-limit 170 --fps 30 demo.webm
# tap the situations in the app
adb emu screenrecord stop
```

To record in Portuguese, tap "Português" at the top of the app before
starting, or open the app already in Portuguese on the Simulator:

```sh
xcrun simctl launch booted locationconfidence.example -AppleLanguages "(pt-BR)" -AppleLocale pt_BR
```

A Simulator that was just created shows a system notice or two in its first
minutes. Wait for them to go away before recording.
