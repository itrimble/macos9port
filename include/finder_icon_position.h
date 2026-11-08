#ifndef OS999_FINDER_ICON_POSITION_H
#define OS999_FINDER_ICON_POSITION_H
/* Positions belong to an item, so renaming it retains its desktop placement.
 * Read returns 1 for a valid position, 0 for none, -1 for invalid/unavailable.
 * Write returns 0 only after the host saved the complete attribute. */
int finder_icon_position_read(const char *root, const char *rel, const char *name, int *x, int *y);
int finder_icon_position_write(const char *root, const char *rel, const char *name, int x, int y);
#endif
