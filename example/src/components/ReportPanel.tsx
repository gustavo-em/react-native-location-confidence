import { StyleSheet, Text, View } from 'react-native';
import type { ProximityReport } from 'react-native-location-confidence';
import { useTexts } from '../i18n/LanguageContext';
import type { LimitName, ReasonName, Texts } from '../i18n/texts';
import { colorOfRelation, colors, fonts } from '../theme';

/*
 * Shows one report of the library in words a person can read.
 *
 * Nothing here is computed about locations: every number and every label
 * comes from the report. This file only chooses which sentence to show;
 * the sentences themselves, in each language, are in i18n/texts.ts.
 */

type Props = Readonly<{
  report: ProximityReport | null;
  /** Earlier answers, oldest first, drawn as a strip of bars. */
  answers?: readonly ProximityReport[];
  /** What to say while there is no report yet. */
  whenEmpty: string;
}>;

function whatTheEvidenceSays(report: ProximityReport, texts: Texts): string {
  if (report.status === 'cancelled') {
    return texts.report.requestCancelled;
  } else if (report.measuredPosition === null) {
    return texts.report.noReadingToJudge;
  } else if (report.evidenceRelation === 'supports_inside') {
    return texts.report.supportsInside;
  } else if (report.evidenceRelation === 'supports_outside') {
    return texts.report.supportsOutside;
  } else {
    return texts.report.cannotTakeASide;
  }
}

/**
 * The limits of the score model that get a sentence. The most serious one
 * comes first, and only the first one that applied is shown.
 */
const LIMITS_BY_SERIOUSNESS: readonly LimitName[] = [
  'mock_location',
  'suspected_jump',
  'approximate_permission',
  'uncertainty_at_least_300m',
  'uncertainty_at_least_100m',
  'unknown_movement_not_fresh',
  'cached',
  'conflicting_evidence',
  'single_fix',
];

/** The reasons of a report that has no reading, in the order they are tried. */
const REASONS_WITH_NO_READING: readonly ReasonName[] = [
  'request_cancelled',
  'app_backgrounded',
  'permission_not_determined',
  'permission_denied',
  'permission_restricted',
  'location_services_disabled',
  'play_services_unavailable',
  'native_provider_unavailable',
  'unknown_session',
  'busy',
  'no_usable_fix',
];

function whyInPlainWords(report: ProximityReport, texts: Texts): string {
  const limitsApplied = report.scoreBreakdown?.appliedCaps ?? [];
  for (const limit of LIMITS_BY_SERIOUSNESS) {
    if (limitsApplied.includes(limit)) {
      return texts.report.limits[limit];
    }
  }
  for (const reason of REASONS_WITH_NO_READING) {
    if (report.diagnostics.reasonCodes.includes(reason)) {
      return texts.report.reasons[reason];
    }
  }
  if (report.measuredPosition === null) {
    return '';
  } else if (report.evidenceRelation === 'inconclusive') {
    return texts.report.eitherSideOfTheLine;
  } else {
    return texts.report.readingsAgree;
  }
}

function theFacts(report: ProximityReport, texts: Texts): string {
  const hasAReading =
    report.distanceMeters !== null &&
    report.effectiveUncertaintyMeters !== null &&
    report.locationAgeMs !== null;
  if (!hasAReading) {
    return texts.report.statusAlone(report.status);
  }
  return texts.report.facts({
    source: report.source,
    ageMs: report.locationAgeMs ?? 0,
    distanceMeters: report.distanceMeters ?? 0,
    couldBeOffMeters: report.effectiveUncertaintyMeters ?? 0,
    status: report.status,
  });
}

/**
 * The index on a ruler from 0 to 100. The filled part is the index. When
 * the score model lowered it, a notch marks where it was before the limits.
 */
function IndexRuler({ report }: { report: ProximityReport }) {
  const texts = useTexts();
  const index = report.qualityScore;
  const beforeLimits = report.scoreBreakdown?.rawScore ?? index;
  const wasLowered = beforeLimits > index;
  const limits = report.scoreBreakdown?.appliedCaps ?? [];

  // The names of the limits stay as the library writes them ("single_fix").
  const caption = wasLowered
    ? `${texts.report.indexCaption} ` +
      texts.report.wasBeforeTheLimits(beforeLimits, limits.join(', '))
    : texts.report.indexCaption;

  return (
    <View
      accessible
      accessibilityLabel={texts.report.indexForScreenReaders(index)}
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
      <Text style={styles.indexCaption}>{caption}</Text>
    </View>
  );
}

/** The earlier answers as bars: height is the index, color is the side. */
function AnswerStrip({ answers }: { answers: readonly ProximityReport[] }) {
  const texts = useTexts();

  return (
    <View
      style={styles.strip}
      accessible
      accessibilityLabel={texts.report.lastAnswersForScreenReaders(
        answers.length
      )}
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
  const texts = useTexts();

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
        {whatTheEvidenceSays(report, texts)}
      </Text>
      <Text style={styles.why}>{whyInPlainWords(report, texts)}</Text>
      <Text style={styles.facts}>{theFacts(report, texts)}</Text>

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
