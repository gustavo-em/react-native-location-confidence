# ADRs — revisão das ADR-001..012 e propostas ADR-013..018

Status possíveis: **Aceita**, **Aceita com emenda**, **Proposta** (aguarda aprovação). Os itens R-xx referem-se a `docs/SPEC_REVIEW.md`.

**Atualização 14/11/2026:** o owner aprovou D-01 a D-09. As emendas da Parte A e as ADR-013 a ADR-017 passam a **Aceita**. ADR-013 foi confirmada pelo spike (Swift importa `lce.h` nas duas configurações de pod; ver `docs/PHASE_0_5_REPORT.md`). ADR-015 continua condicionada à medição de latência em dispositivo. ADR-018 não estava vinculada a uma decisão D-xx e segue como **Proposta**.

## Parte A — ADRs da SPEC

### ADR-001 — TS + core C++17 + adapters — Aceita com emenda

- **Mantido:** opção D (adapters Kotlin/Swift + core C++ compartilhado).
- **Emenda:** registrar que um core em TypeScript (opção A) seria mais barato e de menor risco; D se justifica por manter histórico de coordenadas fora do heap JS, ingerir monitoring sem acordar o JS e permitir reuso fora de RN, não por desempenho nem por "autonomia com JS bloqueado" (R-22). Reduzir as camadas TS ao necessário (R-17).
- **Consequência negativa:** três toolchains, dois caminhos de marshalling, depuração entre linguagens, matriz de ABI. Custo estimado: é a origem da maior parte do esforço das Fases 0.5, 4 e 5.
- **Gatilho de revisão:** spike falhar em linkar o core nas duas plataformas em release.

### ADR-002 — RecoveryPolicy como reducer em C++ — Aceita com emenda

- **Mantido:** política determinística no core; plataforma executa efeitos e impõe deadline real.
- **Emenda (R-16, R-04):** eventos carregam `nowMonotonicMs`; retorno é lista limitada de efeitos; `ASSESS_SNAPSHOT` deixa de ser comando (o core avalia internamente e `FINISH` traz o resultado); comandos de aquisição passam a `START_ACQUISITION`/`STOP_ACQUISITION` se D-03 for aprovada.
- **Consequência negativa:** a política fica longe das APIs que ela dirige; um efeito mal interpretado por uma plataforma só aparece em teste de integração. Mitigação: o mesmo roteiro de eventos roda contra fakes nas duas plataformas.

### ADR-003 — ABI e integração — Aceita com emenda (ver ADR-013)

- **Mantido:** sem ponteiros C++ no TS, lote em vez de callback por fix, fontes compiladas junto da lib.
- **Emenda:** a entrada do core passa a ser uma fachada C; no iOS o wrapper ObjC++ vira plano B.
- **Consequência negativa:** a fachada é uma segunda API a manter além dos headers C++; structs POD de tamanho fixo impõem limites (número de reason codes, tamanho do trace).

### ADR-004 — Histórico e concorrência — Aceita com emenda

- **Mantido:** ring buffer de 10 no core, sem thread própria, sem locks internos.
- **Emenda (R-15, R-10, R-14):** uma fila serial para o módulo inteiro, não uma por sessão; remoção do mais antigo protegendo o melhor candidato; dedup por conteúdo.
- **Consequência negativa:** uma sessão lenta atrasaria as outras. Aceitável porque cada evento custa microssegundos e o trabalho é limitado a 10 amostras; revisar se algum dia houver processamento pesado por sessão.

### ADR-005 — Score, não probabilidade — Aceita

- **Consequência negativa:** um índice não calibrado tem utilidade limitada para automação; consumidores vão tratá-lo como porcentagem apesar dos avisos. O teto de 75 no caminho de leitura única (R-19) precisa estar documentado.

### ADR-006 — Nunca inventar posição — Aceita com emenda

- **Emenda (R-01):** usar a velocidade do próprio fix para limitar a magnitude do deslocamento possível não cria posição e é compatível com esta ADR. O texto deve distinguir "projetar coordenada" (proibido) de "limitar incerteza com evidência do fix" (permitido).
- **Consequência negativa:** sem predição, fixes envelhecidos perdem valor rapidamente; mais resultados inconclusivos do que um filtro de navegação daria.

### ADR-007 — Deadline global e cancelamento estruturado — Aceita com emenda

- **Emenda (R-13):** `requestId` gerado no JS. Registrar que `status = 'cancelled'` em vez de rejeição diverge da convenção de `AbortSignal` (`AbortError`), por escolha.
- **Consequência negativa:** quem usa `try/catch` para cancelamento precisa checar `status`.

### ADR-008 — Uma coleta compartilhada — Aceita com emenda

- **Emenda (R-04):** com stream único e contagem de assinantes, o compartilhamento é o caso normal desde o início, porque monitoring + avaliação (SES-011) já o exige. `EvaluateOptions` não tem opção de modo de precisão na v1, então "políticas incompatíveis" (CON-014) não ocorre pela API pública; o teste fica no nível de porta. Limite de avaliações simultâneas por sessão (proposta: 4), além disso `busy`.
- **Consequência negativa:** contagem de assinantes é fonte clássica de vazamento (stream que nunca para). Coberto por CON-013, CON-015, CON-016.

### ADR-009 — Privacidade — Aceita com emenda

- **Emenda (R-14):** incluir `PrivacyInfo.xcprivacy` no pod.
- **Consequência negativa:** sem persistência não há histórico após process death; o fallback histórico é fraco em sessão recém-criada.

### ADR-010 — Energia e observabilidade — Aceita com emenda

- **Emenda (R-04, R-09):** "one-shot antes de coleta" vira "stream único encerrado no primeiro fix adequado" se D-03 for aprovada; filtro de distância default 0 no monitoring; registrar que no iOS o intervalo de monitoring não reduz consumo.
- **Consequência negativa:** monitoring sem filtro de distância gasta mais; limitado por `maxMonitoringDurationMs`.

### ADR-011 — Testes e confiança de publicação — Aceita com emenda

- **Emenda (R-20, R-07):** definir P0; classificar os 19 casos não automatizáveis; redefinir equivalência entre plataformas; fixtures executadas pela fachada C nas duas plataformas.
- **Consequência negativa:** CI com macOS e emulador Android é lento e caro; testes de dispositivo real continuam manuais.

### ADR-012 — Compatibilidade e versões — Aceita com emenda

- **Emenda (R-06):** "compilar como C++17" vira "fonte no subconjunto C++17, compilada em 17 e em 20", porque o RN 0.88.1 impõe C++20 aos pods. Alvo inicial: RN 0.88.x, Android minSdk 24, iOS 15.1.
- **Consequência negativa:** suporte inicial a uma única minor de RN reduz adoção.

## Parte B — Novas ADRs propostas

### ADR-013 — Fachada C como única entrada do core — Proposta (D-08)

- **Contexto:** ADR-003 prevê JNI de um lado e wrapper ObjC++ do outro, cada um com sua conversão.
- **Alternativas:** (a) wrapper ObjC++ com classes-modelo ObjC, como na SPEC; (b) interop Swift↔C++ direto; (c) fachada C com POD e handles inteiros; (d) coordinator iOS em ObjC++ em vez de Swift.
- **Decisão proposta:** (c). Swift importa C sem camada intermediária; JNI usa os mesmos POD; a fachada é testada em host com sanitizers.
- **Por que não as outras:** (a) duplica modelos e adiciona `.mm` sem teste em host; (b) o modo de interop é configuração de build frágil entre versões de Xcode/RN; (d) elimina o Swift que a SPEC pede e que facilita testes e cancelamento.
- **Consequências negativas:** API C é verbosa; limites fixos nos POD; expor header C ao Swift dentro de um pod precisa de prova.
- **Gatilho de revisão:** spike falhar em importar `lce.h` do Swift nas duas configurações de pod → usar (a).

### ADR-014 — Spike de integração antes do core — Proposta (D-04)

- **Decisão proposta:** Fase 0.5 com esqueleto ponta a ponta e builds release antes da Fase 1; spec Codegen mínimo desde o início.
- **Consequência negativa:** atrasa o início da lógica; parte do esqueleto será reescrita.
- **Alternativa rejeitada:** ordem da SPEC, que descobre problemas de link depois de três fases e tem o gate da Fase 4 dependendo da Fase 6.

### ADR-015 — Stream único de aquisição — Proposta (D-03)

- **Decisão proposta:** uma primitiva `startStream`/`stop` por plataforma; fases inicial e de recovery são política.
- **Consequência negativa:** abre mão de otimizações internas de `getCurrentLocation` no Android; exige medir latência para confirmar que não piora.
- **Gatilho de revisão:** medição em dispositivo mostrar primeiro fix mais lento que o one-shot.

### ADR-016 — Envelope de velocidade do fix no modelo de incerteza — Proposta (D-01)

- **Decisão proposta:** fórmula de R-01, parâmetros como hipóteses versionadas; fallback para 45 m/s sem speed válida.
- **Consequência negativa:** mais um parâmetro não calibrado (aceleração); speed reportada pelo SO pode estar errada, e speed accuracy otimista reduz a margem. Menos conservador que a SPEC.
- **Gatilho de revisão:** dados de campo mostrando deslocamento real acima do envelope.

### ADR-017 — Motion no Android adiado para v1.1 — Proposta (D-02)

- **Decisão proposta:** v1.0 entrega motion no iOS; Android reporta `motion_unavailable`; a porta `MotionSource` existe nas duas plataformas.
- **Consequência negativa:** assimetria entre plataformas na v1.0; testes PLT-013/014 com adapter real ficam como exceção documentada.

### ADR-018 — Fila serial única, definição de background e portas de tempo — Proposta

- **Decisão proposta:** uma fila serial por módulo; background = processo parado (Android) / `didEnterBackground` (iOS), não perda de foco; portas `Clock`, `Scheduler` e `LifecycleSource` injetáveis.
- **Consequência negativa:** avaliação continua rodando sob diálogos do sistema e Central de Controle, o que é uso de sensor com a UI parcialmente coberta; aceitável por ser foreground do ponto de vista do SO.
