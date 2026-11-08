#include "finder_files.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static void file(const char *root, const char *name, const char *body) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "wb"); CHECK(f != NULL);
    if (f) { CHECK(fputs(body, f) >= 0); CHECK(!fclose(f)); }
}
static int content(const char *root, const char *name, const char *body) {
    char path[1024], text[100] = {0}; snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fread(text, 1, sizeof text - 1, f); fclose(f); return !strcmp(text, body);
}
int main(void) {
    char root[] = "/tmp/os999-finder-files-XXXXXX", outside[] = "/tmp/os999-finder-outside-XXXXXX";
    CHECK(mkdtemp(root)); CHECK(mkdtemp(outside)); char out[OSPI_NAME], path[1024];
    file(root, "Game", "data"); file(root, "Game.rsrc", "resource");
    CHECK(!finder_duplicate(root, "", "Game", out)); CHECK(!strcmp(out, "Game copy"));
    CHECK(content(root, "Game copy", "data")); CHECK(content(root, "Game copy.rsrc", "resource"));
    CHECK(!finder_duplicate(root, "", "Game", out)); CHECK(!strcmp(out, "Game copy 2"));
    CHECK(!finder_rename(root, "", "Game", "Renamed"));
    CHECK(content(root, "Renamed", "data")); CHECK(content(root, "Renamed.rsrc", "resource"));
    CHECK(finder_rename(root, "", "Renamed", "Game copy") < 0);
    CHECK(content(root, "Renamed", "data")); CHECK(content(root, "Game copy", "data"));
    file(root, "Reserved.rsrc", "unrelated");
    CHECK(finder_rename(root, "", "Renamed", "Reserved") < 0);
    CHECK(content(root, "Reserved.rsrc", "unrelated"));
    CHECK(finder_rename(root, "", "Renamed", "../escape") < 0);
    CHECK(finder_duplicate(root, "..", "Renamed", out) < 0);
    snprintf(path, sizeof path, "%s/Empty", root); CHECK(!mkdir(path, 0700));
    CHECK(!finder_duplicate(root, "", "Empty", out));
    snprintf(path, sizeof path, "%s/%s", root, out); struct stat st; CHECK(!stat(path, &st) && S_ISDIR(st.st_mode));
    snprintf(path, sizeof path, "%s/Tree", root); CHECK(!mkdir(path, 0700));
    file(root, "Tree/File", "inside");
    CHECK(!finder_duplicate(root, "", "Tree", out)); CHECK(content(root, "Tree copy/File", "inside"));
    file(outside, "Secret", "private"); snprintf(path, sizeof path, "%s/Link", root); CHECK(!symlink(outside, path));
    CHECK(finder_duplicate(root, "Link", "Secret", out) < 0);
    CHECK(finder_duplicate(root, "", "Link", out) < 0);
    snprintf(path, sizeof path, "%s/Tree/Link", root); CHECK(!symlink(outside, path));
    CHECK(finder_duplicate(root, "", "Tree", out) < 0);
    snprintf(path, sizeof path, "%s/Tree copy 2", root); CHECK(lstat(path, &st) < 0);
    CHECK(content(outside, "Secret", "private"));
    CHECK(!finder_trash(root, "", "Renamed", out)); CHECK(!strcmp(out, "Renamed"));
    CHECK(content(root, "Trash/Renamed", "data")); CHECK(content(root, "Trash/Renamed.rsrc", "resource"));
    char parent[OSPI_PATH], restored[OSPI_NAME];
    file(root, "Renamed", "collision");
    CHECK(finder_put_away(root, out, parent, restored) < 0);
    CHECK(content(root, "Renamed", "collision")); CHECK(content(root, "Trash/Renamed", "data"));
    snprintf(path, sizeof path, "%s/Renamed", root); CHECK(!unlink(path));
    CHECK(!finder_put_away(root, out, parent, restored)); CHECK(!parent[0]); CHECK(!strcmp(restored, "Renamed"));
    CHECK(content(root, "Renamed.rsrc", "resource"));
    CHECK(finder_trash(root, "", "System Folder", out) < 0);
    CHECK(finder_trash(root, "Trash", "Renamed", out) < 0);
    CHECK(!finder_alias(root, "", "Renamed", 0, out));
    char target[OSPI_PATH]; CHECK(finder_alias_target(root, "", out, target) == 1);
    CHECK(!strcmp(target, "Renamed")); CHECK(!finder_alias(root, "", "Renamed", 1, out));
    CHECK(finder_alias_target(root, "Favorites", out, target) == 1);
    CHECK(!strcmp(target, "Renamed"));
    file(root, "Evil alias", "OS999-ALIAS-1\n../Secret");
    CHECK(finder_alias_target(root, "", "Evil alias", target) < 0);
    snprintf(path, sizeof path, "%s/Destination", root); CHECK(!mkdir(path, 0700));
    file(root, "Move", "payload"); file(root, "Move.rsrc", "fork");
    CHECK(!finder_copy_to(root, "", "Move", "Destination", out));
    CHECK(content(root, "Destination/Move.rsrc", "fork")); CHECK(content(root, "Move", "payload"));
    CHECK(finder_move(root, "", "Move", "Destination") < 0);
    CHECK(!finder_rename(root, "Destination", "Move", "Saved"));
    CHECK(!finder_move(root, "", "Move", "Destination"));
    CHECK(content(root, "Destination/Move", "payload")); CHECK(content(root, "Destination/Move.rsrc", "fork"));
    CHECK(!content(root, "Move", "payload"));
    CHECK(finder_copy_to(root, "", "Tree", "Tree", out) < 0);
    CHECK(finder_move(root, "", "Tree", "Tree") < 0);
    /* Identity-based aliases follow external moves and reject replacement names. */
    char alias[OSPI_NAME];
    file(root,"Original","original");
    CHECK(!finder_alias(root,"","Original",0,alias));
    CHECK(finder_is_alias(root,"",alias));
    CHECK(!finder_rename(root,"","Original","Moved original"));
    file(root,"Original","replacement");
    CHECK(finder_alias_target(root,"",alias,target)==1 && !strcmp(target,"Moved original"));
    CHECK(!finder_move(root,"","Moved original","Destination"));
    CHECK(finder_alias_target(root,"",alias,target)==1 && !strcmp(target,"Destination/Moved original"));
    CHECK(!finder_rename(root,"","Destination","New destination"));
    CHECK(finder_alias_target(root,"",alias,target)==1 && !strcmp(target,"New destination/Moved original"));
    CHECK(!finder_trash(root,"New destination","Moved original",out));
    CHECK(finder_alias_target(root,"",alias,target)==1 && !strcmp(target,"Trash/Moved original"));
    CHECK(!finder_put_away(root,out,parent,restored));
    CHECK(finder_alias_target(root,"",alias,target)==1 && !strcmp(target,"New destination/Moved original"));
    CHECK(!finder_alias(root,"","New destination",1,out));
    CHECK(!finder_rename(root,"","New destination","Folder moved again"));
    CHECK(finder_alias_target(root,"Favorites",out,target)==1 && !strcmp(target,"Folder moved again"));
    snprintf(path,sizeof path,"%s/Folder moved again/Moved original",root);CHECK(!unlink(path));
    CHECK(finder_is_alias(root,"",alias));
    CHECK(finder_alias_target(root,"",alias,target)<0);
    CHECK(content(root,"Original","replacement"));
    file(root,"Legacy alias","OS999-ALIAS-1\nOriginal");
    CHECK(finder_alias_target(root,"","Legacy alias",target)==1 && !strcmp(target,"Original"));
    file(root,"Invalid alias","OS999-ALIAS-2\n1 2 3 4\n../Secret");
    CHECK(!finder_is_alias(root,"","Invalid alias"));
    CHECK(finder_alias_target(root,"","Invalid alias",target)<0);
    file(root,"Invalid numbers","OS999-ALIAS-2\n1 2 3 1000000000\nOriginal");
    CHECK(finder_alias_target(root,"","Invalid numbers",target)<0);
    file(root,"Overflow identity","OS999-ALIAS-2\n184467440737095516160000 2 3 4\nOriginal");
    CHECK(finder_alias_target(root,"","Overflow identity",target)<0);
    file(root,"Escaping legacy alias","OS999-ALIAS-1\nLink/Secret");
    CHECK(finder_is_alias(root,"","Escaping legacy alias"));
    CHECK(finder_alias_target(root,"","Escaping legacy alias",target)<0);
    CHECK(!archive_remove_tree(root)); CHECK(!archive_remove_tree(outside));
    printf("Finder files: %d checks, %d failures\n", checks, failures); return !!failures;
}
