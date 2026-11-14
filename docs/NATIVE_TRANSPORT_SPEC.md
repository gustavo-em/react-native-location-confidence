# NATIVE_TRANSPORT_SPEC — contratos de ponte (proposta Fase 0)

Cobre o que a SPEC chama de `CodegenCompatibilityReport.md` e `NativeTransportSpec.md`. Os trechos de código são esboços de contrato para revisão, não código de produção.

**Limite desta análise:** os tipos Codegen foram verificados lendo o parser e os geradores de `@react-native/codegen@0.88.1`, não compilando. A SPEC (§4) exige "tipos efetivamente compilados"; isso só fecha no spike (Fase 0.5).

## 1. Três fronteiras, três contratos

```text
JS  ──(1) Codegen/TurboModule──▶  Kotlin            Swift ◀── ObjC++ (.mm, classe TurboModule)
                                    │                 │
                              (2) JNI, arrays     (3) chamada C direta
                                    ▼                 ▼
                                 lce.h  (fachada C, extern "C")
                                    ▼
                                 core C++
```

## 2. Relatório de compatibilidade Codegen (0.88.1)

| Construção TS | Suporte (lido no fonte) | Tipo gerado Android / iOS | Uso proposto |
|---|---|---|---|
| `number`, `boolean`, `string` | sim | `double`/`boolean`/`String` / `double`/`BOOL`/`NSString` | campos |
| `Promise<T>` | sim | `Promise` / blocos resolve e reject | todos os métodos assíncronos |
| objeto / interface / alias | sim | `ReadableMap` (não tipado) / struct C++ tipado em argumentos, `NSDictionary` em retorno | DTOs |
| array de primitivos ou objetos | sim | `ReadableArray` / `NSArray` | `reasonCodes`, `phaseTrace` |
| `T \| null`, campo opcional | sim | boxed/nullable | campos anuláveis |
| união de literais string homogênea | sim | `String` / `NSString` | enums públicos |
| `enum` TS (string ou numérico, sem mistura) | sim | `String` ou `double` | alternativa às uniões |
| união mista (string \| number, objeto \| string) | não | erro de parser/gerador | evitar |
| tipo literal `null` isolado, literal numérico como tipo de campo | não usar | — | `probabilityWithinRadius` e `schemaVersion` são acrescentados pela fachada TS |
| `AbortSignal`, funções, classes | não | — | `cancelRequest(requestId)` |

Consequência prática: no Android todo objeto chega e sai como mapa não tipado, com ou sem spec rica. A tipagem real da ponte é responsabilidade do `mapper.ts`, que valida a forma do retorno.

## 3. Spec Codegen mínimo (esboço)

```ts
export interface Spec extends TurboModule {
  getCapabilities(): Promise<NativeCapabilities>;
  getAuthorization(): Promise<NativeAuthorization>;
  requestLocationPermission(): Promise<NativeAuthorization>;
  requestMotionPermission(): Promise<NativeAuthorization>;
  startSession(options: NativeSessionOptions): Promise<NativeSessionResult>;
  clearHistory(sessionId: string): Promise<void>;
  stopSession(sessionId: string): Promise<void>;
  evaluate(requestId: string, request: NativeEvaluateRequest): Promise<NativeAssessment>;
  cancelRequest(requestId: string): void;
}
```

Regras:

- A fachada TS aplica defaults e validação; o nativo recebe todos os campos preenchidos (sem opcionais em `NativeEvaluateRequest` além de `sessionId: string | null`).
- `requestId` é gerado no JS antes da chamada, para que `AbortSignal` possa cancelar uma request ainda pendente.
- `NativeAssessment` espelha `Assessment` sem `schemaVersion`, `scoreModelVersion` literal e `probabilityWithinRadius`; inclui `transportVersion: number` para o mapper detectar descompasso entre JS e binário.
- Reason codes viajam como strings produzidas pelo core (`lce_reason_code_name`), fonte única para as duas plataformas.

Tipos hoje indefinidos na SPEC (proposta):

```ts
type NativeAuthorization = {
  location: 'not_determined' | 'denied' | 'restricted' | 'granted';
  precision: 'precise' | 'approximate' | 'unknown';
  motion: 'not_determined' | 'denied' | 'restricted' | 'granted' | 'unsupported';
};
type NativeCapabilities = {
  locationServicesEnabled: boolean;
  locationProviderAvailable: boolean;   // Play Services no Android
  motionSupported: boolean;
  mockDetectionSupported: boolean;
  reasonCodes: string[];
};
type NativeSessionResult = {
  id: string;
  monitoring: 'not_requested' | 'active' | 'blocked';
  reasonCodes: string[];
};
```

## 4. Fachada C do core (esboço, ADR-013)

```c
typedef struct lce_engine lce_engine;                 /* opaco, um por instância do módulo */

lce_engine* lce_engine_create(void);
void        lce_engine_destroy(lce_engine*);

int32_t lce_session_open  (lce_engine*, const lce_session_config*, uint32_t* out_session);
int32_t lce_session_clear (lce_engine*, uint32_t session);
int32_t lce_session_close (lce_engine*, uint32_t session);

int32_t lce_ingest_fixes  (lce_engine*, uint32_t session, uint32_t request_or_0,
                           const lce_fix* fixes, uint32_t count,
                           int64_t now_ms, lce_ingest_stats* out);
int32_t lce_ingest_motion (lce_engine*, uint32_t session,
                           const lce_motion_event* events, uint32_t count, int64_t now_ms);

int32_t lce_request_begin (lce_engine*, uint32_t session_or_0, const lce_request_config*,
                           int64_t now_ms, uint32_t* out_request, lce_effects* out);
int32_t lce_request_event (lce_engine*, uint32_t request, const lce_event*, lce_effects* out);
int32_t lce_request_result(lce_engine*, uint32_t request, lce_assessment* out);
int32_t lce_request_end   (lce_engine*, uint32_t request);

const char* lce_reason_code_name(int32_t code);       /* string estática */
uint32_t    lce_abi_version(void);
```

Propriedades:

- Sessões e requests são **handles inteiros** resolvidos por tabela dentro do engine. Handle inválido ou já fechado devolve código de status; não há ponteiro pendente para desreferenciar.
- Structs POD de tamanho fixo (`lce_assessment` com arrays limitados para reason codes e trace). Sem alocação que cruze a fronteira, sem callbacks do core para a plataforma.
- Toda função é `noexcept` com captura total; nenhuma exceção C++ cruza a fronteira.
- `now_ms` sempre vem da plataforma; o core não lê relógio.
- `lce_effects` é uma lista pequena e limitada de efeitos de I/O (`START_ACQUISITION`, `STOP_ACQUISITION`, `QUERY_MOTION`, `FINISH`), cada um com `request`, `generation` e orçamento.
- O engine não é thread-safe por contrato; o confinamento é da fila serial do módulo.

## 5. JNI — desenho e riscos

Desenho: classe Kotlin única com métodos `external`; argumentos e retornos em `DoubleArray`/`LongArray`/`IntArray` com layout documentado ao lado de `lce.h`; `JniBindings.cpp` copia para os POD e chama a fachada. O `.so` não linka contra React Native.

| Risco | Mitigação | Teste |
|---|---|---|
| Use-after-free de ponteiro nativo guardado em `Long` | um `jlong` só (engine), destruído como última tarefa da fila em `invalidate()`; depois disso flag `closed` rejeita chamadas; sessões/requests por handle inteiro | CON-024 (equivalente Android), CON-017 |
| Exceção C++ atravessando JNI (comportamento indefinido) | fachada `noexcept`; status → erro estruturado | CON-026, PLT-029 |
| Exceção Java pendente ignorada | única interação com a JVM é criar/copiar arrays; checar `ExceptionCheck` após `New*Array` | teste de falha de alocação simulada onde viável |
| Vazamento ou estouro de local refs | sem `FindClass`/`GetFieldID` em caminho quente; `Get*ArrayRegion` (cópia), nunca `GetPrimitiveArrayCritical` | CON-025 |
| R8 renomeia classe e quebra símbolos `Java_...` | `RegisterNatives` em `JNI_OnLoad` + regra `-keep` da classe em `consumer-rules.pro` | PLT-032 |
| `UnsatisfiedLinkError` em ABI não empacotada | capturar no carregamento → capability `platform_unsupported` | caso novo em PLT |
| Chamadas de threads diferentes | todo acesso passa pela fila serial do módulo (`limitedParallelism(1)`); o core não usa thread-local | CON-023 |
| STL duplicada ou incompatível | `c++_shared`, igual ao RN; confirmar empacotamento no spike | build release |
| Páginas de 16 KB | flag de page size flexível; conferir alinhamento do `.so` | CI |
| Divergência numérica por FMA | `-ffp-contract=off`, sem `-ffast-math` | fixtures cruzadas |
| Layout de array divergente entre Kotlin e C++ | constantes de layout geradas de um só lugar ou conferidas por teste de eco (Kotlin envia padrão conhecido, C++ devolve) | teste instrumentado |

## 6. iOS: ObjC++ / Swift / C — desenho e riscos

Desenho: `LocationConfidence.mm` (classe TurboModule do template, conforma ao spec gerado e a `RCTInvalidating`) converte argumentos em tipos Foundation e encaminha para uma fachada Swift `@objc`. O coordinator, as portas e os adapters são Swift. Swift chama `lce.h` diretamente. Não há `.mm` com lógica além da classe TurboModule.

| Risco | Mitigação | Teste |
|---|---|---|
| Swift não enxerga `lce.h` dentro do pod (header privado, module map) | spike com pods estáticos e com `use_frameworks! :linkage => :static`; plano B = wrapper ObjC++ da SPEC | PLT-033 |
| Header visível ao Swift inclui C++ e quebra o módulo | `lce.h` é C puro; header do spec Codegen permanece privado | build |
| Nome do import de `-Swift.h` muda com frameworks | `#if __has_include(<Pod/Pod-Swift.h>)` | build nas duas configurações |
| Core compilado como C++20 no pod (imposto pelo RN) | core no subconjunto C++17, CI nos dois padrões | CI host |
| Swift 6.2 com concorrência estrita quebra em consumidores | `swift_version` do pod em modo 5; sem `@MainActor` implícito | build |
| `CLLocationManager` criado fora de thread com run loop não entrega callbacks | criar e operar o manager na main; delegate só repassa para a fila do módulo | PLT-016..021 |
| Continuation retomada duas vezes (crash) ou nunca (vazamento) | não usar continuation para operações canceláveis de longa duração; coordinator orientado a eventos na fila serial, com guarda de generation e resolução única por request | CON-018..020 |
| Bloco resolve/reject chamado após `invalidate` | tabela de requests esvaziada em `invalidate`; blocos liberados sem chamar | CON-017, CON-028 |
| Ciclo de retenção manager ↔ delegate ↔ coordinator | delegate `weak`; closures com `[weak self]`; teste de desalocação | CON-015, CON-024 |
| Chamada de permissão/movimento sem chave no Info.plist derruba o app | checar presença da chave em runtime antes de chamar; erro estruturado | PLT-025 |
| Required reason API (uptime) sem privacy manifest | `PrivacyInfo.xcprivacy` no pod | PUB-027 (novo) |
| `requestLocation()` lento | stream único (R-04) | medição no spike |

## 7. Threads

| Thread / fila | Quem roda | Regra |
|---|---|---|
| JS | fachada TS | só chamadas assíncronas |
| Fila de métodos do TurboModule | entrada dos métodos nativos | apenas enfileira na fila do módulo |
| **Fila serial do módulo** (uma) | coordinator, registry, todas as chamadas a `lce_*`, timers | único lugar que muta estado; nunca bloqueia |
| Main | `CLLocationManager`, prompts de permissão, observadores de lifecycle | sem cálculo; repassa eventos |
| Threads do provedor (FLP, Core Motion) | callbacks do SO | repassam para a fila do módulo |

Timers de deadline pertencem à plataforma, disparam na fila do módulo e carregam `generation`; evento com generation antiga é descartado.

## 8. Sequência de `evaluate` (stream único)

```text
JS evaluate(requestId, req)
 └▶ fila do módulo: valida sessão, checa autorização e foreground
     ├─ sem permissão / serviços desligados ─▶ lce_request_begin ─▶ FINISH(unavailable) ─▶ resolve
     └─ ok: lce_request_begin ─▶ efeitos [START_ACQUISITION, (QUERY_MOTION)]
         agenda deadline total; assina stream (contagem de assinantes)
         a cada lote de fixes: lce_ingest_fixes; lce_request_event(FIXES_INGESTED)
              ├─ efeitos [] ─▶ continua
              └─ efeitos [STOP_ACQUISITION, FINISH] ─▶ cancela timers, remove assinatura,
                                                       lce_request_result ─▶ resolve uma vez
         timer de fase / deadline / abort / background / revogação / stopSession
              └─ lce_request_event(...) ─▶ sempre termina em FINISH
```

Invariante de implementação: todo caminho remove a assinatura do stream e os timers antes de resolver; a resolução passa por um único ponto que marca a request como concluída.
