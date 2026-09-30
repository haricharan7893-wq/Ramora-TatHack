#include "cmd_proc.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>

struct CommandSpec command_list[] = {
    {"PING", 1, handle_ping},
    {"GET", 2, handle_get},
    {"SET", 3, handle_set},
    {"DEL", 2, handle_del},
    {"EXISTS", 2, handle_exists},
    {"INCR", 2, handle_incr},
    {"DECR", 2, handle_decr},
    {"TTL", 3, handle_set_ttl_sec},
    {"TTL", 2, handle_get_ttl_sec},
    {"TTLMS", 3, handle_set_ttl_ms},
    {"TTLMS", 2, handle_get_ttl_ms}
};

size_t command_list_len = sizeof(command_list) / sizeof(command_list[0]);

char* get_string(struct buf* buffer, size_t* offset){
    uint32_t len;
    bufcpylenoffset(buffer, *offset, &len, HEADER_SIZE);

    char* str = malloc(sizeof(char) * (len + 1));
    if (str == NULL){
        die("get_string (cmd_proc.c): malloc");
        exit(EXIT_FAILURE);
    }
    *offset += HEADER_SIZE;
    bufcpylenoffset(buffer, *offset, str, len);
    str[len] = '\0';
    *offset += len;

    return str;
}

static int64_t parse_to_int(char* str){
    int is_neg = 0;
    if (*str == '-'){
        is_neg = 1;
        str++;
    }
    int64_t val = 0;
    while (*str){
        if (*str < '0' || *str > '9'){
            return -2; // -2 for incorrect type
        }

        val *= 10;
        val += *str - '0';
        str++;
    }

    if (is_neg == 1){
        val *= -1;
    }

    if (val < 0) return -1;
    else return val;
}

int handle_ping(struct Conn* conn, size_t offset){
    (void)offset; /* FIX: suppress unused-parameter warning */
    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_str(conn->wbuf, "PONG");
    return rv;
}

int handle_get(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);

    struct buf* val = get_Entry(key);
    free(key);
    
    if (val == NULL){
        int rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
        return rv;
    }

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_str_len(conn->wbuf, bufstart(val), bufsize(val));
    return rv;
}

int handle_set(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    char* val = get_string(conn->rbuf, &offset);
    int rv = set_Entry(key, val);
    free(key);
    free(val);

    if (rv == 0) {
        rv = response_code(conn->wbuf, RES_ERR);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
    }
    rv &= response_u32(conn->wbuf, 0);
    return rv;
}


int handle_del(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int found = del_Entry(key);
    free(key);

    int rv = response_code(conn->wbuf, found ? RES_OK : RES_NX);
    rv &= response_u32(conn->wbuf, 0);
    return rv;
}

int handle_exists(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int found = get_Entry(key) != NULL;
    free(key);

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_int(conn->wbuf, found);
    return rv;
}

static int parse_int64_str(const char *str, int64_t *out){
    if (str == NULL || *str == '\0') return 0;
    char *end = NULL;
    errno = 0;
    intmax_t value = strtoimax(str, &end, 10);
    if (errno == ERANGE || end == str || *end != '\0' ||
        value < INT64_MIN || value > INT64_MAX)
        return 0;
    *out = (int64_t)value;
    return 1;
}

static int handle_delta(struct Conn* conn, size_t offset, int64_t delta){
    char* key = get_string(conn->rbuf, &offset);
    struct buf* current = get_Entry(key);

    int64_t old_value = 0;
    if (current != NULL) {
        if (!parse_int64_str(bufstart(current), &old_value)) {
            free(key);
            int rv = response_code(conn->wbuf, RES_TY);
            rv &= response_u32(conn->wbuf, 0);
            return rv;
        }
    }

    if ((delta > 0 && old_value > INT64_MAX - delta) ||
        (delta < 0 && old_value < INT64_MIN - delta)) {
        free(key);
        int rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
        return rv;
    }

    int64_t new_value = old_value + delta;
    char value[64];
    int n = snprintf(value, sizeof(value), "%" PRId64, new_value);
    int ok = n > 0 && (size_t)n < sizeof(value) && set_Entry(key, value);
    free(key);

    if (!ok) {
        int rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
        return rv;
    }

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_int64(conn->wbuf, new_value);
    return rv;
}

int handle_incr(struct Conn* conn, size_t offset){
    return handle_delta(conn, offset, 1);
}

int handle_decr(struct Conn* conn, size_t offset){
    return handle_delta(conn, offset, -1);
}

// NOTE: DEL used to be wired up here (handle_del). It's been pulled out —
// see PROBLEM_STATEMENT.md, del_Entry() in oper.c/oper.h is still there for you to use.

int handle_get_ttl_sec(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int64_t ttl = get_ttl_Entry_sec(key);
    free(key);

    int rv = 1;
    if (ttl == -2){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 1);
        rv &= response_int64(conn->wbuf, ttl);
    }
    return rv;
}

int handle_get_ttl_ms(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int64_t ttl = get_ttl_Entry_ms(key);
    free(key);

    int rv = 1;
    if (ttl == -2){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 1);
        rv &= response_int64(conn->wbuf, ttl);
    }
    return rv;
}

int handle_set_ttl_sec(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    char* ttl_string = get_string(conn->rbuf, &offset);
    int64_t ttl = parse_to_int(ttl_string);

    if (ttl == -2){
        int rv = 1;
        rv = response_code(conn->wbuf, RES_TY);
        rv &= response_u32(conn->wbuf, 0);
        free(key);
        free(ttl_string);
        return rv;
    }

    int res = set_ttl_Entry_sec(key, ttl);
    free(key);
    free(ttl_string);

    int rv = 1;
    if (res == -1){
        rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == 0){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 0);
    }
    return rv;
}

int handle_set_ttl_ms(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    char* ttl_string = get_string(conn->rbuf, &offset);
    int64_t ttl = parse_to_int(ttl_string);

    if (ttl == -2){
        int rv = 1;
        rv = response_code(conn->wbuf, RES_TY);
        rv &= response_u32(conn->wbuf, 0); /* FIX: was response_code — corrupted response framing */
        free(key);
        free(ttl_string);
        return rv;
    }

    int res = set_ttl_Entry_ms(key, ttl);
    free(key);
    free(ttl_string);

    int rv = 1;
    if (res == -1){ /* FIX: was res == 0 — made the RES_NX else-if dead (unreachable) */
        rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == 0){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 0);
    }
    return rv;
}