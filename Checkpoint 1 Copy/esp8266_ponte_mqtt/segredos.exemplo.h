// Modelo de configuracao da ponte ESP8266. NAO coloque senhas reais aqui.
//
// Copie este arquivo para "segredos.local.h" (na mesma pasta) e preencha.
// O segredos.local.h esta no .gitignore e nunca vai para o repositorio.
//
// Testes: servidor, usuario e senha do SEU cluster HiveMQ Cloud.
// Entrega final: servidor, usuario e senha da ficha de conexao do professor.
//
// Teste de TLS sem HiveMQ: test.mosquitto.org, porta 8886 (certificado Let's
// Encrypt -> ISRG Root X1), com MQTT_USUARIO e MQTT_SENHA vazios ("").
#pragma once

const char WIFI_SSID[]    = "SUA_REDE_WIFI";
const char WIFI_SENHA[]   = "SUA_SENHA_WIFI";

const char MQTT_HOST[]    = "SEU-CLUSTER.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORTA = 8883;
const char MQTT_USUARIO[] = "SEU_USUARIO";
const char MQTT_SENHA[]   = "SUA_SENHA";
