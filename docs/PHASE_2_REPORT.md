# Fase 2 — modelos: relatório (em andamento)

Branch `phase/2-models`. A pedido do owner, a fase é feita em quatro passos, um por peça, com parada para leitura entre eles.

| Passo | Peça | Situação |
|---|---|---|
| 2a | Modelo de incerteza (`UncertaintyModel`) | feito |
| 2b | Detector de saltos (`OutlierDetector`) | pendente |
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
