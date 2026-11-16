import { useCallback, useEffect, useRef, useState } from 'react';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Switch,
  Text,
  TextInput,
  View,
} from 'react-native';
import {
  InvalidArgumentError,
  LocationConfidence,
  type Assessment,
  type Authorization,
  type Capabilities,
  type Session,
} from 'react-native-location-confidence';
import { AssessmentPanel } from '../components/AssessmentPanel';
import { ADDRESS } from '../simulator/world';
import { colors, fonts } from '../theme';

/*
 * The library with the real GPS of this phone.
 *
 * Opening this screen only reads what the phone can do and which
 * permissions exist. The GPS is turned on only by "Evaluate" (or by a
 * session that was asked to keep collecting), and the permission dialog
 * only by "Allow location".
 *
 * Coordinates are shown on this screen and nowhere else: nothing is stored
 * and nothing is sent.
 */

/** A number typed by a person: "12,5" and "12.5" are both fine. */
function numberTyped(text: string): number {
  return Number(text.trim().replace(',', '.'));
}

function Button({
  title,
  onPress,
  disabled = false,
}: {
  title: string;
  onPress: () => void;
  disabled?: boolean;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityState={{ disabled }}
      disabled={disabled}
      onPress={onPress}
      style={[styles.button, disabled && styles.buttonDisabled]}
    >
      <Text style={styles.buttonText}>{title}</Text>
    </Pressable>
  );
}

function Fact({ name, value }: { name: string; value: string }) {
  return (
    <View style={styles.fact}>
      <Text style={styles.factName}>{name}</Text>
      <Text style={styles.factValue}>{value}</Text>
    </View>
  );
}

export function DeviceScreen() {
  const [capabilities, setCapabilities] = useState<Capabilities | null>(null);
  const [authorization, setAuthorization] = useState<Authorization | null>(
    null
  );
  const [session, setSession] = useState<Session | null>(null);
  const [keepCollecting, setKeepCollecting] = useState(false);
  const [latitude, setLatitude] = useState(String(ADDRESS.latitude));
  const [longitude, setLongitude] = useState(String(ADDRESS.longitude));
  const [radius, setRadius] = useState('100');
  const [assessment, setAssessment] = useState<Assessment | null>(null);
  const [showFullReport, setShowFullReport] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const [isEvaluating, setIsEvaluating] = useState(false);

  const evaluationInProgress = useRef<AbortController | null>(null);
  const openSessionId = useRef<string | null>(null);

  const showProblem = useCallback((error: unknown) => {
    if (error instanceof InvalidArgumentError) {
      setProblem(`Check ${error.argument}: ${error.message}`);
    } else {
      setProblem(error instanceof Error ? error.message : String(error));
    }
  }, []);

  /** Reads what the phone can do. Shows no dialog and turns nothing on. */
  const readThePhone = useCallback(async () => {
    try {
      setCapabilities(await LocationConfidence.getCapabilities());
      setAuthorization(await LocationConfidence.getAuthorization());
    } catch (error) {
      showProblem(error);
    }
  }, [showProblem]);

  useEffect(() => {
    readThePhone();
    return () => {
      // Leaving the screen: stop the evaluation and close the session.
      evaluationInProgress.current?.abort();
      if (openSessionId.current !== null) {
        LocationConfidence.stopSession(openSessionId.current).catch(() => {});
      }
    };
  }, [readThePhone]);

  async function allowLocation() {
    setProblem(null);
    try {
      setAuthorization(await LocationConfidence.requestLocationPermission());
      setCapabilities(await LocationConfidence.getCapabilities());
    } catch (error) {
      showProblem(error);
    }
  }

  async function openSession() {
    setProblem(null);
    try {
      const opened = await LocationConfidence.startSession({
        collectionMode: keepCollecting
          ? 'foreground_monitoring'
          : 'evaluations_only',
      });
      openSessionId.current = opened.id;
      setSession(opened);
    } catch (error) {
      showProblem(error);
    }
  }

  async function closeSession() {
    setProblem(null);
    try {
      if (openSessionId.current !== null) {
        await LocationConfidence.stopSession(openSessionId.current);
      }
      openSessionId.current = null;
      setSession(null);
    } catch (error) {
      showProblem(error);
    }
  }

  async function evaluate() {
    setProblem(null);
    setIsEvaluating(true);
    const abort = new AbortController();
    evaluationInProgress.current = abort;
    try {
      const report = await LocationConfidence.evaluateProximity({
        sessionId: openSessionId.current ?? undefined,
        destination: {
          latitude: numberTyped(latitude),
          longitude: numberTyped(longitude),
        },
        radiusMeters: numberTyped(radius),
        signal: abort.signal,
      });
      setAssessment(report);
    } catch (error) {
      showProblem(error);
    } finally {
      evaluationInProgress.current = null;
      setIsEvaluating(false);
    }
  }

  function useTheLastReadingAsTheAddress() {
    const position = assessment?.measuredPosition;
    if (position != null) {
      setLatitude(position.latitude.toFixed(6));
      setLongitude(position.longitude.toFixed(6));
    }
  }

  const locationText =
    authorization === null
      ? 'reading...'
      : authorization.locationGranted
        ? `allowed, ${authorization.precision}`
        : authorization.location.replace('_', ' ');

  return (
    <ScrollView
      style={styles.screen}
      contentContainerStyle={styles.content}
      keyboardShouldPersistTaps="handled"
    >
      <Text style={styles.heading}>This phone</Text>
      <Fact name="Location permission" value={locationText} />
      <Fact
        name="Location switch"
        value={
          capabilities === null
            ? 'reading...'
            : capabilities.locationServicesEnabled
              ? 'on'
              : 'off'
        }
      />
      <Fact
        name="Phone activity"
        value={
          authorization === null
            ? 'reading...'
            : authorization.motion.replace('_', ' ')
        }
      />
      {authorization !== null && !authorization.locationGranted && (
        <Button title="Allow location" onPress={allowLocation} />
      )}

      <Text style={styles.heading}>Session</Text>
      <Text style={styles.explanation}>
        A session remembers the last readings, so an evaluation can still answer
        when the GPS is slow. Without one, each evaluation starts from nothing.
      </Text>
      {session === null ? (
        <>
          <View style={styles.switchRow}>
            <Text style={styles.switchText}>
              Keep collecting readings while it is open
            </Text>
            <Switch
              value={keepCollecting}
              onValueChange={setKeepCollecting}
              trackColor={{ true: colors.ink, false: colors.line }}
            />
          </View>
          <Button title="Open a session" onPress={openSession} />
        </>
      ) : (
        <>
          <Fact name="Session" value={`open, number ${session.id}`} />
          <Fact
            name="Collecting"
            value={
              session.reasonCodes.length > 0
                ? `${session.monitoring}: ${session.reasonCodes.join(', ')}`
                : session.monitoring.replace('_', ' ')
            }
          />
          <Button title="Close the session" onPress={closeSession} />
        </>
      )}

      <Text style={styles.heading}>Delivery address</Text>
      <View style={styles.inputs}>
        <View style={styles.inputWide}>
          <Text style={styles.inputName}>Latitude</Text>
          <TextInput
            style={styles.input}
            value={latitude}
            onChangeText={setLatitude}
            keyboardType="numbers-and-punctuation"
            accessibilityLabel="Latitude of the address"
          />
        </View>
        <View style={styles.inputWide}>
          <Text style={styles.inputName}>Longitude</Text>
          <TextInput
            style={styles.input}
            value={longitude}
            onChangeText={setLongitude}
            keyboardType="numbers-and-punctuation"
            accessibilityLabel="Longitude of the address"
          />
        </View>
        <View style={styles.inputNarrow}>
          <Text style={styles.inputName}>Radius, m</Text>
          <TextInput
            style={styles.input}
            value={radius}
            onChangeText={setRadius}
            keyboardType="numbers-and-punctuation"
            accessibilityLabel="Delivery radius in meters"
          />
        </View>
      </View>
      <Button
        title="Use the last reading as the address"
        onPress={useTheLastReadingAsTheAddress}
        disabled={assessment?.measuredPosition == null}
      />

      <View style={styles.actions}>
        <View style={styles.action}>
          <Button title="Evaluate" onPress={evaluate} disabled={isEvaluating} />
        </View>
        <View style={styles.action}>
          <Button
            title="Cancel"
            onPress={() => evaluationInProgress.current?.abort()}
            disabled={!isEvaluating}
          />
        </View>
      </View>

      {problem !== null && <Text style={styles.problem}>{problem}</Text>}

      <View style={styles.result}>
        <AssessmentPanel
          assessment={assessment}
          whenEmpty={
            isEvaluating
              ? 'Waiting for the GPS...'
              : 'Nothing evaluated yet. "Evaluate" turns the GPS on for a ' +
                'few seconds and reports how good the evidence is.'
          }
        />
      </View>

      {assessment !== null && (
        <>
          <Button
            title={
              showFullReport ? 'Hide the full report' : 'Show the full report'
            }
            onPress={() => setShowFullReport(!showFullReport)}
          />
          {showFullReport && (
            <Text style={styles.fullReport} selectable>
              {JSON.stringify(assessment, null, 2)}
            </Text>
          )}
        </>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: {
    flex: 1,
  },
  content: {
    paddingHorizontal: 16,
    paddingBottom: 32,
  },
  heading: {
    marginTop: 18,
    marginBottom: 6,
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 20,
    color: colors.ink,
  },
  explanation: {
    marginBottom: 8,
    fontSize: 14,
    lineHeight: 19,
    color: colors.inkSoft,
  },
  fact: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    paddingVertical: 7,
    borderBottomWidth: StyleSheet.hairlineWidth,
    borderBottomColor: colors.line,
  },
  factName: {
    fontSize: 15,
    color: colors.inkSoft,
  },
  factValue: {
    flexShrink: 1,
    marginLeft: 12,
    fontSize: 15,
    textAlign: 'right',
    color: colors.ink,
  },
  button: {
    alignItems: 'center',
    marginTop: 10,
    paddingVertical: 11,
    borderWidth: 1,
    borderColor: colors.ink,
    borderRadius: 4,
  },
  buttonDisabled: {
    opacity: 0.35,
  },
  buttonText: {
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 16,
    color: colors.ink,
  },
  switchRow: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  switchText: {
    flexShrink: 1,
    marginRight: 12,
    fontSize: 15,
    color: colors.ink,
  },
  inputs: {
    flexDirection: 'row',
    gap: 8,
  },
  inputWide: {
    flex: 3,
  },
  inputNarrow: {
    flex: 2,
  },
  inputName: {
    marginBottom: 3,
    fontSize: 12,
    color: colors.inkSoft,
  },
  input: {
    paddingHorizontal: 8,
    paddingVertical: 8,
    borderWidth: 1,
    borderColor: colors.inkSoft,
    borderRadius: 4,
    fontSize: 15,
    color: colors.ink,
    backgroundColor: colors.white,
  },
  actions: {
    flexDirection: 'row',
    gap: 8,
    marginTop: 4,
  },
  action: {
    flex: 1,
  },
  problem: {
    marginTop: 12,
    fontSize: 14,
    lineHeight: 19,
    color: colors.alarm,
  },
  result: {
    marginTop: 18,
    paddingTop: 14,
    borderTopWidth: 1,
    borderTopColor: colors.ink,
  },
  fullReport: {
    marginTop: 10,
    fontFamily: fonts.code,
    fontSize: 11,
    lineHeight: 15,
    color: colors.ink,
  },
});
