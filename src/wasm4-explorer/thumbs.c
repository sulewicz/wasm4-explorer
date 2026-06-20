#include "explorer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool is_file(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int expected_fingerprint(const W4XCatalogEntry *entry, char *dest,
                                size_t dest_size)
{
    int written = snprintf(dest, dest_size, "%s\t%s\n",
                           entry->row_fingerprint, entry->image_url);
    return written < 0 || (size_t)written >= dest_size ? -1 : 0;
}

int w4x_thumbnail_path(const W4XPathSet *paths, const W4XCatalogEntry *entry,
                       char *image_path, size_t image_path_size)
{
    int written = snprintf(image_path, image_path_size, "%s/images/%s.png",
                           paths->cache_dir, entry->slug);
    return written < 0 || (size_t)written >= image_path_size ? -1 : 0;
}

int w4x_thumbnail_fingerprint_path(const W4XPathSet *paths,
                                   const W4XCatalogEntry *entry,
                                   char *fingerprint_path,
                                   size_t fingerprint_path_size)
{
    int written = snprintf(fingerprint_path, fingerprint_path_size,
                           "%s/images/%s.fingerprint",
                           paths->cache_dir, entry->slug);
    return written < 0 || (size_t)written >= fingerprint_path_size ? -1 : 0;
}

W4XThumbnailStatus w4x_thumbnail_status(const W4XPathSet *paths,
                                        const W4XCatalogEntry *entry,
                                        bool remove_stale)
{
    char image_path[W4X_PATH_MAX];
    char fingerprint_path[W4X_PATH_MAX];
    if (w4x_thumbnail_path(paths, entry, image_path, sizeof(image_path)) != 0 ||
        w4x_thumbnail_fingerprint_path(paths, entry, fingerprint_path,
                                       sizeof(fingerprint_path)) != 0)
        return W4X_THUMBNAIL_MISSING;

    if (!is_file(image_path) || !is_file(fingerprint_path))
        return W4X_THUMBNAIL_MISSING;

    char expected[W4X_FINGERPRINT_MAX + W4X_URL_MAX + 4];
    if (expected_fingerprint(entry, expected, sizeof(expected)) != 0)
        return W4X_THUMBNAIL_STALE;

    FILE *fp = fopen(fingerprint_path, "r");
    if (fp == NULL)
        return W4X_THUMBNAIL_STALE;
    char actual[sizeof(expected)];
    bool ok = fgets(actual, sizeof(actual), fp) != NULL &&
              strcmp(actual, expected) == 0;
    fclose(fp);

    if (ok)
        return W4X_THUMBNAIL_VALID;

    if (remove_stale) {
        unlink(image_path);
        unlink(fingerprint_path);
    }
    return W4X_THUMBNAIL_STALE;
}

int w4x_thumbnail_mark_valid(const W4XPathSet *paths,
                             const W4XCatalogEntry *entry)
{
    char fingerprint_path[W4X_PATH_MAX];
    if (w4x_thumbnail_fingerprint_path(paths, entry, fingerprint_path,
                                       sizeof(fingerprint_path)) != 0)
        return -1;

    char expected[W4X_FINGERPRINT_MAX + W4X_URL_MAX + 4];
    if (expected_fingerprint(entry, expected, sizeof(expected)) != 0)
        return -1;

    FILE *fp = fopen(fingerprint_path, "w");
    if (fp == NULL)
        return -1;
    fputs(expected, fp);
    return fclose(fp) == 0 ? 0 : -1;
}

const char *w4x_thumbnail_status_string(W4XThumbnailStatus status)
{
    switch (status) {
    case W4X_THUMBNAIL_VALID:
        return "valid";
    case W4X_THUMBNAIL_STALE:
        return "stale";
    case W4X_THUMBNAIL_MISSING:
    default:
        return "missing";
    }
}
