import { useEffect, useRef, useState } from 'react';
import {
  Animated,
  Easing,
  StyleSheet,
  Text,
  View,
  type LayoutChangeEvent,
} from 'react-native';
import type { ProximityReport } from 'react-native-location-confidence';
import { useTexts } from '../i18n/LanguageContext';
import { colorOfRelation, colors, fonts } from '../theme';
import type { ReadingOnTheMap } from './scenarios';
import { STREETS, toPoint, type Point } from './world';

/*
 * The map of the simulation, drawn with plain Views: no map service, no
 * key, no internet.
 *
 * What it shows:
 *   - the streets of a made-up neighborhood;
 *   - the delivery radius around the address;
 *   - the courier, where the simulation really put them;
 *   - the readings the pretend GPS reported;
 *   - around the reading the library used, the area where the phone could
 *     be right now. That area is the library's answer, not a guess of this
 *     screen: it is `effectiveUncertaintyMeters` of the report.
 */

type Props = Readonly<{
  /** Changes when another scenario starts: nothing glides across the map then. */
  sceneKey: string;
  metersAcross: number;
  radiusMeters: number;
  courier: Point | null;
  readings: readonly ReadingOnTheMap[];
  report: ProximityReport | null;
}>;

const SCALE_BAR_METERS = 100;

/** An Animated.Value that glides to `target` every time `target` changes. */
function useGlidingValue(
  target: number,
  durationMs: number,
  sceneKey: string,
  easing: (value: number) => number
): Animated.Value {
  const value = useRef(new Animated.Value(target)).current;
  const sceneOfTheLastValue = useRef(sceneKey);

  useEffect(() => {
    const sceneChanged = sceneOfTheLastValue.current !== sceneKey;
    sceneOfTheLastValue.current = sceneKey;
    if (sceneChanged) {
      value.stopAnimation();
      value.setValue(target);
      return;
    }

    const gliding = Animated.timing(value, {
      toValue: target,
      duration: durationMs,
      easing,
      // Positions and sizes are layout, which the native driver cannot move.
      useNativeDriver: false,
    });
    gliding.start();
    return () => gliding.stop();
  }, [durationMs, easing, sceneKey, target, value]);

  return value;
}

const easeOut = Easing.out(Easing.cubic);

export function StreetMap({
  sceneKey,
  metersAcross,
  radiusMeters,
  courier,
  readings,
  report,
}: Props) {
  const texts = useTexts();
  const [size, setSize] = useState({ width: 0, height: 0 });
  const onLayout = (event: LayoutChangeEvent) => {
    const { width, height } = event.nativeEvent.layout;
    setSize({ width, height });
  };

  // The address is in the middle of the map.
  const pixelsPerMeter = size.width / metersAcross;
  const leftOf = (x: number) => size.width / 2 + x * pixelsPerMeter;
  const topOf = (y: number) => size.height / 2 - y * pixelsPerMeter;

  // --- The library's answer -------------------------------------------------
  const readingUsed =
    report?.measuredPosition != null ? toPoint(report.measuredPosition) : null;
  const couldBeOffMeters = report?.effectiveUncertaintyMeters ?? 0;
  const gpsErrorMeters = report?.horizontalAccuracyMeters ?? 0;

  // An area far larger than the map is drawn only as large as it needs to
  // be to cover the map: the picture is the same and the View stays small.
  const largestRadiusWorthDrawing = size.width * 2;
  const couldBeOffPixels = Math.min(
    couldBeOffMeters * pixelsPerMeter,
    largestRadiusWorthDrawing
  );
  const gpsErrorPixels = Math.min(
    gpsErrorMeters * pixelsPerMeter,
    largestRadiusWorthDrawing
  );

  const areaLeft = useGlidingValue(
    leftOf(readingUsed?.x ?? 0),
    350,
    sceneKey,
    easeOut
  );
  const areaTop = useGlidingValue(
    topOf(readingUsed?.y ?? 0),
    350,
    sceneKey,
    easeOut
  );
  const areaRadius = useGlidingValue(couldBeOffPixels, 350, sceneKey, easeOut);
  const errorRadius = useGlidingValue(gpsErrorPixels, 350, sceneKey, easeOut);

  // --- The courier ------------------------------------------------------------
  const courierLeft = useGlidingValue(
    leftOf(courier?.x ?? 0),
    1000,
    sceneKey,
    Easing.linear
  );
  const courierTop = useGlidingValue(
    topOf(courier?.y ?? 0),
    1000,
    sceneKey,
    Easing.linear
  );

  const areaColor =
    report === null ? colors.ink : colorOfRelation(report.evidenceRelation);
  const radiusPixels = radiusMeters * pixelsPerMeter;
  const streetPixels = Math.max(3, STREETS.widthMeters * pixelsPerMeter);
  const mapIsMeasured = size.width > 0;

  return (
    <View style={styles.map} onLayout={onLayout}>
      {mapIsMeasured && (
        <>
          {STREETS.runningEastToWestAtY.map((y) => (
            <View
              key={`east-west-${y}`}
              style={[
                styles.streetEastToWest,
                { top: topOf(y) - streetPixels / 2, height: streetPixels },
              ]}
            />
          ))}
          {STREETS.runningNorthToSouthAtX.map((x) => (
            <View
              key={`north-south-${x}`}
              style={[
                styles.streetNorthToSouth,
                { left: leftOf(x) - streetPixels / 2, width: streetPixels },
              ]}
            />
          ))}

          {/* Where the phone could be right now, according to the library. */}
          {readingUsed !== null && (
            <Animated.View
              style={[
                styles.areaWhereThePhoneCouldBe,
                {
                  backgroundColor: areaColor,
                  left: Animated.subtract(areaLeft, areaRadius),
                  top: Animated.subtract(areaTop, areaRadius),
                  width: Animated.multiply(areaRadius, 2),
                  height: Animated.multiply(areaRadius, 2),
                  borderRadius: areaRadius,
                },
              ]}
            />
          )}

          {/* The delivery radius. */}
          <View
            style={[
              styles.deliveryRadius,
              {
                left: leftOf(0) - radiusPixels,
                top: topOf(0) - radiusPixels,
                width: radiusPixels * 2,
                height: radiusPixels * 2,
                borderRadius: radiusPixels,
              },
            ]}
          />
          <View
            style={[styles.address, { left: leftOf(0) - 5, top: topOf(0) - 5 }]}
          />
          <Text
            style={[
              styles.radiusText,
              { left: leftOf(0) - 60, top: topOf(0) - radiusPixels - 18 },
            ]}
          >
            {texts.map.deliveryRadius(radiusMeters)}
          </Text>

          {/* The GPS error of the reading the library used. */}
          {readingUsed !== null && (
            <Animated.View
              style={[
                styles.gpsError,
                {
                  borderColor: areaColor,
                  left: Animated.subtract(areaLeft, errorRadius),
                  top: Animated.subtract(areaTop, errorRadius),
                  width: Animated.multiply(errorRadius, 2),
                  height: Animated.multiply(errorRadius, 2),
                  borderRadius: errorRadius,
                },
              ]}
            />
          )}

          {/* What the pretend GPS reported: older readings fade. */}
          {readings.map((reading, position) => {
            const howRecent = (position + 1) / readings.length;
            const isWrong = reading.look === 'wrong';
            const place = {
              left: leftOf(reading.at.x) - 4,
              top: topOf(reading.at.y) - 4,
              opacity: isWrong ? 1 : 0.25 + 0.75 * howRecent,
            };
            return (
              <View
                key={`${position}-${reading.at.x}-${reading.at.y}`}
                style={[styles.reading, isWrong && styles.wrongReading, place]}
              />
            );
          })}

          {courier !== null && (
            <Animated.View
              style={[
                styles.courier,
                {
                  left: Animated.subtract(courierLeft, 8),
                  top: Animated.subtract(courierTop, 8),
                },
              ]}
            />
          )}

          <View style={styles.scale}>
            <View
              style={[
                styles.scaleBar,
                { width: SCALE_BAR_METERS * pixelsPerMeter },
              ]}
            />
            <Text style={styles.scaleText}>{SCALE_BAR_METERS} m</Text>
          </View>
        </>
      )}
    </View>
  );
}

/** What each mark on the map means. */
export function MapLegend() {
  const texts = useTexts();

  return (
    <View style={styles.legend}>
      <View style={styles.legendItem}>
        <View style={[styles.courier, styles.legendMark]} />
        <Text style={styles.legendText}>{texts.map.courier}</Text>
      </View>
      <View style={styles.legendItem}>
        <View style={[styles.reading, styles.legendMark]} />
        <Text style={styles.legendText}>{texts.map.gpsReading}</Text>
      </View>
      <View style={styles.legendItem}>
        <View style={[styles.legendArea, styles.legendMark]} />
        <Text style={styles.legendText}>{texts.map.whereThePhoneCouldBe}</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  map: {
    flex: 1,
    backgroundColor: colors.block,
    borderRadius: 6,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.line,
    overflow: 'hidden',
  },
  streetEastToWest: {
    position: 'absolute',
    left: 0,
    right: 0,
    backgroundColor: colors.street,
  },
  streetNorthToSouth: {
    position: 'absolute',
    top: 0,
    bottom: 0,
    backgroundColor: colors.street,
  },
  areaWhereThePhoneCouldBe: {
    position: 'absolute',
    opacity: 0.22,
  },
  deliveryRadius: {
    position: 'absolute',
    borderWidth: 2,
    borderStyle: 'dashed',
    borderColor: colors.ink,
  },
  radiusText: {
    position: 'absolute',
    width: 120,
    textAlign: 'center',
    fontFamily: fonts.sign,
    fontSize: 12,
    color: colors.ink,
  },
  address: {
    position: 'absolute',
    width: 10,
    height: 10,
    backgroundColor: colors.ink,
    transform: [{ rotate: '45deg' }],
  },
  gpsError: {
    position: 'absolute',
    borderWidth: 1.5,
  },
  reading: {
    position: 'absolute',
    width: 8,
    height: 8,
    borderRadius: 4,
    borderWidth: 1.5,
    borderColor: colors.ink,
    backgroundColor: colors.street,
  },
  wrongReading: {
    borderColor: colors.alarm,
    backgroundColor: colors.alarm,
  },
  courier: {
    position: 'absolute',
    width: 16,
    height: 16,
    borderRadius: 8,
    borderWidth: 3,
    borderColor: colors.white,
    backgroundColor: colors.ink,
  },
  scale: {
    position: 'absolute',
    left: 10,
    bottom: 8,
  },
  scaleBar: {
    height: 4,
    borderLeftWidth: 1.5,
    borderRightWidth: 1.5,
    borderBottomWidth: 1.5,
    borderColor: colors.ink,
  },
  scaleText: {
    marginTop: 2,
    fontFamily: fonts.sign,
    fontSize: 12,
    color: colors.ink,
  },
  legend: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    columnGap: 14,
    rowGap: 2,
    marginTop: 6,
  },
  legendItem: {
    flexDirection: 'row',
    alignItems: 'center',
  },
  legendMark: {
    position: 'relative',
    marginRight: 5,
  },
  legendArea: {
    width: 14,
    height: 14,
    borderRadius: 7,
    backgroundColor: colors.inside,
    opacity: 0.35,
  },
  legendText: {
    fontSize: 12,
    color: colors.inkSoft,
  },
});
