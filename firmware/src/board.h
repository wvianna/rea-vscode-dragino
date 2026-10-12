/*
 * board.h - mapa de pinos da placa alvo (LSN50v2 / "LoRa ST Sensor Node v2.0").
 *
 * FONTES DE EVIDENCIA
 *  - Node-Dragino - LSN50v2/LoRa_ST_Sensor_Node_v2.0.sch.pdf  (esquematico,
 *    pagina unica; conferido visualmente em 2026-10-12)
 *  - Node-Dragino - LSN50v2/LSN50_LoRa_Sensor_Node_UserManual_v1.7.4.md
 *    (secao 1.5 "Pin Definitions and Switch", 2.3 "Working Mode", 2.4)
 *
 * Confirmado pelo esquematico/manual:
 *   PA0/PA1           - entrada ADC do conector JP3 (ADC1/ADC2 no image)
 *   PA2/PA3           - USART2_TX / USART2_RX (console AT, 9600 8N1)
 *   PA4               - ADC3 (MOD3) e INTMOD3 (interrupcao)
 *   PA8               - LED1 (R18 2K -> LED verde -> GND)     [secao 4]
 *   PA9/PA10          - USART1 (bootloader) e T2/T3 do 1-Wire (MOD4)
 *   PA12              - entrada digital (bit do payload)
 *   PB3               - T1 do DS18B20 (MOD4)
 *   PB4               - dreno do sensor resistivo (manual 2.4.4, "PB4 will be
 *                       set to low (0v) at every sampling")
 *   PB5               - habilitacao do +5V: R22 -> gate do PMOS T1; R25 100K
 *                       faz pull-up para VDD, logo LOW = 5V LIGADO
 *   PB6/PB7           - I2C1_SCL / I2C1_SDA (SHT2x/SHT3x/BH1750)
 *   PB14 / PB15       - INTMOD1 / INTMOD2 (linhas com ESD D5/D6 e pull-up 10K)
 *
 * NAO CONFIRMADO (o radio SX1276 fica DENTRO do modulo M.2 "NGFF_M2_KEY_E_SHIELD"
 * identificado como U1; o esquematico disponivel e' apenas o da placa portadora,
 * portanto as linhas SPI1/NSS/RESET/DIO nao aparecem nele):
 *   SCK/MISO/MOSI = PA5/PA6/PA7 (funcao alternativa AF0 de SPI1 - escolha
 *   coerente com o unico SPI habilitado no firmware original, base 0x40013000)
 *   NSS  = PA15 (AF0 de SPI1)
 *   RESET/DIO0 = PB13 / PB12 - ESCOLHA ARBITRARIA, ajustar na placa real.
 * Ver pendencia P-RADIO em firmware/README.md.
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l0xx.h"

#define BOARD_NAME "LSN50v2-compat"

/* ------------------------------------------------------------------------ */
/* Relogio e tempos                                                          */
/* ------------------------------------------------------------------------ */
/* Default seguro: HSI16 (o valor de reset, sempre disponivel). Defina
 * BOARD_USE_PLL para subir para 32 MHz como no LSN50 (manual 1.2 "Clock
 * Speed: 32Mhz"); o PLL e' HSI16/2 * 4. */
/* #define BOARD_USE_PLL 1 */
#define BOARD_HSI16_HZ 16000000u
#define BOARD_PLL_HZ 32000000u

#ifdef BOARD_USE_PLL
#define BOARD_SYSCLK_HZ BOARD_PLL_HZ
#else
#define BOARD_SYSCLK_HZ BOARD_HSI16_HZ
#endif

/* LSI tipico do STM32L0 ~= 37 kHz (datasheet). Usado so' para converter o
 * tempo do wakeup do RTC; o valor nominal tem tolerancia de fabrica. */
#define BOARD_LSI_HZ 37000u

/* ------------------------------------------------------------------------ */
/* Console (AT) - USART2 em PA2/PA3                                          */
/* ------------------------------------------------------------------------ */
#define BOARD_CONSOLE_USART USART2
#define BOARD_CONSOLE_IRQn USART2_IRQn
#define BOARD_CONSOLE_BAUD 9600u
#define BOARD_CONSOLE_PORT GPIOA
#define BOARD_CONSOLE_TX_PIN 2u
#define BOARD_CONSOLE_RX_PIN 3u

/* ------------------------------------------------------------------------ */
/* LED e alimentacao de sensores                                             */
/* ------------------------------------------------------------------------ */
#define BOARD_LED_PORT GPIOA
#define BOARD_LED_PIN 8u

#define BOARD_5V_PORT GPIOB
#define BOARD_5V_PIN 5u

/* ------------------------------------------------------------------------ */
/* I2C (SHT2x / SHT3x / BH1750)                                              */
/* ------------------------------------------------------------------------ */
#define BOARD_I2C I2C1
#define BOARD_I2C_PORT GPIOB
#define BOARD_I2C_SCL_PIN 6u
#define BOARD_I2C_SDA_PIN 7u
/* TIMINGR para 100 kHz com f_I2CCLK = 16 MHz (PRESC=0, SCLDEL=19, SDADEL=2,
 * SCLH=79, SCLL=79 -> 5 us de nivel baixo e 5 us de nivel alto). */
#define BOARD_I2C_TIMINGR 0x13204F4Fu

/* ------------------------------------------------------------------------ */
/* ADC (PA0, PA1, PA4) e referencia interna                                  */
/* ------------------------------------------------------------------------ */
#define BOARD_ADC_GPIO_PORT GPIOA
#define BOARD_ADC_PIN0 0u /* PA0 - ADC_IN0 */
#define BOARD_ADC_PIN1 1u /* PA1 - ADC_IN1 */
#define BOARD_ADC_PIN2 4u /* PA4 - ADC_IN4 */
#define BOARD_ADC_CH0 0u
#define BOARD_ADC_CH1 1u
#define BOARD_ADC_CH2 4u
/* Endereco dos valores de calibracao medidos em fabrica - datasheet
 * STM32L072CZT6 (C1337587), secao 6.3.3 / tabela 28. */
#define BOARD_VREFINT_CAL_ADDR ((const uint16_t *)0x1FF80078u)
#define BOARD_TS_CAL1_ADDR ((const uint16_t *)0x1FF8007Au)
#define BOARD_TS_CAL2_ADDR ((const uint16_t *)0x1FF8007Eu)
#define BOARD_VREFINT_CAL_VDD_MV 3000u

/* ------------------------------------------------------------------------ */
/* 1-Wire / DS18B20 (MOD1 usa PB3; MOD4 usa PB3, PA9, PA10)                   */
/* ------------------------------------------------------------------------ */
#define BOARD_OW1_PORT GPIOB
#define BOARD_OW1_PIN 3u
#define BOARD_OW2_PORT GPIOA
#define BOARD_OW2_PIN 9u
#define BOARD_OW3_PORT GPIOA
#define BOARD_OW3_PIN 10u

/* ------------------------------------------------------------------------ */
/* Entradas digitais e de interrupcao                                        */
/* ------------------------------------------------------------------------ */
#define BOARD_DIN_PORT GPIOA
#define BOARD_DIN_PIN 12u

#define BOARD_INT1_PORT GPIOB /* INTMOD1 - PB14 */
#define BOARD_INT1_PIN 14u
#define BOARD_INT2_PORT GPIOB /* INTMOD2 - PB15 */
#define BOARD_INT2_PIN 15u
#define BOARD_INT3_PORT GPIOA /* INTMOD3 - PA4  */
#define BOARD_INT3_PIN 4u

/* Pino usado como dreno comutado para sensores resistivos (manual 2.4.4). */
#define BOARD_SENSGND_PORT GPIOB
#define BOARD_SENSGND_PIN 4u

/* ------------------------------------------------------------------------ */
/* Radio SX1276 (ver ressalva no topo do arquivo)                            */
/* ------------------------------------------------------------------------ */
#define BOARD_RADIO_SPI SPI1
#define BOARD_RADIO_SPI_PORT GPIOA
#define BOARD_RADIO_SCK_PIN 5u
#define BOARD_RADIO_MISO_PIN 6u
#define BOARD_RADIO_MOSI_PIN 7u
#define BOARD_RADIO_NSS_PORT GPIOA
#define BOARD_RADIO_NSS_PIN 15u
#define BOARD_RADIO_RST_PORT GPIOB /* ASSUMIDO */
#define BOARD_RADIO_RST_PIN 13u    /* ASSUMIDO */
#define BOARD_RADIO_DIO0_PORT GPIOB /* ASSUMIDO */
#define BOARD_RADIO_DIO0_PIN 12u    /* ASSUMIDO */
#define BOARD_RADIO_DIO0_EXTI 12u

/* ------------------------------------------------------------------------ */
/* EEPROM de dados (6 KB em 0x08080000 no STM32L0)                            */
/* ------------------------------------------------------------------------ *//* O firmware original referencia 0x08080000/0x08080004/0x08080058/0x08080060
 * (constantes de 32 bits presentes na imagem) - mesma regiao usada aqui. */
#define BOARD_EEPROM_BASE 0x08080000u
#define BOARD_EEPROM_SIZE 0x1800u /* 6 KB */
#define BOARD_CFG_OFFSET 0x0000u  /* configuracao principal */
#define BOARD_CFG_OFFSET_B 0x0040u /* copia de seguranca */

/* ------------------------------------------------------------------------ */
/* Sensores opcionais dos MOD2/MOD5 - PINOS ASSUMIDOS                        */
/* ------------------------------------------------------------------------ */
/* O manual (2.4.8, 2.4.10) descreve os sensores mas nao fixa os pinos; o
 * esquematico disponivel e' o da placa portadora e mostra apenas o conector
 * JP3/JP4. Escolhas abaixo precisam ser conferidas na bancada (pendencia
 * P-SENSORES em firmware/README.md). */
#define BOARD_HX711_SCK_PORT GPIOA /* ASSUMIDO */
#define BOARD_HX711_SCK_PIN 0u     /* ASSUMIDO */
#define BOARD_HX711_DOUT_PORT GPIOA /* ASSUMIDO */
#define BOARD_HX711_DOUT_PIN 1u     /* ASSUMIDO */

#define BOARD_ULT_TRIG_PORT GPIOA /* ASSUMIDO */
#define BOARD_ULT_TRIG_PIN 1u     /* ASSUMIDO */
#define BOARD_ULT_ECHO_PORT GPIOA /* ASSUMIDO */
#define BOARD_ULT_ECHO_PIN 0u     /* ASSUMIDO */

/* TF-Mini/TF-Luna (UART) usam a USART1 em PA9/PA10. Esta e' uma inferencia
 * apoiada em duas evidencias independentes: (a) o vetor de USART1 aponta para
 * um handler real no firmware v1.8.1, enquanto o de USART2 aponta para o
 * Default_Handler (0x080000e6); (b) o manual 2.3.2 declara suporte a
 * "TF-Mini plus / TF-Luna LiDAR (UART version)" com R3/R4 removidos - R3/R4
 * sao justamente os pull-ups de PA9/PA10 no esquematico. */
#define BOARD_TF_UART USART1
#define BOARD_TF_IRQn USART1_IRQn
#define BOARD_TF_UART_PORT GPIOA
#define BOARD_TF_TX_PIN 9u
#define BOARD_TF_RX_PIN 10u
#define BOARD_TF_BAUD 115200u

#define BOARD_LIDAR_I2C_ADDR 0x62u /* LIDAR-Lite v3HP (manual 2.4.8) */
#define BOARD_SHT2X_I2C_ADDR 0x40u
#define BOARD_SHT3X_I2C_ADDR 0x44u
#define BOARD_BH1750_I2C_ADDR 0x23u

#endif /* BOARD_H */
