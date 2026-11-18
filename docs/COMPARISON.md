# Comparison with other location libraries

`react-native-location-confidence` does not replace the location libraries below. They give your app positions: once, continuously, or in the background. This library answers one question about a position: **how good is the evidence that this phone is inside the delivery radius of this address, right now?**

If your app needs a dot on a map, a route, tracking, or location in the background, use one of them. They are mature and used by many apps. This library is version 0.1.0. So far it has run on an emulator, a simulator and one physical Android phone, indoors.

Everything about the other libraries on this page was read on 17 November 2026 from the npm registry and from each project's own README or documentation. The links are at the end. Libraries change: check their pages before deciding.

## At a glance

| Library | Latest version (published) | Downloads in a week | Position once | Continuous | Background | Geofences | Marks simulated readings |
|---|---|---|---|---|---|---|---|
| `expo-location` | 58.0.22 (Nov 2026) | 4,039,127 | yes | yes | yes | circular | yes |
| `@react-native-community/geolocation` | 3.4.0 (Sep 2024) | 290,672 | yes | yes | iOS, with configuration | no | yes |
| `react-native-geolocation-service` | 5.3.1 (Sep 2022) | 152,447 | yes | yes | not stated | no | Android only |
| `react-native-background-geolocation` | 5.8.2 (Oct 2026) | 109,346 | yes | yes | yes, its main purpose | circular and polygon | yes |
| `react-native-get-location` | 6.0.1 (Dec 2025) | 9,575 | yes | no | no | no | no |
| `react-native-location` | 2.5.0 (Aug 2019) | 3,206 | last known | yes | iOS, with an option | no | Android only |
| **`react-native-location-confidence`** | 0.1.0 (Nov 2026) | new | no: it returns a report | no | no | no | yes, and it lowers the index |

All of them are MIT licensed. `react-native-background-geolocation` also needs a paid license key for release builds; its debug builds work without one.

## When no reading arrives in time

This is where the libraries differ most for a delivery check. Indoors, in a garage or in a lobby, the GPS can take long or deliver nothing.

| Library | What your app gets |
|---|---|
| `@react-native-community/geolocation` | An error with the code `TIMEOUT`, when the `timeout` option passes. |
| `react-native-geolocation-service` | An error with the code `TIMEOUT` (3). By default `timeout` has no limit. |
| `expo-location` | `getCurrentPositionAsync` has no timeout option in its documentation. `getLastKnownPositionAsync` returns `null` when no remembered reading is recent or precise enough. |
| `react-native-background-geolocation` | `getCurrentPosition` fails with error 408 after `timeout` (60 seconds by default). |
| `react-native-get-location` | An error with the code `TIMEOUT` (60 seconds by default). |
| `react-native-location` | `getLatestLocation` returns `null` after `timeout` (10 seconds by default). |
| **`react-native-location-confidence`** | A report, never an error. It waits in steps (a first attempt, then extra time, 12 seconds in all by default). When the GPS stays silent it answers with the last reading it remembers, says how far the phone could have moved since, and lowers the index. With nothing to answer with, the report says `unavailable` and why. |

With the other libraries your app decides what an old position is still worth. That decision is the whole job of this one.

## What this library adds

- **A judgement, not a position.** `supports_inside`, `supports_outside` or `inconclusive`, an index from 0 to 100, and reason codes.
- **The age of a reading counts.** It works out how far the phone could be from the reading by now: the GPS error plus the distance the phone could have moved since.
- **The phone's activity counts** (iOS in this version). A phone that reports standing still keeps an old reading useful for much longer.
- **Readings are compared with each other.** A reading that the others contradict (a GPS jump) is not used.
- **A simulated reading is not trusted.** When the system marks a reading as simulated, the index goes to 0 by default.
- **A simulator of situations.** Your app can replace the GPS with made-up readings to demo or test a slow GPS, a jump or a fake-GPS app.

`react-native-background-geolocation` is the closest in spirit: while tracking, it filters samples with low accuracy or an impossible speed, and it rejects doubtful geofence events. That filter cleans a stream of positions. It does not answer, on demand, how good the evidence is for one address.

## What this library does not have

- No position stream for a map or a route.
- No background location and no geofences.
- No support for the old architecture of React Native, and no Expo Go.
- No reading of the phone's activity on Android yet.
- Almost no test on physical devices yet: one Android phone, indoors and standing still, and no iPhone.
- The numbers behind the index are design choices, not calibrated values.

## Which one to use

| You need | Use |
|---|---|
| The user's position on a map, once or continuously | `expo-location`, `@react-native-community/geolocation` or `react-native-geolocation-service` |
| Tracking with the app closed, or geofences | `expo-location` (background tasks) or `react-native-background-geolocation` |
| To know whether the location evidence is good enough to say "the courier is at this address", while the app is open | `react-native-location-confidence` |

They can live in the same app: one of the libraries above for the map, this one for the check at the door.

## Sources

Versions, dates and licenses: `https://registry.npmjs.org/<package>`. Downloads: `https://api.npmjs.org/downloads/point/last-week/<package>` (the week from 9 to 15 November 2026).

- `expo-location`: [documentation](https://docs.expo.dev/versions/latest/sdk/location/), [source](https://github.com/expo/expo/tree/main/packages/expo-location)
- `@react-native-community/geolocation`: [repository](https://github.com/michalchudziak/react-native-geolocation)
- `react-native-geolocation-service`: [repository](https://github.com/Agontuk/react-native-geolocation-service)
- `react-native-background-geolocation`: [repository](https://github.com/transistorsoft/react-native-background-geolocation), [documentation](https://docs.transistorsoft.com/react-native/)
- `react-native-get-location`: [repository](https://github.com/douglasjunior/react-native-get-location)
- `react-native-location`: [repository](https://github.com/timfpark/react-native-location)

"Not stated" means the project's README does not say it either way.
