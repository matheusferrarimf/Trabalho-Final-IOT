/*
 * UNIDAVI — Projeto Final IoT
 * Espécie: Araucária / Pinheiro-brasileiro (Araucaria angustifolia)
 * Checkpoint 1 — acionamento real com módulos relé
 *
 * Regras do cenário (seção 4.2):
 *   Se temperatura < 10 °C     -> aciona aquecimento (relé no GPIO 18)
 *   Se umidade do solo < 35 %  -> aciona irrigação   (relé no GPIO 19)
 * Valores iguais ao limiar mantêm a saída desligada.
 *
 * O ESP32 só aciona a bobina do relé; a carga (bomba/válvula e
 * aquecedor) fica no contato COM/NO, com fonte própria.
 */
#include <DHT.h>

// ===================== CONFIGURAÇÃO =====================
// 1 = Wokwi (potenciômetro simula o sensor de solo)
// 0 = hardware real (sensor capacitivo, usa a calibração abaixo)
#define MODO_WOKWI 1

// Nível lógico que liga o relé:
// 0 = liga com HIGH (relé padrão do Wokwi)
// 1 = liga com LOW  (maioria dos módulos reais com optoacoplador)
#define RELE_ATIVO_EM_BAIXO 0

const uint8_t PINO_DHT         = 4;
const uint8_t PINO_UMIDADE     = 34;  // ADC1: continua funcionando com Wi-Fi (Checkpoint 2)
const uint8_t PINO_AQUECIMENTO = 18;
const uint8_t PINO_IRRIGACAO   = 19;

const float LIMIAR_TEMPERATURA = 10.0;  // °C  (RNF03)
const float LIMIAR_UMIDADE     = 35.0;  // %   (RNF03)

const unsigned long INTERVALO_LEITURA_MS = 2000;  // RNF02

const uint8_t NIVEL_LIGA    = RELE_ATIVO_EM_BAIXO ? LOW  : HIGH;
const uint8_t NIVEL_DESLIGA = RELE_ATIVO_EM_BAIXO ? HIGH : LOW;

// Calibração do sensor capacitivo — medir no hardware real
// (o sensor capacitivo dá valor ALTO no seco e BAIXO no molhado)
const int ADC_SECO    = 3200;  // sensor no ar
const int ADC_MOLHADO = 1300;  // sensor mergulhado em água

// ===================== ESTADO =====================
DHT dht(PINO_DHT, DHT22);

struct Leituras {
  float temperatura;
  bool  temperaturaValida;
  float umidadeSolo;
  int   adcSolo;          // valor bruto, útil para calibrar
};

struct Estados {
  bool aquecimento;
  bool irrigacao;
};

Leituras leituras = {0, false, 0, 0};
Estados  estados  = {false, false};
unsigned long ultimaLeitura = 0;

// Modo de teste: permite forçar valores exatos (ex.: limite 10 °C / 35 %)
bool  forcarTemp = false, forcarUmid = false;
float tempForcada = 0, umidForcada = 0;

// ===================== LEITURA (RF01, RF02) =====================
float converterUmidade(int adc) {
#if MODO_WOKWI
  float pct = adc * 100.0f / 4095.0f;
#else
  float pct = (float)(ADC_SECO - adc) * 100.0f / (float)(ADC_SECO - ADC_MOLHADO);
#endif
  return constrain(pct, 0.0f, 100.0f);
}

void lerSensores() {
  float t = dht.readTemperature();
  leituras.temperaturaValida = !isnan(t);
  if (leituras.temperaturaValida) leituras.temperatura = t;

  leituras.adcSolo     = analogRead(PINO_UMIDADE);
  leituras.umidadeSolo = converterUmidade(leituras.adcSolo);

  if (forcarTemp) { leituras.temperatura = tempForcada; leituras.temperaturaValida = true; }
  if (forcarUmid) { leituras.umidadeSolo = umidForcada; }
}

// ===================== REGRAS (RF03 a RF06) =====================
void aplicarRegras() {
  // Regra 1: falha de leitura do DHT22 desliga o aquecimento (falha segura)
  estados.aquecimento = leituras.temperaturaValida &&
                        (leituras.temperatura < LIMIAR_TEMPERATURA);
  // Regra 2: independente da regra 1
  estados.irrigacao = (leituras.umidadeSolo < LIMIAR_UMIDADE);
}

// ===================== SAÍDAS (RELÉS) =====================
void atualizarSaidas() {
  digitalWrite(PINO_AQUECIMENTO, estados.aquecimento ? NIVEL_LIGA : NIVEL_DESLIGA);
  digitalWrite(PINO_IRRIGACAO,   estados.irrigacao   ? NIVEL_LIGA : NIVEL_DESLIGA);
}

// ===================== MONITOR SERIAL (RF08) =====================
void imprimirStatus() {
  if (leituras.temperaturaValida) Serial.printf("T=%5.1f °C", leituras.temperatura);
  else                            Serial.print ("T= ERRO   ");

  Serial.printf(" | Solo=%5.1f %% (ADC %4d) | Aquecimento=%-3s | Irrigacao=%-3s",
                leituras.umidadeSolo, leituras.adcSolo,
                estados.aquecimento ? "ON" : "OFF",
                estados.irrigacao   ? "ON" : "OFF");

  if (forcarTemp || forcarUmid) Serial.print("  [TESTE: valor forcado]");
  Serial.println();
}

void processarComandoSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.startsWith("T=")) {
    tempForcada = cmd.substring(2).toFloat(); forcarTemp = true;
  } else if (cmd.startsWith("U=")) {
    umidForcada = cmd.substring(2).toFloat(); forcarUmid = true;
  } else if (cmd == "AUTO") {
    forcarTemp = forcarUmid = false;
  } else {
    Serial.println("Comandos: T=<valor> | U=<valor> | AUTO (volta aos sensores)");
    return;
  }
  Serial.println("> comando aceito: " + cmd);
}

// ===================== CICLO PRINCIPAL (RF07) =====================
void setup() {
  Serial.begin(115200);
  pinMode(PINO_AQUECIMENTO, OUTPUT);
  pinMode(PINO_IRRIGACAO, OUTPUT);
  atualizarSaidas();              // desliga os relés logo em seguida (estados = false)
  analogReadResolution(12);       // 0..4095
  dht.begin();

  Serial.println("=== Araucaria — acionamento com reles (Checkpoint 1) ===");
  Serial.printf("Limiares: T < %.1f °C -> aquecimento | Solo < %.1f %% -> irrigacao\n",
                LIMIAR_TEMPERATURA, LIMIAR_UMIDADE);
  Serial.println("Comandos de teste: T=10 | U=35 | AUTO");
}

void loop() {
  processarComandoSerial();

  // Temporização sem delay(): não bloqueia o loop (RNF02)
  if (millis() - ultimaLeitura >= INTERVALO_LEITURA_MS) {
    ultimaLeitura = millis();
    lerSensores();
    aplicarRegras();
    atualizarSaidas();
    imprimirStatus();
  }
}