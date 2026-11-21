import { useState } from 'react';
import { Pressable, StatusBar, StyleSheet, Text, View } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { LanguageSwitch } from './components/LanguageSwitch';
import { DeviceScreen } from './device/DeviceScreen';
import { LanguageProvider, useTexts } from './i18n/LanguageContext';
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
 *
 * At the top, "English | Português" picks the language of every sentence.
 * Changing it does not restart anything: a scenario keeps playing.
 */

type Screen = 'simulator' | 'device';

const SCREENS: readonly Screen[] = ['simulator', 'device'];

export default function App() {
  return (
    <LanguageProvider>
      <SafeAreaProvider>
        <StatusBar barStyle="dark-content" />
        <HeaderAndScreens />
      </SafeAreaProvider>
    </LanguageProvider>
  );
}

function HeaderAndScreens() {
  const texts = useTexts();
  const [screen, setScreen] = useState<Screen>('simulator');

  return (
    <SafeAreaView style={styles.app}>
      <View style={styles.header}>
        <View style={styles.titleRow}>
          {/* The name of the library: the same in every language. */}
          <Text
            style={styles.title}
            numberOfLines={1}
            adjustsFontSizeToFit
            minimumFontScale={0.7}
          >
            Location confidence
          </Text>
          <LanguageSwitch />
        </View>
        <View style={styles.tabs} accessibilityRole="tablist">
          {SCREENS.map((candidate) => {
            const isShown = candidate === screen;
            return (
              <Pressable
                key={candidate}
                accessibilityRole="tab"
                accessibilityState={{ selected: isShown }}
                onPress={() => setScreen(candidate)}
                style={[styles.tab, isShown && styles.tabShown]}
              >
                <Text style={[styles.tabText, isShown && styles.tabTextShown]}>
                  {texts.tabs[candidate]}
                </Text>
              </Pressable>
            );
          })}
        </View>
      </View>

      {screen === 'simulator' ? <SimulatorScreen /> : <DeviceScreen />}
    </SafeAreaView>
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
  titleRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  title: {
    flex: 1,
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
