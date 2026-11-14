# CLAUDE.md — Location Confidence Engine (v2)

## Documentos obrigatórios

- Leia **integralmente** `SPEC_Location_Confidence_Engine_v2.md` antes de propor ou alterar qualquer arquitetura.
- Essa SPEC (v2.0, 14/11/2026) contém **289 cenários normativos de teste**, decisões ADR-001..ADR-012, contratos públicos e plano de implementação Fase 0..9.
- Se detectar inconsistência, **não corrija silenciosamente**: registre-a em `docs/SPEC_REVIEW.md` com seção/linha, risco, alternativas, recomendação e impacto nos testes; peça decisão quando alterar contrato público.

## Objetivo do projeto

Publicar uma biblioteca npm React Native que avalia a qualidade de evidências de geolocalização para entregas em primeiro plano, recupera leituras imprecisas/timeout de modo adaptativo e **não confunde um score heurístico com porcentagem real de presença**.

## Arquitetura obrigatória

- **TypeScript Clean Architecture:** API tipada, use cases públicos, aplicação e adapters do TurboModule. Sem duplicação de cálculo geográfico, score ou histórico no TS.
- **Kotlin Android:** interfaces `LocationSource`, `MotionSource`, `PermissionSource`, `CoreEnginePort`; adapters Google FLP/Activity Recognition; coordinator de callbacks, timeout e cancelamento.
- **Swift iOS:** protocolos equivalentes; Core Location/Core Motion; coordinator/ownership adequado; Objective-C++ interop.
- **C++17 único e puro:** recuperação como reducer de eventos `RecoveryPolicy`, ring buffer por sessão, sample validation, Haversine, outliers, motion uncertainty, candidate selection, score e assessment. Sem imports de React Native, JSI, Android/Apple nem acesso direto a GPS.
- **Transportes:** TurboModule/Codegen para RN ↔ plataforma; JNI e Objective-C++/wrapper para Kotlin/Swift ↔ C++; não presumir que Codegen cria JNI/ObjC++ por conta própria.

## Regras inegociáveis

1. Não coletar localização durante importação, `getCapabilities`, abertura de tela ou requests sem autorização prévia. `startSession` default `evaluations_only` não ativa GPS; monitoramento `foreground_monitoring` é opt-in explícito.
2. Sem rede, geocoding, Maps, backend, analytics, armazenamento permanente de localização ou tracking background na v1.
3. `qualityScore` é **índice 0–100**, nunca percentual de probabilidade; `probabilityWithinRadius = null` na v1.
4. Não inventar coordenada nova: use somente fixes medidos; distinguir `fresh/recovered/cached/unavailable`, preservar idade real.
5. `stationary` agora não demonstra estacionariedade durante todo o intervalo; lacunas de motion devem ampliar incerteza.
6. Não adicionar `setInterval` infinito, watchers sem owner, JNI por cada operação aritmética, chamadas bloqueantes na UI, threads nativas não canceláveis ou callbacks sem cleanup.
7. Toda sessão/request tem id/generation e lifecycle explícito; cancelar toda API/timer ao stop, abort, permission revoked, background, deadline, invalidation.
8. Não reivindicar suporte de versão RN, iOS, Android ou Expo Go sem compilar e testar; não reivindicar ganho de performance sem benchmark.
9. Não introduzir frameworks DI pesados e não multiplicar classes vazias apenas para “parecer Clean Architecture”.
10. Não rodar `npm publish` nem publicar conteúdo, criar tags ou releases sem pedido explícito do usuário.

## Fluxo do agente — estritamente por fase

### Fase 0: somente análise

1. Inspecionar repo/toolchain instalada. Se repo vazio, discutir `create-react-native-library` e variante suportada.
2. Verificar docs de Codegen e API de módulos C++ **da versão real**. Propor DTOs de bridge plausíveis.
3. Revisar ADRs, estrutura de arquivos, fronteiras de threads, JNI/ObjC++ ownership, política de cancelamento, `foreground_monitoring` e semântica do score.
4. Revisar a SPEC criticamente, mapear os 289 testes em suites/fixtures. Identificar bloqueadores de compilação/permissão e elaborar um plano incremental.
5. Entregar `docs/SPEC_REVIEW.md`, `docs/COMPATIBILITY_PLAN.md`, proposta `docs/ADR/` e `docs/IMPLEMENTATION_PLAN.md` (arquivos de planejamento podem ser criados na Fase 0).
6. **PARAR**, sem código de produção. Pedir aprovação para Fase 1.

### Fases 1 a 8

Executar apenas a fase explicitamente autorizada na SPEC. Cada resposta deve registrar:

- Objetivo da fase e decisões adotadas.
- Arquivos criados/modificados.
- Comandos de build/test realmente executados e exit status.
- IDs normativos de testes criados, aprovados, falhos, bloqueados.
- Falhas conhecidas, riscos e próximo gate de aprovação.

Sem `TODO()` oculto, sem fallback silencioso e sem declarar “teste passou” a partir de leitura de código. Use fixtures determinísticas, fake clocks e unit tests; não dependa de GPS real em CI unit.

## Definition of Done (v1)

- C++ core compilado/testado como pacote independente.
- Mesmo core C++ linkado em ambos Android e iOS; exemplo compilado em release nas configurações anunciadas.
- Contratos TypeScript e Codegen compatíveis com o RN realmente instalado.
- Matriz de 289 testes normativos mapeada; todos os casos críticos implementados; exceções documentadas.
- Cleanup/concorrência/cancelamento demonstrados por testes com callbacks tardios e stress.
- App exemplo em foreground e simulador de fixtures funcionais.
- README inglês, API reference, troubleshooting, permissions/privacy/limitations, changelog, license, CI de release, `npm pack --dry-run`.
- Nenhum upload/log permanente de coordenadas; nenhum claim de “probabilidade real” ou “prova de presença”.

## Prompt preferencial quando iniciar o projeto

> Leia a SPEC e este CLAUDE.md e execute somente a Fase 0. Faça revisão crítica dos trade-offs arquiteturais e verifique compatibilidade do Codegen/JNI/Swift/ObjC++. Não produza código de produção até revisão e aprovação.
