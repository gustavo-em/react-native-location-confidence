# Fase 8 — documentação pública e preparo de release: relatório

Data: 16/11/2026. Branch `phase/8-release`, a partir de `phase/5-ios`.

Nada foi publicado. Não houve `npm publish`, tag, release nem push: o repositório não tem remoto configurado e essas ações dependem de pedido explícito.

## Objetivo

Deixar o pacote pronto para ser avaliado por quem vai publicá-lo: documentação pública em inglês, verificação do que o pacote npm carregaria, e os testes de cada camada ligados à integração contínua.

## O que foi feito

| Item | Arquivo |
|---|---|
| README público, em inglês | `README.md` |
| Histórico de versões | `CHANGELOG.md` |
| Como rodar os testes de cada camada; o tropeço dos binários pré-compilados no iOS | `CONTRIBUTING.md` |
| README do app de exemplo | `example/README.md` |
| Jobs de teste na integração contínua | `.github/workflows/ci.yml` |
| Modelo de bug pede os reason codes e avisa para não colar coordenadas reais | `.github/ISSUE_TEMPLATE/bug_report.yml` |
| O lint ignora pastas de build | `eslint.config.mjs` |

O README diz, logo no começo, o que a biblioteca não é: o índice não é porcentagem nem probabilidade, não é prova de presença, não aprova nem recusa entrega, não é antifraude, não funciona em segundo plano. Também lista os pesos, as velocidades e os limites do índice, e avisa que são escolhas de projeto, não valores calibrados. A tabela de compatibilidade só traz o que foi de fato compilado e executado.

## O que a inspeção do pacote encontrou

`npm pack --dry-run` mostrou dois problemas, corrigidos no commit `fix(android): keep the library's tests out of apps that install it`:

1. Os testes Kotlin e o `CMakeLists.txt` que compila o motor para o computador iam dentro do pacote. Pior: o `build.gradle` fazia toda tarefa de teste unitário da biblioteca depender desse build. Um app que instalasse a biblioteca e rodasse `./gradlew test` tentaria rodar `cmake` na própria máquina.
2. O arquivo solto `android/src/main/cpp/Untitled`, que não está no git, também ia: o npm empacota pela lista `files`, não pelo git.

Depois da correção: 101 arquivos, 164 KB compactado. Entram `src`, `lib`, `android` (sem testes), `ios` (sem testes), `cpp` (sem testes e sem os espelhos de estudo), o podspec, `README.md` e `LICENSE`. Não entram `docs`, `example`, `Package.swift` nem nada de build.

O arquivo `Untitled` continua onde estava. Ele só deixou de ir para o pacote; removê-lo ou movê-lo fica com você.

## Comandos executados

Estado final, tudo a partir deste branch.

| Camada | Comando | Resultado |
|---|---|---|
| Motor C++ | `cmake -S cpp -B <dir> -DLCE_BUILD_TESTS=ON && cmake --build <dir> && <dir>/lce_tests` | exit 0. 354 casos, 498 453 verificações |
| TypeScript | `yarn test` | exit 0. 86 testes |
| TypeScript | `yarn typecheck`, `yarn lint`, `yarn prepare` | exit 0 nos três |
| Kotlin, na JVM | `./gradlew :react-native-location-confidence:testDebugUnitTest` | exit 0. 35 testes |
| Kotlin, no emulador | `./gradlew :react-native-location-confidence:connectedDebugAndroidTest` | exit 0. Os mesmos 35 testes (Android 17, arm64) |
| Swift, no Mac | `swift test` | exit 0. 37 testes |
| Android, app | `./gradlew :app:assembleRelease` (arm64-v8a, x86_64; R8 ligado) | exit 0 |
| iOS, app | `xcodebuild` Debug e Release para o Simulador | `BUILD SUCCEEDED` |
| Pacote | `npm pack --dry-run --json --ignore-scripts` | 101 arquivos |

Os builds C++20 e com AddressSanitizer/UndefinedBehaviorSanitizer do motor foram executados nas fases 1 a 3, e o código do motor não mudou desde então.

## Integração contínua

Foram acrescentados quatro jobs ao `ci.yml`: `test-core` (motor em C++17, C++20 e com sanitizers), `test-js` (Jest), `test-android` (testes Kotlin na JVM) e `test-ios` (`swift test`).

**Nenhum job foi executado.** Não há remoto, então a integração contínua nunca rodou. Os comandos de cada job são os mesmos que passaram nesta máquina, mas o arquivo em si não foi testado.

## IDs normativos

Atendidos e verificados:

- PUB-003: não há chamada de log no código nativo nem no core (conferido por busca no código).
- PUB-004, 005: `clearHistory` e `stopSession`, testados no core e nas duas plataformas.
- PUB-006 a 013: afirmações obrigatórias no README (índice não é porcentagem; não é prova de presença; permissões do Android com preciso e aproximado; textos do `Info.plist` no iOS; só em primeiro plano; mock não é antifraude; Expo Go não suportado; só versões compiladas na tabela).
- PUB-014: pacote inspecionado.
- PUB-020: a biblioteca não declara permissão no Manifest; o pod traz manifesto de privacidade; o README tem a seção de privacidade.
- PUB-021: o README não traz número de desempenho, porque nada foi medido.
- PUB-022: o modelo de bug pede reason codes e não coordenadas.
- PUB-023: as coordenadas dos testes ficam em volta do ponto (0, 0); o exemplo usa uma praça pública.
- PUB-024: `LICENSE` (MIT) presente.

Atendidos por construção, sem teste que os exercite:

- PUB-001 e PUB-002: a biblioteca não tem código de rede nem de arquivo. Não há teste que monitore requisições ou o disco.

Em aberto:

- PUB-015: instalar o pacote em um app fora do monorepo e compilar. Não foi feito.
- PUB-016: versão e tag. Depende de pedido.
- PUB-017, 018: os jobs existem e nunca rodaram.
- PUB-019: só há lint de TypeScript. Kotlin, Swift e C++ não têm linter configurado; os builds do motor usam `-Wall -Wextra -Werror`.
- PUB-025: `peerDependencies` aceita qualquer versão do React Native (`*`), mas só a 0.88.1 foi validada. Restringir a faixa muda quem consegue instalar o pacote, então a decisão é sua.
- PUB-026: as arquiteturas `armeabi-v7a` e `x86` estão na lista padrão do Gradle e não foram compiladas. O README só anuncia `arm64-v8a` e `x86_64`.

## O que falta para um release de verdade

Do gate da fase no plano: "P0 todos verdes; exceções documentadas; dois dispositivos físicos; `npm pack --dry-run` inspecionado".

- **Dois aparelhos físicos.** Nada rodou em aparelho. É o item mais importante: o tempo até a primeira leitura (D-03), o comportamento com economia de bateria e o Core Motion só aparecem em aparelho.
- **Core Motion.** O adaptador do iOS nunca recebeu um relato real.
- **App fora do monorepo** (PUB-015).
- **Integração contínua rodando.**
- **Stress e benchmark**, previstos no plano, não foram feitos.
- **Sua ciência** dos achados R-24 a R-35 e da ADR-019.

## Riscos

- Os números do índice (pesos, velocidades, limites) nunca foram comparados com dados reais de entrega.
- No Android não há leitura da atividade do aparelho. Sem ela, e sem velocidade no GPS, a área onde o celular pode estar cresce 45 m por segundo: poucos segundos de silêncio bastam para "inconclusivo". É o comportamento projetado, e vai surpreender quem não ler o README.

## Adendo de 17/11/2026: nomes novos (D-10) e verificação completa

Branch `refactor/request-and-report`, a partir de `phase/8-release`. Nada foi publicado.

**Objetivo.** Trocar `evaluation` por `request` e `assessment` por `report` no código inteiro, a pedido do owner (decisão D-10 em `SPEC_REVIEW.md`, que traz a tabela de nomes), e dar ao app de exemplo a escolha entre inglês e português (relatório da Fase 7).

**Como foi feito.** A troca de nomes foi feita em três commits, um por camada, cada um verificado antes do seguinte: motor C++; fachada C, Kotlin e Swift; contrato Codegen, TypeScript e exemplo. Depois vieram o seletor de idioma, a documentação e os vídeos.

Estado final, tudo a partir deste branch:

| Camada | Comando | Resultado |
|---|---|---|
| Motor C++, em C++17, em C++20 e com sanitizers | `cmake --build <dir> && <dir>/lce_tests`, nos três | exit 0 nos três. 354 casos, 498 453 verificações |
| Espelhos de estudo | `node --experimental-transform-types --no-warnings cpp/src/<Nome>.study.ts`, os seis | exit 0 nos seis |
| TypeScript | `yarn test` | exit 0. 86 testes |
| TypeScript | `yarn typecheck`, `yarn lint`, `yarn prepare` | exit 0 nos três (ver a nota) |
| Kotlin, na JVM | `./gradlew :react-native-location-confidence:testDebugUnitTest --rerun` | exit 0. 35 testes |
| Kotlin, no emulador | `./gradlew :react-native-location-confidence:connectedDebugAndroidTest` | exit 0. Os mesmos 35 testes (Android 17, arm64) |
| Swift, no Mac | `swift test` | exit 0. 37 testes |
| Android, app | `./gradlew :app:assembleRelease` (arm64-v8a, x86_64; R8 ligado) | exit 0 |
| iOS, app | `xcodebuild` Release para o Simulador | `BUILD SUCCEEDED` |
| Pacote | `npm pack --dry-run --json --ignore-scripts` | 101 arquivos, 164 KB compactado, nenhum nome antigo |

Nota sobre o lint. Na primeira execução `yarn lint` saiu com erro: seis trechos de `Report.study.ts` e `ProximityRequest.study.ts` ficaram mais curtos com os nomes novos e o formatador pedia para juntar as linhas. Foi corrigido com `yarn lint --fix` (só quebra de linha), os dois espelhos foram executados de novo e `yarn lint` passou.

Os números de testes são os mesmos de antes da troca. Nenhum teste foi removido e nenhuma expectativa mudou: só os nomes.

**Não feito.**

- O build Debug do iOS não foi refeito depois da troca; o Release foi.
- Aparelho físico, Core Motion real e integração contínua continuam como estavam.

**Riscos.**

- A API pública agora difere da SPEC §4.2. Quem ler a SPEC e depois o código precisa da tabela de D-10.
- Os documentos da Fase 0 continuam com os nomes antigos, de propósito.

**Sobre o arquivo `Untitled`.** O arquivo solto `android/src/main/cpp/Untitled`, citado acima, não está mais na pasta em 17/11/2026. Ele nunca esteve no git, nem no índice, então nenhum comando de git desta sessão poderia tê-lo apagado. A linha que o exclui do pacote continua no `package.json` e é inofensiva.

## Publicação em 17/11/2026

A pedido do owner, o projeto foi publicado no GitHub e a biblioteca no npm. A tag e o release também foram pedidos por ele.

| O quê | Onde | Estado |
|---|---|---|
| Repositório | https://github.com/gustavo-em/react-native-location-confidence | público; só o branch `main` foi enviado |
| Pacote | `react-native-location-confidence@0.1.0` no npm | 101 arquivos, 160 kB compactado; `latest` aponta para a 0.1.0 |
| Tag e release | `v0.1.0` | aponta para o commit que foi publicado no npm |
| Integração contínua | primeira execução, no push do `main` | os 10 jobs passaram |

O que foi feito antes de publicar:

- **Histórico.** O rascunho do post do LinkedIn saiu do histórico publicado, por escolha do owner. Ele continua na pasta de trabalho, fora do git. Os branches locais antigos ainda o contêm e não foram enviados. Nenhum commit tem coautoria ou menção ao Claude, e todos usam o e-mail noreply do GitHub.
- **Documentação pública.** O README ficou curto, porque é o que o npm mostra: uma frase, a animação do app de exemplo, instalação e um exemplo de dez linhas. A referência completa foi para `docs/GUIDE.md`. `docs/COMPARISON.md` compara a biblioteca com as bibliotecas de localização estabelecidas; os dados foram lidos no dia, do registro do npm e das páginas de cada projeto, e as fontes estão listadas. O README continua com as afirmações obrigatórias (PUB-006 a 013).
- **Pacote empacotado.** O app de exemplo foi compilado contra os arquivos do pacote (`npm pack`, extraído para uma pasta separada), em vez da pasta do repositório: build release no Android (arm64-v8a) e no iOS (Simulador) passaram. Isso cobre o que o PUB-015 quer evitar (arquivo faltando no pacote), mas ainda não é um app criado fora do monorepo.

Como a publicação no npm aconteceu: a conta não tinha confirmação em duas etapas e o npm recusou as duas primeiras tentativas (erro 403). O owner ativou a confirmação e aprovou a publicação no navegador. O npm criou sozinho uma versão `0.0.0-stage`, um marcador temporário da publicação em etapas; ela não é uma versão da biblioteca.

IDs normativos que mudaram de estado:

- PUB-016 (versão e tag): feito.
- PUB-017 e PUB-018 (integração contínua): os jobs rodaram e passaram.
- PUB-015: coberto em parte, como descrito acima.

Continua em aberto:

- PUB-025: `peerDependencies` aceita qualquer versão do React Native; só a 0.88.1 foi validada.
- Aparelhos físicos, Core Motion real, stress e benchmark.
- O registro do npm mostra o e-mail da conta do owner como mantenedor do pacote. Trocar isso é configuração da conta dele.
