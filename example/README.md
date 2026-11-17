# Example app

Two screens, one library.

**Simulated courier.** A made-up courier and a made-up GPS feed the real library. Pick a situation and it plays second by second:

| Situation | What happens |
|---|---|
| Arriving | A courier rides to the address and parks. |
| GPS jump | One reading lands 340 m away. |
| Signal lost | The GPS goes silent. |
| Lost, standing still | The same silence, but the phone reports that it stands still. |
| Fake GPS | The courier is far away and a fake-GPS app reports the door. |
| Approximate only | Only the approximate location is allowed. |

The map is drawn with plain Views: no map service, no key, no internet. Every coordinate is made up and the real GPS stays off.

**This phone.** The same library with the real GPS: read the permissions, ask for the location, open a session, get a report, cancel.

**English or Português.** The switch at the top changes every sentence of the app, and a situation keeps playing while you switch. The app starts in the language of the phone. Reason codes and the names of the limits (for example `single_fix`) stay as the library writes them, because that is what a developer searches for.

## Running it

From the root of the repository:

```sh
yarn
yarn example start
```

and, in another terminal:

```sh
yarn example android
```

or

```sh
cd example/ios && bundle install && bundle exec pod install && cd ../..
yarn example ios
```

## Where things are

| File | What it does |
|---|---|
| `src/simulator/scenarios.ts` | The scripts of the six situations. |
| `src/simulator/useScenarioPlayer.ts` | Plays a script against the library, one second at a time. |
| `src/simulator/StreetMap.tsx` | The map. |
| `src/components/ReportPanel.tsx` | Shows a report in plain words. |
| `src/device/DeviceScreen.tsx` | The screen for the real GPS. |
| `src/i18n/texts.ts` | Every sentence of the app, in English and in Portuguese. |
| `src/i18n/LanguageContext.tsx` | Keeps the chosen language and hands the sentences to the screens. |
| `src/components/LanguageSwitch.tsx` | The switch between English and Português. |
| `ios/LocationConfidenceExample/en.lproj`, `pt.lproj` | Tell iOS that the app speaks both languages, and translate the text of the permission dialog. Without them iOS reports English on every phone. |

Recordings of the simulator are in [`docs/demo`](../docs/demo).
