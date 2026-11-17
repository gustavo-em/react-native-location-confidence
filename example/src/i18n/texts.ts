import type {
  AccuracyAuthorization,
  FixSource,
  PermissionStatus,
  ReportStatus,
  Session,
} from 'react-native-location-confidence';
import type { CaptionId, ScenarioId } from '../simulator/scenarios';

/*
 * Every sentence the example app shows, in English and in Portuguese.
 *
 * The screens never write a sentence themselves: they ask for it here.
 *
 *   const texts = useTexts();            // the language picked at the top
 *   <Text>{texts.device.getReport}</Text>
 *
 * To add a language, copy one of the two objects at the end of this file,
 * translate it, and add it to TEXTS and to LANGUAGES. TypeScript complains
 * about any sentence that is missing.
 *
 * Some things are never translated, because a developer searches for them
 * exactly as they are written:
 *   - the reason codes (for example "no_usable_fix");
 *   - the names of the limits (for example "single_fix");
 *   - the name of the library at the top of the screen;
 *   - the message of an error the library throws.
 */

export type Language = 'en' | 'pt';

/** Each language under its own name, in the order the switch shows them. */
export const LANGUAGES: ReadonlyArray<{ id: Language; name: string }> = [
  { id: 'en', name: 'English' },
  { id: 'pt', name: 'Português' },
];

/** The limits of the score model that the report panel explains. */
export type LimitName =
  | 'mock_location'
  | 'suspected_jump'
  | 'approximate_permission'
  | 'uncertainty_at_least_300m'
  | 'uncertainty_at_least_100m'
  | 'unknown_movement_not_fresh'
  | 'cached'
  | 'conflicting_evidence'
  | 'single_fix';

/** The reasons of a report with no reading that the report panel explains. */
export type ReasonName =
  | 'request_cancelled'
  | 'app_backgrounded'
  | 'permission_not_determined'
  | 'permission_denied'
  | 'permission_restricted'
  | 'location_services_disabled'
  | 'play_services_unavailable'
  | 'native_provider_unavailable'
  | 'unknown_session'
  | 'busy'
  | 'no_usable_fix';

export type Texts = Readonly<{
  /** What a screen reader calls the language switch. */
  language: string;

  tabs: { simulator: string; device: string };

  simulator: {
    pickASituation: string;
    answerShowsUpHere: string;
    scenarioTitles: Record<ScenarioId, string>;
    captions: Record<CaptionId, string>;
  };

  map: {
    /** For example "delivery radius, 80 m". */
    deliveryRadius: (meters: number) => string;
    courier: string;
    gpsReading: string;
    whereThePhoneCouldBe: string;
  };

  report: {
    indexCaption: string;
    /** For example "Was 97 before the limits: single_fix." */
    wasBeforeTheLimits: (before: number, limits: string) => string;
    indexForScreenReaders: (index: number) => string;
    lastAnswersForScreenReaders: (howMany: number) => string;

    requestCancelled: string;
    noReadingToJudge: string;
    supportsInside: string;
    supportsOutside: string;
    cannotTakeASide: string;

    limits: Record<LimitName, string>;
    reasons: Record<ReasonName, string>;
    readingsAgree: string;
    eitherSideOfTheLine: string;

    /** One sentence with the numbers of the reading the report used. */
    facts: (reading: {
      source: FixSource;
      ageMs: number;
      distanceMeters: number;
      couldBeOffMeters: number;
      status: ReportStatus;
    }) => string;
    /** The same sentence when the report has no reading: only the status. */
    statusAlone: (status: ReportStatus) => string;
  };

  device: {
    thisPhone: string;
    locationPermission: string;
    locationSwitch: string;
    phoneActivity: string;
    reading: string;
    /** For example "allowed, precise". */
    allowed: (precision: string) => string;
    permissionStates: Record<PermissionStatus | 'unsupported', string>;
    precisions: Record<AccuracyAuthorization, string>;
    on: string;
    off: string;
    allowLocation: string;

    session: string;
    sessionExplanation: string;
    keepCollecting: string;
    openASession: string;
    closeTheSession: string;
    /** For example "open, number 3". */
    sessionIsOpen: (id: string) => string;
    collecting: string;
    collectingStates: Record<Session['monitoring'], string>;

    deliveryAddress: string;
    latitude: string;
    longitude: string;
    radius: string;
    latitudeForScreenReaders: string;
    longitudeForScreenReaders: string;
    radiusForScreenReaders: string;
    useTheLastReading: string;

    getReport: string;
    cancel: string;
    waitingForTheGps: string;
    noReportYet: string;
    showTheFullReport: string;
    hideTheFullReport: string;
    /** For example "Check radiusMeters: it must be above zero." */
    checkArgument: (argument: string, message: string) => string;
  };
}>;

/** 1.25 becomes "1.3" in English and "1,3" in Portuguese. */
function oneDecimal(value: number, decimalMark: string): string {
  return value.toFixed(1).replace('.', decimalMark);
}

// --- English -------------------------------------------------------------------

const SOURCE_IN_ENGLISH: Record<FixSource, string> = {
  fresh: 'obtained by this request',
  recovered: 'obtained in the extra time',
  cached: 'taken from memory',
  unavailable: 'missing',
};

const english: Texts = {
  language: 'Language',

  tabs: { simulator: 'Simulated courier', device: 'This phone' },

  simulator: {
    pickASituation:
      'Pick a situation above. Every coordinate here is made up: the real ' +
      'GPS stays off and no permission is needed.',
    answerShowsUpHere: "The library's answer shows up here, once per second.",
    scenarioTitles: {
      'arriving': 'Arriving',
      'gps-jump': 'GPS jump',
      'signal-lost': 'Signal lost',
      'lost-standing-still': 'Lost, standing still',
      'fake-gps': 'Fake GPS',
      'approximate': 'Approximate only',
    },
    captions: {
      ridingToTheAddress:
        'A courier rides to the address. The GPS delivers a reading every ' +
        'second, each with an error of about 8 m.',
      atTheEdgeOfTheRadius:
        'At the edge of the delivery radius. With 8 m of error the phone ' +
        'may be on either side of the line.',
      insideAndStillMoving: 'Inside the delivery radius and still moving.',
      parkedAtTheAddress:
        'Parked in front of the address. One reading after another tells ' +
        'the same story.',
      waitingAtTheAddress: 'The courier waits in front of the address.',
      oneReadingFarAway:
        'The courier has not moved, but one reading lands 340 m away (the ' +
        'red dot). Nobody travels that far in one second.',
      oddReadingStillRemembered:
        'Good readings are back, but the odd one is still remembered and ' +
        'contradicts them. Trust stays lower for a few seconds.',
      readingsAgreeAgain: 'The readings tell one story again.',
      gpsWentSilent:
        'The GPS goes silent, as in a garage or a lobby. Nothing tells ' +
        'whether the phone moved since the last reading.',
      silenceGetsLonger:
        'The longer the silence, the farther the phone could be from its ' +
        'last reading.',
      waitingAndStandingStill:
        'The courier waits in front of the address. The phone reports that ' +
        'it is standing still (this report is simulated too).',
      silentButStandingStill:
        'The same silence, but the phone keeps reporting that it stands ' +
        'still. The last reading stays useful for much longer.',
      fakeGpsAtTheDoor:
        'The courier is 150 m away. A fake-GPS app reports readings at the ' +
        'door (the red dots), and the system marks them as mock.',
      unmarkedProvesNothing:
        'A reading that is not marked proves nothing either. This is ' +
        'evidence quality, never proof of presence.',
      blurredOnPurpose:
        'The user allowed only the approximate location. The system blurs ' +
        'every reading on purpose: about 2 km of error.',
    },
  },

  map: {
    deliveryRadius: (meters) => `delivery radius, ${meters} m`,
    courier: 'courier',
    gpsReading: 'GPS reading',
    whereThePhoneCouldBe: 'where the phone could be',
  },

  report: {
    indexCaption: 'Evidence quality index, 0 to 100. Not a percentage.',
    wasBeforeTheLimits: (before, limits) =>
      `Was ${before} before the limits: ${limits}.`,
    indexForScreenReaders: (index) =>
      `Quality index ${index} out of 100. It is not a percentage.`,
    lastAnswersForScreenReaders: (howMany) => `The last ${howMany} answers`,

    requestCancelled: 'Request cancelled',
    noReadingToJudge: 'No reading to judge',
    supportsInside: 'Evidence supports inside the radius',
    supportsOutside: 'Evidence supports outside the radius',
    cannotTakeASide: 'Evidence cannot take a side',

    limits: {
      mock_location: 'The system marked this reading as mock.',
      suspected_jump: 'This reading looks like a GPS jump.',
      approximate_permission: 'Only the approximate location is allowed.',
      uncertainty_at_least_300m:
        'By now the phone could be 300 m or more from the reading.',
      uncertainty_at_least_100m:
        'By now the phone could be 100 m or more from the reading.',
      unknown_movement_not_fresh:
        'An old reading, and nothing tells whether the phone moved since.',
      cached: 'No new reading arrived: this one comes from memory.',
      conflicting_evidence: 'A remembered reading contradicts the others.',
      single_fix: 'A single reading: nothing confirms it yet.',
    },
    reasons: {
      request_cancelled: 'The app cancelled the request.',
      app_backgrounded: 'The app left the screen.',
      permission_not_determined: 'The location permission was never asked.',
      permission_denied: 'The location permission is denied.',
      permission_restricted: 'The location is blocked on this device.',
      location_services_disabled: 'Location is turned off on this phone.',
      play_services_unavailable: 'Google Play services are not available.',
      native_provider_unavailable: 'The location service failed.',
      unknown_session: 'That session is not open.',
      busy: 'Too many requests are running.',
      no_usable_fix: 'No usable reading arrived in time.',
    },
    readingsAgree: 'Recent readings agree with each other.',
    eitherSideOfTheLine:
      'The phone could be on either side of the radius line.',

    facts: ({ source, ageMs, distanceMeters, couldBeOffMeters, status }) =>
      `Reading ${SOURCE_IN_ENGLISH[source]}, ` +
      `${oneDecimal(ageMs / 1000, '.')} s old: ` +
      `${Math.round(distanceMeters)} m from the address, and the phone ` +
      `could be up to ${Math.round(couldBeOffMeters)} m from it. ` +
      `Status ${status}.`,
    statusAlone: (status) => `Status ${status}.`,
  },

  device: {
    thisPhone: 'This phone',
    locationPermission: 'Location permission',
    locationSwitch: 'Location switch',
    phoneActivity: 'Phone activity',
    reading: 'reading...',
    allowed: (precision) => `allowed, ${precision}`,
    permissionStates: {
      not_determined: 'not determined',
      denied: 'denied',
      restricted: 'restricted',
      granted: 'allowed',
      unsupported: 'unsupported',
    },
    precisions: {
      precise: 'precise',
      approximate: 'approximate',
      unknown: 'unknown',
    },
    on: 'on',
    off: 'off',
    allowLocation: 'Allow location',

    session: 'Session',
    sessionExplanation:
      'A session remembers the last readings, so a request can still ' +
      'answer when the GPS is slow. Without one, each request starts from ' +
      'nothing.',
    keepCollecting: 'Keep collecting readings while it is open',
    openASession: 'Open a session',
    closeTheSession: 'Close the session',
    sessionIsOpen: (id) => `open, number ${id}`,
    collecting: 'Collecting',
    collectingStates: {
      not_requested: 'not requested',
      active: 'active',
      blocked: 'blocked',
    },

    deliveryAddress: 'Delivery address',
    latitude: 'Latitude',
    longitude: 'Longitude',
    radius: 'Radius, m',
    latitudeForScreenReaders: 'Latitude of the address',
    longitudeForScreenReaders: 'Longitude of the address',
    radiusForScreenReaders: 'Delivery radius in meters',
    useTheLastReading: 'Use the last reading as the address',

    getReport: 'Get report',
    cancel: 'Cancel',
    waitingForTheGps: 'Waiting for the GPS...',
    noReportYet:
      'No report yet. "Get report" turns the GPS on for a few seconds and ' +
      'reports how good the evidence is.',
    showTheFullReport: 'Show the full report',
    hideTheFullReport: 'Hide the full report',
    checkArgument: (argument, message) => `Check ${argument}: ${message}`,
  },
};

// --- Português -----------------------------------------------------------------

const SOURCE_IN_PORTUGUESE: Record<FixSource, string> = {
  fresh: 'obtida por este pedido',
  recovered: 'obtida no tempo extra',
  cached: 'tirada da memória',
  unavailable: 'ausente',
};

const STATUS_IN_PORTUGUESE: Record<ReportStatus, string> = {
  available: 'disponível',
  degraded: 'degradado',
  unavailable: 'indisponível',
  cancelled: 'cancelado',
};

const portuguese: Texts = {
  language: 'Idioma',

  tabs: { simulator: 'Entregador simulado', device: 'Este celular' },

  simulator: {
    pickASituation:
      'Escolha uma situação acima. Toda coordenada aqui é inventada: o GPS ' +
      'real fica desligado e nenhuma permissão é necessária.',
    answerShowsUpHere:
      'A resposta da biblioteca aparece aqui, uma vez por segundo.',
    scenarioTitles: {
      'arriving': 'Chegando',
      'gps-jump': 'Salto de GPS',
      'signal-lost': 'Sinal perdido',
      'lost-standing-still': 'Sem sinal, parado',
      'fake-gps': 'GPS falso',
      'approximate': 'Só aproximada',
    },
    captions: {
      ridingToTheAddress:
        'Um entregador vai de moto até o endereço. O GPS entrega uma ' +
        'leitura por segundo, cada uma com erro de uns 8 m.',
      atTheEdgeOfTheRadius:
        'Na borda do raio de entrega. Com 8 m de erro, o celular pode ' +
        'estar de um lado ou do outro da linha.',
      insideAndStillMoving: 'Dentro do raio de entrega, ainda em movimento.',
      parkedAtTheAddress:
        'Estacionado em frente ao endereço. Uma leitura depois da outra ' +
        'conta a mesma história.',
      waitingAtTheAddress: 'O entregador espera em frente ao endereço.',
      oneReadingFarAway:
        'O entregador não se moveu, mas uma leitura caiu a 340 m (o ponto ' +
        'vermelho). Ninguém anda tudo isso em um segundo.',
      oddReadingStillRemembered:
        'As leituras boas voltaram, mas a estranha ainda é lembrada e ' +
        'contradiz as outras. A confiança fica menor por alguns segundos.',
      readingsAgreeAgain: 'As leituras voltaram a contar uma história só.',
      gpsWentSilent:
        'O GPS fica mudo, como em uma garagem ou portaria. Nada informa ' +
        'se o celular se moveu desde a última leitura.',
      silenceGetsLonger:
        'Quanto mais longo o silêncio, mais longe o celular pode estar da ' +
        'última leitura.',
      waitingAndStandingStill:
        'O entregador espera em frente ao endereço. O celular informa que ' +
        'está parado (esse aviso também é simulado).',
      silentButStandingStill:
        'O mesmo silêncio, mas o celular continua informando que está ' +
        'parado. A última leitura continua útil por muito mais tempo.',
      fakeGpsAtTheDoor:
        'O entregador está a 150 m. Um app de GPS falso informa leituras ' +
        'na porta (os pontos vermelhos), e o sistema as marca como ' +
        'simuladas.',
      unmarkedProvesNothing:
        'Uma leitura sem essa marca também não prova nada. Isto mede a ' +
        'qualidade da evidência, nunca é prova de presença.',
      blurredOnPurpose:
        'O usuário permitiu só a localização aproximada. O sistema embaça ' +
        'cada leitura de propósito: uns 2 km de erro.',
    },
  },

  map: {
    deliveryRadius: (meters) => `raio de entrega, ${meters} m`,
    courier: 'entregador',
    gpsReading: 'leitura do GPS',
    whereThePhoneCouldBe: 'onde o celular pode estar',
  },

  report: {
    indexCaption:
      'Índice de qualidade da evidência, de 0 a 100. Não é porcentagem.',
    wasBeforeTheLimits: (before, limits) =>
      `Era ${before} antes dos limites: ${limits}.`,
    indexForScreenReaders: (index) =>
      `Índice de qualidade ${index} de 100. Não é porcentagem.`,
    lastAnswersForScreenReaders: (howMany) => `As últimas ${howMany} respostas`,

    requestCancelled: 'Pedido cancelado',
    noReadingToJudge: 'Nenhuma leitura para avaliar',
    supportsInside: 'Evidência aponta: dentro do raio',
    supportsOutside: 'Evidência aponta: fora do raio',
    cannotTakeASide: 'Evidência não aponta um lado',

    limits: {
      mock_location: 'O sistema marcou esta leitura como simulada.',
      suspected_jump: 'Esta leitura parece um salto de GPS.',
      approximate_permission: 'Só a localização aproximada está permitida.',
      uncertainty_at_least_300m:
        'A esta altura o celular pode estar a 300 m ou mais da leitura.',
      uncertainty_at_least_100m:
        'A esta altura o celular pode estar a 100 m ou mais da leitura.',
      unknown_movement_not_fresh:
        'Leitura antiga, e nada informa se o celular se moveu desde então.',
      cached: 'Nenhuma leitura nova chegou: esta vem da memória.',
      conflicting_evidence: 'Uma leitura lembrada contradiz as outras.',
      single_fix: 'Uma leitura só: nada a confirma ainda.',
    },
    reasons: {
      request_cancelled: 'O app cancelou o pedido.',
      app_backgrounded: 'O app saiu da tela.',
      permission_not_determined: 'A permissão de localização nunca foi pedida.',
      permission_denied: 'A permissão de localização foi negada.',
      permission_restricted: 'A localização está bloqueada neste celular.',
      location_services_disabled: 'A localização está desligada neste celular.',
      play_services_unavailable:
        'Os serviços do Google Play não estão disponíveis.',
      native_provider_unavailable: 'O serviço de localização falhou.',
      unknown_session: 'Essa sessão não está aberta.',
      busy: 'Há pedidos demais em andamento.',
      no_usable_fix: 'Nenhuma leitura utilizável chegou a tempo.',
    },
    readingsAgree: 'As leituras recentes concordam entre si.',
    eitherSideOfTheLine:
      'O celular pode estar de um lado ou do outro da linha do raio.',

    facts: ({ source, ageMs, distanceMeters, couldBeOffMeters, status }) =>
      `Leitura ${SOURCE_IN_PORTUGUESE[source]}, de ` +
      `${oneDecimal(ageMs / 1000, ',')} s atrás: a ` +
      `${Math.round(distanceMeters)} m do endereço, e o celular pode ` +
      `estar a até ${Math.round(couldBeOffMeters)} m dela. ` +
      `Estado: ${STATUS_IN_PORTUGUESE[status]}.`,
    statusAlone: (status) => `Estado: ${STATUS_IN_PORTUGUESE[status]}.`,
  },

  device: {
    thisPhone: 'Este celular',
    locationPermission: 'Permissão de localização',
    locationSwitch: 'Chave de localização',
    phoneActivity: 'Atividade do celular',
    reading: 'lendo...',
    allowed: (precision) => `permitida, ${precision}`,
    permissionStates: {
      not_determined: 'nunca pedida',
      denied: 'negada',
      restricted: 'restrita',
      granted: 'permitida',
      unsupported: 'sem suporte',
    },
    precisions: {
      precise: 'precisa',
      approximate: 'aproximada',
      unknown: 'desconhecida',
    },
    on: 'ligada',
    off: 'desligada',
    allowLocation: 'Permitir localização',

    session: 'Sessão',
    sessionExplanation:
      'Uma sessão lembra as últimas leituras, então um pedido ainda ' +
      'consegue responder quando o GPS demora. Sem sessão, cada pedido ' +
      'começa do zero.',
    keepCollecting: 'Continuar coletando leituras enquanto estiver aberta',
    openASession: 'Abrir uma sessão',
    closeTheSession: 'Fechar a sessão',
    sessionIsOpen: (id) => `aberta, número ${id}`,
    collecting: 'Coleta',
    collectingStates: {
      not_requested: 'não pedida',
      active: 'ativa',
      blocked: 'bloqueada',
    },

    deliveryAddress: 'Endereço de entrega',
    latitude: 'Latitude',
    longitude: 'Longitude',
    radius: 'Raio, m',
    latitudeForScreenReaders: 'Latitude do endereço',
    longitudeForScreenReaders: 'Longitude do endereço',
    radiusForScreenReaders: 'Raio de entrega em metros',
    useTheLastReading: 'Usar a última leitura como endereço',

    getReport: 'Pedir relatório',
    cancel: 'Cancelar',
    waitingForTheGps: 'Esperando o GPS...',
    noReportYet:
      'Nenhum relatório ainda. "Pedir relatório" liga o GPS por alguns ' +
      'segundos e informa quão boa é a evidência.',
    showTheFullReport: 'Mostrar o relatório completo',
    hideTheFullReport: 'Esconder o relatório completo',
    checkArgument: (argument, message) => `Confira ${argument}: ${message}`,
  },
};

export const TEXTS: Record<Language, Texts> = {
  en: english,
  pt: portuguese,
};
