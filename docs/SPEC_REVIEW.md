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

## Parte 4 — Achados durante a Fase 1 (aguardam ciência do owner)

### R-24 [A] Evidência conflitante sem cap viola a invariante da própria SPEC

**Local:** §6.7 (tabela de caps e `consistencyFactor = 0.30`), INV-08, "adicionando outlier não pode aumentar consistência".

**Risco:** um fix único e bom tem score bruto de até 94 e é limitado a 75 pelo cap `single_fix`. Se chega uma segunda leitura conflitante, o fator cai para 0,30 (bruto até 90), mas o cap `single_fix` deixa de valer e nenhum outro o substitui. Resultado: 75 → 90. Um outlier aumenta o score.

**Alternativas:** (a) reaproveitar o cap `single_fix` para o caso conflitante; (b) cap próprio.

**Implementado:** (b) cap `conflicting_evidence` = 75. É o menor ajuste que restaura a monotonicidade; o valor pode ser mais baixo se você preferir. **Impacto nos testes:** caso explícito em `TestScore` e propriedade SCR-010 em `TestProperties`.

### R-25 [M] `roundHalfUp` ingênuo arredonda errado metades exatas

**Local:** §6.7, §12.7, SCR-031. Em uma grade de 173.481 entradas cujo score bruto é matematicamente `x,5`, `floor(x + 0.5)` em `double` arredondou para baixo em 19.289 (11%), porque a soma ponderada cai em `x,49999999999999`. Exemplo: accuracy 0, idade 6000 ms, allowance 0, fix único → 91,5 exato, avaliado como 91,49999999999999.

**Implementado:** viés de 1e-9 antes do `floor`. Determinístico entre plataformas (os fatores não usam trigonometria). Casos concretos adicionados a SCR-031.

### R-26 [B] Hipóteses fixadas na Fase 1

| Ponto | Escolha | Onde a SPEC é omissa |
|---|---|---|
| Tolerância de timestamp futuro (VAL-008) | 1000 ms; além disso o fix é rejeitado | "tolerância documentada" sem valor |
| Fix com tempo não confiável | rejeitado na ingestão, contado, nunca armazenado | §5.5 e §6.1 permitem as duas leituras |
| Cap `unknown_movement_not_fresh` | só com `source = cached` | "fix não fresco" sem definição; segue a tabela de D-06 |
| Reason codes de R-12 | os sete nomes sugeridos foram adotados | R-12 não era uma decisão D-xx |
| Fix novo mais antigo que todo o buffer cheio | descartado (`DroppedAsOldest`) | não previsto |

---

## Parte 5 — Achados durante a Fase 2 (aguardam ciência do owner)

### R-27 [A] A ordem fixa das opções do modelo de movimento violava INV-08

**Local:** §6.4 (limites por atividade), D-01 (envelope da velocidade do fix, definido para o caso "sem motion"), INV-08 e §6.7 ("ausência de dados ou perda de motion coverage não aumenta score").

**Risco:** o passo 2a usava a atividade do celular sempre que ela era conhecida, e só olhava a velocidade reportada pelo GPS quando a atividade era desconhecida. Quando a atividade é rápida e o GPS reportou velocidade baixa, saber a atividade dava distância **maior** do que não saber nada:

| Situação | Com a atividade | Sem a atividade |
|---|---:|---:|
| correndo há 3 s, GPS 0,3 ± 0,5 m/s | 24 m | 15,15 m |
| em veículo há 10 s, GPS 0,3 ± 0,5 m/s | 450 m | 138 m |
| caso gerado pelo teste (veículo, 10,766 s, GPS com velocidade) | 484,47 m | 213,57 m |

Perder a evidência de atividade diminuía a distância e, portanto, aumentava o score. Com a integração por trechos do passo 2c (parte do tempo conhecida, parte não), o caso deixaria de ser raro.

**Alternativas:** (a) manter e documentar; (b) ignorar a velocidade do GPS sempre que houver alguma atividade, o que não resolve, porque o problema é a atividade conhecida ser pior do que nenhuma; (c) calcular todas as opções disponíveis e ficar com a menor distância.

**Implementado:** (c). Cada opção é um limite superior ("o celular não pode estar mais longe do que isto"); quando dois limites valem, vale o menor. Não cria classe nova de risco: o envelope de D-01 já era aceito para "atividade desconhecida", que inclui estar em veículo. Os resultados só mudam quando a velocidade do GPS dá distância menor que a atividade; em todos os outros casos são idênticos aos de antes. Pela mesma razão, a velocidade de cada atividade passou a ser limitada pela velocidade máxima configurada.

**Impacto nos testes:** o teste que expôs o defeito ficou como propriedade INV-08 em `TestUncertainty` (duas propriedades) e em `TestPhoneActivityTimeline` (perder um relato não diminui a distância); casos concretos em "the smaller distance wins".

### R-28 [M] Cobertura de movimento: hipóteses fixadas no passo 2c

R-02 deixou para a Fase 2 a decisão dos números de cobertura. O que foi fixado:

| Ponto | Escolha | Onde a SPEC é omissa ou foi revista |
|---|---|---|
| Lacuna tolerada entre relatos (`maxMotionGapMs`) | zero: todo milissegundo sem relato conta como desconhecido | a SPEC sugere 2000 ms como hipótese; R-02 mudou o sentido de "lacuna" para interrupção de assinatura |
| Quem mantém o relato do estado atual "aberto" até agora | a plataforma: ao pedir uma avaliação, estende o relato do estado atual até o mesmo `now` que passa ao core | consequência de R-02 |
| Cauda de latência de detecção (R-02) | não tratada no core; fica em aberto até haver medição em dispositivo (Fases 4 e 5) | R-02 |
| Confiança mínima de um relato | só `High`; `Medium`, `Low` e `Unknown` contam como desconhecido | a SPEC exige "high" para parado e não fala das outras atividades |
| Dois relatos diferentes para o mesmo trecho | trecho desconhecido, com flag; nunca o mais favorável | MOT-014 pede "unknown no conflito" |
| `hasReliableCoverage` do esboço de `MotionInterval` (§4.4) | removido; relato sem cobertura confiável não deve ser enviado, ou vem com confiança abaixo de `High` | campo sem regra de uso na SPEC |
| Capacidade | 32 relatos; com a lista cheia o relato novo é recusado. A regra de esquecimento vem com as sessões (Fase 3) | não previsto |

Consequência conhecida: tirar um relato que **contradiz** outro melhora o resultado, porque o trecho deixa de ser conflito. Isso é exigido por MOT-014 e não é perda de cobertura no sentido de INV-08; a propriedade testada vale para relatos que não se sobrepõem.

As hipóteses do passo 2b (regra de rótulo do detector de saltos) estão em `PHASE_2_REPORT.md`.

### R-29 [M] Escolha da leitura e relatório: hipóteses fixadas no passo 2d

| Ponto | Escolha | Onde a SPEC é omissa ou foi revista |
|---|---|---|
| Critério principal da escolha | menor "quão longe o celular pode estar" (accuracy + deslocamento possível); leitura rotulada "provável salto" perde para qualquer outra | §6.5 itens 3 a 5 |
| Desempates | a mais nova; depois a menor accuracy; depois a que chegou primeiro. O critério "source" da SPEC não entra: a procedência depende de quem pergunta, e duas leituras com a mesma incerteza, idade e accuracy não diferem em nada que ele decidiria | §6.5 item 4 |
| Consistência | conta as leituras do histórico que não contradizem a escolhida. Se qualquer uma a contradiz, vale `ReadingsDisagree` (0,30 e teto 75), mesmo quando o outlier foi identificado | §6.7 ("0.30 if conflicting evidence/no strong consensus") e JMP-013 |
| Só restam prováveis saltos | a melhor delas é devolvida, com status `degraded`, relação inconclusiva e teto 20 | §6.5 item 5 |
| Procedência (`source`) | pelo número de chegada; leitura que o SO entrega de novo não ganha número novo e portanto não vira `fresh` | D-06 pedia "marcar cada amostra com a request e a fase" |
| Só há leituras mais velhas que `maxFallbackAgeMs` | `unavailable`, sem posição, com `location_stale` e `no_usable_fix` | §5.3 item 5 admitia `degraded` |
| Sem permissão de localização | `unavailable` sem posição, mesmo com leitura boa na memória | §6.5 item 1 |
| Velocidade do GPS que não bate com o deslocamento | comparação com a leitura anterior, contando o erro das duas posições e a folga de aceleração (2,5 m/s² × dt / 2). Se não bate, a velocidade é ignorada no cálculo da distância e sai `location_metadata_inconsistent` | JMP-018 só pede o código de razão. O sentido perigoso (GPS diz "quase parado" e as posições mostram movimento) não estava previsto |
| Status com permissão aproximada | sempre `degraded` | D-06 não lista |
| `uncertainty_model_heuristic` | presente em todo relatório que tem posição | §6.4 |
| `motion.state` e `motion.confidence` | o relato de maior confiança que cobre o agora, inclusive `Medium` e `Low` (só informativo; a distância continua usando só `High`) | campos sem regra |
| `motion.coverageStartAgeMs` | há quanto tempo a atividade é conhecida sem interrupção até agora, contado só depois da leitura | campo sem regra |
| `motion.gapsDetected` | parte do tempo desde a leitura é conhecida e parte não | campo sem regra |

Consequência conhecida para INV-08: por leitura, o score e a incerteza são monótonos no tempo. Quando a leitura escolhida muda, o score do relatório pode subir, porque a escolha minimiza a incerteza e não maximiza o score (é o que §6.5 pede). O mesmo vale quando o histórico esquece, aos 120 s, uma leitura que contradizia a escolhida. As propriedades testadas valem "enquanto a leitura escolhida é a mesma".

## Parte 6 — Achados durante a Fase 3 (aguardam ciência do owner)

### R-30 [M] Política de tentativas, motor e fachada: escolhas da Fase 3

| Ponto | Escolha | Onde a SPEC ou a Fase 0 dizia outra coisa |
|---|---|---|
| O que o core devolve à plataforma | como as coisas devem estar agora (GPS ligado? a cada quanto? atividade? acordar quando?) e uma lista de relatórios prontos. Não há lista de efeitos por avaliação nem `generation` | `NATIVE_TRANSPORT_SPEC.md` §4 (`lce_effects`), ADR-002. Ver ADR-019 |
| Quem conta os interessados no GPS | o core: qualquer avaliação em curso ou sessão coletando | ADR-008 deixava na plataforma |
| Timers | um só, para o motor inteiro; chamada adiantada ou repetida é inofensiva | `NATIVE_TRANSPORT_SPEC.md` §7 |
| `QUERY_MOTION` | não existe. A atividade chega por `lce_phone_activity_changed` enquanto alguma avaliação a quer; nenhuma avaliação espera por ela | §5.1 e §5.2 (consulta de até 500 ms), R-03 |
| Relato de atividade em curso | o core guarda a atividade em curso e a conta até o agora de cada avaliação | R-28 dizia que a plataforma estenderia o relato |
| Onde vive a evidência de atividade | no motor: uma linha do tempo do aparelho, não uma por sessão | §3.6 (`ingestMotion(sessionId, ...)`) |
| Motion opt-in | por avaliação (`enableMotionAssistance`), e só quando a permissão já existe. Não há opção na sessão | `IMPLEMENTATION_PLAN.md` §6 item 5 dizia "por sessão" |
| Primeira leitura fraca | entra no tempo extra na hora, sem esperar o fim da primeira tentativa. Com tempo extra zero, continua esperando até o fim da primeira tentativa | §5.1 |
| Primeira tentativa acaba sem nada novo | termina com a memória só quando há leitura lembrada com o celular parado desde ela, precisa e sem suspeita; senão, tempo extra | §5.1 (`HISTORY_STATIONARY_CHECK`) |
| GPS inutilizável no início | não liga o GPS; responde com a memória: `degraded` se há leitura, senão `unavailable` | REC-021 dizia `unavailable`; REC-025 pede `degraded` quando há histórico |
| Sem permissão | `unavailable` sempre, memória não usada | — |
| Background, cancelamento, sessão fechada | `status = cancelled`, sem posição | CON-009 admitia "cancelar/degraded conforme política" |
| Permissão perdida em uso | `unavailable` com `permission_denied`; históricos apagados | CON-021, §5.5 |
| Falha do provedor | termina com a memória e `native_provider_unavailable`; a coleta das sessões para | REC-023, REC-025 |
| Limites | 8 sessões, 16 avaliações, 4 por sessão; acima disso `busy` | ADR-008 propunha 4 por sessão |
| Coleta de sessão (monitoring) | uma leitura guardada a cada `monitoringIntervalMs`; o GPS é pedido no menor intervalo entre as sessões, e a cada 1 s enquanto há avaliação | R-09 |
| Coleta que parou | não volta sozinha. Se parou por tempo, os relatórios seguintes daquela sessão trazem `monitoring_duration_reached`; as outras causas não são anexadas | SES-006, SES-007 |
| Reason code novo | `simulated_location_source` | não previsto |
| `fallbackCause` | o motivo de ter ido além da primeira tentativa: `initial_fix_timeout`, ou a fraqueza da primeira leitura (`location_low_accuracy`, `possible_mock_location`, `precision_reduced`, `suspected_gps_jump`, `location_stale`, `no_usable_fix`) | campo sem regra |
| `ageReliability = unreliable` | só quando nada pôde ser usado e leituras foram descartadas por problema de hora | campo sem regra |
| Entrega de mais de 16 leituras de uma vez | a fachada passa ao motor em pedaços de 16; numa sessão que só coleta, só o primeiro pedaço é guardado (o ritmo de coleta vale por pedaço) | não previsto |

## Parte 7 — Achados durante as fases de plataforma e de API (aguardam ciência do owner)

### R-31 [M] Android: escolhas da Fase 4

| Ponto | Escolha | Onde a SPEC ou a Fase 0 dizia outra coisa |
|---|---|---|
| Como os dados atravessam o JNI | cada struct vira uma lista de números (`DoubleArray`) escrita e lida na mesma ordem, com um número de conferência no fim; sem acesso a campos Java por nome | `NATIVE_TRANSPORT_SPEC.md` descrevia objetos |
| Coordenação | um `EngineCoordinator` para o motor inteiro; não há `NativeEvaluationCoordinator` por avaliação nem `SessionRegistry`, porque o core guarda as sessões (R-30) | §3.4 |
| Concorrência | uma fila serial de uma thread; sem coroutines | §3.4 (`CoroutineScope`), plano §3 (coroutines-test) |
| Testes | os mesmos testes Kotlin rodam na JVM (motor compilado para o computador) e no aparelho (a `.so` entregue) | plano §3 previa suítes separadas |
| GPS | só `requestLocationUpdates` em alta precisão, no intervalo pedido pelo motor; `getLastLocation` e `getCurrentLocation` nunca são chamados | §5.1 (one-shot); D-03 |
| `source = cached` | significa apenas "lembrada pela sessão"; a leitura velha guardada pelo sistema não é consultada no Android | §4.2 |
| Background | `ProcessLifecycleOwner`; o aviso chega cerca de 0,7 s depois de o app sair da tela | R-08 |
| `denied` contra `not_determined` | o Android não diferencia; `denied` só depois de o app pedir nesta execução | §4.5 |
| Motion | não suportado na v1.0: `motion: 'unsupported'`, `motion_unavailable`; `requestMotionPermission` não mostra diálogo | D-02 |
| Modo simulação | o app troca o GPS por leituras próprias; sem GPS real, sem permissão, e todo relatório leva `simulated_location_source` | não previsto |
| Sinal de mock | `Location.isMock` (Android 12+) ou `isFromMockProvider` | — |
| Permissões no Manifest | a biblioteca não declara nenhuma; o app declara as duas de localização | R-21 |
| `libc++_shared.so` | fica fora do AAR (o React Native já traz); o app de teste instrumentado mantém a sua cópia | — |

### R-32 [M] Fachada TypeScript: escolhas da Fase 6

| Ponto | Escolha | Onde a SPEC ou a Fase 0 dizia outra coisa |
|---|---|---|
| Módulo nativo ausente | importar nunca falha; a primeira chamada rejeita com `NativeModuleError` (`native_module_missing`) | §4.5 falava em "erro irrecuperável de Codegen na inicialização" |
| Versão do nativo | cada relatório traz a versão do motor; se diferir da esperada pelo JavaScript, rejeita com `native_version_mismatch` | não previsto |
| Sessões demais | `startSession` rejeita com `LocationConfidenceError` de código `busy` | D-07 definia só `{ id, monitoring, reasonCodes }` |
| `SessionOptions` | sem `mode` e sem `historyCapacity` (D-07); sem `monitoringDistanceFilterMeters` (R-09) | §4.2 |
| `maxMonitoringDurationMs` | mínimo 1000; acima de 3 600 000 (1 hora) é reduzido para 1 hora | §4.2 dizia "capped" sem número |
| `monitoringIntervalMs` | inteiro de 1000 a 60000 | §4.2 só dava o padrão |
| `maxFreshAgeMs`, `maxFallbackAgeMs` | no máximo 120000: é o que o motor lembra | §4.3 só exigia `>= 0` |
| `initialFixBudgetMs` | acima do tempo total é reduzido para o tempo total | §4.3 ("max total") |
| Cancelado antes de começar | relatório feito em JavaScript: `cancelled`, `request_cancelled`, `phaseTrace` = `started`, `finished`, `permissionPrecision` = `unknown` | §4.3 só dizia `status='cancelled'` |
| Erros vindos do nativo | viram `InvalidArgumentError` ou `NativeModuleError`; código desconhecido vira `native_internal_error` | — |
| Simulação | `LocationConfidence.simulation` (`enable`, `disable`, `pushReadings`, `setPhoneActivity`) | não previsto |
| O que o pacote exporta | o objeto `LocationConfidence`, as três classes de erro e os tipos; a função que monta o objeto fica interna | R-17 |

### R-33 [B] App de exemplo: escolhas da Fase 7

| Ponto | Escolha | Onde a SPEC ou a Fase 0 dizia outra coisa |
|---|---|---|
| Demonstração | coordenadas inventadas em volta de um ponto fixo público; o simulador só troca a origem das leituras, e o motor decide tudo | plano §2: "modo simulação com fixtures" |
| Roteiros | código TypeScript (`scenarios.ts`), não arquivos JSON | plano §2 |
| Mapa | desenhado com Views; nenhum serviço de mapas, chave ou rede | — |
| Avaliações do simulador | tempo total 1 s, primeira tentativa 0,8 s, sem tempo extra | padrões da §4.2 (12 s, 4,5 s, 7 s) valem na tela "This phone" |
| Linha do tempo | uma faixa com as últimas respostas (altura = índice, cor = relação) | plano §2: "timeline" |
| Explicações | o exemplo traduz limites e motivos para frases; os códigos aparecem ao lado | — |
| Dependência extra | `react-native-safe-area-context`, só no exemplo | — |

### R-34 [M] iOS: escolhas da Fase 5

| Ponto | Escolha | Onde a SPEC ou a Fase 0 dizia outra coisa |
|---|---|---|
| Como o Swift fala com o motor | lê os structs de `lce.h` diretamente; não há wrapper Objective-C++ entre o Swift e o core | §3.5 (`LocationConfidenceCxxAdapter.mm`); D-08 previa esse wrapper só como plano B |
| Papel do `.mm` | só converte os tipos C++ gerados pelo Codegen em dicionários e chama o Swift | §3.5 |
| Coordenação e concorrência | as mesmas do Android (R-31): um coordenador, uma fila serial | §3.5 |
| Hora da leitura | calculada a partir do relógio de parede e marcada como estimada; `ageReliability` é sempre `estimated` no iOS | R-14 |
| Relógio | `CLOCK_MONOTONIC`, que continua contando com o aparelho dormindo | R-14 |
| Intervalo de leituras | não existe no iOS; o motor guarda uma leitura a cada `monitoringIntervalMs` | R-09 |
| Motion | fluxo ao vivo (`startActivityUpdates`); nenhuma consulta ao histórico | §5.2, PLT-026 |
| Permissão de motion | o sensor só é usado quando a permissão já existe; o diálogo aparece só em `requestMotionPermission`, que faz uma consulta vazia para provocá-lo | PLT-025 |
| Dois estados de movimento ao mesmo tempo | vale o que permite o movimento mais rápido | não previsto |
| Sinal de mock | `CLLocationSourceInformation.isSimulatedBySoftware`; ausente vira "desconhecido" | PLT-022, PLT-023 |
| `Info.plist` sem o texto de localização | `requestLocationPermission` rejeita com `missing_usage_description` em vez de nunca responder | não previsto |
| Testes | `swift test` no Mac, por um `Package.swift` que não é distribuído; os mesmos roteiros do Kotlin | plano §3 previa XCTest com o app |
| Manifesto de privacidade | `PrivacyInfo.xcprivacy` no pod: sem rastreio, sem dado coletado, motivo `35F9.1` para o relógio | R-14 |

### R-35 [M] Preparo de release: achados da Fase 8

| Ponto | Situação |
|---|---|
| Testes Kotlin e o build do motor para o computador iam no pacote npm, e o `build.gradle` os ligava a toda tarefa de teste | corrigido: ficam fora do pacote, e o `build.gradle` só monta a parte de testes quando as fontes de teste existem |
| Arquivo solto `android/src/main/cpp/Untitled` ia no pacote npm | excluído do pacote; o arquivo não foi tocado |
| `yarn lint` falhava depois de rodar os testes, por causa de relatórios e arquivos gerados em pastas de build | o lint passou a ignorar essas pastas |
| Binários pré-compilados do React Native na variante errada depois de trocar de configuração no iOS | não é da biblioteca; documentado no `CONTRIBUTING.md` |
| `peerDependencies` aceita qualquer React Native; só a 0.88.1 foi validada (PUB-025) | em aberto: decisão do owner |
| `files` do `package.json` lista `react-native.config.js`, que não existe na raiz | inofensivo; mantido |
| Integração contínua | jobs escritos, nunca executados (não há remoto) |
| PUB-015, aparelhos físicos, stress e benchmark | não feitos |

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
