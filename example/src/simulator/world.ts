import type { Coordinates } from 'react-native-location-confidence';

/*
 * The made-up place of the simulation.
 *
 * On the map a position is written in meters from the delivery address:
 * x grows to the east, y grows to the north. The library works with
 * latitude and longitude, so this file converts between the two.
 */

export type Point = Readonly<{ x: number; y: number }>;

/**
 * Where the pretend delivery address sits on the globe: a public square in
 * São Paulo. It is only a fixed point for the math. No real location is
 * read anywhere in the simulation.
 */
export const ADDRESS: Coordinates = {
  latitude: -23.55052,
  longitude: -46.633308,
};

const METERS_PER_DEGREE_OF_LATITUDE = (Math.PI * 6371008.8) / 180;

// Going east, one degree is shorter the farther the place is from the equator.
const METERS_PER_DEGREE_OF_LONGITUDE =
  METERS_PER_DEGREE_OF_LATITUDE * Math.cos((ADDRESS.latitude * Math.PI) / 180);

export function toCoordinates(point: Point): Coordinates {
  return {
    latitude: ADDRESS.latitude + point.y / METERS_PER_DEGREE_OF_LATITUDE,
    longitude: ADDRESS.longitude + point.x / METERS_PER_DEGREE_OF_LONGITUDE,
  };
}

export function toPoint(coordinates: Coordinates): Point {
  return {
    x:
      (coordinates.longitude - ADDRESS.longitude) *
      METERS_PER_DEGREE_OF_LONGITUDE,
    y:
      (coordinates.latitude - ADDRESS.latitude) * METERS_PER_DEGREE_OF_LATITUDE,
  };
}

/** The streets of the pretend neighborhood, by where their middle runs. */
export const STREETS = {
  widthMeters: 14,
  runningEastToWestAtY: [-280, -150, -20, 110, 240, 370],
  runningNorthToSouthAtX: [-360, -230, -100, 30, 160, 290],
};
