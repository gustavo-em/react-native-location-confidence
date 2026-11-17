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
  type ProximityReport,
  type Authorization,
  type Capabilities,
  type Session,
} from 'react-native-location-confidence';
import { ReportPanel } from '../components/ReportPanel';
import { useTexts } from '../i18n/LanguageContext';
import { ADDRESS } from '../simulator/world';
import { colors, fonts } from '../theme';

/*
 * The library with the real GPS of this phone.
 *
 * Opening this screen only reads what the phone can do and which
 * permissions exist. The GPS is turned on only by "Get report" (or by a
 * session that was asked to keep collecting), and the permission dialog
 * only by "Allow location".
 *
 * Coordinates are shown on this screen and nowhere else: nothing is stored
 * and nothing is sent.
 */

/**
 * What went wrong. Kept as facts, not as a finished sentence, so that it
 * can be shown in the language the app is in at the moment.
 */
type Problem = Readonly<{
  /** The argument the library refused, for example "radiusMeters". */
  argument: string | null;
  /** The library's own message. It is written for the developer, in English. */
  message: string;
}>;

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
  const texts = useTexts();
  const [capabilities, setCapabilities] = useState<Capabilities | null>(null);
  const [authorization, setAuthorization] = useState<Authorization | null>(
    null
  );
  const [session, setSession] = useState<Session | null>(null);
  const [keepCollecting, setKeepCollecting] = useState(false);
  const [latitude, setLatitude] = useState(String(ADDRESS.latitude));
  const [longitude, setLongitude] = useState(String(ADDRESS.longitude));
  const [radius, setRadius] = useState('100');
  const [report, setReport] = useState<ProximityReport | null>(null);
  const [showFullReport, setShowFullReport] = useState(false);
  const [problem, setProblem] = useState<Problem | null>(null);
  const [isWaitingForTheReport, setIsWaitingForTheReport] = useState(false);

  const requestInProgress = useRef<AbortController | null>(null);
  const openSessionId = useRef<string | null>(null);

  const showProblem = useCallback((error: unknown) => {
    if (error instanceof InvalidArgumentError) {
      setProblem({ argument: error.argument, message: error.message });
    } else {
      setProblem({
        argument: null,
        message: error instanceof Error ? error.message : String(error),
      });
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
      // Leaving the screen: stop the request and close the session.
      requestInProgress.current?.abort();
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
          : 'requests_only',
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

  async function getReport() {
    setProblem(null);
    setIsWaitingForTheReport(true);
    const abort = new AbortController();
    requestInProgress.current = abort;
    try {
      const received = await LocationConfidence.getProximityReport({
        sessionId: openSessionId.current ?? undefined,
        destination: {
          latitude: numberTyped(latitude),
          longitude: numberTyped(longitude),
        },
        radiusMeters: numberTyped(radius),
        signal: abort.signal,
      });
      setReport(received);
    } catch (error) {
      showProblem(error);
    } finally {
      requestInProgress.current = null;
      setIsWaitingForTheReport(false);
    }
  }

  function useTheLastReadingAsTheAddress() {
    const position = report?.measuredPosition;
    if (position != null) {
      setLatitude(position.latitude.toFixed(6));
      setLongitude(position.longitude.toFixed(6));
    }
  }

  // --- The words for what the phone reported -----------------------------------
  const words = texts.device;

  let locationPermissionText: string;
  if (authorization === null) {
    locationPermissionText = words.reading;
  } else if (authorization.locationGranted) {
    locationPermissionText = words.allowed(
      words.precisions[authorization.precision]
    );
  } else {
    locationPermissionText = words.permissionStates[authorization.location];
  }

  let locationSwitchText: string;
  if (capabilities === null) {
    locationSwitchText = words.reading;
  } else if (capabilities.locationServicesEnabled) {
    locationSwitchText = words.on;
  } else {
    locationSwitchText = words.off;
  }

  const phoneActivityText =
    authorization === null
      ? words.reading
      : words.permissionStates[authorization.motion];

  // When the collecting could not start, the reason codes say why. They
  // stay as the library writes them.
  let collectingText = '';
  if (session !== null) {
    collectingText = words.collectingStates[session.monitoring];
    if (session.reasonCodes.length > 0) {
      collectingText += `: ${session.reasonCodes.join(', ')}`;
    }
  }

  let problemText: string | null = null;
  if (problem !== null && problem.argument !== null) {
    problemText = words.checkArgument(problem.argument, problem.message);
  } else if (problem !== null) {
    problemText = problem.message;
  }

  return (
    <ScrollView
      style={styles.screen}
      contentContainerStyle={styles.content}
      keyboardShouldPersistTaps="handled"
    >
      <Text style={styles.heading}>{words.thisPhone}</Text>
      <Fact name={words.locationPermission} value={locationPermissionText} />
      <Fact name={words.locationSwitch} value={locationSwitchText} />
      <Fact name={words.phoneActivity} value={phoneActivityText} />
      {authorization !== null && !authorization.locationGranted && (
        <Button title={words.allowLocation} onPress={allowLocation} />
      )}

      <Text style={styles.heading}>{words.session}</Text>
      <Text style={styles.explanation}>{words.sessionExplanation}</Text>
      {session === null ? (
        <>
          <View style={styles.switchRow}>
            <Text style={styles.switchText}>{words.keepCollecting}</Text>
            <Switch
              value={keepCollecting}
              onValueChange={setKeepCollecting}
              trackColor={{ true: colors.ink, false: colors.line }}
            />
          </View>
          <Button title={words.openASession} onPress={openSession} />
        </>
      ) : (
        <>
          <Fact name={words.session} value={words.sessionIsOpen(session.id)} />
          <Fact name={words.collecting} value={collectingText} />
          <Button title={words.closeTheSession} onPress={closeSession} />
        </>
      )}

      <Text style={styles.heading}>{words.deliveryAddress}</Text>
      <View style={styles.inputs}>
        <View style={styles.inputWide}>
          <Text style={styles.inputName}>{words.latitude}</Text>
          <TextInput
            style={styles.input}
            value={latitude}
            onChangeText={setLatitude}
            keyboardType="numbers-and-punctuation"
            accessibilityLabel={words.latitudeForScreenReaders}
          />
        </View>
        <View style={styles.inputWide}>
          <Text style={styles.inputName}>{words.longitude}</Text>
          <TextInput
            style={styles.input}
            value={longitude}
            onChangeText={setLongitude}
            keyboardType="numbers-and-punctuation"
            accessibilityLabel={words.longitudeForScreenReaders}
          />
        </View>
        <View style={styles.inputNarrow}>
          <Text style={styles.inputName}>{words.radius}</Text>
          <TextInput
            style={styles.input}
            value={radius}
            onChangeText={setRadius}
            keyboardType="numbers-and-punctuation"
            accessibilityLabel={words.radiusForScreenReaders}
          />
        </View>
      </View>
      <Button
        title={words.useTheLastReading}
        onPress={useTheLastReadingAsTheAddress}
        disabled={report?.measuredPosition == null}
      />

      <View style={styles.actions}>
        <View style={styles.action}>
          <Button
            title={words.getReport}
            onPress={getReport}
            disabled={isWaitingForTheReport}
          />
        </View>
        <View style={styles.action}>
          <Button
            title={words.cancel}
            onPress={() => requestInProgress.current?.abort()}
            disabled={!isWaitingForTheReport}
          />
        </View>
      </View>

      {problemText !== null && (
        <Text style={styles.problem}>{problemText}</Text>
      )}

      <View style={styles.result}>
        <ReportPanel
          report={report}
          whenEmpty={
            isWaitingForTheReport ? words.waitingForTheGps : words.noReportYet
          }
        />
      </View>

      {report !== null && (
        <>
          <Button
            title={
              showFullReport ? words.hideTheFullReport : words.showTheFullReport
            }
            onPress={() => setShowFullReport(!showFullReport)}
          />
          {showFullReport && (
            <Text style={styles.fullReport} selectable>
              {JSON.stringify(report, null, 2)}
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
