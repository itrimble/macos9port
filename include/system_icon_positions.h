#ifndef OS999_SYSTEM_ICON_POSITIONS_H
#define OS999_SYSTEM_ICON_POSITIONS_H
typedef struct { int mask, x[2], y[2]; } SystemIconPositions;
/* A missing preference returns 0, a valid one 1, and invalid/unavailable data -1.
 * Failed reads leave defaults. Writes replace only this volume's preference. */
int system_icon_positions_load(const char *root, SystemIconPositions *positions);
int system_icon_positions_save(const char *root, const SystemIconPositions *positions);
#endif
