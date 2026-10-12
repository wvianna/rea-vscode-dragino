#include "at.h"

#include <string.h>

const char *at_status_str(at_status_t st)
{
    switch (st) {
    case AT_OK:
        return "OK";
    case AT_ERROR:
        return "AT_ERROR";
    case AT_PARAM_ERROR:
        return "AT_PARAM_ERROR";
    case AT_BUSY_ERROR:
        return "AT_BUSY_ERROR";
    case AT_TEST_PARAM_OVERFLOW:
        return "AT_TEST_PARAM_OVERFLOW";
    case AT_NO_NETWORK_JOINED:
        return "AT_NO_NETWORK_JOINED";
    case AT_RX_ERROR:
        return "AT_RX_ERROR";
    default:
        return "AT_ERROR";
    }
}

void at_init(at_ctx_t *at, const at_cmd_t *table, size_t count,
             at_write_fn write, void *user)
{
    memset(at, 0, sizeof(*at));
    at->table = table;
    at->count = count;
    at->write = write;
    at->user = user;
}

static void out(at_ctx_t *at, const char *text)
{
    if (at->write && text) {
        at->write(text, at->user);
    }
}

/* Resposta completa: [valor]<CR><LF><Status><CR><LF> */
static void respond(at_ctx_t *at, const char *value, at_status_t st)
{
    if (value && value[0]) {
        out(at, value);
        out(at, "\r\n");
    }
    out(at, "\r\n");
    out(at, at_status_str(st));
    out(at, "\r\n");
}

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool eq_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        if (upper(*a) != upper(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

const at_cmd_t *at_find(const at_ctx_t *at, const char *name)
{
    /* aceita "+VER" e "VER" */
    if (name[0] == '+') {
        name++;
    }
    for (size_t i = 0; i < at->count; i++) {
        const char *n = at->table[i].name;
        if (n[0] == '+') {
            n++;
        }
        if (eq_nocase(n, name)) {
            return &at->table[i];
        }
    }
    return NULL;
}

static void print_short_help(at_ctx_t *at)
{
    /* Strings do firmware original (0x080150bc..0x0801511e). */
    out(at, "AT+<CMD>?        : Help on <CMD>\r\n");
    out(at, "AT+<CMD>         : Run <CMD>\r\n");
    out(at, "AT+<CMD>=<value> : Set the value\r\n");
    out(at, "AT+<CMD>=?       : Get the value\r\n");
    for (size_t i = 0; i < at->count; i++) {
        if (at->table[i].help) {
            out(at, at->table[i].help);
            out(at, "\r\n");
        }
    }
}

at_status_t at_exec(at_ctx_t *at, const char *line)
{
    char value[AT_LINE_MAX];
    const char *p = line;

    /* Linha vazia nao produz resposta (comportamento do console original). */
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0' || *p == '\r' || *p == '\n') {
        return AT_OK;
    }

    if (upper(p[0]) != 'A' || upper(p[1]) != 'T') {
        respond(at, NULL, AT_ERROR);
        return AT_ERROR;
    }
    p += 2;

    /* "AT" e "AT?" */
    if (*p == '\0' || *p == '\r' || *p == '\n') {
        respond(at, NULL, AT_OK);
        return AT_OK;
    }
    if (*p == '?') {
        print_short_help(at);
        respond(at, NULL, AT_OK);
        return AT_OK;
    }

    bool has_plus = (*p == '+');
    if (has_plus) {
        p++;
    }

    /* nome do comando */
    char name[16];
    size_t n = 0;
    while (*p && *p != '?' && *p != '=' && *p != '\r' && *p != '\n' && *p != '=') {
        if (n + 1 < sizeof(name)) {
            name[n++] = *p;
        }
        p++;
    }
    name[n] = '\0';
    if (n == 0) {
        respond(at, NULL, AT_ERROR);
        return AT_ERROR;
    }

    const at_cmd_t *cmd = at_find(at, name);
    if (cmd == NULL) {
        respond(at, NULL, AT_ERROR);
        return AT_ERROR;
    }

    /* ATZ: sem resposta de status no original; aqui respondemos OK. */
    if (cmd->name[0] == 'Z' && !has_plus) {
        respond(at, NULL, AT_OK);
        if (at->on_reset) {
            at->on_reset(at->user);
        }
        return AT_OK;
    }

    if (*p == '?') { /* AT+CMD? -> ajuda */
        value[0] = '\0';
        if (cmd->help) {
            out(at, cmd->help);
            out(at, "\r\n");
        }
        respond(at, NULL, AT_OK);
        return AT_OK;
    }

    if (*p == '=') {
        p++;
        if (*p == '?') { /* AT+CMD=? -> leitura */
            value[0] = '\0';
            if (!cmd->get) {
                respond(at, NULL, AT_ERROR);
                return AT_ERROR;
            }
            at_status_t st = cmd->get(value, sizeof(value), at->user);
            respond(at, value, st);
            return st;
        }
        /* AT+CMD=<valor> */
        char arg[AT_LINE_MAX];
        size_t a = 0;
        while (*p && *p != '\r' && *p != '\n') {
            if (a + 1 < sizeof(arg)) {
                arg[a++] = *p;
            }
            p++;
        }
        arg[a] = '\0';
        value[0] = '\0';
        if (!cmd->set) {
            respond(at, NULL, AT_ERROR);
            return AT_ERROR;
        }
        at_status_t st = cmd->set(arg, value, sizeof(value), at->user);
        respond(at, value, st);
        return st;
    }

    /* AT+CMD -> executa (set sem argumento) */
    value[0] = '\0';
    if (!cmd->set) {
        respond(at, NULL, AT_ERROR);
        return AT_ERROR;
    }
    at_status_t st = cmd->set(NULL, value, sizeof(value), at->user);
    respond(at, value, st);
    return st;
}

void at_feed(at_ctx_t *at, char c)
{
    if (c == '\r' || c == '\n') {
        if (at->len == 0 && !at->overflow) {
            return; /* ignora CR/LF redundantes */
        }
        at->line[at->len] = '\0';
        if (at->overflow) {
            at->overflow = false;
            at->len = 0;
            respond(at, NULL, AT_TEST_PARAM_OVERFLOW);
            return;
        }
        at_status_t st = at_exec(at, at->line);
        (void)st;
        at->len = 0;
        return;
    }
    if (at->len + 1 >= sizeof(at->line)) {
        at->overflow = true; /* descarta a linha e responde overflow no fim */
        return;
    }
    at->line[at->len++] = c;
}
