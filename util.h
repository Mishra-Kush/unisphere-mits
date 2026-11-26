#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <sqlite3.h>
#include "json.h"
typedef struct { char today[16], hm[8]; } Now;           /* college-local time (IST, UTC+5:30) */
void ist_now(Now *n);
int  qs_get(const char *query, const char *key, char *out, size_t n);  /* URL-decoded; 1 if present */
int  path_id(const char *path, const char *prefix, int *id, const char **rest);
int  valid_date(const char *s);                           /* YYYY-MM-DD, real calendar date */
int  valid_time(const char *s);                           /* HH:MM */
void like_pattern(const char *in, char *out, size_t n);   /* %escaped% for LIKE ... ESCAPE '\' */
void instagram_url(const char *handle, char *out, size_t n);
void slugify(const char *in, char *out, size_t n);
int  in_list(const char *v, const char *const *list);
void audit_log(sqlite3 *db, int user_id, const char *action, const char *target);
/* JSON helpers: every kv_* emits ,"key":value */
void kv_s(Buf *b, const char *k, const char *v);
void kv_sn(Buf *b, const char *k, const char *v);        /* null when empty */
void kv_i(Buf *b, const char *k, long long v);
void kv_in(Buf *b, const char *k, long long v);          /* null when <= 0 */
void kv_b(Buf *b, const char *k, int v);
/* tiny parameterised query builder (all values are bound, never concatenated) */
typedef struct { Buf sql; char vals[16][170]; int n; } Q;
void q_init(Q *q, const char *base);
void q_param(Q *q, const char *val);
void q_and(Q *q, const char *cond, const char *val);     /* cond has 0 or 1 '?' */
void q_like(Q *q, const char *cond, const char *pattern, int times);
sqlite3_stmt *q_prepare(Q *q, sqlite3 *db);              /* binds values in order */
static inline const char *txt_(sqlite3_stmt *s, int i) { const unsigned char *t = sqlite3_column_text(s, i); return t ? (const char *)t : ""; }
#define TXT(s, i) txt_((s), (i))
#endif
