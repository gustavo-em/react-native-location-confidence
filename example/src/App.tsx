import { useState } from 'react';
import { Pressable, StatusBar, StyleSheet, Text, View } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { DeviceScreen } from './device/DeviceScreen';
import { SimulatorScreen } from './simulator/SimulatorScreen';
import { colors, fonts } from './theme';

/*
 * The example app of react-native-location-confidence.
 *
 * Two screens:
 *   - "Simulated courier": a made-up courier and a made-up GPS feed the real
 *     library, so that hard situations can be played on demand.
 *   - "This phone": the same library with the real GPS of the phone.
 *
 * Only one of them is alive at a time. Leaving the simulator turns the
 * simulation off, so the other screen always talks to the real GPS.
 */

type Screen = 'simulator' | 'device';

const SCREENS: ReadonlyArray<{ id: Screen; title: string }> = [
  { id: 'simulator', title: 'Simulated courier' },
  { id: 'device', title: 'This phone' },
];

export default function App() {
  const [screen, setScreen] = useState<Screen>('simulator');

  return (
    <SafeAreaProvider>
      <StatusBar barStyle="dark-content" />
      <SafeAreaView style={styles.app}>
        <View style={styles.header}>
          <Text style={styles.title}>Location confidence</Text>
          <View style={styles.tabs} accessibilityRole="tablist">
            {SCREENS.map((candidate) => {
              const isShown = candidate.id === screen;
              return (
                <Pressable
                  key={candidate.id}
                  accessibilityRole="tab"
                  accessibilityState={{ selected: isShown }}
                  onPress={() => setScreen(candidate.id)}
                  style={[styles.tab, isShown && styles.tabShown]}
                >
                  <Text
                    style={[styles.tabText, isShown && styles.tabTextShown]}
                  >
                    {candidate.title}
                  </Text>
                </Pressable>
              );
            })}
          </View>
        </View>

        {screen === 'simulator' ? <SimulatorScreen /> : <DeviceScreen />}
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

const styles = StyleSheet.create({
  app: {
    flex: 1,
    backgroundColor: colors.paper,
  },
  header: {
    paddingHorizontal: 16,
    paddingTop: 6,
    paddingBottom: 10,
  },
  title: {
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 26,
    color: colors.ink,
  },
  tabs: {
    flexDirection: 'row',
    marginTop: 6,
    borderBottomWidth: 1,
    borderBottomColor: colors.line,
  },
  tab: {
    marginRight: 18,
    paddingVertical: 6,
    borderBottomWidth: 2,
    borderBottomColor: 'transparent',
  },
  tabShown: {
    borderBottomColor: colors.ink,
  },
  tabText: {
    fontSize: 15,
    color: colors.inkSoft,
  },
  tabTextShown: {
    color: colors.ink,
    fontWeight: '600',
  },
});
