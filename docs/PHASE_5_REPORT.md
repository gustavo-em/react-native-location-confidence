# Fase 5 — iOS: relatório

Data: 16/11/2026. Branch `phase/5-ios`, a partir de `phase/7-example`. A fase 5 foi executada depois das fases 6 e 7, que serviram para verificar o Android.

## Objetivo

Ligar o mesmo motor C++ ao iPhone. O lado iOS é um gêmeo do lado Android: os mesmos nomes, a mesma divisão em arquivos, os mesmos testes. O que muda é só como cada plataforma fala com o GPS e com o motor.

## Ordem das chamadas

```text
JavaScript
  |  NativeLocationConfidence.getReport(requestId, request)
  v
LocationConfidence.mm            Objective-C++: recebe os tipos gerados pelo React Native (que são C++)
  |                              e os transforma em dicionários
  v
LocationConfidenceBridge.swift   monta as peças uma vez; dicionários <-> tipos Swift
  v
EngineCoordinator.swift          fala com o motor e depois OBEDECE o motor (igual ao Kotlin)
  v
CoreEngine.swift                 tipos Swift <-> structs de lce.h
  v
o motor C++                      todas as decisões
```

No Android, entre o Kotlin e o motor há dois arquivos (`CoreEngine.kt` e `JniBindings.cpp`) e os valores atravessam como listas de números, porque o Kotlin não lê um struct de C. O Swift lê: `CoreEngine.swift` preenche `lce_request_settings` e lê `lce_report` campo a campo, sem lista e sem número de conferência.

Por que existe um arquivo `.mm`: o React Native gera, para os argumentos, tipos em C++ (`JS::NativeLocationConfidence::NativeRequest`). O Swift não enxerga C++. O `.mm` é a única coisa que enxerga os dois lados, e não faz mais nada além de converter.

## Arquivos

| Arquivo | Papel | Gêmeo no Android |
|---|---|---|
| `ios/Models/Models.swift` | os tipos que vão e voltam do motor | `models/Models.kt` |
| `ios/Ports/Ports.swift` | o que o coordenador precisa do mundo | `ports/Ports.kt` |
| `ios/Core/CoreEngine.swift` | tipos Swift de um lado, structs de C do outro | `core/CoreEngine.kt` + `JniBindings.cpp` |
| `ios/Application/EngineCoordinator.swift` | fala com o motor e obedece; modo simulação | `application/EngineCoordinator.kt` |
| `ios/Adapters/CoreLocationSource.swift` | o GPS de verdade (Core Location) | `FusedLocationSource.kt` |
| `ios/Adapters/CoreMotionActivitySource.swift` | a atividade do aparelho (Core Motion) | `NoPhoneActivitySource.kt` |
| `ios/Adapters/IOSDeviceState.swift` | permissões, precisão, chave de localização, primeiro plano | `AndroidDeviceState.kt` |
| `ios/Adapters/DispatchSerialQueue.swift`, `MonotonicClock.swift` | fila e relógio | `SingleThreadQueue.kt`, `ElapsedRealtimeClock.kt` |
| `ios/Bridge/LocationConfidenceBridge.swift` | o que o `.mm` chama | `LocationConfidenceModule.kt` |
| `ios/LocationConfidence.mm` | o que o JavaScript enxerga | — |
| `ios/PrivacyInfo.xcprivacy` | manifesto de privacidade | — |
| `ios/Tests/` | os testes Swift | `android/src/sharedTest/` |
| `Package.swift` | só para rodar os testes no Mac | — |

## O que é diferente no iOS

- **Hora da leitura.** O iOS carimba cada localização com o relógio de parede, o que o usuário pode mudar nos ajustes. O relógio do motor é outro (`CLOCK_MONOTONIC`, que não anda para trás e continua contando com o aparelho dormindo). O adaptador calcula "esta leitura tem 300 ms, então foi tirada há 300 ms no relógio do motor" e marca a hora como estimada. Por isso todo relatório no iOS traz `ageReliability: 'estimated'`; no Android é `monotonic`.
- **Intervalo.** O iOS não tem "uma leitura a cada N ms". Ele entrega no ritmo dele e o motor guarda o que precisa (R-09).
- **Atividade do aparelho.** Existe (Core Motion), ao contrário do Android nesta versão. Ela só conta como disponível quando a permissão já foi dada; assim uma avaliação nunca faz aparecer o diálogo de "Movimento e Preparo Físico". Quando o iOS diz duas coisas ao mesmo tempo ("em veículo" e "parado", no sinal fechado), vale a que permite o movimento mais rápido.
- **Localização simulada.** O iOS informa quando a localização foi produzida por software (Simulador, arquivo GPX no Xcode). Isso vira `possible_mock_location`. No Simulador toda localização vem marcada assim.
- **Segundo plano.** `didEnterBackground` e `willEnterForeground`. Um diálogo de permissão ou a Central de Controle por cima do app não contam como segundo plano (R-08).
- **Permissão perdida.** Quando a permissão muda com o app aberto, ou quando o app volta para a tela, o coordenador confere se a localização foi retirada e avisa o motor, que apaga o que lembrava.

## Como os testes rodam

`Package.swift`, na raiz, existe só para isto:

```sh
swift test
```

Ele compila o motor C++ e o código Swift que não precisa de iPhone (o invólucro do motor, o coordenador, a fila e o relógio) e roda `ios/Tests` no próprio Mac, sem simulador. São os mesmos roteiros dos testes Kotlin, com o motor de verdade por baixo. O cenário dourado dá os mesmos números do C++ e do Kotlin.

A biblioteca não é distribuída como pacote Swift: os apps a recebem pelo CocoaPods, e `Package.swift` não entra no pacote npm.

O que só existe no iOS (Core Location, Core Motion, a ponte com o React Native) é compilado com o app de exemplo e foi exercitado no Simulador.

## Comandos executados

| Comando | Resultado |
|---|---|
| `swift test` (raiz) | exit 0. 37 testes, 0 falhas: `CoreEngineTests` 13, `EngineCoordinatorTests` 24 |
| `pod install` (em `example/ios`) | exit 0. 88 pods |
| `xcodebuild ... -configuration Debug -sdk iphonesimulator` | `BUILD SUCCEEDED`, sem avisos nos arquivos da biblioteca |
| `xcodebuild ... -configuration Release -sdk iphonesimulator` | `BUILD SUCCEEDED` |
| `USE_FRAMEWORKS=static pod install` e `xcodebuild ... -configuration Release` | o pod foi compilado como `LocationConfidence.framework` e o app foi ligado em seguida, com os símbolos `lce_` do motor dentro. A linha `BUILD SUCCEEDED` não ficou registrada (o filtro que apliquei ao log a cortou); a conclusão vem dos produtos gerados e de seus horários. Depois, `pod install` normal para voltar ao padrão |

O app leva o `LocationConfidencePrivacy.bundle` com o manifesto de privacidade.

### Um tropeço do ambiente, sem relação com a biblioteca

O primeiro build Debug falhou na ligação (`Undefined symbols ... facebook::react::Sealable`) e, depois, o app fechava ao abrir (`Symbol not found ... hermes`). Causa: o exemplo usa os binários pré-compilados do React Native, que existem em variante debug e release. O último build do spike tinha sido Release; o `pod install` de hoje apagou o marcador que dizia qual variante estava instalada, e os scripts do React Native, sem marcador, supõem que é a debug. Ficou a variante release com um app debug. Escrever `Release` nos três marcadores (`.last_build_configuration`) fez os scripts trocarem as variantes no build seguinte. O passo a passo está no `CONTRIBUTING.md`.

## Verificação no Simulador

iPhone 17 Pro, iOS 26.1.

| Situação | O que se viu |
|---|---|
| As seis situações do simulador do exemplo | as mesmas respostas do Android (97, 60, 35, 60, 0, 15) |
| Tela "This phone" ao abrir | `not determined`; nenhum diálogo |
| "Allow location" | o diálogo do sistema aparece só aqui, com o texto do `Info.plist`; depois, `allowed, precise` |
| Avaliar | a seta de localização acende; resposta a 40 m do endereço (o ponto fixado no Simulador), `possible_mock_location`, índice 0: o Simulador marca toda localização como simulada |
| Cancelar no meio | `cancelled`, `request_cancelled`; a seta de localização some segundos depois |
| Outro app na frente durante a avaliação | ao voltar: `cancelled`, `app_backgrounded` |

## IDs normativos

Aprovados em teste automatizado (`swift test`): REC-001, 005, 011, 023; CON-002, 006, 007, 009, 010, 011, 012, 013, 015, 016, 017, 018, 019, 020, 023, 025, 028; GEO-012 (segunda barreira); PLT-018, 019 (permissão restrita ou negada: o GPS nunca é ligado), PLT-024 na parte "sem a permissão de atividade o sensor não é tocado"; SES-001, 002, 008, 011.

Verificados à mão no Simulador: PLT-016, PLT-022, PLT-024 (sem dados de movimento a avaliação segue), PLT-028, PLT-033 (build release com CocoaPods).

Atendidos por construção, sem teste que os exercite: PLT-020 (precisão negativa vira "sem precisão", e o motor descarta a leitura), PLT-021 (cada localização de um lote mantém a própria idade), PLT-023 (`sourceInformation` ausente vira "desconhecido"), PLT-025 (sem `NSMotionUsageDescription` o Core Motion nunca é tocado), PLT-027 (nada reabre uma avaliação quando a permissão muda), CON-024 e CON-027 (entregas atrasadas e falhas do Core Location passam pelo mesmo filtro e pelo mesmo caminho testados com o GPS falso).

Não se aplicam mais: PLT-026 (não há consulta ao histórico de movimento; só o fluxo ao vivo).

## Não feito

- **Aparelho físico.** Nada rodou em um iPhone de verdade.
- **Core Motion em execução.** O Simulador não tem coprocessador de movimento, então `CoreMotionActivitySource` compila, mas nunca recebeu um relato real. A lógica que usa a atividade é a do motor e está testada; a tradução do que o iOS entrega, não.
- **PLT-017** (precisão reduzida no iOS) não foi provocado no Simulador.
- **Testes dos adaptadores.** `CoreLocationSource` e `IOSDeviceState` não têm teste automatizado; foram exercitados pelo app.

## Riscos e próximo passo

- No iOS, `ageReliability` é sempre `estimated`. Se o usuário mudar a hora do aparelho entre a leitura e a entrega, a idade calculada fica errada; a leitura não é descartada por isso.
- A seta de localização do iOS continua acesa por alguns segundos depois de `stopUpdatingLocation`. É comportamento do sistema.
- Próximo: documentação pública e preparo de release (fase 8).
