# 🌲 Monitoramento IoT da Araucária

Sistema embarcado com **ESP32** para monitorar e controlar automaticamente as condições de cultivo da **Araucária / Pinheiro-brasileiro** (*Araucaria angustifolia*). O sistema lê a temperatura do ar e a umidade do solo e aciona, por meio de módulos relé, o **aquecimento** e a **irrigação** sempre que os valores saem da faixa adequada para a espécie.

Projeto Final da disciplina de **Internet das Coisas — UNIDAVI**.

[![Simular no Wokwi](https://img.shields.io/badge/Simular%20no-Wokwi-2ea44f?logo=wokwi)](https://wokwi.com/projects/477988258882690049)
![ESP32](https://img.shields.io/badge/ESP32-DevKit%20C%20v4-E7352C?logo=espressif&logoColor=white)
![Arduino](https://img.shields.io/badge/Arduino-C%2B%2B-00979D?logo=arduino&logoColor=white)

---

## 📑 Sumário

- [Visão geral](#-visão-geral)
- [Regras de controle](#-regras-de-controle)
- [Hardware](#-hardware)
- [Ligações](#-ligações)
- [Como simular](#-como-simular)
- [Configuração](#️-configuração)
- [Comandos de teste pelo Monitor Serial](#-comandos-de-teste-pelo-monitor-serial)
- [Estrutura do repositório](#-estrutura-do-repositório)
- [Roadmap](#️-roadmap)
- [Autores](#-autores)

---

## 🔎 Visão geral

A cada **2 segundos** o ESP32:

1. lê a temperatura no **DHT22** e a umidade do solo na entrada analógica;
2. aplica as regras de controle;
3. liga ou desliga os relés de aquecimento e irrigação;
4. imprime o estado do sistema no Monitor Serial.

A temporização é feita com `millis()`, sem `delay()`, então o loop nunca fica bloqueado e o sistema continua respondendo a comandos pela serial a qualquer momento.

## 📏 Regras de controle

| Condição | Ação | Saída |
|---|---|---|
| Temperatura **< 10 °C** | Liga o aquecimento | Relé no **GPIO 18** |
| Umidade do solo **< 35 %** | Liga a irrigação | Relé no **GPIO 19** |

- As duas regras são **independentes**.
- Valores **iguais** ao limiar mantêm a saída **desligada**.
- **Falha segura:** se a leitura do DHT22 falhar, o aquecimento é desligado.
- Os relés iniciam desligados assim que o ESP32 liga.

> ⚠️ O ESP32 aciona apenas a bobina do relé. A carga (bomba/válvula e aquecedor) deve ficar no contato **COM/NO** do relé, alimentada por uma **fonte própria**.

## 🔧 Hardware

| Componente | Função |
|---|---|
| ESP32 DevKit C v4 | Microcontrolador |
| DHT22 | Sensor de temperatura |
| Sensor capacitivo de umidade do solo | Leitura da umidade (simulado por um **potenciômetro** no Wokwi) |
| 2 × Módulo relé | Acionamento do aquecimento e da irrigação |

## 🔌 Ligações

| Componente | Pino do componente | ESP32 |
|---|---|---|
| DHT22 | VCC | 3V3 |
| DHT22 | GND | GND |
| DHT22 | SDA (dados) | **GPIO 4** |
| Sensor de solo / potenciômetro | VCC | 3V3 |
| Sensor de solo / potenciômetro | GND | GND |
| Sensor de solo / potenciômetro | SIG | **GPIO 34** |
| Relé de aquecimento | IN | **GPIO 18** |
| Relé de irrigação | IN | **GPIO 19** |
| Relés | VCC / GND | 5V / GND |

O sensor de solo usa o **GPIO 34**, que pertence ao **ADC1** e continua funcionando com o Wi-Fi ligado, o que será necessário a partir do Checkpoint 2.

## ▶️ Como simular

**Online (mais rápido):** abra o projeto direto no Wokwi e clique em ▶️:
👉 https://wokwi.com/projects/477988258882690049

**A partir deste repositório:**

1. Acesse [wokwi.com](https://wokwi.com) e crie um novo projeto **ESP32 (Arduino)**.
2. Substitua o conteúdo de `sketch.ino` e `diagram.json` pelos arquivos deste repositório.
3. Adicione a biblioteca **DHT sensor library** (já listada em `libraries.txt`).
4. Inicie a simulação.
5. Para testar as regras, clique no DHT22 para alterar a temperatura e gire o potenciômetro para alterar a umidade do solo.

**No hardware real (Arduino IDE):**

1. Instale o suporte às placas ESP32 e a biblioteca **DHT sensor library** (Adafruit).
2. Ajuste as constantes da seção [Configuração](#️-configuração).
3. Selecione a placa *ESP32 Dev Module*, compile e grave.
4. Abra o Monitor Serial em **115200 baud**.

## ⚙️ Configuração

As principais opções ficam no início do `sketch.ino`:

```cpp
#define MODO_WOKWI 1           // 1 = Wokwi (potenciômetro) | 0 = sensor capacitivo real
#define RELE_ATIVO_EM_BAIXO 0  // 0 = relé liga com HIGH (Wokwi) | 1 = liga com LOW (maioria dos módulos reais)

const float LIMIAR_TEMPERATURA = 10.0;          // °C
const float LIMIAR_UMIDADE     = 35.0;          // %
const unsigned long INTERVALO_LEITURA_MS = 2000;
```

### Calibração do sensor de solo (hardware real)

O sensor capacitivo retorna valor **alto no seco** e **baixo no molhado**. Antes de usar com `MODO_WOKWI 0`:

1. Deixe o sensor no ar e anote o valor `ADC` mostrado no Monitor Serial → `ADC_SECO`.
2. Mergulhe o sensor em água e anote o valor → `ADC_MOLHADO`.
3. Atualize as constantes:

```cpp
const int ADC_SECO    = 3200;  // sensor no ar
const int ADC_MOLHADO = 1300;  // sensor mergulhado em água
```

## 🧪 Comandos de teste pelo Monitor Serial

Para validar os casos-limite sem depender dos sensores, é possível forçar valores pela serial:

| Comando | Efeito |
|---|---|
| `T=<valor>` | Força a temperatura (ex.: `T=10`) |
| `U=<valor>` | Força a umidade do solo (ex.: `U=35`) |
| `AUTO` | Volta a usar as leituras reais dos sensores |

Exemplo de saída:

```
=== Araucaria — acionamento com reles (Checkpoint 1) ===
Limiares: T < 10.0 °C -> aquecimento | Solo < 35.0 % -> irrigacao
Comandos de teste: T=10 | U=35 | AUTO
T= 15.0 °C | Solo= 50.0 % (ADC 2048) | Aquecimento=OFF | Irrigacao=OFF
> comando aceito: T=9.5
T=  9.5 °C | Solo= 50.0 % (ADC 2048) | Aquecimento=ON  | Irrigacao=OFF  [TESTE: valor forcado]
```

## 📁 Estrutura do repositório

```
.
└── Checkpoint 1 Copy/
    ├── sketch.ino          # Código do ESP32
    ├── diagram.json        # Circuito do Wokwi
    ├── libraries.txt       # Bibliotecas usadas na simulação
    └── wokwi-project.txt   # Link do projeto no Wokwi
```

## 🗺️ Roadmap

- [x] **Checkpoint 1** — leitura dos sensores e acionamento com módulos relé
- [ ] **Checkpoint 2** — conectividade Wi-Fi e envio dos dados via MQTT
- [ ] **Checkpoint 3** — painel de monitoramento com Node-RED

## 👥 Autores

- **Sofya Bruzadelli Borges Seidler de Andrade**
- **Matheus Ferrari dos Santos**

UNIDAVI — Internet das Coisas
