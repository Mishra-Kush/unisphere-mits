#ifndef JSON_H
#define JSON_H
#include <stddef.h>
#define JSON_MAX_FIELDS 24
#define JSON_MAX_STRING 4096
typedef struct { char key[40]; char *val; } JField;
typedef struct { JField f[JSON_MAX_FIELDS]; int n; } JObj;
/* Parses ONE flat JSON object (string/number/true/false/null values; no nesting). 0 = ok. */
int json_parse_flat(const char *s, JObj *o);
const char *json_get(const JObj *o, const char *key);   /* NULL if missing or null */
void json_free(JObj *o);

typedef struct { char *d; size_t n, cap; } Buf;
void buf_init(Buf *b);
void buf_free(Buf *b);
int  buf_puts(Buf *b, const char *s);
int  buf_printf(Buf *b, const char *fmt, ...);
void buf_json_str(Buf *b, const char *s);               /* adds a quoted, escaped JSON string */
#endif
