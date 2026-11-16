# Fase 7 — app de exemplo e demonstração: relatório

Data: 16/11/2026. Branch `phase/7-example`, a partir de `phase/6-ts-api`.

## Objetivo

Um app que mostre a biblioteca funcionando e sirva de demonstração pública. Duas telas:

| Tela | O que faz |
|---|---|
| "Simulated courier" | um entregador inventado e um GPS inventado alimentam a biblioteca de verdade; seis situações tocam sob demanda |
| "This phone" | a mesma biblioteca com o GPS real do aparelho: permissão, sessão, avaliar, cancelar |

Só uma tela existe por vez. Sair do simulador desliga a simulação, então a outra tela sempre fala com o GPS real.

## Por que coordenadas inventadas

- A demonstração não expõe a localização de ninguém.
- As situações difíceis (salto de GPS, sinal perdido, GPS falso) acontecem na hora em que se pede, e do mesmo jeito toda vez.
- O que aparece no painel não é encenado: o simulador só troca de onde vêm as leituras. Quem decide o índice, a relação e os motivos é o motor, pelo mesmo caminho do GPS real. Todo relatório simulado leva `simulated_location_source`.

O ponto fixo no globo é uma praça pública em São Paulo, usado só para a conta de metros para graus.

## Ordem das chamadas no simulador

```text
toque em uma situação
  v
useScenarioPlayer.play(scenario)
  |-- simulation.enable()            o GPS real fica desligado daqui em diante
  |-- startSession()
  `-- a cada segundo:
        |-- scenario.momentAt(second)        onde o entregador está, o que o GPS "diz"
        |-- evaluateProximity(...)           1. pergunta: a avaliação fica esperando leitura
        |-- simulation.setPhoneActivity()    2. o que o celular "diz" que está fazendo
        |-- simulation.pushReadings()           e o que o GPS "entrega" (às vezes nada)
        `-- await da resposta                3. mostra: mapa e painel
```

Cada avaliação do simulador espera no máximo 0,8 s por uma leitura e não usa tempo extra, para a tela acompanhar a situação de perto.

## As seis situações

O que o motor respondeu no emulador (os números mudam um pouco a cada execução, porque as idades são tempo real):

| Situação | O que o simulador faz | O que o motor responde |
|---|---|---|
| Arriving | entregador vem de moto e estaciona a 30 m | fora (97) → inconclusivo na borda do raio → dentro (97) |
| GPS jump | uma leitura cai a 340 m | a leitura não é usada: responde com a anterior, da memória (60, `cached` e `conflicting_evidence`, motivo `suspected_gps_jump`); depois 75 enquanto a leitura estranha contradiz as novas; depois 97 |
| Signal lost | o GPS para de entregar | da memória (60); a área onde o celular pode estar cresce; passa a inconclusivo; 35 quando a área chega a 100 m e 15 quando chega a 300 m |
| Lost, standing still | igual, mas o celular informa que está parado | continua "dentro" com 60: depois de 16 s a área é de 16 m |
| Fake GPS | entregador a 150 m; leituras na porta, marcadas como mock | 0, inconclusivo, `possible_mock_location` |
| Approximate only | leituras com 2 km de erro, permissão aproximada | 15, inconclusivo, `precision_reduced` |

"Signal lost" e "Lost, standing still" lado a lado são o resumo da biblioteca: o mesmo silêncio, e saber que o celular ficou parado mantém a leitura antiga útil.

## Arquivos

| Arquivo | Papel |
|---|---|
| `example/src/App.tsx` | cabeçalho e troca de tela |
| `example/src/theme.ts` | cores e letras |
| `example/src/simulator/world.ts` | o lugar inventado: metros para graus, ruas |
| `example/src/simulator/scenarios.ts` | os roteiros das seis situações |
| `example/src/simulator/useScenarioPlayer.ts` | toca um roteiro contra a biblioteca |
| `example/src/simulator/StreetMap.tsx` | o mapa, desenhado com Views (sem serviço de mapas, sem chave, sem internet) |
| `example/src/simulator/SimulatorScreen.tsx` | a tela do simulador |
| `example/src/components/AssessmentPanel.tsx` | mostra um relatório em palavras: régua do índice, frase, fatos, motivos |
| `example/src/device/DeviceScreen.tsx` | a tela do GPS real |
| `docs/demo/` | vídeos e capturas |

Os roteiros não sabem nada de pontuação. O painel não calcula nada sobre localização: escolhe palavras para o que o relatório traz.

## Como o índice é mostrado

Sem símbolo de porcentagem, em lugar nenhum. A legenda fixa diz "Evidence quality index, 0 to 100. Not a percentage." O índice aparece em uma régua de 0 a 100; quando um limite o reduziu, um risco vermelho marca o valor anterior e a legenda nomeia os limites aplicados (por exemplo "Was 72 before the limits: cached, uncertainty_at_least_100m").

## Comandos executados

| Comando | Resultado |
|---|---|
| `npm exec --no -- tsc --noEmit` (raiz, inclui o exemplo) | exit 0 |
| `npm exec --no -- eslint src example/src` | exit 0, sem avisos |
| `./gradlew :app:assembleDebug -PreactNativeArchitectures=arm64-v8a` | exit 0 |
| `./gradlew :app:assembleRelease -PreactNativeArchitectures=arm64-v8a,x86_64` | exit 0 |

As seis situações foram tocadas no emulador (Android 17, arm64) com o build release e gravadas. A tela "This phone" foi verificada no mesmo emulador; o resultado está no relatório da fase 4.

## IDs normativos

- SCR-005: o índice é exibido sem `%` e com a legenda "Not a percentage".
- Fluxos pedidos no plano: permissão ausente (resposta `unavailable`, sem diálogo), degradado (sinal perdido), cancelamento (botão "Cancel", resposta `cancelled`).

## Não feito

- **iOS.** O exemplo é o mesmo código nas duas plataformas, mas só rodou no Android. Depende da fase 5.
- **Aparelho físico.**
- **Roteiros em arquivo JSON.** São código TypeScript (`scenarios.ts`).
- **Gravação fluida.** Os vídeos têm cerca de 10 quadros por segundo: é o que o gravador do emulador entrega. `docs/demo/README.md` diz como regravar.

## Riscos e próximo passo

- A dependência `react-native-safe-area-context` entrou só no exemplo.
- No build de desenvolvimento em Android 17 o React Native pede a permissão de rede local para falar com o Metro. O diálogo não é da biblioteca e não aparece no build release.
- Próximo: iOS (fase 5).
