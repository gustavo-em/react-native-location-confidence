import { useCallback, useEffect, useRef, useState } from 'react';
import {
  LocationConfidence,
  type Assessment,
} from 'react-native-location-confidence';
import type { Moment, ReadingOnTheMap, Scenario } from './scenarios';
import { ADDRESS, toCoordinates } from './world';

/*
 * Plays a scenario, one second at a time, against the real library.
 *
 * Every second:
 *
 *   1. ask the library for an evaluation      (it waits for a reading)
 *   2. hand over what the pretend GPS reports (maybe nothing)
 *   3. show the answer when it comes
 *
 * The library runs exactly as it would with a real GPS. The only thing
 * replaced is where the readings come from.
 */

/**
 * Short waits, so the screen follows the scenario closely: an evaluation
 * waits at most 0.8 s for a reading and then answers with what it remembers.
 */
const EVALUATION_OPTIONS = {
  totalDeadlineMs: 1000,
  initialFixBudgetMs: 800,
  recoveryWindowMs: 0,
} as const;

const READINGS_SHOWN_ON_THE_MAP = 10;
const ANSWERS_KEPT_FOR_THE_STRIP = 28;

export type PlayerState = Readonly<{
  scenario: Scenario | null;
  isPlaying: boolean;
  second: number;
  moment: Moment | null;
  /** The last readings the pretend GPS reported, oldest first. */
  readings: readonly ReadingOnTheMap[];
  assessment: Assessment | null;
  /** The answers of this run, oldest first, for the strip under the index. */
  answers: readonly Assessment[];
  problem: string | null;
}>;

const NOTHING_PLAYED_YET: PlayerState = {
  scenario: null,
  isPlaying: false,
  second: 0,
  moment: null,
  readings: [],
  assessment: null,
  answers: [],
  problem: null,
};

function wait(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

export function useScenarioPlayer(): {
  state: PlayerState;
  play: (scenario: Scenario) => void;
} {
  const [state, setState] = useState<PlayerState>(NOTHING_PLAYED_YET);

  // Each run gets a number. A run stops as soon as it sees that the number
  // moved on: another scenario was picked, or the screen was closed.
  const runNumber = useRef(0);
  const sessionId = useRef<string | null>(null);
  const evaluationInProgress = useRef<AbortController | null>(null);

  const stopWhatIsRunning = useCallback(async () => {
    runNumber.current += 1;
    evaluationInProgress.current?.abort();
    evaluationInProgress.current = null;

    const sessionToClose = sessionId.current;
    sessionId.current = null;
    if (sessionToClose !== null) {
      await LocationConfidence.stopSession(sessionToClose);
    }
  }, []);

  const play = useCallback(
    (scenario: Scenario) => {
      async function run() {
        await stopWhatIsRunning();
        const thisRun = runNumber.current;
        const stillThisRun = () => runNumber.current === thisRun;

        setState({
          ...NOTHING_PLAYED_YET,
          scenario,
          isPlaying: true,
          moment: scenario.momentAt(0),
        });

        // The readings come from this app from now on: the real GPS stays
        // off and no permission is needed.
        await LocationConfidence.simulation.enable();
        await LocationConfidence.simulation.setPhoneActivity(null);
        const session = await LocationConfidence.startSession();
        if (!stillThisRun()) {
          await LocationConfidence.stopSession(session.id);
          return;
        }
        sessionId.current = session.id;

        for (let second = 0; second < scenario.lengthSeconds; second += 1) {
          const startedAt = Date.now();
          const moment = scenario.momentAt(second);

          setState((before) => ({
            ...before,
            second,
            moment,
            readings: [...before.readings, ...moment.readings].slice(
              -READINGS_SHOWN_ON_THE_MAP
            ),
          }));

          // 1. Ask. The evaluation is now waiting for a reading.
          const abort = new AbortController();
          evaluationInProgress.current = abort;
          const answer = LocationConfidence.evaluateProximity({
            sessionId: session.id,
            destination: ADDRESS,
            radiusMeters: scenario.radiusMeters,
            options: EVALUATION_OPTIONS,
            signal: abort.signal,
          });

          // 2. Report what the pretend phone and the pretend GPS say.
          if (moment.phoneActivity !== undefined) {
            await LocationConfidence.simulation.setPhoneActivity(
              moment.phoneActivity
            );
          }
          if (moment.readings.length > 0) {
            await LocationConfidence.simulation.pushReadings(
              moment.readings.map((reading) => ({
                ...toCoordinates(reading.at),
                accuracyMeters: reading.accuracyMeters,
                speedMps: reading.speedMps,
                speedAccuracyMps: reading.speedAccuracyMps,
                flaggedAsMock: reading.flaggedAsMock,
                approximate: reading.approximate,
              }))
            );
          }

          // 3. Show the answer.
          const assessment = await answer;
          if (!stillThisRun()) {
            return;
          }
          setState((before) => ({
            ...before,
            assessment,
            answers: [...before.answers, assessment].slice(
              -ANSWERS_KEPT_FOR_THE_STRIP
            ),
          }));

          const spent = Date.now() - startedAt;
          await wait(Math.max(0, 1000 - spent));
          if (!stillThisRun()) {
            return;
          }
        }

        setState((before) => ({ ...before, isPlaying: false }));
      }

      run().catch((error: unknown) => {
        setState((before) => ({
          ...before,
          isPlaying: false,
          problem: error instanceof Error ? error.message : String(error),
        }));
      });
    },
    [stopWhatIsRunning]
  );

  // Leaving the screen: stop everything and give the GPS back to the phone.
  useEffect(() => {
    return () => {
      stopWhatIsRunning()
        .then(() => LocationConfidence.simulation.disable())
        .catch(() => {
          // The screen is gone; there is nobody to tell.
        });
    };
  }, [stopWhatIsRunning]);

  return { state, play };
}
