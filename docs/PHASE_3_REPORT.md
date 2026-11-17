# Fase 3 — política de tentativas, motor e fachada C: relatório

Data: 16/11/2026. Branch `phase/3-policy-facade`, a partir de `phase/2-models`.

## Objetivo

Fechar o core C++: tudo o que a plataforma (Kotlin/Swift) precisa para funcionar passa a existir atrás de `lce.h`. Três peças:

| Peça | Arquivos | Na SPEC |
|---|---|---|
| Uma avaliação, do pedido à resposta | `ProximityRequest.hpp/.cpp` | `RecoveryPolicy` |
| O motor: sessões, avaliações em curso, atividade do celular | `Engine.hpp/.cpp` | `LocationEngine` |
| A porta de entrada em C | `cpp/include/location_confidence/lce.h`, `CFacade.cpp` | fachada C (ADR-013) |

## Ordem das chamadas

```text
plataforma (Kotlin / Swift)
  |
  |  lce_start_request()               lce_readings_arrived()        lce_time_passed()   ...
  v
CFacade.cpp          só traduz: struct C <-> tipos C++
  v
Engine.cpp
  |-- startRequest()
  |     `-- request.start()                               ProximityRequest.cpp
  |-- whenReadingsArrive()
  |     |-- checkReading()                                ReadingChecker.cpp   (fase 1)
  |     |-- storeOneReading() -> history.store()          LocationHistory.cpp  (fase 1)
  |     `-- request.whenReadingsArrive()
  |           `-- buildReport()                           Report.cpp           (fase 2)
  |-- whenTimePasses()
  |     `-- request.whenTimePasses()
  `-- collectFinishedRequests()        o relatório vai para a lista de espera

depois de CADA chamada, a plataforma pergunta e obedece:
  lce_take_finished_report()             há relatório pronto? entrega ao app
  lce_what_the_platform_must_do_now()    GPS ligado? a cada quanto? atividade? acordar quando?
```

## Uma avaliação, passo a passo

```text
PRIMEIRA TENTATIVA (até 4,5 s)
  leitura boa chega            -> fim ("fresh")
  leitura fraca chega          -> TEMPO EXTRA na hora
  nada chega                   -> há leitura lembrada boa o bastante?  fim : TEMPO EXTRA

TEMPO EXTRA (até 7 s)
  leitura boa chega            -> fim ("recovered")
  o tempo extra acaba          -> fim com o que há na memória, ou sem nada

A QUALQUER MOMENTO
  tempo total (12 s), cancelamento, background, sessão fechada, permissão perdida -> fim
```

Uma avaliação sempre termina, e termina uma vez só. Ela não lê relógio e não toca no GPS: recebe "o que aconteceu" e "que horas são".

## Decisões adotadas

A mudança de desenho mais importante da fase: **o core não devolve uma lista de ordens; devolve como as coisas devem estar agora.**

O esboço da Fase 0 (`NATIVE_TRANSPORT_SPEC.md` §4) previa efeitos por avaliação (`START_ACQUISITION`, `STOP_ACQUISITION`, `FINISH`) com `generation`. Uma ordem pode se perder ou ser executada duas vezes, e a plataforma precisaria contar quantas avaliações usam o GPS. No lugar disso:

- `lce_what_the_platform_must_do_now()` responde "o GPS deve estar ligado? a cada quanto? as atualizações de atividade? quando devo ser chamado de novo?". A plataforma compara com o que tem e ajusta. A mesma resposta duas vezes significa "não mude nada".
- Quem conta os interessados no GPS é o core. Duas avaliações na mesma sessão compartilham o GPS sem que Kotlin ou Swift saibam disso (ADR-008).
- Há um timer só, para o motor inteiro. Chamar `lce_time_passed()` adiantado ou repetido é inofensivo, então não existe `generation` de timer.
- Relatórios prontos ficam numa lista de espera e saem por `lce_take_finished_report()`.

Com isso o código de plataforma fica pequeno e igual nas duas: avisa o core, depois obedece. Está registrado como ADR-019 (proposta) em `docs/ADR/README.md`.

Outras decisões estão em `SPEC_REVIEW.md`, R-30. As principais:

- Não existe `QUERY_MOTION`. A atividade do celular chega por `lce_phone_activity_changed()`, e nenhuma avaliação espera por ela.
- O core guarda a atividade "em curso" e a conta até o agora de cada avaliação. A plataforma só informa mudanças e a parada das atualizações (R-28 dizia que a plataforma estenderia o relato).
- GPS inutilizável no início (localização desligada, sem provedor, app fora do foreground): o GPS não é ligado e a resposta vem da memória (`degraded` se há leitura, `unavailable` se não). Sem permissão, é sempre `unavailable` e a memória não é usada.
- Cancelamento, background e sessão fechada dão `status = cancelled`, sem posição.
- Sessão que não existe dá relatório `unavailable` com `unknown_session`, não erro.
- Limites: 8 sessões, 16 avaliações, 4 por sessão. Acima disso, `busy`.
- Novo reason code: `simulated_location_source`, para quando o app troca o GPS por leituras próprias (demo e testes). São 44 códigos agora.
- `LCE_ABI_VERSION` passou a 2.

## Arquivos

Criados: `cpp/src/ProximityRequest.hpp`, `cpp/src/ProximityRequest.cpp`, `cpp/src/Engine.hpp`, `cpp/src/Engine.cpp`, `cpp/src/ProximityRequest.study.ts`, `cpp/tests/TestProximityRequest.cpp`, `cpp/tests/TestEngine.cpp`, `docs/PHASE_3_REPORT.md`.

Reescritos: `cpp/include/location_confidence/lce.h`, `cpp/src/CFacade.cpp`, `cpp/tests/TestFacade.cpp`.

Modificados: `cpp/src/ReasonCodes.hpp/.cpp` (código novo), `cpp/src/PhoneActivityTimeline.hpp/.cpp` (a lista de relatos aprendeu a esquecer: `storeAndForgetTheOldestIfFull`, `forgetReportsThatEndedBefore`), `cpp/tests/TestReasonCodes.cpp`, `cpp/CMakeLists.txt`, `docs/SPEC_REVIEW.md`, `docs/ADR/README.md`.

## Como os testes simulam a plataforma

`TestEngine.cpp` tem um "celular de mentira" (`FakePhone`) que faz o que o código Kotlin/Swift vai fazer: depois de cada chamada, pega os relatórios, liga ou desliga o GPS de mentira conforme o motor mandar, e guarda quando o motor quer ser acordado. O tempo só anda quando o teste manda, e o GPS só entrega leitura enquanto está ligado. É o "driver de simulação" previsto no plano.

`TestFacade.cpp` usa só o que `lce.h` anuncia. Tem um cenário dourado (uma avaliação, uma leitura, todos os campos do relatório) que será repetido, com os mesmos números, nos testes Android (por JNI) e iOS (por Swift). Tem também um teste por campo que atravessa a porta: muda um campo de entrada e confere o que sai.

## Comandos executados e resultado

| # | Comando (resumo) | Exit | Resultado |
|---|---|---:|---|
| 1 | host C++17, `-Werror`, `lce_tests` | 0 | 354 casos, 0 falhas |
| 2 | host C++20 | 0 | 354 casos, 0 falhas |
| 3 | host C++17 com ASan + UBSan | 0 | 354 casos, 0 falhas, nenhum aviso dos sanitizers |
| 4 | 330 mutações (94 na avaliação, 106 no motor, 130 na fachada) | — | 327 detectadas, 3 equivalentes |
| 5 | espelho `ProximityRequest.study.ts` contra o C++: 10.000 avaliações geradas | 0 | 0 diferenças |
| 6 | os seis espelhos rodando sozinhos; `eslint` e `tsc` | 0 | sem erros |

Sobre as mutações: na primeira rodada, 317 foram detectadas, 9 não compilaram (deixavam função sem uso, e o build usa `-Werror`) e 4 não foram detectadas. As 9 foram reescritas e passaram a ser detectadas. Das 4, uma virou checagem nova (a precisão da permissão no relatório de avaliação interrompida). As outras 3 são equivalentes: a fachada confere se o número de um texto está na lista, e a função do core que ela chama confere de novo; tirar uma das duas checagens não muda nada. As duas ficam.

Antes de rodar as mutações, a lista em si mostrou lacunas, que viraram testes: leitura lembrada imprecisa, simulada ou suspeita não encurta a espera; atividade anterior a uma mudança é lembrada; atividade de confiança média não diminui a distância; background encerra o que se sabe da atividade; leitura com hora ligeiramente no futuro é usada e avisada; leitura que já chega com mais de 2 minutos não é guardada; e um teste por campo da fachada.

A suíte passou de 252 para 354 casos: 34 em `TestProximityRequest`, 47 em `TestEngine` e 22 em `TestFacade` (que tinha 1).

O executor de mutações ganhou uma versão rápida: cada trabalhador compila uma cópia inteira uma vez e, a cada mutação, recompila só o que depende do arquivo alterado. Para não testar binário velho, ele confere que o executável de teste é mais novo que a alteração.

## IDs normativos

Implementados e aprovados em host (por simulação, sem plataforma):

- REC-001 a 012, 014 a 017, 021, 022, 025 a 029. REC-013 (consulta de motion que nunca responde) não se aplica mais: não há consulta; o caso equivalente é MOT-012. REC-018 e 019 já estavam cobertos pela escolha da leitura (fase 2).
- CON-002, 004, 005, 006, 007, 009, 010, 011, 012, 013, 014, 015, 016, 018, 019, 020, 021 no nível do core.
- SES-001 a 012 no nível do core (SES-013 é validação do TypeScript).
- HIS-005, 006, 010, 011, 012, 013, 014, 016, 025, 027.
- MOT-010, 011, 012, 020, 022, 024, 025, 031.
- PUB-004, 005. PLT-003, 004 e 029 na parte que cabe ao core.
- GEO-012 a 019 como segunda barreira na fachada (a primeira é a validação em TypeScript).
- VAL-007, 008, 014 pelo caminho completo.
- NFR-04 e INV-10 como propriedades: toda avaliação termina até o tempo total e uma vez só; depois de qualquer sequência de pedidos e eventos, nada fica ligado.

Ficam para as fases de plataforma: REC-020, 023, 024 (comportamento das APIs do SO), CON-001, 003, 008, 017, 022 a 028, MOT-021, e os grupos PLT e PUB que dependem de Android/iOS.

## Não feito

- **TSan no teste de fachada**, previsto no plano. O motor é de uma thread só por contrato e a fachada não tem estado global, então não há o que o TSan observar em host. O confinamento a uma fila é da plataforma e é testado lá (CON-023).
- **Fixtures em JSON e nlohmann/json**, previstos no plano. Os roteiros são código C++ nos testes. O cenário dourado é repetido à mão nos testes de cada plataforma.
- **Espelho de estudo do `Engine` e da fachada.** A fachada só traduz valores. O motor é principalmente organização (listas de sessões e de avaliações); o desenho da ordem das chamadas e os três exemplos estão no topo de `Engine.cpp`. O espelho da avaliação (`ProximityRequest.study.ts`) existe e foi conferido contra o C++.
- Android e iOS ainda não chamam nada disso. O que está commitado neste branch compila nas duas plataformas (elas só usam `lce_abi_version`), mas a tela do exemplo passa a mostrar versão 2.

## Riscos e próximo passo

- A cauda de latência de detecção de atividade (R-02) continua em aberto até haver medição em aparelho.
- ADR-019 muda o contrato entre core e plataforma em relação ao esboço da Fase 0. Está implementada e testada, mas aguarda sua ciência.
- Próxima fase: Android (JNI completo, portas, GPS do Google Play services, coordenador).
