/*
 * UNIDAVI - Projeto Final IoT
 * Mega como "cabo USB" para gravar o ESP8266 do shield Doit ESP-12E.
 *
 * Use so para gravar o esp8266_ponte_mqtt. Depois grave de novo o sketch_mega_mqtt_at.
 *
 * Passo a passo:
 *   1. Grave ESTE sketch no Mega (placa "Arduino Mega 2560").
 *   2. No shield: chaves DIP 3 e 4 em ON (ESP em modo de gravacao).
 *   3. Desligue e religue o USB: o ESP liga ja em modo de gravacao.
 *   4. Abra o esp8266_ponte_mqtt e escolha a placa "Generic ESP8266 Module"
 *      (Flash Size "1MB (FS:none OTA:~502KB)", Erase Flash "All Flash Contents",
 *      Upload Speed 115200, Reset Method "no dtr (aka ck)"),
 *      NA MESMA porta COM do Mega, e clique em Carregar.
 *   5. Terminou: chaves 3 e 4 em OFF, desligue e religue o USB.
 *
 * O ESP precisa estar ligado ao Serial3 do Mega (TX do ESP -> pino 15 RX3,
 * RX do ESP -> pino 14 TX3), a mesma ligacao usada pelo firmware final.
 */
const unsigned long BAUD = 115200UL;

void setup() {
  Serial.begin(BAUD);
  Serial3.begin(BAUD);
}

void loop() {
  // Repassa os bytes nos dois sentidos, sem alterar nada.
  while (Serial.available()) Serial3.write(Serial.read());
  while (Serial3.available()) Serial.write(Serial3.read());
}
