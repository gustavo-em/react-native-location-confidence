# ESPECIFICAÇÃO DE ENGENHARIA — Location Confidence Engine

**Versão da especificação:** 2.0 — 14/11/2026  
**Status:** Ready for design review / NOT IMPLEMENTED  
**Nome provisório de pacote:** `react-native-location-confidence` (disponibilidade no npm NÃO confirmada)  
**Tipo de produto:** biblioteca npm React Native, iOS + Android, New Architecture/TurboModules; exemplo de aplicação de entregas  
**Linguagens:** TypeScript (fachada e casos de uso), Kotlin (Android), Swift (iOS), Objective-C++/JNI (adaptação), C++17 (algoritmos compartilhados).  
**Destinatário:** Claude Code, equipe técnica e revisores de código.  
**Idioma:** documentação em português nesta SPEC; README público e código/comentários em inglês.  
**Princípio principal:** nunca confundir qualidade de uma evidência geográfica com probabilidade real de presença física.

> **ATENÇÃO:** Esta é uma especificação de projeto, não um relato de implementação. Nenhum teste, benchmark ou build descrito aqui foi executado. Os limiares numéricos são hipóteses iniciais para experimentar e ajustar; não são garantias estatísticas, antifraude ou normas de localização.

---

# 0. Sumário executivo e contrato com o agente

## 0.1 Problema de produto

Um entregador precisa verificar proximidade de um destino. A localização pode ser negada, aproximada, lenta, imprecisa, antiga, artificialmente simulada, contraditória, indisponível sob teto/cobertura ou interrompida por mudança de lifecycle. Apenas `distance <= radius` produz falsos positivos e falsos negativos. A aplicação consumidora necessita de **evidências, procedência, idade, qualidade e diagnósticos**, sem bloquear o usuário indefinidamente.

## 0.2 Proposta de valor

Uma API única em TypeScript dispara uma avaliação nativa adaptativa. Android/iOS obtêm posição e atividade sob permissões explícitas; um **core C++17 único** administra histórico de amostras, detecta saltos suspeitos, estima incerteza conservadora, classifica evidência e fornece um score de qualidade determinístico de 0 a 100. A biblioteca não dá autorização automática para concluir entregas.

## 0.3 Escopo v1 obrigatório

1. Leitura on-demand em foreground (uma avaliação). Sessão foreground opcional para formar buffer prévio, **sempre explícita**.
2. Android Fused Location Provider (com ausência de Play Services diagnosticada); iOS Core Location; autorização precisa/reduzida.
3. Evidência auxiliar de movimento via Activity Recognition/Core Motion, **opcional** e degradável a `unknown`.
4. Buffer circular em memória de até 10 amostras úteis/diagnósticas por sessão, sem persistência.
5. Tentativa de localização atual, coleta curta de múltiplas leituras, avaliação de histórico, detecção de outliers, propagação de incerteza temporal.
6. Core C++ compartilhado e testável sem RN nem simulador, compilando em Android, iOS e em host de testes.
7. TurboModule/Codegen, fachada pública tipada, cancelamento, timeout, suporte a testes determinísticos, app de exemplo.
8. Documentação de privacidade, permissões, instalação, limitações, benchmark e publicação npm.

## 0.4 Fora de escopo e proibições

- Sem backend, geocoding, Google Maps, upload de coordenadas, endpoints de conectividade, analytics ou coleta oculta.
- Sem localização contínua em background, `ACCESS_BACKGROUND_LOCATION`, foreground service, manutenção de rastreamento após process death, geofences persistentes ou execução com app encerrado.
- Sem ML, filtro Kalman, simulação Monte Carlo, dead reckoning real, navegação, cálculo de rotas, triangulação própria de Wi-Fi/celular, detecção antifraude infalível ou `probabilityWithinRadius` numérica na v1.
- Não solicitar permissão no `import`, no mount de hook ou automaticamente durante `evaluate`. Sem elevar precisão quando o usuário optou por localização aproximada.
- Nunca devolver `{latitude:0, longitude:0}` como fallback, inventar speed/accuracy ou converter ausência de sensor em `stationary`.
- Não serializar trajetos para logs/crash reports e não gravar em disco por padrão.

## 0.5 Critérios de sucesso do projeto

- **Corretude:** comportamento determinístico em fixtures; nenhuma aproximação implícita tratada como GPS real.
- **Portabilidade:** um algoritmo C++ comum; adapters Android/iOS só traduzem dados e gerenciam APIs/lifecycle.
- **Segurança operacional:** cada operação termina/cancela; nenhum listener vaza; nenhuma Promise é resolvida duas vezes; callbacks tardios ignorados.
- **Qualidade de engenharia:** arquitetura documentada por ADRs, 100% dos testes críticos implementados, builds release nativos reais antes de publicar, benchmarks com método reproduzível.
- **Adoção:** README em inglês com exemplo, permissões, caveats de Expo Go, diagnóstico e matriz de compatibilidade.

## 0.6 Regras para o Claude Code

1. **Fase 0 é somente planejamento.** Inspecionar o repositório, verificar versões instaladas, escrever ADRs e propor contratos/arquivos. Não escrever código de produção sem aprovação explícita da Fase 1.
2. Não presumir que o exemplo da documentação RN mais recente é compatível com o scaffold instalado. Registrar versionamento real de RN, React, Codegen, NDK, Xcode, Gradle, CocoaPods, Kotlin e deployment targets.
3. Se algum contrato Codegen não suportar object/union/array com o formato desejado, criar **DTOs internos simples** e converter na fachada TS; não enfraquecer o domínio para acomodar transporte.
4. Não criar framework interno genérico desnecessário, container DI externo, banco SQLite, JNI callback por amostra ou threads do C++ sem necessidade.
5. Cada fase gera: arquivos alterados, decisões, testes realmente rodados, falhas conhecidas, próximos riscos; sem dizer “passou” quando não foi executado.
6. Substituir estimativas arbitrárias por hipóteses anotadas e tests de invariantes. Uma simulação verde não prova a qualidade do GPS real.
7. Implementar primeiro core C++ e fixtures; depois adapters, e só então integração, example e npm.
8. Nunca degradar silenciosamente: se a feature não tem API/permissão, registrar capability e reason code e seguir o fallback seguro.
9. Se duas alternativas gerarem trade-offs importantes, emitir ADR incluindo contexto, alternativas, decisão, consequências e gatilho de revisão.

---

# 1. Requisitos: funcionais, não funcionais e invariantes

## 1.1 Funcionais [FR]

| ID | Requisito | Critério de aceitação |
|---|---|---|
| FR-01 | Obter capabilities e autorização sem prompt | Status correto por plataforma; não ativa GPS |
| FR-02 | Pedir permissões apenas via método explícito | UI OS só é exibida após chamada explícita do host |
| FR-03 | Avaliar destino circular | Retorno com distância, raio e evidência com unidades claras |
| FR-04 | Obter uma leitura atual | Usa API de one-shot, valida fix recebido, permite timeout |
| FR-05 | Janela curta de recuperação | Recebe 0..N amostras até orçamento global; cleanup garantido |
| FR-06 | Buffer com 10 amostras | Circular, deduplicação, preservação de timestamp, por sessão |
| FR-07 | Detectar saltos | Sinalizar suspeita sem descartar automaticamente movimento legítimo |
| FR-08 | Avaliar movimento auxiliar | Só atribuir continuidade de imobilidade se período coberto |
| FR-09 | Ajustar incerteza por idade/movimento | Nunca reduzir incerteza sem evidência nova, modelo identificado |
| FR-10 | Gerar qualityScore 0–100 | Fórmula, breakdown, caps e versão do modelo disponíveis |
| FR-11 | Retornar explicação e status | `status`, `reasonCodes`, `source`, `isMeasured`, `capabilities` |
| FR-12 | Cancelar/stop/foreground | Liberação de recursos, nunca entregar resultados tardios |
| FR-13 | Executar sem internet | Sem rede ou requests HTTP do SDK |
| FR-14 | Publicar pacote npm | Exemplo iOS/Android e instruções de instalação compilam |
| FR-15 | Testes determinísticos | Fixtures C++, mocks Kotlin/Swift, integração RN e hardware |
| FR-16 | Privacidade por padrão | Sem persistência, coordenadas em telemetry ou permissões ocultas |

## 1.2 Não funcionais [NFR]

| ID | Objetivo | Verificação |
|---|---|---|
| NFR-01 | Sem trabalho pesado na UI/Main Thread | análise de threads e teste de stress |
| NFR-02 | Algoritmo core independente de RN | executável de testes C++ sem JS/RN/SDK de plataforma |
| NFR-03 | Consistência Android/iOS | mesmas fixtures produzem os mesmos outputs core |
| NFR-04 | Encerramento bounded | deadline total independente de callbacks faltantes |
| NFR-05 | Recursos não vazam | testes de cancelar/repetir/foreground-background 100 ciclos |
| NFR-06 | Score monotônico no mesmo contexto | testes de propriedade com variação de idade/accuracy |
| NFR-07 | Observar sem vazar | diagnostics sem latitude/longitude em logs por default |
| NFR-08 | Compatibilidade documentada | matriz por RN/Android/iOS/arquitetura, builds release |
| NFR-09 | Sem ANR, deadlock, data race | execução off main e sanitizers onde disponíveis |
| NFR-10 | Suportar downgrade razoável | motion indisponível não impede localização básica |

## 1.3 Invariantes invioláveis [INV]

- INV-01: permissão `precise` não implica `horizontalAccuracyMeters` baixo; permissão `approximate` não pode ser contornada.
- INV-02: `stationary` detectado **agora** não prova imobilidade desde o fix; o fallback estacionário requer evidência temporal contínua válida.
- INV-03: leituras recebidas no mesmo callback ou repetidas não são novas evidências independentes de precisão.
- INV-04: o score é **índice de qualidade**, não `probabilityWithinRadius` nem verificação antifraude.
- INV-05: `source=estimated` somente se no futuro houver modelo de predição explícito; v1 jamais move coordenadas estimadas.
- INV-06: freshness, source e idade devem refletir o fix **original**, não o horário em que o JS solicitou a avaliação.
- INV-07: todas as medidas possuem unidades em nomes de campo (`Meters`, `Ms`, `Mps`); rejeitar NaN/Inf.
- INV-08: `qualityScore` não melhora apenas porque a idade aumenta, accuracy piora ou cobertura de evidência desaparece (mesmo contexto, mesma amostra).
- INV-09: `qualityScore > 0` não autoriza entrega; relação de proximidade também não é prova.
- INV-10: toda subscrição nativa ativa tem owner, id, deadline/cancel handle e cleanup idempotente.
- INV-11: avaliações simultâneas nunca misturam destino, radius, requestId ou resultado.
- INV-12: cancelamento de uma avaliação não mata uma sessão usada por outro consumidor; `stopSession` invalida todas as avaliações ligadas à sessão.
- INV-13: nenhum score positivo sem coordenada + timestamp/idade confiáveis.
- INV-14: cálculo de idade monotônico na sessão; não comparar relógios monotônicos de OS diferentes, nem época de parede com uptime bruto.
- INV-15: validação de presença física exige múltiplas provas no app consumidor; esta biblioteca só fornece evidências.

---

# 2. Decisões de arquitetura — ADRs completos

## ADR-001 — TS Clean Architecture + núcleo C++17 + adapters hexagonais

**Contexto:** o algoritmo inclui geodesia, ring buffer, exclusão/penalização de outliers, scores, propagação de incerteza. Duplicá-lo em Kotlin e Swift causa drift; executá-lo integralmente em JS é simples, mas perde autonomia quando JS está bloqueado.

**Alternativas:** (A) tudo TS; (B) algoritmos duplicados Kotlin/Swift; (C) TurboModule C++ puro fazendo também aquisição; (D) **adaptadores Kotlin/Swift + core C++ compartilhado**.

**Decisão:** D. Core C++17 compilável e testável isoladamente; Kotlin/Swift expõem API de plataforma e gerenciam eventos/threads. TypeScript mantém contrato público e negócio do aplicativo. C++ não importa React Native/JSI, Android, Foundation ou STL não portátil. Um módulo TurboModule por plataforma direciona chamadas para os adapters, que invocam C++ via JNI/ObjC++.

**Justificativa:** um algoritmo central, alto isolamento, testes nativos determinísticos; lifecycle e permissões continuam no idioma do SDK correspondente; custo de bridges permanece controlado por operações em lote. **O ganho de milissegundos não é a justificativa principal; mediremos antes de afirmar desempenho.**

**Consequências negativas:** toolchain C++ no npm, JNI e ObjC++ aumentam debugging/build/CI; dois adaptadores precisam ser mantidos; mais superfícies para erro de threading e ownership.

**Revisar se:** suporte RN/Codegen inviabiliza formato escolhido ou manutenção supera o benefício; avaliar puro C++ TurboModule/JSI, sem mudar o core.

## ADR-002 — Políticas de aquisição versus decisão do algoritmo

**Decisão:** estado e política *determinísticos* de recuperação são expressos em C++ por uma **máquina de estados redutora de eventos** (`RecoveryPolicy::onEvent`). Coordinators Kotlin/Swift apenas executam ações assíncronas retornadas (`REQUEST_FIX`, `QUERY_MOTION`, `COLLECT_WINDOW`, `ASSESS`, `FINISH`, `CANCEL`). Os próprios adapters são responsáveis por timeout/cancelamento efetivo no SO. O TS não reproduz essa máquina; ele a invoca por um único `evaluate` público.

**Motivo:** evitar divergência de regras entre plataformas; manter testes rápidos em host C++, porém não criar runtime/event-loop C++ completo. C++ recebe eventos, devolve ações; platform orchestrator converte cada ação em operação assíncrona.

**Cuidados:** a plataforma **sempre** impõe deadline real e passa eventos `DEADLINE_REACHED`, `PERMISSION_REVOKED`, `APP_BACKGROUNDED`; a máquina não deve esperar callback inexistente. Ordem de eventos determinística por requestId + sequence; C++ nunca bloqueia.

**Alternativa rejeitada:** duplicar if/else da recuperação em Swift e Kotlin; fácil v1, difícil garantir comportamento idêntico. Aceito duplicar somente scaffolding do dispatcher de eventos.

## ADR-003 — ABI e integração

**Decisão:** API C++ interna sem dependência de RN (`LocationEngine`, `RecoveryPolicy`, `LocationSample`, `Assessment`); JNI usa DTOs compactos por **lote**, não callbacks por fix; iOS Objective-C++ converte structs Swift via bridge ObjC/Objective-C++ e chama C++. Não expor ponteiros C++ em TypeScript. Não presumir ABI binária estável entre compiladores; compilar fontes junto da lib para cada plataforma.

**Razão:** formato simples, ownership claro, sem manter bindings independentes de JSI. A camada `.mm` não contém lógica geográfica.

**Risco:** Swift ↔ ObjC++ exige wrapper interoperável e testes de linkage; JNI pode gastar tempo de marshalling — medir antes de otimizar.

## ADR-004 — Histórico e concorrência

**Decisão:** ring buffer de **10 fixes** por sessão **dentro do core C++**. Plataforma entrega fix normalizado ao core em lote ou um evento isolado numa fila serial; o core não possui sua própria thread. Uma **serial executor/queue por sessão** protege `LocationEngine` e `RecoveryPolicy`; assim, os métodos C++ são single-thread confinados e não dependem de locks internos.

**Motivo:** evita lock em cada leitura, corridas interplataforma e histórico diferente em Android/iOS. Histórico serve para diagnósticos; uma sessão deve ser iniciada explicitamente para acumular leituras antes da avaliação.

**Retenção:** somente em memória, 10 amostras e até 2 minutos; sem reutilização entre sessões/logins. Eventos de callback duplicados são deduplicados por identificador técnico/tempo/posição/sensor, não por arredondamento grosseiro.

**Alternativa:** mutex C++ + coleção compartilhada. Rejeitada em v1 pela superfície de deadlock; reavaliar se processar em múltiplas threads for exigido.

## ADR-005 — Score de qualidade, não probabilidade

**Decisão:** `qualityScore` inteiro [0,100] + breakdown, `scoreModelVersion = "heuristic-v1"`; `probabilityWithinRadius: null` na v1. Um índice 78 é **78 pontos**, NÃO 78% de certeza, nem probabilidade calibrada.

**Motivo:** `horizontalAccuracy` de Android/iOS não tem contrato estatístico universal idêntico, erros correlacionados e vieses por ambiente/dispositivo. Um % de presença exigiria datasets com verdade de referência + validação de calibração por contexto. Score não governa sozinho a decisão do negócio.

**Revisar se:** houver validação empírica reproduzível e relatório de calibração (reliability diagram, Brier score, intervalos por plataforma/condição).

## ADR-006 — Preferir recência e consistência, nunca inventar posição

**Decisão:** sem filtros de navegação/predição de coordenadas na v1. Selecionar fix medido, com `effectiveUncertaintyMeters` acrescido de deslocamento plausível desde a leitura; `source` deve permanecer `fresh`, `recovered` ou `cached`, e nunca `estimated` até existir dead reckoning real.

**Motivo:** prever caminho com direção/velocidade desatualizada pode gerar falsa proximidade e erros perigosos. Um aparelho parado não implica pessoa parada; motion é evidência auxiliar.

## ADR-007 — Deadline global + cancelamento estruturado

**Decisão:** v1 foreground-only. `totalDeadlineMs` cobre permissão já existente, one-shot, motion, recovery e avaliação; `AbortSignal` em TS traduzido para `cancelRequest(id)`; native callback tardio ignorado por request generation; resultado cancelado estruturado (`status="cancelled"`) em vez de rejeição, salvo erro de programação/invalid argument.

**Motivo:** UX previsível e ausência de callbacks vazando. Configuração inválida falha antes de ativar GPS; erro interno vira diagnóstico estável, sem stack trace de sensor nos dados públicos.

## ADR-008 — Uma coleta compartilhada, avaliações distintas

**Decisão:** por sessão, permitir **uma aquisição GPS ativa**; avaliações concorrentes usam coleta single-flight quando configuração de precisão for compatível, mas **avaliam independentemente** destino/raio/deadline. Uma avaliação pode cancelar seu interesse sem remover sensores se houver outros assinantes. Sessão tem contagem de assinantes; `stopSession` cancela todos.

**Estratégia de entrega:** V1 inicial pode serializar avaliações e retornar `busy` estruturado para a segunda, **desde que** ADR e testes registrem a limitação. Não implementar single-flight complexa sem testes de corridas.

## ADR-009 — Privacidade, permissões, ausência de rede

**Decisão:** mínimo de permissões necessárias; nenhuma permissão implícita, coleta apenas em sessão foreground explícita; memory-only e zero rede. Motion é opcional. Nenhuma coordenada em logs/crash reports; `clearHistory` e `stopSession` apagam estado e referências.

**Motivo:** trajetos são dados potencialmente sensíveis; um SDK npm deve limitar coleta e oferecer controle ao app hospedeiro.

## ADR-010 — Energia e observabilidade

**Decisão:** one-shot antes de coleta de updates, `recoveryWindowMs` limitada, encerramento antecipado quando candidatos adequados surgirem. Relatar `durationMs`, etapas, contagens e reason codes sem coordenadas em logs. Benchmarks de CPU e bateria **medidos**, não prometidos.

## ADR-011 — Estratégia de testes e confiança de publicação

**Decisão:** testes de propriedade, fixtures comuns C++, suites Android/iOS e E2E em dispositivo real; CI compila ambos os sistemas (iOS só em runner macOS). Nenhuma publicação com build release ausente ou apenas testes simulados. Sem afirmar suporte Expo Go; requer development build/prebuild com integração nativa (verificar versões).

## ADR-012 — Política de compatibilidade e versões

**Decisão:** New Architecture/TurboModules apenas na primeira versão; registrar `peerDependencies` e deployment targets testados, fixar toolchains do CI. Compilar como C++17. Suporte a RN e sistemas mínimos só será anunciado depois de build real. Não usar versionamento “latest” em CI reprodutível.

---

# 3. Arquitetura — fronteiras e responsabilidades

## 3.1 Diagrama de fluxo e dependências

```text
APP EXEMPLO (MVVM / hooks)   APP CONSUMIDOR
            \                  /
             --> PUBLIC API TYPESCRIPT
                    |         |
               use cases   contracts/config/validation
                    |
              NativeLocationPort
                    |
               Codegen / TurboModule  (somente tipos DTO transportáveis)
                    |
              +-----+---------------------+
              |                           |
        ANDROID KOTLIN               iOS SWIFT / ObjC++
        Incoming Adapter             Incoming Adapter
              |                           |
        NativeCoordinator            NativeCoordinator
        serial executor              serial executor
        LocationPort                 LocationPort
        MotionPort                   MotionPort
        PermissionPort               PermissionPort
              |                           |
       FusedLocationAdapter       CoreLocationAdapter
       MotionRecognitionAdapter   CoreMotionAdapter
              \                           /
               -> JNI / ObjC++ adapter <-
                          |
                C++17 SHARED ENGINE
             RecoveryPolicy (event->action)
             LocationHistory (10 fixes)
             SampleValidator
             DistanceCalculator
             OutlierDetector
             CandidateSelector
             MotionUncertaintyModel
             QualityScoreEvaluator
                          |
                    Assessment DTO
                          |
                  TypeScript facade
```

**Direção das dependências:** TS Domain não importa RN; TS Application depende de domínio/port, Infrastructure implementa port. Core C++ não importa SDK, React Native, JNI, Objective-C ou clocks de plataforma. Kotlin/Swift `Coordinator` depende de portas da plataforma e adapter para `CppEnginePort`; classes concretas implementam portas (Dependency Inversion). Bridge é um inbound adapter, não domínio.

## 3.2 TypeScript — Clean Architecture

```text
src/
  domain/
    contracts/LocationConfidence.ts       # conceitos públicos, Value Objects
    policies/DeliveryDecisionPolicy.ts    # somente exemplo de regra do consumidor
    errors/ArgumentValidationError.ts
  application/
    use-cases/EvaluateProximityUseCase.ts
    use-cases/StartSessionUseCase.ts
    use-cases/StopSessionUseCase.ts
    ports/NativeLocationPort.ts
    dtos/EvaluationRequest.ts
  infrastructure/
    rn/NativeLocationPortAdapter.ts       # native calls, error map, AbortSignal
    rn/NativeLocationConfidence.ts        # CODEGEN SPEC
    mapping/NativeDtoMapper.ts
  index.ts
  __tests__/
example/
  src/features/delivery/
    presentation/DeliveryScreen.tsx
    presentation/useDeliveryViewModel.ts
    application/DeliveryWorkflow.ts
    infrastructure/LocationConfidenceGateway.ts
```

**Regra:** TypeScript não reimplementa Haversine, cálculo do score, histórico ou estados de recuperação. Seu caso de uso faz validação pública, escolhe política de negócios do consumidor (ex.: UI de entrega), aciona o serviço nativo, traduz resultado e controla cancelamento. Nenhuma tela chama TurboModule diretamente. Não exportar classes C++ no JS.

## 3.3 Core C++17 — domínio e política de recuperação

```text
cpp/
  include/location_confidence/
    Types.hpp
    LocationEngine.hpp
    RecoveryPolicy.hpp
    LocationHistory.hpp
    LocationAssessment.hpp
  src/
    GeoMath.cpp
    SampleValidator.cpp
    LocationHistory.cpp
    OutlierDetector.cpp
    MotionEvidenceAnalyzer.cpp
    UncertaintyModel.cpp
    CandidateSelector.cpp
    QualityScore.cpp
    RecoveryPolicy.cpp
    LocationEngine.cpp
  tests/
    fixtures/
    TestGeoMath.cpp
    TestHistory.cpp
    TestOutliers.cpp
    TestMotion.cpp
    TestRecoveryPolicy.cpp
    TestScore.cpp
    TestProperties.cpp
  CMakeLists.txt
```

**Pure core:** `evaluateSnapshot(snapshot, options)` sem I/O e determinístico; `RecoveryPolicy` reduz eventos de aquisições e emite comandos. Histórico do C++ é mutável mas confinado em uma sessão/queue. Não fazer chamadas bloqueantes do C++ para Kotlin/Swift. Preferir structs simples, `std::optional`, `std::vector`, `enum class`, `std::chrono` somente após normalização de tempos; sem dependências binárias terceiras no núcleo inicial.

## 3.4 Android — Hexagonal

```text
android/src/main/java/com/.../
  bridge/LocationConfidenceModule.kt      # inbound adapter TurboModule
  application/NativeEvaluationCoordinator.kt
  application/SessionRegistry.kt
  ports/LocationSource.kt
  ports/MotionSource.kt
  ports/PermissionSource.kt
  ports/CoreEnginePort.kt
  adapters/FusedLocationAdapter.kt
  adapters/ActivityRecognitionAdapter.kt
  adapters/AndroidPermissionsAdapter.kt
  adapters/JniCoreEngineAdapter.kt
  models/PlatformLocationFix.kt
android/src/main/cpp/
  JniBindings.cpp
  CMakeLists.txt
```

**Kotlin:** executa permissões já concedidas, callbacks, solicitação one-shot, coleção limitada, motion quando autorizado, tracking do lifecycle, agenda ações `RecoveryPolicy` oriundas do core. `CoroutineScope` ligado à vida do módulo, sem `GlobalScope`. `suspendCancellableCoroutine`/callbackFlow, cancelamento forte e `finally` removendo `LocationCallback`. Reaproveitar apenas executor serial para uma sessão.

## 3.5 iOS — Hexagonal

```text
ios/
  Bridge/LocationConfidenceTurboModule.mm
  Bridge/LocationConfidenceCxxAdapter.mm
  Application/NativeEvaluationCoordinator.swift
  Application/SessionRegistry.swift
  Ports/LocationSource.swift
  Ports/MotionSource.swift
  Ports/PermissionSource.swift
  Ports/CoreEnginePort.swift
  Adapters/CoreLocationAdapter.swift
  Adapters/CoreMotionAdapter.swift
  Adapters/IOSPermissionsAdapter.swift
  Adapters/CxxEngineAdapter.swift
  Models/PlatformLocationFix.swift
```

**Swift:** `CLLocationManager` com delegate retido por owner; callbacks no executor apropriado, serialização de sessão e bridge ObjC++ bem definida. Async operations com cancellation e timeout; delegate não faz cálculo geográfico. Não usar implementações de Swift que C++ tente importar diretamente; ObjC-compatible bridge (ou wrapper C de fachada) é necessária e precisa de teste compilado.

## 3.6 Contratos da hexagonal nativa

```text
LocationSource:
  requestCurrentFix(options, cancelToken) -> LocationFix | unavailable
  collectWindow(options, onFix, cancelToken) -> collectionOutcome
  stopAll(sessionId)

MotionSource:
  getCapabilities() -> supported / authorization
  queryEvidence(interval, cancelToken) -> MotionEvidence | unknown
  subscribeSessionMotion(sessionId, onMotion) -> cancelHandle

PermissionSource:
  getLocationAuthorization() -> Authorization
  getMotionAuthorization() -> Authorization
  requestLocationAuthorizationExplicitly() -> Authorization
  requestMotionAuthorizationExplicitly() -> Authorization

CoreEnginePort:
  createSession(sessionId, policyConfig)
  ingestFixes(sessionId, normalizedFixBatch)
  ingestMotion(sessionId, normalizedMotionEvents)
  dispatchRecoveryEvent(requestId, event) -> RecoveryAction
  evaluateSnapshot(requestId, context) -> Assessment
  stopSession(sessionId)
```

**DIP verificável:** `application/` no Kotlin/Swift não importa FusedLocationProviderClient, CLLocationManager, JNI nem React; somente interfaces e models próprios. Binding no composition root monta adapters concretos.

## 3.7 Fronteira de responsabilidades — revisão de PR

| Operação | TypeScript | Kotlin/Swift | C++ |
|---|---|---|---|
| API pública e parâmetros de negócio | Responsável | DTO | Não |
| Checar/solicitar permissões | Chamada explícita | Responsável | Consome metadata |
| One-shot, updates, motion | Chamada única | Responsável | Não chama SO |
| Lifecycle/timeout/cancel real | Faz AbortSignal | Responsável | Estado/ações determinísticos |
| Buffer de 10 leituras | Não | Encaminha por sessão | Responsável |
| Dedup, distância, outliers | Não | Não | Responsável |
| Incerteza, score e relação | Não | Não | Responsável |
| Autorizar entrega/UX | App consumidor | Não | Nunca autoriza |
| CI/build native | Configuração | Configuração | Suite isolada |

---
# 4. API TypeScript pública e contrato de transporte

> **Regra:** a sintaxe abaixo é API desejada, não garantia de que todos os objetos são representáveis diretamente pelo Codegen. Na Fase 0 deve haver `CodegenCompatibilityReport.md` com tipos efetivamente compilados. A API TypeScript pode ter shape rico; o DTO de ponte é distinto.

## 4.1 Exemplo de uso público

```ts
import { LocationConfidence } from 'react-native-location-confidence';

const capabilities = await LocationConfidence.getCapabilities();
const auth = await LocationConfidence.getAuthorization();

// Aplicativo decide QUANDO perguntar ao usuário.
if (!auth.locationGranted) {
  await LocationConfidence.requestLocationPermission();
}

const session = await LocationConfidence.startSession({
  mode: 'foreground',
  historyCapacity: 10,
  collectionMode: 'foreground_monitoring', // opt-in: cria histórico prévio
  monitoringIntervalMs: 5000,
});

const controller = new AbortController();
try {
  const assessment = await LocationConfidence.evaluateProximity({
    sessionId: session.id,
    destination: { latitude: -20.8117, longitude: -49.3768 },
    radiusMeters: 100,
    options: {
      totalDeadlineMs: 12000,
      recoveryWindowMs: 7000,
      maxFreshAgeMs: 10000,
      maxHorizontalAccuracyMeters: 50,
      enableMotionAssistance: true,
    },
    signal: controller.signal,
  });
  // App pode solicitar prova adicional independentemente do score.
  console.log(assessment.qualityScore, assessment.evidenceRelation);
} finally {
  await LocationConfidence.stopSession(session.id);
}
```

## 4.2 Tipos públicos (normativos)

```ts
export type Coordinates = Readonly<{
  latitude: number;  // WGS-84 [-90,90]
  longitude: number; // WGS-84 [-180,180]
}>;
export type MotionState =
  | 'stationary' | 'walking' | 'running' | 'cycling'
  | 'vehicle' | 'unknown';
export type AccuracyAuthorization = 'precise' | 'approximate' | 'unknown';
export type FixSource = 'fresh' | 'recovered' | 'cached' | 'unavailable';
export type AssessmentStatus = 'available' | 'degraded' | 'unavailable' | 'cancelled';
export type EvidenceRelation = 'supports_inside' | 'supports_outside' | 'inconclusive';
export type AccuracyAgeReliability = 'monotonic' | 'estimated' | 'unreliable';

export interface Assessment {
  schemaVersion: 1;
  scoreModelVersion: 'heuristic-v1';
  requestId: string;
  sessionId: string | null;
  status: AssessmentStatus;
  source: FixSource;
  measuredPosition: Coordinates | null;  // NÃO estimar coordenadas na v1
  distanceMeters: number | null;
  radiusMeters: number;
  horizontalAccuracyMeters: number | null;
  effectiveUncertaintyMeters: number | null; // conservador, não CI de 95%
  locationAgeMs: number | null;
  ageReliability: AccuracyAgeReliability | null;
  permissionPrecision: AccuracyAuthorization;
  qualityScore: number;                  // INTEGER 0..100, NÃO porcentagem
  probabilityWithinRadius: null;        // reservado para eventual modelo calibrado
  evidenceRelation: EvidenceRelation;   // NÃO é autorização de entrega
  motion: {
    state: MotionState;
    confidence: 'high' | 'medium' | 'low' | 'unknown';
    continuousStationaritySupported: boolean;
    coverageStartAgeMs: number | null;
    gapsDetected: boolean;
  };
  scoreBreakdown: {
    accuracyFactor: number;
    freshnessFactor: number;
    movementFactor: number;
    consistencyFactor: number;
    rawScore: number;
    appliedCaps: string[];
  } | null;
  diagnostics: {
    reasonCodes: string[];
    phaseTrace: string[];
    initialFixTimedOut: boolean;
    recoveryAttempted: boolean;
    samplesReceived: number;
    uniqueSamplesRetained: number;
    samplesAnalyzed: number;
    outliersFlagged: number;
    durationMs: number;
    fallbackCause: string | null;
    simulatedLocationReported: boolean | null;
  };
}

export interface SessionOptions {
  mode: 'foreground';
  historyCapacity?: 10;       // v1 only supports 10
  collectionMode?: 'evaluations_only' | 'foreground_monitoring';
  // default evaluations_only: startSession apenas cria contexto em memória
  monitoringIntervalMs?: number;        // default 5000; apenas se monitoring ativo
  monitoringDistanceFilterMeters?: number; // default 10, plataforma pode ajustar
  maxMonitoringDurationMs?: number;     // default 900000 (15 min), capped
}
export interface EvaluateOptions {
  totalDeadlineMs?: number;                 // default 12000
  initialFixBudgetMs?: number;              // default 4500, capped to total
  recoveryWindowMs?: number;               // default 7000, capped to remaining
  maxFreshAgeMs?: number;                  // default 10000
  maxFallbackAgeMs?: number;               // default 60000
  maxHorizontalAccuracyMeters?: number;   // default 50
  enableMotionAssistance?: boolean;       // default true, graceful degradation
  allowHistoricalFallback?: boolean;     // default true
  maxPlausibleSpeedMps?: number;          // default 45 (config/heuristic)
}
export interface EvaluateRequest {
  sessionId?: string; // omitted -> ephemeral operation, no session history
  destination: Coordinates;
  radiusMeters: number;
  options?: EvaluateOptions;
  signal?: AbortSignal;
}

export interface LocationConfidenceApi {
  getCapabilities(): Promise<Capabilities>;
  getAuthorization(): Promise<Authorization>;
  requestLocationPermission(): Promise<Authorization>;
  requestMotionPermission(): Promise<Authorization>;
  startSession(options?: SessionOptions): Promise<{ id: string }>;
  clearHistory(sessionId: string): Promise<void>;
  stopSession(sessionId: string): Promise<void>;
  evaluateProximity(request: EvaluateRequest): Promise<Assessment>;
}
```

**Sem listeners de localização na fachada por enquanto:** para reduzir superfície pública, os eventos da sessão permanecem nativos. O example recebe o assessment no fim; se eventos de progresso forem realmente necessários, adicionar `subscribeDiagnostics` opt-in com payload sem coordenadas e testes de event emitter depois. Não adiantar arquitetura de stream público só para criar APIs.

## 4.3 Regras de validação

- `latitude`, `longitude`, `radiusMeters`, limites de timeout e accuracy precisam ser finitos, não `NaN`/`Infinity`.
- Latitude `[-90,90]` e longitude `[-180,180]` inclusive. `-0` é válido (normalizar `-0` opcionalmente); coordenada `[0,0]` pode ser **real** e nunca deve ser confundida com missing.
- `radiusMeters > 0`, máximo de negócio configurável e documentado; v1 sugere faixa [1, 100000] m para evitar uso inadvertido, sem impedir evolução mediante ADR.
- Timeout e janela precisam ser inteiros e dentro de limites seguros; `totalDeadlineMs` default 12000, min 1000, max 60000; `recoveryWindowMs` default 7000, min 0, max 20000; `initialFixBudgetMs` default 4500, min 0, max total; `historyCapacity` apenas 10. `collectionMode` default `evaluations_only` NÃO ativa GPS no startSession. `foreground_monitoring` é opt-in: `monitoringIntervalMs` default 5000 (min 1000, max 60000), distance filter default 10m, `maxMonitoringDurationMs` default 900000 (15 min) com teto configurável/validado na Fase 0. Intervalos são preferências ao SO, não cadência garantida.
- `maxFreshAgeMs` default 10000, `maxFallbackAgeMs` default 60000, ambos >=0; `maxFallbackAgeMs >= maxFreshAgeMs` ou erro de argumentos.
- `maxHorizontalAccuracyMeters > 0`; `maxPlausibleSpeedMps > 0`. Valores absurdos ou tipos inválidos são `InvalidArgumentError`, sem ativar sensor.
- `sessionId` inexistente -> `unknown_session`, sem iniciar coleta e sem cair para sessão global.
- `AbortSignal.aborted` antes de iniciar -> `status='cancelled'`, sem native call além de validação local.
- `startSession` sem autorização retorna falha estruturada de permissão, não pede permissão automaticamente.

## 4.4 Native DTO e transportes

**Fase 0:** gerar `NativeTransportSpec.md` verificando cada tipo no Codegen da versão instalada. Preferir campos primitivos (`double`, `boolean`, `string`, arrays de structs simples) e `null` somente onde suportado. Campos não representáveis viram DTO plano interno com campos opcionais simulados com `hasX`, convertidos na fronteira; sem string JSON gigante salvo necessidade comprovada e ADR. Evitar expor detalhes de JNI/ObjC++ à aplicação.

**Modelo nativo normalizado antes de entrar no core:**

```cpp
struct LocationSample {
  std::uint64_t sampleSequence;               // identidade intra-sessão
  double latitudeDeg;
  double longitudeDeg;
  double horizontalAccuracyMeters;
  double ageAtIngestMs;                       // idade estimada na ingestão
  std::int64_t monotonicIngestMs;             // relativo à sessão, mesma base
  bool ageReliable;
  bool approximatePermission;
  bool hasSpeed;
  double speedMetersPerSecond;                // válido apenas se hasSpeed
  bool hasCourse;
  double courseDegrees;
  enum class Source { Fused, CoreLocation, SystemCache } source;
  enum class MockFlag { Reported, NotReported, Unknown } mockFlag;
};

struct MotionInterval {
  std::int64_t startMonotonicMs;
  std::int64_t endMonotonicMs;
  enum class Kind { Stationary, Walking, Running, Cycling, Vehicle, Unknown } kind;
  enum class Confidence { High, Medium, Low, Unknown } confidence;
  bool hasReliableCoverage;
};
```

**Não utilizar `ageAtIngestMs` como relógio congelado:** ao avaliar em `nowMonotonicMs`, `ageNow = ageAtIngestMs + max(0, nowMonotonicMs - monotonicIngestMs)`. Os tempos relativos de sessão devem ser normalizados pela plataforma antes de chegar ao core. Se essa base for comprometida, declarar `ageReliable=false` e não usar em decisão de proximidade.

## 4.5 Erros, status e reason codes

**Erros de programação** (reject typed): `InvalidArgumentError`, violação de contrato interno, erro irrecuperável de Codegen na inicialização.  
**Falhas operacionais** (Assessment estruturado): permissão negada, GPS desligado, timeout, motion indisponível, app background, nenhuma amostra, perda de serviço, cancelamento.

Códigos públicos estáveis (snake_case):

```text
permission_not_determined
permission_denied
permission_restricted
precision_reduced
location_services_disabled
motion_permission_denied
motion_unavailable
motion_insufficient_coverage
motion_delayed
play_services_unavailable
platform_unsupported
app_backgrounded
foreground_required
initial_fix_timeout
recovery_window_timeout
overall_deadline_reached
native_provider_unavailable
location_accuracy_unavailable
location_invalid_coordinates
location_invalid_timestamp
location_stale
location_low_accuracy
location_age_unreliable
location_future_timestamp
clock_discontinuity
history_empty
history_only_inconclusive
samples_duplicate
samples_out_of_order
suspected_gps_jump
possible_mock_location
no_usable_fix
request_cancelled
session_stopped
busy
native_internal_error
```

**Norma:** `reasonCodes` não contém latitude/longitude, erro de SO em texto livre nem stack trace. Para sem localização: `measuredPosition=null`, `distanceMeters=null`, `effectiveUncertaintyMeters=null`, `qualityScore=0`, `evidenceRelation='inconclusive'`.

---

# 5. Política de aquisição, fallback e máquina de estados

## 5.1 Estados e eventos

```text
CREATED
  -> CHECK_CAPABILITIES_AND_PERMISSION
  -> REQUEST_INITIAL_FIX
      -> INITIAL_FIX_ASSESSMENT
          -> COMPLETE if eligible
          -> MOTION_EVIDENCE_LOOKUP if weak/timeout
              -> HISTORY_STATIONARY_CHECK
                  -> COMPLETE if admissible (continuity supported)
                  -> START_RECOVERY_WINDOW otherwise
              -> START_RECOVERY_WINDOW if moving/unknown/no history
                  -> INGEST_RECOVERY_FIXES
                      -> COMPLETE early when adequate candidate
                      -> HISTORY_EVALUATION at window timeout
  -> COMPLETE / DEGRADED / UNAVAILABLE / CANCELLED
```

**Eventos enviados ao C++ reducer:** `START`, `AUTH_STATE`, `INITIAL_FIX_RECEIVED`, `INITIAL_FIX_TIMEOUT`, `MOTION_EVIDENCE_RECEIVED`, `MOTION_EVIDENCE_UNAVAILABLE`, `RECOVERY_FIX_RECEIVED`, `RECOVERY_WINDOW_END`, `DEADLINE_REACHED`, `ABORT`, `APP_BACKGROUNDED`, `SESSION_STOPPED`, `NATIVE_ERROR`.

**Comandos retornados pelo C++:** `REQUEST_INITIAL_FIX`, `QUERY_MOTION`, `COLLECT_RECOVERY`, `STOP_COLLECTION`, `ASSESS_SNAPSHOT`, `FINISH_RESULT`, `FINISH_CANCELLED`, `FINISH_UNAVAILABLE`. Cada command possui `requestId`, `generation`, budget máximo e argumentos; side-effects não executados dentro do reducer.

**Deadline externo obrigatório:** coordinator Kotlin/Swift usa timer monotônico real e cancela listener mesmo quando a máquina não recebeu evento. Se o deadline vencer, pode rodar avaliação **síncrona e limitada com dados já em memória** antes de finalizar; não inicia sensores adicionais e sempre cumpre o limite de wall time acrescido só do custo mínimo de cleanup. Registrar tolerância no benchmark.

## 5.2 Orçamento de tempo e prioridades

| Etapa | Default inicial | Regra |
|---|---:|---|
| Operação inteira | 12000 ms | orçamento global rigoroso |
| Pedido inicial | até 4500 ms | usa max update age pequeno e valida fix real |
| Consulta rápida de motion disponível | até 500 ms | consulta já ativa/registrada; sem aguardar minutos |
| Janela de recovery | até 7000 ms | restante do budget global prevalece |
| Avaliar snapshot core | meta exploratória <10 ms para 10 amostras | não bloquear UI; medir aparelhos reais |

O orçamento é **compartilhado**, não 4500+500+7000 se ultrapassar 12000. Se motion indisponível, pular. Uma posição boa recuperada permite fim antecipado. Se 0 leituras novas e histórico vazio, `unavailable`.

## 5.3 Hierarquia de recuperação

1. **Fresh:** posição retornada pela API atual com metadata e idade aceitáveis; pode ser cache muito recente — sempre rotular idade real. Não confundir `getCurrentLocation()` com fix necessariamente novo.
2. **Motion:** consultar evidência previamente registrada ou query bounded; `stationary` só ajuda se cobre intervalo do fix até agora sem lacunas relevantes. Nunca pular recovery porque atividade `STILL` isolada chegou agora.
3. **Recovery:** usar updates de alta qualidade por janela curta, com cooldown e stop. Colecionar 0..N leituras, anotar fix e ordenar por timestamp normalizado; evitar busy loop.
4. **History:** analisar até 10 últimos fixes em janela de no máximo 120 s, e selecionar elegíveis até `maxFallbackAgeMs`. Distinguir `cached` de `recovered`. Não misturar contexto de usuários/sessões.
5. **Degraded/unavailable:** se só existe posição antiga/instável, pode retornar `degraded` com score baixo e relação inconclusiva; sem coordenada admissível retornar `unavailable`.

## 5.4 Não assumir “parado” pela posição

- Célula de motion isolada `stationary` **não cobre o passado**.
- Se o celular estiver parado enquanto entregador caminha sem ele, a evidência é sobre o **aparelho**, não sobre a pessoa.
- Activity Recognition pode atrasar, omitir, ter permission denied e fornecer categorias conflitantes.
- Se o último fix não tem idade temporal confiável, motion não o recupera automaticamente.
- Durante transição carro→parado, usar evidência de deslocamento no intervalo, não apenas estado final.
- Se dispositivo mexe sem locomoção (bolso, moto vibrando, elevador) isso não é prova geográfica.

## 5.5 Atualização de histórico

- Sessão `startSession()` é explícita e foreground; ring buffer começa vazio, cap=10. `collectionMode='evaluations_only'` apenas cria contexto e o histórico acumula fixes obtidos em avaliações. `collectionMode='foreground_monitoring'` (opt-in) registra atualizações ativas de localização e pode formar histórico antes do primeiro timeout, sob limites de frequência, duração e energia. O app escolhe esse modo conscientemente; nenhum rastreamento oculto.
- Buffer inclui fixes válidos sintaticamente mas imprecisos (para diagnostics), com flags; invalid data é contado mas não retido.
- Entrada ordenada por timestamp normalizado/captura; callbacks fora de ordem não substituem um fix melhor automaticamente.
- Ao exceder 10, remover fix cronologicamente mais antigo; pruning por idade >120 s.
- Fim de sessão, logout do app consumidor quando chamar `stopSession`, permission revoked e invalidation da bridge limpam ring buffer.
- Avaliação sem sessão usa um contexto efêmero e não herda outra sessão.
- Nunca emitir histórico bruto completo pela API pública; diagnostics retorna apenas contadores. A posição selecionada é exposta somente como resultado intencional.

## 5.6 Concorrência e ownership

**Preferido v1:** single-flight de aquisição por sessão com assinaturas: 2 requests podem compartilhar a observação de GPS, mas têm budgets, destinos e resultados próprios. A coleta usa `requestId`/`generation`; cada request controla sua própria avaliação; finalização de uma não encerra o fix de outra. Se isso não for implementável de maneira verificável em v1, usar fila/`busy` até implementar com segurança — registrar desvio da SPEC em ADR antes de codificar.

**Confinamento:** todos os comandos que mutam uma sessão C++ entram em executor serial por sessão; JNI pode ocorrer de threads diferentes, mas adapter marshals para a fila. Não manter ponteiro para objeto C++ depois de `stopSession`, evitar use-after-free ao cancelar/receber callbacks atrasados.

**Cancelamento:** `AbortSignal` cancelado -> mensagem `cancelRequest(requestId)`; callback tardio recebe generation antiga e é descartado. No iOS cancelar `Task` não remove automaticamente todos os delegates; adapter tem `stopUpdatingLocation` e termina continuations exatamente uma vez. Android garante `removeLocationUpdates` no finally.

---

# 6. Modelo de dados, tempos, geometria e análise C++

## 6.1 Validação de amostras

**Válida sintaticamente:** lat/lon finitos e na faixa, accuracy horizontal disponível e `>=0`, tempo de fix plausível, idade `>=0` e finita, speed se disponível `>=0` finita, course se disponível [0,360) (normalizar 360 se proveniente da plataforma). Lat/lon (0,0) é válido — nunca inferir missing por coordenada.

**Válida para decisão:** idade confiável, age <= maxFallbackAgeMs, permission state autorizado, ausência de incompatibilidades críticas. Accuracy acima do limite pode continuar no buffer, mas não se torna “alta qualidade” por agregação artificial.

**Tempo:** Android obter `Location.elapsedRealtimeNanos` + `SystemClock.elapsedRealtimeNanos`; no iOS `CLLocation.timestamp` é wall clock, calcular idade no recebimento e manter uptime local de ingestão. Atualizações de relógio, reboot, timestamp futuro, derivação inconsistente => `ageReliability='unreliable'`; nenhuma frescura artificial. Não comparar uptime Android com uptime iOS.

## 6.2 Distância Haversine

- WGS84 latitude/longitude em graus, radianos convertidos; usar raio médio terrestre explícito `R=6371008.8 m`, documentado como aproximação esférica para distâncias de delivery.
- `a = sin²(Δφ/2) + cos φ1 * cos φ2 * sin²(Δλ/2)`; aplicar clamp `a ∈ [0,1]`; `c=2 atan2(sqrt(a),sqrt(1-a))`; `distance=R*c`.
- `Δλ` normalizado em torno da dateline para estabilidade; comparar longitude +180/-180 corretamente; usar double.
- Distância esférica não mede trajeto por ruas, altitude, acesso a condomínio ou entrada real do endereço.

## 6.3 Deduplicação e outliers

**Dedup:** preferir identity de fix ou equivalência estrita (source, monotonic timestamp, coords, accuracy) dentro da sessão. Repetição de callback não aumenta consistência. `dt<=0` não participa de velocidade. Ordenar antes do outlier detector.

**Plausibilidade para par A,B:**

```text
d = haversine(A, B)
dt = (timestampB - timestampA) / 1000 s, require dt>0
uncertaintySlack = k * (accuracyA + accuracyB), initially k=1
allowed = maxPlausibleSpeedMps*dt + uncertaintySlack
suspectedJump = (d > allowed)
```

`k=1` é heurístico, não confidence interval calibrado. `dt` e origem devem ser confiáveis. Duas leituras ruins com ±200m cada podem impedir detectar um salto — **é aceitável ficar inconclusivo**, nunca reclassificar como boa leitura por falsa segurança.

**A-B-A:** B isolado distante e A/C temporalmente próximos com alta precisão → marcar B suspeito. **A-B-B-B:** movimento real possível, não rejeitar série coerente apenas por salto inicial. **A-B-A com GPS precisão grosseira:** evidência insuficiente para rejeição forte, apenas flags.

Outliers ficam em diagnóstico (contagem/razão) e podem permanecer no buffer como suspeitos; o CandidateSelector não os usa para resultado suportado sem corroborar. Não fazer média ingênua de latitude/longitude — especialmente dateline.

## 6.4 Movimento, continuidade e incerteza

**Motion intervals:** normalizar eventos para segmentos com início/fim, estado e nível de confiança. Cobertura total exige que todo o intervalo do fix até agora tenha evidência compatível, sem lacunas acima de limiar configurado (sugestão `maxMotionGapMs=2000`, hipótese), e sem interrupções por background/permission changes. Mesmo “stationary” contínuo não é prova: aplicar drift bound >0.

**Modelo conservador v1 (heurístico, não estatístico):**

```text
elapsedS = ageNowMs / 1000
movementSpeedBoundMps:
  stationary_with_continuous_high_coverage -> 0.5
  walking                                  -> 3
  running                                  -> 8
  cycling                                  -> 12
  vehicle                                  -> 45
  unknown/partial_coverage                 -> 45
movementAllowanceMeters = movementSpeedBoundMps * elapsedS
effectiveUncertaintyMeters = horizontalAccuracyMeters + movementAllowanceMeters
```

**Nota essencial:** esses números são limites *operacionais de projeto* e não velocidades físicas máximas garantidas. Um aparelho pode ser transportado excepcionalmente mais rápido. Para histórico com mudança de estado e cobertura completa, integrar segmento por segmento (`Σ bound(state_i) * dt_i`), sendo `unknown` nos gaps. Se origem só informa speed instantânea antiga, não projetar direção/posição. Versionar os limites e fornecer reason code de heurística.

**Movimento parado:** o tempo de imobilidade suportada deve cobrir o intervalo, não apenas o último evento. Se não cobrir, aplicar bound de `unknown` nos segmentos não observados e não reduzir uncertainty artificialmente.

**Não expandir coordenada:** v1 `measuredPosition` é sempre o fix capturado; incerteza é uma margem de possibilidade, não nova posição.

## 6.5 Seleção do candidato

Ordenação de preferência (regras testáveis):

1. Rejeitar sintaticamente inválidos, timestamps não confiáveis, sem autorização ou além de `maxFallbackAgeMs` para decisão.
2. Identificar duplicatas/outliers e confiabilidade da provenance.
3. Priorizar fix **fresco**, preciso e coerente; se um fix recente é ruim e histórico é bom mas antigo, comparar incerteza efetiva, não escolher automaticamente o mais recente nem o antigo.
4. Ordenar elegíveis por menor `effectiveUncertaintyMeters`, depois idade, depois accuracy, depois source; empate estável por `sampleSequence`.
5. Suspeita forte de salto não pode ser escolhida como “alta qualidade” sem corroborar; retorno `degraded/inconclusive` ou excluir de decisão se necessário.
6. Se somente sample histórica com movimento unknown, aplicam-se limites severos de incerteza e caps de score; nunca fingir que houve localização nova.

**Alerta de UX:** algoritmo que sempre escolhe menor erro pode selecionar fix muito antigo quando o usuário se move. O modelo de incerteza deve penalizar movimento/idade **antes** da seleção; caso contrário revisão falha.

## 6.6 Geometria do destino e evidência

`d=distância entre fix selecionado e centro`, `u=effectiveUncertaintyMeters`, `r=radiusMeters`.

```text
if no eligible fix / time unreliable / mock disallowed policy:
   evidenceRelation = inconclusive
else if u finite and d + u <= r and requiredQualityGatesMet:
   evidenceRelation = supports_inside
else if u finite and max(0, d - u) > r and requiredQualityGatesMet:
   evidenceRelation = supports_outside
else:
   evidenceRelation = inconclusive
```

`supports_inside` **não** equivale a certeza de presença física, mesmo se `d+u<=r`. Chamar de **evidência compatível**, não de “confirmado”. Exigir age <= configuredMaxAge e source admissível para relação suportada; dados degradados podem continuar inconclusivos independentemente de d/u. Score não substitui gates.

**Borda:** igualdade `d+u == r` é suportada dentro pela regra; igualdade `d-u == r` é inconclusiva (fora usa `>`). Testar com tolerância numérica epsilon documentada para floats próximos do limite.

## 6.7 QualityScore — regra determinística v1

O score é calculado somente para um candidato válido e com idade confiável. Inputs normalizados:

```text
accuracyFactor    = clamp(1 - horizontalAccuracyMeters / 100, 0, 1)
freshnessFactor   = clamp(1 - ageNowMs / 60000, 0, 1)
movementFactor    = clamp(1 - movementAllowanceMeters / 100, 0, 1)
consistencyFactor = 0.60 if only 1 eligible distinct sample;
                    0.80 if 2 coherent independent-enough distinct fixes;
                    1.00 if >=3 coherent fixes;
                    0.30 if conflicting evidence/no strong consensus
rawScore = roundHalfUp(100*(0.40*accuracyFactor +
                           0.25*freshnessFactor +
                           0.20*movementFactor +
                           0.15*consistencyFactor))
score = clamp(rawScore,0,100)
```

Aplicar o **menor cap** aplicável, registrando todos:

| Situação | Cap sugerido | Por quê |
|---|---:|---|
| `source=cached` | 60 | histórico não é fix novo |
| precisão reduzida pelo SO | 30 | não simular alta qualidade |
| movimento unknown + fix não fresco | 40 | crescimento de incerteza desconhecido |
| `effectiveUncertaintyMeters >= 100` | 35 | muito ampla para raio pequeno |
| `effectiveUncertaintyMeters >= 300` | 15 | evidência muito fraca |
| only single fix, no corroboration | 75 | sem consistência adicional |
| suspeita relevante de GPS jump | 20 | posição possivelmente incorreta |
| localização marcada como simulada | 0 na política demo, configurável | anti-spoofing não garantido |
| idade não confiável/sem fix/invalid | 0 | ausência de evidência utilizável |

**Propriedades mandatórias:** aumentar age/accuracy de uma mesma amostra mantendo motion/evidência/metadata constantes não eleva score; ausência de dados ou perda de motion coverage não aumenta score; adicionando outlier não pode aumentar consistência; resultado não ultrapassa caps. Arredondamento consistente (`roundHalfUp` em C++, explicitamente testado; não usar diferenças de arredondamento por plataforma).

**Problema do score por weighted sum:** os fatores são correlacionados e pesos não calibrados. Documentar isso no README, não mostrar `%` na UI, só `qualityScore: 74/100` com label "qualidade dos dados (índice experimental)". Se calibrar mais tarde, usar outro campo/versão, sem trocar silenciosamente semântica do score.

## 6.8 Propriedades e tolerâncias de métricas

- `qualityScore` inteiro 0..100; finite.
- Distância/esfera double finite; raio maior que 0; incerteza >= accuracy.
- Age nunca negativo; futuridade do timestamp além tolerância marcada como unreliable.
- Para mesma amostra e estado: `age(t+δ) >= age(t)` e `effectiveUncertainty(t+δ) >= effectiveUncertainty(t)`.
- Alterar apenas precisão original para pior não melhora score/relacionamento.
- Não elevar score pela duplicação do mesmo fix.
- Não criar distância útil quando não há posição.

---
# 7. Implementação Android — contratos de plataforma

## 7.1 Fonte de posição

- Usar `FusedLocationProviderClient` do Google Play services quando presente; verificar dependência/versionamento via Gradle. Não alegar que Play Services é sempre disponível. Sem Play Services, v1 pode sinalizar capability unavailable (fallback via `LocationManager` separado/ADR futuro).
- `getCurrentLocation(CurrentLocationRequest, CancellationToken)` com estratégia de `maxUpdateAgeMillis=0` quando adequado, **mas sempre conferir timestamp e accuracy recebidos**, pois “current” não garante fix produzido no exato instante da chamada. O API pode retornar `null`.
- `requestLocationUpdates(LocationRequest, LocationCallback, ...)` na recovery window; remover listener em `finally`, timeout, permissão revogada, background, cancel e stopSession. Não reiniciar sensor sem limite.
- Leitura com `hasAccuracy()==false`: inválida para posição admissível; pode constar em contadores sem coordenada nos logs.
- Idade preferencialmente de `Location.getElapsedRealtimeNanos()` vs `SystemClock.elapsedRealtimeNanos()`; nunca derivar duração de `System.currentTimeMillis()` se monotonic existe.
- `Location.isMock()` nas versões suportadas; verificar compatibilidade e pré-API, inclusive flag unknown; um mock marker é evidência parcial e não garante detecção de spoof.
- `getLocationAvailability()` pode ajudar diagnostics, mas é **estimativa e não prova** de que fix será obtido.
- Altitude e velocidade do sistema são opcionais; speed horizontal deve ser validada, não utilizada como prova de imobilidade contínua.

## 7.2 Permissões, precisão e activity

- Permissões de localização: coarse/fine conforme use case e manifests do host. Em Android 12+, usuário pode conceder approximate mesmo que fine seja pedida; distinguir estado real e eventuais reinícios de processo.
- `ACTIVITY_RECOGNITION` apenas quando motion assistance foi explicitamente habilitada e solicitada pelo app. Sem ela, motion vira `unknown` sem exception fatal. `requestMotionPermission()` é a entrada explícita do host, usando o mecanismo de runtime permission conforme Android API level.
- `ActivityRecognitionClient` fornece transitions/updates com atraso possível; **não confundir** classificação `STILL` com posição fixa ao longo do intervalo.
- `ACCESS_BACKGROUND_LOCATION` **fora do escopo**. Operação foreground-only; aceitar suspensão/restrição pelo SO e retornar diagnóstico.
- Não exibir Android Settings resolution dialog automaticamente; `SettingsClient` pode futuramente expor `actionRequired` e exigir ação explícita do app.

## 7.3 Threads, lifecycle e C++

- `NativeEvaluationCoordinator` pode usar coroutines no escopo do módulo; múltiplos callbacks apenas alimentam o executor serial da sessão. Evitar bloquear main thread/JSI synchronous call para acquisition.
- JNI chama C++ em worker serial e entrega resultados como DTO. Usar local/global JNI references corretamente e evitar manter objetos Kotlin vivos por ponteiro nativo sem ownership explícito.
- `onCatalystInstanceDestroy`/invalidation equivalente conforme RN instalado deve cancelar/coletar sessões. Identificar API real da versão antes de codificar.
- Se app background, cancelar a avaliação ativa (v1); ao voltar, exige reinício/novo evaluate explícito. Não deixar tracking ativo silenciosamente.
- Testar Doze/battery saver como limitação documentada, não tentar contornar políticas do sistema.

## 7.4 Manifests e publish

- O host deve receber orientação explícita de permissões; a biblioteca não deve adicionar permissões excessivas no manifest por conveniência.
- Documentar mínimo Android API e arquitetura suportada **após build**; Gradle prefab/CMake/NDK, ABIs arm64-v8a e x86_64 pelo menos no fluxo CI; considerar armeabi-v7a apenas se validado.
- Testar R8/ProGuard consumer rules e shrink minify em release; métodos JNI precisam continuar resolvendo após obfuscation.

---

# 8. Implementação iOS — contratos de plataforma

## 8.1 Localização e permissões

- Usar `CLLocationManager` com delegate/ownership seguro. `desiredAccuracy` é **objetivo** e não garantia; `horizontalAccuracy < 0` indica informação inválida. `timestamp` é data de parede; registrar idade no recebimento e clock monotônico local.
- `requestLocation()` one-shot ou `startUpdatingLocation()` + `stopUpdatingLocation()` para recovery; decisão na Fase 0 documenta latência/risco. Permissão apenas `whenInUse` na v1; sem background entitlement.
- `accuracyAuthorization` indica `fullAccuracy` ou `reducedAccuracy` onde suportado. Não pedir full accuracy automaticamente. Expor recomendação configurável para o app, e uma chamada explícita futura a `requestTemporaryFullAccuracyAuthorization` se necessário com chave de purpose correta.
- `NSLocationWhenInUseUsageDescription` no app exemplo; biblioteca explica ao app hospedeiro que deve adicionar descrição. Sem key ou com API não disponível, documentar/retornar erro controlado quando possível.
- `CLLocationSourceInformation` pode identificar simulação de software/acessório onde disponível, mas não autentica presença. Não tratar `false` como prova antifraude.

## 8.2 Movimento via Core Motion

- `CMMotionActivityManager.isActivityAvailable()`/authorization e `NSMotionUsageDescription`; não fazer query sem propósito/consentimento. iOS não oferece necessariamente um request genérico de permissão de movimento igual ao Android: a implementação de `requestMotionPermission()` deve executar uma consulta mínima explícita e documentada, capaz de disparar a autorização quando cabível; `evaluate` e `getCapabilities` não podem disparar essa primeira autorização por conta própria. Verificar comportamento real no device e relatar se o status é `not_determined`.
- Stream iniciado durante sessão opt-in pode dar evidência corrente; historical query `queryActivityStarting(from:to:...)` pode **atrasar minutos** — não esperar essa query para responder em deadline de 12s. Se não houver cobertura, `unknown`.
- Confiança `low/medium/high` do Core Motion tem semântica específica. Convertê-la para enum de evidência, não uma probabilidade percentual.

## 8.3 Swift ↔ Objective-C++ ↔ C++

- Configuração de bridging header/módulo compatível com scaffold; Objective-C++ compila `.mm` e inclui headers C++; Swift não importa C++ arbitrariamente assumindo interoperabilidade automática.
- Manter wrappers finos de conversão (sem Haversine, score ou outlier em Swift/ObjC++).
- Garantir que objetos Swift/manager vivam durante a callback e sejam liberados em stop/invalidation. Evitar retain cycles no delegate e nas closures; `Task` cancellation também precisa parar manager/continuations.
- Se chamada de Core Location precisar de thread/run loop apropriado, criar/gerenciar o manager lá e não atravessar threads arbitrariamente; usar @MainActor somente para OS interop, mandar processamento de C++ a uma fila não-main.
- Validar iOS Simulator x86_64/arm64 conforme runner e device arm64. Testar módulo linkado no app real de exemplo, não só XCTest de C++.

---

# 9. Performance, memória, bateria, observabilidade e privacidade

## 9.1 Performance: o que realmente medir

- `acquisitionLatencyMs` (espera do SO), `motionLatencyMs`, `recoveryWindowDurationMs`, `coreEvaluationMs` (CPU C++), `bridgeTransferMs`, `endToEndMs` e percentis p50/p95/p99.
- Distinguir **latência da aquisição GPS** do tempo de cálculo em C++. Não publicar alegação de FPS ou “x vezes mais rápido” sem baseline.
- Benchmark de 1, 10, 100, 1000 fixes em core host (seus números são sintéticos) e 10 fixes em device real; medir overhead JNI/ObjC++ separadamente e reportar warm-up/amostras/carga.
- Preferir **batch de fixes** para não fazer JNI em loop por amostra; não forçar mega-array se atualizações são raras. Medir antes de otimizar.
- `RecoveryPolicy::onEvent` e `evaluateSnapshot` são CPU-bound síncronos e devem ter limite de input (cap=10), garantindo custo bounded.
- Não bloquear main thread com `join`/mutex/async wait. Coroutines/tasks sempre cancellable. Run Thread Sanitizer/Address Sanitizer no host se houver suporte.

## 9.2 Energia

- Default coleta on-demand (`evaluations_only`); sessão `foreground_monitoring` opt-in pode observar posição em foreground com frequência moderada/duração bounded, para formar histórico antes do primeiro pedido. Não prometer consumo específico de mAh antes de medir.
- Ajustar accuracy ao contexto e desligar updates assim que fix útil surgir; recuperar só uma vez por avaliação, sem retry infinito. Distinguir permission prompts de energy optimization.
- Perfil real mínimo: smartphone Android intermediário, iPhone físico, 15 minutos em cenário estático/andando/com sinal fraco, comparar baseline de APIs nativas quando possível, registrar ambiente e ferramentas usadas.

## 9.3 Privacidade e threat model

**Ativos:** localização exata, trajetória, horários de entrega, inferências de movimento.  
**Ameaças:** vazamento por logs, persistência indevida, histórico atravessando usuários, mocks/spoofing, event handlers sobrevivendo ao logout, app com permissões mais amplas do que necessário.

**Controles:** sem rede, sem persistência, `clearHistory`, `stopSession`, sem coordinates nos reasonCodes/crash/analytics por default, permissões explícitas e sessão foreground limitada. Never upload location. Host decide consentimento e política de retenção. Documentar que nenhum método comprova presença física ou previne fraude deliberada.

**Logs:** `requestId`, fase, contagem, duração e reason code; no release nenhuma latitude/longitude precisa em logs. Debug opt-in explicitamente informado, preferir fixtures/posições fictícias. Usar CI secrets padrão sem enviar nenhum dado real.

## 9.4 Idempotência e estabilidade da API

- `stopSession` repetido deve ser no-op seguro ou status `already_stopped` documentado; contrato escolher **no-op**.
- `clearHistory` idempotente.
- Repetição de `cancelRequest` não resolve duas vezes.
- Quando JS reexecuta avaliação, cada chamada usa novo requestId; `requestId` não é coordenada/PII.
- Exceptions internas não vazam mensagens de APIs privadas; mapear erros conhecidos e preservar stack só em debug local.

---

# 10. Testes: estratégia, fixture contract e pirâmide

## 10.1 Camadas de teste e ferramentas

| Nível | Ferramenta sugerida | Verificação |
|---|---|---|
| C++ unit/fixture | CMake + GoogleTest/Catch2 (escolher uma e fixar) | geodesia, outliers, history, uncertainty, score, state reducer |
| C++ property/metamorphic | generator determinístico/seed fixa | invariantes de score, geometria, tempo, sequências |
| Android native | JUnit + fake sources; instrumentação Play Services quando possível | permissions, lifecycle, callbacks, JNI, request cancellation |
| iOS native | XCTest + fake CoreLocation/CoreMotion ports | delegate, cancel, timeout, ObjC++ wrapper, lifecycle |
| TS unit/contract | Jest ou Vitest conforme scaffold | API pública, input validation, DTO mapper, AbortSignal |
| React Native example | integration/e2e com mocks e devices | fluxo de avaliação e UI diante de falhas |
| CI release | GitHub Actions/Linux Android + macOS iOS | build, linking, Codegen, archive, R8, package tarball |
| Manual field | Android e iPhone físicos | GPS indoor/outdoor/túnel/approximate/motion/bateria |

**Determinismo obrigatório:** fake clock monotônico, fake locations/motion com offsets, sem `sleep(7000)`, sem GPS real em unit tests. Todos os test IDs abaixo devem existir como cases nomeados ou mapeados para fixture parametrizada. Para fixtures C++ usar estrutura JSON apenas como dados/serialização nos testes; core não precisa importar JSON library.

## 10.2 Formato de fixture

```json
{
  "id": "REC-005",
  "sessionTimeMs": 100000,
  "destination": {"latitude": -20.8117, "longitude": -49.3768},
  "radiusMeters": 100,
  "permission": "precise",
  "fixes": [{
    "id": 1,
    "latitude": -20.8117,
    "longitude": -49.3768,
    "accuracyMeters": 10,
    "ageAtIngestMs": 2000,
    "monotonicIngestMs": 99000
  }],
  "motionIntervals": [],
  "nativeEvents": [],
  "expected": {
    "status": "available",
    "source": "fresh",
    "qualityScoreMin": 1,
    "qualityScoreMax": 100,
    "probabilityWithinRadius": null,
    "mustHaveReasonCodes": []
  }
}
```

**Fixture quality rules:** se `expected` não tiver score exato, justificar tolerância/intervalo; para score deterministic-v1 exigem-se **também** fixtures com score exato calculado pela regra. Golden fixtures não devem ser atualizadas apenas para “deixar teste verde” sem ADR/versão da fórmula.

## 10.3 Definição de sucesso/falha dos testes

- `PASS` se expected fields e side-effects observados correspondem; `FAIL` se erro de status, score/invariante, resource leak, falso callback ou falta de cleanup.
- Teste de timeout precisa observar **nenhum listener ativo após finalizar**, mesmo quando assessment é degraded.
- Para Android/iOS distinguir `unit mocked`, `simulator`, `real device`; não considerar emulador prova de sensores físicos.
- Mocks de localização mock/spoof devem verificar metadados, sem alegar detecção universal.
- No CI, suites nativas C++ são gate obrigatório; integration release iOS/Android são gate de publicação; testes físicos são plano de validação manual documentado.

---
# 11. Catálogo normativo de testes extremos e cenários de sucesso

**Formato:** o texto de cada caso expressa `Given / When / Then` em duas colunas; o agente deve transformá-lo em testes com as fixtures e asserts exatos, incluindo side-effects quando aplicável. Os IDs são estáveis e podem ser usados nas PRs e no changelog.

## 11.1 GEO — geodesia e bordas numéricas — 26 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| GEO-001 | **Coordenadas iguais.** A=(0,0), B=(0,0) | Haversine exatamente 0 m; coordenada zero aceita |
| GEO-002 | **Um grau de latitude no equador.** A=(0,0), B=(1,0) | Distância aproximadamente 111195,08 m com tolerância ±2 m |
| GEO-003 | **Antimeridiano curto.** A=(0,179.999), B=(0,-179.999) | Distância ~222 m, não circunferência terrestre |
| GEO-004 | **Longitude +180 versus -180.** A=(0,180), B=(0,-180) | Distância 0 m |
| GEO-005 | **Próximo ao Polo Norte.** A=(89.999,0), B=(89.999,180) | Distância pequena e finita |
| GEO-006 | **Antípodas.** A=(0,0), B=(0,180) | Distância finita próxima de pi*R; sem NaN |
| GEO-007 | **Latitudes simétricas.** A=(10,20), B=(-10,20) | Resultado simétrico d(A,B)=d(B,A) |
| GEO-008 | **Mesma longitude.** A=(20,30), B=(21,30) | Resultado positivo e finito |
| GEO-009 | **Mesma latitude.** A=(20,30), B=(20,31) | Resultado positivo e finito |
| GEO-010 | **Latitude +90.** A=(90,0), B=(89,0) | Aceito e finito |
| GEO-011 | **Latitude -90.** A=(-90,0), B=(-89,0) | Aceito e finito |
| GEO-012 | **Latitude 90.00001.** Entrada malformada | InvalidArgumentError, nenhum sensor iniciado |
| GEO-013 | **Longitude 180.00001.** Entrada malformada | InvalidArgumentError, nenhum sensor iniciado |
| GEO-014 | **NaN em latitude.** Entrada malformada | InvalidArgumentError, sem JNI |
| GEO-015 | **Infinity em raio.** Entrada malformada | InvalidArgumentError, sem JNI |
| GEO-016 | **Raio zero.** radiusMeters=0 | InvalidArgumentError |
| GEO-017 | **Raio negativo.** radiusMeters=-1 | InvalidArgumentError |
| GEO-018 | **Raio 100000.** radiusMeters=100000 | Aceito conforme limite documentado |
| GEO-019 | **Raio 100001.** radiusMeters=100001 | InvalidArgumentError conforme limite v1 |
| GEO-020 | **Distância na borda interna.** d+u=r exatamente | supports_inside, se outros gates válidos |
| GEO-021 | **Distância na borda externa.** d-u=r exatamente | inconclusive, não supports_outside |
| GEO-022 | **Fora estrito.** d-u>r com epsilon controlado | supports_outside se gates válidos |
| GEO-023 | **Incerteza abraça borda.** d-u<r e d+u>r | inconclusive |
| GEO-024 | **Simetria aleatória.** 1000 pares de coordenadas válidas seed fixa | d(A,B)=d(B,A) tolerância 1e-7 relativa |
| GEO-025 | **Monotonicidade local da distância.** Fix constante e destino deslocado em pequeno trecho geodésico | Distância calculada muda continuamente sem saltos espúrios |
| GEO-026 | **Negative zero.** lat=-0, lon=-0 | Aceito como (0,0); não considerado missing |

## 11.2 VAL — validade, timestamps e proveniência — 26 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| VAL-001 | **Fix sem accuracy Android.** hasAccuracy=false | Não elegível; reason location_accuracy_unavailable |
| VAL-002 | **Fix accuracy 0.** Accuracy=0 finita | Sintaticamente válido; sem garantia de GPS exato |
| VAL-003 | **Fix accuracy -1 iOS.** horizontalAccuracy=-1 | Descartado; reason invalid accuracy |
| VAL-004 | **Fix accuracy NaN.** Leitura malformada | Descartada antes de qualquer aritmética |
| VAL-005 | **Latitude recebida 91.** Leitura malformada | Descartada e conta em diagnostics |
| VAL-006 | **Longitude recebida -181.** Leitura malformada | Descartada e conta em diagnostics |
| VAL-007 | **Timestamp futuro +2min.** Fix marcado como futuro acima tolerância | Age unreliable; score 0 |
| VAL-008 | **Timestamp futuro pequeno.** Dentro tolerância documentada | Clampar idade a 0 apenas com reason e regra explícita |
| VAL-009 | **Android monotonic normal.** Fix age=2s, agora avança 3s | Age computada aumenta para 5s |
| VAL-010 | **Android wall clock muda.** Fix monotonic válido e relógio de parede retrocede 1h | Age permanece estável |
| VAL-011 | **Android elapsed negativo.** Fix monotonicIngest no futuro | Age unreliable, sem score positivo |
| VAL-012 | **Android reboot entre fixes.** Sessão/processo descontinuado | Limpar sessão antiga; não comparar monotonic pré/pós reboot |
| VAL-013 | **Timestamps iguais distintas posições.** dt=0 | Não dividir por zero; marcar conflito/out-of-order |
| VAL-014 | **Callback antigo após fix novo.** Sequence chega invertida | Histórico ordenado; resultado não retrocede automaticamente |
| VAL-015 | **iOS clock salto.** Mudança manual de horário entre timestamps | Age unreliable; sem confiança artificial |
| VAL-016 | **iOS fix antigo.** CLLocation timestamp 5 minutos atrás | Não apresenta como fresh |
| VAL-017 | **iOS didUpdateLocations contém lista antiga e nova.** Batch com idades distintas | Cada fix mantém sua idade; nova é candidata |
| VAL-018 | **Permissão full accuracy mas erro 500m.** Leitura muito imprecisa | Não ganha status alta qualidade por permission |
| VAL-019 | **Permissão approximate e accuracy 5km.** Dado formalmente válido | Degraded/inconclusive; cap approximate |
| VAL-020 | **Speed negativa.** hasSpeed=true, value=-1 | Ignorar speed, metadado inválido |
| VAL-021 | **Bearing 361 graus.** HasCourse true inválido | Normalizar/rejeitar course conforme contrato, não quebrar avaliação |
| VAL-022 | **Sem speed/bearing.** Flags ausentes | Não inferir 0 m/s nem direção |
| VAL-023 | **Mock flag true.** SO marcou simulado | reason possible_mock_location, policy cap definida |
| VAL-024 | **Mock flag unknown.** Flag indisponível | Não inferir false nem garantia antifraude |
| VAL-025 | **Source is cached.** API current entregou fix com idade permitida mas cache | Rotular origem/idade real, não fresh inventado |
| VAL-026 | **Zero zero real.** Fix lat0 lon0 timestamp válido | Posição mantida, não null |

## 11.3 HIS — ring buffer, dedup e seleção — 26 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| HIS-001 | **Sessão nova sem fixes.** startSession | Buffer zero, capacity 10 |
| HIS-002 | **Primeiro fix.** Uma posição sintaticamente válida | Buffer size 1 |
| HIS-003 | **Dez fixes.** Dez timestamps únicos | Buffer size 10 |
| HIS-004 | **Décimo primeiro fix.** 11 fixes válidos | Mais antigo removido, size 10 |
| HIS-005 | **Pruning por idade.** Fix >120s em buffer | Removido sem exceder capacity |
| HIS-006 | **Callback duplicado idêntico.** Mesmo sampleSequence/source/timestamp | Não duplica nem eleva consistency |
| HIS-007 | **Mesmo local em timestamps distintos.** GPS parado com timestamps distintos | Mantém fixes distintos; sem alegar independência estatística |
| HIS-008 | **Fix inválido.** Lat inválida | Não armazenado; count invalid atualizado |
| HIS-009 | **Fix impreciso.** Accuracy 500 m mas dado válido | Mantém para diagnostics, não automaticamente elegível |
| HIS-010 | **Sessão A e sessão B.** Duas sessões isoladas | Fix de A não aparece em B |
| HIS-011 | **Fim de sessão.** stopSession | Zera memória e referências da sessão |
| HIS-012 | **Limpar duas vezes.** clearHistory repetido | Sem crash nem residual |
| HIS-013 | **Stop duas vezes.** stopSession repetido | Idempotente |
| HIS-014 | **Reinício de sessão mesmo usuário.** new sessionId | Histórico não é reaproveitado |
| HIS-015 | **Logout e clear.** Host chama stopSession | Nenhuma amostra anterior permanece |
| HIS-016 | **Callback velho com generation anterior.** Após stop, novo sessionId | Ignorado; não corrompe buffer novo |
| HIS-017 | **Ordem invertida de callbacks.** Fix timestamps 10, 8, 9 | Histórico ordenado por origem temporal, não ordem de recebimento |
| HIS-018 | **Candidato antigo exato e novo ruim.** Antigo 50s/5m, novo 1s/200m | Selecionar segundo incerteza efetiva/gates, nunca menor accuracy bruta por padrão |
| HIS-019 | **Candidato novo exato e antigo ruim.** Novo 2s/5m, antigo 40s/200m | Preferir novo se elegível |
| HIS-020 | **Todos fixes muito antigos.** Ages >maxFallbackAgeMs | Sem posição admissível para decisão |
| HIS-021 | **Somente 1 fix.** History count=1 | Score cap single_fix; consistency 0.60 |
| HIS-022 | **Somente 2 coerentes.** Dois fixes diferentes coerentes | Consistency 0.80 conforme modelo |
| HIS-023 | **3 coerentes.** 3 fixes válidos e temporalmente consistentes | Consistency 1.00 conforme modelo |
| HIS-024 | **Dado repetido 3x.** Um fix em 3 callbacks | Consistency permanece single_fix |
| HIS-025 | **Mais de 10k callbacks.** Input prolongado em sessão | Memória bounded, tamanho <=10, sem degradação crescente |
| HIS-026 | **Fim do processo.** Process death | Sem restauração implícita de histórico |

## 11.4 JMP — saltos, falsos positivos e movimento real — 24 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| JMP-001 | **A-B-A saltos gigantes precisos.** A e C próximos, B 450m em 2s accuracy 5m | B sinalizado suspected_jump, não escolhido como corroborado |
| JMP-002 | **A-B-B-B coerente.** Mudança inicial de área e B replicado com timestamps progressivos | Não eliminar sistematicamente movimento real; avaliação cautelosa |
| JMP-003 | **A-B somente.** Uma única discrepância abrupta | Sinalizar suspeita, não declarar B falsa com certeza |
| JMP-004 | **A-B-A com erro GPS ±300m.** Grande accuracy cobre deslocamento | Não declarar salto impossível; classificação inconclusiva |
| JMP-005 | **Velocidade limite exato.** d=maxV*dt + accuracies | Não sinalizar por regra estrita `>` |
| JMP-006 | **Velocidade acima limite.** d>maxV*dt + accuracies | Suspected jump |
| JMP-007 | **Tempo delta zero.** dt=0 | Não dividir por zero, marcar conflito |
| JMP-008 | **Tempo delta negativo.** Fix recebido out-of-order | Ordenar antes de comparar; sem velocidade negativa |
| JMP-009 | **Velocidade real de moto.** Sequência ~15m/s com accuracy 5m | Não marcar salto por limite default 45m/s |
| JMP-010 | **Corrida elevada.** Sequência 6m/s com motion running | Não marcar salto default |
| JMP-011 | **Teleporte de 4km em 1s.** accuracy 5m ambas | Sinalizar outlier |
| JMP-012 | **Motorista retorna ao ponto inicial.** Série ida e volta com passagens intermediárias | Não classificar como A-B-A isolado |
| JMP-013 | **Indoor multipath.** Fix oscilando entre duas ruas | Maior incerteza/inconclusive, sem high score |
| JMP-014 | **Candidato isolado ruim no fim.** Último fix sozinho deslocado 1km | Não substituir boa série anterior por outlier |
| JMP-015 | **Vários fixes pulando juntos.** Cluster distinto consistente após deslocamento longo | Permitir mudança verdadeira após corroborar, mas não “autenticar” novo cluster |
| JMP-016 | **Longitude cruzando dateline.** 179.999 para -179.999 | Não tratar como salto global |
| JMP-017 | **Polo Norte.** Diferença de longitude ampla em alta latitude | Sem falso salto absurdo |
| JMP-018 | **Speed API elevada mas coords iguais.** Metadados conflitantes | Não tomar speed como verdade absoluta, reason inconsistência |
| JMP-019 | **Dois provedores discordam.** Fixes com precisões e fontes diversas | Seleção por evidência, razão de conflito |
| JMP-020 | **Interpolação proibida.** Só A antigo e B suspeito | Não inventar coordenada intermediária |
| JMP-021 | **Outlier adicionado a série boa.** 3 coerentes + 1 ruim | Quality não aumenta |
| JMP-022 | **Duplicação de outlier.** Outlier repetido 10x | Não virar novo cluster corroborado |
| JMP-023 | **Salto com dt=1ms.** Distância >limite provável | Sem crash/overflow, suspeita |
| JMP-024 | **Variação minúscula GPS.** Distância <1m accuracy10m | Sem flag salto |

## 11.5 MOT — movimento e cobertura temporal — 25 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| MOT-001 | **Stationary corrente e cache de 60s.** Apenas evento STILL agora | Não considerar imobilidade durante 60s |
| MOT-002 | **Stationary contínuo integral.** Evidência alta 0..30s sem gap | Aplicar bound stationary 0.5m/s, com cap de histórico |
| MOT-003 | **Stationary com gap no meio.** Gap >2s entre eventos | Parte gap tratada unknown; coverage false |
| MOT-004 | **Stationary até há 10s, agora moving.** Trajetória mudou | Não recuperar cache como parado |
| MOT-005 | **Vehicle durante 10s.** Fix antigo 10s e vehicle | Movement allowance 450m com bound default |
| MOT-006 | **Walking 5s.** Fix antigo 5s e walking | Movement allowance 15m com bound default |
| MOT-007 | **Running 5s.** Fix antigo 5s e running | Movement allowance 40m default |
| MOT-008 | **Cycling 5s.** Fix antigo 5s e cycling | Movement allowance 60m default |
| MOT-009 | **Unknown 5s.** Sem motion evidência | Movement allowance 225m default |
| MOT-010 | **Motion não permitido.** Permission denied | Continua recuperação sem motion, sem prompt automático |
| MOT-011 | **Motion não suportado.** Hardware ausente | unknown + reason motion_unavailable |
| MOT-012 | **Motion atrasado.** Event timestamp após deadline | Ignorar avaliação concluída; não mutar resultado |
| MOT-013 | **Motion order invertida.** Eventos delivered out-of-order | Ordenar intervalos e detectar ambiguidades |
| MOT-014 | **States conflitantes.** STILL e VEHICLE no mesmo intervalo | Unknown no conflito; não escolher o mais favorável |
| MOT-015 | **Movement state no passado mas sem coverage.** último evento WALKING 30s atrás | Não extrapolar como evidência contínua sem limite |
| MOT-016 | **Stationary e GPS muito impreciso.** accuracy 1000m | Motion não torna fix preciso |
| MOT-017 | **Stationary e GPS 3min antigo.** age > maxFallback | Fix não elegível para decisão |
| MOT-018 | **Pessoa caminha sem celular.** Celular ficou estacionário | Documentar motion reflete aparelho, não pessoa |
| MOT-019 | **Elevador em prédio.** Atividade imprecisa e posição próxima | Sem prova de porta/andar; incerteza permanece |
| MOT-020 | **Veículo estacionou agora.** Vehicle→Stationary há 2s, fix de 30s | Considerar movimento anterior, não salvar fix antigo por estado final |
| MOT-021 | **Motion callback negativo.** Erro do sensor | Fallback unknown, sem exceptions não tratadas |
| MOT-022 | **Activity confidence low.** STILL low confidence | Não habilitar cobertura alta |
| MOT-023 | **Activity confidence high mas gap.** STILL high, falta intervalo | Coverage false, unknown para gap |
| MOT-024 | **Permissão revogada no meio.** Motion deixa de estar autorizado | Não manter último estado como prova atual |
| MOT-025 | **Reentrada foreground.** Motion session reiniciada | Cobertura anterior não transportada sem prova |

## 11.6 REC — sucesso/fallback e timeout — 29 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| REC-001 | **Fix excelente na primeira leitura.** Age 1s accuracy 5m | available/fresh, recoveryAttempted=false, encerra cedo |
| REC-002 | **Fix mediano dentro dos limites.** Age 5s accuracy 40m | Aceitar conforme gates e avaliar raio |
| REC-003 | **Fix inicial impreciso, recovery bom.** Fix 500m depois 8m no recovery | recovered; candidato melhor e cleanup |
| REC-004 | **Fix inicial timeout, 3 updates bons.** No initial; stream 3 fixes coerentes | recovered; score finito e diagnostics initialFixTimedOut=true |
| REC-005 | **Fix inicial timeout, nenhum update.** Sem histórico | unavailable, score 0 e no_usable_fix |
| REC-006 | **Fix inicial timeout, histórico recente estacionário.** Histórico 10s/8m + coverage completa | Pode retornar cached degraded/assessment explicável |
| REC-007 | **Fix inicial timeout, histórico recente em movimento.** Fix 30s ago + vehicle | Não afirmar proximidade precisa; incerteza cresce |
| REC-008 | **Fix impreciso e motion desconhecido.** accuracy 200m e motion unknown | Coleta curta permitida; fallback de history se necessário |
| REC-009 | **Recovery primeira atualização excelente.** Evento bom em t+1s | Stop early; não esperar 7s completos |
| REC-010 | **Recovery com updates ruins apenas.** accuracy >limite e history ruim | degraded/inconclusive ou unavailable; sem high quality |
| REC-011 | **Recovery não entrega callback.** Window timer expire | Retorno limitado no tempo, listeners zero |
| REC-012 | **Deadline global durante initial.** t=12s before any fix | overall_deadline_reached, cleanup |
| REC-013 | **Deadline global durante motion.** Motion nunca responde | Ignorar query tardia, seguir/terminar bounded |
| REC-014 | **Deadline global durante recovery.** Recovery não terminou | Cancelar updates e avaliar dados já recebidos de forma bounded |
| REC-015 | **Deadline curto.** totalDeadline=1000 ms | Não iniciar recovery de 7000ms |
| REC-016 | **Recovery window zero.** recoveryWindowMs=0 | Pular stream, usar história/degraded |
| REC-017 | **Sem sessão prévia.** evaluate sem sessionId | Contexto efêmero; history empty inicial |
| REC-018 | **Cached ruim mas fresh disponível.** Cached age 20s, fresh age 1s | Priorizar fresh elegível |
| REC-019 | **Cached mais preciso mas age ruim.** Cached accuracy2 age55s vehicle | Incerteza maior, não seleção por accuracy sozinha |
| REC-020 | **GPS retorna null Android.** CurrentLocation returns null | Sem crash, initial_fix_timeout/absence |
| REC-021 | **GPS sem serviço.** Location services disabled | unavailable e guidance, sem requests extra |
| REC-022 | **Android Play Services ausente.** Capability missing | unavailable/platform reason, sem crash |
| REC-023 | **iOS Core Location delegate error.** NSError transient | Fallback bounded com reason, sem Promise pendente |
| REC-024 | **Permissão approximate.** SO retorna fix reduzido | Cap approximate aplicado, nunca request full silenciosa |
| REC-025 | **Erro de provider mas histórico admissível.** Fresh provider fail, history recent | Degraded e reason da falha mantido |
| REC-026 | **Nenhuma position mas motion stationary.** Só motion, sem coordenada | unavailable, score 0 |
| REC-027 | **Lote de fixes parciais.** 2 válidos 2 inválidos | Contadores corretos e avaliação só sobre válidos |
| REC-028 | **Clock inconsistent no recovery.** Timestamp retrocesso bruto | Age unreliable, score zero para candidato inválido |
| REC-029 | **Replay de fixture.** Mesmo input e seed | Mesmo DTO byte-equivalente após remover requestId/duration |

## 11.7 SCR — cálculo do score/invariantes e geometria de decisão — 32 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| SCR-001 | **Sem fix.** Nenhum candidato | qualityScore=0; probabilityWithinRadius=null |
| SCR-002 | **Fix fresco acurácia ótima.** accuracy0, age0, stationary0, 3 coerentes | Score 100 se gates e sem caps |
| SCR-003 | **Fix fresco único.** accuracy10 age2000ms stationary interval2s | Score roundHalfUp(...), cap single_fix75 |
| SCR-004 | **Score inteiro.** Qualquer amostra válida | 0<=score<=100 e inteiro |
| SCR-005 | **Score não é probabilidade.** Qualquer assessment | probabilityWithinRadius=null e label sem símbolo % |
| SCR-006 | **Age cresce sozinho.** Mesmo fix/estado, age2s→10s | Score não aumenta |
| SCR-007 | **Accuracy piora sozinha.** Mesmo fix, acc10→50 | Score não aumenta |
| SCR-008 | **Movimento unknown substitui stationary.** Mesmas demais entradas | Score não aumenta pela perda de evidência |
| SCR-009 | **Duplicatas acrescentadas.** 1 fix repetido 3x | Score não aumenta por consistency |
| SCR-010 | **Outlier acrescentado.** Base boa + GPS jump | Score não aumenta por outlier |
| SCR-011 | **Cap approximate.** Score bruto95 com autorização approximate | Score<=30 |
| SCR-012 | **Cap cached.** Score bruto95 com source cached | Score<=60 |
| SCR-013 | **Cap unknown old.** Fix cached e movimento unknown | Score<=40 |
| SCR-014 | **Cap uncertainty100.** effectiveUncertainty=100 | Score<=35 |
| SCR-015 | **Cap uncertainty300.** effectiveUncertainty=300 | Score<=15 |
| SCR-016 | **Cap simulated.** Flag mocked e demo policy | Score=0 conforme política |
| SCR-017 | **Cap multiple.** Cached+approximate+uncertainty300 | Score<=15 (menor cap aplicável) |
| SCR-018 | **Não aplicar cap se false.** Fix fresh sem flags limitantes | Sem cap indevido |
| SCR-019 | **Age reliability false.** Timestamp unreliable | Score0 e inconclusive |
| SCR-020 | **Longitude precision rounding.** Destinos próximos sem movimento | Score independe de longitude salvo consistência/proximidade |
| SCR-021 | **Dentro suportado.** d=30,u=15,r=100 qualidade elegível | supports_inside |
| SCR-022 | **Fora suportado.** d=250,u=20,r=100 qualidade elegível | supports_outside |
| SCR-023 | **Proximidade indefinida.** d=95,u=20,r=100 | inconclusive |
| SCR-024 | **Indefinido com score alto.** d=99,u=5,r=100 score alto | inconclusive; score não força status |
| SCR-025 | **Dentro matemático mas age ruim.** d=1,u grande ou age inadmissível | inconclusive |
| SCR-026 | **Raio minúsculo.** r=1, accuracy10 | Não afirmar dentro apenas por lat/lon iguais |
| SCR-027 | **Raio amplo.** r=100000, fresh fix | Relação baseada em d,u,r, não atalho score |
| SCR-028 | **Idade aumenta uncertainty.** Age1s→20s mesmo bound | effectiveUncertainty não diminui |
| SCR-029 | **Accuracy aumenta uncertainty.** Accuracy10→50 mesmo age | effectiveUncertainty aumenta 40m |
| SCR-030 | **Mock false não autentica.** MockFlag NotReported | Sem campo isTrustedIdentity/proofOfPresence |
| SCR-031 | **Arredondamento .5.** rawScore termina exatamente em .5 | Usar roundHalfUp identicamente em todas plataformas |
| SCR-032 | **Score versionado.** Qualquer avaliação | scoreModelVersion='heuristic-v1' |

## 11.8 CON — concorrência, cancelamento e lifecycle — 28 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| CON-001 | **Abort antes de iniciar.** AbortSignal.aborted true | cancelled; nenhum sensor ativado |
| CON-002 | **Abort enquanto one-shot.** Cancel no t+2s | Cancelar token, resultado uma vez, nenhum listener |
| CON-003 | **Abort enquanto motion.** Cancel durante query | Não esperar query tardia, cleanup |
| CON-004 | **Abort durante recovery.** Cancel no t+5s | removeLocationUpdates e status cancelled |
| CON-005 | **Abort depois de complete.** Cancel tardio | Resultado não reescrito, nenhuma dupla resolução |
| CON-006 | **Cancel duas vezes.** Mesmo requestId | Idempotente; nenhum crash |
| CON-007 | **StopSession enquanto evaluate.** Sessão termina | Avaliações daquela sessão canceladas |
| CON-008 | **StopSession duas vezes.** Mesma sessão | Sem erro e callbacks inativos |
| CON-009 | **Background durante one-shot.** App background | Cancelar/degraded conforme política, reason app_backgrounded |
| CON-010 | **Background durante recovery.** App background | Stop updates, nenhum hidden tracking |
| CON-011 | **Foreground novamente.** Após interrupção | Não retomar automaticamente; caller reinicia |
| CON-012 | **Dois request IDs destinos distintos.** Mesma sessão | Não misturar raio/destino/DTO |
| CON-013 | **Dois request IDs cancel um.** Shared collection active | Outro mantém sua assinatura se single-flight |
| CON-014 | **Duas requests com políticas incompatíveis.** Accuracy distinta | Não compartilhar coleta sem compatibilidade; busy/serialized explicável |
| CON-015 | **100 starts/stops.** Loop determinístico no fake | Nenhum listener/timer/referência residual |
| CON-016 | **100 cancels.** Fakes com callback tardio | Nenhuma double resolve e nenhuma memory leak óbvia |
| CON-017 | **Callback após invalidation.** JS runtime foi destruído | Ignorar e não chamar ponte órfã |
| CON-018 | **Callback duplicado provider.** Provider invoca listener 2x | Não resolve Promise 2x |
| CON-019 | **Timer antes do callback.** Deadline vence e callback chega na sequência | Finaliza uma vez, ignora callback |
| CON-020 | **Callback antes do timer.** Fix bom e timer tenta vencer | Finaliza uma vez, cancela timer |
| CON-021 | **Permissão revogada em runtime.** Durante recovery | Interromper e reason permission_denied |
| CON-022 | **Reboot ou processo morto.** SessionId perdido | Não recupera histórico em segredo |
| CON-023 | **Thread concorrente JNI.** Duas threads chamam ingest na mesma sessão | Serialização garante ring buffer estável |
| CON-024 | **ObjC++ ownership.** Session stop com callback pendente | Sem use-after-free/crash |
| CON-025 | **JNI local refs stress.** 1000 cycles | Sem overflow de refs nem dangling pointers |
| CON-026 | **Exception Kotlin durante callback.** Erro interno capturado | Erro estruturado e cleanup garantido |
| CON-027 | **Exception Swift delegate.** Erro de CLLocationManager | Resultado controlado, cleanup |
| CON-028 | **Sem JS runtime ativo.** Chamada nativa em teardown | Sem tentar emitir event em JS inexistente |

## 11.9 PLT — compatibilidade, permissões e falhas do SO — 34 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| PLT-001 | **Android precise granted.** Fine disponível e services on | Capabilities refletem precise |
| PLT-002 | **Android approximate granted.** Coarse only | PermissionPrecision approximate, cap aplicado |
| PLT-003 | **Android permission denied.** Sem grant | unavailable sem prompt automático |
| PLT-004 | **Android permission never asked.** not_determined | Sem pedido automático em evaluate |
| PLT-005 | **Android serviços desligados.** Location disabled | unavailable reason services_disabled |
| PLT-006 | **Android permission revocation process restart.** Android 12+ muda accuracy | Sessão perdida, não restaurar histórico |
| PLT-007 | **Android FLP cache older than requested.** getCurrent retorna cache | Verificar idade real e source coerente |
| PLT-008 | **Android getLocationAvailability false.** Fix ainda pode chegar | Não abortar só por hint de availability |
| PLT-009 | **Android Play Services missing.** FLP indisponível | Capability reason, sem crash |
| PLT-010 | **Android Doze/battery saver.** Updates atrasados | Deadline/cleanup, não busy loop |
| PLT-011 | **Android mock location reported.** isMock available true | Diagnóstico mock parcial |
| PLT-012 | **Android no mock signal.** API/OS sem flag | Mock unknown e não trusted |
| PLT-013 | **Android activity permission denied.** ACTIVITY_RECOGNITION negada | Motion unknown; GPS segue |
| PLT-014 | **Android transition stopped in STILL.** Long stationary e updates raros | Não inferir cobertura completa sem evidência |
| PLT-015 | **Android location listener lifecycle.** Request cancel | removeLocationUpdates chamado sempre |
| PLT-016 | **iOS fullAccuracy.** authorized precise | Precision full, mas accuracy do fix ainda validada |
| PLT-017 | **iOS reducedAccuracy.** Authorized reduced | Não alterar desiredAccuracy para contornar |
| PLT-018 | **iOS location permission restricted.** Restriction enabled | Retorna status permission_restricted |
| PLT-019 | **iOS whenInUse denied.** Usuário nega | Sem repeated prompt em loop |
| PLT-020 | **iOS horizontalAccuracy negative.** CLLocation invalid | Descartada, reason |
| PLT-021 | **IOS delegate entrega old+new.** DidUpdateLocations batch | Idade real de cada fix mantida |
| PLT-022 | **iOS source simulatedBySoftware true.** Simulação GPX | Flag de simulação, sem alegar antifraude |
| PLT-023 | **iOS sourceInformation nil.** SO não reporta | Flag unknown |
| PLT-024 | **iOS Motion availability false.** Sem hardware/motion data | Unknown, fallback funciona |
| PLT-025 | **iOS Motion Info.plist absent.** Feature configurada sem NSMotionUsageDescription | Pré-build/QA bloqueia ativação; não publicar crash |
| PLT-026 | **iOS historical motion query 2min delay.** CMMotion query atrasada | Deadline global domina, não esperar |
| PLT-027 | **iOS CLLocationManager permission callback delayed.** Autorização muda depois do deadline | Não reabrir avaliação automaticamente |
| PLT-028 | **iOS app em background.** v1 foreground-only | Cessa coleta e retorna reason |
| PLT-029 | **iOS native wrapper malformed DTO.** ObjC++ recebe data inválida | Erro estruturado e sem crash |
| PLT-030 | **Expo Go host.** Pacote precisa módulo nativo | README esclarece development build, não promete Expo Go |
| PLT-031 | **RN Codegen type unsupported.** Spec com union incompatível | Fase 0 transforma DTO ou bloqueia merge; não muda runtime silenciosamente |
| PLT-032 | **R8 minify.** Android release shrinker enabled | JNI ainda acessível e build release funcional |
| PLT-033 | **CocoaPods link.** iOS release build | C++ linkado, símbolo resolvido |
| PLT-034 | **Device no GPS hardware.** Tablet/Simulator | Test em fake/emulator; capability diag apropriado |

## 11.10 PUB — embalagem, privacidade, documentação e publicação — 26 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| PUB-001 | **No network by default.** Teste monitorando requests | Nenhuma requisição HTTP enviada pela lib |
| PUB-002 | **No disk by default.** Após sessão | Nenhum arquivo com coordenadas persistido |
| PUB-003 | **Logs in release.** Avaliação com coords reais | Logs sem coords, sem histórico bruto |
| PUB-004 | **ClearHistory during session.** Chamada explícita | Buffer apagado, sessão continua se permitido |
| PUB-005 | **StopSession on logout.** Host encerra sessão | Sem vazamento entre usuários |
| PUB-006 | **Docs qualityScore.** README de API | Explicitamente índice não percentual |
| PUB-007 | **Docs proof of presence.** README de limitação | Explica não ser prova física |
| PUB-008 | **Docs Android permission.** Setup Android | Permissões mínimas e precise/approximate explicadas |
| PUB-009 | **Docs iOS permission.** Setup iOS | WhenInUse + Motion usage descriptions |
| PUB-010 | **Docs background.** README | Foreground-only e riscos de background explicitados |
| PUB-011 | **Docs mock.** README | Não prometer antifraude |
| PUB-012 | **Docs Expo.** README | Expo Go não suportado sem módulo compilado |
| PUB-013 | **Docs iOS/Android version.** Readme compatibility | Somente versões realmente compiladas anunciadas |
| PUB-014 | **Npm tarball inspection.** npm pack --dry-run | Inclui fontes cpp/ios/android e não inclui secrets/logs |
| PUB-015 | **Npm import smoke.** Sample consumer app local tarball | Import, Codegen, build, invoke successfully |
| PUB-016 | **Tag/version.** GitHub release e changelog | Semver alinhado a breaking API |
| PUB-017 | **CI Linux Android.** Release build + C++ unit | Executado e artefatos disponibilizados |
| PUB-018 | **CI macOS iOS.** Simulator build + native tests | Executado e artefatos disponibilizados |
| PUB-019 | **Static analysis.** Kotlin/Swift/C++ lint onde configurado | Sem warnings críticos não justificados |
| PUB-020 | **Security/permissions review.** Release checklist | Manifest/Info.plist revisados e privacy note presente |
| PUB-021 | **Benchmark honesty.** README performance | Dados só se medidos com device/versão/build |
| PUB-022 | **Crash reproduction.** Issue template | Reprodução pede reason codes, não coordenadas pessoais |
| PUB-023 | **No sensitive test fixtures.** Repository fixtures | Somente coordenadas sintéticas/públicas |
| PUB-024 | **License.** Publicar npm | Licença escolhida e arquivo LICENSE presentes |
| PUB-025 | **Peer dependencies.** package.json | Faixa de RN validada; não pressuposta |
| PUB-026 | **ABI coverage.** CI builds | Arquiteturas anunciadas realmente compiladas |

## 11.11 SES — modos de sessão e monitoramento explícito — 13 testes

| ID | Situação / Given | Resultado esperado / Then |
|---|---|---|
| SES-001 | **Sessão default.** startSession sem collectionMode | Não inicia GPS nem motion; contexto memory-only |
| SES-002 | **Monitoring opt-in.** collectionMode=foreground_monitoring com permissão | Inicia atualizações foreground explicitamente |
| SES-003 | **Monitoring sem permissão.** collectionMode=foreground_monitoring sem autorização | Não abre prompt implícito nem ativa sensor |
| SES-004 | **Monitoring captura prévio.** 3 updates com sessão ativa | Histórico fica disponível no próximo timeout de avaliação |
| SES-005 | **Monitoring desativado.** collectionMode=evaluations_only | Histórico só cresce com avaliações explícitas |
| SES-006 | **Background enquanto monitora.** App deixa foreground | Cancela update ativo e não reinicia ocultamente |
| SES-007 | **Duração máxima de monitoring.** maxMonitoringDurationMs atingido | Updates encerrados, sem timer ativo |
| SES-008 | **Stop monitoring.** stopSession chamado | Remove listeners, buffer limpo, sem dados residuais |
| SES-009 | **Permissão revogada no monitoring.** Usuário revoga localização | Interrompe coleta, reason e cleanup |
| SES-010 | **Intervalo solicitado não garantido.** OS agrupa 2 updates | Trata callbacks por timestamps sem assumir cadence exata |
| SES-011 | **Monitoring e avaliação concorrente.** Avaliação usa sessão monitorada | Não cria duplicação de listener sem necessidade |
| SES-012 | **Várias sessões de monitoring.** Duas sessões independentes | Política evita duplicar coleta indevida e não mistura buffers |
| SES-013 | **Config monitoring inválida.** interval=0 ou maxDuration negativo | InvalidArgumentError antes de ligar GPS |

**Total normativo: 289 casos explícitos** (GEO=26, VAL=26, HIS=26, JMP=24, MOT=25, REC=29, SCR=32, CON=28, PLT=34, PUB=26, SES=13). Esses casos representam especificações a implementar; não são resultados de execução.

# 12. Fixtures de referência com expectativas calculáveis

Os casos da seção 11 são o catálogo de comportamento. Esta seção cria exemplos numéricos de teste para impedir que o time implemente um score diferente em cada plataforma.

## 12.1 Fixture `score_perfect_three_fixes`

**Given:** três fixes coerentes, accuracy=0, age=0, movementAllowance=0, permissions precise, latest measured source fresh, sem flags/suspeitas. **When:** avaliar um candidato. **Then:** fatores accuracy=1, freshness=1, movement=1, consistency=1; raw=100; caps vazios; `qualityScore=100`; `probabilityWithinRadius=null`.

## 12.2 Fixture `score_single_fix_default`

**Given:** uma amostra fresca: accuracy=10 m, age=2000 ms, intervalo de motion stationary coberto, movement bound 0.5 m/s, movementAllowance=1 m; no caps exceto single-fix. **When:** calcular:

```text
accuracyFactor  = 1 - 10/100   = 0.90
freshnessFactor = 1 - 2000/60000 ≈ 0.96666667
movementFactor  = 1 - 1/100    = 0.99
consistency     = 0.60
raw = roundHalfUp(100*(0.4*0.9 + 0.25*0.9666667 + 0.2*0.99 + 0.15*0.6))
    = roundHalfUp(88.9666667) = 89
cap single-fix = 75
qualityScore = 75
```

**Then:** score exatamente **75**, `scoreBreakdown.rawScore=89`, appliedCaps contém `single_fix`.

## 12.3 Fixture `score_cached_vehicle`

**Given:** fix cached com age=10000 ms, accuracy=10m, movimento vehicle cobrindo o período, 3 fixes coerentes; `movementAllowance=45*10=450m`; `effectiveUncertainty=460m`. **When:** calcular:

```text
accuracyFactor  = 0.90
freshnessFactor = 1 - 10/60 = 0.833333...
movementFactor  = clamp(1-450/100,0,1)=0
consistency     = 1
raw = roundHalfUp(100*(0.4*0.9+0.25*0.833333+0.2*0+0.15*1))
    = roundHalfUp(71.833333) = 72
caps: cached=60, uncertainty>=100=35, uncertainty>=300=15
qualityScore = 15
```

**Then:** score exatamente **15**, evidência de proximidade tende a inconclusiva em raio de 100m.

## 12.4 Fixture `far_outside_without_proving_presence`

**Given:** fix com `d=500m`, uncertainty `u=20m`, destino radius `r=100m`, idade e qualidade válidas. **Then:** `supports_outside` pela geometria (`500-20>100`), sem expressão de probabilidade e sem autorização de negócio.

## 12.5 Fixture `aged_stationary_gap`

**Given:** fix de 20s e motion `STILL` apenas nos últimos 2s. **Then:** 18s desconhecidos contabilizados com bound `unknown` e somente 2s com bound `stationary`; nunca aplicar stationary 20s. `continuousStationaritySupported=false`.

## 12.6 Fixture `recovery_aba`

**Given:** A=(0,0) t=0s accuracy 5m, B ~500m distante t=2s accuracy5m, C=(0,0) t=4s accuracy5m; limite 45m/s. **Then:** B suspeito, C/ A são coerentes, não escolher B como fresh melhor apenas por timestamp.

## 12.7 Numeric precision policy

C++ cálculo usa double; golden tests de distância com tolerância numérica explícita (ex.: 1e-6 relativo ou 0.01m absoluto nas distâncias sintéticas). Score final arredonda **após** cálculo em double e fica inteiro sem divergência entre compile targets. Não atualizar números de fixture sem atualizar `scoreModelVersion` se a semântica pública mudar.

---

# 13. Plano incremental de implementação (fases e gates)

## Fase 0 — Discovery e architecture review (NÃO escrever código de produção)

**Entregáveis:**

- Inspecionar repo e versions; registrar `docs/COMPATIBILITY_PLAN.md` com versões reais, SDK, iOS min/Android min, NDK, Xcode, Swift/ObjC++ interop, React Native/codegen e instalações necessárias.
- Validar scaffold `create-react-native-library` e escolher **platform TurboModule with shared C++ core** ou alternativa de integração justificada. Criar ADR-013 se mudar ADR-001.
- Desenhar sequência de avaliação e cancelamento, contratos Codegen minimalistas, DTOs, ownership e lifecycle.
- Escrever `docs/ADR/001-012.md` ou decisões equivalentes, sem copiar apenas os títulos. Justificar consequência negativa de cada uma.
- Criar mapa `Test ID → test suite / fixture`, dar estimativa de esforço por risco sem prometer datas.

**Gate:** aprovação de API, camadas, toolchain e tipos Codegen. **Não avançar automaticamente.**

## Fase 1 — Núcleo C++ compilável e testes básicos

**Implementar:** `Types`, validação, Haversine, ring buffer, dedup, score model v1 (sem plataforma). `CMakeLists.txt` cross-platform.  
**Tests:** GEO, VAL, HIS, SCR (subset relevante).  
**Gate:** testes C++ host passam; sem include RN/SDK e sem dependências de plataforma.

## Fase 2 — Modelos de movimento, outliers e propagação temporal

**Implementar:** OutlierDetector, MotionEvidenceAnalyzer, UncertaintyModel, CandidateSelector, propriedades de monotonicidade.  
**Tests:** JMP, MOT, SCR, property-based; golden fixtures.  
**Gate:** relação de proximidade não autoriza localização sem qualidade; controle de clocks e bordas validado.

## Fase 3 — RecoveryPolicy e state reducer no C++

**Implementar:** reducer evento→comando, comandos limitados por orçamento, recurso de single-flight simplificado apenas se justificado, simulation driver com fake events.  
**Tests:** REC, CON por simulação pura.  
**Gate:** todos os caminhos terminam sem operações pendentes; abort/deadline determinísticos.

## Fase 4 — Android adapter e JNI

**Implementar:** setup CMake/NDK, Kotlin ports, Fused Location, permission, optional Activity Recognition, JNI batch, coordinator serial, session registry, cancel/invalidation.  
**Tests:** JUnit fakes, testes JNI, integração com build debug/release e R8.  
**Gate:** app example Android consegue chamada real; no listener leak após cancel.

## Fase 5 — iOS adapters e Objective-C++

**Implementar:** podspec/source files C++17, Swift Core Location/Core Motion, protocols, ObjC++ bridge, lifecycle, cancellation.  
**Tests:** XCTest fakes, chamada no iOS simulator/device, build release, ownership/callback.  
**Gate:** mesmo core produz resultados idênticos para fixtures C++ no Android/iOS.

## Fase 6 — TurboModule + Codegen + public facade TS

**Implementar:** native DTO transport, Spec TS, facade Clean Architecture, converters, errors, AbortSignal, getCapabilities/permission explicit APIs, start/stop/evaluate.  
**Tests:** TS unit/contract, Codegen generation, compilation, smoke Android/iOS.  
**Gate:** nenhum algoritmo matemático duplicado no TS/Kotlin/Swift; API funciona em ambas plataformas.

## Fase 7 — App exemplo real e simulador de cenários

**Implementar:** app delivery minimalista com tela de destino (coords configuráveis), permissões, start/evaluate/cancel/stop, results + breakdown, timeline e modo simulação com fixtures sintéticas.  
**Tests:** UI loading/cancel/error, permission denied, degraded result, unsupported motion, mock GPS e SES.  
**Gate:** diagnóstico mostra explicitamente qualidade 0–100 como índice, **não porcentagem**; funciona sem API key e sem internet.

## Fase 8 — Stress, benchmark, segurança e publicação

**Implementar:** CI release builds, lint/static checks, docs EN, package/peerDeps/semver, privacy note, changelog, license, contribution guidelines, ADRs, issue templates.  
**Tests:** CON/PLT/PUB, sanity on two physical devices, smoke `npm pack --dry-run` and local tarball installation.  
**Gate:** release iOS/Android comprovados, sem PII vazada e todos testes críticos passados; só então publicar no npm.

## Fase 9 — Evolução pós-v1 (não implementar por acidente)

- Probabilidade calibrada com dataset real e metodologia auditável.
- Monitoramento background (solicitação distinta, discussão de privacy/energia).
- Egress de métricas agregadas **sem coordenadas** opt-in; não padrão.
- Opcional Kalman/reprojection/offline map matching mediante benchmarks e validação.
- Suporte a fallback Android sem Play Services (LocationManager), se for requisito de adoção.

---

# 14. Matriz de rastreabilidade (requisito → módulo → teste → gate)

| Requisito | Módulo principal | Testes | Gate |
|---|---|---|---|
| FR-01/02 permissões/capabilities | PermissionPort Kotlin/Swift + TS facade | PLT, PUB | Fases 4–7 |
| FR-03 distância/raio | C++ GeoMath / CandidateSelector | GEO, SCR | Fases 1–2 |
| FR-04 one-shot | Kotlin/Swift LocationSource | REC, PLT | Fases 4–5 |
| FR-05 multi-fix recovery | RecoveryPolicy + NativeCoordinator | REC, CON | Fases 3–5 |
| FR-06 buffer 10 | C++ LocationHistory | HIS, CON | Fases 1,3 |
| FR-07 outlier | C++ OutlierDetector | JMP | Fase 2 |
| FR-08 movimento | MotionPort + Core MotionEvidence | MOT, PLT | Fases 2,4,5 |
| FR-09 incerteza temporal | C++ UncertaintyModel | MOT, SCR, VAL | Fase 2 |
| FR-10 score | C++ QualityScore | SCR, GEO | Fases 1–2 |
| FR-11 diagnostics | C++ Assessment + adapters | VAL, REC, PUB | Fases 3–7 |
| FR-12 cancel/stop | Coordinator/session registry | CON | Fases 3–6 |
| FR-13 offline/privacidade | Sem adapter de rede + tests | PUB | Fase 8 |
| FR-14 publicar npm | Packaging/toolchains | PLT, PUB | Fase 8 |
| FR-15 testes determinísticos | fixtures/clock/ports | todas 289 | Fases 1–8 |
| FR-16 privacidade | memória + logs redigidos | PUB | Fase 8 |

**Regras de PR:** toda feature/bug deve citar FR/INV, IDs de teste e ADR relevante; alterações de score precisam atualizar golden fixtures e considerar versionamento de modelo; alterações públicas TypeScript precisam atualização do README e changelog.

---

# 15. Checklist de arquitetura / review sênior

## 15.1 Core C++

- [ ] C++17 puro; não inclui JNI, React, UIKit, Google APIs, Objective-C, Foundation.
- [ ] Nenhuma classe de negócio duplicada em Kotlin/Swift/TS.
- [ ] API C++ não mantém callbacks nativos ou ponteiros para objetos Java/Swift.
- [ ] Tempo normalizado por adapter; nunca mistura epoch e monotonic.
- [ ] Ring buffer bounded, dedup, ordem temporal, caps e score determinísticos.
- [ ] Sem `NaN`, `Inf`, integer overflow, longitude wraparound incorreto, `dt=0` division.
- [ ] Não deduz estacionaridade usando um único evento `STILL`.
- [ ] Source, flags simulados e reason codes representam evidência real do SO.

## 15.2 Plataforma

- [ ] Permissões só são pedidas em métodos explícitos.
- [ ] Resolver/cancelar chamadas assíncronas exatamente uma vez.
- [ ] JNI/ObjC++ wrapper fino, sem cálculos geográficos.
- [ ] No callback depois de stop/invalidation; generation guards.
- [ ] Cancelamento interrompe localização, consulta de movimento e timers.
- [ ] Callback desordenado é tratado e não lança exceção em main thread.
- [ ] Não iniciar foreground service nem tracking background oculto.
- [ ] Sem Play Services, motion ou permissão de precisão, degrada de forma segura.

## 15.3 TypeScript/Produto

- [ ] API pública documentada com exemplos e retornos estáveis.
- [ ] `AbortSignal` não atravessa Codegen como objeto.
- [ ] Score não aparece com `%`, probabilidade continua `null`.
- [ ] UI não contém lógica de GPS; views usam ViewModel/use case.
- [ ] Não existe estado global de coordenadas no JS; sessions explícitas.
- [ ] Erros de argumento não ativam GPS.
- [ ] README deixa claro que evidência de GPS não confirma entrega.

## 15.4 Publicação

- [ ] Testes 289 mapeados; todos críticos executados, failures resolvidas ou riscos aceitos e descritos.
- [ ] `npm pack --dry-run` inspecionado; licensing e secrets ok.
- [ ] Android release + iOS release + simulador compilam e RN Codegen executa.
- [ ] Compatibilidade com versões testadas e limitações no README.
- [ ] Benchmarks reais publicados apenas se executados e reproduzíveis.
- [ ] Sem internet e sem persistência por padrão, com política de privacidade documentada.

---

# 16. Riscos conhecidos, trade-offs e decisões em aberto

| Risco | Impacto | Estratégia / condição para decidir |
|---|---|---|
| JNI/ObjC++ complexos | Build e manutenção | prova de conceito de linking na Fase 0/4/5; wrapper fino; testes de ABI |
| API Codegen não suporta DTO desejado | Tipagem incorreta | contrato mínimo compilado primeiro; DTO plano interno |
| Motion history atrasado/sem cobertura | Estimativa exageradamente otimista | coverage unknown; não aguardá-lo além de budget |
| Score não calibrado | Usuário interpreta 80 como 80% | label de índice, `probabilityWithinRadius=null`, docs |
| Recalcular incerteza de posição antiga | Pode parecer posição nova | `source=cached`, age real, uncertainty maior, sem dead reckoning |
| “Estacionário” detectado incorretamente | Falso positivo na entrega | exige cobertura, permanece não determinístico, prova adicional do negócio |
| GPS indoor multipath e spoof | Coordenada enganosa | outlier flags e no proof-of-presence; app usa código de entrega |
| Concentração das chamadas JNI | Latência/carga | normalização e batch, medir antes de otimizar |
| Sem histórico em sessão nova | Fallback pode não funcionar | retornar indisponível e explicar que histórico depende de coleta prévia |
| C++ aumenta peso do pacote | Adoção npm | inspecionar bundle/tarball, compat matrix, comparar baseline TS em ADR |
| Histórico durante mudanças de usuário | Privacidade | sessionId isolado, clear/stop, sem persistência |
| Android hardware sem Play Services | Sem FLP | capability, docs, novo adapter futuro se exigido |
| App suspenso/background | Eventos não entregues | foreground-only; deadline + cleanup |
| Dependência de pessoa física para validação | Resultados irreprodutíveis | fixtures sintéticas + protocolo de field tests documentado |
| Uso em decisão antifraude | Prejuízo ao entregador | SDK fornece evidência explicável, não decisão final |

## 16.1 Perguntas que devem ser resolvidas na Fase 0

1. Qual versão RN está realmente instalada? Ela suporta os tipos e a estratégia de TurboModule desejados?
2. Qual versão do scaffold suporta Kotlin/Swift + núcleo C++ sem patches de app consumidor? O pacote deve adicionar o código C++ via Gradle/CMake e podspec?
3. Qual envelope de versões Android/iOS tem hardware/SDK relevante e pode ser testado em CI?
4. Como o iOS Swift ↔ ObjC++ preservará ownership, `async/await` e cancelamento sem um ciclo de retenção?
5. O feature gate `motion` será opt-in na sessão ou apenas consulta se permissão já concedida? Recomenda-se opt-in explícito.
6. O suporte a requests concorrentes será single-flight completo na primeira release ou fila/busy? ADR com testes obrigatório.
7. Qual é o contrato final do score e reason codes depois de rodar as golden fixtures? Qual versão de score model?
8. Como `getCurrentLocation` em Android distinguirá cached vs fresh apenas por idade/metadata disponível, sem alegar detectar origem perfeita?
9. Como clocks iOS se comportam em alteração manual de data e process lifetime? Testar device real e usar conservative unknown.
10. Como publicar e testar o tarball npm em um app consumidor externo ao monorepo (smoke real)?

---

# 17. Referências técnicas verificadas e fontes de decisões

**Consultadas em 14/11/2026; verificar novamente antes de implementar, pois APIs/versões podem mudar.**

1. React Native — Cross-Platform Native Modules (C++): https://reactnative.dev/docs/the-new-architecture/pure-cxx-modules
2. React Native — Turbo Native Modules: https://reactnative.dev/docs/turbo-native-modules-introduction
3. React Native — Using Codegen: https://reactnative.dev/docs/the-new-architecture/using-codegen
4. React Native — Create a Library: https://reactnative.dev/docs/the-new-architecture/create-module-library
5. Android — FusedLocationProviderClient: https://developers.google.com/android/reference/com/google/android/gms/location/FusedLocationProviderClient
6. Android — Current location and last known: https://developer.android.com/develop/sensors-and-location/location/retrieve-current
7. Android — Location (accuracy, mock, monotonic): https://developer.android.com/reference/android/location/Location
8. Android — ActivityRecognitionClient: https://developers.google.com/android/reference/com/google/android/gms/location/ActivityRecognitionClient
9. Apple — CLLocationManager: https://developer.apple.com/documentation/corelocation/cllocationmanager
10. Apple — desiredAccuracy: https://developer.apple.com/documentation/corelocation/cllocationmanager/desiredaccuracy
11. Apple — CMMotionActivityManager: https://developer.apple.com/documentation/coremotion/cmmotionactivitymanager
12. Apple — historical motion query (may delay): https://developer.apple.com/documentation/coremotion/cmmotionactivitymanager/queryactivitystarting(from:to:to:withhandler:)
13. Apple — CLLocationSourceInformation: https://developer.apple.com/documentation/corelocation/cllocationsourceinformation
14. Apple — Getting current device location: https://developer.apple.com/documentation/corelocation/getting-the-current-location-of-a-device

## 17.1 Limitações da evidência

- Documentação oficial confirma APIs e comportamentos, **não** confirma performance, consumo de bateria ou confiabilidade estatística do nosso algoritmo.
- Os limites de score, de movimento e idade são decisões de produto para protótipo e devem ser benchmarkados/calibrados, não atribuídos ao Android/Apple.
- Sensor de movimento não mede presença humana; GPS não localiza a entrada de um prédio necessariamente; spoofing pode contornar os flags do sistema.

---

# 18. Prompt inicial para Claude Code

```text
Leia integralmente `SPEC_Location_Confidence_Engine_v2.md` e `CLAUDE.md`.
A SPEC é normativa e deve orientar a implementação de uma biblioteca npm React Native
com clean architecture em TypeScript, adapters hexagonais Kotlin/Swift e core C++17
compartilhado para algoritmos e RecoveryPolicy. Não escreva código de produção ainda.

Execute SOMENTE FASE 0:
1. Inspecione arquivos/versões existentes e viabilidade de toolchain e Codegen.
2. Proponha a estrutura final, o contrato mínimo de Codegen e os wrappers JNI/ObjC++.
3. Revise cada ADR, inclusive riscos, custos, alternativas e qualquer inconsis-
   tência de sincronização, performance, permissão, timestamp ou score.
4. Mapeie os 289 testes normativos em suites e identifique os riscos críticos.
5. Identifique contradições/ambiguidade da spec antes de aplicar mudanças;
   apresente correções propostas separadamente, sem silenciosamente apagar requisitos.
6. Entregue Fase 0 com artefatos de arquitetura e o plano de implementação faseado.

Não avance à Fase 1 antes de minha aprovação explícita.
Nunca alegue build/test/benchmark sem executá-lo e registrar evidência.
```

---

**Fim da SPEC v2.0.** Este documento descreve um projeto a implementar; **nenhum dos 289 casos equivale a teste já criado ou aprovado.**
