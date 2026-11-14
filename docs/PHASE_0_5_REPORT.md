# Fase 0.5 — spike de integração: relatório

Data: 14/11/2026. Autorizada pelo owner na mesma data.

## Objetivo

Provar, antes de escrever o core, que a cadeia `JS → TurboModule → plataforma → fachada C → core C++` compila, linka e roda em release nas duas plataformas, e descobrir quais tipos o Codegen instalado realmente aceita.

## Decisões adotadas

- Scaffold `create-react-native-library@0.64.0`, tipo `turbo-module`, linguagens `kotlin-objc`, exemplo vanilla, React Native 0.88.1. Pacote `react-native-location-confidence`, pod `LocationConfidence`, package Java `com.locationconfidence`.
- Fachada C (`cpp/include/location_confidence/lce.h`) como única entrada do core. Hoje só `lce_abi_version()`.
- Android: `.so` próprio (`liblocation-confidence.so`) sem link contra React Native; natives registrados em `JNI_OnLoad`; classe mantida por `consumer-rules.pro`; STL `c++_shared` fornecida pelo app.
- iOS: `lce.h` é o único header público do pod, o que o coloca no umbrella header e o torna visível ao Swift. `.mm` do TurboModule chama Swift (`@objc`), Swift chama C. Sem wrapper ObjC++ intermediário.
- Node fixado em 24.22.3 (`.nvmrc`), instalado via nvm sem alterar o default do sistema. NDK 27.1.12297006 instalado ao lado do 28.2.

## Arquivos criados ou modificados

Criados: `cpp/include/location_confidence/lce.h`, `cpp/src/CFacade.cpp`, `cpp/CMakeLists.txt`, `cpp/tests/AbiProbe.cpp`, `android/CMakeLists.txt`, `android/consumer-rules.pro`, `android/src/main/cpp/JniBindings.cpp`, `android/src/main/java/com/locationconfidence/core/NativeCore.kt`, `ios/CoreBridge.swift`.

Modificados: `LocationConfidence.podspec`, `android/build.gradle`, `android/src/main/java/com/locationconfidence/LocationConfidenceModule.kt`, `ios/LocationConfidence.mm`, `src/NativeLocationConfidence.ts`, `src/index.tsx`, `example/src/App.tsx`, `example/android/app/build.gradle` (R8 ligado em release), `package.json` (exclusões de `files`), `.nvmrc`.

Removidos: `src/multiply.tsx`, `src/multiply.native.tsx` (exemplo do template).

Os membros `spike*` do spec Codegen e a tela do exemplo são descartáveis; serão substituídos pelos DTOs reais.

## Comandos executados e resultado

| # | Comando (resumo) | Exit | Observação |
|---|---|---:|---|
| 1 | `cmake` + build + `ctest` do core em host, `-std=c++17` | 0 | 1/1 passou |
| 2 | idem, `-std=c++20` | 0 | 1/1 passou |
| 3 | `yarn typecheck` | 0 | — |
| 4 | `yarn lint` | 0 | — |
| 5 | `./gradlew assembleDebug -PreactNativeArchitectures=arm64-v8a,x86_64` | 0 | Codegen Android gerou o spec; CMake/NDK 27.1 compilou o core |
| 6 | `./gradlew assembleRelease` com R8, mesmas ABIs | 0 | ver verificações abaixo |
| 7 | `pod install` (pods como bibliotecas estáticas) | 0 | Codegen iOS gerou o spec |
| 8 | `xcodebuild` Release, simulador arm64, bibliotecas estáticas | 0 | BUILD SUCCEEDED |
| 9 | `USE_FRAMEWORKS=static pod install` + `xcodebuild` Release | 0 | BUILD SUCCEEDED |
| 10 | instalar e abrir o app Release no simulador iPhone 17 Pro (iOS 26.1), nas duas configurações | 0 | tela mostrou `abiVersion: 1` e o eco completo |

A primeira execução do item 1 falhou por erro meu no probe de teste (faltava `<cstdint>`); corrigido e reexecutado.

Verificações sobre o APK release:

- contém `lib/arm64-v8a/liblocation-confidence.so` e `lib/x86_64/liblocation-confidence.so`;
- segmentos `LOAD` alinhados em `0x4000` (16 KB) nas duas ABIs;
- `JNI_OnLoad` exportado;
- `mapping.txt` do R8: `com.locationconfidence.core.NativeCore` manteve o nome, e `nativeAbiVersion` não foi renomeado.

## Tipos Codegen efetivamente compilados (RN 0.88.1, Android e iOS)

| Construção | Onde | Resultado |
|---|---|---|
| `Promise<number>` | retorno | compilou; observado em execução no iOS |
| `Promise<objeto>` com objeto aninhado, `string[]`, array de objetos, `number \| null`, `string \| null`, `boolean` | retorno | compilou; observado em execução no iOS |
| `string` | argumento | compilou |
| objeto com `string \| null`, `number`, `boolean`, união de literais string, `string[]` | argumento | compilou; iOS gerou struct com acessores, Android recebe `ReadableMap` |
| método `void` síncrono com argumento `string` | — | compilou |

Como previsto, o tipo de retorno não é verificado no lado nativo em nenhuma plataforma (mapa/dicionário livres). A garantia de forma fica com o mapper TS.

## Respostas que o spike deu

| Pergunta | Resposta |
|---|---|
| Swift dentro do pod enxerga a fachada C sem wrapper ObjC++? | Sim, nas duas configurações de pod. ADR-013 confirmada; plano B não é necessário |
| `.mm` do TurboModule consegue chamar Swift em pod misto? | Sim, com import condicional de `-Swift.h` |
| Core compila como C++17 e como C++20? | Sim |
| R8 preserva a ponte JNI com `RegisterNatives` + regra de keep? | Preserva nomes; execução no Android não foi verificada |
| Scaffold precisa de patch no app consumidor? | Não. Alterações ficaram no pacote; no exemplo só liguei o R8 |

## Não feito ou não verificado

- **Execução no Android.** Não há AVD nem dispositivo Android conectado. O caminho JS → Kotlin → JNI → C foi compilado, linkado e inspecionado, mas não executado. `JNI_OnLoad` e `System.loadLibrary` só serão provados em runtime.
- **Medição de latência one-shot contra stream** (condição da ADR-015). Exige código de localização e dispositivo; fica para o início das Fases 4/5. ADR-015 segue condicional.
- **Build para iPhone físico** e ABIs `armeabi-v7a`/`x86`. Só simulador arm64 e as duas ABIs Android listadas.
- **CI.** O workflow do template existe e não foi tocado; não há job de C++ em host. Nada foi enviado a remoto.
- **Jest.** O scaffold aplicou só ESLint; Jest entra na Fase 6.
- **Privacy manifest do pod.** Ainda não há uso de API de uptime; entra com o código de tempo.
- Nenhum teste normativo da SPEC foi implementado. A evidência de build se relaciona a PLT-031, PLT-032 (parcial: sem execução) e PLT-033 (simulador).
- Nada foi commitado.

## Pendências pequenas encontradas

- `lce_abi_version` está exportado no `.so`; aplicar `-fvisibility=hidden` e exportar só `JNI_OnLoad`.
- `Info.plist` do exemplo tem `NSLocationWhenInUseUsageDescription` vazio (aviso do Xcode); preencher antes de qualquer código de localização.
- `peerDependencies` do template estão como `*`; restringir quando a matriz for validada.
- Avisos de API obsoleta do Gradle vindos do template/plugin do RN, não do nosso código.

## Próximo gate

Fase 1 (core base em C++: tipos, validação, Haversine, ring buffer, dedup, score v1, classificação de relação; testes GEO, VAL, HIS, SCR em host). Requer aprovação explícita. Recomendo antes disponibilizar um emulador ou aparelho Android para fechar a verificação de runtime pendente.
