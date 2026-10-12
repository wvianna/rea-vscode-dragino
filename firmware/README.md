# Firmware próprio para o LSN50v2 / LoRa ST (STM32L072CZT6)

Reimplementação, para o **mesmo MCU** do nó Dragino LSN50, da funcionalidade
levantada em `docs/firmware/LSN50_AU915_v1.8.1-analise.md`: motor de comandos
AT, configuração persistente em EEPROM, aquisição de sensores com payloads
MOD1..MOD6, rádio SX1276 + LoRaWAN Class A (ABP/OTAA) e console serial.

## Estrutura

```
firmware/
  platformio.ini          dois ambientes: l072 (alvo) e native (testes)
  boards/lsn50v2.json     definição de placa (STM32L072CZ, 192K/20K, CMSIS)
  core/                   núcleo PORTÁTIL (sem header de MCU; testado no PC)
    at.[ch]               motor AT: tabela de descritores + parser + respostas
    config.[ch]           configuração, defaults, codec little-endian, CRC-16
    payload.[ch]          MOD1..MOD6 (codificador e decodificador)
    crypto.[ch]           AES-128 (FIPS-197) e AES-CMAC (RFC 4493)
  src/                    código do alvo (CMSIS, sem HAL)
    board.h               mapa de pinos + evidência de cada um
    hw.[ch]               clock, GPIO, USART, SPI, I2C, ADC, 1-Wire, EEPROM, RTC/STOP, EXTI
    console.[ch]          printf próprio (%d %u %x %s %c %f) e log dos sensores
    sensors.[ch]          SHT2x/SHT3x/BH1750, DS18B20, ADC, HX711, distância, interrupções
    at_cmds.[ch]          tabela AT do nó (60 comandos) ligada ao core
    sx1276.[ch]           driver do rádio por SPI1
    lorawan.[ch]          join OTAA, uplink, janelas RX1/RX2, downlink
    main.c                máquina de estados do fluxo do manual §2.1
  tests/test_core.c       testes de host do núcleo
```

## Build e testes

```bash
cd firmware
pio run -e l072              # firmware.elf/.bin do alvo
pio run -e native -t exec    # testes de host do núcleo portátil
```

Estado verificado nesta sessão:

- alvo: **0 erros, 0 avisos**, 40 592 B de flash (20,6 % de 192 KB) e 1 688 B
  de RAM (8,2 % de 20 KB);
- host: **88 verificações, 0 falhas** — AES-128 contra o vetor do FIPS-197
  (cifra e decifra), AES-CMAC contra os quatro vetores do RFC 4493, codec de
  configuração (ida/volta, CRC inválido, reset de fábrica preservando chaves),
  layouts MOD1..MOD6 (bateria 2885 mV → `0x0B45`, temperatura 26,1 °C →
  `0x0105`, −19,3 °C → `0xFF3F`) e o parser AT (formato de resposta, faixa de
  valor, comando desconhecido, `ATZ`, overflow de linha).

## De onde veio cada decisão

| Implementação | Evidência |
|---|---|
| Alvo STM32L072CZ, 192 KB/20 KB | manual §1.2 + `datasheets/STM32L072CZT6…pdf` |
| Tabela AT com nome/id/ajuda/get/set | descritores de 24 B em `0x08013E34` do hex |
| Resposta `<valor><CR><LF><CR><LF><Status><CR><LF>` | "Dragino LoRa AT Command Sets" §1.1 |
| Strings `AT_ERROR`, `AT_PARAM_ERROR`, … | 0x08014F5A–0x08014FC2 |
| EEPROM de dados em `0x08080000` | constantes `0x08080000/4/58/60` no hex + mapa do STM32L0 |
| `AT+FDR` preserva chaves | texto "Reset Parameters to Factory Default, **Keys Reserve**" |
| Payloads e escalas MOD1..MOD6 | manual §2.3 e §2.4 |
| `+5V` ligado em nível baixo, LED em PA8, I2C em PB6/PB7, ADC em PA0/PA1/PA4, DS18B20 em PB3/PA9/PA10, INTMOD1/2/3 em PB14/PB15/PA4, dreno PB4 | esquemático v2.0 (renderizado em `docs/firmware/`) + manual §1.5/§2.4.4 |
| Console a 9600 8N1 | manual §1.1 |
| Bateria lida por `VREFINT` (VDDA = 3,000 V × CAL/dado) | canais internos `ADC_IN17`/`ADC_IN18` e `VREFINT_CAL` em `0x1FF80078` (datasheet §6.3.3) |
| AES/CMAC próprios | LoRaWAN 1.0.2 exige AES-CMAC para MIC e AES-ECB para a cifra do payload |
| Plano de canais AU915 (915,2 MHz + 200 kHz × canal; RX2 923,3 MHz) | tabela `AT+CHE` do documento de comandos §5.19 |

## Uso

```text
AT                      -> OK
AT+VER=?                -> v0.1.0 AU915
AT+DEUI=00 44 34 22 33 45 55 55
AT+APPKEY=00 35 55 55 22 23 55 53 43 24 23 42 34 35 35 35
AT+NJM=1                -> OTAA
AT+JOIN                 -> Join request... / Joined
AT+MODE...              -> AT+MOD=4 (3× DS18B20)
AT+TDC=300000           -> uplink a cada 5 min
AT+GETSENSORVALUE       -> imprime a amostra no console
AT+CFG                  -> despeja toda a configuração
ATZ                     -> reinicia o MCU
```

Configuração por downlink: na porta de aplicação o texto recebido é executado
como comando AT; na porta 0 são aceitos os comandos MAC documentados para o
LSN50 (manual §2.5.2): `0x0A aa` = `AT+MOD`, `0x06 …bb` = `AT+INTMOD`,
`0x07 aa bb` = `AT+5VT`, `0x08 01` = `AT+WEIGRE`, `0x08 02 aa bb` = `AT+WEIGAP`.

### Decodificar o payload no servidor

`tools/decode_payload.py` é o espelho em Python de `core/payload.c` (mesmas
escalas, mesmos modos) e traz autoverificação:

```bash
python3 tools/decode_payload.py --selftest
python3 tools/decode_payload.py --mode 1 450B05011F0203EB001D02
# battery_mv: 2885 / temp_ds18b20_C: 26.1 / adc_pa0_mv: 543  (exemplos do manual §2.4)
```

## Comandos AT implementados

Os 60 nomes abaixo reproduzem os do v1.8.1 (mesmos ids e textos de ajuda). Os
que estão marcados com ✱ foram implementados como *flags* de configuração sem
efeito de rede (o original responde `1`/`OK`; aqui eles são persistidos e
consultáveis, mas não alteram o comportamento do MAC).

`Z`, `+FDR`, `+VER`, `+CFG`, `+DEUI`, `+APPEUI`, `+APPKEY`, `+NWKSKEY`,
`+APPSKEY`, `+DADDR`, `+NWKID`, `+NJM`, `+NJS`, `+JOIN`, `+SEND`, `+SENDB`,
`+RECV`, `+RECVB`, `+ADR`, `+DR`, `+TXP`, `+CLASS`, `+DCS`, `+PNM`, `+DWELLT`✱,
`+RPL`✱, `+DECRYPT`✱, `+DISMACANS`✱, `+DISFCNTCHECK`✱, `+DDETECT`✱,
`+RXDATEST`✱, `+SETMAXNBTRANS`, `+FCU`, `+FCD`, `+CFM`, `+CFS`, `+TDC`, `+RJTDC`,
`+RX1DL`, `+RX2DL`, `+JN1DL`, `+JN2DL`, `+RX1WTO`, `+RX2WTO`, `+RX2DR`, `+RX2FQ`,
`+CHS`, `+CHE`, `+PORT`, `+MOD`, `+INTMOD1`, `+INTMOD2`, `+INTMOD3`, `+5VT`,
`+WEIGAP`, `+WEIGRE`, `+SETCNT`, `+GETSENSORVALUE`, `+DEBUG`, `+RSSI`, `+SNR`.

## Pendências de bancada (não verificáveis sem hardware)

- **P-RADIO** — NSS/RESET/DIO0 do SX1276 são **suposições**
  (`src/board.h`): o rádio fica dentro do módulo M.2 e o esquemático do acervo
  cobre só a placa portadora. Os pinos SPI (PA5/PA6/PA7) são a escolha natural
  para o SPI1, que é o único SPI usado pelo firmware original.
- **P-SENSORES** — pinos do HX711 e do ultrassom também são suposições; o
  TF-Luna usa a USART1 (PA9/PA10) por inferência (§7 do relatório).
- **P-CONSOLE** — implementado em **USART2** (PA2/PA3), como diz o manual
  §1.5; o firmware v1.8.1 analisado tem handler real só na USART1. Troque
  `BOARD_CONSOLE_USART`/`BOARD_CONSOLE_IRQn` em `src/board.h` se a bancada
  mostrar o contrário.
- **P-CLOCK** — o padrão é HSI16 (valor de reset, seguro). `BOARD_USE_PLL`
  habilita os 32 MHz declarados no manual; antes de usar em campo, conferir
  VOS/latência de flash no osciloscópio.
- **P-IWDG** — `hw_iwdg_init()` existe mas **não é chamada**: o IWDG continua
  rodando durante o STOP e reiniciaria o nó a cada `AT+TDC` longo.
- **P-TDC** — o `AT+TDC` é cumprido com o RTC (resolução de 1 s, base LSI
  ~37 kHz nominal) mais um complemento em milissegundos com o MCU acordado.
  A precisão depende do LSI; para uso em produção, calibrar ou usar LSE.
- **P-TESTE** — nada foi executado em silício: o que está verificado é build,
  link e lógica do núcleo portátil. Comunicação LoRaWAN real, EEPROM, STOP mode
  e temporização dos sensores precisam de validação em bancada.
