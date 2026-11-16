import { Platform } from 'react-native';
import type { EvidenceRelation } from 'react-native-location-confidence';

/*
 * The look of the example app: a street chart. Cool paper, navy ink, and
 * one color for each thing the evidence can say.
 */
export const colors = {
  paper: '#F3F5F1',
  ink: '#16233B',
  inkSoft: '#5A6678',
  line: '#C9D1CB',
  block: '#E1E7E1',
  street: '#FCFDFB',
  inside: '#17805A',
  inconclusive: '#A86F08',
  outside: '#3F5F99',
  alarm: '#BF3A2B',
  white: '#FFFFFF',
};

export const fonts = {
  // Condensed lettering, like the one on road signs, for numbers and titles.
  sign: Platform.select({
    ios: 'Avenir Next Condensed',
    default: 'sans-serif-condensed',
  }),
  code: Platform.select({ ios: 'Menlo', default: 'monospace' }),
};

/** The color of what the evidence says about the delivery radius. */
export function colorOfRelation(relation: EvidenceRelation): string {
  if (relation === 'supports_inside') {
    return colors.inside;
  } else if (relation === 'supports_outside') {
    return colors.outside;
  } else {
    return colors.inconclusive;
  }
}
