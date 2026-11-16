// Study file: TypeScript mirror of PhoneActivityTimeline.hpp and PhoneActivityTimeline.cpp.
// Same names, same order. Not part of the library. If it ever disagrees with
// the C++, the C++ is right.
// Run: node --no-warnings --experimental-transform-types cpp/src/PhoneActivityTimeline.study.ts

/*
THE BUSINESS RULE

  The phone can report its activity: "from second 10 to second 25 I was
  standing still, and I am very sure". This file takes all the reports and
  answers, for one GPS reading: since that reading was taken, how long did the
  phone spend in each activity?

  Time that no report covers is unknown. When two reports say different things
  about the same time, that time is unknown too. Only reports the phone is
  very sure about (High) are used.

THE ORDER OF THE CALLS

  calculateTimeInEachPhoneActivity(history, readingTakenAtMs, nowMs)
  │
  ├─ if no time has passed: returns the table with every line at zero
  │
  ├─ findMomentsOfChange()                                        STEP 1
  │    ├─ reportCanBeUsed()            once for each report
  │    └─ puts the moments in time order
  │
  └─ for each slice of time between two neighbor moments
       ├─ findActivityDuringSlice()                               STEP 2
       │    └─ reportCanBeUsed()       once for each report
       └─ timeInEachActivity.add()                                STEP 3

ONE CALL, STEP BY STEP

  The reading was taken at second 0 and now is second 30. Two reports:

      reading taken                                             now
           |                                                     |
           |== standing still ==|          |====== walking ======|
           0                   10         15                    30   seconds

    calculateTimeInEachPhoneActivity(history, 0, 30000)
      findMomentsOfChange()                 -> 0, 10000, 15000, 30000
      slice 0 to 10000
        findActivityDuringSlice()           -> StandingStill   (a report covers it)
        add(StandingStill, 10000)
      slice 10000 to 15000
        findActivityDuringSlice()           -> Unknown         (no report covers it)
        add(Unknown, 5000)
      slice 15000 to 30000
        findActivityDuringSlice()           -> Walking         (a report covers it)
        add(Walking, 15000)
      returns   standing still 10000,   walking 15000,   unknown 5000

WHAT THE REST OF THE ENGINE DOES WITH THE ANSWER

  calculateDistancePhoneCouldHaveMoved() in UncertaintyModel.cpp turns the
  table into meters: the speed of each activity x the seconds spent in it.

      0.5 x 10 + 3 x 15 + 45 x 5 = 5 + 45 + 225 = 275 meters

  The unknown time counts at the fastest speed (45), so every hole in the
  reports makes the distance bigger. Today only the tests call these functions.
*/

export enum PhoneActivityConfidence {
  High,
  Medium,
  Low,
  Unknown,
}

export type PhoneActivityReport = Readonly<{
  startedAtMs: number;
  endedAtMs: number;
  activity: PhoneActivity;
  confidence: PhoneActivityConfidence;
}>;

export const MAX_STORED_ACTIVITY_REPORTS = 32;

export class PhoneActivityHistory {
  private reports: PhoneActivityReport[] = [];

  store(report: PhoneActivityReport): boolean {
    const thereIsRoom = this.reports.length < MAX_STORED_ACTIVITY_REPORTS;
    if (!thereIsRoom) {
      return false;
    }
    this.reports.push(report);
    return true;
  }

  clear(): void {
    this.reports = [];
  }

  size(): number {
    return this.reports.length;
  }

  at(index: number): PhoneActivityReport {
    const report = this.reports[index];
    if (report === undefined) {
      throw new Error(`there is no report at position ${index}`);
    }
    return report;
  }
}

class ActivityDuringSlice {
  activity: PhoneActivity = PhoneActivity.Unknown;
  twoReportsDisagree: boolean = false;
}

function reportCanBeUsed(report: PhoneActivityReport): boolean {
  const phoneIsVerySure = report.confidence === PhoneActivityConfidence.High;
  const activityIsKnown = report.activity !== PhoneActivity.Unknown;
  const endsAfterItStarts = report.endedAtMs > report.startedAtMs;

  return phoneIsVerySure && activityIsKnown && endsAfterItStarts;
}

function findMomentsOfChange(
  history: PhoneActivityHistory,
  readingTakenAtMs: number,
  nowMs: number
): number[] {
  const result: number[] = [];
  result.push(readingTakenAtMs);
  result.push(nowMs);

  for (let position = 0; position < history.size(); position += 1) {
    const report = history.at(position);

    if (!reportCanBeUsed(report)) {
      continue;
    }

    const startIsBetweenReadingAndNow =
      report.startedAtMs > readingTakenAtMs && report.startedAtMs < nowMs;
    if (startIsBetweenReadingAndNow) {
      result.push(report.startedAtMs);
    }

    const endIsBetweenReadingAndNow =
      report.endedAtMs > readingTakenAtMs && report.endedAtMs < nowMs;
    if (endIsBetweenReadingAndNow) {
      result.push(report.endedAtMs);
    }
  }

  result.sort((one, other) => one - other);

  return result;
}

function findActivityDuringSlice(
  history: PhoneActivityHistory,
  sliceStartedAtMs: number,
  sliceEndedAtMs: number
): ActivityDuringSlice {
  const result = new ActivityDuringSlice();
  let someReportCoversTheSlice = false;

  for (let position = 0; position < history.size(); position += 1) {
    const report = history.at(position);

    const reportCoversTheSlice =
      reportCanBeUsed(report) &&
      report.startedAtMs <= sliceStartedAtMs &&
      report.endedAtMs >= sliceEndedAtMs;
    if (!reportCoversTheSlice) {
      continue;
    }

    if (!someReportCoversTheSlice) {
      result.activity = report.activity;
      someReportCoversTheSlice = true;
    } else if (report.activity !== result.activity) {
      result.twoReportsDisagree = true;
    }
  }

  if (result.twoReportsDisagree) {
    result.activity = PhoneActivity.Unknown;
  }

  return result;
}

export function calculateTimeInEachPhoneActivity(
  history: PhoneActivityHistory,
  readingTakenAtMs: number,
  nowMs: number
): TimeInEachPhoneActivity {
  const timeInEachActivity = new TimeInEachPhoneActivity();

  if (readingTakenAtMs < 0) {
    readingTakenAtMs = 0;
  }

  const noTimeHasPassed = nowMs <= readingTakenAtMs;
  if (noTimeHasPassed) {
    return timeInEachActivity;
  }

  // Step 1
  const momentsOfChange = findMomentsOfChange(history, readingTakenAtMs, nowMs);

  for (let position = 0; position + 1 < momentsOfChange.length; position += 1) {
    const sliceStartedAtMs = momentsOfChange[position];
    const sliceEndedAtMs = momentsOfChange[position + 1];
    if (sliceStartedAtMs === undefined || sliceEndedAtMs === undefined) {
      continue;
    }
    const millisecondsOfSlice = sliceEndedAtMs - sliceStartedAtMs;

    const sliceIsEmpty = millisecondsOfSlice === 0;
    if (sliceIsEmpty) {
      continue;
    }

    // Step 2
    const found = findActivityDuringSlice(
      history,
      sliceStartedAtMs,
      sliceEndedAtMs
    );

    // Step 3
    timeInEachActivity.add(found.activity, millisecondsOfSlice);

    if (found.twoReportsDisagree) {
      timeInEachActivity.twoReportsDisagreed = true;
    }
  }

  return timeInEachActivity;
}

// Borrowed from UncertaintyModel.hpp so this file runs alone.

export enum PhoneActivity {
  StandingStill,
  Walking,
  Running,
  Cycling,
  InVehicle,
  Unknown,
}

export class TimeInEachPhoneActivity {
  millisecondsStandingStill: number = 0;
  millisecondsWalking: number = 0;
  millisecondsRunning: number = 0;
  millisecondsCycling: number = 0;
  millisecondsInVehicle: number = 0;

  millisecondsUnknown: number = 0;

  twoReportsDisagreed: boolean = false;

  add(phoneActivity: PhoneActivity, milliseconds: number): void {
    const timeIsNegative = milliseconds < 0;
    if (timeIsNegative) {
      return;
    }

    switch (phoneActivity) {
      case PhoneActivity.StandingStill:
        this.millisecondsStandingStill += milliseconds;
        break;
      case PhoneActivity.Walking:
        this.millisecondsWalking += milliseconds;
        break;
      case PhoneActivity.Running:
        this.millisecondsRunning += milliseconds;
        break;
      case PhoneActivity.Cycling:
        this.millisecondsCycling += milliseconds;
        break;
      case PhoneActivity.InVehicle:
        this.millisecondsInVehicle += milliseconds;
        break;
      case PhoneActivity.Unknown:
        this.millisecondsUnknown += milliseconds;
        break;
    }
  }

  totalMilliseconds(): number {
    return (
      this.millisecondsStandingStill +
      this.millisecondsWalking +
      this.millisecondsRunning +
      this.millisecondsCycling +
      this.millisecondsInVehicle +
      this.millisecondsUnknown
    );
  }
}

// Examples. The expected tables are the ones the C++ tests check.

function phoneSaid(
  activity: PhoneActivity,
  fromSeconds: number,
  toSeconds: number,
  confidence: PhoneActivityConfidence = PhoneActivityConfidence.High
): PhoneActivityReport {
  return {
    startedAtMs: fromSeconds * 1000,
    endedAtMs: toSeconds * 1000,
    activity,
    confidence,
  };
}

function show(
  text: string,
  reports: readonly PhoneActivityReport[],
  secondsNow: number,
  expected: Readonly<{
    standingStill?: number;
    walking?: number;
    inVehicle?: number;
    unknown?: number;
    twoReportsDisagreed?: boolean;
  }>
): void {
  const history = new PhoneActivityHistory();
  for (const oneReport of reports) {
    history.store(oneReport);
  }

  const table = calculateTimeInEachPhoneActivity(history, 0, secondsNow * 1000);

  const upToDate =
    table.millisecondsStandingStill === (expected.standingStill ?? 0) &&
    table.millisecondsWalking === (expected.walking ?? 0) &&
    table.millisecondsInVehicle === (expected.inVehicle ?? 0) &&
    table.millisecondsUnknown === (expected.unknown ?? 0) &&
    table.twoReportsDisagreed === (expected.twoReportsDisagreed ?? false);
  if (!upToDate) {
    throw new Error(`${text}: out of date, the C++ tests expect another table`);
  }

  console.log(text);
  console.log(
    `  standing still ${table.millisecondsStandingStill}` +
      `   walking ${table.millisecondsWalking}` +
      `   in vehicle ${table.millisecondsInVehicle}` +
      `   unknown ${table.millisecondsUnknown}` +
      (table.twoReportsDisagreed ? '   two reports disagreed' : '')
  );
}

const { StandingStill, Walking, InVehicle } = PhoneActivity;

show(
  'two reports with a hole between them',
  [phoneSaid(StandingStill, 0, 10), phoneSaid(Walking, 15, 30)],
  30,
  { standingStill: 10000, walking: 15000, unknown: 5000 }
);
show('no reports at all, 5 s', [], 5, { unknown: 5000 });
show(
  '"standing still" only for the last second of a minute',
  [phoneSaid(StandingStill, 59, 60)],
  60,
  { standingStill: 1000, unknown: 59000 }
);
show(
  'one report that covers the whole 30 s',
  [phoneSaid(StandingStill, -5, 30)],
  30,
  { standingStill: 30000 }
);
show(
  'standing still for 20 s, then walking for 10 s',
  [phoneSaid(StandingStill, 0, 20), phoneSaid(Walking, 20, 30)],
  30,
  { standingStill: 20000, walking: 10000 }
);
show(
  'two reports that disagree from second 10 to second 20',
  [phoneSaid(StandingStill, 0, 30), phoneSaid(Walking, 10, 20)],
  30,
  { standingStill: 20000, unknown: 10000, twoReportsDisagreed: true }
);
show(
  'a vehicle that parked 2 s ago',
  [phoneSaid(InVehicle, 0, 28), phoneSaid(StandingStill, 28, 30)],
  30,
  { standingStill: 2000, inVehicle: 28000 }
);
show(
  'a report the phone is not very sure about',
  [phoneSaid(StandingStill, 0, 30, PhoneActivityConfidence.Medium)],
  30,
  { unknown: 30000 }
);
