import { StyleSheet, Text, View } from 'react-native';
import type { ProximityReport } from 'react-native-location-confidence';
import { colorOfRelation, colors, fonts } from '../theme';

/*
 * Shows one report of the library in words a person can read.
 *
 * Nothing here is computed about locations: every number and every label
 * comes from the report. This file only chooses the words.
 */

type Props = Readonly<{
  report: ProximityReport | null;
  /** Earlier answers, oldest first, drawn as a strip of bars. */
  answers?: readonly ProximityReport[];
  /** What to say while there is no report yet. */
  whenEmpty: string;
}>;

function whatTheEvidenceSays(report: ProximityReport): string {
  if (report.status === 'cancelled') {
    return 'Request cancelled';
  } else if (report.measuredPosition === null) {
    return 'No reading to judge';
  } else if (report.evidenceRelation === 'supports_inside') {
    return 'Evidence supports inside the radius';
  } else if (report.evidenceRelation === 'supports_outside') {
    return 'Evidence supports outside the radius';
  } else {
    return 'Evidence cannot take a side';
  }
}

/**
 * The limits the score model applied, in plain words. The most serious one
 * comes first, and only that one is shown.
 */
const LIMITS_IN_PLAIN_WORDS: ReadonlyArray<readonly [string, string]> = [
  ['mock_location', 'The system marked this reading as mock.'],
  ['suspected_jump', 'This reading looks like a GPS jump.'],
  ['approximate_permission', 'Only the approximate location is allowed.'],
  [
    'uncertainty_at_least_300m',
    'By now the phone could be 300 m or more from the reading.',
  ],
  [
    'uncertainty_at_least_100m',
    'By now the phone could be 100 m or more from the reading.',
  ],
  [
    'unknown_movement_not_fresh',
    'An old reading, and nothing tells whether the phone moved since.',
  ],
  ['cached', 'No new reading arrived: this one comes from memory.'],
  ['conflicting_evidence', 'A remembered reading contradicts the others.'],
  ['single_fix', 'A single reading: nothing confirms it yet.'],
];

/** The reasons of a report that has no reading, in plain words. */
const REASONS_IN_PLAIN_WORDS: ReadonlyArray<readonly [string, string]> = [
  ['request_cancelled', 'The app cancelled the request.'],
  ['app_backgrounded', 'The app left the screen.'],
  ['permission_not_determined', 'The location permission was never asked.'],
  ['permission_denied', 'The location permission is denied.'],
  ['permission_restricted', 'The location is blocked on this device.'],
  ['location_services_disabled', 'Location is turned off on this phone.'],
  ['play_services_unavailable', 'Google Play services are not available.'],
  ['native_provider_unavailable', 'The location service failed.'],
  ['unknown_session', 'That session is not open.'],
  ['busy', 'Too many requests are running.'],
  ['no_usable_fix', 'No usable reading arrived in time.'],
];

function whyInPlainWords(report: ProximityReport): string {
  const limits = report.scoreBreakdown?.appliedCaps ?? [];
  for (const [limit, words] of LIMITS_IN_PLAIN_WORDS) {
    if (limits.includes(limit)) {
      return words;
    }
  }
  for (const [reason, words] of REASONS_IN_PLAIN_WORDS) {
    if (report.diagnostics.reasonCodes.includes(reason)) {
      return words;
    }
  }
  if (report.measuredPosition === null) {
    return '';
  } else if (report.evidenceRelation === 'inconclusive') {
    return 'The phone could be on either side of the radius line.';
  } else {
    return 'Recent readings agree with each other.';
  }
}

function meters(value: number): string {
  return `${Math.round(value)} m`;
}

function seconds(milliseconds: number): string {
  return `${(milliseconds / 1000).toFixed(1)} s`;
}

const WHERE_THE_READING_CAME_FROM = {
  fresh: 'obtained by this request',
  recovered: 'obtained in the extra time',
  cached: 'taken from memory',
  unavailable: 'missing',
} as const;

function theFacts(report: ProximityReport): string {
  const hasAReading =
    report.distanceMeters !== null &&
    report.effectiveUncertaintyMeters !== null &&
    report.locationAgeMs !== null;
  if (!hasAReading) {
    return `Status ${report.status}.`;
  }
  return (
    `Reading ${WHERE_THE_READING_CAME_FROM[report.source]}, ` +
    `${seconds(report.locationAgeMs ?? 0)} old: ` +
    `${meters(report.distanceMeters ?? 0)} from the address, and the ` +
    `phone could be up to ` +
    `${meters(report.effectiveUncertaintyMeters ?? 0)} from it. ` +
    `Status ${report.status}.`
  );
}

/**
 * The index on a ruler from 0 to 100. The filled part is the index. When
 * the score model lowered it, a notch marks where it was before the limits.
 */
function IndexRuler({ report }: { report: ProximityReport }) {
  const index = report.qualityScore;
  const beforeLimits = report.scoreBreakdown?.rawScore ?? index;
  const wasLowered = beforeLimits > index;
  const limits = report.scoreBreakdown?.appliedCaps ?? [];

  return (
    <View
      accessible
      accessibilityLabel={`Quality index ${index} out of 100. It is not a percentage.`}
    >
      <View style={styles.indexRow}>
        <Text style={styles.index}>{index}</Text>
        <View style={styles.ruler}>
          <View style={styles.rulerTrack}>
            <View style={[styles.rulerFill, { width: `${index}%` }]} />
            {wasLowered && (
              <View style={[styles.rulerNotch, { left: `${beforeLimits}%` }]} />
            )}
          </View>
          <View style={styles.rulerTicks}>
            {[0, 25, 50, 75, 100].map((tick) => (
              <Text key={tick} style={styles.rulerTick}>
                {tick}
              </Text>
            ))}
          </View>
        </View>
      </View>
      <Text style={styles.indexCaption}>
        Evidence quality index, 0 to 100. Not a percentage.
        {wasLowered
          ? ` Was ${beforeLimits} before the limits: ${limits.join(', ')}.`
          : ''}
      </Text>
    </View>
  );
}

/** The earlier answers as bars: height is the index, color is the side. */
function AnswerStrip({ answers }: { answers: readonly ProximityReport[] }) {
  return (
    <View
      style={styles.strip}
      accessible
      accessibilityLabel={`The last ${answers.length} answers`}
    >
      {answers.map((answer) => (
        <View
          key={answer.requestId}
          style={[
            styles.stripBar,
            {
              height: 3 + (answer.qualityScore / 100) * 23,
              backgroundColor: colorOfRelation(answer.evidenceRelation),
            },
          ]}
        />
      ))}
    </View>
  );
}

export function ReportPanel({ report, answers, whenEmpty }: Props) {
  if (report === null) {
    return <Text style={styles.empty}>{whenEmpty}</Text>;
  }

  const reasons = report.diagnostics.reasonCodes;

  return (
    <View>
      <IndexRuler report={report} />
      {answers !== undefined && answers.length > 1 && (
        <AnswerStrip answers={answers} />
      )}

      <Text
        style={[
          styles.verdict,
          { color: colorOfRelation(report.evidenceRelation) },
        ]}
      >
        {whatTheEvidenceSays(report)}
      </Text>
      <Text style={styles.why}>{whyInPlainWords(report)}</Text>
      <Text style={styles.facts}>{theFacts(report)}</Text>

      <View style={styles.reasons}>
        {reasons.map((reason) => (
          <Text key={reason} style={styles.reason}>
            {reason}
          </Text>
        ))}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  empty: {
    fontSize: 15,
    lineHeight: 21,
    color: colors.inkSoft,
  },
  indexRow: {
    flexDirection: 'row',
    alignItems: 'center',
  },
  index: {
    width: 74,
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 52,
    lineHeight: 56,
    color: colors.ink,
    fontVariant: ['tabular-nums'],
  },
  ruler: {
    flex: 1,
  },
  rulerTrack: {
    height: 14,
    backgroundColor: colors.white,
    borderWidth: 1,
    borderColor: colors.ink,
  },
  rulerFill: {
    height: '100%',
    backgroundColor: colors.ink,
  },
  rulerNotch: {
    position: 'absolute',
    top: -4,
    bottom: -4,
    width: 2,
    marginLeft: -1,
    backgroundColor: colors.alarm,
  },
  rulerTicks: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: 2,
  },
  rulerTick: {
    fontFamily: fonts.sign,
    fontSize: 11,
    color: colors.inkSoft,
  },
  indexCaption: {
    fontSize: 12,
    lineHeight: 16,
    color: colors.inkSoft,
  },
  verdict: {
    marginTop: 10,
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 22,
    lineHeight: 26,
  },
  why: {
    marginTop: 2,
    fontSize: 15,
    lineHeight: 20,
    color: colors.ink,
  },
  facts: {
    marginTop: 4,
    fontSize: 13,
    lineHeight: 18,
    color: colors.inkSoft,
  },
  reasons: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 5,
    marginTop: 8,
  },
  reason: {
    paddingHorizontal: 6,
    paddingVertical: 2,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.inkSoft,
    borderRadius: 3,
    fontFamily: fonts.code,
    fontSize: 10.5,
    color: colors.ink,
  },
  strip: {
    flexDirection: 'row',
    alignItems: 'flex-end',
    gap: 2,
    height: 26,
    marginTop: 6,
  },
  stripBar: {
    flex: 1,
    maxWidth: 10,
  },
});
