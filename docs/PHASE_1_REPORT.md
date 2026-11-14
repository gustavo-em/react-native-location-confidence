# Fase 1 — core base em C++: relatório

Data: 14/11/2026. Autorizada pelo owner na mesma data. Branch `phase/1-core-base`.

## Objetivo

Implementar a base do core compartilhado, sem plataforma: tipos, validação de amostras, distância Haversine, histórico circular com dedup, score `heuristic-v1` e classificação da relação com o destino. Gate: testes GEO, VAL, HIS e SCR de core passando em host, sem include de plataforma.

## Decisões adotadas

- Headers C++ internos ficam em `cpp/src/` (`.hpp`); `cpp/include/` continua contendo só a fachada C. Assim o pod expõe ao Swift apenas `lce.h`.
- Modelo de tempo aprovado na Fase 0 (R-14): um campo `fixTimeMonotonicMs`; `sequence` atribuído pelo histórico e usado só para desempate; dedup por conteúdo.
- Remoção do buffer protege o melhor candidato (D-05). O histórico recebe o `sequence` protegido de quem o chama; o cálculo do "melhor" chega com o seletor na Fase 2.
- Classificação de relação sem tolerância numérica (R-18) e só com gates de integridade (D-06).
- `mockLocationPolicy` com `ZeroScore` e `FlagOnly` (D-07).
- Framework de teste: doctest 2.4.12, header único vendorizado em `cpp/tests/third_party/` com SHA-256 registrado. Não entra no pacote npm.
- Gerador de propriedades próprio sobre `std::mt19937_64`, para a mesma sequência em qualquer biblioteca padrão.
- Android: só `JNI_OnLoad` é exportado do `.so` (pendência do spike resolvida).

Achados e hipóteses que mudam ou completam a SPEC estão em `SPEC_REVIEW.md`, Parte 4 (R-24, R-25, R-26). O mais importante: **R-24**, cap novo `conflicting_evidence` = 75, sem o qual uma leitura conflitante elevava o score de 75 para 90.

## Arquivos

Criados em `cpp/src/`: `Types.hpp`, `ReasonCodes.hpp/.cpp`, `GeoMath.hpp/.cpp`, `SampleValidator.hpp/.cpp`, `LocationHistory.hpp/.cpp`, `ProximityRelation.hpp/.cpp`, `QualityScore.hpp/.cpp` (1059 linhas com `CFacade.cpp`).

Criados em `cpp/tests/`: `TestMain.cpp`, `TestSupport.hpp`, `TestFacade.cpp`, `TestGeoMath.cpp`, `TestRelation.cpp`, `TestValidation.cpp`, `TestHistory.cpp`, `TestScore.cpp`, `TestProperties.cpp`, `TestReasonCodes.cpp`, `third_party/doctest.h`, `third_party/README.md`.

Modificados: `cpp/CMakeLists.txt` (fontes, opções `LCE_SANITIZE` e `LCE_WERROR`), `android/CMakeLists.txt` (visibilidade oculta), `docs/SPEC_REVIEW.md`.

Removido: `cpp/tests/AbiProbe.cpp` (substituído por `TestFacade.cpp`).

## Comandos executados e resultado

| # | Comando (resumo) | Exit | Resultado |
|---|---|---:|---|
| 1 | host, `-std=c++17`, `-Wall -Wextra -Wpedantic -Werror`, `lce_tests` | 0 | 110 casos, 65.301 asserções, 0 falhas |
| 2 | host, `-std=c++20`, mesmas flags | 0 | 110 / 0 falhas |
| 3 | host, C++17 com AddressSanitizer + UndefinedBehaviorSanitizer | 0 | 110 / 0 falhas, nenhum relato |
| 4 | `./gradlew assembleRelease` (R8, `arm64-v8a` + `x86_64`, NDK 27.1) | 0 | core compilado; `.so` exporta só `JNI_OnLoad` |
| 5 | `pod install` + `xcodebuild` Release, simulador arm64, pods estáticos | 0 | core compilado como C++20; BUILD SUCCEEDED |
| 6 | busca por includes de plataforma em `cpp/src` e `cpp/include` | — | 0 ocorrências |

Verificação da força dos testes: apliquei 8 mutações isoladas ao core em cópia temporária (trocar `<=` por `<` na relação, remover o cap de conflito, ignorar a proteção do melhor fix, remover o wrap do antimeridiano, remover o viés de arredondamento, desligar o dedup, desligar a tolerância de timestamp futuro, aplicar o último cap em vez do menor). Na primeira rodada 7 foram detectadas; a de arredondamento passou despercebida. Acrescentei casos a SCR-031 e reforcei o gerador de propriedades; as 8 passaram a ser detectadas.

## IDs normativos

**Implementados e aprovados em host (91 da SPEC + 1 novo):**

- GEO: 001–026 (todos).
- VAL: 001–009, 011, 013, 014, 018–024, 026.
- HIS: 001–010, 012, 014, 017, 021–025; novo HIS-027.
- SCR: 001–004, 006–008, 010–019, 021–027, 029, 031, 032.
- Fixtures §12.1, §12.2, §12.3 e §12.4 com valores exatos.

**Cobertura parcial (parte de core feita; o restante depende de outra fase):**

| ID | Feito agora | Falta |
|---|---|---|
| GEO-012..019 | rejeição defensiva no core | "nenhum sensor iniciado / sem JNI" na fachada TS (Fase 6) |
| VAL-009 | idade cresce com o relógio monotônico | normalização no adapter Android (Fase 4) |
| VAL-013 | ambos os fixes retidos em ordem de chegada | marcação de conflito (Fase 2) |
| HIS-008, HIS-010, HIS-014 | no nível de instância | registry de sessões (Fase 3) |
| HIS-021..023 | mapeamento contagem → consistência | critério de "coerente" (Fase 2) |
| SCR-008, SCR-010 | no nível da função de score | com modelo de incerteza e detector de outliers (Fase 2) |

**Adiados (nenhum falho, nenhum bloqueado):**

- Fase 2: HIS-018, 019, 020; SCR-009, 020, 028; VAL-025.
- Fase 3: HIS-011, 013, 015, 016.
- Fases 4–5: VAL-010, 012, 015, 016, 017.
- Fase 6 / documentação: SCR-005, 030.
- Manual: HIS-026.

## Falhas conhecidas e riscos

- Os testes rodaram só em host (macOS arm64, Apple clang 17). No Android e no iOS o core foi compilado, não executado; a execução das fixtures nas plataformas vem com a fachada C (Fase 3) e os adapters (Fases 4–5).
- Fixtures em JSON ainda não existem: os casos estão escritos em C++. O plano previa JSON como fonte única; proponho introduzi-lo na Fase 3, quando houver a fachada por onde as três plataformas as executam.
- Execução no Android: verificada depois em emulador arm64 com o APK release desta branch (ver `PHASE_0_5_REPORT.md`). Só a função de versão de ABI atravessa a ponte; os módulos da Fase 1 ainda não são chamados pelas plataformas.
- Limiares do score, tolerância de timestamp e demais números continuam sendo hipóteses não calibradas.
- R-24 altera a tabela de caps da SPEC e aguarda sua ciência.

## Próximo gate

Fase 2 (detector de outliers, evidência de motion, modelo de incerteza com envelope de velocidade, seletor de candidato, propriedades). Requer aprovação explícita.
