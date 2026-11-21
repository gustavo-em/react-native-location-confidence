import type { Coordinates } from 'react-native-location-confidence';

/*
 * The place of the simulation.
 *
 * The streets are real: Alameda Santos, one block from Avenida Paulista, in
 * São Paulo. What happens on them is made up: the courier, the GPS
 * readings, and the delivery address itself, which is only a point picked
 * on the map. No real location is read anywhere in the simulation.
 *
 * A scenario writes a position in meters from the delivery address:
 *   x grows along the street of the address, the way the courier rides it;
 *   y grows to the left of that way.
 * On the screen x goes to the right and y goes up, so the street of the
 * address lies flat across the map.
 *
 * The library works with latitude and longitude, so this file converts
 * between the two.
 */

export type Point = Readonly<{ x: number; y: number }>;

/**
 * Where the pretend delivery address is: 20 m off Alameda Santos, 100 m
 * after the corner with Alameda Ministro Rocha Azevedo.
 */
export const ADDRESS: Coordinates = {
  latitude: -23.561466,
  longitude: -46.658125,
};

/** The compass direction of x. Alameda Santos runs to the southeast. */
const DIRECTION_OF_X_DEGREES = 133.8;

/** The compass direction of y, which is also the top of the map. */
export const DIRECTION_AT_THE_TOP_OF_THE_MAP_DEGREES =
  DIRECTION_OF_X_DEGREES - 90;

const EARTH_RADIUS_METERS = 6371008.8;
const METERS_PER_DEGREE_OF_LATITUDE = (Math.PI * EARTH_RADIUS_METERS) / 180;

// Going east, one degree is shorter the farther the place is from the equator.
const METERS_PER_DEGREE_OF_LONGITUDE =
  METERS_PER_DEGREE_OF_LATITUDE * Math.cos((ADDRESS.latitude * Math.PI) / 180);

// One meter along x is this much to the east and this much to the north.
const directionOfX = (DIRECTION_OF_X_DEGREES * Math.PI) / 180;
const EAST_OF_X = Math.sin(directionOfX);
const NORTH_OF_X = Math.cos(directionOfX);

export function toCoordinates(point: Point): Coordinates {
  const metersEast = point.x * EAST_OF_X - point.y * NORTH_OF_X;
  const metersNorth = point.x * NORTH_OF_X + point.y * EAST_OF_X;
  return {
    latitude: ADDRESS.latitude + metersNorth / METERS_PER_DEGREE_OF_LATITUDE,
    longitude: ADDRESS.longitude + metersEast / METERS_PER_DEGREE_OF_LONGITUDE,
  };
}

export function toPoint(coordinates: Coordinates): Point {
  const metersEast =
    (coordinates.longitude - ADDRESS.longitude) *
    METERS_PER_DEGREE_OF_LONGITUDE;
  const metersNorth =
    (coordinates.latitude - ADDRESS.latitude) * METERS_PER_DEGREE_OF_LATITUDE;
  return {
    x: metersEast * EAST_OF_X + metersNorth * NORTH_OF_X,
    y: -metersEast * NORTH_OF_X + metersNorth * EAST_OF_X,
  };
}

/** A map draws the whole world this many points wide at zoom 0. */
const WORLD_WIDTH_AT_ZOOM_ZERO = 512;

/**
 * The zoom at which a map that is `pointsAcross` wide on the screen shows
 * `metersAcross` of this place. Each zoom step doubles the size of things.
 */
export function mapZoomThatShows(
  metersAcross: number,
  pointsAcross: number
): number {
  const metersPerPoint = metersAcross / pointsAcross;
  const metersPerPointAtZoomZero =
    (360 * METERS_PER_DEGREE_OF_LONGITUDE) / WORLD_WIDTH_AT_ZOOM_ZERO;
  return Math.log2(metersPerPointAtZoomZero / metersPerPoint);
}
