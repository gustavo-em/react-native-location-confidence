# Fase 6 — fachada TypeScript: relatório

Data: 16/11/2026. Branch `phase/6-ts-api`, a partir de `phase/4-android`. A fase 6 veio antes da 5 (iOS) porque o app de exemplo, usado para verificar o Android, precisa dela.

## Objetivo

Dar ao app um objeto só, `LocationConfidence`, com tipos ricos e erros claros. A camada é fina de propósito: confere os argumentos, chama o lado nativo e renomeia a resposta. Nenhuma conta de localização acontece em JavaScript.

## Ordem das chamadas

```text
app
  |  LocationConfidence.getProximityReport({ destination, radiusMeters, options, signal })
  v
LocationConfidence.ts
  |-- checkProximityRequest()           validation.ts   lança InvalidArgumentError; nada nativo ainda
  |-- checkSignal()
  |-- makeRequestId()                   "lc-1-k3f9x2"
  |
  |-- o sinal já estava abortado?
  |     `-- reportCancelledBeforeItStarted()            mapper.ts   responde aqui, sem chamar o nativo
  |
  |-- native.getReport(requestId, request)              Kotlin / Swift
  |     (se o app abortar no meio: native.cancelRequest(requestId))
  `-- toProximityReport(requestId, nativeReport)        mapper.ts   números do motor -> textos públicos
```

Um exemplo, do começo ao fim:

```text
getProximityReport({ destination: { latitude: 0, longitude: 0 }, radiusMeters: 100 })

  checkProximityRequest  ->  { sessionId: null, latitude: 0, longitude: 0, radiusMeters: 100,
                               totalDeadlineMs: 12000, initialFixBudgetMs: 4500, recoveryWindowMs: 7000,
                               maxFreshAgeMs: 10000, maxFallbackAgeMs: 60000, ... }
  native.getReport       ->  { transportVersion: 2, status: 0, source: 0, evidenceRelation: 0,
                               qualityScore: 75, appliedCaps: ['single_fix'], ... }
  toProximityReport      ->  { status: 'available', source: 'fresh', evidenceRelation: 'supports_inside',
                               qualityScore: 75, probabilityWithinRadius: null, ... }
```

## Arquivos

| Arquivo | Papel |
|---|---|
| `src/NativeLocationConfidence.ts` | o contrato Codegen (commit da fase 4) |
| `src/types.ts` | os tipos públicos |
| `src/errors.ts` | `LocationConfidenceError`, `InvalidArgumentError`, `NativeModuleError` |
| `src/validation.ts` | limites, padrões e conferência dos argumentos |
| `src/mapper.ts` | o único lugar que sabe qual número do motor significa qual texto |
| `src/LocationConfidence.ts` | monta o objeto público sobre um módulo nativo (que os testes trocam por um falso) |
| `src/index.tsx` | o que o pacote exporta |
| `src/__tests__/` | Jest, em Node puro, sem React Native |

## O que é erro e o que é resposta

Erros (a Promise é rejeitada) são só engano de programação ou instalação quebrada:

| Erro | `code` | Quando |
|---|---|---|
| `InvalidArgumentError` | `invalid_argument` | argumento sem sentido; traz `argument` com o nome do campo |
| `NativeModuleError` | `native_module_missing` | a parte nativa não está no app (falta rebuild, ou Expo Go) |
| `NativeModuleError` | `native_version_mismatch` | o código nativo é de outra versão do motor |
| `NativeModuleError` | `platform_unsupported` | a biblioteca nativa não existe para o processador do aparelho |
| `NativeModuleError` | `native_internal_error` | o lado nativo falhou de um jeito não previsto |
| `NativeModuleError` | `no_activity` | Android: não há tela para mostrar o diálogo de permissão |
| `LocationConfidenceError` | `busy` | sessões demais abertas ao mesmo tempo |

O resto (sem permissão, GPS desligado, tempo esgotado, cancelamento, app no fundo) volta como relatório, com `status` e `reasonCodes`.

## Decisões adotadas

Registradas em `SPEC_REVIEW.md`, R-32. As principais:

- **Importar nunca falha.** O módulo nativo é buscado com `TurboModuleRegistry.get`; se faltar, o erro aparece na primeira chamada, dizendo o que fazer.
- **Versão conferida em cada relatório.** Se o nativo é de outra versão do motor, o relatório é recusado em vez de ser lido errado.
- **Sessões demais é erro.** O motor aceita 8 sessões. Devolver uma sessão sem `id` só empurraria o problema para a chamada seguinte.
- **`maxMonitoringDurationMs` tem teto de 1 hora.** A SPEC diz "capped" sem dar o número. Valor acima do teto é reduzido para o teto, não recusado.
- **Sem `monitoringDistanceFilterMeters`.** Segue R-09: filtro de distância esvazia o histórico de quem está parado.
- **API de simulação** (`LocationConfidence.simulation`), nova, para demonstração e testes.

## Comandos executados

Na raiz, com Node 24.22.3.

| Comando | Resultado |
|---|---|
| `npm exec --no -- tsc --noEmit` | exit 0 |
| `npm exec --no -- eslint src example/src jest.config.js` | exit 0, sem avisos |
| `npm exec --no -- jest` | exit 0. 4 arquivos, 86 testes, 0 falhas |
| `npm exec --no -- bob build` | exit 0. 7 arquivos em `lib/module`, tipos em `lib/typescript` |

O Jest roda com `watchman: false` (uma varredura simples de diretório basta para poucos arquivos).

## IDs normativos

Aprovados em Jest:

- GEO-012 a 019 e GEO-026 (validação; "nenhum sensor iniciado" é conferido pelo módulo falso, que anota toda chamada).
- CON-001 (abortado antes de começar: `cancelled`, nenhuma chamada nativa), CON-002 e 005 na parte do JavaScript (uma resposta só; abortar depois da resposta não faz nada; nenhum ouvinte sobra no sinal), CON-006, CON-008, CON-012, CON-013.
- SES-001, 002, 003 e 013 na parte do JavaScript.
- SCR-005 (`probabilityWithinRadius` é sempre `null`, conferido também pelo compilador) e SCR-030 (nenhum campo afirma autenticidade; conferido pelo compilador e sobre o JSON do relatório).
- O cenário dourado atravessa o mapper com os mesmos números do C++ e do Kotlin.

## Não feito

- **Teste de tipos com ferramenta própria** (tsd ou parecido). As duas conferências de tipo que importam estão em `mapper.test.ts` e são verificadas pelo `tsc`.
- **Smoke no iOS**: depende da fase 5. No Android o app de exemplo exercitou a fachada inteira no emulador (ver relatório da fase 4).

## Riscos e próximo passo

- O `requestId` usa `Math.random` só para não repetir nomes entre avaliações em curso. Não é identificador de segurança.
- Próximo: app de exemplo (fase 7) e iOS (fase 5).
