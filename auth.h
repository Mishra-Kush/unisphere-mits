#ifndef AUTH_H
#define AUTH_H
#include "api.h"
typedef struct { int user_id; char role[8], csrf[40], name[128], email[256], enrollment[32]; } Session;
void auth_init(void);
int  auth_current(Ctx *c, Session *s);                 /* 1 = valid, unexpired session */
int  auth_csrf_ok(const Ctx *c, const Session *s);     /* X-CSRF-Token header matches session */
int  auth_require(Ctx *c, Session *s, const char *role);
void auth_student_register(Ctx *c);
void auth_student_login(Ctx *c);
void auth_admin_login(Ctx *c);
void auth_logout(Ctx *c);
void auth_me(Ctx *c);
#endif
