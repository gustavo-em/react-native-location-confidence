# Fase 2 — modelos: relatório (em andamento)

Branch `phase/2-models`. A pedido do owner, a fase é feita em quatro passos, um por peça, com parada para leitura entre eles.

| Passo | Peça | Situação |
|---|---|---|
| 2a | Modelo de incerteza (`UncertaintyModel`) | feito |
| 2b | Detector de saltos (`JumpDetector`; na SPEC, `OutlierDetector`) | feito |
| 2c | Evidência de movimento (`MotionEvidenceAnalyzer`) | pendente |
| 2d | Seletor de candidato (`CandidateSelector`) | pendente |

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
