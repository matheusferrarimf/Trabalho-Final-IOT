/*
 * UNIDAVI - Projeto Final IoT - Checkpoint 3
 * Arduino Mega 2560 + shield Doit ESP8266 ESP-12E (4 chaves DIP).
 * O Mega le sensores, aplica as regras, aciona os LEDs e o LCD.
 * O ESP8266 do shield roda o sketch esp8266_ponte_mqtt (MQTT sobre TLS, porta 8883)
 * e recebe as mensagens do Mega pelo Serial3 (pinos 14 TX3 / 15 RX3).
 *
 * CHAVES DIP DO SHIELD
 *   Funcionamento ..... 3 e 4 OFF (ESP inicia normal)
 *   Gravar o ESP ...... 3 e 4 ON + sketch mega_gravar_esp no Mega (ver o arquivo)
 *
 * Araucaria: T < 10 C -> aquecimento; umidade do solo < 35% -> irrigacao (bomba)
 * Grandiuva: umidade do solo < 30% -> irrigacao (bomba); luminosidade > 90% -> sombreamento
 *
 * PINOS
 *   DHT22 ........ D28        Botao ........ D30 (entre o pino e o GND)
 *   Solo ......... A15        LED aquecedor  D22
 *   LDR .......... A14        LED bomba .... D24
 *                             LED sombra ... D26
 *   LCD 16x2 (paralelo, 4 bits): RS D32 | E D34 | D4 D36 | D5 D38 | D6 D40 | D7 D42
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
const unsigned long TROCA_PAGINA_MS = 3000UL;   // tempo de cada tela do LCD
const unsigned long ESPECIE_PONTE_MS = 5000UL;  // reenvio da especie ativa ao ESP
const unsigned long PONTE_SILENCIO_MS = 8000UL; // sem "S" do ESP por esse tempo = ponte fora

// ---------- Ponte com o ESP8266 ----------
// Servidor, usuario, senha e prefixo ficam no sketch do ESP (esp8266_ponte_mqtt).
// Topicos: unidavi/iot/t27/1025897_36728/{especie}/{sensor|atuador|status}/...
#define PONTE Serial3
const unsigned long PONTE_BAUD = 115200UL;

DHT dht(PINO_DHT, DHT22);
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
uint8_t paginaDisplay = 0;               // 0 = leituras, 1 = atuadores, 2 = rede
unsigned long ultimaTrocaPagina = 0;

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
// Estado da rede informado pelo ESP
bool wifiOk = false, mqttOk = false;
unsigned long ultimoStatusPonte = 0, ultimoEnvioEspecie = 0;
char linhaPonte[32];
uint8_t tamLinhaPonte = 0;
bool publicacaoPendente = true, atuadoresPublicados = false;
Estados ultimosEstadosPublicados;

bool leituraBotaoAnterior = HIGH, estadoBotaoEstavel = HIGH;
unsigned long ultimaMudancaBotao = 0, primeiroClique = 0;
uint8_t cliques = 0;

// ---------- Utilidades ----------
const char* nomePlanta() { return plantaAtiva == ARAUCARIA ? "Araucaria" : "Grandiuva"; }
const char* especieTopico() { return plantaAtiva == ARAUCARIA ? "araucaria" : "grandiuva"; }
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
  Serial.print(F("Ponte ESP: ")); Serial.println(ponteAtiva() ? F("OK") : F("SEM RESPOSTA"));
  Serial.print(F("WiFi: ")); Serial.println(wifiOk ? F("CONECTADO") : F("DESCONECTADO"));
  Serial.print(F("MQTT (TLS 8883): ")); Serial.println(mqttOk ? F("CONECTADO") : F("DESCONECTADO"));
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

    default:  // rede
      if (!ponteAtiva()) {
        snprintf(l1, sizeof(l1), "ESP: SEM RESP.");
        snprintf(l2, sizeof(l2), "Fios 14/15 DIP34");
      } else {
        snprintf(l1, sizeof(l1), "WiFi: %s", wifiOk ? "OK" : "OFF");
        snprintf(l2, sizeof(l2), "MQTT TLS: %s", mqttOk ? "OK" : "OFF");
      }
      break;
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
  // O ESP publica "offline" na especie anterior e reconecta com a LWT da nova.
  // Quando reconectar ele manda "C" e o Mega republica tudo.
  enviarEspecie();
  mqttOk = false;
  publicacaoPendente = true;
  atuadoresPublicados = false;
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

// ---------- Ponte com o ESP8266 ----------
bool ponteAtiva() {
  return ultimoStatusPonte != 0 && millis() - ultimoStatusPonte < PONTE_SILENCIO_MS;
}

void enviarEspecie() {
  PONTE.print(F("E,"));
  PONTE.print(especieTopico());
  PONTE.print('\n');
  ultimoEnvioEspecie = millis();
}

void iniciarPonte() {
  PONTE.begin(PONTE_BAUD);
  Serial.println(F("Aguardando a ponte ESP8266 (MQTT/TLS)..."));
  enviarEspecie();
}

void processarLinhaPonte(const char* l) {
  // S,<wifi>,<mqtt>
  if (l[0] == 'S' && l[1] == ',' && l[3] == ',' && l[5] == '\0') {
    bool mqttAntes = mqttOk;
    wifiOk = l[2] == '1';
    mqttOk = l[4] == '1';
    ultimoStatusPonte = millis();
    if (mqttOk != mqttAntes) {
      Serial.print(F("[PONTE] MQTT ")); Serial.println(mqttOk ? F("conectado") : F("desconectado"));
    }
  } else if (l[0] == 'C' && l[1] == '\0') {
    // O ESP acabou de conectar ao broker: republicar estados retidos e leituras.
    mqttOk = wifiOk = true;
    ultimoStatusPonte = millis();
    atuadoresPublicados = false;
    publicacaoPendente = true;
    Serial.println(F("[PONTE] Conectado ao broker (TLS)"));
  }
  // Outras linhas (ruido do boot do ESP) sao ignoradas.
}

void lerPonte() {
  while (PONTE.available()) {
    char c = (char)PONTE.read();
    if (c == '\r') continue;
    if (c == '\n') {
      linhaPonte[tamLinhaPonte] = '\0';
      if (tamLinhaPonte) processarLinhaPonte(linhaPonte);
      tamLinhaPonte = 0;
    } else if (tamLinhaPonte < sizeof(linhaPonte) - 1) {
      linhaPonte[tamLinhaPonte++] = c;
    } else {
      tamLinhaPonte = 0;   // linha longa demais: descarta
    }
  }
  if (!ponteAtiva()) wifiOk = mqttOk = false;
  // Reenvia a especie de tempos em tempos: cobre o ESP reiniciando sozinho.
  if (millis() - ultimoEnvioEspecie >= ESPECIE_PONTE_MS) enviarEspecie();
}

void numeroOuNull(char* destino, float valor, bool valido) {
  if (valido) dtostrf(valor, 0, 1, destino);
  else strcpy(destino, "null");
}

// Envia ao ESP: P,<retida>,<sufixo>,<payload>. O ESP monta o topico com o prefixo.
bool publicarMensagem(const char* sufixo, const char* payload, bool retida) {
  if (!mqttOk) return false;
  PONTE.print(F("P,"));
  PONTE.print(retida ? '1' : '0');
  PONTE.print(',');
  PONTE.print(sufixo);
  PONTE.print(',');
  PONTE.print(payload);
  PONTE.print('\n');
  Serial.print(F("[MQTT] ")); Serial.print(especieTopico()); Serial.print('/');
  Serial.print(sufixo); Serial.print(' '); Serial.println(payload);
  return true;
}

bool publicarSensor(const char* nome, float valor, bool valido, const char* unidade) {
  char numero[16], payload[72], sufixo[40];
  numeroOuNull(numero, valor, valido);
  snprintf(payload, sizeof(payload), "{\"valor\":%s,\"unidade\":\"%s\"}", numero, unidade);
  snprintf(sufixo, sizeof(sufixo), "sensor/%s", nome);
  return publicarMensagem(sufixo, payload, false);
}

bool publicarAtuador(const char* nome, bool ligado) {
  char sufixo[40];
  snprintf(sufixo, sizeof(sufixo), "atuador/%s", nome);
  return publicarMensagem(sufixo, ligado ? "{\"estado\":\"ligado\"}" : "{\"estado\":\"desligado\"}", true);
}

void publicarAtuadores() {
  if (!mqttOk) return;
  bool ok = true;
  // Nome do topico igual ao da Especificacao e da tabela "regras": aquecimento.
  if (!atuadoresPublicados || estados.aquecedor != ultimosEstadosPublicados.aquecedor)
    ok = publicarAtuador("aquecimento", estados.aquecedor) && ok;
  if (!atuadoresPublicados || estados.bomba != ultimosEstadosPublicados.bomba)
    ok = publicarAtuador("irrigacao", estados.bomba) && ok;
  if (!atuadoresPublicados || estados.sombreamento != ultimosEstadosPublicados.sombreamento)
    ok = publicarAtuador("sombreamento", estados.sombreamento) && ok;
  if (ok) { ultimosEstadosPublicados = estados; atuadoresPublicados = true; }
}

void publicarTelemetria() {
  if (!mqttOk) return;
  // O status "online" e publicado pelo ESP ao conectar (e "offline" pela LWT).
  bool ok = publicarSensor("temperatura", leituras.temperatura, leituras.temperaturaValida, "C");
  ok = publicarSensor("umidade-solo", leituras.umidadeSolo, leituras.soloValido, "%") && ok;
  ok = publicarSensor("luminosidade", leituras.luminosidade,
                       plantaAtiva == GRANDIUVA && leituras.luzValida, "%") && ok;
  publicacaoPendente = !ok;
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

  Serial.println(F("=== UNIDAVI IoT - Mega + ponte ESP8266 (MQTT sobre TLS) ==="));
  Serial.println(F("Calibre: SECO/MOLHADO (solo) e ESCURO/CLARO (LDR)"));
  atualizarTudo();
  ultimaPublicacao = millis();
  iniciarPonte();
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

  lerPonte();
  publicarAtuadores();

  if (millis() - ultimaPublicacao >= PUBLICACAO_MS ||
      (publicacaoPendente && millis() - ultimaPublicacao >= 250UL)) {
    ultimaPublicacao = millis();
    publicarTelemetria();
  }
}
