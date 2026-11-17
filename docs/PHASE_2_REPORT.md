# Fase 2 — modelos: relatório (concluída em 16/11/2026)

Branch `phase/2-models`. A pedido do owner, a fase foi feita em quatro passos, um por peça. Os passos 2a a 2c tiveram parada para leitura entre eles; no dia 16/11/2026 o owner pediu que o restante da implementação fosse feito de uma vez ("vou tentar entender tudo quando tudo terminar"), então o passo 2d e as fases seguintes não têm mais parada.

| Passo | Peça | Situação |
|---|---|---|
| 2a | Modelo de incerteza (`UncertaintyModel`) | feito |
| 2b | Detector de saltos (`JumpDetector`; na SPEC, `OutlierDetector`) | feito |
| 2c | Evidência de movimento (`PhoneActivityTimeline`; na SPEC, `MotionEvidenceAnalyzer`) | feito |
| 2d | Seletor de candidato e relatório (`ReadingSelection` e `Report`; na SPEC, `CandidateSelector` e `evaluateSnapshot`) | feito |

---

## Passo 2a — modelo de incerteza

Data: 14/11/2026.

### Objetivo

Responder "quão longe o celular pode ter ido desde que a leitura foi tirada?". O resultado é o `possibleMovementMeters` que o score e a relação dentro/fora já esperavam e que ninguém calculava.

### Decisões adotadas

- Três formas de estimar, da melhor para a pior: atividade conhecida; velocidade reportada pelo GPS junto com a leitura (decisão D-01); pior caso.
- Fórmula com velocidade reportada: `(velocidade + 2 × erro da velocidade) × tempo + 2,5 × tempo² / 2`, nunca acima do pior caso. Exige velocidade **e** erro da velocidade; só um dos dois não basta.
- `movementIsUnknown` é verdadeiro apenas no pior caso. Com velocidade reportada, o movimento conta como estimado com evidência.
- Toda estimativa, inclusive a de atividade conhecida, respeita o teto `velocidade máxima plausível × tempo`.
- Estilo de código legível, conforme pedido: nomes por extenso, condições nomeadas, comentários com a regra de negócio e exemplos numéricos.

Hipóteses fixadas onde a SPEC é omissa (somam-se a R-26):

| Ponto | Escolha |
|---|---|
| Velocidade de veículo e de "desconhecido" | usam `maxPlausibleSpeedMps` da avaliação (default 45), em vez de 45 fixo |
| Aceleração assumida no envelope | 2,5 m/s² |
| Fator de segurança sobre o erro da velocidade | 2 |
| Configuração inválida de velocidade máxima (zero, negativa, não numérica) | volta ao default |
| Idade negativa | tratada como zero |

### Arquivos

Criados: `cpp/src/UncertaintyModel.hpp`, `cpp/src/UncertaintyModel.cpp`, `cpp/tests/TestUncertainty.cpp`.
Modificado: `cpp/CMakeLists.txt`.

### Comandos executados e resultado

| # | Comando (resumo) | Exit | Resultado |
|---|---|---:|---|
| 1 | host C++17, `-Werror`, `lce_tests` | 0 | 127 casos, 0 falhas |
| 2 | host C++20 | 0 | 127 casos, 0 falhas |
| 3 | host C++17 com ASan + UBSan | 0 | 127 casos, 0 falhas |
| 4 | 7 mutações isoladas em `UncertaintyModel.cpp` | — | 7 de 7 detectadas |

Mutações aplicadas: retirar o teto na estimativa por velocidade reportada; zerar o fator de segurança; retirar o termo de aceleração; alterar a velocidade de caminhada; não tratar idade negativa; retirar o teto na atividade conhecida; aceitar velocidade sem erro. Uma delas precisou ser reexecutada isoladamente porque a primeira rodada colidiu com outra execução no mesmo diretório temporário; o resultado reportado é o da reexecução.

### IDs normativos

Implementados e aprovados em host:

- MOT-005, 006, 007, 008, 009 (limites por atividade e pior caso).
- MOT-016 (parado não torna preciso um fix impreciso).
- SCR-028 (idade maior não reduz a incerteza).
- SCR-008, agora completo: trocar "parado" por "desconhecido" nunca aumenta o score.
- Novos, do envelope de velocidade: MOT-026 (velocidade reportada reduz a estimativa), MOT-027 (velocidade sem erro não é confiável), MOT-028 (nunca passa do pior caso), MOT-029 (nunca diminui com a idade), MOT-030 (perder a velocidade não reduz a estimativa nem aumenta o score).

Parcial: MOT-002 (o limite de 0,5 m/s para "parado" está feito; a exigência de cobertura contínua vem no passo 2c).

### Não feito

- Android e iOS não foram recompilados com o arquivo novo; fica para o fim da Fase 2. O arquivo é C++ puro e compila em C++17 e C++20 no host.
- A integração por trechos ("parado 2 s, desconhecido 18 s") depende do passo 2c.
- Nada chama o modelo ainda fora dos testes; o seletor (2d) será o primeiro a usá-lo.

### Atualização — nomes e estrutura simplificados (15/11/2026)

A pedido do owner, o módulo foi refatorado para leitura mais fácil, sem mudar resultados.

| Antes | Agora |
|---|---|
| `estimatePossibleMovement` | `calculateDistancePhoneCouldHaveMoved` |
| `totalUncertaintyMeters` | `calculateHowFarPhoneCouldBeFromReading` |
| `topSpeedFor` | `speedForPhoneActivity` |
| `distanceFromReportedSpeed` | `calculateDistanceUsingSpeedReportedByGps` |
| `usableFastestSpeed` | `replaceInvalidSpeedWithDefault` |
| `smallerOf` | removida; virou um `if` no fim da função principal |
| `MovementKind` | `PhoneActivity` (`StandingStill`, `InVehicle`, ...) |
| `MovementEstimate` / `possibleMovementMeters` / `basis` | `DistancePhoneCouldHaveMoved` / `meters` / `howItWasCalculated` |
| `MovementEstimateBasis` | `HowDistanceWasCalculated` |
| `movementIsUnknown()` | `weOnlyAssumedTheWorst()` |
| `ageOfReadingMs` | `millisecondsSinceReading` |

Estrutura: a função principal passou de três blocos com `return` no meio para um único `if / else if / else`, com o limite ("nunca mais que a maior distância possível") aplicado uma vez no fim.

Verificação: 128 casos em C++17, C++20 e com sanitizers; 7 mutações detectadas, cada uma com recompilação forçada do arquivo alterado e da biblioteca (uma primeira rodada em lote reaproveitou um binário antigo e foi descartada). Um teste novo fixa a ordem das opções (atividade conhecida vem antes da velocidade do GPS), que antes não era coberta: a mutação que trocava a ordem passava despercebida.

Também existe `cpp/src/UncertaintyModel.study.ts`, espelho de estudo em TypeScript pedido pelo owner. Não faz parte da biblioteca (fora do build e do pacote npm) e foi comparado com o C++ em 20.000 entradas geradas, sem diferenças.

---

## Passo 2b — detector de saltos

Data: 16/11/2026.

### Objetivo

Achar as leituras do histórico que parecem erro do GPS ("salto": o celular aparece longe e volta em seguida). O detector só coloca um rótulo em cada leitura. Não remove, não altera e não inventa posição.

### Decisões adotadas

- **Regra do par** (SPEC §6.3, sem mudança): duas leituras se contradizem quando `distância entre elas > velocidade máxima × tempo entre elas + erro do GPS de uma + erro do GPS da outra`. A comparação é estrita (`>`). O tempo entre elas é sempre positivo, qualquer que seja a ordem de chegada. Com tempo zero sobra só a soma dos erros; nada é dividido.
- **Regra do rótulo**: cada leitura conta quantas leituras estão do lado dela (ela mesma mais as que não a contradizem) e quantas estão contra (as que a contradizem).

| Situação | Rótulo |
|---|---|
| nenhuma contra | `LooksFine` |
| mais contra do que do lado | `ProbablyGpsJump` |
| qualquer outro caso (inclui empate) | `InConflict` |

- Todas as leituras do histórico são comparadas entre si (até 45 pares), não só as vizinhas no tempo.
- O arquivo se chama `JumpDetector`, e não `OutlierDetector` como na SPEC, pelo mesmo critério de nomes simples usado nos outros módulos.
- `replaceInvalidSpeedWithDefault` deixou de ser privada do `UncertaintyModel.cpp` e passou a ser anunciada no `UncertaintyModel.hpp`, porque o detector precisa da mesma regra (velocidade máxima inválida volta a 45).

Hipóteses fixadas onde a SPEC é omissa (somam-se a R-26):

| Ponto | Escolha | Onde a SPEC é omissa |
|---|---|---|
| Qual leitura de um par contraditório é a suspeita | a que tem mais leituras contra do que do lado | §6.3 dá exemplos (A-B-A, A-B-B-B) sem regra geral |
| Empate, inclusive duas leituras sozinhas que se contradizem | `InConflict` nas duas; nenhuma é chamada de salto | JMP-003 pede suspeita sem certeza |
| Leitura contradita, mas com a maioria do lado dela | `InConflict`, não `LooksFine` | JMP-013 e JMP-015 pedem que o grupo não seja "autenticado" |
| "Suspeita forte de salto" (§6.5 e gates de D-06) | corresponde a `ProbablyGpsJump`; `InConflict` é a suspeita fraca | o termo é usado sem definição |
| Três ou mais leituras que se contradizem todas entre si | todas `ProbablyGpsJump` | não previsto |

Consequência conhecida: uma leitura ruim mantém `InConflict` nas leituras que ela contradiz enquanto estiver no histórico (até 2 minutos). É a escolha conservadora; ligar os rótulos ao score e aos gates é trabalho do passo 2d.

### Arquivos

Criados: `cpp/src/JumpDetector.hpp`, `cpp/src/JumpDetector.cpp`, `cpp/tests/TestJumps.cpp`.
Modificados: `cpp/CMakeLists.txt`, `cpp/src/UncertaintyModel.hpp`, `cpp/src/UncertaintyModel.cpp`.

### Comandos executados e resultado

| # | Comando (resumo) | Exit | Resultado |
|---|---|---:|---|
| 1 | host C++17, `-Werror`, `lce_tests` | 0 | 155 casos, 0 falhas |
| 2 | host C++20 | 0 | 155 casos, 0 falhas |
| 3 | host C++17 com ASan + UBSan | 0 | 155 casos, 0 falhas, nenhum aviso dos sanitizers |
| 4 | 16 mutações isoladas em `JumpDetector.cpp` e `JumpDetector.hpp` | — | 16 de 16 detectadas |

Cada mutação rodou em uma cópia nova de `cpp/` com pasta de build nova, para não reaproveitar binário antigo. Mutações aplicadas: `>=` no limite; erros do GPS fora do limite; só um erro do GPS; tempo negativo; milissegundos divididos por 100; empate virando salto; leitura que não conta a si mesma; leitura comparada consigo mesma; flag de provedor invertida; velocidade máxima customizada ignorada; velocidade inválida não substituída; saltos não contados; conflitos não contados; número de leituras não preenchido; `someReadingsContradictEachOther` ignorando saltos; contradição contada do lado errado.

A suíte passou de 128 para 155 casos: os 27 novos estão em `TestJumps.cpp`. O caso de propriedade roda 3.000 históricos gerados com semente fixa e verifica que as contagens fecham e que uma leitura nova nunca limpa uma dúvida já existente.

### IDs normativos

Implementados e aprovados em host: JMP-002, 003, 004, 005, 006, 007, 008, 009, 010, 011, 012, 015, 016, 017, 022, 023, 024.

Parte do detector feita; a parte de seleção ou de score fica para o passo 2d:

| ID | Feito aqui | Falta (2d) |
|---|---|---|
| JMP-001 | B recebe `ProbablyGpsJump` | B não ser escolhido |
| JMP-013 | nenhuma leitura fica `LooksFine` | score sem valor alto |
| JMP-014 | a leitura isolada do fim recebe `ProbablyGpsJump` | a série boa continuar sendo a escolhida |
| JMP-019 | flag `readingsFromDifferentProvidersContradict` | seleção por evidência e reason `provider_conflict` |
| JMP-021 | as leituras boas não ganham apoio e passam a `InConflict` | score não aumentar |

Pendentes: JMP-018 (velocidade alta reportada com coordenadas iguais), JMP-020 (proibição de interpolar, que é da seleção) e SCR-010.

### Não feito

- Android e iOS não foram recompilados com os arquivos novos; fica para o fim da Fase 2.
- Nada chama o detector fora dos testes; o seletor (2d) será o primeiro a usá-lo.
- Não há espelho de estudo em TypeScript para este módulo (só é feito a pedido).

---

## Verificação da versão dos passos 2a e 2b, com testes de uso

Data: 16/11/2026. Pedido do owner antes de seguir para o passo 2c.

### O que foi executado

| # | Verificação | Resultado |
|---|---|---|
| 1 | suíte de host no commit do passo 2b (C++17, C++20, ASan + UBSan) | 155 casos, 0 falhas nas três |
| 2 | `./gradlew assembleRelease` (R8, `arm64-v8a` + `x86_64`, NDK 27.1) | exit 0; `UncertaintyModel.cpp` e `JumpDetector.cpp` compilados para as duas ABIs |
| 3 | app de exemplo no emulador `Medium_Phone` (arm64, API 37) | abre, mostra `abiVersion: 1`, `nativeloader` carregou `liblocation-confidence.so`, nenhum erro de JNI no log |
| 4 | testes de uso novos (`cpp/tests/TestUsage.cpp`) | 6 cenários, todos passaram com os números calculados à mão antes de rodar |
| 5 | espelho TypeScript do detector de saltos (`cpp/src/JumpDetector.study.ts`) contra o C++ | 20.000 históricos e 20.000 pares gerados, 0 diferenças |

Sobre o item 2: o `.so` gerado não mudou. Nada na ponte JNI chama as funções novas, então o linker as deixa de fora (conferido com `llvm-nm`: nenhum símbolo do código novo no `.so`). O que a compilação prova é que os arquivos novos compilam com o NDK; eles só entram no binário quando a fachada C os usar (Fase 3).

### Testes de uso

`TestUsage.cpp` liga as peças na ordem em que o seletor (2d) vai ligar: `checkReading` → `history.store` → `checkAllReadingsForGpsJumps` → `calculateTimeInEachPhoneActivity` → `calculateDistancePhoneCouldHaveMoved` → `calculateHowFarPhoneCouldBeFromReading` → `distanceBetweenMeters` → `classifyRelation` → `calculateQualityScore`. Enquanto o seletor não existe, a ligação é feita à mão dentro do teste (`judgeReading`).

Endereço na origem, raio de 100 m, erro do GPS de 5 m. Depois do passo 2c são 7 cenários:

| Cenário | Pode ter andado | Relação | Score |
|---|---:|---|---:|
| USE-1 na porta, celular avisou "parado", leitura de 1 s | 0,5 m | dentro | 97 |
| USE-2 na porta, sem aviso de atividade, leitura de 1 s | 45 m | dentro | 89 |
| USE-2 a mesma leitura com 3 s | 135 m | inconclusivo | 35 |
| USE-3 GPS pulou 450 m e voltou: a leitura que pulou | 1,5 m | inconclusivo | 20 |
| USE-3 a leitura boa mais nova | 0,5 m | dentro | 75 |
| USE-4 andando a 174 m do endereço | 3 m | fora | 97 |
| USE-5 uma leitura com velocidade do GPS, 3 s | 15,15 m | dentro | 75 |
| USE-6 uma leitura de 20 s, nada mais | 900 m | inconclusivo | 15 |
| USE-7 "parado" só nos últimos 2 s de 10 s | 361 m | inconclusivo | 15 |
| USE-7 "parado" nos 10 s inteiros | 5 m | dentro | 93 |

### Defeito encontrado no passo 2a (R-27)

Um teste novo, "perder a atividade do celular nunca diminui a distância nem aumenta o score", falhou contra o código do passo 2a:

```
REQUIRE( withoutActivity.meters >= withActivity.meters )
values: REQUIRE( 213.57 >= 484.47 )      atividade: em veículo, 10,766 s, GPS com velocidade
```

Saber que o celular estava em um veículo dava 484 m; não saber nada dava 213 m. Registro e análise em `SPEC_REVIEW.md`, R-27.

---

## Correção no passo 2a — a menor distância vence

Data: 16/11/2026.

### O que mudou

- **Regra:** antes, as três opções eram tentadas em ordem e só uma rodava. Agora toda opção disponível é calculada e vence a menor distância. Cada opção é um limite superior, então o menor limite é o que vale, e saber mais nunca piora a resposta.
- **Entrada:** a função principal recebia uma única atividade para o tempo todo. Agora recebe a tabela `TimeInEachPhoneActivity` (quanto tempo em cada atividade), que o passo 2c preenche. A opção 1 soma os pedaços: `velocidade da atividade × segundos` para cada linha. Tempo sem informação conta na velocidade máxima.
- **Limite por atividade:** `speedForPhoneActivity` nunca devolve mais que a velocidade máxima configurada.

| Antes | Agora |
|---|---|
| `calculateDistancePhoneCouldHaveMoved(reading, millisecondsSinceReading, phoneActivity)` | `calculateDistancePhoneCouldHaveMoved(reading, timeInEachActivity)` |
| — | `TimeInEachPhoneActivity` (tabela, com `add`, `totalMilliseconds`, `weKnowNothing`, `standingStillTheWholeTime`) |
| — | `wholeTimeInOneActivity(phoneActivity, milliseconds)`: a tabela do caso simples |
| — | `calculateDistanceUsingPhoneActivity` (helper privado da opção 1) |

### O que não mudou

Todos os resultados numéricos dos testes do passo 2a continuam iguais (andando 5 s = 15 m, GPS quase parado 3 s = 15,15 m, etc.), inclusive o rótulo `howItWasCalculated` em cada um. O resultado só é diferente quando a velocidade do GPS dá distância menor que a atividade conhecida.

---

## Passo 2c — linha do tempo da atividade do celular

Data: 16/11/2026.

### Objetivo

Responder, para uma leitura: desde que ela foi tirada, quanto tempo o celular passou em cada atividade? A resposta é a tabela `TimeInEachPhoneActivity`, que o modelo de movimento (2a) transforma em metros.

### Decisões adotadas

- A entrada é uma lista de relatos (`PhoneActivityReport`): início, fim, atividade e confiança.
- Três passos: achar os momentos em que algo muda (início e fim de cada relato, mais a hora da leitura e o agora); cada fatia entre dois momentos vizinhos recebe uma atividade; as fatias são somadas na tabela.
- Fatia sem relato é desconhecida. Fatia com dois relatos de atividades diferentes é desconhecida e liga a flag `twoReportsDisagreed`.
- Só relatos com confiança `High` são usados.
- O arquivo se chama `PhoneActivityTimeline`, e não `MotionEvidenceAnalyzer` como na SPEC, pelo critério de nomes simples.

As hipóteses onde a SPEC é omissa estão em `SPEC_REVIEW.md`, R-28 (lacuna tolerada zero, plataforma estende o relato atual até o agora, cauda de latência em aberto, confiança mínima, capacidade de 32 relatos).

### Arquivos

Criados: `cpp/src/PhoneActivityTimeline.hpp`, `cpp/src/PhoneActivityTimeline.cpp`, `cpp/src/PhoneActivityTimeline.study.ts`, `cpp/src/JumpDetector.study.ts`, `cpp/tests/TestPhoneActivityTimeline.cpp`, `cpp/tests/TestUsage.cpp`.
Modificados: `cpp/src/UncertaintyModel.hpp`, `cpp/src/UncertaintyModel.cpp`, `cpp/src/UncertaintyModel.study.ts`, `cpp/tests/TestUncertainty.cpp`, `cpp/tests/TestJumps.cpp` e `cpp/tests/TestSupport.hpp` (helpers compartilhados), `cpp/CMakeLists.txt`, `docs/SPEC_REVIEW.md`.

### Comandos executados e resultado

| # | Comando (resumo) | Exit | Resultado |
|---|---|---:|---|
| 1 | host C++17, `-Werror`, `lce_tests` | 0 | 194 casos, 0 falhas |
| 2 | host C++20 | 0 | 194 casos, 0 falhas |
| 3 | host C++17 com ASan + UBSan | 0 | 194 casos, 0 falhas, nenhum aviso dos sanitizers |
| 4 | 31 mutações isoladas (15 em `PhoneActivityTimeline`, 16 em `UncertaintyModel`) | — | 31 de 31 detectadas |
| 5 | espelhos TypeScript contra o C++: 20.000 casos do modelo e 20.000 da linha do tempo | 0 | 0 diferenças |
| 6 | `eslint` e `tsc` nos três arquivos `.study.ts` | 0 | sem erros nem avisos |
| 7 | `./gradlew assembleRelease` com os arquivos finais | 0 | `PhoneActivityTimeline.cpp`, `UncertaintyModel.cpp` e `JumpDetector.cpp` compilados para `arm64-v8a` e `x86_64` |

A suíte passou de 155 para 194 casos: 7 de uso, 8 a mais em `TestUncertainty` (26 no total) e 24 em `TestPhoneActivityTimeline`. Cada mutação rodou em cópia nova de `cpp/` com pasta de build nova.

Uma mutação possível não entrou na lista por ser equivalente: aceitar relato que termina antes de começar. Um relato assim nunca cobre uma fatia, então o resultado é o mesmo com ou sem a checagem; ela fica no código como regra explícita.

Um teste de propriedade ("perder um relato não diminui a distância") aceita 1 nanômetro de folga: "em veículo" e "desconhecido" contam os dois a 45 m/s, e a mesma soma feita em outro agrupamento difere na 13ª casa decimal.

### IDs normativos

Implementados e aprovados em host:

- MOT-001, 002, 003, 004, 013, 014, 015, 020, 022, 023 na linha do tempo.
- MOT-005, 006, 007, 008, 009 agora também pelo caminho completo (relatos → tabela → metros).
- MOT-031 (novo, de R-02): um único relato que cobre o tempo todo basta.
- MOT-029 e INV-08 como propriedades com relatos gerados.

MOT-002 deixa de ser parcial: a exigência de cobertura contínua está feita.

Pendentes:

| ID | Motivo |
|---|---|
| MOT-017 | idade acima de `maxFallbackAgeMs` torna a leitura inelegível: é do seletor (2d) |
| MOT-010, 011, 012, 021, 024, 025 | permissão, hardware, atraso e ciclo de vida: plataforma e política (Fases 3 a 5) |
| MOT-018, 019 | documentação (movimento é do aparelho, não da pessoa); o aviso já está no cabeçalho de `PhoneActivityTimeline.hpp` |
| JMP-018, JMP-020, SCR-010 e a parte de seleção de JMP-001/013/014/019/021 | seletor (2d) |

### Não feito

- iOS não foi recompilado nesta rodada (o owner pediu Android primeiro).
- O `.so` do Android não contém o código novo: nada na fachada C o chama ainda.
- Nada chama as três peças fora dos testes; o seletor (2d) será o primeiro.
- Os campos do DTO `motion` (`state`, `confidence`, `coverageStartAgeMs`) ainda não são derivados; `gapsDetected` e `continuousStationaritySupported` já saem da tabela (`millisecondsUnknown > 0` e `standingStillTheWholeTime()`).
- A cauda de latência de detecção (R-02) continua em aberto até haver medição em dispositivo.

---

## Passo 2d — escolha da leitura e relatório final

Data: 16/11/2026.

### Objetivo

Duas perguntas, uma em cada arquivo:

1. `ReadingSelection`: de todas as leituras guardadas, qual usar? (`chooseBestReading`)
2. `Report`: com essa leitura, qual é o relatório? Dentro ou fora do raio, score, status, procedência e razões. (`buildReport`)

`buildReport` é o `evaluateSnapshot` da SPEC: trabalha só com o que está na memória em um instante e não espera nada. Esperar leituras novas é a Fase 3.

### Ordem das chamadas

```text
buildReport(history, activityHistory, nowMs, asked, request)            Report.cpp
  |-- sem permissão de localização?  -> relatório "unavailable" e fim
  |-- chooseBestReading()                                               ReadingSelection.cpp
  |     |-- checkAllReadingsForGpsJumps()                               JumpDetector.cpp   (2b)
  |     `-- para cada leitura que não é velha demais
  |           |-- calculateTimeInEachPhoneActivity()                    PhoneActivityTimeline.cpp (2c)
  |           |-- gpsSpeedMatchesTheMovement()
  |           |-- calculateDistancePhoneCouldHaveMoved()                UncertaintyModel.cpp (2a)
  |           `-- firstIsBetterThanSecond()
  |-- findWhereTheReadingCameFrom()        fresh, recovered ou cached
  |-- classifyRelation()                                                ProximityRelation.cpp (fase 1)
  |-- calculateQualityScore()                                           QualityScore.cpp      (fase 1)
  |-- decideTheStatus()                    available ou degraded
  `-- describePhoneActivity() e writeDownTheReasons()
```

### Decisões adotadas

- As leituras são comparadas por um número só: **quão longe o celular pode estar da leitura agora** (erro do GPS + distância que ele pode ter andado). Nem a mais nova nem a mais precisa ganha por padrão (HIS-018).
- Cinco regras, em ordem: provável salto de GPS perde; menor "quão longe" ganha; depois a mais nova; depois o menor erro de GPS; depois a que chegou primeiro.
- A leitura escolhida é sempre uma leitura medida. Nada é misturado nem interpolado (JMP-020).
- Procedência (`source`) pelo **número de chegada**: a avaliação guarda qual era o próximo número quando começou e quando entrou no tempo extra. Os campos `obtainedByRequestId` e `obtainedDuring` da Fase 1 saíram: com avaliações simultâneas na mesma sessão, uma leitura não pertence a uma request só.
- Novo campo `captureTimeWasEstimated` na leitura (hora calculada a partir do relógio de parede, caso do iOS). Vira `ageReliability = estimated` no relatório.
- Os arquivos se chamam `ReadingSelection` e `Report`, pelo critério de nomes simples.

As hipóteses onde a SPEC é omissa estão em `SPEC_REVIEW.md`, R-29.

### Arquivos

Criados: `cpp/src/ReadingSelection.hpp`, `cpp/src/ReadingSelection.cpp`, `cpp/src/Report.hpp`, `cpp/src/Report.cpp`, `cpp/src/ReadingSelection.study.ts`, `cpp/src/Report.study.ts`, `cpp/src/NodeRuntime.study.ts`, `cpp/tests/TestReadingSelection.cpp`, `cpp/tests/TestReport.cpp`.

Modificados: `cpp/src/Types.hpp` e `cpp/src/ReadingChecker.hpp/.cpp` (campos da leitura), `cpp/src/LocationHistory.hpp` (`arrivalNumberOfNextReading`), `cpp/src/UncertaintyModel.hpp/.cpp` (as duas constantes da opção 2 passaram para o cabeçalho), `cpp/src/PhoneActivityTimeline.hpp/.cpp` (duas perguntas novas para o relatório: `findSinceWhenPhoneActivityIsKnown` e `findPhoneActivityAtMoment`), os três espelhos de estudo existentes, `cpp/tests/TestSupport.hpp`, `cpp/tests/TestUsage.cpp` (os cenários de uso agora passam pelo relatório de verdade, sem ligação manual), `cpp/CMakeLists.txt`, `tsconfig.json`.

### Espelhos de estudo: agora um importa o outro

Com as peças empilhadas, copiar o código emprestado em cada espelho deixaria os arquivos novos com centenas de linhas repetidas. Os espelhos passaram a se importar (`import ... from './JumpDetector.study.ts'`), do jeito que um `.cpp` inclui um `.hpp`. Os exemplos do fim de cada arquivo ficam dentro de `if (import.meta.main)` e só rodam quando aquele arquivo é o que foi executado. `NodeRuntime.study.ts` só ensina o TypeScript a aceitar `import.meta.main`.

```sh
node --no-warnings --experimental-transform-types cpp/src/ReadingSelection.study.ts
node --no-warnings --experimental-transform-types cpp/src/Report.study.ts
```

### Comandos executados e resultado

| # | Comando (resumo) | Exit | Resultado |
|---|---|---:|---|
| 1 | host C++17, `-Werror`, `lce_tests` | 0 | 252 casos, 0 falhas |
| 2 | host C++20 | 0 | 252 casos, 0 falhas |
| 3 | host C++17 com ASan + UBSan | 0 | 252 casos, 0 falhas, nenhum aviso dos sanitizers |
| 4 | 86 mutações isoladas (23 em `ReadingSelection`, 54 em `Report`, 9 nas funções novas de `PhoneActivityTimeline`) | — | 86 de 86 detectadas |
| 5 | espelho `Report.study.ts` (que usa os outros quatro) contra o C++: 20.000 casos gerados | 0 | 0 diferenças |
| 6 | os cinco espelhos rodando sozinhos; `eslint` e `tsc` | 0 | sem erros |
| 7 | `bob build` (pacote JS) depois da mudança no `tsconfig.json` | 0 | ok |

Sobre as mutações: na primeira rodada, 71 foram detectadas, 4 não foram e 11 não compilaram (deixavam variável sem uso, e o build usa `-Werror`). As 4 não detectadas viraram testes novos (o erro da velocidade reportada, a velocidade máxima e a idade máxima escolhidas pelo app, e um relato de atividade que ainda não começou). As 11 foram reescritas para compilar. Na segunda rodada, 15 de 15.

Sobre a comparação com o TypeScript: a coluna da distância até o endereço aceita diferença relativa de 1e-9. Ela passa por `sin`, `cos` e `atan2`, e a biblioteca matemática do C++ e a do JavaScript diferem no último dígito (R-07). Todas as outras colunas (status, procedência, score, tetos, razões, atividade) são idênticas.

A suíte passou de 194 para 252 casos: 23 em `TestReadingSelection` e 35 em `TestReport`. Os 7 cenários de uso continuam com os mesmos números; o USE-6 ganhou a checagem de procedência (`cached`, porque a leitura tem 20 s).

### IDs normativos

Implementados e aprovados em host:

- HIS-018, 019, 020, 022, 023, 024 pelo caminho completo.
- JMP-001, 014, 018, 019, 020, 021 (parte de seleção).
- MOT-014, 017, 022 no relatório.
- SCR-001, 009, 010, 011, 016, 020, 024, 025, 026, 027, 030.
- VAL-018, 019, 023, 024, 025.
- Fixtures 12.1 a 12.6 da SPEC, com score exato (100; 75 com bruto 89; 15 com bruto 72).
- INV-08 como propriedade, por leitura (ver limitação abaixo).

Com isso a Fase 2 fecha os grupos JMP e MOT de core e a parte de SCR que dependia de seleção. JMP-013 (multipath) é coberto pela regra de consistência: havendo contradição, o score fica limitado a 75 e a relação continua valendo só pela geometria.

### Limitação conhecida

Por leitura, o score nunca sobe e a incerteza nunca diminui com o passar do tempo (testado como propriedade). Entre leituras isso não é garantido: a escolha minimiza a incerteza, não maximiza o score, então no instante em que a leitura escolhida muda o score do relatório pode subir. O mesmo acontece quando o histórico esquece, depois de 2 minutos, uma leitura que contradizia a escolhida. Registrado em R-29.

### Não feito

- iOS e Android não foram recompilados neste passo; isso volta nas Fases 4 e 5, quando a fachada C passa a chamar este código.
- Os contadores de diagnóstico da avaliação (`readingsReceived`, `durationMs`, passos) existem no relatório, mas quem os preenche é a Fase 3.

