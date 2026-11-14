# IMPLEMENTATION_PLAN — plano incremental (proposta Fase 0)

Pressupõe as recomendações de `SPEC_REVIEW.md`. Onde uma decisão D-xx for recusada, a fase correspondente volta ao texto da SPEC. Esforço em tamanhos relativos (P/M/G), sem datas.

## 1. Estrutura final proposta

```text
package.json, <Nome>.podspec, react-native.config.js, .nvmrc
src/
  index.ts  types.ts  validation.ts  errors.ts
  NativeLocationConfidence.ts        # spec Codegen
  mapper.ts  LocationConfidence.ts   # fachada com porta nativa injetável
  __tests__/
cpp/
  include/location_confidence/       # lce.h (C) + headers C++ internos
  src/                               # GeoMath, SampleValidator, LocationHistory, OutlierDetector,
                                     # MotionEvidenceAnalyzer, UncertaintyModel, CandidateSelector,
                                     # QualityScore, RecoveryPolicy, LocationEngine, CFacade
  tests/                             # host: unit, propriedades, fachada; fixtures/*.json
  CMakeLists.txt
android/
  build.gradle  consumer-rules.pro  CMakeLists.txt
  src/main/cpp/JniBindings.cpp
  src/main/java/.../  bridge/ application/ ports/ adapters/ models/
  src/test/ (JVM, fakes)   src/androidTest/ (JNI, fixtures, stress)
ios/
  LocationConfidence.h/.mm           # classe TurboModule
  Application/ Ports/ Adapters/ Models/   # Swift
  PrivacyInfo.xcprivacy
  Tests/                             # XCTest
example/                             # app de entregas + simulador de fixtures
docs/
```

Diferenças em relação à SPEC §3: sem `LocationConfidenceCxxAdapter.mm` nem `CxxEngineAdapter.swift` (substituídos pela fachada C); TS sem pastas domain/application/infrastructure; portas `Clock`, `Scheduler`, `LifecycleSource` adicionadas.

## 2. Fases e gates

| Fase | Conteúdo | Gate de saída | Esforço | Risco |
|---|---|---|---|---|
| **0** | Esta revisão | sua aprovação das decisões D-01..D-09 | — | — |
| **0.5 Spike** | Node 24 e NDK 27.1; scaffold `turbo-module`/`kotlin-objc`; `lce_abi_version()` alcançada por JS nas duas plataformas (JNI no Android; `.mm` → Swift → C no iOS); spec Codegen mínimo compilado com um objeto, um array, um anulável e uma união de literais; builds release (R8 ligado; pods estáticos e frameworks estáticos); CI esqueleto; medir primeiro fix one-shot contra stream em um dispositivo, se disponível | os quatro builds release passam; relatório Codegen atualizado com tipos realmente compilados; decisão final sobre ADR-013 e ADR-015 | M | **Alto** |
| **1 Core base** | tipos, validação, Haversine, ring buffer, dedup, score v1, classificação de relação; CMake host em C++17 e C++20; ASan/UBSan | GEO, VAL, HIS, SCR de core passam em host; nenhum include de plataforma | M | Baixo |
| **2 Modelos** | outliers, motion, incerteza (com envelope se D-01), seleção, propriedades | JMP, MOT, SCR restantes e propriedades passam; fixtures §12 com score exato | M | Médio (modelo) |
| **3 Política + fachada C** | reducer, driver de simulação, `lce.h` completo, tabela de handles; TSan no teste de fachada | REC e parte CON por simulação; todo roteiro termina em `FINISH`; fachada testada com handles inválidos | M–G | Médio |
| **4 Android** | CMake/JNI, portas, FLP, permissões, lifecycle, coordinator, registry, `invalidate` | testes JVM com fakes e tempo virtual; teste instrumentado de JNI e fixtures; exemplo faz avaliação real; release com R8; zero listeners após cancelamento | G | Alto |
| **5 iOS** | Swift: Core Location, Core Motion, permissões, lifecycle, coordinator; privacy manifest | XCTest com fakes; fixtures pela fachada C com mesmos resultados do Android; exemplo em simulador e device; release | G | Alto |
| **6 Fachada TS** | DTO completo, mapper, validação, erros, `AbortSignal` | Jest; teste de tipos; smoke nas duas plataformas | M | Baixo |
| **7 Exemplo** | app de entregas, timeline, modo simulação com fixtures | fluxos de permissão negada, degradado, cancelamento; score exibido como índice, sem `%` | M | Baixo |
| **8 Release** | stress, benchmark, CI de release, docs EN, tarball em app externo, checklist | P0 todos verdes; exceções documentadas; dois dispositivos físicos; `npm pack --dry-run` inspecionado. Publicação só com pedido explícito | M–G | Médio |

Cada fase encerra com o relatório exigido pelo `CLAUDE_GeoReliant.md`: objetivo, arquivos, comandos executados com exit status, IDs de teste criados/aprovados/falhos/bloqueados, riscos.

## 3. Ferramentas de teste propostas

| Camada | Ferramenta | Motivo |
|---|---|---|
| C++ host | doctest (header único, vendorizado em `cpp/tests/third_party`) + nlohmann/json só nos testes | sem download em CI, compilação rápida; o core não ganha dependência |
| Propriedades C++ | `std::mt19937_64` com seed fixa e mapeamento próprio para intervalos | `std::uniform_real_distribution` não produz a mesma sequência entre libstdc++ e libc++ |
| Android JVM | JUnit + coroutines-test (tempo virtual) + fakes das portas | sem `sleep`, sem Play Services |
| Android instrumentado | AndroidX Test | JNI real, fixtures pela fachada, R8 |
| iOS | XCTest com fakes das portas e `Scheduler` falso | sem `sleep`, sem GPS |
| TS | Jest (o que vier no scaffold) + teste de tipos | validação, mapper, abort |
| Docs | script que verifica afirmações obrigatórias e proibidas no README | PUB-006..013 |

Fixtures JSON são a fonte única: executadas em host (C++), no Android instrumentado e no XCTest, sempre pela fachada C.

## 4. Mapa dos 289 casos → suíte

Suítes: **C** = C++ host; **F** = fachada C/registry (host + plataforma); **T** = Jest/tipos; **A** = Android JVM com fakes; **I** = iOS XCTest com fakes; **AI** = mesmo roteiro nas duas plataformas; **D** = dispositivo/instrumentado/sanitizer; **CI** = build ou verificação estática; **DOC** = verificação de documento; **REL** = checklist de release; **MAN** = protocolo manual.

| Grupo | IDs | Suíte |
|---|---|---|
| GEO | 001–011, 026 | C `TestGeoMath` (026 também T) |
| GEO | 012–019 | T (validação; nativo não é chamado) + C defensivo na fachada |
| GEO | 020–023 | C `TestRelation` |
| GEO | 024–025 | C `TestProperties` |
| VAL | 001–008, 011, 013, 014, 018–026 | C `TestValidation` |
| VAL | 009, 010, 012 | A (normalização de tempo com relógio falso) |
| VAL | 015, 016, 017 | I |
| HIS | 001–009, 012, 014, 017–025 | C `TestHistory` (021–024 também `TestScore`) |
| HIS | 010, 011, 013, 015, 016 | F + AI |
| HIS | 026 | MAN (por construção: sem persistência; ver PUB-002) |
| JMP | 001–024 | C `TestOutliers` (020 também seleção) |
| MOT | 001–009, 013–017, 020, 022, 023 | C `TestMotion` |
| MOT | 010, 011, 012, 021, 024, 025 | AI (012 também C reducer) |
| MOT | 018, 019 | DOC |
| REC | 001–019, 025–029 | C `TestRecoveryPolicy` |
| REC | 020, 022 | A |
| REC | 023 | I |
| REC | 021, 024 | AI |
| SCR | 001–004, 006–029, 031, 032 | C `TestScore`, `TestRelation`, `TestProperties` |
| SCR | 005, 030 | T (tipos) + DOC |
| CON | 001, 005, 006 | T |
| CON | 002–004, 007–021, 026–028 | AI (012–014 também C reducer; 027 só I) |
| CON | 023, 025 | D (Android instrumentado) + F com TSan |
| CON | 024 | D (iOS) + F com ASan |
| CON | 022 | MAN |
| PLT | 001–009, 011–015 | A |
| PLT | 016–024, 026–029 | I |
| PLT | 010 | MAN |
| PLT | 025, 031, 032, 033 | CI |
| PLT | 030 | DOC |
| PLT | 034 | AI + MAN |
| PUB | 001, 003, 014, 015, 017–019, 023, 025, 026 | CI |
| PUB | 002 | D |
| PUB | 004, 005 | F + AI |
| PUB | 006–013, 021 | DOC |
| PUB | 016, 020, 022, 024 | REL |
| SES | 001–012 | AI (004, 005, 010 também C) |
| SES | 013 | T |

Totais por suíte primária: C 153, F 7, T 14, A/I/AI 78, D 4, CI 14, DOC 12, REL 4, MAN 3 = 289.

**P0 proposto:** todo caso ligado a uma INV, mais todos os CON, SCR e REC.

**Casos novos propostos** (IDs acrescentados, sem renumerar): MOT-026..030 (envelope de velocidade), MOT-031 (estado estável sem eventos com assinatura contínua), HIS-027 (proteção do melhor candidato), CON-029 (perda de foco sem background não cancela), PLT-035 (`UnsatisfiedLinkError` → capability), PUB-027 (privacy manifest).

**Exceções previstas:** PLT-013/014 com adapter real (se D-02), CON-022, HIS-026 e PLT-010 só manuais.

## 5. Riscos críticos, em ordem

1. Pod misto Swift + ObjC++ + C com header C visível ao Swift. Tratado no spike; plano B definido.
2. Modelo de incerteza sem motion inutilizando o resultado (R-01). Decisão de modelo, não de código.
3. Vazamento de listener/assinatura sob cancelamento e corrida timer × callback. Coberto por roteiro único de eventos nas duas plataformas e pelo ponto único de resolução.
4. Paridade Android/iOS do mapa de retorno. Coberto por golden de transporte comparado nas duas plataformas.
5. R8 e JNI em release. Coberto no spike e de novo na Fase 4.
6. Latência real das APIs de localização e de motion. Só medição em dispositivo responde.
7. Disponibilidade de dois dispositivos físicos para as Fases 7–8.

## 6. Perguntas da SPEC §16.1 — situação

| # | Situação |
|---|---|
| 1 | RN não instalado; alvo proposto 0.88.1. Tipos suportados lidos no fonte do Codegen; compilação no spike |
| 2 | Nenhum scaffold entrega Kotlin/Swift + core C++; montagem manual sobre `kotlin-objc`; C++ entra por Gradle/CMake e podspec |
| 3 | Proposta: Android 24+, iOS 15.1+; confirmar em CI |
| 4 | Coordinator orientado a eventos em fila serial, sem continuation de longa duração; delegate fraco; resolução única (NATIVE_TRANSPORT_SPEC §6) |
| 5 | Motion opt-in por sessão; `evaluate` nunca dispara a primeira autorização |
| 6 | Stream compartilhado com contagem de assinantes desde a v1; limite por sessão e `busy` acima dele |
| 7 | Score `heuristic-v1` com as emendas aprovadas; reason codes de R-12 |
| 8 | `source` por procedência + idade (R-11); sem alegar detectar cache do SO além da idade |
| 9 | Comparar deltas de relógio de parede e monotônico; teste em device na Fase 5 |
| 10 | Job de CI instala o tarball em app externo ao monorepo (PUB-015), Fase 8 |
