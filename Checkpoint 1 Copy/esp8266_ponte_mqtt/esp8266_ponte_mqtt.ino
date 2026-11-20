/*
 * UNIDAVI - Projeto Final IoT - Checkpoint 3
 * Ponte MQTT sobre TLS no ESP8266 (ESP-12E) do shield Doit, sobre o Arduino Mega
 *
 * O Mega le os sensores, aplica as regras e manda as mensagens por Serial3.
 * Este sketch roda NO ESP8266 e faz a conexao segura com o broker:
 *   Wi-Fi -> NTP (hora certa) -> TLS 1.2 com SNI na porta 8883, validando o
 *   certificado do servidor pelo ISRG Root X1 -> MQTT com usuario e senha.
 *
 * GRAVACAO (o Mega serve de cabo USB, sem adaptador):
 *   1. Grave o sketch mega_gravar_esp no Mega.
 *   2. Shield: chaves DIP 3 e 4 em ON. Desligue e religue o USB.
 *   3. Placa na IDE: "Generic ESP8266 Module" (core ESP8266 da Espressif),
 *      Flash Size "1MB (FS:none OTA:~502KB)", Erase Flash "All Flash Contents",
 *      Upload Speed 115200, Reset Method "no dtr (aka ck)",
 *      mesma porta COM do Mega. Biblioteca: PubSubClient (Nick O'Leary).
 *   4. Depois: chaves 3 e 4 em OFF, religue o USB e grave o sketch_mega_mqtt_at no Mega.
 * Isso apaga o firmware AT do ESP. Ele pode ser regravado depois, se precisar.
 *
 * CONFIGURACAO: copie segredos.exemplo.h para segredos.local.h e preencha.
 *
 * PROTOCOLO COM O MEGA (115200, uma mensagem por linha):
 *   Mega -> ESP   E,<especie>                    especie ativa (araucaria | grandiuva)
 *   Mega -> ESP   P,<0|1 retida>,<sufixo>,<json> publica em PREFIXO/<especie>/<sufixo>
 *   ESP  -> Mega  S,<wifi 0|1>,<mqtt 0|1>        estado da rede (a cada 2 s)
 *   ESP  -> Mega  C                              MQTT acabou de conectar: republicar
 *
 * O Serial (UART0) e o canal com o Mega: os logs de depuracao vao para o
 * Serial1 (somente TX, GPIO2), que nao interfere na comunicacao.
 */
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

#if __has_include("segredos.local.h")
#include "segredos.local.h"
#else
#include "segredos.exemplo.h"
#endif

// Prefixo da equipe. A ponte so publica dentro dele.
const char MQTT_PREFIXO[] = "unidavi/iot/t27/1025897_36728";
const char CLIENT_ID_BASE[] = "t27-1025897_36728";

const unsigned long RETENTATIVA_WIFI_MS = 15000UL;
const unsigned long RETENTATIVA_MQTT_MS = 5000UL;
const unsigned long STATUS_MS = 2000UL;
const time_t HORA_VALIDA = 1700000000;   // 2023-11-14: abaixo disso o NTP ainda nao respondeu

// Certificado raiz publico do Let's Encrypt (ISRG Root X1), valido ate 04/06/2035.
// SHA-256: 96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6
static const char ISRG_ROOT_X1[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";

BearSSL::X509List* raizConfiavel = nullptr;   // criada no setup()
BearSSL::WiFiClientSecure clienteTls;
PubSubClient mqtt(clienteTls);

char especie[16] = "";   // vazio ate o Mega informar a especie ativa
unsigned long ultimaTentativaWiFi = 0, ultimaTentativaMQTT = 0, ultimoStatus = 0;
bool horaPedida = false;
bool ultimoWifi = false, ultimoMqtt = false;

char linha[320];
size_t tamLinha = 0;
bool linhaTruncada = false;

#define LOG(...) Serial1.printf(__VA_ARGS__)

// ---------- Utilidades ----------
bool especieValida(const char* e) {
  return strcmp(e, "araucaria") == 0 || strcmp(e, "grandiuva") == 0;
}

void montarTopico(char* dest, size_t n, const char* sufixo) {
  snprintf(dest, n, "%s/%s/%s", MQTT_PREFIXO, especie, sufixo);
}

void enviarStatus() {
  bool wifi = WiFi.status() == WL_CONNECTED;
  bool conectado = mqtt.connected();
  Serial.printf("S,%d,%d\n", wifi ? 1 : 0, conectado ? 1 : 0);
  ultimoWifi = wifi;
  ultimoMqtt = conectado;
  ultimoStatus = millis();
}

// ---------- Comandos do Mega ----------
void trocarEspecie(const char* nova) {
  if (!especieValida(nova) || strcmp(nova, especie) == 0) return;
  if (mqtt.connected()) {
    char topico[96];
    montarTopico(topico, sizeof(topico), "status");
    mqtt.publish(topico, "offline", true);   // desconexao limpa nao dispara a LWT
    mqtt.disconnect();
  }
  strncpy(especie, nova, sizeof(especie) - 1);
  especie[sizeof(especie) - 1] = '\0';
  ultimaTentativaMQTT = millis() - RETENTATIVA_MQTT_MS;   // reconecta ja
  LOG("Especie ativa: %s\n", especie);
  enviarStatus();
}

// P,<retida>,<sufixo>,<payload>  (o payload pode conter virgulas)
void publicarDoMega(char* resto) {
  char* virgula1 = strchr(resto, ',');
  if (!virgula1) return;
  *virgula1 = '\0';
  char* sufixo = virgula1 + 1;
  char* virgula2 = strchr(sufixo, ',');
  if (!virgula2) return;
  *virgula2 = '\0';
  const char* payload = virgula2 + 1;

  bool retida = resto[0] == '1';
  // O sufixo precisa ser sensor/<nome> ou atuador/<nome>, sem curingas.
  if (strpbrk(sufixo, "#+") || strstr(sufixo, "..")) return;
  if (strncmp(sufixo, "sensor/", 7) != 0 && strncmp(sufixo, "atuador/", 8) != 0) return;
  if (!especie[0] || !mqtt.connected()) return;

  char topico[128];
  montarTopico(topico, sizeof(topico), sufixo);
  bool ok = mqtt.publish(topico, payload, retida);
  LOG("%s %s %s\n", ok ? "PUB" : "FALHA", topico, payload);
}

void processarLinha(char* l) {
  if (l[0] == 'E' && l[1] == ',') trocarEspecie(l + 2);
  else if (l[0] == 'P' && l[1] == ',') publicarDoMega(l + 2);
  // qualquer outra coisa (ruido de boot, linha truncada) e ignorada
}

void lerMega() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      linha[tamLinha] = '\0';
      if (!linhaTruncada && tamLinha) processarLinha(linha);
      tamLinha = 0;
      linhaTruncada = false;
    } else if (tamLinha < sizeof(linha) - 1) {
      linha[tamLinha++] = c;
    } else {
      linhaTruncada = true;
    }
  }
}

// ---------- Rede ----------
void manterRede() {
  unsigned long agora = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (agora - ultimaTentativaWiFi >= RETENTATIVA_WIFI_MS) {
      ultimaTentativaWiFi = agora;
      LOG("Conectando Wi-Fi %s...\n", WIFI_SSID);
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_SENHA);
    }
    return;
  }

  if (!horaPedida) {
    configTime(0, 0, "pool.ntp.org", "time.google.com", "a.st1.ntp.br");
    horaPedida = true;
    LOG("Wi-Fi OK, IP %s. Sincronizando hora...\n", WiFi.localIP().toString().c_str());
  }

  if (mqtt.connected()) { mqtt.loop(); return; }
  if (!especie[0]) return;                     // aguarda o Mega dizer a especie
  if (time(nullptr) < HORA_VALIDA) return;     // sem hora certa o certificado nao valida
  if (agora - ultimaTentativaMQTT < RETENTATIVA_MQTT_MS) return;

  ultimaTentativaMQTT = agora;
  char topicoStatus[96], clientId[64];
  montarTopico(topicoStatus, sizeof(topicoStatus), "status");
  snprintf(clientId, sizeof(clientId), "%s-%s-esp01", CLIENT_ID_BASE, especie);

  clienteTls.setX509Time(time(nullptr));
  LOG("[MQTT] TLS %s:%u (%s)\n", MQTT_HOST, MQTT_PORTA, clientId);
  // Usuario vazio = broker sem autenticacao (so para teste, ex.: test.mosquitto.org:8886).
  bool comLogin = MQTT_USUARIO[0] != '\0';
  bool ok = mqtt.connect(clientId, comLogin ? MQTT_USUARIO : nullptr, comLogin ? MQTT_SENHA : nullptr,
                         topicoStatus, 1, true, "offline");
  if (ok) {
    mqtt.publish(topicoStatus, "online", true);
    LOG("[MQTT] conectado\n");
    Serial.print("C\n");                        // Mega republica atuadores e leituras
  } else {
    char erro[96];
    clienteTls.getLastSSLError(erro, sizeof(erro));
    LOG("[MQTT] falha state=%d tls=%s\n", mqtt.state(), erro);
  }
  ultimaTentativaMQTT = millis();
  enviarStatus();
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  Serial1.begin(115200);
  LOG("\nPonte MQTT/TLS - UNIDAVI IoT\n");

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  raizConfiavel = new BearSSL::X509List(ISRG_ROOT_X1);
  clienteTls.setTrustAnchors(raizConfiavel);    // valida o servidor pelo ISRG Root X1 (nada de setInsecure)

  mqtt.setServer(MQTT_HOST, MQTT_PORTA);         // conecta pelo hostname: o SNI vai junto
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(10);

  ultimaTentativaWiFi = millis() - RETENTATIVA_WIFI_MS;
  ultimaTentativaMQTT = millis() - RETENTATIVA_MQTT_MS;
}

void loop() {
  lerMega();
  manterRede();
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (millis() - ultimoStatus >= STATUS_MS || wifi != ultimoWifi || mqtt.connected() != ultimoMqtt)
    enviarStatus();
}
