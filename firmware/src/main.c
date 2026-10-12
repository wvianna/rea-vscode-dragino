/*
 * main.c - loop principal do no'.
 *
 * Fluxo equivalente ao diagrama "How it works" do manual (secao 2.1):
 *
 *   Power on -> (OTAA join | ABP) -> ciclo:
 *       Enable 5V -> Data acquisition -> Disable 5V -> Uplink packet (LED)
 *       -> Open receive window (RX1/RX2) -> Print out data (change config if
 *       needed) -> TDC timeout -> Enter stop mode -> Exit stop mode
 *
 * Nos modos com contagem (MOD6) a interrupcao incrementa o contador enquanto
 * o MCU esta' em STOP; o EXTI tambem e' fonte de wakeup.
 */
#include <stdbool.h>
#include <string.h>

#include "at.h"
#include "at_cmds.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "hw.h"
#include "lorawan.h"
#include "payload.h"
#include "sensors.h"
#include "sx1276.h"

static uint32_t g_last_uplink_ms;

static void banner(void)
{
    /* O original imprime "LSN50 Device / Image Version: v1.8.1 /
     * LoRaWan Stack: DR-LWS-007 / DevEui= ..." (strings em 0x0800828e). */
    console_puts("\r\n" BOARD_NAME "\r\n");
    console_printf("Image Version: %s\r\n", CFG_IMAGE_VERSION);
    console_printf("Frequency Band: %s\r\n", CFG_FREQ_BAND);
    console_printf("DevEui= %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   g_app.cfg.deveui[0], g_app.cfg.deveui[1], g_app.cfg.deveui[2],
                   g_app.cfg.deveui[3], g_app.cfg.deveui[4], g_app.cfg.deveui[5],
                   g_app.cfg.deveui[6], g_app.cfg.deveui[7]);
    console_printf("Work mode: MOD=%u\r\n", (unsigned)g_app.cfg.mode);
    console_printf("TDC=%lu ms\r\n", (unsigned long)g_app.cfg.tdc_ms);
    if (!lw_radio_ok()) {
        console_puts("AVISO: SX1276 nao respondeu no SPI (verifique\
 BOARD_RADIO_* em src/board.h)\r\n");
    }
    console_puts("\r\n");
}

/* Recebe as linhas AT que chegam pela UART; devolve quantos comandos rodaram. */
static unsigned at_service(uint32_t window_ms)
{
    uint32_t start = hw_millis();
    unsigned count = 0;
    do {
        char c;
        while (hw_console_rx_pop(&c)) {
            at_feed(at_cmds_context(), c);
            count++;
        }
        at_cmds_save_if_dirty();
    } while ((uint32_t)(hw_millis() - start) < window_ms);
    return count;
}

static void on_reset(void *user)
{
    (void)user;
    /* responde OK antes de reiniciar (ATZ) */
    hw_delay_ms(20);
    hw_system_reset();
}

static void do_join(void)
{
    console_puts("Join request...\r\n");
    if (lw_join(&g_app.cfg)) {
        console_puts("Joined\r\n");
        hw_led_blink(2);
        at_cmds_save_if_dirty();
    } else {
        console_puts("Join falhou\r\n");
    }
}

static void do_cycle(void)
{
    uint8_t payload[PAYLOAD_MAX];
    size_t len;
    sample_t sample;
    bool sample_ok;

    /* (1) ligar o 5V e esperar o tempo configurado por AT+5VT */
    sensors_power(true);
    hw_delay_ms(g_app.cfg.five_vt_ms);

    /* (2) aquisicao dos dados */
    sample_ok = sensors_read(&g_app.cfg, &sample);
    if (sample_ok) {
        at_cmds_set_sample(&sample);
    }

    /* (3) desligar o 5V (so' depois de amostrar - manual 2.8) */
    sensors_power(false);

    if (!sample_ok) {
        return;
    }

    len = at_cmds_build_payload(payload, sizeof(payload));
    if (len == 0) {
        console_puts("Payload vazio\r\n");
        return;
    }

    /* (4) uplink (LED pisca no envio, como no manual 1.5.4) */
    hw_led(true);
    bool sent = lw_uplink(&g_app.cfg, payload, len, g_app.cfg.port,
                          g_app.cfg.cfm != 0);
    hw_led(false);
    if (!sent) {
        console_puts("Falha no uplink\r\n");
        return;
    }
    g_last_uplink_ms = hw_millis();
    at_cmds_save_if_dirty();

    /* (5) janelas de recepcao e downlink */
    if (lw_receive_windows(&g_app.cfg, true)) {
        at_cmds_save_if_dirty();
    }
}

static void maybe_join(void)
{
    if (g_app.cfg.nj_mode == 0) { /* ABP: sessao ja' existe */
        lw_set_joined(true);
        return;
    }
    if (!lw_joined()) {
        do_join();
    }
}

int main(void)
{
    hw_init();
    console_init();

    memset(&g_app, 0, sizeof(g_app));
    at_cmds_init(&g_app);
    at_cmds_context()->on_reset = on_reset;

    sensors_init();
    sensors_int_apply(&g_app.cfg);

    (void)sx1276_init();
    lw_init(&g_app.cfg, at_cmd_apply_text);

    hw_rtc_init();
    banner();

    /* Com OTAA, tenta entrar na rede no boot (igual ao default do LSN50) */
    maybe_join();

    for (;;) {
        /* comandos AT pedidos pelo console */
        if (g_app.reboot) {
            g_app.reboot = false;
            hw_system_reset();
        }
        if (g_app.join_request) {
            g_app.join_request = false;
            do_join();
        }
        if (g_app.send_request) {
            g_app.send_request = false;
            hw_led(true);
            bool ok = lw_uplink(&g_app.cfg, g_app.tx_data, g_app.tx_len,
                                g_app.tx_port, g_app.cfg.cfm != 0);
            hw_led(false);
            if (ok) {
                lw_receive_windows(&g_app.cfg, true);
                g_last_uplink_ms = hw_millis();
            }
            at_cmds_save_if_dirty();
        }
        if (g_app.sample_request) {
            g_app.sample_request = false;
            sensors_power(true);
            hw_delay_ms(g_app.cfg.five_vt_ms);
            sample_t s;
            if (sensors_read(&g_app.cfg, &s)) {
                at_cmds_set_sample(&s);
                console_dump_sample(&s);
            }
            sensors_power(false);
        }

        at_cmds_save_if_dirty();

        /* ciclo periodico de TDC */
        uint32_t elapsed = hw_millis() - g_last_uplink_ms;
        if (elapsed >= g_app.cfg.tdc_ms) {
            maybe_join();
            do_cycle();
            continue;
        }

        /* espera ate' o proximo ciclo; acorda tambem nas interrupcoes.
         * O RTC tem granularidade de 1 s (WUT sobre LSI), portanto o resto
         * do intervalo e' cumprido com o MCU acordado. */
        uint32_t remaining = g_app.cfg.tdc_ms - elapsed;
        uint32_t seconds = remaining / 1000u;
        uint32_t rest = remaining % 1000u;

        if (at_service(rest > 200u ? 200u : rest) > 0) {
            /* houve atividade no console: reinicia a contagem do restante */
            continue;
        }
        rest = (rest > 200u) ? (rest - 200u) : 0u;
        if (seconds > 0u || rest > 0u) {
            console_puts("Enter stop mode\r\n");
            if (seconds > 0u) {
                hw_sleep_seconds(seconds);
            }
            if (rest > 0u) {
                hw_delay_ms(rest);
            }
            hw_woke_from_stop_clear();
            console_puts("Exit stop mode\r\n");
        }
    }
}
