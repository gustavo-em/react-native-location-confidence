# SPEC_REVIEW — revisão crítica da SPEC v2.0

Data: 14/11/2026. Cada item traz: local na SPEC, risco, alternativas, recomendação, impacto nos testes. Nada foi alterado na SPEC. Itens marcados **[DECISÃO]** mudam contrato público ou modelo e precisam de aprovação.

Os números de latência e comportamento de APIs de SO citados aqui vêm de conhecimento das APIs, não de medição neste projeto. Devem ser confirmados em dispositivo.

Severidade: **A** = compromete a utilidade ou a entrega; **M** = ambiguidade ou risco que gera retrabalho; **B** = ajuste.

---

## Parte 1 — Modelo e algoritmo

### R-01 [A] [DECISÃO D-01] O bound `unknown = 45 m/s` torna o resultado quase sempre inconclusivo sem motion

**Local:** §6.4 (linhas 784–797), §6.7 caps, §4.3 `maxFreshAgeMs`, MOT-009, REC-001.

**Risco:** motion é opcional e, na prática, ausente na maioria dos apps. Sem ele, `movementAllowance = 45 × idade`:

| Fix (accuracy 5 m) | `effectiveUncertainty` | Consequência |
|---|---:|---|
| idade 1 s | 50 m | `supports_inside` em raio de 100 m só se d <= 50 m |
| idade 2 s | 95 m | só se d <= 5 m |
| idade 3 s | 140 m | cap 35; `supports_inside` impossível em r = 100 m |
| idade 10 s (ainda "fresh" por `maxFreshAgeMs`) | 455 m | cap 15 |

Um fix "fresh" segundo a própria SPEC recebe score 15. `maxFreshAgeMs = 10000` e o modelo de incerteza se contradizem, e o caso de uso central (raio de 100 m, sem permissão de motion) devolve `inconclusive` quase sempre.

**Alternativas:**
1. Manter e documentar (biblioteca correta, porém inútil sem motion).
2. Baixar o bound default (arbitrário, menos conservador sem evidência).
3. **Usar a velocidade reportada pelo próprio fix como evidência**, com envelope de aceleração: `allowance(t) = min(vmax·t, (v + k·σv)·t + a·t²/2)`, onde `v` = speed do fix, `σv` = speed accuracy, `a` = aceleração máxima de projeto (hipótese 2,5 m/s²), `k` = 2. Sem speed ou sem speed accuracy válidos, cai no bound de 45 m/s.

**Recomendação:** alternativa 3. Não cria coordenada (ADR-006 preservado), não usa speed como prova de imobilidade contínua (§7.1 preservado, pois o envelope cresce com t²), e é monotônico na idade. Exemplo: v = 0,3, σv = 0,5, idade 3 s → 15 m em vez de 135 m; idade 10 s → 138 m em vez de 450 m. Exige o campo `speedAccuracyMps` em `LocationSample` (Android API 26+, iOS `speedAccuracy`). Os parâmetros são hipóteses versionadas em `scoreModelVersion`.

**Impacto nos testes:** MOT-009 permanece (sem speed → 225 m). Novos casos MOT-026..030: speed válida, speed sem accuracy, envelope saturando em vmax, monotonicidade em t, perda de speed não aumenta score. Fixtures 12.2 e 12.3 inalteradas.

### R-02 [A] Modelo de cobertura de motion incompatível com APIs que emitem por mudança

**Local:** §6.4 `maxMotionGapMs = 2000`, MOT-002, MOT-003, MOT-023, PLT-014.

**Risco:** `CMMotionActivityManager` e Activity Recognition entregam eventos quando o estado muda, não em cadência fixa. Um aparelho parado por 30 s gera um evento e depois silêncio. Com limiar de lacuna de 2 s entre eventos, "cobertura contínua" nunca é verdadeira e o bound estacionário nunca se aplica. Além disso, a classificação atrasa em relação à realidade; o trecho mais recente pode já ser movimento não detectado.

**Recomendação:** redefinir cobertura como "assinatura ativa e ininterrupta desde antes do fix até agora, com o último estado reportado persistindo", e tratar a cauda de latência de detecção `L` (hipótese por plataforma) com o envelope de aceleração de R-01 a partir do repouso. Interrupções de assinatura (background, revogação, reinício) continuam virando `unknown`.

**Impacto nos testes:** MOT-003 e MOT-023 passam a significar "lacuna de assinatura", não "lacuna entre eventos". Novo caso: estado estável sem eventos por 30 s com assinatura contínua → cobertura verdadeira. Decidir os números na Fase 2 com fixtures.

### R-03 [A] [DECISÃO D-02] Activity Recognition no Android tem custo alto e valor baixo para um deadline de 12 s

**Local:** §7.2, §5.2 ("consulta rápida de motion até 500 ms"), PLT-013, PLT-014.

**Risco:** a API entrega via `PendingIntent`/`BroadcastReceiver`, não por callback; o intervalo é consultivo e atrasos de dezenas de segundos são comuns; exige permissão de runtime adicional (API 29+) e receiver no manifest da biblioteca. Uma "consulta de 500 ms" não existe nessa API. É o adapter mais complexo do projeto e só produz evidência em sessões longas de monitoring.

**Alternativas:** (a) implementar como na SPEC; (b) **v1.0 com motion só no iOS e Android reportando `motion_unavailable`**, adapter Android em v1.1 após medir latência real; (c) acelerômetro bruto no Android (sem permissão, sem latência), mas imobilidade do aparelho não exclui translação em veículo suave, então não sustenta o bound estacionário.

**Recomendação:** (b). Está dentro da letra da SPEC (§0.3 item 3: motion opcional e degradável) e R-01 já cobre o problema de envelhecimento nas duas plataformas sem permissão extra. Os testes de porta (`MotionSource` fake) continuam valendo no Android.

**Impacto nos testes:** PLT-013 e PLT-014 ficam no nível de coordinator com fake; a verificação com adapter real é marcada "adiada v1.1" na matriz, com exceção documentada.

### R-04 [M] [DECISÃO D-03] Duas primitivas de aquisição (one-shot e depois stream) em vez de um stream único

**Local:** §5.1–5.3, §7.1, §8.1, ADR-010.

**Risco:** no iOS, `requestLocation()` espera convergência e frequentemente leva mais que os 4,5 s do orçamento inicial, ou falha com `kCLErrorLocationUnknown`. No Android, cancelar `getCurrentLocation` e iniciar `requestLocationUpdates` cria um intervalo sem listener e dois ciclos de vida para vazar. Com `foreground_monitoring` já existe um stream, e a avaliação teria um terceiro caminho.

**Recomendação:** uma primitiva por plataforma: `startStream(config) → handle` / `stop(handle)`. "Fix inicial" e "recovery" viram fases da política sobre o mesmo stream; o reducer decide quando parar. Comandos passam a ser `START_ACQUISITION`, `STOP_ACQUISITION`, `QUERY_MOTION`, `FINISH`. Energia: stream encerrado no primeiro fix adequado equivale a one-shot. O compartilhamento de ADR-008 fica trivial (o stream é o recurso com contagem de assinantes).

**Impacto nos testes:** REC-020 ("getCurrentLocation retorna null") e PLT-007 mantêm o ID com Given adaptado ("stream não entrega fix no orçamento inicial" / "primeiro fix do stream é cache antigo"). Comparar latência das duas abordagens em dispositivo no spike antes de fechar.

### R-07 [M] Equivalência "byte a byte" entre plataformas não é garantível com `double`

**Local:** NFR-03, REC-029, §12.7.

**Risco:** clang pode fundir multiplicação e soma em FMA no arm64 e não no x86_64; `sin`/`cos`/`atan2` vêm de libm diferentes (bionic, Apple, glibc) e podem diferir no último bit.

**Recomendação:** compilar o core com `-ffp-contract=off` e sem `-ffast-math`; definir a equivalência como: campos inteiros e enums idênticos, `double` com tolerância relativa 1e-9. Os fatores do score não usam trigonometria, então o inteiro final é determinístico fora de limiares exatos. REC-029 passa a exigir identidade byte a byte apenas no mesmo binário.

### R-10 [M] [DECISÃO D-05] Remoção por idade deixa amostras ruins expulsarem o melhor fix

**Local:** §5.5, HIS-004, HIS-009.

**Risco:** fixes imprecisos são retidos "para diagnóstico". Dez leituras ruins em 10 s de recovery expulsam o único fix bom de 15 s atrás.

**Recomendação:** ao encher, remover o mais antigo **que não seja o melhor candidato atual** (menor incerteza efetiva entre os elegíveis). Uma regra, custo O(10).

**Impacto nos testes:** HIS-004 continua válido quando o mais antigo não é o melhor; novo HIS-027 cobre a proteção.

### R-11 [M] [DECISÃO D-06] Semântica indefinida de `source`, `status` e `requiredQualityGatesMet`

**Local:** §4.2, §5.3, §6.6; fixture 12.3 rotula `cached` um fix com idade 10000 ms, igual a `maxFreshAgeMs`.

**Proposta normativa:**

| Campo | Regra |
|---|---|
| `source = fresh` | fix adquirido por esta request, antes do início da recovery, com idade <= `maxFreshAgeMs` |
| `source = recovered` | adquirido por esta request durante a recovery, com idade <= `maxFreshAgeMs` |
| `source = cached` | qualquer outro fix selecionado: histórico de requests anteriores ou de monitoring, ou fix mais velho que `maxFreshAgeMs` (inclui cache do SO) |
| `status = available` | candidato `fresh`/`recovered`, idade confiável, accuracy <= `maxHorizontalAccuracyMeters`, sem suspeita forte de salto, política de mock satisfeita |
| `status = degraded` | existe candidato, mas falha algum gate acima ou é `cached` |
| `status = unavailable` | nenhum candidato |
| gates de `evidenceRelation` | apenas integridade: idade confiável, idade <= `maxFallbackAgeMs`, permissão autorizada, sem suspeita forte de salto, política de mock. Limiares de qualidade **não** entram, porque `u` já carrega accuracy e idade; aplicá-los de novo gera `inconclusive` falso em raios grandes (SCR-027) |

Exige marcar cada amostra com a request e a fase que a adquiriu.

**Impacto nos testes:** fixture 12.3 fica coerente (histórico → `cached` por procedência). SCR-025 preservado (idade > `maxFallbackAgeMs` → inconclusivo).

### R-18 [B] Epsilon na borda

**Local:** §6.6, GEO-020..022. Igualdade exata `d+u == r` não é construível via Haversine. **Recomendação:** nenhuma tolerância em produção (epsilon tornaria a borda menos conservadora); testar a função pura `classifyRelation(d, u, r)` com `double` exatos.

### R-19 [B] Teto prático de 75 no caminho feliz

**Local:** §6.7 (cap `single_fix`), REC-001, REC-009. Avaliação one-shot com parada antecipada nunca passa de 75. É coerente com o desenho; precisa constar no README para que ninguém configure limiar acima de 75 esperando aprovação em uma leitura.

### R-09 [M] `monitoringDistanceFilterMeters = 10` esvazia o histórico quando o entregador está parado

**Local:** §4.2, §4.3. Com filtro de distância, aparelho parado não gera updates; após 120 s o buffer está vazio justamente no cenário em que o fallback histórico mais ajuda. **Recomendação:** default 0 (sem filtro), apenas intervalo. Nota: no iOS não há parâmetro de intervalo; `monitoringIntervalMs` vira limitação de ingestão e não reduz consumo. Documentar.

---

## Parte 2 — Arquitetura e integração

### R-05 [A] [DECISÃO D-04] A ordem das fases adia o maior risco e contém uma dependência circular

**Local:** §13, §0.6 regra 7.

**Risco:** o risco dominante é integração (pod misto Swift/ObjC++/C++, CMake/JNI, R8, Codegen). A SPEC só o encontra nas Fases 4–6, depois de três fases de C++. E o gate da Fase 4 ("app exemplo Android consegue chamada real") depende do TurboModule, que é Fase 6.

**Recomendação:** inserir **Fase 0.5 — spike de integração** antes da Fase 1: scaffold, uma função C trivial (`lce_abi_version()`) alcançada por JS → TurboModule → Kotlin → JNI e JS → TurboModule → ObjC++ → Swift → C, com build release nas duas plataformas. O spec Codegen mínimo nasce aí e cresce por fase. A regra "core primeiro" continua valendo para a lógica.

### R-06 [M] "C++17" contra o padrão C++20 imposto pelo RN nos pods

**Local:** ADR-012, §3.3. Verificado: RN 0.88.1 força `CLANG_CXX_LANGUAGE_STANDARD = c++20` nos pods. No iOS o core será compilado como C++20 junto com os `.mm`. **Recomendação:** core escrito no subconjunto C++17 e compilado em CI nos dois padrões (17 no host e Android, 20 no host e iOS). Emenda a ADR-012.

### R-13b [M] Fachada C como única entrada do core (proposta ADR-013)

**Local:** ADR-003, §3.5, §8.3.

**Risco do desenho atual:** no iOS, `Swift → wrapper ObjC++ (.mm) → C++` exige classes-modelo ObjC duplicando os structs C++, mais uma camada `.mm` sem teste em host. No Android, JNI lendo campos de objetos Kotlin depende de nomes preservados contra R8.

**Recomendação:** `cpp/include/location_confidence/lce.h`, `extern "C"`, handles opacos inteiros, structs POD, códigos de status, todas as funções `noexcept`. Swift importa C diretamente; JNI copia arrays primitivos para os mesmos POD. A serialização é definida uma vez e testada em host (inclusive com sanitizers). Se o spike mostrar que expor o header C ao Swift dentro do pod é problemático, o plano B é exatamente o wrapper ObjC++ da SPEC. Detalhes em `NATIVE_TRANSPORT_SPEC.md`.

### R-15 [M] Executor serial por sessão conflita com aquisição compartilhada entre sessões

**Local:** ADR-004, ADR-008, SES-012. Aquisição de GPS é recurso do processo; confinamento por sessão obriga coordenação entre filas. **Recomendação:** uma fila serial para o módulo inteiro. Cada evento custa microssegundos; não há ganho em paralelizar sessões, e some uma classe inteira de corridas.

### R-16 [M] Reducer: comando `ASSESS_SNAPSHOT` é ida e volta inútil; eventos sem relógio

**Local:** §5.1, §3.6. O core é dono do histórico e avalia em menos de 10 ms; devolver "avalie" para a plataforma chamar de volta só cria um estado em que ela pode esquecer. **Recomendação:** comandos apenas para efeitos de I/O; `FINISH` já carrega o assessment. Todo evento leva `nowMonotonicMs` (o reducer não tem relógio). O retorno é uma lista pequena de efeitos, não um único.

### R-08 [M] "App em background" sem definição gera cancelamentos falsos

**Local:** §7.3, CON-009, CON-010, SES-006. `onHostPause` (Android) e `willResignActive` (iOS) disparam em diálogo de permissão, Central de Controle, Face ID. O próprio `requestLocationPermission()` cancelaria uma avaliação em curso. **Recomendação:** background = `ProcessLifecycleOwner` `ON_STOP` no Android e `didEnterBackground` no iOS. Novo caso de teste: perda de foco sem background não cancela.

### R-14 [M] Modelo de tempo redundante; relógio no iOS; privacy manifest ausente

**Local:** §4.4, §6.1, §8.1, §9.3.

- `ageAtIngestMs` + `monotonicIngestMs` são dois campos que podem se contradizer. **Recomendação:** um campo, `fixTimeMonotonicMs`, mais `ageReliable`. Base monotônica do processo, não "relativa à sessão".
- `sampleSequence` atribuído pelo adapter não identifica duplicata (a mesma leitura entregue duas vezes recebe dois números). Dedup por conteúdo; sequence atribuído pelo core só para desempate.
- iOS: usar relógio monotônico contínuo; detectar salto de relógio de parede comparando delta de parede e delta monotônico entre ingestões (VAL-015).
- **Lacuna da SPEC:** APIs de uptime no iOS são "required reason APIs". O pod precisa de `PrivacyInfo.xcprivacy` declarando a categoria de tempo de boot. Conferir a lista vigente da Apple no spike. Novo caso PUB-027.

### R-17 [B] Camadas TS em excesso; portas nativas faltando

**Local:** §3.2, §3.6, CLAUDE regra 9. Uma classe de use case por método para uma fachada de oito métodos é o que a regra 9 proíbe. **Recomendação:** `types.ts`, `validation.ts`, `errors.ts`, `NativeLocationConfidence.ts` (spec Codegen), `mapper.ts`, `LocationConfidence.ts` (fachada com porta nativa injetável), `index.ts`. `DeliveryDecisionPolicy` vai para o exemplo. No nativo faltam as portas que tornam os testes determinísticos: `Clock`, `Scheduler` (timers) e `LifecycleSource`.

### R-22 [B] ADR-001: o custo do core C++ deve ficar explícito

Um core em TypeScript teria uma implementação só, testes em Jest e nenhum risco de JNI/ObjC++/CMake. O argumento "autonomia com JS bloqueado" é fraco: com JS bloqueado a Promise não resolve de qualquer forma. O que sustenta a opção D é manter histórico de coordenadas fora do heap JS, ingestão de monitoring sem acordar o JS e reuso futuro fora de RN. **Recomendação:** manter D, registrar esse trade-off na ADR e reduzir o risco com R-05 e R-13b.

---

## Parte 3 — Contrato público e catálogo

### R-13 [M] [DECISÃO D-07] Lacunas na API pública

| Lacuna | Proposta |
|---|---|
| `Capabilities` e `Authorization` usados mas não definidos | definidos em `NATIVE_TRANSPORT_SPEC.md` §3 |
| `startSession` "retorna falha estruturada" mas o tipo é `{ id }` | `{ id, monitoring: 'not_requested' \| 'active' \| 'blocked', reasonCodes }`; `evaluations_only` não exige permissão |
| cap de mock "configurável" sem opção | `mockLocationPolicy?: 'zero_score' \| 'flag_only'`; default a decidir |
| `mode: 'foreground'` e `historyCapacity?: 10` com valor único | remover da v1; adicionar depois não quebra |
| quem gera `requestId` | o JS, antes da chamada nativa; do contrário não há como cancelar uma request cuja Promise ainda não voltou |
| `sessionId` inexistente: erro ou assessment? | assessment `unavailable` com `unknown_session` (corrida comum com `stopSession`, não erro de programação) |

### R-12 [M] Reason codes exigidos por testes e ausentes do catálogo (§4.5)

`unknown_session` (§4.3), `location_invalid_accuracy` (VAL-003, PLT-020), `location_metadata_inconsistent` (JMP-018), `provider_conflict` (JMP-019), `motion_conflicting_evidence` (MOT-014), `monitoring_duration_reached` (SES-007), `uncertainty_model_heuristic` (§6.4 pede um código de heurística). Propor inclusão; os nomes são sugestão.

### R-20 [M] Parte do catálogo não é teste automatizável; "crítico" não está definido

Dos 289 casos, 153 são testes puros de core, e 19 são verificação de documento, checklist de release ou protocolo manual (por exemplo MOT-018, PUB-006..013, CON-022). Mapeamento completo em `IMPLEMENTATION_PLAN.md` §4. **Proposta de "crítico" (P0):** todo caso que protege uma INV, mais todos os CON, SCR e REC.

### R-21 [B] Permissão no Android a partir de biblioteca

`requestLocationPermission()` precisa de Activity (`PermissionAwareActivity`). Sem Activity corrente, retornar erro estruturado. Abaixo da API 29 não há permissão de runtime para activity recognition.

### R-23 [B] Itens administrativos

Nome do pacote a decidir (dois candidatos livres no npm hoje); `CLAUDE_GeoReliant.md` → `CLAUDE.md`; contagem 289 conferida (26+26+26+24+25+29+32+28+34+26+13).

---

## Decisões — aprovadas pelo owner em 14/11/2026

| ID | Decisão | Resultado |
|---|---|---|
| D-01 | Envelope de velocidade do fix no modelo de incerteza | aprovada |
| D-02 | Motion no Android na v1.0 | aprovada: adapter real adiado para v1.1 |
| D-03 | Stream único em vez de one-shot + stream | aprovada, sujeita à medição de latência no spike |
| D-04 | Fase 0.5 de spike antes da Fase 1 | aprovada; execução autorizada |
| D-05 | Proteção do melhor candidato na remoção do buffer | aprovada |
| D-06 | Tabela normativa de `source`/`status`/gates | aprovada |
| D-07 | Ajustes de API pública (R-13) | aprovada; `mockLocationPolicy` default = `zero_score` |
| D-08 | Fachada C (ADR-013) | aprovada, plano B = wrapper ObjC++ da SPEC |
| D-09 | Nome do pacote | `react-native-location-confidence` |

A SPEC v2.0 não foi editada. Estas decisões prevalecem sobre o texto dela nos pontos indicados em cada R-xx.
