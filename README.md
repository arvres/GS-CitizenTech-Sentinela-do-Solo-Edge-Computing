# Sentinela do Solo — Estaca Inteligente (Edge Computing)

Global Solution 2026.2 · CitizenTech · FIAP · Engenharia de Software 1º ano
Disciplina: **Edge Computing & Computer Systems**

**Integrantes:**

- Anderson Marcolino — RM 574066
- Caio Maluza — RM 572906
- Felipe Luan — RM 569565
- Pedro Henrique Alves — RM 569192
- Rafaella Pazzanese - RM 572296

**Links:**
- Simulação Wokwi: [](https://wokwi.com/projects/480861247681298433)
- Vídeo (YouTube): link do vídeo

---

## 1. Descrição do projeto

Na região metropolitana de São Paulo, deslizamentos de terra durante tempestades matam famílias que vivem em encostas. O solo cede em silêncio, a Defesa Civil não consegue vigiar cada barranco e o aviso ao morador chega tarde ou gera pânico (ODS 11 e ODS 13).

O **Sentinela do Solo** usa uma *estaca inteligente* cravada nos barrancos mais críticos. Este projeto é o **dispositivo de borda (Edge)** dessa estaca: um ESP32 que lê a inclinação do terreno e a chuva, **decide sozinho, mesmo sem internet**, e avisa no local com LEDs, buzzer e display. Quando há conexão, envia telemetria em JSON para um back-end via MQTT.

## 2. Arquitetura em três camadas

| Camada | O que é | Papel |
|---|---|---|
| 1. Dispositivo IoT | ESP32 no Wokwi | Lê, filtra, calcula e decide localmente (funciona offline) |
| 2. Back-end | Broker MQTT público (HiveMQ) | Recebe o JSON de telemetria |
| 3. Dashboard/App | React do projeto (Sentinela do Solo) | Consome os dados e mostra o estado ao morador, à Defesa Civil e à empresa |

Telemetria publicada no tópico `gs2026/sentinela-solo/<grupo>/telemetria`:

```json
{"id_estaca":"EST-01","cluster_id":"CL-ARGISSOLO-01","angulo_theta":12.34,
 "taxa":0.450,"chuva_mm_h":10.0,"chuva_acum_mm":35.2,
 "umidade_solo":44.0,"estado":1,"timestamp":"2026-11-18T10:00:00-03:00"}
```

## 3. Componentes utilizados

| Componente | Pino ESP32 | Função |
|---|---|---|
| MPU6050 | SDA 21 / SCL 22 (I2C 0x68) | Mede a inclinação θ do terreno |
| Potenciômetro | GPIO 34 | Simula a intensidade da chuva (0–50 mm/h) |
| LED verde + 220 Ω | GPIO 25 | Estado 1 (solo estável) |
| LED amarelo + 220 Ω | GPIO 26 | Estado 2 (atenção) e estado 0 (erro, piscando) |
| LED vermelho + 220 Ω | GPIO 27 | Estado 3 (perigo) |
| Buzzer | GPIO 14 | Bip curto (amarelo) e sirene contínua (vermelho) |
| Display OLED SSD1306 | SDA 21 / SCL 22 (I2C 0x3C) | Mostra θ, taxa, chuva e estado |
| Botão | GPIO 13 (INPUT_PULLUP) | Simula falha do sensor para testar o estado 0 |

## 4. Lógica de funcionamento

Valores de referência (iguais em todas as disciplinas do projeto):

- Taxa crítica de ruptura: **2,0 °/h**
- Margem pessimista (40%): **0,8 °/h**
- Chuva de saturação: **80 mm em 24 h**

Fluxo a cada leitura (1 s), sem `delay()`, usando `millis()`:

1. **Leitura**: θ = arctan(ax / √(ay² + az²)) a partir do MPU6050; chuva pelo potenciômetro.
2. **Validação**: NaN, θ fora de ±90°, I2C sem resposta ou falha simulada → **estado 0**.
3. **Média móvel** (5 amostras) para descartar ruído (vibração, vento).
4. **Derivada**: dθ/dt = (θ_atual − θ_anterior) / Δt, em °/h.
5. **Integral da chuva**: regra do trapézio numa janela de 24 h simuladas.
6. **Classificação**:

| Estado | Condição | Resposta local |
|---|---|---|
| 0 Erro | leitura inválida | LED amarelo piscando, sem som, mensagem no Serial |
| 1 Verde | taxa < 0,8 °/h e chuva < 80 mm | LED verde aceso |
| 2 Amarelo | taxa ≥ 0,8 °/h **ou** chuva ≥ 80 mm | LED amarelo + bip curto a cada 1 s |
| 3 Vermelho | taxa ≥ 2,0 °/h | LED vermelho + sirene contínua + display invertido |

7. **Retorno seguro**: depois de um vermelho, quando |dθ/dt| fica abaixo de 0,1 °/h por 3 ciclos seguidos (o limite de dθ/dt → 0), o display indica que a taxa tende a zero. A liberação oficial do retorno continua sendo uma decisão humana da Defesa Civil.

> **Aceleração de tempo:** na vida real a taxa é em °/h. Na simulação, **1 s real = 1 h simulada** (`HORAS_SIM_POR_SEGUNDO = 1.0`). Assim, mudar θ em 1° por segundo equivale a 1 °/h.

Funções principais: `lerSensores()`, `suavizar()`, `calcularTaxa()`, `atualizarChuva()`, `classificarEstado()`, `atuar()`, `enviarTelemetria()`.

## 5. Como executar a simulação

1. Abra o link do Wokwi (ou crie um projeto **ESP32** e cole `sketch.ino`, `diagram.json` e `libraries.txt`).
2. Clique em **Start Simulation** e abra o **Serial Monitor**.
3. Roteiro de teste:
   - **Verde**: deixe o MPU6050 parado e o potenciômetro em zero.
   - **Amarelo (chuva)**: gire o potenciômetro para o máximo e aguarde ~2 s (≈ 80 mm acumulados).
   - **Amarelo (taxa)**: clique no MPU6050 e aumente a aceleração X aos poucos, o suficiente para a taxa passar de 0,8 °/h.
   - **Vermelho**: aumente a aceleração X mais rápido até a taxa passar de 2,0 °/h.
   - **Retorno seguro**: pare de mexer no MPU6050; a taxa cai para ~0 e o display indica retorno seguro. Volte o potenciômetro a zero para a chuva acumulada sair da janela de 24 s.
   - **Erro (estado 0)**: segure o botão vermelho; o LED amarelo pisca sem alarme falso.
4. Telemetria: abra o [HiveMQ Web Client](https://www.hivemq.com/demos/websocket-client/), conecte e assine o tópico `gs2026/sentinela-solo/<grupo>/telemetria`.

## 6. Observações

- O broker HiveMQ é público: use um nome de tópico exclusivo do grupo.
- Se o WiFi/MQTT falhar, o dispositivo continua decidindo e acionando os alertas localmente (RNF01).
- O envio de telemetria é não bloqueante e não interfere na decisão local.
