/*
 * ============================================================
 *  SENTINELA DO SOLO - Estaca Inteligente (Edge Computing)
 *  Global Solution 2026.2 - CitizenTech - FIAP
 *  Disciplina: Edge Computing & Computer Systems
 * ------------------------------------------------------------
 *  INTEGRANTES (nome completo e RM):
 *   - Anderson Marcolino — RM 574066
 *   - Caio Maluza — RM 572906
 *   - Felipe Luan — RM 569565
 *   - Pedro Henrique Alves — RM 569192
 *   - Rafaella Pazzanese - RM 572296
 * ------------------------------------------------------------
 *  O que o dispositivo faz:
 *   1. Le a inclinacao do terreno (MPU6050) e a chuva (potenciometro).
 *   2. Filtra ruido com media movel.
 *   3. Calcula a velocidade de movimentacao do solo: dTheta/dt (graus/hora).
 *   4. Integra a chuva (regra do trapezio) numa janela de 24 h simuladas.
 *   5. Decide LOCALMENTE (sem internet) o estado e aciona LEDs, buzzer e OLED.
 *   6. Quando ha conexao, publica telemetria em JSON via MQTT.
 *
 *  Estados:
 *   0 = ERRO de leitura   (LED amarelo piscando, sem alarme falso)
 *   1 = VERDE             (solo estavel)
 *   2 = AMARELO           (taxa >= 40% da critica OU solo saturado de chuva)
 *   3 = VERMELHO          (taxa >= taxa critica, sirene continua)
 *
 *  Valores de referencia (iguais em TODAS as disciplinas do projeto):
 *   taxa critica = 2,0 graus/h | margem pessimista = 40% (0,8 graus/h)
 *   chuva de saturacao = 80 mm em 24 h
 *
 *  ACELERACAO DE TEMPO (importante para a demonstracao):
 *   Na vida real a taxa e medida em graus por HORA. Na simulacao
 *   seria impossivel esperar horas, entao 1 segundo real = 1 hora
 *   simulada (HORAS_SIM_POR_SEGUNDO). Ajuste se quiser.
 * ============================================================
 */

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <time.h>

// ---------------------- PINOS ----------------------
const int PIN_SDA       = 21;   // I2C: MPU6050 e OLED
const int PIN_SCL       = 22;
const int PIN_CHUVA     = 34;   // potenciometro = intensidade da chuva
const int PIN_LED_VERDE = 25;
const int PIN_LED_AMAR  = 26;
const int PIN_LED_VERM  = 27;
const int PIN_BUZZER    = 14;
const int PIN_FALHA     = 13;   // botao: segurar = simula falha do sensor

// ---------------------- I2C ----------------------
const int MPU_ADDR  = 0x68;
const int OLED_ADDR = 0x3C;

// ---------------------- PARAMETROS DO MODELO ----------------------
const float TAXA_CRITICA        = 2.0f;                  // graus/h
const float MARGEM              = 0.4f;                  // 40% (margem pessimista)
const float TAXA_AMARELO        = TAXA_CRITICA * MARGEM; // 0,8 graus/h
const float CHUVA_SATURACAO_MM  = 80.0f;                 // mm em 24 h
const float CHUVA_MAX_MM_H      = 50.0f;                 // fundo de escala do potenciometro
const float TAXA_ESTAVEL        = 0.1f;                  // |taxa| abaixo disso = "tendendo a zero"
const int   LEITURAS_ESTAVEIS   = 3;                     // ciclos seguidos para confirmar estabilidade
const int   N_MEDIA             = 5;                     // tamanho da media movel
const int   JANELA_CHUVA        = 24;                    // 24 leituras = 24 h simuladas
const float HORAS_SIM_POR_SEGUNDO = 1.0f;                // 1 s real = 1 h simulada

// ---------------------- TEMPOS (millis, sem delay) ----------------------
const unsigned long INTERVALO_LEITURA_MS    = 1000;
const unsigned long INTERVALO_TELEMETRIA_MS = 5000;
const unsigned long PISCA_ERRO_MS           = 300;

// ---------------------- REDE / MQTT ----------------------
const char* WIFI_SSID   = "Wokwi-GUEST";
const char* WIFI_PASS   = "";
const int   WIFI_CANAL  = 6;
const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORTA  = 1883;
//                                                Esse é o caminho para receber os dados pelo HiveMQ
const char* MQTT_TOPICO = "gs2026/sentinela-solo/GS-DigitalRequiem/telemetria";
const char* ID_ESTACA   = "EST-01";
const char* CLUSTER_ID  = "CL-ARGISSOLO-01";

// ---------------------- OBJETOS ----------------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ---------------------- ESTADO GLOBAL ----------------------
float thetaBuf[N_MEDIA];
int   nBuf = 0, iBuf = 0;
float thetaSuav = 0, thetaSuavAnt = 0;
bool  temAnterior = false;

float taxa = 0;                 // dTheta/dt em graus/h
float chuvaMmH = 0, chuvaAnt = 0;
float chuvaAcum = 0;            // mm acumulados na janela (integral)
float janelaChuva[JANELA_CHUVA];
int   iJanela = 0;

int   estado = 1;
bool  oledOk = false;
bool  houveVermelho = false;
bool  retornoSeguro = false;
int   contEstavel = 0, cicloRetorno = 0;

unsigned long ultimaLeitura = 0, ultimaTelemetria = 0;
unsigned long ultimaTentativaWifi = 0, ultimaTentativaMqtt = 0;
int somAtual = 0;

// ============================================================
//  SENSORES
// ============================================================

// Le o acelerometro do MPU6050 e calcula theta em graus:
// theta = arctan( ax / sqrt(ay^2 + az^2) )
bool lerMPU(float &theta) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);                         // registrador do ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;   // I2C nao respondeu
  if (Wire.requestFrom(MPU_ADDR, 6, true) != 6) return false;

  int16_t rx = (Wire.read() << 8) | Wire.read();
  int16_t ry = (Wire.read() << 8) | Wire.read();
  int16_t rz = (Wire.read() << 8) | Wire.read();

  float ax = rx / 16384.0f;                 // escala +-2g
  float ay = ry / 16384.0f;
  float az = rz / 16384.0f;

  theta = atan2(ax, sqrt(ay * ay + az * az)) * 180.0f / PI;
  return true;
}

// Retorna false se QUALQUER leitura for invalida -> estado 0
bool lerSensores(float &theta, float &chuva) {
  if (digitalRead(PIN_FALHA) == LOW) return false;      // falha simulada pelo botao
  if (!lerMPU(theta)) return false;                      // I2C sem resposta
  if (isnan(theta) || theta < -90.0f || theta > 90.0f) return false; // fora da escala fisica

  int raw = analogRead(PIN_CHUVA);
  if (raw < 0 || raw > 4095) return false;
  chuva = raw * CHUVA_MAX_MM_H / 4095.0f;                // mm/h
  return true;
}

// ============================================================
//  PROCESSAMENTO (EDGE)
// ============================================================

// Media movel: descarta ruido (vibracao, vento, etc.)
float suavizar(float t) {
  thetaBuf[iBuf] = t;
  iBuf = (iBuf + 1) % N_MEDIA;
  if (nBuf < N_MEDIA) nBuf++;
  float soma = 0;
  for (int i = 0; i < nBuf; i++) soma += thetaBuf[i];
  return soma / nBuf;
}

// Derivada numerica: (theta_atual - theta_anterior) / delta_t  [graus/h]
float calcularTaxa(float thetaAtual, float thetaAntes, float dtHoras) {
  return (thetaAtual - thetaAntes) / dtHoras;
}

// Integral da chuva (regra do trapezio) numa janela deslizante de 24 h
void atualizarChuva(float chuva, float dtHoras) {
  float incremento = ((chuva + chuvaAnt) / 2.0f) * dtHoras;   // mm neste intervalo
  janelaChuva[iJanela] = incremento;
  iJanela = (iJanela + 1) % JANELA_CHUVA;
  chuvaAnt = chuva;

  float soma = 0;
  for (int i = 0; i < JANELA_CHUVA; i++) soma += janelaChuva[i];
  chuvaAcum = soma;
}

// Decide o estado. Mesma regra do Python e do React.
int classificarEstado(bool leituraValida, float taxaAbs, float chuvaMm) {
  if (!leituraValida)                                   return 0;
  if (taxaAbs >= TAXA_CRITICA)                          return 3;
  if (taxaAbs >= TAXA_AMARELO || chuvaMm >= CHUVA_SATURACAO_MM) return 2;
  return 1;
}

// ============================================================
//  ATUADORES
// ============================================================

void setSom(int freq) {                // evita reiniciar o tom a cada loop
  if (freq == somAtual) return;
  if (freq > 0) tone(PIN_BUZZER, freq); else noTone(PIN_BUZZER);
  somAtual = freq;
}

void atuar(int est) {
  unsigned long t = millis();
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_AMAR, LOW);
  digitalWrite(PIN_LED_VERM, LOW);

  switch (est) {
    case 0:  // ERRO: amarelo piscando, sem som (nenhum alarme falso)
      digitalWrite(PIN_LED_AMAR, (t / PISCA_ERRO_MS) % 2 == 0);
      setSom(0);
      break;
    case 1:  // VERDE
      digitalWrite(PIN_LED_VERDE, HIGH);
      setSom(0);
      break;
    case 2:  // AMARELO: bip curto intermitente (150 ms a cada 1 s)
      digitalWrite(PIN_LED_AMAR, HIGH);
      setSom((t % 1000) < 150 ? 1000 : 0);
      break;
    case 3:  // VERMELHO: sirene continua
      digitalWrite(PIN_LED_VERM, HIGH);
      setSom(2000);
      break;
  }
}

void atualizarDisplay() {
  if (!oledOk) return;
  display.clearDisplay();
  display.invertDisplay(estado == 3);   // vermelho: tela invertida em destaque
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);  display.print("SENTINELA DO SOLO");
  display.setCursor(0, 11); display.printf("Theta: %.1f graus", thetaSuav);
  display.setCursor(0, 21); display.printf("Taxa: %.2f gr/h", taxa);
  display.setCursor(0, 31); display.printf("Chuva24h: %.1f mm", chuvaAcum);

  display.setTextSize(2);
  display.setCursor(0, 42);
  const char* nomes[] = {"ERRO", "VERDE", "AMARELO", "VERMELHO"};
  display.print(nomes[estado]);

  display.setTextSize(1);
  display.setCursor(0, 57);
  if (retornoSeguro && estado != 3) display.print("Taxa ~0: retorno seguro");
  display.display();
}

// ============================================================
//  REDE (nunca bloqueia a decisao local)
// ============================================================

void gerenciarRede() {
  unsigned long t = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (t - ultimaTentativaWifi > 15000) {
      ultimaTentativaWifi = t;
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
    }
    return;                                    // sem rede: a estaca segue decidindo sozinha
  }
  if (!mqtt.connected()) {
    if (t - ultimaTentativaMqtt > 10000) {
      ultimaTentativaMqtt = t;
      String cid = String("sentinela-") + ID_ESTACA + "-" + String((uint32_t)esp_random(), HEX);
      mqtt.connect(cid.c_str());
    }
    return;
  }
  mqtt.loop();
}

void montarTimestamp(char *buf, size_t n) {
  time_t agora = time(nullptr);
  if (agora > 1700000000) {                    // NTP sincronizado
    struct tm info;
    localtime_r(&agora, &info);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%S-03:00", &info);
  } else {
    snprintf(buf, n, "uptime-%lus", millis() / 1000);
  }
}

void enviarTelemetria() {
  if (!mqtt.connected()) return;               // offline: simplesmente nao envia
  float umidade = constrain(chuvaAcum / CHUVA_SATURACAO_MM * 100.0f, 0.0f, 100.0f);
  char ts[32];
  montarTimestamp(ts, sizeof(ts));

  char json[320];
  snprintf(json, sizeof(json),
    "{\"id_estaca\":\"%s\",\"cluster_id\":\"%s\",\"angulo_theta\":%.2f,"
    "\"taxa\":%.3f,\"chuva_mm_h\":%.1f,\"chuva_acum_mm\":%.1f,"
    "\"umidade_solo\":%.1f,\"estado\":%d,\"timestamp\":\"%s\"}",
    ID_ESTACA, CLUSTER_ID, thetaSuav, taxa, chuvaMmH, chuvaAcum, umidade, estado, ts);

  mqtt.publish(MQTT_TOPICO, json);
  Serial.print("[MQTT] "); Serial.println(json);
}

// ============================================================
//  CICLO DE LEITURA
// ============================================================

void cicloLeitura(unsigned long agora) {
  float dtH = ((agora - ultimaLeitura) / 1000.0f) * HORAS_SIM_POR_SEGUNDO;
  ultimaLeitura = agora;

  float theta = 0, chuva = 0;
  bool ok = lerSensores(theta, chuva);

  if (ok) {
    thetaSuav = suavizar(theta);
    taxa = temAnterior ? calcularTaxa(thetaSuav, thetaSuavAnt, dtH) : 0;
    thetaSuavAnt = thetaSuav;
    temAnterior = true;
    chuvaMmH = chuva;
    atualizarChuva(chuva, dtH);
  }

  estado = classificarEstado(ok, fabsf(taxa), chuvaAcum);

  // Retorno seguro: |dTheta/dt| tende a zero depois de um vermelho
  if (estado == 3) houveVermelho = true;
  if (ok && fabsf(taxa) < TAXA_ESTAVEL) contEstavel++; else contEstavel = 0;
  retornoSeguro = houveVermelho && contEstavel >= LEITURAS_ESTAVEIS;
  if (retornoSeguro && ++cicloRetorno >= 5) { houveVermelho = false; cicloRetorno = 0; }
  if (!retornoSeguro) cicloRetorno = 0;

  const char* nomes[] = {"ERRO", "VERDE", "AMARELO", "VERMELHO"};
  if (ok) {
    Serial.printf("theta=%.2f gr | taxa=%.3f gr/h | chuva=%.1f mm/h | acum24h=%.1f mm | estado=%d (%s)%s\n",
                  thetaSuav, taxa, chuvaMmH, chuvaAcum, estado, nomes[estado],
                  retornoSeguro ? " | RETORNO SEGURO" : "");
  } else {
    Serial.println("[ERRO] leitura invalida -> estado 0 (sem alarme falso)");
  }
  atualizarDisplay();
}

// ============================================================
//  SETUP / LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMAR, OUTPUT);
  pinMode(PIN_LED_VERM, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_FALHA, INPUT_PULLUP);

  for (int i = 0; i < JANELA_CHUVA; i++) janelaChuva[i] = 0;

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.beginTransmission(MPU_ADDR);       // acorda o MPU6050
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);

  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oledOk) Serial.println("[AVISO] OLED nao encontrado");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
  configTime(-3 * 3600, 0, "pool.ntp.org");
  mqtt.setServer(MQTT_BROKER, MQTT_PORTA);
  mqtt.setBufferSize(384);
  mqtt.setSocketTimeout(2);

  Serial.println("Sentinela do Solo - estaca inteligente iniciada");
  Serial.printf("Taxa critica=%.1f gr/h | amarelo a partir de %.1f gr/h | saturacao=%.0f mm\n",
                TAXA_CRITICA, TAXA_AMARELO, CHUVA_SATURACAO_MM);
}

void loop() {
  unsigned long agora = millis();

  if (agora - ultimaLeitura >= INTERVALO_LEITURA_MS) cicloLeitura(agora);

  atuar(estado);          // LEDs e buzzer rodam todo loop (piscar/bipar sem delay)
  gerenciarRede();

  if (agora - ultimaTelemetria >= INTERVALO_TELEMETRIA_MS) {
    ultimaTelemetria = agora;
    enviarTelemetria();
  }
}
