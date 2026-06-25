// Created by sm4925 on 2026/6/14

#include "ollama_backend.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define OLLAMA_DEFAULT_HOST "127.0.0.1"
#define OLLAMA_DEFAULT_PORT "11434"
#define MAX_HTTP_RESPONSE   (4u * 1024u * 1024u)
#define MAX_ERROR_TEXT      4096

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuilder;

static bool sb_init(StringBuilder *sb, size_t initial) {
    sb->data = malloc(initial);
    if (!sb->data) return false;

    sb->len = 0;
    sb->cap = initial;
    sb->data[0] = '\0';
    return true;
}

static void sb_free(StringBuilder *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static bool sb_reserve(StringBuilder *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return true;

    size_t new_cap = sb->cap ? sb->cap : 1024;
    while (new_cap < sb->len + extra + 1) {
        if (new_cap > SIZE_MAX / 2) return false;
        new_cap *= 2;
    }

    char *new_data = realloc(sb->data, new_cap);
    if (!new_data) return false;

    sb->data = new_data;
    sb->cap = new_cap;
    return true;
}

static bool sb_append_n(StringBuilder *sb, const char *text, size_t n) {
    if (!sb_reserve(sb, n)) return false;

    memcpy(sb->data + sb->len, text, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return true;
}

static bool sb_append(StringBuilder *sb, const char *text) {
    return sb_append_n(sb, text, strlen(text));
}

static bool sb_append_json_string(StringBuilder *sb, const char *text) {
    if (!sb_append(sb, "\"")) return false;

    for (const unsigned char *p = (const unsigned char *) text; *p; p++) {
        char tmp[8];

        switch (*p) {
            case '\\':
                if (!sb_append(sb, "\\\\")) return false;
                break;
            case '"':
                if (!sb_append(sb, "\\\"")) return false;
                break;
            case '\n':
                if (!sb_append(sb, "\\n")) return false;
                break;
            case '\r':
                if (!sb_append(sb, "\\r")) return false;
                break;
            case '\t':
                if (!sb_append(sb, "\\t")) return false;
                break;
            case '\b':
                if (!sb_append(sb, "\\b")) return false;
                break;
            case '\f':
                if (!sb_append(sb, "\\f")) return false;
                break;
            default:
                if (*p < 0x20) {
                    snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
                    if (!sb_append(sb, tmp)) return false;
                } else {
                    if (!sb_append_n(sb, (const char *) p, 1)) return false;
                }
                break;
        }
    }

    return sb_append(sb, "\"");
}

static char *xstrdup(const char *s) {
    if (!s) s = "";

    char *copy = malloc(strlen(s) + 1);
    if (copy) strcpy(copy, s);
    return copy;
}

static void set_response_error(ModelResponse *out,
                               int exit_code,
                               bool timed_out,
                               const char *message) {
    out->exit_code = exit_code;
    out->timed_out = timed_out;
    out->text = xstrdup(message ? message : "Ollama backend error");
}

static long long monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;

    return (long long) ts.tv_sec * 1000LL + (long long) ts.tv_nsec / 1000000LL;
}

static bool parse_host_env(char *host,
                           size_t host_size,
                           char *port,
                           size_t port_size) {
    const char *env = getenv("OLLAMA_HOST");

    if (!env || *env == '\0') {
        snprintf(host, host_size, "%s", OLLAMA_DEFAULT_HOST);
        snprintf(port, port_size, "%s", OLLAMA_DEFAULT_PORT);
        return true;
    }

    const char *start = env;
    if (strncmp(start, "http://", 7) == 0) {
        start += 7;
    }

    const char *slash = strchr(start, '/');
    size_t authority_len = slash ? (size_t) (slash - start) : strlen(start);

    if (authority_len == 0 || authority_len >= 256) {
        return false;
    }

    char authority[256];
    memcpy(authority, start, authority_len);
    authority[authority_len] = '\0';

    char *colon = strrchr(authority, ':');
    if (colon && colon[1] != '\0') {
        *colon = '\0';
        snprintf(host, host_size, "%s", authority);
        snprintf(port, port_size, "%s", colon + 1);
    } else {
        snprintf(host, host_size, "%s", authority);
        snprintf(port, port_size, "%s", OLLAMA_DEFAULT_PORT);
    }

    return host[0] != '\0' && port[0] != '\0';
}

static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        return -1;
    }

    int fd = -1;

    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static bool send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }

        if (n == 0) {
            return false;
        }

        sent += (size_t) n;
    }

    return true;
}

static bool recv_all_with_timeout(int fd, int timeout_seconds, char **out) {
    StringBuilder sb;

    if (!sb_init(&sb, 8192)) {
        return false;
    }

    long long start = monotonic_ms();
    long long timeout_ms = (long long) (timeout_seconds > 0 ? timeout_seconds : 1) * 1000LL;

    while (sb.len < MAX_HTTP_RESPONSE) {
        long long elapsed = monotonic_ms() - start;

        if (elapsed > timeout_ms) {
            sb_free(&sb);
            return false;
        }

        long long remaining = timeout_ms - elapsed;

        struct timeval tv;
        tv.tv_sec = (time_t) (remaining / 1000LL);
        tv.tv_usec = (suseconds_t) ((remaining % 1000LL) * 1000LL);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        int ready = select(fd + 1, &readfds, NULL, NULL, &tv);

        if (ready < 0) {
            if (errno == EINTR) continue;
            sb_free(&sb);
            return false;
        }

        if (ready == 0) {
            sb_free(&sb);
            return false;
        }

        char tmp[8192];
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            sb_free(&sb);
            return false;
        }

        if (n == 0) {
            break;
        }

        if (!sb_append_n(&sb, tmp, (size_t) n)) {
            sb_free(&sb);
            return false;
        }
    }

    *out = sb.data;
    return true;
}

static const char *http_body(const char *http) {
    const char *body = strstr(http, "\r\n\r\n");
    if (body) return body + 4;

    body = strstr(http, "\n\n");
    if (body) return body + 2;

    return http;
}

static int http_status_code(const char *http) {
    int code = 0;

    if (sscanf(http, "HTTP/%*s %d", &code) == 1) {
        return code;
    }

    return 0;
}

static const char *find_json_key_string_start(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = json;

    while ((p = strstr(p, pattern)) != NULL) {
        p += strlen(pattern);

        while (*p && isspace((unsigned char) *p)) p++;

        if (*p != ':') {
            continue;
        }

        p++;

        while (*p && isspace((unsigned char) *p)) p++;

        if (*p == '"') {
            return p + 1;
        }
    }

    return NULL;
}

static char *json_unescape_from_string_start(const char *p) {
    StringBuilder sb;

    if (!sb_init(&sb, 1024)) {
        return NULL;
    }

    while (*p && *p != '"') {
        if (*p != '\\') {
            if (!sb_append_n(&sb, p, 1)) {
                sb_free(&sb);
                return NULL;
            }

            p++;
            continue;
        }

        p++;

        if (!*p) {
            break;
        }

        switch (*p) {
            case '"':
                if (!sb_append(&sb, "\"")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case '\\':
                if (!sb_append(&sb, "\\")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case '/':
                if (!sb_append(&sb, "/")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case 'b':
                if (!sb_append(&sb, "\b")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case 'f':
                if (!sb_append(&sb, "\f")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case 'n':
                if (!sb_append(&sb, "\n")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case 'r':
                if (!sb_append(&sb, "\r")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case 't':
                if (!sb_append(&sb, "\t")) {
                    sb_free(&sb);
                    return NULL;
                }
                break;

            case 'u': {
                /*
                 * Decode the 4-hex-digit codepoint.  Ollama's JSON encoder
                 * escapes HTML-special ASCII characters (<, >, &, ') as \uXXXX
                 * even though they are perfectly valid UTF-8.  We must decode
                 * those to get correct source code (e.g. #include <stdio.h>).
                 * For non-ASCII codepoints we emit UTF-8; we do NOT silently
                 * substitute '?' because that corrupts diff context lines.
                 */
                unsigned int cp = 0;
                int digits = 0;
                for (int i = 0; i < 4 && isxdigit((unsigned char) p[1]); i++) {
                    p++;
                    unsigned char d = (unsigned char)*p;
                    cp = (cp << 4) | (d >= '0' && d <= '9' ? (unsigned)(d - '0')
                                    : d >= 'a' && d <= 'f' ? (unsigned)(d - 'a' + 10)
                                    :                        (unsigned)(d - 'A' + 10));
                    digits++;
                }
                if (digits == 4) {
                    char tmp[5];
                    int tlen = 0;
                    if (cp < 0x80) {
                        tmp[tlen++] = (char)cp;
                    } else if (cp < 0x800) {
                        tmp[tlen++] = (char)(0xC0 | (cp >> 6));
                        tmp[tlen++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        tmp[tlen++] = (char)(0xE0 | (cp >> 12));
                        tmp[tlen++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        tmp[tlen++] = (char)(0x80 | (cp & 0x3F));
                    }
                    if (!sb_append_n(&sb, tmp, (size_t)tlen)) {
                        sb_free(&sb);
                        return NULL;
                    }
                } else {
                    if (!sb_append(&sb, "?")) {
                        sb_free(&sb);
                        return NULL;
                    }
                }
                break;
            }

            default:
                if (!sb_append_n(&sb, p, 1)) {
                    sb_free(&sb);
                    return NULL;
                }
                break;
        }

        p++;
    }

    return sb.data;
}

static char *json_get_string(const char *json, const char *key) {
    const char *start = find_json_key_string_start(json, key);

    if (!start) {
        return NULL;
    }

    return json_unescape_from_string_start(start);
}

static bool build_ollama_request(const char *model,
                                 const char *prompt,
                                 char **out_body) {
    const char *system_prompt =
        "You are ArchAgent's local coding backend. "
        "Return ONLY the exact requested text format. "
        "Do not add commentary outside PLAN, PATCH and TESTS. "
        "The PATCH section must contain one unified diff fenced as diff.";

    StringBuilder sb;

    if (!sb_init(&sb, strlen(prompt) + 4096)) {
        return false;
    }

    bool ok = true;

    ok = ok && sb_append(&sb, "{");
    ok = ok && sb_append(&sb, "\"model\":");
    ok = ok && sb_append_json_string(&sb, model);

    ok = ok && sb_append(&sb, ",\"system\":");
    ok = ok && sb_append_json_string(&sb, system_prompt);

    ok = ok && sb_append(&sb, ",\"prompt\":");
    ok = ok && sb_append_json_string(&sb, prompt);

    ok = ok && sb_append(&sb, ",\"stream\":false");

    ok = ok && sb_append(
        &sb,
        ",\"options\":{"
        "\"temperature\":0.1,"
        "\"num_ctx\":2048,"
        "\"num_predict\":2048"
        "}"
    );

    ok = ok && sb_append(&sb, "}");

    if (!ok) {
        sb_free(&sb);
        return false;
    }

    *out_body = sb.data;
    return true;
}

bool ollama_backend_generate(const char *prompt,
                             const char *model_name,
                             int timeout_seconds,
                             ModelResponse *out) {
    if (!prompt || !out) {
        return false;
    }

    out->text = NULL;
    out->exit_code = -1;
    out->timed_out = false;

    const char *model =
        (model_name && model_name[0])
            ? model_name
            : ARCHAGENT_DEFAULT_OLLAMA_MODEL;

    char host[256];
    char port[32];

    if (!parse_host_env(host, sizeof(host), port, sizeof(port))) {
        set_response_error(
            out,
            2,
            false,
            "Invalid OLLAMA_HOST. Expected http://host:port or host:port."
        );
        return false;
    }

    char *body = NULL;

    if (!build_ollama_request(model, prompt, &body)) {
        set_response_error(out, 2, false, "Failed to build Ollama JSON request.");
        return false;
    }

    int fd = connect_tcp(host, port);

    if (fd < 0) {
        free(body);
        set_response_error(
            out,
            127,
            false,
            "Could not connect to Ollama at 127.0.0.1:11434. "
            "Start Ollama with `ollama serve` or open the Ollama app."
        );
        return false;
    }

    StringBuilder http;

    if (!sb_init(&http, strlen(body) + 1024)) {
        close(fd);
        free(body);
        return false;
    }

    char header[1024];

    snprintf(
        header,
        sizeof(header),
        "POST /api/generate HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Content-Type: application/json\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        host,
        port,
        strlen(body)
    );

    bool send_ok =
        sb_append(&http, header) &&
        sb_append(&http, body) &&
        send_all(fd, http.data, http.len);

    sb_free(&http);
    free(body);

    if (!send_ok) {
        close(fd);
        set_response_error(out, 1, false, "Failed to send request to Ollama.");
        return false;
    }

    char *raw_http = NULL;

    if (!recv_all_with_timeout(fd, timeout_seconds, &raw_http)) {
        close(fd);
        set_response_error(
            out,
            -1,
            true,
            "Ollama backend timed out or connection was closed before a complete response arrived."
        );
        out->timed_out = true;
        return false;
    }

    close(fd);

    int status = http_status_code(raw_http);
    const char *json = http_body(raw_http);

    char *error = json_get_string(json, "error");

    if (status != 200 || error) {
        char msg[MAX_ERROR_TEXT];

        snprintf(
            msg,
            sizeof(msg),
            "Ollama request failed%s%s%s",
            error ? ": " : "",
            error ? error : "",
            status ? "" : " (invalid HTTP response)"
        );

        free(raw_http);
        free(error);

        set_response_error(out, status ? status : 1, false, msg);
        return false;
    }

    char *response = json_get_string(json, "response");

    free(raw_http);

    if (!response || response[0] == '\0') {
        free(response);
        set_response_error(out, 1, false, "Ollama returned no response text.");
        return false;
    }

    out->text = response;
    out->exit_code = 0;
    out->timed_out = false;

    return true;
}