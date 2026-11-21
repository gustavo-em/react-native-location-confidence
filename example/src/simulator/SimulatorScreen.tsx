import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { ReportPanel } from '../components/ReportPanel';
import { useTexts } from '../i18n/LanguageContext';
import { colors, fonts } from '../theme';
import { SCENARIOS } from './scenarios';
import { MapLegend, StreetMap } from './StreetMap';
import { useScenarioPlayer } from './useScenarioPlayer';

/*
 * The simulator: a made-up courier, a made-up GPS and the real library.
 *
 * Picking a situation plays it second by second. The map shows what the
 * simulation is doing; the panel under it shows what the library answers.
 *
 * Two signs at the top of the map say what the pretend phone knows at this
 * second: whether the GPS is delivering readings, and what the phone
 * reports about its own movement. Those two things are what the library
 * has to work with.
 */
export function SimulatorScreen() {
  const texts = useTexts();
  const { state, play } = useScenarioPlayer();
  const { scenario, moment } = state;

  const partPlayed =
    scenario === null ? 0 : (state.second + 1) / scenario.lengthSeconds;

  // What is written under the map: a problem, if there is one; otherwise
  // what the scenario is doing; before any scenario, what to do.
  let underTheMap: string;
  if (state.problem !== null) {
    underTheMap = state.problem;
  } else if (moment !== null) {
    underTheMap = texts.simulator.captions[moment.caption];
  } else {
    underTheMap = texts.simulator.pickASituation;
  }

  // --- The two signs on the map -------------------------------------------------
  const gpsIsSilent = moment !== null && moment.readings.length === 0;
  const gpsSign = gpsIsSilent
    ? texts.map.gpsSilentFor(state.secondsWithoutAReading)
    : texts.map.gpsDelivering;
  const movementSign =
    state.phoneActivity === null
      ? texts.map.movementNotReported
      : texts.map.phoneReports(texts.map.activities[state.phoneActivity]);

  return (
    <ScrollView
      style={styles.screen}
      contentContainerStyle={styles.content}
      showsVerticalScrollIndicator={false}
    >
      <ScrollView
        horizontal
        showsHorizontalScrollIndicator={false}
        style={styles.scenarios}
        contentContainerStyle={styles.scenariosContent}
      >
        {SCENARIOS.map((candidate) => {
          const isTheOnePlaying = candidate.id === scenario?.id;
          return (
            <Pressable
              key={candidate.id}
              accessibilityRole="button"
              accessibilityState={{ selected: isTheOnePlaying }}
              onPress={() => play(candidate)}
              style={[
                styles.scenario,
                isTheOnePlaying && styles.scenarioPicked,
              ]}
            >
              <Text
                style={[
                  styles.scenarioText,
                  isTheOnePlaying && styles.scenarioTextPicked,
                ]}
              >
                {texts.simulator.scenarioTitles[candidate.id]}
              </Text>
            </Pressable>
          );
        })}
      </ScrollView>

      <View style={styles.mapFrame}>
        <StreetMap
          sceneKey={scenario?.id ?? 'nothing'}
          metersAcross={scenario?.metersAcrossTheMap ?? 400}
          radiusMeters={scenario?.radiusMeters ?? 80}
          courier={moment?.courier ?? null}
          readings={state.readings}
          report={state.report}
        />
        {moment !== null && (
          <View style={styles.signs} pointerEvents="none">
            <View style={styles.sign}>
              <View
                style={[styles.gpsLight, gpsIsSilent && styles.gpsLightOff]}
              />
              <Text style={styles.signText}>{gpsSign}</Text>
            </View>
            <View style={styles.sign}>
              <Text style={styles.signText}>{movementSign}</Text>
            </View>
          </View>
        )}
      </View>
      <View style={styles.progressTrack}>
        <View style={[styles.progress, { width: `${partPlayed * 100}%` }]} />
      </View>

      <View style={styles.underTheMap}>
        <MapLegend />
        <Text style={styles.caption}>{underTheMap}</Text>

        <ReportPanel
          report={state.report}
          answers={state.answers}
          whenEmpty={texts.simulator.answerShowsUpHere}
        />
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: {
    flex: 1,
  },
  content: {
    paddingBottom: 24,
  },
  // The map goes from one edge of the screen to the other.
  mapFrame: {
    aspectRatio: 1.2,
  },
  signs: {
    position: 'absolute',
    top: 8,
    left: 8,
    right: 8,
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'flex-start',
    gap: 8,
  },
  sign: {
    flexDirection: 'row',
    alignItems: 'center',
    flexShrink: 1,
    paddingHorizontal: 8,
    paddingVertical: 4,
    borderWidth: 1,
    borderColor: colors.ink,
    borderRadius: 4,
    backgroundColor: colors.white,
  },
  signText: {
    flexShrink: 1,
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 13,
    lineHeight: 17,
    color: colors.ink,
  },
  gpsLight: {
    width: 9,
    height: 9,
    marginRight: 6,
    borderRadius: 5,
    backgroundColor: colors.inside,
  },
  gpsLightOff: {
    borderWidth: 2,
    borderColor: colors.alarm,
    backgroundColor: colors.white,
  },
  progressTrack: {
    height: 3,
    backgroundColor: colors.line,
  },
  progress: {
    height: 3,
    backgroundColor: colors.ink,
  },
  underTheMap: {
    paddingHorizontal: 16,
    paddingTop: 8,
  },
  caption: {
    minHeight: 60,
    marginTop: 6,
    marginBottom: 10,
    fontSize: 15,
    lineHeight: 20,
    color: colors.ink,
  },
  scenarios: {
    flexGrow: 0,
    marginBottom: 10,
  },
  scenariosContent: {
    paddingHorizontal: 16,
    gap: 8,
  },
  scenario: {
    paddingHorizontal: 12,
    paddingVertical: 8,
    borderWidth: 1,
    borderColor: colors.ink,
    borderRadius: 4,
  },
  scenarioPicked: {
    backgroundColor: colors.ink,
  },
  scenarioText: {
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 16,
    color: colors.ink,
  },
  scenarioTextPicked: {
    color: colors.white,
  },
});
