#include "explorer.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct W4XInstalledMeta {
    char slug[W4X_SLUG_MAX];
    char title[W4X_TITLE_MAX];
    char author[W4X_AUTHOR_MAX];
    char authors_json[W4X_METADATA_MAX];
    char date[W4X_DATE_MAX];
    char description[W4X_METADATA_MAX];
    char cart_url[W4X_URL_MAX];
    char image_url[W4X_URL_MAX];
    char page_url[W4X_URL_MAX];
    char manual_url[W4X_URL_MAX];
    char license[W4X_METADATA_MAX];
    char remote_hash[W4X_HASH_MAX];
    char image_hash[W4X_HASH_MAX];
    char remote_updated_at[W4X_DATE_MAX];
    char installed_filename[W4X_TITLE_MAX];
    char installed_image[W4X_TITLE_MAX];
    char installed_at[W4X_DATE_MAX];
} W4XInstalledMeta;

typedef struct W4XNameList {
    char **items;
    size_t count;
} W4XNameList;

static bool is_file(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool path_exists(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

static int copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0)
        return -1;
    if (src == NULL)
        src = "";

    int written = snprintf(dest, dest_size, "%s", src);
    return written < 0 || (size_t)written >= dest_size ? -1 : 0;
}

static bool has_prefix(const char *value, const char *prefix)
{
    return value != NULL && prefix != NULL &&
           strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool has_suffix_ci(const char *value, const char *suffix)
{
    if (value == NULL || suffix == NULL)
        return false;
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len &&
           strcasecmp(value + value_len - suffix_len, suffix) == 0;
}

static int path_join(char *dest, size_t dest_size, const char *left,
                     const char *right)
{
    if (left == NULL || right == NULL)
        return -1;

    size_t left_len = strlen(left);
    const char *sep = left_len > 0 && left[left_len - 1] == '/' ? "" : "/";
    int written = snprintf(dest, dest_size, "%s%s%s", left, sep, right);
    return written < 0 || (size_t)written >= dest_size ? -1 : 0;
}

static void strip_trailing_slashes(char *path)
{
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
}

static int mkdir_p(const char *dir_path)
{
    if (dir_path == NULL || dir_path[0] == '\0')
        return -1;

    char path[W4X_PATH_MAX];
    if (copy_string(path, sizeof(path), dir_path) != 0)
        return -1;

    strip_trailing_slashes(path);
    for (char *p = path + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(path, 0777) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }

    if (mkdir(path, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int make_tmp_path(char *dest, size_t dest_size, const char *path,
                         const char *suffix)
{
    int written = snprintf(dest, dest_size, "%s.%ld.%s", path, (long)getpid(),
                           suffix);
    return written < 0 || (size_t)written >= dest_size ? -1 : 0;
}

static int copy_file(const char *src, const char *dest)
{
    FILE *in = fopen(src, "rb");
    if (in == NULL)
        return -1;

    FILE *out = fopen(dest, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }

    char buf[8192];
    int rc = 0;
    while (!feof(in)) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n > 0 && fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
        if (ferror(in)) {
            rc = -1;
            break;
        }
    }

    if (fclose(out) != 0)
        rc = -1;
    fclose(in);
    return rc;
}

static const char *normalized_catalog_value(const char *value)
{
    if (value == NULL || value[0] == '\0' || strcmp(value, "-") == 0 ||
        strcmp(value, "unknown") == 0)
        return "";
    return value;
}

static void current_timestamp(char *dest, size_t dest_size)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    if (now != (time_t)-1 && localtime_r(&now, &tm_buf) != NULL &&
        strftime(dest, dest_size, "%Y-%m-%dT%H:%M:%S%z", &tm_buf) > 0)
        return;
    copy_string(dest, dest_size, "unknown-time");
}

static void meta_from_entry(const W4XCatalogEntry *entry, W4XInstalledMeta *meta)
{
    memset(meta, 0, sizeof(*meta));
    copy_string(meta->slug, sizeof(meta->slug), entry->slug);
    copy_string(meta->title, sizeof(meta->title), entry->title);
    copy_string(meta->author, sizeof(meta->author), entry->author);
    copy_string(meta->authors_json, sizeof(meta->authors_json),
                entry->authors_json[0] != '\0' ? entry->authors_json : "[]");
    copy_string(meta->date, sizeof(meta->date),
                normalized_catalog_value(entry->date));
    copy_string(meta->description, sizeof(meta->description), entry->description);
    copy_string(meta->cart_url, sizeof(meta->cart_url), entry->cart_url);
    copy_string(meta->image_url, sizeof(meta->image_url), entry->image_url);
    copy_string(meta->page_url, sizeof(meta->page_url), entry->page_url);
    copy_string(meta->manual_url, sizeof(meta->manual_url), entry->manual_url);
    copy_string(meta->license, sizeof(meta->license),
                normalized_catalog_value(entry->license));
    copy_string(meta->remote_hash, sizeof(meta->remote_hash),
                normalized_catalog_value(entry->wasm_sha256));
    copy_string(meta->image_hash, sizeof(meta->image_hash),
                normalized_catalog_value(entry->image_sha256));
    copy_string(meta->remote_updated_at, sizeof(meta->remote_updated_at),
                normalized_catalog_value(entry->remote_updated_at));
}

static void write_shell_quoted(FILE *fp, const char *value)
{
    fputc('\'', fp);
    for (const char *p = value != NULL ? value : ""; *p != '\0'; p++) {
        if (*p == '\'')
            fputs("'\"'\"'", fp);
        else
            fputc(*p, fp);
    }
    fputc('\'', fp);
}

static void write_env_var(FILE *fp, const char *key, const char *value)
{
    fprintf(fp, "%s=", key);
    write_shell_quoted(fp, value);
    fputc('\n', fp);
}

static int write_meta_file(const char *path, const W4XInstalledMeta *meta,
                           bool include_installed)
{
    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), path, "tmp") != 0)
        return -1;

    FILE *fp = fopen(tmp, "w");
    if (fp == NULL)
        return -1;

    write_env_var(fp, "SLUG", meta->slug);
    write_env_var(fp, "TITLE", meta->title);
    write_env_var(fp, "AUTHOR", meta->author);
    write_env_var(fp, "AUTHORS_JSON", meta->authors_json);
    write_env_var(fp, "DATE", meta->date);
    write_env_var(fp, "DESCRIPTION", meta->description);
    write_env_var(fp, "CART_URL", meta->cart_url);
    write_env_var(fp, "IMAGE_URL", meta->image_url);
    write_env_var(fp, "PAGE_URL", meta->page_url);
    write_env_var(fp, "MANUAL_URL", meta->manual_url);
    write_env_var(fp, "LICENSE", meta->license);
    write_env_var(fp, "REMOTE_HASH", meta->remote_hash);
    write_env_var(fp, "IMAGE_HASH", meta->image_hash);
    write_env_var(fp, "REMOTE_UPDATED_AT", meta->remote_updated_at);
    if (include_installed) {
        write_env_var(fp, "INSTALLED_FILENAME", meta->installed_filename);
        write_env_var(fp, "INSTALLED_IMAGE", meta->installed_image);
        write_env_var(fp, "INSTALLED_AT", meta->installed_at);
    }

    int close_rc = fclose(fp);
    if (close_rc != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int parse_env_value(const char *raw, char *dest, size_t dest_size)
{
    if (dest_size == 0)
        return -1;

    size_t pos = 0;
    if (raw[0] == '\'') {
        const char *p = raw + 1;
        while (*p != '\0' && *p != '\n' && *p != '\r') {
            if (*p == '\'') {
                if (strncmp(p, "'\"'\"'", 5) == 0) {
                    if (pos + 1 >= dest_size)
                        break;
                    dest[pos++] = '\'';
                    p += 5;
                    continue;
                }
                break;
            }
            if (pos + 1 >= dest_size)
                break;
            dest[pos++] = *p++;
        }
    }
    else {
        const char *p = raw;
        while (*p != '\0' && *p != '\n' && *p != '\r') {
            if (pos + 1 >= dest_size)
                break;
            dest[pos++] = *p++;
        }
    }

    dest[pos] = '\0';
    return 0;
}

static void maybe_copy_env_field(const char *line, const char *key, char *dest,
                                 size_t dest_size)
{
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) == 0 && line[key_len] == '=')
        parse_env_value(line + key_len + 1, dest, dest_size);
}

static int load_meta_file(const char *path, W4XInstalledMeta *meta)
{
    memset(meta, 0, sizeof(*meta));
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return -1;

    char line[4096];
    while (fgets(line, sizeof(line), fp) != NULL) {
        maybe_copy_env_field(line, "SLUG", meta->slug, sizeof(meta->slug));
        maybe_copy_env_field(line, "TITLE", meta->title, sizeof(meta->title));
        maybe_copy_env_field(line, "AUTHOR", meta->author, sizeof(meta->author));
        maybe_copy_env_field(line, "AUTHORS_JSON", meta->authors_json,
                             sizeof(meta->authors_json));
        maybe_copy_env_field(line, "DATE", meta->date, sizeof(meta->date));
        maybe_copy_env_field(line, "DESCRIPTION", meta->description,
                             sizeof(meta->description));
        maybe_copy_env_field(line, "CART_URL", meta->cart_url,
                             sizeof(meta->cart_url));
        maybe_copy_env_field(line, "IMAGE_URL", meta->image_url,
                             sizeof(meta->image_url));
        maybe_copy_env_field(line, "PAGE_URL", meta->page_url,
                             sizeof(meta->page_url));
        maybe_copy_env_field(line, "MANUAL_URL", meta->manual_url,
                             sizeof(meta->manual_url));
        maybe_copy_env_field(line, "LICENSE", meta->license,
                             sizeof(meta->license));
        maybe_copy_env_field(line, "REMOTE_HASH", meta->remote_hash,
                             sizeof(meta->remote_hash));
        maybe_copy_env_field(line, "IMAGE_HASH", meta->image_hash,
                             sizeof(meta->image_hash));
        maybe_copy_env_field(line, "REMOTE_UPDATED_AT", meta->remote_updated_at,
                             sizeof(meta->remote_updated_at));
        maybe_copy_env_field(line, "INSTALLED_FILENAME", meta->installed_filename,
                             sizeof(meta->installed_filename));
        maybe_copy_env_field(line, "INSTALLED_IMAGE", meta->installed_image,
                             sizeof(meta->installed_image));
        maybe_copy_env_field(line, "INSTALLED_AT", meta->installed_at,
                             sizeof(meta->installed_at));
    }

    fclose(fp);
    return 0;
}

static bool filename_char_is_blocked(unsigned char c)
{
    return c < 32 || c == '/' || c == ':' || c == '*' || c == '?' ||
           c == '"' || c == '<' || c == '>' || c == '|';
}

static void sanitize_stem(const char *value, char *dest, size_t dest_size)
{
    dest[0] = '\0';
    if (value == NULL || dest_size == 0)
        return;

    char tmp[W4X_TITLE_MAX * 2];
    size_t tmp_pos = 0;
    for (const unsigned char *p = (const unsigned char *)value;
         *p != '\0' && tmp_pos + 1 < sizeof(tmp); p++) {
        if (filename_char_is_blocked(*p) || isspace(*p))
            tmp[tmp_pos++] = ' ';
        else
            tmp[tmp_pos++] = (char)*p;
    }
    tmp[tmp_pos] = '\0';

    size_t start = 0;
    while (tmp[start] == ' ')
        start++;
    size_t end = strlen(tmp);
    while (end > start && tmp[end - 1] == ' ')
        end--;

    bool pending_space = false;
    size_t pos = 0;
    for (size_t i = start; i < end && pos + 1 < dest_size; i++) {
        if (tmp[i] == ' ') {
            pending_space = true;
            continue;
        }
        if (pending_space && pos > 0 && dest[pos - 1] != '-') {
            dest[pos++] = '-';
            if (pos + 1 >= dest_size)
                break;
        }
        pending_space = false;
        if (tmp[i] == '-' && pos > 0 && dest[pos - 1] == '-')
            continue;
        dest[pos++] = tmp[i];
    }

    while (pos > 0 && dest[pos - 1] == '-')
        pos--;
    dest[pos] = '\0';

    while (dest[0] == '-')
        memmove(dest, dest + 1, strlen(dest));
}

static int build_unique_filename(const char *rom_dir, const char *title,
                                 const char *slug, char *dest,
                                 size_t dest_size)
{
    char stem[W4X_TITLE_MAX];
    sanitize_stem(title, stem, sizeof(stem));
    if (stem[0] == '\0')
        sanitize_stem(slug, stem, sizeof(stem));
    if (stem[0] == '\0')
        copy_string(stem, sizeof(stem), "cart");

    for (int index = 1; index < 10000; index++) {
        int written = index == 1 ?
            snprintf(dest, dest_size, "%s.wasm", stem) :
            snprintf(dest, dest_size, "%s-%d.wasm", stem, index);
        if (written < 0 || (size_t)written >= dest_size)
            return -1;

        char path[W4X_PATH_MAX];
        if (path_join(path, sizeof(path), rom_dir, dest) != 0)
            return -1;
        if (!path_exists(path))
            return 0;
    }

    return -1;
}

static int run_wget(const char *url, const char *dest)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execlp("wget", "wget", "-q", "-O", dest, url, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int run_sha256sum(const char *path, char *dest, size_t dest_size)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[1]);
        execlp("sha256sum", "sha256sum", path, (char *)NULL);
        _exit(127);
    }

    close(pipe_fds[1]);
    size_t pos = 0;
    while (pos + 1 < dest_size) {
        ssize_t n = read(pipe_fds[0], dest + pos, dest_size - pos - 1);
        if (n <= 0)
            break;
        pos += (size_t)n;
    }
    close(pipe_fds[0]);
    dest[pos] = '\0';

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (!WIFEXITED(status))
        return -1;
    if (WEXITSTATUS(status) == 127)
        return -2;
    if (WEXITSTATUS(status) != 0)
        return -1;

    char hash[W4X_HASH_MAX];
    size_t hash_pos = 0;
    for (const char *p = dest; *p != '\0' && hash_pos + 1 < sizeof(hash); p++) {
        if (isxdigit((unsigned char)*p))
            hash[hash_pos++] = (char)tolower((unsigned char)*p);
        else
            break;
    }
    hash[hash_pos] = '\0';
    if (hash_pos == 0)
        return -1;
    return copy_string(dest, dest_size, hash);
}

static int verify_sha256_if_available(const char *path, const char *expected)
{
    expected = normalized_catalog_value(expected);
    if (expected[0] == '\0')
        return 0;

    char actual[W4X_HASH_MAX];
    int rc = run_sha256sum(path, actual, sizeof(actual));
    if (rc == -2)
        return 0;
    if (rc != 0)
        return -1;
    return strcasecmp(actual, expected) == 0 ? 0 : -1;
}

static int copy_or_download_atomic(const char *source_url, const char *dest_path,
                                   const char *expected_hash)
{
    if (source_url == NULL || source_url[0] == '\0')
        return -1;

    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), dest_path, "tmp") != 0)
        return -1;
    unlink(tmp);

    int rc = -1;
    if (has_prefix(source_url, "file://")) {
        const char *source_path = source_url + strlen("file://");
        if (is_file(source_path))
            rc = copy_file(source_path, tmp);
    }
    else if (has_prefix(source_url, "http://") || has_prefix(source_url, "https://")) {
        rc = run_wget(source_url, tmp);
    }
    else if (is_file(source_url)) {
        rc = copy_file(source_url, tmp);
    }

    if (rc != 0 || verify_sha256_if_available(tmp, expected_hash) != 0 ||
        rename(tmp, dest_path) != 0) {
        unlink(tmp);
        return -1;
    }

    return 0;
}

static int copy_file_atomic(const char *source_path, const char *dest_path,
                            const char *expected_hash)
{
    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), dest_path, "tmp") != 0)
        return -1;
    unlink(tmp);

    if (copy_file(source_path, tmp) != 0 ||
        verify_sha256_if_available(tmp, expected_hash) != 0 ||
        rename(tmp, dest_path) != 0) {
        unlink(tmp);
        return -1;
    }

    return 0;
}

static int install_image(const W4XPathSet *paths, const W4XCatalogEntry *entry,
                         const char *img_dir, const char *cart_stem,
                         W4XInstalledMeta *meta, W4XInstallResult *result)
{
    char image_filename[W4X_TITLE_MAX];
    int written = snprintf(image_filename, sizeof(image_filename), "%s.png", cart_stem);
    if (written < 0 || (size_t)written >= sizeof(image_filename))
        return -1;

    char image_path[W4X_PATH_MAX];
    if (path_join(image_path, sizeof(image_path), img_dir, image_filename) != 0)
        return -1;

    char image_rel[W4X_TITLE_MAX];
    written = snprintf(image_rel, sizeof(image_rel), "Imgs/%s", image_filename);
    if (written < 0 || (size_t)written >= sizeof(image_rel))
        return -1;

    if (is_file(image_path)) {
        if (verify_sha256_if_available(image_path, meta->image_hash) != 0)
            return -1;
        result->reused_image = true;
        result->installed_image = true;
    }
    else {
        char cached_image[W4X_PATH_MAX];
        bool installed = false;
        if (w4x_thumbnail_status(paths, entry, true) == W4X_THUMBNAIL_VALID &&
            w4x_thumbnail_path(paths, entry, cached_image, sizeof(cached_image)) == 0 &&
            is_file(cached_image) &&
            copy_file_atomic(cached_image, image_path, meta->image_hash) == 0) {
            installed = true;
        }
        else if (meta->image_url[0] != '\0' &&
                 copy_or_download_atomic(meta->image_url, image_path,
                                         meta->image_hash) == 0) {
            installed = true;

            if (w4x_thumbnail_path(paths, entry, cached_image,
                                   sizeof(cached_image)) == 0) {
                char cache_dir[W4X_PATH_MAX];
                if (path_join(cache_dir, sizeof(cache_dir), paths->cache_dir,
                              "images") == 0 && mkdir_p(cache_dir) == 0 &&
                    copy_file_atomic(image_path, cached_image, meta->image_hash) == 0)
                    w4x_thumbnail_mark_valid(paths, entry);
            }
        }

        if (!installed && meta->image_url[0] != '\0')
            return -1;
        result->installed_image = installed;
    }

    if (result->installed_image) {
        copy_string(meta->installed_image, sizeof(meta->installed_image), image_rel);
        copy_string(result->image_rel, sizeof(result->image_rel), image_rel);
        copy_string(result->image_path, sizeof(result->image_path), image_path);
    }
    return 0;
}

static void json_escape(FILE *fp, const char *value)
{
    for (const unsigned char *p = (const unsigned char *)(value != NULL ? value : "");
         *p != '\0'; p++) {
        switch (*p) {
        case '\\':
            fputs("\\\\", fp);
            break;
        case '"':
            fputs("\\\"", fp);
            break;
        case '\t':
            fputs("\\t", fp);
            break;
        case '\r':
            fputs("\\r", fp);
            break;
        case '\n':
            fputs("\\n", fp);
            break;
        default:
            if (*p < 32)
                fprintf(fp, "\\u%04x", *p);
            else
                fputc(*p, fp);
            break;
        }
    }
}

static int write_installed_index_json(const char *index_tsv,
                                      const char *index_json)
{
    char tmp_json[W4X_PATH_MAX];
    if (make_tmp_path(tmp_json, sizeof(tmp_json), index_json, "tmp") != 0)
        return -1;

    FILE *in = fopen(index_tsv, "r");
    FILE *out = fopen(tmp_json, "w");
    if (in == NULL || out == NULL) {
        if (in != NULL)
            fclose(in);
        if (out != NULL)
            fclose(out);
        unlink(tmp_json);
        return -1;
    }

    fputs("{\n  \"items\": [\n", out);
    char line[4096];
    bool first_line = true;
    bool first_item = true;
    while (fgets(line, sizeof(line), in) != NULL) {
        if (first_line) {
            first_line = false;
            continue;
        }
        char *fields[5] = {0};
        char *cursor = line;
        for (size_t i = 0; i < 5; i++) {
            fields[i] = cursor;
            char *tab = strchr(cursor, i == 4 ? '\n' : '\t');
            if (tab != NULL) {
                *tab = '\0';
                cursor = tab + 1;
            }
            else {
                cursor = "";
            }
        }
        if (fields[0] == NULL || fields[0][0] == '\0')
            continue;

        if (!first_item)
            fputs(",\n", out);
        fputs("    {\"slug\":\"", out);
        json_escape(out, fields[0]);
        fputs("\",\"filename\":\"", out);
        json_escape(out, fields[1]);
        fputs("\",\"image\":\"", out);
        json_escape(out, fields[2]);
        fputs("\",\"installed_at\":\"", out);
        json_escape(out, fields[3]);
        fputs("\",\"remote_hash\":\"", out);
        json_escape(out, fields[4]);
        fputs("\"}", out);
        first_item = false;
    }
    fputs("\n  ]\n}\n", out);

    int close_rc = fclose(out);
    fclose(in);
    if (close_rc != 0 || rename(tmp_json, index_json) != 0) {
        unlink(tmp_json);
        return -1;
    }
    return 0;
}

static int write_installed_index(const char *meta_dir,
                                 const W4XInstalledMeta *meta)
{
    char index_tsv[W4X_PATH_MAX];
    char index_json[W4X_PATH_MAX];
    if (path_join(index_tsv, sizeof(index_tsv), meta_dir, "installed-index.tsv") != 0 ||
        path_join(index_json, sizeof(index_json), meta_dir, "installed-index.json") != 0)
        return -1;

    char tmp_tsv[W4X_PATH_MAX];
    if (make_tmp_path(tmp_tsv, sizeof(tmp_tsv), index_tsv, "tmp") != 0)
        return -1;

    FILE *out = fopen(tmp_tsv, "w");
    if (out == NULL)
        return -1;
    fputs("slug\tfilename\timage\tinstalled_at\tremote_hash\n", out);

    FILE *in = fopen(index_tsv, "r");
    if (in != NULL) {
        char line[4096];
        bool first = true;
        while (fgets(line, sizeof(line), in) != NULL) {
            if (first) {
                first = false;
                continue;
            }
            char slug[W4X_SLUG_MAX];
            size_t slug_len = strcspn(line, "\t\r\n");
            if (slug_len >= sizeof(slug))
                slug_len = sizeof(slug) - 1;
            memcpy(slug, line, slug_len);
            slug[slug_len] = '\0';
            if (strcmp(slug, meta->slug) != 0)
                fputs(line, out);
        }
        fclose(in);
    }

    fprintf(out, "%s\t%s\t%s\t%s\t%s\n", meta->slug, meta->installed_filename,
            meta->installed_image, meta->installed_at, meta->remote_hash);

    int close_rc = fclose(out);
    if (close_rc != 0 || rename(tmp_tsv, index_tsv) != 0) {
        unlink(tmp_tsv);
        return -1;
    }

    return write_installed_index_json(index_tsv, index_json);
}

static int compare_names(const void *left, const void *right)
{
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static void free_name_list(W4XNameList *list)
{
    if (list == NULL)
        return;
    for (size_t i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int append_name(W4XNameList *list, const char *name)
{
    char **next = realloc(list->items, (list->count + 1) * sizeof(*list->items));
    if (next == NULL)
        return -1;
    list->items = next;
    list->items[list->count] = strdup(name);
    if (list->items[list->count] == NULL)
        return -1;
    list->count++;
    return 0;
}

static int list_wasm_files(const char *rom_dir, W4XNameList *list)
{
    memset(list, 0, sizeof(*list));
    DIR *dir = opendir(rom_dir);
    if (dir == NULL)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        if (!has_suffix_ci(entry->d_name, ".wasm"))
            continue;
        char path[W4X_PATH_MAX];
        if (path_join(path, sizeof(path), rom_dir, entry->d_name) == 0 &&
            is_file(path) && append_name(list, entry->d_name) != 0) {
            closedir(dir);
            free_name_list(list);
            return -1;
        }
    }
    closedir(dir);

    qsort(list->items, list->count, sizeof(*list->items), compare_names);
    return 0;
}

static void xml_escape(FILE *fp, const char *value)
{
    for (const char *p = value != NULL ? value : ""; *p != '\0'; p++) {
        switch (*p) {
        case '&':
            fputs("&amp;", fp);
            break;
        case '<':
            fputs("&lt;", fp);
            break;
        case '>':
            fputs("&gt;", fp);
            break;
        case '"':
            fputs("&quot;", fp);
            break;
        case '\'':
            fputs("&apos;", fp);
            break;
        default:
            fputc(*p, fp);
            break;
        }
    }
}

static void write_xml_tag(FILE *fp, const char *name, const char *value)
{
    if (value == NULL || value[0] == '\0')
        return;
    fprintf(fp, "    <%s>", name);
    xml_escape(fp, value);
    fprintf(fp, "</%s>\n", name);
}

static void title_from_stem(const char *stem, char *dest, size_t dest_size)
{
    size_t pos = 0;
    bool start_word = true;
    for (const unsigned char *p = (const unsigned char *)stem;
         *p != '\0' && pos + 1 < dest_size; p++) {
        char c = (*p == '-' || *p == '_') ? ' ' : (char)*p;
        if (c == ' ') {
            if (pos > 0 && dest[pos - 1] != ' ')
                dest[pos++] = ' ';
            start_word = true;
            continue;
        }
        dest[pos++] = start_word ? (char)toupper((unsigned char)c) : c;
        start_word = false;
    }
    while (pos > 0 && dest[pos - 1] == ' ')
        pos--;
    dest[pos] = '\0';
}

static bool date_to_es(const char *date, char *dest, size_t dest_size)
{
    if (date == NULL || strlen(date) != 10 || date[4] != '-' || date[7] != '-')
        return false;
    for (size_t i = 0; i < 10; i++) {
        if ((i == 4 || i == 7) ? false : !isdigit((unsigned char)date[i]))
            return false;
    }
    int written = snprintf(dest, dest_size, "%.4s%.2s%.2sT000000",
                           date, date + 5, date + 8);
    return written >= 0 && (size_t)written < dest_size;
}

static bool meta_matches_filename(const W4XInstalledMeta *meta,
                                  const char *filename, const char *stem)
{
    if (meta->installed_filename[0] != '\0' &&
        strcmp(meta->installed_filename, filename) == 0)
        return true;
    if (meta->slug[0] != '\0' && strcmp(meta->slug, stem) == 0)
        return true;
    if (meta->title[0] != '\0') {
        char sanitized[W4X_TITLE_MAX];
        char candidate[W4X_TITLE_MAX];
        sanitize_stem(meta->title, sanitized, sizeof(sanitized));
        int written = snprintf(candidate, sizeof(candidate), "%s.wasm", sanitized);
        if (written >= 0 && (size_t)written < sizeof(candidate) &&
            strcmp(candidate, filename) == 0)
            return true;
    }
    return false;
}

static bool find_meta_for_rom(const char *meta_dir, const char *filename,
                              const char *stem, W4XInstalledMeta *meta)
{
    memset(meta, 0, sizeof(*meta));
    DIR *dir = opendir(meta_dir);
    if (dir == NULL)
        return false;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_suffix_ci(entry->d_name, ".env"))
            continue;
        char path[W4X_PATH_MAX];
        if (path_join(path, sizeof(path), meta_dir, entry->d_name) != 0)
            continue;
        W4XInstalledMeta candidate;
        if (load_meta_file(path, &candidate) == 0 &&
            meta_matches_filename(&candidate, filename, stem)) {
            *meta = candidate;
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static bool image_rel_exists(const char *rom_dir, const char *rel_path)
{
    char path[W4X_PATH_MAX];
    return rel_path != NULL && rel_path[0] != '\0' &&
           path_join(path, sizeof(path), rom_dir, rel_path) == 0 && is_file(path);
}

static void build_description(const W4XInstalledMeta *meta, char *dest,
                              size_t dest_size)
{
    copy_string(dest, dest_size, meta->description);
    if (meta->page_url[0] != '\0') {
        strncat(dest, dest[0] != '\0' ? "\n\nSource: " : "Source: ",
                dest_size - strlen(dest) - 1);
        strncat(dest, meta->page_url, dest_size - strlen(dest) - 1);
    }
    if (meta->license[0] != '\0') {
        strncat(dest, dest[0] != '\0' ? "\nLicense: " : "License: ",
                dest_size - strlen(dest) - 1);
        strncat(dest, meta->license, dest_size - strlen(dest) - 1);
    }
}

static int write_miyoogamelist(const char *rom_dir, const char *img_dir,
                               const char *meta_dir)
{
    W4XNameList roms;
    if (list_wasm_files(rom_dir, &roms) != 0)
        return -1;

    char gamelist[W4X_PATH_MAX];
    if (path_join(gamelist, sizeof(gamelist), rom_dir, "miyoogamelist.xml") != 0) {
        free_name_list(&roms);
        return -1;
    }

    if (is_file(gamelist)) {
        char stamp[64];
        current_timestamp(stamp, sizeof(stamp));
        char backup[W4X_PATH_MAX];
        int written = snprintf(backup, sizeof(backup), "%s.%s.bak", gamelist, stamp);
        if (written > 0 && (size_t)written < sizeof(backup))
            copy_file(gamelist, backup);
    }

    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), gamelist, "tmp") != 0) {
        free_name_list(&roms);
        return -1;
    }

    FILE *fp = fopen(tmp, "w");
    if (fp == NULL) {
        free_name_list(&roms);
        return -1;
    }

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", fp);
    fputs("<gameList>\n", fp);
    for (size_t i = 0; i < roms.count; i++) {
        const char *filename = roms.items[i];
        char stem[W4X_TITLE_MAX];
        copy_string(stem, sizeof(stem), filename);
        char *dot = strrchr(stem, '.');
        if (dot != NULL)
            *dot = '\0';

        W4XInstalledMeta meta;
        bool has_meta = find_meta_for_rom(meta_dir, filename, stem, &meta);
        char name[W4X_TITLE_MAX];
        if (has_meta && meta.title[0] != '\0')
            copy_string(name, sizeof(name), meta.title);
        else
            title_from_stem(stem, name, sizeof(name));

        char image[W4X_PATH_MAX] = "";
        if (has_meta && image_rel_exists(rom_dir, meta.installed_image)) {
            snprintf(image, sizeof(image), "./%s", meta.installed_image);
        }
        else {
            char stem_image[W4X_PATH_MAX];
            int written = snprintf(stem_image, sizeof(stem_image), "%s/%s.png",
                                   img_dir, stem);
            if (written > 0 && (size_t)written < sizeof(stem_image) &&
                is_file(stem_image))
                snprintf(image, sizeof(image), "./Imgs/%s.png", stem);
        }

        char desc[W4X_METADATA_MAX + W4X_URL_MAX + W4X_METADATA_MAX + 64];
        desc[0] = '\0';
        if (has_meta)
            build_description(&meta, desc, sizeof(desc));

        char releasedate[32] = "";
        if (has_meta)
            date_to_es(meta.date, releasedate, sizeof(releasedate));

        char path[W4X_TITLE_MAX + 8];
        snprintf(path, sizeof(path), "./%s", filename);
        fputs("  <game>\n", fp);
        write_xml_tag(fp, "path", path);
        write_xml_tag(fp, "name", name);
        write_xml_tag(fp, "image", image);
        write_xml_tag(fp, "desc", desc);
        write_xml_tag(fp, "developer", has_meta ? meta.author : "");
        write_xml_tag(fp, "releasedate", releasedate);
        fputs("  </game>\n", fp);
    }
    fputs("</gameList>\n", fp);

    int close_rc = fclose(fp);
    free_name_list(&roms);
    if (close_rc != 0 || rename(tmp, gamelist) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static void remove_onion_rom_cache(const W4XPathSet *paths)
{
    const char *cache_names[] = {"WASM4_cache2.db", "WASM4_cache6.db"};
    for (size_t i = 0; i < sizeof(cache_names) / sizeof(cache_names[0]); i++) {
        char cache_path[W4X_PATH_MAX];
        if (path_join(cache_path, sizeof(cache_path), paths->rom_dir,
                      cache_names[i]) == 0)
            unlink(cache_path);
    }
}

static void reset_onion_list_window(const W4XPathSet *paths)
{
    char reset_script[W4X_PATH_MAX];
    char onion_romroot[W4X_PATH_MAX];

    if (path_join(reset_script, sizeof(reset_script), paths->sd_root,
                  ".tmp_update/script/reset_list.sh") != 0 ||
        path_join(onion_romroot, sizeof(onion_romroot), paths->sd_root,
                  "Emu/WASM4/../../Roms/WASM4") != 0)
        return;

    if (!is_file(reset_script))
        return;

    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", reset_script, onion_romroot, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        w4x_log(paths, "failed to fork Onion game-list reset helper");
        return;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        w4x_log(paths, "Onion game-list reset helper did not complete cleanly");
}

static void invalidate_onion_game_list(const W4XPathSet *paths)
{
    remove_onion_rom_cache(paths);
    reset_onion_list_window(paths);
    sync();
}

static int ensure_install_dirs(const W4XPathSet *paths, char *img_dir,
                               size_t img_dir_size, char *meta_dir,
                               size_t meta_dir_size, char *cache_meta_dir,
                               size_t cache_meta_dir_size)
{
    if (path_join(img_dir, img_dir_size, paths->rom_dir, "Imgs") != 0 ||
        path_join(meta_dir, meta_dir_size, paths->rom_dir, ".wasm4") != 0 ||
        path_join(cache_meta_dir, cache_meta_dir_size, paths->cache_dir, "meta") != 0)
        return -1;
    return mkdir_p(paths->rom_dir) == 0 && mkdir_p(img_dir) == 0 &&
           mkdir_p(meta_dir) == 0 && mkdir_p(cache_meta_dir) == 0 ? 0 : -1;
}

static const char *env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static int parent_dir(char *dest, size_t dest_size, const char *path)
{
    if (copy_string(dest, dest_size, path) != 0)
        return -1;
    char *slash = strrchr(dest, '/');
    if (slash == NULL)
        return -1;
    if (slash == dest) {
        dest[1] = '\0';
        return 0;
    }
    *slash = '\0';
    return 0;
}

static int ensure_parent_dir(const char *path)
{
    char dir[W4X_PATH_MAX];
    return parent_dir(dir, sizeof(dir), path) == 0 ? mkdir_p(dir) : -1;
}

static int json_escape_to_buffer(char *dest, size_t dest_size, const char *value)
{
    if (dest_size == 0)
        return -1;

    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)(value != NULL ? value : "");
         *p != '\0'; p++) {
        const char *escaped = NULL;
        char unicode[8];
        switch (*p) {
        case '\\':
            escaped = "\\\\";
            break;
        case '"':
            escaped = "\\\"";
            break;
        case '\t':
            escaped = "\\t";
            break;
        case '\r':
            escaped = "\\r";
            break;
        case '\n':
            escaped = "\\n";
            break;
        default:
            if (*p < 32) {
                snprintf(unicode, sizeof(unicode), "\\u%04x", *p);
                escaped = unicode;
            }
            break;
        }

        if (escaped != NULL) {
            size_t len = strlen(escaped);
            if (pos + len >= dest_size)
                return -1;
            memcpy(dest + pos, escaped, len);
            pos += len;
        }
        else {
            if (pos + 1 >= dest_size)
                return -1;
            dest[pos++] = (char)*p;
        }
    }

    dest[pos] = '\0';
    return 0;
}

static void cmd_quote(FILE *fp, const char *value)
{
    fputc('"', fp);
    for (const char *p = value != NULL ? value : ""; *p != '\0'; p++) {
        if (*p == '\\' || *p == '"' || *p == '$' || *p == '`')
            fputc('\\', fp);
        fputc(*p, fp);
    }
    fputc('"', fp);
}

static int touch_file(const char *path)
{
    FILE *fp = fopen(path, "a");
    if (fp == NULL)
        return -1;
    return fclose(fp) == 0 ? 0 : -1;
}

static int write_launch_command(const char *cmd_path, const char *launcher,
                                const char *cart_path)
{
    if (ensure_parent_dir(cmd_path) != 0)
        return -1;

    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), cmd_path, "tmp") != 0)
        return -1;

    FILE *fp = fopen(tmp, "w");
    if (fp == NULL)
        return -1;

    fputs("LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ", fp);
    cmd_quote(fp, launcher);
    fputc(' ', fp);
    cmd_quote(fp, cart_path);
    fputc('\n', fp);

    int close_rc = fclose(fp);
    if (close_rc != 0 || rename(tmp, cmd_path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int default_return_marker_path(const W4XPathSet *paths, char *dest,
                                      size_t dest_size)
{
    char runtime_dir[W4X_PATH_MAX];
    if (path_join(runtime_dir, sizeof(runtime_dir), paths->cache_dir,
                  "runtime") != 0 ||
        path_join(dest, dest_size, runtime_dir, "return_to_explorer") != 0)
        return -1;
    return 0;
}

static int write_return_marker(const W4XPathSet *paths, const char *cart_path,
                               char *marker_path, size_t marker_path_size)
{
    char default_marker[W4X_PATH_MAX];
    if (default_return_marker_path(paths, default_marker,
                                   sizeof(default_marker)) != 0)
        return -1;

    const char *selected_marker =
        env_or_default("WASM4_RETURN_MARKER", default_marker);
    if (copy_string(marker_path, marker_path_size, selected_marker) != 0 ||
        ensure_parent_dir(marker_path) != 0)
        return -1;

    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), marker_path, "tmp") != 0)
        return -1;

    FILE *fp = fopen(tmp, "w");
    if (fp == NULL)
        return -1;

    fprintf(fp, "%s\n", cart_path);

    int close_rc = fclose(fp);
    if (close_rc != 0 || rename(tmp, marker_path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int current_recent_path(const W4XPathSet *paths, const char *sysdir,
                               char *dest, size_t dest_size)
{
    char default_recent[W4X_PATH_MAX];
    char default_hidden[W4X_PATH_MAX];
    char show_recents[W4X_PATH_MAX];
    if (path_join(default_recent, sizeof(default_recent), paths->sd_root,
                  "Roms/recentlist.json") != 0 ||
        path_join(default_hidden, sizeof(default_hidden), paths->sd_root,
                  "Roms/recentlist-hidden.json") != 0 ||
        path_join(show_recents, sizeof(show_recents), sysdir,
                  "config/.showRecents") != 0)
        return -1;

    const char *recent = env_or_default("WASM4_RECENTLIST_PATH", default_recent);
    const char *hidden = env_or_default("WASM4_RECENTLIST_HIDDEN_PATH",
                                        default_hidden);
    return copy_string(dest, dest_size, is_file(show_recents) ? recent : hidden);
}

static int write_recent_entry(const W4XPathSet *paths,
                              const W4XCatalogEntry *entry,
                              const W4XInstallResult *install,
                              const char *launcher, const char *sysdir,
                              char *recent_path, size_t recent_path_size)
{
    if (current_recent_path(paths, sysdir, recent_path, recent_path_size) != 0 ||
        ensure_parent_dir(recent_path) != 0)
        return -1;

    char label_json[W4X_TITLE_MAX * 2];
    char rom_json[W4X_PATH_MAX * 2];
    char image_json[W4X_PATH_MAX * 2];
    char launcher_json[W4X_PATH_MAX * 2];
    if (json_escape_to_buffer(label_json, sizeof(label_json), entry->title) != 0 ||
        json_escape_to_buffer(rom_json, sizeof(rom_json), install->cart_path) != 0 ||
        json_escape_to_buffer(image_json, sizeof(image_json), install->image_path) != 0 ||
        json_escape_to_buffer(launcher_json, sizeof(launcher_json), launcher) != 0)
        return -1;

    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), recent_path, "tmp") != 0)
        return -1;

    FILE *out = fopen(tmp, "w");
    if (out == NULL)
        return -1;

    if (install->image_path[0] != '\0' && is_file(install->image_path)) {
        fprintf(out,
                "{\"label\":\"%s\",\"rompath\":\"%s\",\"imgpath\":\"%s\",\"launch\":\"%s\",\"type\":5}\n",
                label_json, rom_json, image_json, launcher_json);
    }
    else {
        fprintf(out,
                "{\"label\":\"%s\",\"rompath\":\"%s\",\"launch\":\"%s\",\"type\":5}\n",
                label_json, rom_json, launcher_json);
    }

    FILE *in = fopen(recent_path, "r");
    if (in != NULL) {
        char needle[W4X_PATH_MAX * 2 + 16];
        int written = snprintf(needle, sizeof(needle), "\"rompath\":\"%s\"", rom_json);
        if (written < 0 || (size_t)written >= sizeof(needle)) {
            fclose(in);
            fclose(out);
            unlink(tmp);
            return -1;
        }

        char line[8192];
        while (fgets(line, sizeof(line), in) != NULL) {
            if (strstr(line, needle) == NULL)
                fputs(line, out);
        }
        fclose(in);
    }

    int close_rc = fclose(out);
    if (close_rc != 0 || rename(tmp, recent_path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int w4x_prepare_onion_launch(const W4XPathSet *paths,
                             const W4XCatalogEntry *entry,
                             const W4XInstallResult *install,
                             W4XLaunchResult *result)
{
    if (paths == NULL || entry == NULL || install == NULL || result == NULL)
        return -1;
    if (!is_file(install->cart_path))
        return -1;

    memset(result, 0, sizeof(*result));

    const char *launcher = env_or_default("WASM4_LAUNCHER", paths->launcher_path);
    if (access(launcher, X_OK) != 0)
        return -1;

    char default_sysdir[W4X_PATH_MAX];
    if (path_join(default_sysdir, sizeof(default_sysdir), paths->sd_root,
                  ".tmp_update") != 0)
        return -1;
    const char *sysdir = env_or_default("WASM4_TMP_UPDATE_DIR", default_sysdir);

    char cmd_path[W4X_PATH_MAX];
    if (path_join(cmd_path, sizeof(cmd_path), sysdir, "cmd_to_run.sh") != 0)
        return -1;

    char recent_path[W4X_PATH_MAX];
    char return_marker_path[W4X_PATH_MAX];
    if (write_recent_entry(paths, entry, install, launcher, sysdir,
                           recent_path, sizeof(recent_path)) != 0 ||
        write_return_marker(paths, install->cart_path, return_marker_path,
                            sizeof(return_marker_path)) != 0 ||
        write_launch_command(cmd_path, launcher, install->cart_path) != 0)
        return -1;

    const char *runtime_tmp = env_or_default("WASM4_RUNTIME_TMP_DIR", "/tmp");
    char quick_switch[W4X_PATH_MAX];
    if (path_join(quick_switch, sizeof(quick_switch), runtime_tmp,
                  "quick_switch") != 0 ||
        touch_file(quick_switch) != 0)
        return -1;

    copy_string(result->cmd_path, sizeof(result->cmd_path), cmd_path);
    copy_string(result->recent_path, sizeof(result->recent_path), recent_path);
    copy_string(result->quick_switch_path, sizeof(result->quick_switch_path),
                quick_switch);
    copy_string(result->return_marker_path, sizeof(result->return_marker_path),
                return_marker_path);
    sync();
    return 0;
}

int w4x_install_catalog_entry(const W4XPathSet *paths,
                              const W4XCatalogEntry *entry,
                              W4XInstallResult *result)
{
    if (paths == NULL || entry == NULL || result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));

    char img_dir[W4X_PATH_MAX];
    char meta_dir[W4X_PATH_MAX];
    char cache_meta_dir[W4X_PATH_MAX];
    if (ensure_install_dirs(paths, img_dir, sizeof(img_dir), meta_dir,
                            sizeof(meta_dir), cache_meta_dir,
                            sizeof(cache_meta_dir)) != 0)
        return -1;

    W4XInstalledMeta meta;
    meta_from_entry(entry, &meta);

    char cache_meta_path[W4X_PATH_MAX];
    if (path_join(cache_meta_path, sizeof(cache_meta_path), cache_meta_dir,
                  entry->slug) != 0)
        return -1;
    strncat(cache_meta_path, ".env", sizeof(cache_meta_path) - strlen(cache_meta_path) - 1);
    if (write_meta_file(cache_meta_path, &meta, false) != 0)
        return -1;

    char installed_meta_path[W4X_PATH_MAX];
    if (path_join(installed_meta_path, sizeof(installed_meta_path), meta_dir,
                  entry->slug) != 0)
        return -1;
    strncat(installed_meta_path, ".env",
            sizeof(installed_meta_path) - strlen(installed_meta_path) - 1);

    W4XInstalledMeta installed_meta;
    if (load_meta_file(installed_meta_path, &installed_meta) == 0 &&
        installed_meta.installed_filename[0] != '\0') {
        char installed_path[W4X_PATH_MAX];
        if (path_join(installed_path, sizeof(installed_path), paths->rom_dir,
                      installed_meta.installed_filename) == 0 &&
            is_file(installed_path))
            copy_string(meta.installed_filename, sizeof(meta.installed_filename),
                        installed_meta.installed_filename);
    }

    if (meta.installed_filename[0] == '\0' &&
        build_unique_filename(paths->rom_dir, meta.title, meta.slug,
                              meta.installed_filename,
                              sizeof(meta.installed_filename)) != 0)
        return -1;

    char cart_path[W4X_PATH_MAX];
    if (path_join(cart_path, sizeof(cart_path), paths->rom_dir,
                  meta.installed_filename) != 0)
        return -1;

    if (is_file(cart_path)) {
        if (verify_sha256_if_available(cart_path, meta.remote_hash) != 0)
            return -1;
        result->reused_cart = true;
    }
    else if (copy_or_download_atomic(meta.cart_url, cart_path,
                                     meta.remote_hash) != 0) {
        return -1;
    }

    char cart_stem[W4X_TITLE_MAX];
    copy_string(cart_stem, sizeof(cart_stem), meta.installed_filename);
    char *dot = strrchr(cart_stem, '.');
    if (dot != NULL)
        *dot = '\0';

    if (install_image(paths, entry, img_dir, cart_stem, &meta, result) != 0)
        return -1;

    current_timestamp(meta.installed_at, sizeof(meta.installed_at));
    if (write_meta_file(installed_meta_path, &meta, true) != 0 ||
        write_installed_index(meta_dir, &meta) != 0 ||
        write_miyoogamelist(paths->rom_dir, img_dir, meta_dir) != 0)
        return -1;

    invalidate_onion_game_list(paths);

    copy_string(result->cart_path, sizeof(result->cart_path), cart_path);
    copy_string(result->filename, sizeof(result->filename), meta.installed_filename);
    return 0;
}
