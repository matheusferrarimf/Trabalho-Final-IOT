# Checkpoint 3 · Araucária e Grandiúva

Turma **t27**, equipe **1025897_36728**. O fluxo usa `unidavi/iot/t27/1025897_36728/#` nos testes e na entrega.

## 1. Broker e placa

O [Compose do Checkpoint 2](../cp2-broker/docker-compose.yaml) sobe o Mosquitto e o Node-RED locais. O HiveMQ Cloud é provisionado pelo [painel do HiveMQ](https://app.hivemq.com/).

1. Use um cluster próprio no HiveMQ Cloud. O plano Serverless gratuito já está fechado para novas instâncias e os clusters existentes serão encerrados em 31/12/2026. Se a equipe ainda precisa de um cluster próprio gratuito, confirme com o professor qual ambiente ele aceita para os testes. No cluster de teste, crie em **Access Management** uma permissão personalizada para `unidavi/iot/t27/1025897_36728/#` e atribua-a a uma credencial de teste. A permissão precisa permitir a publicação da placa e a assinatura do Node-RED. Use um tópico descartável do seu cluster para testar a recusa fora do prefixo. [Encerramento do Serverless](https://docs.hivemq.com/hivemq-cloud/serverless-end-of-life.html) · [Permissões do HiveMQ Cloud](https://docs.hivemq.com/hivemq-cloud/authn-authz.html)
2. **Firmware: Mega + ESP8266 como ponte TLS.** O firmware AT do ESP8266 não faz TLS com SNI, então o ESP-12E do shield Doit (4 chaves DIP) foi regravado com [`esp8266_ponte_mqtt`](../Checkpoint%201%20Copy/esp8266_ponte_mqtt/esp8266_ponte_mqtt.ino). Ele usa `WiFiClientSecure` (BearSSL) e `PubSubClient` na porta **8883**, com SNI pelo hostname e validação do servidor pelo **ISRG Root X1** embutido no código. Não há `setInsecure`. Antes do TLS, o ESP acerta o relógio por NTP, que é necessário para validar o certificado. O Mega ([`sketch_mega_mqtt_at`](../Checkpoint%201%20Copy/sketch_mega_mqtt_at/sketch_mega_mqtt_at.ino)) continua com sensores, regras, LEDs, LCD, botão e calibração, e manda as mensagens ao ESP pelo Serial3. Os tópicos e os payloads são os mesmos do CP2. A única mudança é que o atuador da Araucária agora se chama `aquecimento`, como na Especificação.

   O ESP fica ligado ao Serial3 do Mega: TX do ESP no pino 15 (RX3) e RX do ESP no pino 14 (TX3). O próprio Mega faz o papel do cabo USB na gravação do ESP, sem adaptador.

   | Etapa | Chaves DIP do shield | Na Arduino IDE |
   |---|---|---|
   | 1. Preparar a gravação | — | Gravar [`mega_gravar_esp`](../Checkpoint%201%20Copy/mega_gravar_esp/mega_gravar_esp.ino) no Mega (**Arduino Mega 2560**) |
   | 2. Gravar o ESP8266 | 3 e 4 **ON**, depois desligar e religar o USB | Abrir `esp8266_ponte_mqtt`, placa **Generic ESP8266 Module** (Flash Size *1MB (FS:none OTA:~502KB)*, Erase Flash *All Flash Contents*, Upload 115200, Reset Method *no dtr (aka ck)*), **mesma porta COM** do Mega. Biblioteca **PubSubClient** |
   | 3. Gravar o Mega | 3 e 4 **OFF**, depois desligar e religar o USB | Gravar `sketch_mega_mqtt_at` (**Arduino Mega 2560**, bibliotecas **DHT sensor library** + Adafruit Unified Sensor) |
   | Funcionamento | 3 e 4 OFF | Monitor Serial do Mega em 115200 |

   Se a gravação parar em `Connecting...`, desligue e religue o USB com as chaves 3 e 4 em ON e tente de novo. Se ainda falhar, um capacitor de 10 µF entre RESET e GND do Mega impede que ele reinicie durante a gravação (retire depois). A gravação apaga o firmware AT do ESP.

   Antes de gravar o ESP, copie `segredos.exemplo.h` para `segredos.local.h` na mesma pasta e preencha o Wi-Fi, o host do cluster, o usuário e a senha. O `segredos.local.h` está no `.gitignore`. A página "rede" do LCD mostra `WiFi: OK` e `MQTT TLS: OK` quando a ponte está conectada. Se aparecer `ESP: SEM RESP.`, confira as chaves 1 e 2 e o seletor RXD3/TXD3.

   Protocolo Mega ↔ ESP (115200, uma mensagem por linha):
   - `E,<especie>`: espécie ativa, reenviada a cada 5 s;
   - `P,<retida>,<sufixo>,<json>`: publicação. O ESP monta o tópico com o prefixo da equipe e recusa curingas e sufixos fora de `sensor/` e `atuador/`;
   - `S,<wifi>,<mqtt>`: estado da rede, a cada 2 s;
   - `C`: o MQTT conectou, e o Mega republica os atuadores retidos e as leituras.

   O `online` é publicado pelo ESP ao conectar e o `offline` vem da LWT. Na troca de espécie, o ESP publica `offline` na anterior e reconecta com a LWT da nova.
3. Na apresentação, troque no `segredos.local.h` só o host, o usuário e a senha pelos dados da ficha do professor, e regrave o ESP. Faça o mesmo no nó do broker do Node-RED. Use o prefixo t27 da equipe e IDs de cliente distintos (placa: `t27-1025897_36728-<especie>-esp01`). A credencial compartilhada da turma permite a conexão, e o isolamento final depende do prefixo usado por cada equipe.

## 2. Fluxo e painel

1. Instale `node-red-dashboard` no Node-RED pelo **Manage palette**. Importe `node_red_checkpoint3.json` em uma instância com o painel legado. Se houver um fluxo do Checkpoint 2 ativo, desative a aba antiga durante a demonstração para evitar uma assinatura paralela no broker público.
2. Edite a configuração **HiveMQ Cloud · CP3**. Substitua o host de exemplo pelo hostname da sua conta e preencha usuário e senha na aba de segurança. O arquivo já define 8883, TLS e validação do certificado. As credenciais ficam no armazenamento do Node-RED.
3. Configure as variáveis de ambiente abaixo na sessão que inicia o Node-RED. Use o endereço HTTPS do projeto e sua chave **publishable** ou **anon**. Para a chave, a leitura abaixo desativa o eco no PowerShell.

   ```powershell
   $env:SUPABASE_URL = 'https://SEU-ID.supabase.co'
   $cp3Token = Read-Host 'Chave pública do Supabase' -AsSecureString
   $env:SUPABASE_KEY = [System.Net.NetworkCredential]::new('', $cp3Token).Password
   Remove-Variable cp3Token
   node-red
   ```

4. Faça **Deploy**. Abra `http://localhost:1880/ui/` e escolha **Araucária** ou **Grandiúva** pelos botões no topo do painel. As abas mostram os sensores e atuadores de cada cenário. O histórico do painel usa os 30 minutos mais recentes da sessão do Node-RED e continua ao navegar entre abas. O Supabase preserva o histórico de longo prazo.
5. O estado da placa vem do tópico retido e da última vontade. Ao trocar a espécie pelo botão, o kit publica `offline` na anterior. Se a conexão do Node-RED cair, os valores anteriores ficam identificados como desatualizados. Para um estado de atuador recebido como mensagem retida, o horário indicado é o da primeira observação pelo Node-RED. O firmware envia o estado sem carimbo de hora.

## 3. Supabase e ThingSpeak

1. Use um projeto Supabase dedicado à atividade. Em **SQL Editor**, execute `supabase_schema.sql` desta pasta. O script cadastra a equipe t27 e as duas espécies. O papel `anon` recebe somente INSERT nas quatro tabelas de dados. O fluxo envia `Prefer: return=minimal`.
2. Crie um canal no ThingSpeak na sua conta. Ative os oito campos na ordem indicada.

   | Campo | Nome |
   |---|---|
   | 1 | Luminosidade (%) |
   | 2 | Umidade do solo (%) |
   | 3 | Umidade do ar (%) |
   | 4 | Temperatura (°C) |
   | 5 | Estado geral (código) |
   | 6 | Atuadores ligados |
   | 7 | Espécie (código) |
   | 8 | Idade da leitura (s) |

   O código da espécie é **1** para Araucária e **8** para Grandiúva. Os códigos do estado geral são **0** OFFLINE, **1** SEM DADOS, **2** ALERTA, **3** ATUANDO, **4** NORMAL e **5** INATIVA (reservado aos trios). Campos sem leitura ficam vazios. O campo 4 registra também a temperatura do DHT22 na Grandiúva, publicada pelo firmware como leitura adicional. As regras e os sensores exibidos na aba da Grandiúva são os do cenário, solo e luminosidade.
3. No Table Editor do Supabase, localize a linha da equipe em `integracoes` e preencha o ID do canal e a Write API Key. Ative `pg_net` e `pg_cron` nas extensões do Supabase e execute no SQL Editor:

   ```sql
   select cron.schedule('thingspeak_a_cada_minuto', '* * * * *',
                        'select public.fn_enviar_thingspeak()');
   ```

4. Confirme `v_estado_atual`, `v_thingspeak_payload`, `cron.job_run_details` e `net._http_response`. A função enfileira o POST e a resposta HTTP confirma o envio. Compare a amostra publicada no canal com a espécie e os valores daquele minuto. A espécie da última leitura recebida determina o próximo envio.

## 4. Roteiro de teste e apresentação

| Situação | O que conferir |
|---|---|
| Araucária, 10 °C e solo a 35% | NORMAL, aquecimento e irrigação desligados. A 9 °C ou 34%, respectivamente, muda para ATUANDO. |
| Grandiúva, solo a 30% e luz a 90% | NORMAL. A 29% no solo ou 91% de luz, o atuador correspondente liga e aparece ATUANDO. |
| Placa online com um sensor sem leituras válidas | Após mais de 30 s, SEM DADOS. O outro sensor continua atualizando. |
| Divergência com leituras recentes | Se persistir por mais de 20 s, ALERTA com o atuador e o estado esperado. |
| Desligar e religar a placa | Aguardar a última vontade, conferir OFFLINE e ocultação dos estados atuais. Ao religar, confirmar a volta das leituras sem reiniciar o painel. |
| Interromper e restabelecer o Node-RED → broker | Indicador desconectado e valores antigos destacados. Ao restabelecer, conferir nova leitura válida em cada sensor. |
| Acesso no HiveMQ próprio | Conectar via 8883 com credencial válida. Em sessão separada, verificar a recusa sem credenciais e a negação de publicar ou assinar fora do prefixo. Registrar a resposta do cliente MQTT sem mostrar credenciais. |
| Caminho dos dados | Conferir o mesmo acionamento no display, no painel e nas quatro tabelas. Conferir o próximo envio no canal e o agendamento do banco. |

Na apresentação de **até 8 minutos**, Sofya apresenta as duas espécies e começa a arquitetura (1 min de cenários e 2 min de arquitetura, total). Matheus conduz os 3 min de demonstração em hardware, com troca de espécie e comparação dos dados. Dividam 1 min de segurança e 1 min de aprendizados. Reservem tempo de aquecimento do histórico antes da apresentação. Façam o ensaio na rede da sala e registrem painel em NORMAL, ATUANDO e OFFLINE, display, Supabase, ThingSpeak e testes de segurança. Guardem um vídeo de até 1 min do sistema em funcionamento para eventual falha de rede.
