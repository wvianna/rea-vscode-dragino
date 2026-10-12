/*
 * hw.c - implementacao bare-metal (CMSIS) para o STM32L072.
 *
 * Todas as decisoes de relogio partem de HSI16 (valor de reset, portanto
 * seguro). BOARD_USE_PLL sobe para 32 MHz, o valor declarado no manual do
 * LSN50, mas exige revisao do VOS/latencia de flash antes de usar em campo.
 */
#include "hw.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Estado interno                                                            */
/* ------------------------------------------------------------------------ */
static volatile uint32_t g_ms;
static volatile bool g_from_stop;
static volatile uint32_t g_exti_count[3];
static volatile uint8_t g_exti_level;
static volatile uint32_t g_exti_take[3];

void SysTick_Handler(void)
{
    g_ms++;
}

/* ------------------------------------------------------------------------ */
/* Relogio e tempo                                                           */
/* ------------------------------------------------------------------------ */

void hw_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;

    /* Range 1 (max 32 MHz) com 1 wait state: valido em qualquer frequencia
     * <= 32 MHz, inclusive com o HSI16 de reset. */
    PWR->CR = (PWR->CR & ~PWR_CR_VOS_Msk) | PWR_CR_VOS_0;
    FLASH->ACR |= FLASH_ACR_LATENCY;

#ifdef BOARD_USE_PLL
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0) {
    }
    /* PLL: HSI16 / 2 * 4 = 32 MHz */
    RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL_Msk | RCC_CFGR_PLLDIV_Msk);
    RCC->CFGR |= (RCC_CFGR_PLLMUL_0 | RCC_CFGR_PLLDIV_0);
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0) {
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL) {
    }
#endif

    /* GPIOs */
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;
    (void)RCC->IOPENR;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;

    /* Timebase de 1 us com TIM21 (16 bits, wrap a cada 65 ms) */
    RCC->APB2ENR |= RCC_APB2ENR_TIM21EN;
    TIM21->PSC = (uint16_t)((BOARD_SYSCLK_HZ / 1000000u) - 1u);
    TIM21->ARR = 0xFFFFu;
    TIM21->EGR = TIM_EGR_UG;
    TIM21->CR1 = TIM_CR1_CEN;

    SysTick_Config(BOARD_SYSCLK_HZ / 1000u);
}

uint32_t hw_millis(void)
{
    return g_ms;
}

uint32_t hw_micros(void)
{
    return (uint32_t)TIM21->CNT;
}

void hw_delay_us(uint32_t us)
{
    uint16_t start = (uint16_t)TIM21->CNT;
    if (us == 0) {
        return;
    }
    if (us > 60000u) {
        /* acima do periodo do contador, divide em blocos */
        while (us > 60000u) {
            hw_delay_us(60000u);
            us -= 60000u;
        }
    }
    while ((uint16_t)((uint16_t)TIM21->CNT - start) < (uint16_t)us) {
    }
}

void hw_delay_ms(uint32_t ms)
{
    uint32_t start = g_ms;
    while ((uint32_t)(g_ms - start) < ms) {
    }
}

void hw_system_reset(void)
{
    NVIC_SystemReset();
}

/* ------------------------------------------------------------------------ */
/* GPIO                                                                      */
/* ------------------------------------------------------------------------ */

static void gpio_clock(GPIO_TypeDef *port)
{
    (void)port; /* os clocks de GPIOA/GPIOB/GPIOC ja' sao ligados em hw_init */
}

void hw_gpio_cfg_output(GPIO_TypeDef *port, uint8_t pin, bool level)
{
    gpio_clock(port);
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
    port->OTYPER &= ~(1u << pin);
    port->OSPEEDR &= ~(3u << (pin * 2u));
    hw_gpio_write(port, pin, level);
}

void hw_gpio_cfg_input(GPIO_TypeDef *port, uint8_t pin, uint8_t pupd)
{
    gpio_clock(port);
    port->MODER &= ~(3u << (pin * 2u));
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | ((uint32_t)pupd << (pin * 2u));
}

void hw_gpio_cfg_af(GPIO_TypeDef *port, uint8_t pin, uint8_t af, uint8_t pupd)
{
    gpio_clock(port);
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (2u << (pin * 2u));
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | ((uint32_t)pupd << (pin * 2u));
    port->OTYPER &= ~(1u << pin);
    uint32_t idx = pin / 8u;
    uint32_t pos = (pin % 8u) * 4u;
    port->AFR[idx] = (port->AFR[idx] & ~(0xFu << pos)) | ((uint32_t)af << pos);
}

void hw_gpio_cfg_analog(GPIO_TypeDef *port, uint8_t pin)
{
    gpio_clock(port);
    port->MODER |= (3u << (pin * 2u));
    port->PUPDR &= ~(3u << (pin * 2u));
}

void hw_gpio_set(GPIO_TypeDef *port, uint8_t pin)
{
    port->BSRR = (uint32_t)(1u << pin);
}

void hw_gpio_clr(GPIO_TypeDef *port, uint8_t pin)
{
    port->BRR = (uint32_t)(1u << pin);
}

void hw_gpio_write(GPIO_TypeDef *port, uint8_t pin, bool level)
{
    if (level) {
        hw_gpio_set(port, pin);
    } else {
        hw_gpio_clr(port, pin);
    }
}

void hw_gpio_toggle(GPIO_TypeDef *port, uint8_t pin)
{
    port->ODR ^= (uint32_t)(1u << pin);
}

bool hw_gpio_read(GPIO_TypeDef *port, uint8_t pin)
{
    return (port->IDR & (1u << pin)) != 0;
}

void hw_led(bool on)
{
    hw_gpio_write(BOARD_LED_PORT, BOARD_LED_PIN, on);
}

void hw_led_blink(unsigned times)
{
    for (unsigned i = 0; i < times; i++) {
        hw_led(true);
        hw_delay_ms(50);
        hw_led(false);
        hw_delay_ms(50);
    }
}

void hw_5v(bool on)
{
    /* PB5 -> R22 -> gate do PMOS: LOW liga o +5V (manual 1.5) */
    hw_gpio_write(BOARD_5V_PORT, BOARD_5V_PIN, !on);
}

/* ------------------------------------------------------------------------ */
/* Console UART                                                              */
/* ------------------------------------------------------------------------ */

#define CONSOLE_RX_BUF 256u
static volatile uint8_t g_rx_buf[CONSOLE_RX_BUF];
static volatile uint16_t g_rx_head;
static volatile uint16_t g_rx_tail;

void hw_console_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;

    hw_gpio_cfg_af(BOARD_CONSOLE_PORT, BOARD_CONSOLE_TX_PIN, 4u, HW_PUPD_UP);
    hw_gpio_cfg_af(BOARD_CONSOLE_PORT, BOARD_CONSOLE_RX_PIN, 4u, HW_PUPD_UP);

    USART_TypeDef *u = BOARD_CONSOLE_USART;
    u->CR1 = 0;
    u->BRR = (BOARD_SYSCLK_HZ + (BOARD_CONSOLE_BAUD / 2u)) / BOARD_CONSOLE_BAUD;
    u->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_EnableIRQ(BOARD_CONSOLE_IRQn);
}

void USART2_IRQHandler(void)
{
    USART_TypeDef *u = BOARD_CONSOLE_USART;
    if (u->ISR & USART_ISR_RXNE) {
        uint8_t c = (uint8_t)(u->RDR & 0xFFu);
        uint16_t next = (uint16_t)((g_rx_head + 1u) % CONSOLE_RX_BUF);
        if (next != g_rx_tail) {
            g_rx_buf[g_rx_head] = c;
            g_rx_head = next;
        }
    }
    if (u->ISR & USART_ISR_ORE) {
        (void)u->RDR; /* limpa overrun */
    }
}

bool hw_console_rx_pop(char *c)
{
    if (g_rx_tail == g_rx_head) {
        return false;
    }
    *c = (char)g_rx_buf[g_rx_tail];
    g_rx_tail = (uint16_t)((g_rx_tail + 1u) % CONSOLE_RX_BUF);
    return true;
}

void hw_console_tx(char c)
{
    USART_TypeDef *u = BOARD_CONSOLE_USART;
    while ((u->ISR & USART_ISR_TXE) == 0) {
    }
    u->TDR = (uint8_t)c;
}

void hw_console_write(const char *s)
{
    while (*s) {
        hw_console_tx(*s++);
    }
}

/* ------------------------------------------------------------------------ */
/* USART1 - TF-Luna / TF-Mini (MOD2)                                         */
/* ------------------------------------------------------------------------ */

#define TF_RX_BUF 64u
static volatile uint8_t g_tf_buf[TF_RX_BUF];
static volatile uint16_t g_tf_head;
static volatile uint16_t g_tf_tail;

void hw_tf_uart_init(uint32_t baud)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    hw_gpio_cfg_af(BOARD_TF_UART_PORT, BOARD_TF_TX_PIN, 4u, HW_PUPD_UP);
    hw_gpio_cfg_af(BOARD_TF_UART_PORT, BOARD_TF_RX_PIN, 4u, HW_PUPD_UP);

    USART_TypeDef *u = BOARD_TF_UART;
    u->CR1 = 0;
    u->BRR = (BOARD_SYSCLK_HZ + (baud / 2u)) / baud;
    u->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_EnableIRQ(BOARD_TF_IRQn);
}

void hw_tf_uart_deinit(void)
{
    USART_TypeDef *u = BOARD_TF_UART;
    u->CR1 = 0;
    NVIC_DisableIRQ(BOARD_TF_IRQn);
    RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN;
    g_tf_head = g_tf_tail = 0;
    /* devolve os pinos ao barramento 1-Wire (MOD4 usa PA9/PA10) */
    hw_gpio_cfg_input(BOARD_TF_UART_PORT, BOARD_TF_TX_PIN, HW_PUPD_UP);
    hw_gpio_cfg_input(BOARD_TF_UART_PORT, BOARD_TF_RX_PIN, HW_PUPD_UP);
}

void USART1_IRQHandler(void)
{
    USART_TypeDef *u = BOARD_TF_UART;
    if (u->ISR & USART_ISR_RXNE) {
        uint8_t c = (uint8_t)(u->RDR & 0xFFu);
        uint16_t next = (uint16_t)((g_tf_head + 1u) % TF_RX_BUF);
        if (next != g_tf_tail) {
            g_tf_buf[g_tf_head] = c;
            g_tf_head = next;
        }
    }
}

bool hw_tf_uart_rx_pop(uint8_t *b)
{
    if (g_tf_tail == g_tf_head) {
        return false;
    }
    *b = g_tf_buf[g_tf_tail];
    g_tf_tail = (uint16_t)((g_tf_tail + 1u) % TF_RX_BUF);
    return true;
}

/* ------------------------------------------------------------------------ */
/* SPI1 - radio SX1276                                                       */
/* ------------------------------------------------------------------------ */

void hw_spi_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    hw_gpio_cfg_af(BOARD_RADIO_SPI_PORT, BOARD_RADIO_SCK_PIN, 0u, HW_PUPD_NONE);
    hw_gpio_cfg_af(BOARD_RADIO_SPI_PORT, BOARD_RADIO_MISO_PIN, 0u, HW_PUPD_UP);
    hw_gpio_cfg_af(BOARD_RADIO_SPI_PORT, BOARD_RADIO_MOSI_PIN, 0u, HW_PUPD_NONE);
    hw_gpio_cfg_output(BOARD_RADIO_NSS_PORT, BOARD_RADIO_NSS_PIN, true);
    hw_gpio_cfg_output(BOARD_RADIO_RST_PORT, BOARD_RADIO_RST_PIN, true);
    hw_gpio_cfg_input(BOARD_RADIO_DIO0_PORT, BOARD_RADIO_DIO0_PIN, HW_PUPD_NONE);

    SPI_TypeDef *spi = BOARD_RADIO_SPI;
    spi->CR1 = 0;
    /* MSTR, software slave management (SSI alto), 8 bits, CPOL=0/CPHA=0,
     * prescaler 8 -> 2 MHz com SYSCLK de 16 MHz (SX1276 suporta ate' 10 MHz).
     * O SPI do STM32L0 nao tem FIFO configuravel (FRXTH/DS) como as familias
     * F3/L4; o formato de 8 bits e' o default (CR1.DFF = 0). */
    spi->CR2 = 0;
    spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_1;
    spi->CR1 |= SPI_CR1_SPE;
}

uint8_t hw_spi_xfer(uint8_t out)
{
    SPI_TypeDef *spi = BOARD_RADIO_SPI;
    while ((spi->SR & SPI_SR_TXE) == 0) {
    }
    *(volatile uint8_t *)&spi->DR = out;
    while ((spi->SR & SPI_SR_RXNE) == 0) {
    }
    return *(volatile uint8_t *)&spi->DR;
}

/* ------------------------------------------------------------------------ */
/* I2C1                                                                      */
/* ------------------------------------------------------------------------ */

#define I2C_TIMEOUT_LOOPS 200000u

void hw_i2c_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    (void)RCC->APB1ENR;

    hw_gpio_cfg_af(BOARD_I2C_PORT, BOARD_I2C_SCL_PIN, 1u, HW_PUPD_NONE);
    hw_gpio_cfg_af(BOARD_I2C_PORT, BOARD_I2C_SDA_PIN, 1u, HW_PUPD_NONE);

    I2C_TypeDef *i2c = BOARD_I2C;
    i2c->CR1 &= ~I2C_CR1_PE;
    i2c->TIMINGR = BOARD_I2C_TIMINGR;
    i2c->CR1 = I2C_CR1_PE;
}

static bool i2c_wait(I2C_TypeDef *i2c, uint32_t mask)
{
    uint32_t guard = I2C_TIMEOUT_LOOPS;
    while (guard--) {
        uint32_t sr = i2c->ISR;
        if (sr & I2C_ISR_NACKF) {
            i2c->ICR = I2C_ICR_NACKCF;
            return false;
        }
        if (sr & mask) {
            return true;
        }
    }
    return false;
}

static void i2c_stop(I2C_TypeDef *i2c)
{
    i2c->CR2 |= I2C_CR2_STOP;
    (void)i2c->ISR;
}

static bool i2c_start(I2C_TypeDef *i2c, uint8_t addr7, uint32_t nbytes, bool read)
{
    i2c->CR2 = (uint32_t)(addr7 << 1) | (nbytes << I2C_CR2_NBYTES_Pos) |
               I2C_CR2_START | (read ? I2C_CR2_RD_WRN : 0u);
    return i2c_wait(i2c, read ? I2C_ISR_RXNE : I2C_ISR_TXIS);
}

bool hw_i2c_write(uint8_t addr7, const uint8_t *data, size_t len)
{
    I2C_TypeDef *i2c = BOARD_I2C;
    size_t sent = 0;

    if (len == 0 || len > 255) {
        return false;
    }
    if (!i2c_start(i2c, addr7, (uint32_t)len, false)) {
        i2c_stop(i2c);
        return false;
    }
    while (sent < len) {
        i2c->TXDR = data[sent++];
        if (!i2c_wait(i2c, I2C_ISR_TXIS)) {
            i2c_stop(i2c);
            return false;
        }
    }
    if (!i2c_wait(i2c, I2C_ISR_TC)) {
        i2c_stop(i2c);
        return false;
    }
    i2c_stop(i2c);
    return true;
}

bool hw_i2c_read(uint8_t addr7, uint8_t *data, size_t len)
{
    I2C_TypeDef *i2c = BOARD_I2C;
    size_t got = 0;

    if (len == 0 || len > 255) {
        return false;
    }
    if (!i2c_start(i2c, addr7, (uint32_t)len, true)) {
        i2c_stop(i2c);
        return false;
    }
    while (got < len) {
        if (!i2c_wait(i2c, I2C_ISR_RXNE)) {
            i2c_stop(i2c);
            return false;
        }
        data[got++] = (uint8_t)i2c->RXDR;
    }
    i2c_stop(i2c);
    return true;
}

bool hw_i2c_probe(uint8_t addr7)
{
    I2C_TypeDef *i2c = BOARD_I2C;
    uint8_t dummy = 0;
    if (!i2c_start(i2c, addr7, 1u, false)) {
        i2c_stop(i2c);
        return false;
    }
    i2c->TXDR = dummy;
    bool ok = i2c_wait(i2c, I2C_ISR_TC);
    i2c_stop(i2c);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* ADC                                                                       */
/* ------------------------------------------------------------------------ */

#define ADC_CH_VREFINT 17u
#define ADC_CH_TS 18u

void hw_adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADCEN;
    (void)RCC->APB2ENR;
    /* No STM32L0 o clock do ADC e' derivado de PCLK (nao ha ADCSEL no
     * RCC_CCIPR como nas familias L4/G4). */

    hw_gpio_cfg_analog(BOARD_ADC_GPIO_PORT, BOARD_ADC_PIN0);
    hw_gpio_cfg_analog(BOARD_ADC_GPIO_PORT, BOARD_ADC_PIN1);
    hw_gpio_cfg_analog(BOARD_ADC_GPIO_PORT, BOARD_ADC_PIN2);

    ADC1->CR &= ~ADC_CR_ADEN;
    ADC1->CFGR1 = 0; /* 12 bits, single conversion */
    ADC1->CFGR2 = 0;
    ADC1_COMMON->CCR = ADC_CCR_VREFEN | ADC_CCR_TSEN;
    ADC1->SMPR = 7u << ADC_SMPR_SMP_Pos; /* 160,5 ciclos - alta impedancia */

    ADC1->CR |= ADC_CR_ADCAL;
    while (ADC1->CR & ADC_CR_ADCAL) {
    }
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    while ((ADC1->ISR & ADC_ISR_ADRDY) == 0) {
    }
}

uint16_t hw_adc_read_raw(uint8_t channel)
{
    ADC1->CHSELR = (uint32_t)(1u << channel);
    ADC1->ISR = ADC_ISR_EOC;
    ADC1->CR |= ADC_CR_ADSTART;
    uint32_t guard = 100000u;
    while ((ADC1->ISR & ADC_ISR_EOC) == 0 && guard--) {
    }
    uint16_t v = (uint16_t)(ADC1->DR & 0xFFFu);
    ADC1->ISR = ADC_ISR_EOC;
    return v;
}

uint32_t hw_adc_vdda_mv(void)
{
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) {
        acc += hw_adc_read_raw(ADC_CH_VREFINT);
    }
    uint32_t vrefint = acc / 8u;
    uint32_t cal = *BOARD_VREFINT_CAL_ADDR;
    if (vrefint == 0 || cal == 0 || cal == 0xFFFFu) {
        return BOARD_VREFINT_CAL_VDD_MV;
    }
    /* VDDA = 3,000 V * VREFINT_CAL / VREFINT_DATA */
    return (BOARD_VREFINT_CAL_VDD_MV * cal) / vrefint;
}

uint32_t hw_adc_pin_mv(uint8_t channel)
{
    uint32_t acc = 0;
    for (int i = 0; i < 4; i++) {
        acc += hw_adc_read_raw(channel);
    }
    uint32_t raw = acc / 4u;
    return (raw * hw_adc_vdda_mv()) / 4095u;
}

int16_t hw_adc_temp_x10(void)
{
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) {
        acc += hw_adc_read_raw(ADC_CH_TS);
    }
    uint32_t ts = acc / 8u;
    uint32_t cal1 = *BOARD_TS_CAL1_ADDR; /* 30 C */
    uint32_t cal2 = *BOARD_TS_CAL2_ADDR; /* 130 C */
    if (cal2 <= cal1) {
        return 0;
    }
    int32_t temp_x10 = 300 + (int32_t)(((ts - cal1) * 1000) / (cal2 - cal1));
    return (int16_t)temp_x10;
}

/* ------------------------------------------------------------------------ */
/* 1-Wire / DS18B20                                                          */
/* ------------------------------------------------------------------------ */

void ow_init(const ow_pin_t *p)
{
    /* saida em open-drain logico: alterna entrada (liberado, pull-up externo
     * de 4,7K no barramento) e saida em nivel baixo. */
    hw_gpio_cfg_input(p->port, p->pin, HW_PUPD_NONE);
}

static void ow_low(const ow_pin_t *p)
{
    hw_gpio_cfg_output(p->port, p->pin, false);
}

static void ow_release(const ow_pin_t *p)
{
    hw_gpio_cfg_input(p->port, p->pin, HW_PUPD_NONE);
}

bool ow_reset(const ow_pin_t *p)
{
    ow_low(p);
    hw_delay_us(480);
    ow_release(p);
    hw_delay_us(70);
    bool present = !hw_gpio_read(p->port, p->pin);
    hw_delay_us(410);
    return present;
}

void ow_write_byte(const ow_pin_t *p, uint8_t v)
{
    for (int bit = 0; bit < 8; bit++) {
        if (v & 0x01u) {
            ow_low(p);
            hw_delay_us(6);
            ow_release(p);
            hw_delay_us(64);
        } else {
            ow_low(p);
            hw_delay_us(60);
            ow_release(p);
            hw_delay_us(10);
        }
        v >>= 1;
    }
}

uint8_t ow_read_byte(const ow_pin_t *p)
{
    uint8_t v = 0;
    for (int bit = 0; bit < 8; bit++) {
        ow_low(p);
        hw_delay_us(3);
        ow_release(p);
        hw_delay_us(10);
        if (hw_gpio_read(p->port, p->pin)) {
            v |= (uint8_t)(1u << bit);
        }
        hw_delay_us(53);
    }
    return v;
}

bool ow_read_temp_x10(const ow_pin_t *p, int16_t *x10)
{
    uint8_t sp[9];

    if (!ow_reset(p)) {
        return false;
    }
    ow_write_byte(p, 0xCCu); /* SKIP ROM */
    ow_write_byte(p, 0x44u); /* CONVERT T */
    hw_delay_ms(750);        /* resolucao de 12 bits */

    if (!ow_reset(p)) {
        return false;
    }
    ow_write_byte(p, 0xCCu);
    ow_write_byte(p, 0xBEu); /* READ SCRATCHPAD */
    for (int i = 0; i < 9; i++) {
        sp[i] = ow_read_byte(p);
    }
    /* CRC-8 do Dallas (polinomio 0x8C refletido) sobre os 8 primeiros bytes */
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t b = sp[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (uint8_t)((crc ^ b) & 1u);
            crc = (uint8_t)(crc >> 1);
            if (mix) {
                crc ^= 0x8Cu;
            }
            b = (uint8_t)(b >> 1);
        }
    }
    if (crc != sp[8]) {
        return false;
    }
    int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
    /* LSB = 0,0625 C -> x10 = raw * 10 / 16 = raw * 5 / 8 */
    *x10 = (int16_t)((raw * 5) / 8);
    return true;
}

/* ------------------------------------------------------------------------ */
/* EEPROM de dados                                                           */
/* ------------------------------------------------------------------------ */

static bool flash_unlock(void)
{
    if ((FLASH->PECR & FLASH_PECR_PELOCK) == 0) {
        return true;
    }
    FLASH->PEKEYR = 0x89ABCDEFu;
    FLASH->PEKEYR = 0x02030405u;
    return (FLASH->PECR & FLASH_PECR_PELOCK) == 0;
}

static void flash_lock(void)
{
    FLASH->PECR |= FLASH_PECR_PELOCK;
}

static void flash_wait(void)
{
    while (FLASH->SR & FLASH_SR_BSY) {
    }
    FLASH->SR = FLASH_SR_EOP;
}

bool hw_eeprom_read(uint16_t offset, void *dst, size_t len)
{
    if ((size_t)offset + len > BOARD_EEPROM_SIZE) {
        return false;
    }
    memcpy(dst, (const void *)(BOARD_EEPROM_BASE + offset), len);
    return true;
}

bool hw_eeprom_write(uint16_t offset, const void *src, size_t len)
{
    if ((size_t)offset + len > BOARD_EEPROM_SIZE) {
        return false;
    }
    if (!flash_unlock()) {
        return false;
    }

    const uint8_t *p = (const uint8_t *)src;
    bool ok = true;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t word = 0;
        size_t chunk = (len - i >= 4u) ? 4u : (len - i);
        memcpy(&word, p + i, chunk);
        volatile uint32_t *addr = (volatile uint32_t *)(BOARD_EEPROM_BASE + offset + i);

        /* Apaga a palavra e grava o novo valor (a EEPROM so' muda 1 -> 0). */
        FLASH->PECR |= FLASH_PECR_ERASE | FLASH_PECR_DATA;
        *addr = 0;
        flash_wait();
        FLASH->PECR &= ~FLASH_PECR_ERASE;

        FLASH->PECR |= FLASH_PECR_DATA;
        *addr = word;
        flash_wait();
        FLASH->PECR &= ~FLASH_PECR_DATA;

        if (*addr != word) {
            ok = false;
            break;
        }
    }
    flash_lock();
    return ok;
}

/* ------------------------------------------------------------------------ */
/* RTC + STOP mode                                                           */
/* ------------------------------------------------------------------------ */

void hw_rtc_init(void)
{
    /* LSI + RTC no dominio de backup */
    PWR->CR |= PWR_CR_DBP;
    RCC->CSR |= RCC_CSR_LSION;
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0) {
    }
    RCC->CSR = (RCC->CSR & ~RCC_CSR_RTCSEL_Msk) | RCC_CSR_RTCSEL_LSI;
    RCC->CSR |= RCC_CSR_RTCEN;
    while ((RCC->CSR & RCC_CSR_RTCEN) == 0) {
    }

    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;

    RTC->ISR |= RTC_ISR_INIT;
    while ((RTC->ISR & RTC_ISR_INITF) == 0) {
    }
    /* ck_spre = LSI / (PREDIV_A+1) / (PREDIV_S+1) = 37000/128/289 ~ 1 Hz */
    RTC->PRER = (127u << 16) | 288u;
    RTC->ISR &= ~RTC_ISR_INIT;

    /* WUT com base ck_spre (1 Hz); RTC_IRQn acorda do STOP */
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    while ((RTC->ISR & RTC_ISR_WUTWF) == 0) {
    }
    NVIC_EnableIRQ(RTC_IRQn);
    g_from_stop = false;
}

void RTC_IRQHandler(void)
{
    if (RTC->ISR & RTC_ISR_WUTF) {
        RTC->ISR &= ~RTC_ISR_WUTF;
        g_from_stop = true;
    }
}

void hw_sleep_seconds(uint32_t seconds)
{
    if (seconds == 0) {
        return;
    }
    if (seconds > 0xFFFFu) {
        seconds = 0xFFFFu;
    }

    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;
    RTC->CR &= ~RTC_CR_WUTE;
    while ((RTC->ISR & RTC_ISR_WUTWF) == 0) {
    }
    RTC->WUTR = (uint32_t)seconds; /* ck_spre = 1 Hz */
    RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL_Msk) | RTC_CR_WUCKSEL_2; /* ck_spre */
    RTC->CR |= RTC_CR_WUTIE | RTC_CR_WUTE;
    RTC->WPR = 0xFFu; /* re-lock */

    PWR->CR |= PWR_CR_CWUF;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    PWR->CR &= ~PWR_CR_PDDS;  /* STOP, nao standby */
    PWR->CR |= PWR_CR_LPSDSR | PWR_CR_ULP | PWR_CR_FWU;
    __DSB();
    __WFI();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    RTC->WPR = 0xFFu;
    g_from_stop = true;
}

bool hw_woke_from_stop(void)
{
    return g_from_stop;
}

void hw_woke_from_stop_clear(void)
{
    g_from_stop = false;
}

/* ------------------------------------------------------------------------ */
/* EXTI - contagem de interrupcoes                                           */
/* ------------------------------------------------------------------------ */

/* SYSCFG_EXTICR: 0 = PA, 1 = PB, 2 = PC, 3 = PD, 5 = PH */
static uint8_t port_index(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        return 0;
    }
    if (port == GPIOB) {
        return 1;
    }
    if (port == GPIOC) {
        return 2;
    }
    return 0;
}

static void exti_line_source(uint8_t line, GPIO_TypeDef *port)
{
    uint32_t idx = line / 4u;
    uint32_t pos = (line % 4u) * 4u;
    SYSCFG->EXTICR[idx] = (SYSCFG->EXTICR[idx] & ~(0xFu << pos)) |
                          ((uint32_t)port_index(port) << pos);
}

void hw_exti_init(void)
{
    hw_gpio_cfg_input(BOARD_INT1_PORT, BOARD_INT1_PIN, HW_PUPD_UP);
    hw_gpio_cfg_input(BOARD_INT2_PORT, BOARD_INT2_PIN, HW_PUPD_UP);
    hw_gpio_cfg_input(BOARD_INT3_PORT, BOARD_INT3_PIN, HW_PUPD_UP);

    exti_line_source(14u, BOARD_INT1_PORT);
    exti_line_source(15u, BOARD_INT2_PORT);
    exti_line_source(4u, BOARD_INT3_PORT);

    NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void hw_exti_config(uint8_t idx, uint8_t mode)
{
    uint32_t line = 0;
    switch (idx) {
    case HW_INT1:
        line = 14u;
        break;
    case HW_INT2:
        line = 15u;
        break;
    case HW_INT3:
        line = 4u;
        break;
    default:
        return;
    }

    uint32_t bit = 1u << line;
    EXTI->IMR &= ~bit;
    EXTI->EMR &= ~bit;
    EXTI->RTSR &= ~bit;
    EXTI->FTSR &= ~bit;

    if (mode == 0) {
        return;
    }
    if (mode == 1 || mode == 3) {
        EXTI->RTSR |= bit;
    }
    if (mode == 1 || mode == 2) {
        EXTI->FTSR |= bit;
    }
    EXTI->IMR |= bit;
}

void EXTI4_15_IRQHandler(void)
{
    uint32_t pending = EXTI->PR;
    if (pending & (1u << 14)) {
        EXTI->PR = (1u << 14);
        g_exti_count[0]++;
        g_exti_take[0]++;
    }
    if (pending & (1u << 15)) {
        EXTI->PR = (1u << 15);
        g_exti_count[1]++;
        g_exti_take[1]++;
    }
    if (pending & (1u << 4)) {
        EXTI->PR = (1u << 4);
        g_exti_count[2]++;
        g_exti_take[2]++;
    }
}

uint32_t hw_exti_count(uint8_t idx)
{
    return (idx < 3) ? g_exti_count[idx] : 0;
}

uint32_t hw_exti_take_count(uint8_t idx)
{
    if (idx >= 3) {
        return 0;
    }
    uint32_t v = g_exti_take[idx];
    g_exti_take[idx] = 0;
    return v;
}

bool hw_exti_seen_level(uint8_t idx)
{
    switch (idx) {
    case HW_INT1:
        return hw_gpio_read(BOARD_INT1_PORT, BOARD_INT1_PIN);
    case HW_INT2:
        return hw_gpio_read(BOARD_INT2_PORT, BOARD_INT2_PIN);
    case HW_INT3:
        return hw_gpio_read(BOARD_INT3_PORT, BOARD_INT3_PIN);
    default:
        return false;
    }
}

/* ------------------------------------------------------------------------ */
/* Watchdog                                                                  */
/* ------------------------------------------------------------------------ */

void hw_iwdg_init(uint32_t timeout_ms)
{
    /* LSI ~37 kHz. Prescaler 256 -> ~144 Hz, entao 1 contagem ~ 6,9 ms.
     * ATENCAO: o IWDG continua rodando no STOP mode; nao habilite se o
     * intervalo de uplink (AT+TDC) for maior que o timeout. */
    uint32_t ticks = (timeout_ms * BOARD_LSI_HZ) / (1000u * 256u);
    if (ticks == 0) {
        ticks = 1;
    }
    if (ticks > 0xFFFu) {
        ticks = 0xFFFu;
    }
    IWDG->KR = 0x5555u; /* desbloqueia PR e RLR */
    IWDG->PR = 7u;      /* /256 */
    IWDG->RLR = (uint16_t)ticks;
    IWDG->KR = 0xAAAAu; /* recarrega */
    IWDG->KR = 0xCCCCu; /* liga */
}

void hw_iwdg_feed(void)
{
    IWDG->KR = 0xAAAAu;
}
