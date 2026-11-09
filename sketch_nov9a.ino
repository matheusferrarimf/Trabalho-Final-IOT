/*
 * UNIDAVI - Projeto Final IoT
 * Mega 2560 WiFi R3 + ESP8266 com firmware AT (ESP8266 NAO precisa ser regravado)
 *
 * Araucaria: T < 10 C -> aquecedor; umidade do solo < 35% -> bomba
 * Grandiuva: umidade do solo < 30% -> bomba; luminosidade > 90% -> sombreamento
 *
 * PINOS
 *   DHT22 ........ D28        Botao ........ D30 (entre o pino e o GND)
 *   Solo ......... A15        LED aquecedor  D22
 *   LDR .......... A14        LED bomba .... D24
 *                             LED sombra ... D26
 *   LCD 16x2 (paralelo, 4 bits): RS D12 | E D11 | D4 D5 | D5 D4 | D6 D3 | D7 D2
 *   (RW, K e V0 no GND/contraste; A no 5V com 220R; D0-D3 sem ligacao)
 *
 * Botao: 3 cliques em 2 segundos alternam a planta.
 *
 * CALIBRACAO (Monitor Serial, 115200, "Nova linha"):
 *   SECO / MOLHADO  -> solo       ESCURO / CLARO -> LDR
 *   Os valores ficam salvos na EEPROM (nao precisa refazer ao reiniciar).
 *   LIMPAR apaga a calibracao salva.
 */
#include <DHT.h>
#include <WiFiEspAT.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>   // ja vem com a Arduino IDE
#include <math.h>
#include <string.h>

// ---------- Pinos ----------
const uint8_t PINO_DHT = 28, PINO_SOLO = A15, PINO_LDR = A14, PINO_BOTAO = 30;
const uint8_t LED_AQUECEDOR = 22, LED_BOMBA = 24, LED_SOMBREAMENTO = 26;
const uint8_t LCD_RS = 32, LCD_E = 34, LCD_D4 = 36, LCD_D5 = 38, LCD_D6 = 40, LCD_D7 = 42;

// ---------- Regras do cenario ----------
const float LIMIAR_T = 10.0f, LIMIAR_ARAUCARIA = 35.0f;
const float LIMIAR_GRANDIUVA = 30.0f, LIMIAR_LUZ = 90.0f;

// ---------- Tempos ----------
const unsigned long LEITURA_MS = 2000UL, PUBLICACAO_MS = 10000UL;
const unsigned long DEBOUNCE_MS = 40UL, TRIPLO_CLIQUE_MS = 2000UL;
const unsigned long RETENTATIVA_REDE_MS = 15000UL, RETENTATIVA_MQTT_MS = 5000UL;
const unsigned long TROCA_PAGINA_MS = 3000UL;   // tempo de cada tela do LCD

// ---------- Rede (preencher com os dados reais) ----------
const char WIFI_SSID[] = "Redmi Note 14 Pro 5G";
const char WIFI_SENHA[] = "sofyafofo";
const char MQTT_HOST[] = "10.144.159.104"; // EXEMPLO: IP do PC com o broker, NAO do Mega
const uint16_t MQTT_PORTA = 1883;
const char MQTT_USUARIO[] = "";           // vazio caso sem autenticacao
const char MQTT_SENHA[] = "";
const char MQTT_CLIENT_ID[] = "unidavi-mega-plantas-01";
const char MQTT_TOPICO[] = "unidavi/iot/plantas/telemetria";

DHT dht(PINO_DHT, DHT22);
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
uint8_t paginaDisplay = 0;               // 0 = leituras, 1 = atuadores, 2 = rede
unsigned long ultimaTrocaPagina = 0;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

enum Planta { ARAUCARIA, GRANDIUVA };
Planta plantaAtiva = ARAUCARIA;

// ---------- Calibracao (salva na EEPROM) ----------
const uint16_t CAL_MAGIC = 0xA5C3;
struct Calibracao {
  uint16_t magic = CAL_MAGIC;
  int seco = -1, molhado = -1;   // solo
  int escuro = -1, claro = -1;   // LDR
} cal;

struct Leituras {
  float temperatura = NAN;
  bool temperaturaValida = false;
  int adcSolo = 0;
  float umidadeSolo = 0;
  bool soloValido = false;
  int adcLdr = 0;
  float luminosidade = 0;
  bool luzValida = false;
} leituras;

struct Estados {
  bool aquecedor = false;
  bool bomba = false;
  bool sombreamento = false;
} estados;

// Valores forcados pelo Serial (para demonstracao)
bool forcarTemp = false, forcarSolo = false, forcarLuz = false;
float tempForcada = 0, soloForcado = 0, luzForcada = 0;

unsigned long ultimaLeitura = 0, ultimaPublicacao = 0;
unsigned long ultimaTentativaWiFi = 0, ultimaTentativaMQTT = 0;
bool interfaceWiFiOK = false;

bool leituraBotaoAnterior = HIGH, estadoBotaoEstavel = HIGH;
unsigned long ultimaMudancaBotao = 0, primeiroClique = 0;
uint8_t cliques = 0;

// ---------- Utilidades ----------
const char* nomePlanta() { return plantaAtiva == ARAUCARIA ? "Araucaria" : "Grandiuva"; }
float limiarSolo() { return plantaAtiva == ARAUCARIA ? LIMIAR_ARAUCARIA : LIMIAR_GRANDIUVA; }

int lerADC(uint8_t pino) {
  analogRead(pino); // descarta a primeira leitura apos trocar de canal
  long soma = 0;
  for (uint8_t i = 0; i < 10; i++) soma += analogRead(pino);
  return (int)(soma / 10);
}

// Converte ADC em 0-100%. Funciona com a escala invertida (seco > molhado).
float porcentagem(int adc, int zero, int cem) {
  return constrain((adc - zero) * 100.0f / (cem - zero), 0.0f, 100.0f);
}

bool pontosValidos(int a, int b) { return a >= 0 && b >= 0 && abs(a - b) >= 20; }

// ---------- EEPROM ----------
void carregarCalibracao() {
  EEPROM.get(0, cal);
  if (cal.magic != CAL_MAGIC) cal = Calibracao(); // EEPROM vazia: comeca sem calibracao
}

void salvarCalibracao() { EEPROM.put(0, cal); }

// ---------- Sensores e regras ----------
void lerSensores() {
  float t = dht.readTemperature();
  leituras.temperaturaValida = !isnan(t);
  if (leituras.temperaturaValida) leituras.temperatura = t;

  leituras.adcSolo = lerADC(PINO_SOLO);
  leituras.soloValido = pontosValidos(cal.seco, cal.molhado);
  if (leituras.soloValido)
    leituras.umidadeSolo = porcentagem(leituras.adcSolo, cal.seco, cal.molhado);

  leituras.adcLdr = lerADC(PINO_LDR);
  leituras.luzValida = pontosValidos(cal.escuro, cal.claro);
  if (leituras.luzValida)
    leituras.luminosidade = porcentagem(leituras.adcLdr, cal.escuro, cal.claro);

  if (forcarTemp) { leituras.temperatura = tempForcada; leituras.temperaturaValida = true; }
  if (forcarSolo) { leituras.umidadeSolo = soloForcado; leituras.soloValido = true; }
  if (forcarLuz)  { leituras.luminosidade = luzForcada; leituras.luzValida = true; }
}

void aplicarRegras() {
  estados.aquecedor = plantaAtiva == ARAUCARIA && leituras.temperaturaValida &&
                      leituras.temperatura < LIMIAR_T;
  estados.bomba = leituras.soloValido && leituras.umidadeSolo < limiarSolo();
  estados.sombreamento = plantaAtiva == GRANDIUVA && leituras.luzValida &&
                         leituras.luminosidade > LIMIAR_LUZ;
}

void atualizarSaidas() {
  digitalWrite(LED_AQUECEDOR, estados.aquecedor ? HIGH : LOW);
  digitalWrite(LED_BOMBA, estados.bomba ? HIGH : LOW);
  digitalWrite(LED_SOMBREAMENTO, estados.sombreamento ? HIGH : LOW);
}

void imprimirStatus() {
  Serial.println(F("\n========= MONITORAMENTO ========="));
  Serial.print(F("Planta: ")); Serial.println(nomePlanta());

  Serial.print(F("Temperatura: "));
  if (leituras.temperaturaValida) { Serial.print(leituras.temperatura, 1); Serial.println(F(" C")); }
  else Serial.println(F("ERRO"));

  Serial.print(F("Solo ADC: ")); Serial.println(leituras.adcSolo);
  Serial.print(F("Umidade do solo: "));
  if (leituras.soloValido) { Serial.print(leituras.umidadeSolo, 1); Serial.println(F("%")); }
  else Serial.println(F("NAO CALIBRADO (use SECO e MOLHADO)"));
  Serial.print(F("Limiar solo: ")); Serial.print(limiarSolo(), 1); Serial.println('%');

  Serial.print(F("LDR ADC: ")); Serial.println(leituras.adcLdr);
  Serial.print(F("Luminosidade: "));
  if (plantaAtiva == ARAUCARIA) Serial.println(F("NAO APLICAVEL"));
  else if (!leituras.luzValida) Serial.println(F("NAO CALIBRADO (use ESCURO e CLARO)"));
  else { Serial.print(leituras.luminosidade, 1); Serial.println('%'); }

  Serial.print(F("Aquecedor: ")); Serial.println(estados.aquecedor ? F("ON") : F("OFF"));
  Serial.print(F("Bomba: ")); Serial.println(estados.bomba ? F("ON") : F("OFF"));
  Serial.print(F("Sombra: ")); Serial.println(estados.sombreamento ? F("ON") : F("OFF"));
  Serial.print(F("WiFi: "));
  Serial.println(interfaceWiFiOK && WiFi.status() == WL_CONNECTED ? F("CONECTADO") : F("DESCONECTADO"));
  Serial.print(F("MQTT: ")); Serial.println(mqtt.connected() ? F("CONECTADO") : F("DESCONECTADO"));
}

// ---------- Display LCD 16x2 ----------
// Escreve uma linha completa (16 colunas), apagando o que sobrou da anterior.
void escreverLinha(uint8_t linha, const char* texto) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texto);
  lcd.setCursor(0, linha);
  lcd.print(buf);
}

void formatarPct(char* dest, size_t n, bool valido, float v) {
  if (valido) snprintf(dest, n, "%d%%", (int)(v + 0.5f));
  else strcpy(dest, "--");
}

void atualizarDisplay() {
  char l1[24], l2[24], a[8], b[8], t[8];

  switch (paginaDisplay) {
    case 0:  // planta, temperatura, solo e luz
      if (leituras.temperaturaValida) dtostrf(leituras.temperatura, 4, 1, t);
      else strcpy(t, "ERR");
      snprintf(l1, sizeof(l1), "%-9s %sC", nomePlanta(), t);

      formatarPct(a, sizeof(a), leituras.soloValido, leituras.umidadeSolo);
      if (plantaAtiva == ARAUCARIA) strcpy(b, "N/A");
      else formatarPct(b, sizeof(b), leituras.luzValida, leituras.luminosidade);
      snprintf(l2, sizeof(l2), "S:%s L:%s", a, b);
      break;

    case 1:  // atuadores e limiar do solo
      snprintf(l1, sizeof(l1), "Aq:%s Bomba:%s",
               estados.aquecedor ? "ON" : "OFF", estados.bomba ? "ON" : "OFF");
      snprintf(l2, sizeof(l2), "Som:%s Lim:%d%%",
               estados.sombreamento ? "ON" : "OFF", (int)limiarSolo());
      break;

    default: {  // rede
      bool wifiOk = interfaceWiFiOK && WiFi.status() == WL_CONNECTED;
      snprintf(l1, sizeof(l1), "WiFi: %s", wifiOk ? "OK" : "OFF");
      snprintf(l2, sizeof(l2), "MQTT: %s", mqtt.connected() ? "OK" : "OFF");
      break;
    }
  }
  escreverLinha(0, l1);
  escreverLinha(1, l2);
}

void atualizarTudo() {
  lerSensores();
  aplicarRegras();
  atualizarSaidas();
  imprimirStatus();
  atualizarDisplay();
  ultimaLeitura = millis();
}

// ---------- Botao ----------
void trocarPlanta() {
  plantaAtiva = plantaAtiva == ARAUCARIA ? GRANDIUVA : ARAUCARIA;
  Serial.print(F("\n>>> PLANTA ALTERADA: ")); Serial.println(nomePlanta());
  paginaDisplay = 0;                 // mostra a nova planta no LCD na hora
  ultimaTrocaPagina = millis();
  atualizarTudo();
}

void verificarBotao() {
  unsigned long agora = millis();
  bool leitura = digitalRead(PINO_BOTAO);
  if (leitura != leituraBotaoAnterior) ultimaMudancaBotao = agora;
  leituraBotaoAnterior = leitura;

  if (agora - ultimaMudancaBotao >= DEBOUNCE_MS && leitura != estadoBotaoEstavel) {
    estadoBotaoEstavel = leitura;
    if (estadoBotaoEstavel == LOW) {
      if (!cliques || agora - primeiroClique > TRIPLO_CLIQUE_MS) {
        cliques = 0;
        primeiroClique = agora;
      }
      cliques++;
      Serial.print(F("Clique: ")); Serial.println(cliques);
      if (cliques == 3) { cliques = 0; trocarPlanta(); }
    }
  }
  if (cliques && agora - primeiroClique > TRIPLO_CLIQUE_MS) cliques = 0;
}

// ---------- Calibracao ----------
void calibrarSolo(bool seco) {
  int valor = lerADC(PINO_SOLO);
  if (seco) cal.seco = valor; else cal.molhado = valor;
  salvarCalibracao();
  Serial.print(seco ? F("ADC SECO: ") : F("ADC MOLHADO: ")); Serial.println(valor);
  Serial.println(pontosValidos(cal.seco, cal.molhado)
                 ? F("Solo calibrado e salvo")
                 : F("Falta o outro ponto ou diferenca ADC < 20"));
}

void calibrarLuz(bool claro) {
  int valor = lerADC(PINO_LDR);
  if (claro) cal.claro = valor; else cal.escuro = valor;
  salvarCalibracao();
  Serial.print(claro ? F("ADC CLARO: ") : F("ADC ESCURO: ")); Serial.println(valor);
  Serial.println(pontosValidos(cal.escuro, cal.claro)
                 ? F("Luz calibrada e salva")
                 : F("Falta o outro ponto ou diferenca ADC < 20"));
}

// ---------- Comandos Serial ----------
void processarSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "SECO") calibrarSolo(true);
  else if (cmd == "MOLHADO") calibrarSolo(false);
  else if (cmd == "ESCURO") calibrarLuz(false);
  else if (cmd == "CLARO") calibrarLuz(true);
  else if (cmd == "LIMPAR") { cal = Calibracao(); salvarCalibracao(); Serial.println(F("Calibracao apagada")); }
  else if (cmd == "AUTO") { forcarTemp = forcarSolo = forcarLuz = false; }
  else if (cmd.startsWith("T=")) { tempForcada = cmd.substring(2).toFloat(); forcarTemp = true; }
  else if (cmd.startsWith("U=")) { soloForcado = constrain(cmd.substring(2).toFloat(), 0.0f, 100.0f); forcarSolo = true; }
  else if (cmd.startsWith("L=")) { luzForcada = constrain(cmd.substring(2).toFloat(), 0.0f, 100.0f); forcarLuz = true; }
  else if (cmd == "PLANTA") { trocarPlanta(); return; }
  else {
    Serial.println(F("Comandos: SECO MOLHADO ESCURO CLARO LIMPAR T=9 U=20 L=95 AUTO PLANTA"));
    return;
  }
  atualizarTudo();
}

// ---------- Rede ----------
void iniciarRede() {
  Serial3.begin(115200);
  WiFi.init(Serial3);
  if (WiFi.status() == WL_NO_MODULE) {
    interfaceWiFiOK = false;
    Serial.println(F("ESP8266 nao respondeu: confira firmware AT, baud e DIP"));
    return;
  }
  interfaceWiFiOK = true;
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setBufferSize(384);
  mqtt.setSocketTimeout(2);
  mqtt.setKeepAlive(30);
  Serial.println(F("Interface AT encontrada. Tentando conectar Wi-Fi..."));
  ultimaTentativaWiFi = millis();
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
}

void manterRede() {
  if (!interfaceWiFiOK) return;
  unsigned long agora = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (agora - ultimaTentativaWiFi >= RETENTATIVA_REDE_MS) {
      ultimaTentativaWiFi = agora;
      Serial.println(F("Reconectando Wi-Fi..."));
      WiFi.begin(WIFI_SSID, WIFI_SENHA);
    }
    return;
  }

  if (!mqtt.connected()) {
    if (agora - ultimaTentativaMQTT >= RETENTATIVA_MQTT_MS) {
      ultimaTentativaMQTT = agora;
      bool ok = strlen(MQTT_USUARIO) ? mqtt.connect(MQTT_CLIENT_ID, MQTT_USUARIO, MQTT_SENHA)
                                     : mqtt.connect(MQTT_CLIENT_ID);
      if (ok) Serial.println(F("MQTT conectado"));
      else { Serial.print(F("Falha MQTT, state=")); Serial.println(mqtt.state()); }
    }
  } else mqtt.loop();
}

void numeroOuNull(char* destino, float valor, bool valido) {
  if (valido) dtostrf(valor, 0, 1, destino);
  else strcpy(destino, "null");
}

void publicarTelemetria() {
  if (!interfaceWiFiOK || !mqtt.connected()) return;
  char t[16], u[16], l[16], payload[300];
  bool luzOk = plantaAtiva == GRANDIUVA && leituras.luzValida;
  numeroOuNull(t, leituras.temperatura, leituras.temperaturaValida);
  numeroOuNull(u, leituras.umidadeSolo, leituras.soloValido);
  numeroOuNull(l, leituras.luminosidade, luzOk);
  int len = snprintf(payload, sizeof(payload),
      "{\"planta\":\"%s\",\"temperatura\":%s,\"umidadeSolo\":%s,"
      "\"luminosidade\":%s,\"soloValido\":%s,\"luzValida\":%s,"
      "\"aquecedor\":%s,\"bomba\":%s,\"sombreamento\":%s}",
      nomePlanta(), t, u, l,
      leituras.soloValido ? "true" : "false",
      luzOk ? "true" : "false",
      estados.aquecedor ? "true" : "false",
      estados.bomba ? "true" : "false",
      estados.sombreamento ? "true" : "false");
  if (len <= 0 || len >= (int)sizeof(payload)) { Serial.println(F("Payload excedeu buffer")); return; }
  if (mqtt.publish(MQTT_TOPICO, payload)) {
    Serial.println(F("[MQTT] Publicado:")); Serial.println(payload);
  } else Serial.println(F("[MQTT] Falha ao publicar"));
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);

  pinMode(LED_AQUECEDOR, OUTPUT);
  pinMode(LED_BOMBA, OUTPUT);
  pinMode(LED_SOMBREAMENTO, OUTPUT);
  digitalWrite(LED_AQUECEDOR, LOW);
  digitalWrite(LED_BOMBA, LOW);
  digitalWrite(LED_SOMBREAMENTO, LOW);
  pinMode(PINO_BOTAO, INPUT_PULLUP);

  dht.begin();
  carregarCalibracao();

  lcd.begin(16, 2);
  escreverLinha(0, "UNIDAVI IoT");
  escreverLinha(1, "Iniciando...");
  delay(1500);

  Serial.println(F("=== UNIDAVI IoT - Mega + ESP8266 AT + MQTT ==="));
  Serial.println(F("Calibre: SECO/MOLHADO (solo) e ESCURO/CLARO (LDR)"));
  atualizarTudo();
  ultimaPublicacao = millis();
  iniciarRede();
}

void loop() {
  verificarBotao();
  processarSerial();

  if (millis() - ultimaLeitura >= LEITURA_MS) atualizarTudo();

  if (millis() - ultimaTrocaPagina >= TROCA_PAGINA_MS) {
    ultimaTrocaPagina = millis();
    paginaDisplay = (paginaDisplay + 1) % 3;
    atualizarDisplay();
  }

  manterRede();

  if (millis() - ultimaPublicacao >= PUBLICACAO_MS) {
    ultimaPublicacao = millis();
    publicarTelemetria();
  }
}