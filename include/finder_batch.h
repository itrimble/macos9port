#ifndef OS999_FINDER_BATCH_H
#define OS999_FINDER_BATCH_H
#include "ospi.h"
void finder_undo_begin(Session *s);
int finder_undo_reserve(Session *s, int count);
#define finder_undo_entries(s) ((s)->expanded_undo ? (s)->expanded_undo : (s)->undo_entries)
void finder_undo_record(Session *s, int type, const char *parent, const char *destination, const char *name);
int finder_batch_command(Session *s, int command);
#endif
