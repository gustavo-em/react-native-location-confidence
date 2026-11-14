# COMPATIBILITY_PLAN — Fase 0

Data: 14/11/2026. Nada aqui foi compilado. "Verificado" significa lido na máquina ou no tarball publicado no npm; "a verificar" só fecha no spike de integração (Fase 0.5 proposta).

## 1. Estado do repositório

- Diretório contém apenas `CLAUDE_GeoReliant.md` e `SPEC_Location_Confidence_Engine_v2.md`. Sem `package.json`, sem scaffold, sem git.
- `CLAUDE_GeoReliant.md` não é carregado automaticamente pelo Claude Code (o nome esperado é `CLAUDE.md`). Recomendo renomear quando o scaffold for criado.

## 2. Toolchain instalada (verificado)

| Item | Instalado | Exigido por RN 0.88.1 / scaffold | Situação |
|---|---|---|---|
| Node | 25.9.0 | RN: `^22.13.0 \|\| ^24.3.0 \|\| >=26.0.0`; create-react-native-library 0.64.0: `^22.22.2 \|\| ^24.15.0 \|\| >=26.0.0` | **Bloqueador.** Node 25 está fora das duas faixas. Instalar Node 24 LTS (>= 24.15) e fixar em `.nvmrc`. |
| npm / Yarn | 11.12.1 / 1.22.22 | scaffold usa Yarn moderno via corepack | A verificar no scaffold |
| JDK | OpenJDK 17.0.17 | AGP 9.2.1 | Compatível em princípio; a verificar |
| Android SDK platforms | 31, 33, 34, 35, 36, 37.0 | compileSdk 37, targetSdk 36, minSdk 24 | OK |
| Build-tools | 35.0.0, 36.0.0, 37.0.0 | 37.0.0 | OK |
| NDK | 28.2.13676358 | RN fixa `27.1.12297006` | **Divergente.** Instalar 27.1.12297006 (ou deixar o Gradle baixar) para não testar com NDK diferente do anunciado. |
| CMake (SDK) | 3.18.1, 3.22.1 | AGP usa o do SDK | OK |
| CMake (host) | 4.4.3 | testes C++ em host | OK; usar `cmake_minimum_required` >= 3.13 |
| Xcode | 26.1.1 (17B100) | a verificar | Único runtime de simulador: iOS 26.1 |
| Swift | 6.2.1 | — | Fixar `swift_version` do pod em modo de linguagem 5 (ver NATIVE_TRANSPORT_SPEC §6) |
| Apple clang | 17.0.0 | — | OK |
| CocoaPods | 1.16.2 | — | OK, mas exige `LANG=en_US.UTF-8` no shell |
| Ruby | 3.3.0 | — | OK |

## 3. Versões-alvo (verificado no npm em 14/11/2026)

| Pacote | Versão | Observação |
|---|---|---|
| react-native | 0.88.1 (latest) | peer `react ^19.3.0` |
| @react-native/codegen | 0.88.1 | parser lido; ver NATIVE_TRANSPORT_SPEC §2 |
| create-react-native-library | 0.64.0 | `SUPPORTED_REACT_NATIVE_VERSION = 0.88.0` |
| react-native-builder-bob | 0.44.1 | — |

Pins internos do RN 0.88.1 (`gradle/libs.versions.toml`, `scripts/cocoapods/helpers.rb`): minSdk 24, targetSdk 36, compileSdk 37, NDK 27.1.12297006, AGP 9.2.1, Kotlin 2.2.0, iOS mínimo 15.1, padrão C++ dos pods **c++20**.

## 4. Scaffold: variante recomendada

`create-react-native-library` 0.64.0 oferece, para Turbo Module, as linguagens `kotlin-objc` e `cpp`. `kotlin-swift` só existe para Nitro Modules.

**Recomendação:** tipo `turbo-module`, linguagens `kotlin-objc`, exemplo "App with Community CLI". Depois, manualmente:

1. Android: adicionar `externalNativeBuild`/CMake ao `android/build.gradle`. O template `kotlin-objc` **não** traz CMake; o template `cpp` traz, mas linka contra `jsi`/`reactnative`, o que nosso `.so` JNI não precisa.
2. iOS: manter a classe `.mm` do template como TurboModule; adicionar Swift ao pod (o podspec do template já inclui `ios/**/*.{h,m,mm,swift,cpp}`); estender `source_files` para `cpp/**/*.{h,hpp,cpp}`.
3. Core em `cpp/` na raiz do pacote, com `CMakeLists.txt` próprio para host.

Não existe variante oficial "Kotlin + Swift + core C++ para TurboModule". A resposta à pergunta 16.1-2 da SPEC é: nenhum scaffold entrega isso pronto; a montagem é manual e precisa ser provada por build.

**Alternativa considerada:** Nitro Modules (`kotlin-swift`). Dá Swift/Kotlin tipados sem camada ObjC++, mas impõe dependência de runtime `react-native-nitro-modules` ao app consumidor e usa interop Swift↔C++. Rejeitada para v1 por contrariar ADR-012 (TurboModule/Codegen oficial) e aumentar a superfície de dependências. Gatilho de revisão: se o spike mostrar que pod misto Swift + ObjC++ é inviável.

## 5. Matriz de compatibilidade proposta (nada disso é anunciável antes de build real)

| Eixo | Proposta inicial | Como validar |
|---|---|---|
| React Native | 0.88.x, New Architecture apenas | exemplo debug + release |
| Android | minSdk 24, ABIs `arm64-v8a` e `x86_64`; demais só se compiladas em CI | `assembleRelease` com R8 |
| iOS | 15.1+, device arm64 e simulador arm64 | `xcodebuild` Release; pods estáticos e `use_frameworks! :linkage => :static` |
| Expo | apenas development build; Expo Go não suportado | documentação; teste em Fase 8 se desejado |
| Play Services Location | versão a fixar no scaffold | build |

Faixa de `peerDependencies` só será ampliada (ex.: 0.86, 0.87) se o CI compilar cada versão.

## 6. Bloqueadores de compilação e permissão identificados

**Atualização após a Fase 0.5:** B1, B2, B3, B4 e B5 resolvidos (Node 24.22.3 via nvm, NDK 27.1.12297006 instalado, repositório git criado pelo scaffold, nome `react-native-location-confidence`, pod misto provado em release nas duas configurações). B6, B7 e B8 seguem abertos (B8 em parte: há emulador Android e simulador iOS; faltam aparelhos físicos). Evidência em `PHASE_0_5_REPORT.md`.

| # | Bloqueador | Ação |
|---|---|---|
| B1 | Node 25.9.0 fora da faixa de engines | Node 24 LTS |
| B2 | NDK 27.1.12297006 ausente | instalar |
| B3 | Sem git | `git init` no scaffold |
| B4 | Nome do pacote não decidido | `react-native-location-confidence` e `react-native-georeliant` retornam 404 no npm hoje (livres); decidir antes do scaffold, pois o nome entra em package Java, nome do pod e nome do spec Codegen |
| B5 | Pod misto Swift + ObjC++ + C++ sem prova de link | spike |
| B6 | iOS: uso de relógio de uptime exige declaração em `PrivacyInfo.xcprivacy` | incluir no pod (ver SPEC_REVIEW R-14) |
| B7 | Android: prompt de permissão exige Activity (`PermissionAwareActivity`) | erro estruturado quando não houver Activity |
| B8 | Sem dispositivos físicos confirmados | Fases 7–8 exigem um Android e um iPhone |
