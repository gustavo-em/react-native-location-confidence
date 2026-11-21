import { useEffect, useRef, useState } from 'react';
import {
  Animated,
  Easing,
  StyleSheet,
  Text,
  View,
  type LayoutChangeEvent,
} from 'react-native';
import { Camera, Map } from '@maplibre/maplibre-react-native';
import type { ProximityReport } from 'react-native-location-confidence';
import { useTexts } from '../i18n/LanguageContext';
import { colorOfRelation, colors, fonts } from '../theme';
import type { ReadingOnTheMap } from './scenarios';
import {
  ADDRESS,
  DIRECTION_AT_THE_TOP_OF_THE_MAP_DEGREES,
  mapZoomThatShows,
  toPoint,
  type Point,
} from './world';

/*
 * The map of the simulation.
 *
 * Underneath there is a real street map: MapLibre draws it with free map
 * tiles from OpenFreeMap (data from OpenStreetMap), with no key and no
 * account. It always shows the same public place, never where the phone
 * is, and it cannot be dragged. With no internet it stays blank and
 * everything else still works.
 *
 * On top of it, plain Views show what the simulation is doing:
 *   - the delivery address and the delivery radius around it;
 *   - the courier, where the simulation really put them;
 *   - the readings the pretend GPS reported;
 *   - around the reading the library used, the area where the phone could
 *     be right now. That area is the library's answer, not a guess of this
 *     screen: it is `effectiveUncertaintyMeters` of the report.
 *
 * The map and the Views agree because both are centered on the address and
 * the zoom of the map is computed from the same "meters across".
 */

const MAP_STYLE = 'https://tiles.openfreemap.org/styles/positron';

/** The credit the map data asks for. It is the same in every language. */
const MAP_CREDIT = '© OpenStreetMap · OpenMapTiles · OpenFreeMap';

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

// The pin of the address is a square with three round corners, turned so
// that the sharp corner points down at the address.
const PIN_SIZE = 22;
const PIN_TIP_BELOW_ITS_MIDDLE = (PIN_SIZE * Math.SQRT2) / 2;

const COURIER_SIZE = 20;

// The tag that says how far the phone could be. It has a fixed width so
// that it can be kept inside the map.
const AREA_TAG_WIDTH = 172;

function keepBetween(value: number, lowest: number, highest: number): number {
  return Math.min(Math.max(value, lowest), highest);
}

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

  // The tag sits just under the area. When the area is larger than the map,
  // the tag stays at the bottom of the map instead of leaving with the edge.
  const areaTagLeft = useGlidingValue(
    keepBetween(
      leftOf(readingUsed?.x ?? 0) - AREA_TAG_WIDTH / 2,
      8,
      size.width - AREA_TAG_WIDTH - 8
    ),
    350,
    sceneKey,
    easeOut
  );
  const areaTagTop = useGlidingValue(
    keepBetween(
      topOf(readingUsed?.y ?? 0) + couldBeOffPixels + 6,
      44,
      size.height - 52
    ),
    350,
    sceneKey,
    easeOut
  );

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
  const mapIsMeasured = size.width > 0;

  return (
    <View style={styles.map} onLayout={onLayout}>
      {mapIsMeasured && (
        <>
          {/*
            The street map. It is only a picture: touches pass through, and
            a screen reader hears one sentence about it instead of the
            instructions for dragging a map.
          */}
          <View
            style={StyleSheet.absoluteFill}
            pointerEvents="none"
            accessible
            accessibilityRole="image"
            accessibilityLabel={texts.map.mapForScreenReaders}
            importantForAccessibility="no-hide-descendants"
          >
            <Map
              style={StyleSheet.absoluteFill}
              mapStyle={MAP_STYLE}
              androidView="texture"
              dragPan={false}
              touchZoom={false}
              doubleTapZoom={false}
              doubleTapHoldZoom={false}
              touchRotate={false}
              touchPitch={false}
              attribution={false}
              logo={false}
              compass={false}
            >
              <Camera
                center={[ADDRESS.longitude, ADDRESS.latitude]}
                zoom={mapZoomThatShows(metersAcross, size.width)}
                bearing={DIRECTION_AT_THE_TOP_OF_THE_MAP_DEGREES}
                duration={0}
              />
            </Map>
          </View>

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
          <Text
            style={[
              styles.radiusText,
              { left: leftOf(0) - 70, top: topOf(0) - radiusPixels - 21 },
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

          {/* The delivery address: the tip of the pin is on it. */}
          <View
            style={[
              styles.pin,
              {
                left: leftOf(0) - PIN_SIZE / 2,
                top: topOf(0) - PIN_TIP_BELOW_ITS_MIDDLE - PIN_SIZE / 2,
              },
            ]}
          >
            <View style={styles.pinHole} />
          </View>

          {readingUsed !== null && (
            <Animated.View
              style={[
                styles.areaTag,
                {
                  backgroundColor: areaColor,
                  left: areaTagLeft,
                  top: areaTagTop,
                },
              ]}
            >
              <Text style={styles.areaTagText} numberOfLines={1}>
                {texts.map.couldBeUpTo(Math.round(couldBeOffMeters))}
              </Text>
            </Animated.View>
          )}

          {courier !== null && (
            <>
              <Animated.View
                style={[
                  styles.courier,
                  {
                    left: Animated.subtract(courierLeft, COURIER_SIZE / 2),
                    top: Animated.subtract(courierTop, COURIER_SIZE / 2),
                  },
                ]}
              />
              {/* Above and to the right, where it covers the least. */}
              <Animated.View
                style={[
                  styles.courierTag,
                  {
                    left: Animated.add(courierLeft, COURIER_SIZE / 2 + 2),
                    top: Animated.subtract(courierTop, 25),
                  },
                ]}
              >
                <Text style={styles.courierTagText}>{texts.map.courier}</Text>
              </Animated.View>
            </>
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
          <Text style={styles.credit}>{MAP_CREDIT}</Text>
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
    // What shows while the street map loads, or when there is no internet.
    backgroundColor: colors.block,
    overflow: 'hidden',
  },
  areaWhereThePhoneCouldBe: {
    position: 'absolute',
    opacity: 0.28,
  },
  deliveryRadius: {
    position: 'absolute',
    borderWidth: 2,
    borderStyle: 'dashed',
    borderColor: colors.ink,
  },
  radiusText: {
    position: 'absolute',
    width: 140,
    textAlign: 'center',
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 13,
    color: colors.ink,
    // A pale edge keeps the words readable over the lines of the map.
    textShadowColor: colors.white,
    textShadowRadius: 3,
  },
  pin: {
    position: 'absolute',
    width: PIN_SIZE,
    height: PIN_SIZE,
    alignItems: 'center',
    justifyContent: 'center',
    borderTopLeftRadius: PIN_SIZE / 2,
    borderTopRightRadius: PIN_SIZE / 2,
    borderBottomLeftRadius: PIN_SIZE / 2,
    backgroundColor: colors.ink,
    transform: [{ rotate: '45deg' }],
  },
  pinHole: {
    width: 8,
    height: 8,
    borderRadius: 4,
    backgroundColor: colors.white,
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
    backgroundColor: colors.white,
  },
  wrongReading: {
    borderColor: colors.alarm,
    backgroundColor: colors.alarm,
  },
  courier: {
    position: 'absolute',
    width: COURIER_SIZE,
    height: COURIER_SIZE,
    borderRadius: COURIER_SIZE / 2,
    borderWidth: 3,
    borderColor: colors.white,
    backgroundColor: colors.ink,
    elevation: 3,
    shadowColor: colors.ink,
    shadowOpacity: 0.35,
    shadowRadius: 3,
    shadowOffset: { width: 0, height: 1 },
  },
  courierTag: {
    position: 'absolute',
    paddingHorizontal: 6,
    paddingVertical: 2,
    borderRadius: 3,
    backgroundColor: colors.ink,
  },
  areaTag: {
    position: 'absolute',
    width: AREA_TAG_WIDTH,
    alignItems: 'center',
    paddingVertical: 2,
    borderRadius: 3,
  },
  areaTagText: {
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 12,
    lineHeight: 16,
    color: colors.white,
  },
  courierTagText: {
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 12,
    lineHeight: 16,
    color: colors.white,
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
  credit: {
    position: 'absolute',
    right: 0,
    bottom: 0,
    paddingHorizontal: 5,
    paddingVertical: 1,
    fontSize: 9,
    color: colors.ink,
    backgroundColor: 'rgba(255, 255, 255, 0.8)',
  },
  legend: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    columnGap: 14,
    rowGap: 2,
  },
  legendItem: {
    flexDirection: 'row',
    alignItems: 'center',
  },
  legendMark: {
    position: 'relative',
    marginRight: 5,
  },
  // The area takes the color of the answer, so its sample has none of them.
  legendArea: {
    width: 14,
    height: 14,
    borderRadius: 7,
    backgroundColor: colors.ink,
    opacity: 0.3,
  },
  legendText: {
    fontSize: 12,
    color: colors.inkSoft,
  },
});
