#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
typedef SOCKET socket_t;
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
typedef int socket_t;
#define close_socket close
#define INVALID_SOCKET (-1)
#endif

#define MAX_APPLICANTS 512
#define MAX_BODY 8192
#define MAX_REQUEST 16384
#define MAX_RESPONSE 262144
#define DEPT_COUNT 8
#define LEGACY_DEPT_COUNT 4

typedef enum { PENDING, ADMITTED, CANCELLED } Status;
typedef struct {
    int id, score, board, tenth, department;
    unsigned long order;
    Status status;
    char name[80];
} Applicant;
typedef struct {
    const char *name;
    int capacity;
    int heap[MAX_APPLICANTS]; /* Applicant array indices, not copies. */
    int size;
} Department;

static Applicant applicants[MAX_APPLICANTS];
static int count = 0, next_id = 1001;
static unsigned long next_order = 1;
static Department depts[DEPT_COUNT] = {
    { .name = "Computer Science", .capacity = 0 },
    { .name = "Electronics", .capacity = 0 },
    { .name = "Mechanical", .capacity = 0 },
    { .name = "Civil", .capacity = 0 },
    { .name = "Information Technology", .capacity = 0 },
    { .name = "Electrical & Electronics", .capacity = 0 },
    { .name = "AI & Data Science", .capacity = 0 },
    { .name = "Biotechnology", .capacity = 0 }
};
static const char *data_path = "data/admissions.tsv";

/* Entrance, Class 12, Class 10, then earlier application break ties. */
static bool higher(int ai, int bi) {
    const Applicant *a = &applicants[ai], *b = &applicants[bi];
    if (a->score != b->score) return a->score > b->score;
    if (a->board != b->board) return a->board > b->board;
    if (a->tenth != b->tenth) return a->tenth > b->tenth;
    return a->order < b->order;
}
static void swap_int(int *a, int *b) { int t = *a; *a = *b; *b = t; }
static void heap_push(int d, int idx) {
    Department *dep = &depts[d];
    int i = dep->size++;
    dep->heap[i] = idx;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!higher(dep->heap[i], dep->heap[p])) break;
        swap_int(&dep->heap[i], &dep->heap[p]);
        i = p;
    }
}
static int heap_pop(int d) {
    Department *dep = &depts[d];
    if (!dep->size) return -1;
    int winner = dep->heap[0];
    dep->heap[0] = dep->heap[--dep->size];
    int i = 0;
    while (2 * i + 1 < dep->size) {
        int child = 2 * i + 1;
        if (child + 1 < dep->size && higher(dep->heap[child + 1], dep->heap[child])) child++;
        if (!higher(dep->heap[child], dep->heap[i])) break;
        swap_int(&dep->heap[child], &dep->heap[i]);
        i = child;
    }
    return winner;
}
static int admitted_count(int d) {
    int n = 0;
    for (int i = 0; i < count; i++)
        if (applicants[i].department == d && applicants[i].status == ADMITTED) n++;
    return n;
}
static int fill_seats(int d) {
    int filled = 0;
    while (admitted_count(d) < depts[d].capacity && depts[d].size) {
        int idx = heap_pop(d);
        applicants[idx].status = ADMITTED;
        filled++;
    }
    return filled;
}
static int find_id(int id) {
    for (int i = 0; i < count; i++) if (applicants[i].id == id) return i;
    return -1;
}
static bool valid_name(const char *s) {
    size_t n = strlen(s);
    if (n < 2 || n >= sizeof applicants[0].name) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || s[i] == '\t' || s[i] == '\n') return false;
    return true;
}
static int add_applicant(const char *name, int score, int board, int tenth, int d) {
    if (!valid_name(name) || score < 0 || score > 100 || board < 0 || board > 100 ||
        tenth < 0 || tenth > 100 ||
        d < 0 || d >= DEPT_COUNT || count >= MAX_APPLICANTS) return -1;
    Applicant *a = &applicants[count];
    *a = (Applicant){ .id = next_id++, .score = score, .board = board, .tenth = tenth,
        .department = d, .order = next_order++, .status = PENDING };
    strcpy(a->name, name);
    heap_push(d, count++);
    return a->id;
}
static bool update_tenth(int id, int tenth) {
    int i = find_id(id);
    if (i < 0 || tenth < 0 || tenth > 100 || applicants[i].tenth >= 0) return false;
    applicants[i].tenth = tenth;
    if (applicants[i].status == PENDING) {
        int d = applicants[i].department;
        depts[d].size = 0;
        for (int j = 0; j < count; j++)
            if (applicants[j].department == d && applicants[j].status == PENDING)
                heap_push(d, j);
    }
    return true;
}
static bool cancel_admission(int id) {
    int i = find_id(id);
    if (i < 0 || applicants[i].status != ADMITTED) return false;
    int d = applicants[i].department;
    applicants[i].status = CANCELLED;
    fill_seats(d);
    return true;
}
static bool save_state(void) {
    char temp[512];
    if (snprintf(temp, sizeof temp, "%s.tmp", data_path) >= (int)sizeof temp) return false;
    FILE *f = fopen(temp, "w");
    if (!f) return false;
    fprintf(f, "ADMISSIONS2 %d %lu", next_id, next_order);
    for (int d = 0; d < DEPT_COUNT; d++) fprintf(f, " %d", depts[d].capacity);
    fputc('\n', f);
    for (int i = 0; i < count; i++) {
        const Applicant *a = &applicants[i];
        fprintf(f, "%d\t%lu\t%d\t%d\t%d\t%d\t%d\t%s\n", a->id, a->order,
            a->score, a->board, a->tenth, a->department, a->status, a->name);
    }
    if (fclose(f) != 0) { remove(temp); return false; }
#ifdef _WIN32
    if (!MoveFileExA(temp, data_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (rename(temp, data_path) != 0) {
#endif
        remove(temp); return false;
    }
    return true;
}
static bool load_state(void) {
    FILE *f = fopen(data_path, "r");
    if (!f) return errno == ENOENT;
    char line[512], marker[32];
    int capacities[DEPT_COUNT];
    if (!fgets(line, sizeof line, f) || sscanf(line, "%31s", marker) != 1) {
        fclose(f); return false;
    }
    bool legacy = strcmp(marker, "ADMISSIONS1") == 0;
    if (legacy) {
        if (sscanf(line, "%31s %d %lu %d %d %d %d", marker, &next_id, &next_order,
                &capacities[0], &capacities[1], &capacities[2], &capacities[3]) != 7) {
            fclose(f); return false;
        }
        for (int d = LEGACY_DEPT_COUNT; d < DEPT_COUNT; d++) capacities[d] = depts[d].capacity;
    } else if (strcmp(marker, "ADMISSIONS2") == 0) {
        if (sscanf(line, "%31s %d %lu %d %d %d %d %d %d %d %d", marker,
                &next_id, &next_order, &capacities[0], &capacities[1], &capacities[2],
                &capacities[3], &capacities[4], &capacities[5], &capacities[6],
                &capacities[7]) != 11) { fclose(f); return false; }
    } else { fclose(f); return false; }
    for (int d = 0; d < DEPT_COUNT; d++) {
        if (capacities[d] < 0 || capacities[d] > MAX_APPLICANTS) { fclose(f); return false; }
        depts[d].capacity = capacities[d];
    }
    while (fgets(line, sizeof line, f)) {
        if (count == MAX_APPLICANTS) { fclose(f); return false; }
        Applicant *a = &applicants[count];
        int status, offset = 0;
        a->tenth = -1;
        int parsed = legacy
            ? sscanf(line, "%d\t%lu\t%d\t%d\t%d\t%d\t%n", &a->id,
                &a->order, &a->score, &a->board, &a->department, &status, &offset)
            : sscanf(line, "%d\t%lu\t%d\t%d\t%d\t%d\t%d\t%n", &a->id,
                &a->order, &a->score, &a->board, &a->tenth, &a->department, &status, &offset);
        if (parsed != (legacy ? 6 : 7) ||
            !offset || a->department < 0 || a->department >= DEPT_COUNT ||
            a->score < 0 || a->score > 100 || a->board < 0 || a->board > 100 ||
            a->tenth < -1 || a->tenth > 100 ||
            status < PENDING || status > CANCELLED) { fclose(f); return false; }
        line[strcspn(line, "\r\n")] = 0;
        if (!valid_name(line + offset)) { fclose(f); return false; }
        strcpy(a->name, line + offset);
        a->status = (Status)status;
        if (a->status == PENDING) heap_push(a->department, count);
        count++;
    }
    fclose(f);
    return true;
}

typedef struct { char *buf; size_t len, cap; bool overflow; } Writer;
static void put(Writer *w, const char *s) {
    size_t n = strlen(s);
    if (w->len + n + 1 > w->cap) { w->overflow = true; return; }
    memcpy(w->buf + w->len, s, n + 1);
    w->len += n;
}
static void number(Writer *w, long n) { char s[40]; snprintf(s, sizeof s, "%ld", n); put(w, s); }
static void json_string(Writer *w, const char *s) {
    put(w, "\"");
    for (; *s; s++) {
        char escaped[8];
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { escaped[0] = '\\'; escaped[1] = c; escaped[2] = 0; put(w, escaped); }
        else if (c < 32) { snprintf(escaped, sizeof escaped, "\\u%04x", c); put(w, escaped); }
        else { escaped[0] = c; escaped[1] = 0; put(w, escaped); }
    }
    put(w, "\"");
}
static const char *status_name(Status s) {
    return s == PENDING ? "Waiting" : s == ADMITTED ? "Admitted" : "Cancelled";
}
static void applicant_json(Writer *w, const Applicant *a) {
    put(w, "{\"id\":"); number(w, a->id);
    put(w, ",\"name\":"); json_string(w, a->name);
    put(w, ",\"score\":"); number(w, a->score);
    put(w, ",\"board\":"); number(w, a->board);
    put(w, ",\"tenth\":"); number(w, a->tenth);
    put(w, ",\"department\":"); number(w, a->department);
    put(w, ",\"order\":"); number(w, (long)a->order);
    put(w, ",\"status\":"); json_string(w, status_name(a->status));
    put(w, "}");
}
/* Extract from a copy so the actual waiting heap remains untouched. */
static void ranked_ids_json(Writer *w, const Department *dep) {
    int copy[MAX_APPLICANTS];
    int size = dep->size;
    memcpy(copy, dep->heap, (size_t)size * sizeof copy[0]);
    put(w, "[");
    for (int rank = 0; rank < dep->size; rank++) {
        if (rank) put(w, ",");
        int winner = copy[0];
        number(w, applicants[winner].id);
        copy[0] = copy[--size];
        int i = 0;
        while (2 * i + 1 < size) {
            int child = 2 * i + 1;
            if (child + 1 < size && higher(copy[child + 1], copy[child])) child++;
            if (!higher(copy[child], copy[i])) break;
            swap_int(&copy[child], &copy[i]);
            i = child;
        }
    }
    put(w, "]");
}
static void state_json(Writer *w) {
    put(w, "{\"departments\":[");
    for (int d = 0; d < DEPT_COUNT; d++) {
        if (d) put(w, ",");
        put(w, "{\"id\":"); number(w, d);
        put(w, ",\"name\":"); json_string(w, depts[d].name);
        put(w, ",\"capacity\":"); number(w, depts[d].capacity);
        put(w, ",\"admitted\":"); number(w, admitted_count(d));
        put(w, ",\"heap\":[");
        for (int i = 0; i < depts[d].size; i++) {
            if (i) put(w, ",");
            number(w, applicants[depts[d].heap[i]].id);
        }
        put(w, "],\"ranking\":");
        ranked_ids_json(w, &depts[d]);
        put(w, "}");
    }
    put(w, "],\"applicants\":[");
    for (int i = 0; i < count; i++) {
        if (i) put(w, ",");
        applicant_json(w, &applicants[i]);
    }
    put(w, "]}");
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void form_value(const char *body, const char *key, char *out, size_t cap) {
    out[0] = 0;
    size_t keylen = strlen(key);
    const char *p = body;
    while (*p) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > keylen && !strncmp(p, key, keylen) && p[keylen] == '=') {
            p += keylen + 1;
            size_t n = 0;
            while (p < end && n + 1 < cap) {
                if (*p == '%' && p + 2 < end && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0) {
                    out[n++] = (char)(hex_digit(p[1]) * 16 + hex_digit(p[2])); p += 3;
                } else out[n++] = (*p++ == '+') ? ' ' : p[-1];
            }
            out[n] = 0;
            return;
        }
        p = *end ? end + 1 : end;
    }
}
static bool form_int(const char *body, const char *key, int *value) {
    char s[32], *end;
    form_value(body, key, s, sizeof s);
    if (!*s) return false;
    long n = strtol(s, &end, 10);
    if (*end || n < -1000000 || n > 1000000) return false;
    *value = (int)n;
    return true;
}
static void send_response(socket_t client, int code, const char *type, const char *body, size_t size) {
    char header[512];
    int len = snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        code, code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 404 ? "Not Found" : "Server Error", type, size);
    if (len < 0 || len >= (int)sizeof header) return;
    (void)send(client, header, len, 0);
    while (size) {
        int sent = send(client, body, (int)size, 0);
        if (sent <= 0) break;
        body += sent; size -= (size_t)sent;
    }
}
static void json_error(socket_t client, int code, const char *message) {
    char buffer[512] = "";
    Writer w = { buffer, 0, sizeof buffer, false };
    put(&w, "{\"error\":"); json_string(&w, message); put(&w, "}");
    send_response(client, code, "application/json; charset=utf-8", buffer, w.len);
}
static void serve_file(socket_t client, const char *path, const char *type) {
    FILE *f = fopen(path, "rb");
    if (!f) { json_error(client, 404, "File not found"); return; }
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) { fclose(f); json_error(client, 500, "File error"); return; }
    long length = ftell(f);
    rewind(f);
    char *contents = malloc((size_t)length + 1);
    if (!contents) { fclose(f); json_error(client, 500, "Out of memory"); return; }
    size_t read_count = fread(contents, 1, (size_t)length, f);
    fclose(f);
    if (read_count == (size_t)length) send_response(client, 200, type, contents, read_count);
    else json_error(client, 500, "File error");
    free(contents);
}
static void handle_client(socket_t client) {
    char req[MAX_REQUEST + 1] = "";
    size_t used = 0, bodylen = 0;
    char *separator = NULL;
    while (used < MAX_REQUEST) {
        int n = recv(client, req + used, (int)(MAX_REQUEST - used), 0);
        if (n <= 0) return;
        used += (size_t)n; req[used] = 0;
        separator = strstr(req, "\r\n\r\n");
        if (separator) break;
    }
    if (!separator) { json_error(client, 400, "Invalid request"); return; }
    char method[8], path[128];
    if (sscanf(req, "%7s %127s", method, path) != 2) { json_error(client, 400, "Invalid request"); return; }
    char *body = separator + 4;
    if (!strcmp(method, "GET")) {
        if (!strcmp(path, "/") || !strcmp(path, "/index.html")) serve_file(client, "web/index.html", "text/html; charset=utf-8");
        else if (!strcmp(path, "/style.css")) serve_file(client, "web/style.css", "text/css; charset=utf-8");
        else if (!strcmp(path, "/app.js")) serve_file(client, "web/app.js", "text/javascript; charset=utf-8");
        else if (!strcmp(path, "/api/state")) {
            char *buffer = malloc(MAX_RESPONSE);
            if (!buffer) { json_error(client, 500, "Out of memory"); return; }
            Writer w = { buffer, 0, MAX_RESPONSE, false };
            buffer[0] = 0; state_json(&w);
            if (w.overflow) json_error(client, 500, "Response too large");
            else send_response(client, 200, "application/json; charset=utf-8", buffer, w.len);
            free(buffer);
        } else json_error(client, 404, "Not found");
        return;
    }
    if (strcmp(method, "POST")) { json_error(client, 400, "Unsupported method"); return; }
    char *cl = strstr(req, "Content-Length:");
    if (!cl || cl > separator || sscanf(cl, "Content-Length: %zu", &bodylen) != 1 || bodylen > MAX_BODY) {
        json_error(client, 400, "Invalid body length"); return;
    }
    size_t have = used - (size_t)(body - req);
    while (have < bodylen && used < MAX_REQUEST) {
        int n = recv(client, req + used, (int)(MAX_REQUEST - used), 0);
        if (n <= 0) return;
        used += (size_t)n; have += (size_t)n;
    }
    if (have < bodylen) { json_error(client, 400, "Incomplete body"); return; }
    body[bodylen] = 0;
    int a, b, c, d;
    char name[80];
    if (!strcmp(path, "/api/applicants")) {
        form_value(body, "name", name, sizeof name);
        if (!form_int(body, "score", &a) || !form_int(body, "board", &b) ||
            !form_int(body, "tenth", &c) || !form_int(body, "department", &d) ||
            add_applicant(name, a, b, c, d) < 0) {
            json_error(client, 400, "Enter a name, three valid marks (0–100), and a department"); return;
        }
    } else if (!strcmp(path, "/api/tenth")) {
        if (!form_int(body, "id", &a) || !form_int(body, "tenth", &b) ||
            !update_tenth(a, b)) {
            json_error(client, 400, "Class 10 mark can only be added once to an older application"); return;
        }
    } else if (!strcmp(path, "/api/demo")) {
        if (count != 0) { json_error(client, 400, "Sample data can only be loaded into an empty workspace"); return; }
        depts[0].capacity = 2;
        depts[1].capacity = 1;
        add_applicant("Asha Raman", 88, 91, 95, 0);
        add_applicant("Kiran Shah", 96, 84, 99, 0);
        add_applicant("Mira Nair", 96, 90, 89, 0);
        add_applicant("Sara Iyer", 96, 90, 92, 0);
        add_applicant("Priya Rao", 92, 88, 91, 1);
        add_applicant("Rahul Das", 82, 86, 85, 1);
    } else if (!strcmp(path, "/api/allocate")) {
        if (!form_int(body, "department", &d) || d < 0 || d >= DEPT_COUNT) {
            json_error(client, 400, "Choose a department"); return;
        }
        fill_seats(d);
    } else if (!strcmp(path, "/api/cancel")) {
        if (!form_int(body, "id", &a) || !cancel_admission(a)) {
            json_error(client, 400, "Only admitted applicants can be cancelled"); return;
        }
    } else if (!strcmp(path, "/api/capacity")) {
        if (!form_int(body, "department", &d) || !form_int(body, "capacity", &a) ||
            d < 0 || d >= DEPT_COUNT || a < admitted_count(d) || a > MAX_APPLICANTS) {
            json_error(client, 400, "Capacity must be at least the number already admitted"); return;
        }
        depts[d].capacity = a;
    } else { json_error(client, 404, "Not found"); return; }
    if (!save_state()) { json_error(client, 500, "Could not save records"); return; }
    const char *ok = "{\"ok\":true}";
    send_response(client, 200, "application/json; charset=utf-8", ok, strlen(ok));
}
static int run_server(int port, const char *host) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WinSock startup failed\n"); return 1; }
#endif
    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((unsigned short)port) };
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid host address\n");
        close_socket(server);
        return 1;
    }
    if (bind(server, (struct sockaddr *)&addr, sizeof addr) || listen(server, 10)) {
        perror("bind/listen"); close_socket(server); return 1;
    }
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    printf("Admission app listening on %s:%d\nPress Ctrl+C to stop.\n", host, port);
    fflush(stdout);
    for (;;) {
        socket_t client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            break;
        }
        handle_client(client);
        close_socket(client);
    }
    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
}
static int read_int(const char *prompt) {
    char line[128];
    printf("%s", prompt); fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return -1;
    char *end;
    long n = strtol(line, &end, 10);
    if (end == line || (*end != '\n' && *end != 0) || n < 0 || n > 1000000) return -1;
    return (int)n;
}
static void console_list(void) {
    puts("\nID     Name                 Department              Entrance Class12 Class10 Status");
    for (int i = 0; i < count; i++) {
        Applicant *a = &applicants[i];
        char tenth[16];
        if (a->tenth < 0) strcpy(tenth, "—");
        else snprintf(tenth, sizeof tenth, "%d", a->tenth);
        printf("%-6d %-20.20s %-23.23s %8d %7d %7s %s\n", a->id, a->name,
            depts[a->department].name, a->score, a->board, tenth, status_name(a->status));
    }
    puts("\nHeap arrays (level order; root is the next applicant):");
    for (int d = 0; d < DEPT_COUNT; d++) {
        printf("%s [%d/%d seats]:", depts[d].name, admitted_count(d), depts[d].capacity);
        for (int j = 0; j < depts[d].size; j++) printf(" %d", applicants[depts[d].heap[j]].id);
        putchar('\n');
    }
}
static void console_mode(void) {
    char name[80];
    for (;;) {
        puts("\n1 Add applicant   2 Allocate seats   3 Cancel admission\n4 View records    5 Set seats        0 Exit");
        int choice = read_int("Choice: ");
        if (choice == 0 || choice < 0) break;
        int d, id, score, board, tenth;
        switch (choice) {
        case 1:
            printf("Name: "); fflush(stdout);
            if (!fgets(name, sizeof name, stdin)) return;
            name[strcspn(name, "\n")] = 0;
            score = read_int("Entrance score (0-100): ");
            board = read_int("Class 12 percentage (0-100): ");
            tenth = read_int("Class 10 percentage (0-100): ");
            puts("0 Computer Science, 1 Electronics, 2 Mechanical, 3 Civil");
            puts("4 Information Technology, 5 Electrical & Electronics, 6 AI & Data Science, 7 Biotechnology");
            d = read_int("Department (0-7): ");
            id = add_applicant(name, score, board, tenth, d);
            if (id < 0) puts("Invalid applicant details or maximum reached.");
            else { save_state(); printf("Application ID: %d\n", id); }
            break;
        case 2:
            d = read_int("Department (0-7): ");
            if (d < 0 || d >= DEPT_COUNT) puts("Invalid department.");
            else { printf("Allocated %d seat(s).\n", fill_seats(d)); save_state(); }
            break;
        case 3:
            id = read_int("Admitted application ID: ");
            if (!cancel_admission(id)) puts("Only admitted applicants can be cancelled.");
            else { save_state(); puts("Cancelled; next waiting applicant promoted if available."); }
            break;
        case 4: console_list(); break;
        case 5:
            d = read_int("Department (0-7): ");
            score = read_int("New seat capacity: ");
            if (d < 0 || d >= DEPT_COUNT || score < admitted_count(d) || score > MAX_APPLICANTS)
                puts("Invalid capacity (cannot remove occupied seats).");
            else { depts[d].capacity = score; save_state(); puts("Saved."); }
            break;
        default: puts("Choose 0 to 5.");
        }
    }
}
int main(int argc, char **argv) {
    int port = 8080;
    const char *host = "127.0.0.1";
    const char *env_port = getenv("PORT");
    if (env_port && *env_port) {
        char *end;
        long n = strtol(env_port, &end, 10);
        if (*end || n < 1 || n > 65535) { fprintf(stderr, "Invalid PORT\n"); return 1; }
        port = (int)n;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data_path = argv[++i];
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) {
            host = argv[++i];
            if (strcmp(host, "127.0.0.1") && strcmp(host, "0.0.0.0")) {
                fprintf(stderr, "Host must be 127.0.0.1 or 0.0.0.0\n"); return 1;
            }
        }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port < 1 || port > 65535) { fprintf(stderr, "Invalid port\n"); return 1; }
        } else if (strcmp(argv[i], "--console")) {
            fprintf(stderr, "Usage: %s [--console] [--host 127.0.0.1|0.0.0.0] [--port 8080] [--data path]\n", argv[0]); return 1;
        }
    }
    if (!strcmp(data_path, "data/admissions.tsv")) {
#ifdef _WIN32
        (void)_mkdir("data");
#else
        (void)mkdir("data", 0755);
#endif
    }
    if (!load_state()) { fprintf(stderr, "Could not load %s; check the data file.\n", data_path); return 1; }
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--console")) { console_mode(); return 0; }
    return run_server(port, host);
}
