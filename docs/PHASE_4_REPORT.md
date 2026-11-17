# Fase 4 — Android: relatório

Data: 16/11/2026. Branch `phase/4-android`, a partir de `phase/3-policy-facade`.

## Objetivo

Ligar o motor C++ ao Android de verdade: o JavaScript chama o módulo Kotlin, o Kotlin liga e desliga o GPS conforme o motor manda, e o relatório volta para o JavaScript. Nenhuma regra sobre localização mora no Kotlin.

## Ordem das chamadas

```text
JavaScript
  |  NativeLocationConfidence.getReport(requestId, request)
  v
LocationConfidenceModule.kt      traduz: objeto JS <-> objetos Kotlin; monta as peças uma vez
  v
EngineCoordinator.kt             fala com o motor e depois OBEDECE o motor
  |
  |   1. conta ao motor o que aconteceu     engine.startRequest / readingsArrived / timePassed ...
  |   2. obeyTheEngine()
  |        |-- takeFinishedReport()   repete até não haver mais; entrega cada um a quem pediu
  |        `-- whatThePlatformMustDoNow()
  |              |-- adjustLocationUpdates()   GPS ligado? a cada quanto?
  |              |-- adjustActivityUpdates()   sensor de atividade ligado?
  |              `-- adjustTimer()             acordar quando?
  v
CoreEngine.kt                    objetos Kotlin <-> listas de números
  v
JniBindings.cpp                  listas de números <-> structs de lce.h
  v
o motor C++                      todas as decisões
```

Tudo o que mexe no motor roda em uma fila só (`SingleThreadQueue`, uma thread chamada "LocationConfidence"). O GPS, o timer e as chamadas do JavaScript entram nessa fila e são atendidos um de cada vez; é isso que dispensa travas.

## Como um valor atravessa o JNI

Cada struct do `lce.h` atravessa como uma lista de números (`DoubleArray`), escrita de um lado e lida do outro na mesma ordem. Exemplo, o estado do aparelho, que são 6 números:

```text
Kotlin  NumberWriter:  permission, precision, locationOn, provider, foreground, activitySupport
C++     NumberReader:  permission, precision, locationOn, provider, foreground, activitySupport
```

Toda lista que sai do C++ termina com o número 7741. O Kotlin confere: se a quantidade de campos de um lado não bater com a do outro, a leitura para no lugar errado, o 7741 não aparece e a chamada falha com erro claro, em vez de embaralhar campos em silêncio. Os motivos do relatório (64 bits) atravessam como dois números de 32 bits, porque um `double` só guarda 53 bits inteiros sem perda.

O arquivo C++ não usa memória dinâmica: os vetores têm tamanho fixo na pilha, então nada pode lançar exceção através do JNI.

## Arquivos

| Arquivo | Papel |
|---|---|
| `android/src/main/cpp/JniBindings.cpp` | registra as funções nativas (`RegisterNatives`) e traduz listas de números em structs |
| `core/NativeCore.kt` | a lista das funções nativas |
| `core/CoreEngine.kt` | objetos Kotlin de um lado, listas de números do outro |
| `models/Models.kt` | os tipos Kotlin (estado do aparelho, leitura, relatório...) |
| `ports/Ports.kt` | o que o coordenador precisa do mundo: relógio, fila, GPS, sensor de atividade, estado do aparelho, motor |
| `application/EngineCoordinator.kt` | fala com o motor e obedece; também o modo simulação |
| `adapters/FusedLocationSource.kt` | o GPS de verdade (Google Play services) |
| `adapters/AndroidDeviceState.kt` | permissões, chave de localização, Play services, primeiro plano |
| `adapters/SingleThreadQueue.kt`, `ElapsedRealtimeClock.kt`, `NoPhoneActivitySource.kt` | fila, relógio, sensor de atividade (ausente na v1.0, D-02) |
| `LocationConfidenceModule.kt` | o que o JavaScript enxerga |
| `android/src/sharedTest/java/...` | os testes Kotlin, que rodam na JVM e no aparelho |
| `android/src/test/cpp/CMakeLists.txt` | compila o mesmo `JniBindings.cpp` para o computador, para os testes de JVM |

`application/` só importa `ports/` e `models/`: nada de Play services, JNI ou React ali.

## Decisões adotadas

Registradas em `SPEC_REVIEW.md`, R-31. As principais:

- **Só fluxo de leituras, nunca leitura avulsa.** O GPS é pedido com `requestLocationUpdates` em alta precisão, no intervalo que o motor informa (1 s durante uma avaliação; o intervalo da sessão quando ela só coleta). `getLastLocation` e `getCurrentLocation` não são chamados (D-03).
- **Uma fila serial em vez de coroutines.** O plano falava em coroutines com tempo virtual. Uma fila de uma thread, com uma versão manual nos testes (`ManualQueue.advanceTo(ms)`), dá o mesmo controle do tempo com menos peças.
- **Um coordenador para o motor inteiro.** A SPEC previa um coordenador por avaliação e um registro de sessões. Como o core guarda as sessões e conta quem precisa do GPS (ADR-019), sobra um coordenador só.
- **Os mesmos testes em dois lugares.** Os 35 testes Kotlin rodam na JVM, contra o motor compilado para o computador, e no emulador, contra a `.so` como ela é entregue.
- **Modo simulação.** Não previsto na SPEC. O app troca o GPS por leituras próprias (`simulateReadings`); enquanto estiver ligado o GPS real não é usado, nenhuma permissão é necessária e todo relatório leva `simulated_location_source`.
- **A biblioteca não declara permissões no Manifest.** O app declara `ACCESS_FINE_LOCATION` e `ACCESS_COARSE_LOCATION` (R-21). O exemplo faz isso.

## Erro encontrado e corrigido

`EngineCoordinator.appCameToForeground` consultava o estado do aparelho antes de comparar com o estado anterior, e a consulta já atualizava o valor guardado. Uma permissão retirada enquanto o app estava fora da tela nunca era percebida. O teste `a_permission_taken_away_while_the_app_was_not_visible_wipes_what_was_remembered` cobre o caso.

## Comandos executados

Todos a partir de `example/android`, com Node 24.22.3.

| Comando | Resultado |
|---|---|
| `./gradlew :react-native-location-confidence:testDebugUnitTest` | exit 0. `CoreEngineTest` 12/12, `EngineCoordinatorTest` 23/23 |
| `./gradlew :react-native-location-confidence:connectedDebugAndroidTest -PreactNativeArchitectures=arm64-v8a` | exit 0. Os mesmos 35 testes, 0 falhas, no emulador `Medium_Phone` (Android 17, arm64) |
| `./gradlew :app:assembleDebug -PreactNativeArchitectures=arm64-v8a` | exit 0 |
| `./gradlew :app:assembleRelease -PreactNativeArchitectures=arm64-v8a,x86_64` | exit 0, com R8 ligado. `liblocation-confidence.so`: 70 960 bytes (arm64), 76 744 bytes (x86_64) |
| `./gradlew :react-native-location-confidence:assembleRelease` | exit 0. O AAR contém só `liblocation-confidence.so` e as regras de R8; `libc++_shared.so` fica de fora |

O APK release foi instalado no emulador e rodou um cenário completo do simulador: a ponte JNI funciona com R8 (a regra em `consumer-rules.pro` preserva `NativeCore`).

## Verificação no emulador

Feita com o app de exemplo, tela "This phone", GPS do emulador fixado a 40 m do endereço. O que o Android registrou está em `adb shell dumpsys location`.

| Situação | O que se viu |
|---|---|
| Avaliar sem permissão | resposta imediata `unavailable`, `permission_not_determined`; nenhum diálogo; GPS nunca pedido |
| "Allow location" | o diálogo do sistema aparece só aqui; depois, `allowed, precise` |
| Avaliar com permissão | pedido de GPS em alta precisão a cada 1 s em nome do app; resposta `available`, `fresh`, 40 m, índice 75 com limite `single_fix`; em seguida `ProviderRequest[OFF]` |
| Cancelar no meio | registro do GPS criado às 20:19:29.702 e removido às 20:19:29.957; resposta `cancelled`, `request_cancelled` |
| Sessão coletando | pedido de GPS a cada 5 s; uma avaliação 20 s depois responde 98, sem o limite `single_fix` |
| Fechar a sessão | `ProviderRequest[OFF]` |
| App vai para o fundo com sessão coletando | registro removido 0,7 s depois de HOME; ao voltar, continua desligado |

## IDs normativos

Aprovados em teste automatizado, na JVM e no emulador: REC-001, 005, 011, 023; CON-002, 006, 007, 009, 010, 011, 012, 013, 015, 016, 017, 018, 019, 020, 023, 025, 028; GEO-012 (segunda barreira); PLT-003, 005, 009, 015; SES-001, 002, 008, 011. O cenário dourado tem os mesmos números do teste da fachada em C++, agora também em arm64.

Verificados à mão no emulador (tabela acima): PLT-001, PLT-004, PLT-015, PLT-032 (R8), CON-004 na parte de remover as atualizações, SES-006 e CON-011.

Atendidos por construção, sem teste que os exercite: PLT-008 (`onLocationAvailability` não é usado, então um aviso de "indisponível" não encerra nada) e PLT-012 (no Android 7+ sempre existe o sinal de mock, então `mock unknown` não ocorre nesta plataforma).

Não verificados: PLT-002 (permissão só aproximada) e PLT-011 (leitura marcada como mock) pelo caminho real. O mapeamento existe em `FusedLocationSource` e o comportamento do motor para os dois casos está testado no core e aparece no simulador, mas nenhum dos dois foi provocado no GPS do emulador. PLT-013 e PLT-014 dependem do sensor de atividade, que ficou para a v1.1.

## Não feito

- **Sensor de atividade no Android** (Activity Recognition): adiado para a v1.1 por D-02. `motion` responde `unsupported` e todo relatório leva `motion_unavailable`. No modo simulação a atividade pode ser informada pelo app, e o motor a usa.
- **Aparelho físico.** Tudo foi verificado em emulador. Falta medir em aparelho o tempo até a primeira leitura (D-03) e o comportamento com economia de bateria (PLT-010).
- **Play services ausente** só foi exercitado com o estado falso nos testes, não em um aparelho sem Play services.
- **CON-026** (exceção Kotlin dentro de callback): não há teste que injete a exceção. As chamadas do Play services que podem falhar estão dentro de `try`/`catch` e viram `onFailure()`.
- **Fixtures em JSON**: como na Fase 3, o cenário dourado é repetido à mão.

## Riscos e próximo passo

- No Android, "permissão negada" só é distinguível de "nunca pedida" depois que o app pediu nesta execução. Antes disso a resposta é `not_determined`. É limitação da plataforma e vai para a documentação.
- O Android avisa que o app saiu da tela cerca de 0,7 s depois; nesse intervalo o GPS ainda está ligado.
- Entre o commit que troca o contrato Codegen e o fim da fase de iOS, o lado iOS não compila: ele ainda implementa o contrato do spike.
- Próximo: fachada TypeScript (Fase 6) e app de exemplo (Fase 7), que já foram usados para a verificação acima; depois iOS (Fase 5).
