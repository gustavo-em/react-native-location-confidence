import { useEffect, useState } from 'react';
import { Platform, ScrollView, StyleSheet, Text } from 'react-native';
import { getAbiVersion, spikeEcho } from 'react-native-location-confidence';

// Phase 0.5 integration spike screen: shows that JS reaches the shared C core
// through the TurboModule on this platform and prints the echoed payload.
export default function App() {
  const [output, setOutput] = useState('running...');

  useEffect(() => {
    let active = true;

    async function run() {
      try {
        const abiVersion = await getAbiVersion();
        const echo = await spikeEcho('spike-1', {
          sessionId: null,
          radiusMeters: 100,
          enabled: true,
          mode: 'alpha',
          codes: ['first', 'second'],
        });
        if (active) {
          setOutput(JSON.stringify({ abiVersion, echo }, null, 2));
        }
      } catch (error) {
        if (active) {
          setOutput(`error: ${String(error)}`);
        }
      }
    }

    run();
    return () => {
      active = false;
    };
  }, []);

  return (
    <ScrollView contentContainerStyle={styles.container}>
      <Text style={styles.title}>Integration spike ({Platform.OS})</Text>
      <Text testID="spike-output" style={styles.output}>
        {output}
      </Text>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    paddingTop: 80,
    paddingHorizontal: 20,
    paddingBottom: 40,
  },
  title: {
    fontSize: 18,
    fontWeight: '600',
    marginBottom: 12,
  },
  output: {
    fontFamily: Platform.select({ ios: 'Menlo', default: 'monospace' }),
    fontSize: 13,
  },
});
