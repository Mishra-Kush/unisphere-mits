#ifndef HTTP_H
#define HTTP_H
#include <stddef.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#else
typedef int sock_t;
#define SOCK_BAD (-1)
#endif

#define HTTP_MAX_HEADER 16384
#define HTTP_MAX_BODY   65536

typedef struct {
    char method[8], path[512], query[512];
    char cookie[1024], origin[256], host[256], csrf[128], content_type[64], client_ip[64];
    char *body; size_t body_len;
} HttpReq;

sock_t http_listen(const char *bind_ip, int port);
sock_t http_accept(sock_t listener, HttpReq *req);
int    http_read(sock_t c, HttpReq *req);   /* 0 ok, -1 drop connection, else HTTP status to send */
void   http_req_free(HttpReq *req);
void   http_send(sock_t c, int status, const char *ctype, const char *body, size_t len,
                 const char *extra_headers, int is_api);
void   http_close(sock_t c);
extern int http_last_status;
#endif
