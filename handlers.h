#ifndef HANDLERS_H
#define HANDLERS_H
#include "api.h"
#include "auth.h"
#include "util.h"
typedef struct { const char *query; int admin; int club_id; int uid; int mine; int limit_default; } EvOpts;
const char *event_state(const char *status, int enabled, const char *date, const char *start, const char *end,
                        const char *reg_opens, const char *reg_closes, int capacity, int count, const Now *now);
int  events_write_list(Ctx *c, Buf *b, const EvOpts *o, const Now *now);
int  clubs_write_list(Ctx *c, Buf *b, const char *query, int admin, int limit_default);
/* clubs.c */
void clubs_list(Ctx *c, int admin);  void clubs_get(Ctx *c, int id);  void clubs_events(Ctx *c, int id);
void clubs_create(Ctx *c);  void clubs_update(Ctx *c, int id);  void clubs_archive(Ctx *c, int id);
/* events.c */
void events_list(Ctx *c, int admin);  void events_get(Ctx *c, int id);  void search_all(Ctx *c);
void events_create(Ctx *c);  void events_update(Ctx *c, int id);  void events_archive(Ctx *c, int id);
/* registrations.c */
void reg_register(Ctx *c, int event_id);  void reg_cancel(Ctx *c, int event_id);
void reg_mine(Ctx *c);  void reg_attendees(Ctx *c, int event_id);
/* announcements.c */
void ann_list(Ctx *c, int admin);  void ann_create(Ctx *c);  void ann_update(Ctx *c, int id);  void ann_delete(Ctx *c, int id);
void admin_dashboard(Ctx *c);
#endif
