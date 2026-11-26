#ifndef FIELDS_H
#define FIELDS_H
#include "api.h"
#define FIELD_MAX 2100
#define MAX_FIELDS 20
typedef enum { F_TEXT, F_ENUM, F_DATE, F_TIME, F_INT, F_BOOL, F_IMAGE, F_HANDLE } FKind;
typedef struct { const char *name, *column; FKind kind; int min, max; const char *const *list; int required, nullable; const char *label; } Field;
int fields_validate(Ctx *c, JObj *o, const Field *fs, int nf, int creating, char (*out)[FIELD_MAX], int *present, int *isnull);
const char *fields_val(const Field *fs, int nf, char (*out)[FIELD_MAX], const int *present, const int *isnull, const char *name);
int fields_insert(sqlite3 *db, const char *table, const Field *fs, int nf, char (*out)[FIELD_MAX], const int *present, const int *isnull);
int fields_update(sqlite3 *db, const char *table, int id, const Field *fs, int nf, char (*out)[FIELD_MAX], const int *present, const int *isnull);
#endif
