# UNIDAVI — API e Dashboard de monitoramento de plantas

Projeto para o **Mega 2560 WiFi R3 + ESP8266 (firmware AT)**, baseado no sketch fornecido. **Não é necessário alterar o `.ino` para usar o fluxo**, desde que o MQTT já esteja conectando e publicando normalmente.

O pacote contém um **fluxo importável para Node-RED Dashboard 2.0 (FlowFuse)**, dois endpoints HTTP de leitura e mensagens de teste.

## Arquitetura

```text
DHT22 (temperatura) ─┐
Solo A15 (umidade) ──┼─> Arduino Mega + ESP8266 ── MQTT Wi-Fi ──> Mosquitto:1883
LDR A14 (luz) ───────┘                  tópico: unidavi/iot/plantas/telemetria
                                                          |
                                               Node-RED (MQTT in)
                                                          |
                                          normalização + histórico em memória
                                                  /                 \
                                         Dashboard 2.0          API HTTP GET
```

O Arduino faz leitura a cada **2 segundos** (`LEITURA_MS = 2000`) e publicação MQTT a cada **10 segundos** (`PUBLICACAO_MS = 10000`). O painel se atualiza ao receber cada mensagem (a cada 10 s, quando tudo está funcionando). Após 30 s sem mensagem, mostra **Sem atualização**.

### Dados exibidos

- Planta ativa: **Araucária** ou **Grandiúva**.
- **Temperatura do ar (°C)** via DHT22 (somente temperatura, conforme código atual).
- **Umidade do solo (%)** via sensor A15, após calibração SECO/MOLHADO.
- **Luminosidade (%)** via LDR A14, após calibração ESCURO/CLARO. **O `.ino` publica `null` para Araucária**, então o painel mostra **N/A** nessa planta.
- Atuadores: aquecedor D22, bomba D24 e sombreamento D26; o painel **apenas observa** os estados enviados pelo Arduino. Não controla saídas remotas.
- Indicador de recebimento/atraso, último horário recebido e linhas de tendências (com amostras recentes da planta ativa).

## 1. Pré-requisitos no PC

1. **Node.js (versão LTS suportada pelo Node-RED)** instalado.
2. **Node-RED**. No PowerShell:
   ```powershell
   npm install -g node-red
   node-red
   ```
   Abra **http://localhost:1880** (mantenha o terminal aberto).
3. **Dashboard 2.0**: no editor Node-RED, menu superior direito **Manage palette → Install**; pesquise e instale **`@flowfuse/node-red-dashboard`**. Não instale o `node-red-dashboard` antigo.
4. **Broker MQTT Mosquitto** em execução (pode ser no mesmo PC do Node-RED).

Guia oficial Node-RED Windows: https://nodered.org/docs/getting-started/windows

Guia oficial Dashboard 2.0: https://dashboard.flowfuse.com/getting-started

## 2. Configure o Mosquitto

O broker recebe as mensagens do Arduino. Caso o Mosquitto esteja no **mesmo PC do Node-RED**:

- O Arduino precisa do **IPv4 local do PC** no campo `MQTT_HOST` (encontre usando `ipconfig`), **não** `127.0.0.1` e **não** o IP da placa.
- O Node-RED se conecta ao mesmo broker por `127.0.0.1:1883` (configuração já presente no arquivo importável).
- Para um laboratório em **rede privada e confiável**, um arquivo simples `mosquitto.conf` pode conter:

  ```conf
  listener 1883
  allow_anonymous true
  ```

  Em seguida, execute o Mosquitto usando esse arquivo de configuração (`mosquitto -c caminho/para/mosquitto.conf -v`). No Windows, a instalação pode iniciar um serviço automaticamente; se a porta 1883 já estiver em uso, ajuste a configuração do serviço em vez de iniciar uma segunda instância.

  **Segurança:** essa configuração anônima é **somente para demonstração em LAN isolada/confiável**. Não publique a porta 1883 na internet. Restrinja a regra do Firewall do Windows à rede privada e, em ambientes fora do laboratório, configure autenticação de clientes e proteja o acesso ao Node-RED.

Se o Mosquitto estiver em outro equipamento, altere também o campo **Server** no nó MQTT do fluxo Node-RED. O IP no `.ino` e o host do Node-RED devem apontar **para o mesmo broker**.

## 3. Configure o `.ino`

O sketch já usa:

```cpp
const char MQTT_HOST[] = "IP_DO_PC_COM_MOSQUITTO"; // Ex.: IPv4 obtido no ipconfig
const uint16_t MQTT_PORTA = 1883;
const char MQTT_TOPICO[] = "unidavi/iot/plantas/telemetria";
```

Mantenha o SSID e a senha do Wi-Fi apenas no seu arquivo local; **não compartilhe as credenciais reais** ao publicar o código. Se a senha copiada anteriormente era real, troque-a antes de tornar o projeto público.

Se o Mosquitto exigir usuário/senha, preencha também `MQTT_USUARIO` e `MQTT_SENHA` no sketch e no nó de conexão MQTT do Node-RED.

**Não é preciso regravar firmware do ESP8266:** preserve o firmware AT e as configurações DIP/Serial3 do seu sketch.

Confirme no Monitor Serial (115200) se aparecem mensagens como `WiFi: CONECTADO`, `MQTT conectado` e `[MQTT] Publicado:`.

## 4. Importe e execute o fluxo

1. No Node-RED (http://localhost:1880), clique no menu **Import → select a file**.
2. Selecione **`node_red_unidavi_plantas.json`**.
3. Clique no nó **MQTT - telemetria do Arduino** e confira **Server** (`127.0.0.1`) e **Port** (`1883`) e **Topic** (`unidavi/iot/plantas/telemetria`).
4. Clique em **Deploy** (canto superior direito).
5. Abra o painel em **http://localhost:1880/dashboard/plantas**.

Se você usa Node-RED em outro dispositivo, substitua `localhost` pelo IP do dispositivo que executa o Node-RED, respeitando o acesso de rede.

> Antes de ter mensagens, o painel mostrará **Aguardando dados**. Isso é esperado.

## 5. Teste SEM a placa

No fluxo importado, há dois nós de teste (Inject):

- **TESTAR: Araucária** → temperatura 8,7 °C, solo 27,5%, aquecedor e bomba ligados, luz N/A.
- **TESTAR: Grandiúva** → temperatura 24,1 °C, solo 24,5%, luminosidade 96,2%, bomba e sombra ligados.

Clique no quadradinho à esquerda de cada Inject e abra/atualize o Dashboard. O indicador mostrará **Simulação ativa / Dados de teste**. Os testes alimentam também os endpoints HTTP, mas **não ligam nenhum LED físico**. Quando o Arduino voltar a publicar no tópico MQTT, os dados reais substituirão a simulação automaticamente.

Para validar os valores físicos pelo próprio Arduino, use os comandos Serial já previstos no sketch (`T=9`, `U=20`, `L=95`, `PLANTA`, `AUTO`) e aguarde a próxima publicação MQTT. Esses comandos alteram os valores de teste no firmware e, diferentemente dos Injects do Node-RED, podem alterar o estado dos LEDs.

## 6. API HTTP

Os endpoints são de **leitura**, usam JSON e funcionam enquanto o Node-RED estiver executando. **Não é necessário usar outra linguagem nem outro servidor**: os nós `HTTP In → Function → HTTP Response` implementam a API.

### `GET /api/plantas/atual`

```
http://localhost:1880/api/plantas/atual
```

Retorna HTTP **200** com o pacote mais recente, indicador `online`, `idadeSegundos` e taxa de publicação. Antes de qualquer leitura, retorna HTTP **503** e `erro: sem_telemetria`. O campo `online` indica **dado recebido há no máximo 30 s** (e não mede diretamente a conexão da placa); leituras simuladas também contam para esse indicador.

Exemplo ilustrativo de resposta:

```json
{
  "online": true,
  "idadeSegundos": 3,
  "intervaloPublicacaoSegundos": 10,
  "telemetria": {
    "planta": "Grandiuva",
    "temperatura": 24.1,
    "umidadeSolo": 24.5,
    "luminosidade": 96.2,
    "soloValido": true,
    "luzValida": true,
    "aquecedor": false,
    "bomba": true,
    "sombreamento": true,
    "limiarSolo": 30,
    "limiarTemperatura": 10,
    "limiarLuminosidade": 90,
    "origem": "mqtt",
    "recebidoEm": "2026-11-09T22:00:00.000Z"
  }
}
```

O campo `recebidoEm` é o horário **do PC/Node-RED no instante em que recebeu o pacote**, não um relógio do Arduino.

### `GET /api/plantas/historico`

```
http://localhost:1880/api/plantas/historico
http://localhost:1880/api/plantas/historico?limite=20
http://localhost:1880/api/plantas/historico?limite=20&planta=Grandiuva
```

Retorna `totalDisponivel`, `limite`, `planta` e `amostras` (lista de telemetrias). Padrão: até **60** amostras. Máximo: **360**. Os dados ficam somente na **memória do Node-RED** e são apagados ao reiniciar ou fazer deploy completo. A API não implementa banco de dados nem armazenamento permanente.

Teste no PowerShell:

```powershell
Invoke-RestMethod http://localhost:1880/api/plantas/atual
Invoke-RestMethod 'http://localhost:1880/api/plantas/historico?limite=5'
```

## 7. Texto para a entrega da atividade

> O painel inicial foi desenvolvido utilizando o Node-RED Dashboard 2.0, integrado ao Arduino Mega 2560 com módulo ESP8266 por meio do protocolo MQTT. Os sensores são consultados pelo microcontrolador a cada 2 segundos, enquanto a telemetria é publicada no tópico `unidavi/iot/plantas/telemetria` a cada 10 segundos. O dashboard apresenta a espécie selecionada (Araucária ou Grandiúva), temperatura do ambiente em graus Celsius, umidade do solo em porcentagem e luminosidade em porcentagem quando aplicável à Grandiúva. O painel também apresenta o estado de acionamento dos atuadores simulados por LEDs (aquecedor, irrigação e sombreamento), além de indicadores de atualização e tendências recentes. O servidor Node-RED disponibiliza uma API HTTP para consulta da última leitura e do histórico recente. Como o firmware publica `null` para luminosidade quando a Araucária está ativa, o painel identifica essa medição como não aplicável, respeitando as regras de monitoramento implementadas no protótipo.

Inclua **capturas do dashboard com leituras reais da placa** e, se solicitado, capturas do fluxo MQTT e dos resultados da API. Use os nós de simulação apenas para a etapa de teste e identifique explicitamente qualquer captura simulada.

## 8. Solução de problemas

- **`MQTT desconectado` no Serial:** verifique `MQTT_HOST` no `.ino`, broker 1883 e Firewall, rede Wi-Fi 2,4 GHz e credenciais MQTT.
- **Nó `MQTT in` desconectado:** teste o broker com Node-RED configurado para `127.0.0.1:1883` (se no mesmo PC); em outros equipamentos use o IP real.
- **O dashboard abre em branco:** confirme que `@flowfuse/node-red-dashboard` está instalado e que o fluxo foi importado/deployado. Abra a URL completa `/dashboard/plantas`.
- **Solo aparece “—”:** é porque `soloValido` é falso ou o valor não foi publicado. Faça `SECO` e `MOLHADO` no Serial para calibrar o sensor.
- **Luminosidade “N/A”:** comportamento esperado com Araucária. Na Grandiúva, `—` pode indicar ausência de calibração (`ESCURO` e `CLARO`).
- **Após trocar planta os valores não mudam imediatamente:** o sketch atualiza internamente no clique, porém somente publica MQTT na próxima janela de 10 segundos.

