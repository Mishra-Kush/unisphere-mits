#ifndef API_H
#define API_H
#include <sqlite3.h>
#include "http.h"
#include "json.h"
typedef struct { sqlite3 *db; sock_t sock; const HttpReq *req; int secure_cookie; } Ctx;
void api_send(Ctx *c, int status, const char *json, const char *extra_headers);
void api_error(Ctx *c, int status, const char *code, const char *message);
void api_db_error(Ctx *c);
int  api_origin_ok(const HttpReq *r);
int  api_read_json(Ctx *c, JObj *o);   /* checks Origin, Content-Type, body; sends the error itself and returns 0 on failure */
#endif
