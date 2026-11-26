/* Minimal single-threaded HTTP/1.1 server core (Connection: close). */
#include "http.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int http_last_status = 0;

sock_t http_listen(const char *bind_ip, int port) {
    struct sockaddr_in a;
    sock_t s;
    int yes = 1;
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return SOCK_BAD;
#endif
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_BAD) return SOCK_BAD;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, bind_ip, &a.sin_addr) != 1) return SOCK_BAD;
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) { http_close(s); return SOCK_BAD; }
    return s;
}

void http_close(sock_t c) {
#ifdef _WIN32
    closesocket(c);
#else
    close(c);
#endif
}

sock_t http_accept(sock_t listener, HttpReq *req) {
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    sock_t c = accept(listener, (struct sockaddr *)&a, &al);
    if (c == SOCK_BAD) return SOCK_BAD;
#ifdef _WIN32
    { DWORD tv = 10000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
      setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv); }
#else
    { struct timeval tv = {10, 0}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
      setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv); }
#endif
    memset(req, 0, sizeof *req);
    inet_ntop(AF_INET, &a.sin_addr, req->client_ip, sizeof req->client_ip);
    return c;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static void cp(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

void http_req_free(HttpReq *r) { free(r->body); r->body = NULL; r->body_len = 0; }

int http_read(sock_t c, HttpReq *r) {
    static char buf[HTTP_MAX_HEADER + 1];
    size_t len = 0, i, have, need = 0;
    char *end = NULL, *eol, *line, *sp1, *sp2, *q;
    int seen_cl = 0;

    for (;;) {
        int n = (int)recv(c, buf + len, (int)(HTTP_MAX_HEADER - len), 0);
        if (n <= 0) return -1;
        len += (size_t)n;
        buf[len] = '\0';
        if (strlen(buf) != len) return 400;                 /* NUL bytes in header */
        if ((end = strstr(buf, "\r\n\r\n")) != NULL) break;
        if (len >= HTTP_MAX_HEADER) return 431;
    }
    eol = strstr(buf, "\r\n");
    *eol = '\0';
    sp1 = strchr(buf, ' ');
    sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
    if (!sp1 || !sp2 || strncmp(sp2 + 1, "HTTP/1.", 7) != 0) return 400;
    *sp1 = '\0'; *sp2 = '\0';
    if (strlen(buf) > 7) return 400;
    for (i = 0; buf[i]; i++) if (!isupper((unsigned char)buf[i])) return 400;
    cp(r->method, sizeof r->method, buf);
    if (sp1[1] != '/' ) return 400;
    if (strlen(sp1 + 1) > 1000) return 414;
    if ((q = strchr(sp1 + 1, '?')) != NULL) { *q = '\0'; cp(r->query, sizeof r->query, q + 1); }
    if (strlen(sp1 + 1) >= sizeof r->path) return 414;
    cp(r->path, sizeof r->path, sp1 + 1);

    line = eol + 2;
    while (line <= end) {
        char *e2 = strstr(line, "\r\n"), *colon, *v;
        if (!e2) break;
        *e2 = '\0';
        if (*line) {
            if (!(colon = strchr(line, ':'))) return 400;
            *colon = '\0';
            v = colon + 1;
            while (*v == ' ' || *v == '\t') v++;
            if (ieq(line, "Cookie")) cp(r->cookie, sizeof r->cookie, v);
            else if (ieq(line, "Origin")) cp(r->origin, sizeof r->origin, v);
            else if (ieq(line, "Host")) cp(r->host, sizeof r->host, v);
            else if (ieq(line, "X-CSRF-Token")) cp(r->csrf, sizeof r->csrf, v);
            else if (ieq(line, "Content-Type")) cp(r->content_type, sizeof r->content_type, v);
            else if (ieq(line, "Transfer-Encoding")) return 501;
            else if (ieq(line, "Content-Length")) {
                char *ep;
                unsigned long cl;
                if (seen_cl || !isdigit((unsigned char)*v)) return 400;
                cl = strtoul(v, &ep, 10);
                if (*ep != '\0') return 400;
                if (cl > HTTP_MAX_BODY) return 413;
                need = cl; seen_cl = 1;
            }
        }
        line = e2 + 2;
    }
    have = len - (size_t)(end + 4 - buf);
    if (have > need) have = need;
    r->body = malloc(need + 1);
    if (!r->body) return 500;
    memcpy(r->body, end + 4, have);
    while (have < need) {
        int n = (int)recv(c, r->body + have, (int)(need - have), 0);
        if (n <= 0) { http_req_free(r); return -1; }
        have += (size_t)n;
    }
    r->body[need] = '\0';
    r->body_len = need;
    return 0;
}

static const char *status_text(int s) {
    switch (s) {
    case 200: return "OK"; case 201: return "Created"; case 204: return "No Content";
    case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden";
    case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 409: return "Conflict";
    case 413: return "Payload Too Large"; case 414: return "URI Too Long"; case 415: return "Unsupported Media Type";
    case 422: return "Unprocessable Entity"; case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large"; case 501: return "Not Implemented";
    default: return "Internal Server Error";
    }
}

static void send_all(sock_t c, const char *p, size_t n) {
    while (n > 0) {
        int w = (int)send(c, p, (int)(n > 65536 ? 65536 : n), MSG_NOSIGNAL);
        if (w <= 0) return;
        p += w; n -= (size_t)w;
    }
}

void http_send(sock_t c, int status, const char *ctype, const char *body, size_t len,
               const char *extra, int is_api) {
    char head[1536];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
        "X-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\nReferrer-Policy: same-origin\r\n"
        "Content-Security-Policy: default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'\r\n"
        "Cache-Control: %s\r\n%s\r\n",
        status, status_text(status), ctype, len, is_api ? "no-store" : "no-cache", extra ? extra : "");
    http_last_status = status;
    if (n > 0 && (size_t)n < sizeof head) send_all(c, head, (size_t)n);
    if (len) send_all(c, body, len);
}
