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
      </View>
      <View style={styles.progressTrack}>
        <View style={[styles.progress, { width: `${partPlayed * 100}%` }]} />
      </View>
      <MapLegend />

      <Text style={styles.caption}>{underTheMap}</Text>

      <ReportPanel
        report={state.report}
        answers={state.answers}
        whenEmpty={texts.simulator.answerShowsUpHere}
      />
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: {
    flex: 1,
  },
  content: {
    paddingHorizontal: 16,
    paddingBottom: 24,
  },
  mapFrame: {
    aspectRatio: 1.35,
  },
  progressTrack: {
    height: 2,
    marginTop: 4,
    backgroundColor: colors.line,
  },
  progress: {
    height: 2,
    backgroundColor: colors.ink,
  },
  caption: {
    minHeight: 60,
    marginTop: 8,
    marginBottom: 10,
    fontSize: 15,
    lineHeight: 20,
    color: colors.ink,
  },
  scenarios: {
    flexGrow: 0,
    marginHorizontal: -16,
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
