#include "finder_batch.h"
#include "finder_files.h"
#include "volume_capacity.h"
#include "finder_state.h"
#include "session_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int finder_undo_reserve(Session *s, int count) {
    if (count < 0 || count > OSPI_FOLDER_LIMIT) return -1;
    if (count <= (s->expanded_undo ? s->undo_capacity : OSPI_MAX_ITEMS)) return 0;
    FinderUndo *entries = calloc((size_t)count, sizeof *entries);
    if (!entries) { dialog_info(s, "Finder", "There is not enough memory to record Undo. No files were changed."); return -1; }
    memcpy(entries, finder_undo_entries(s), (size_t)s->undo_count * sizeof *entries);
    free(s->expanded_undo); s->expanded_undo = entries; s->undo_capacity = count;
    return 0;
}
void finder_undo_begin(Session *s) {
    s->undo_count = 0;
}
void finder_undo_record(Session *s, int type, const char *parent, const char *destination, const char *name) {
    if (s->undo_count >= (s->expanded_undo ? s->undo_capacity : OSPI_MAX_ITEMS)) return;
    FinderUndo *entry = &finder_undo_entries(s)[s->undo_count++];
    memset(entry, 0, sizeof *entry);
    entry->type = type;
    snprintf(entry->parent, sizeof entry->parent, "%s", parent);
    snprintf(entry->destination, sizeof entry->destination, "%s", destination);
    snprintf(entry->name, sizeof entry->name, "%s", name);
    finder_identity(s->root, type == 2 ? "Trash" : destination, name, entry);
}
static void undo(Session *s) {
    while (s->undo_count) {
        FinderUndo *entry = &finder_undo_entries(s)[s->undo_count - 1];
        FinderUndo live = {0};
        const char *location = entry->type == 2 ? "Trash" : entry->destination;
        if (!entry->identity_valid || finder_identity(s->root, location, entry->name, &live) ||
            live.device != entry->device || live.inode != entry->inode ||
            live.file_kind != entry->file_kind || live.fork_present != entry->fork_present ||
            (live.fork_present && (live.fork_device != entry->fork_device || live.fork_inode != entry->fork_inode))) {
            dialog_info(s, "Undo", "The item or its resource fork has changed identity or is unavailable. Undo left it untouched. Restore the original item at this location before trying again.");
            break;
        }
        char parent[OSPI_PATH], name[OSPI_NAME];
        int result = entry->type == 1 ? finder_rename(s->root, entry->destination, entry->name, entry->original) : entry->type == 2 ? finder_put_away(s->root, entry->name, parent, name) :
            entry->type == 3 ? finder_move(s->root, entry->destination, entry->name, entry->parent) :
            finder_trash(s->root, entry->destination, entry->name, name);
        if (result) {
            dialog_info(s, "Undo", "An item could not be restored. No existing file was replaced. Resolve the conflicting name or missing folder, then choose Undo again to finish the remaining items.");
            break;
        }
        if (entry->type == 1) finder_state_retarget(s, entry->destination, entry->name, entry->destination, entry->original);
        else if (entry->type == 2) finder_state_retarget(s, "Trash", entry->name, parent, name);
        else if (entry->type == 3) finder_state_retarget(s, entry->destination, entry->name, entry->parent, entry->name);
        else finder_state_retarget(s, entry->destination, entry->name, "Trash", name);
        s->undo_count--;
    }
    reload_folders(s);
}
int finder_batch_command(Session *s, int command) {
    if (command == CMD_UNDO && s->undo_count) { undo(s); return 1; }
    if (command != CMD_DUPLICATE && command != CMD_MAKE_ALIAS && command != CMD_FAVORITE &&
        command != CMD_MOVE_TRASH && command != CMD_CLEAR && command != CMD_PUT_AWAY) return 0;
    Window *w = finder_current(s);
    if (!w || w->kind != WIN_FOLDER || w->search_results) return 1;
    int selected = 0;
    for (int i = 0; i < w->nitems; i++) selected += !!window_items(w)[i].selected;
    if (finder_undo_reserve(s, selected)) return 1;
    finder_undo_begin(s);
    int done = 0, failed = 0;
    for (int i = 0; i < w->nitems; i++) {
        if (!window_items(w)[i].selected) continue;
        const char *name = window_items(w)[i].name;
        char created[OSPI_NAME], parent[OSPI_PATH];
        int result;
        if (command == CMD_DUPLICATE) result = finder_duplicate(s->root, w->rel, name, created);
        else if (command == CMD_MAKE_ALIAS || command == CMD_FAVORITE)
            result = finder_alias(s->root, w->rel, name, command == CMD_FAVORITE, created);
        else if (command == CMD_PUT_AWAY) result = finder_put_away(s->root, name, parent, created);
        else result = finder_trash(s->root, w->rel, name, created);
        if (result) { failed++; continue; }
        done++;
        if (command == CMD_PUT_AWAY) {
            finder_state_retarget(s, "Trash", name, parent, created);
            finder_undo_record(s, 4, "Trash", parent, created);
            continue;
        }
        int trash = command == CMD_MOVE_TRASH || command == CMD_CLEAR;
        if (trash) snprintf(s->sound_event, sizeof s->sound_event, "trash.drop");
        if (trash) finder_state_retarget(s, w->rel, name, "Trash", created);
        finder_undo_record(s, trash ? 2 : 4, w->rel,
            command == CMD_FAVORITE ? "Favorites" : w->rel, created);
    }
    reload_folders(s);
    if (failed) {
        char body[200];
        const char *full = volume_disk_full_notice(1);
        snprintf(body, sizeof body, "%d item(s) completed; %d could not be changed. Existing files were not replaced. Undo applies to the completed items when available.", done, failed);
        dialog_info(s, "Finder", full ? full : body);
    }
    return 1;
}
