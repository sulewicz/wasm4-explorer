#include "explorer.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define W4X_DEFAULT_PLAY_URL "https://wasm4.org/play/"
#define W4X_DEFAULT_TTL_SECONDS 86400L
#define W4X_FIELD_MAX 2048

typedef struct W4XBuffer {
    char *data;
    size_t len;
} W4XBuffer;

static bool is_file(const char *path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static long parse_long_default(const char *value, long fallback)
{
    if (value == NULL || value[0] == '\0')
        return fallback;

    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    while (end != NULL && isspace((unsigned char)*end))
        end++;
    if (errno != 0 || end == value || *end != '\0')
        return fallback;
    return parsed;
}

static int make_tmp_path(char *dest, size_t dest_size, const char *path,
                         const char *suffix)
{
    int written = snprintf(dest, dest_size, "%s.%ld.%s", path, (long)getpid(), suffix);
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

static int read_file(const char *path, W4XBuffer *buffer)
{
    memset(buffer, 0, sizeof(*buffer));

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return -1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    buffer->data = calloc((size_t)size + 1, 1);
    if (buffer->data == NULL) {
        fclose(fp);
        return -1;
    }

    buffer->len = fread(buffer->data, 1, (size_t)size, fp);
    int rc = ferror(fp) ? -1 : 0;
    fclose(fp);

    if (rc != 0) {
        free(buffer->data);
        memset(buffer, 0, sizeof(*buffer));
    }
    return rc;
}

static void free_buffer(W4XBuffer *buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static int catalog_log(const W4XPathSet *paths, const char *fmt, ...)
{
    char log_path[W4X_PATH_MAX];
    int written = snprintf(log_path, sizeof(log_path), "%s/catalog.log", paths->logs_dir);
    if (written < 0 || (size_t)written >= sizeof(log_path))
        return -1;

    FILE *fp = fopen(log_path, "a");
    if (fp == NULL)
        return -1;

    time_t now = time(NULL);
    struct tm tm_buf;
    char stamp[64] = "unknown-time";
    if (now != (time_t)-1 && localtime_r(&now, &tm_buf) != NULL)
        strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S%z", &tm_buf);

    fprintf(fp, "%s ", stamp);
    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fputc('\n', fp);

    fclose(fp);
    return 0;
}

static uint64_t fnv1a_update(uint64_t hash, const void *data, size_t len)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t fnv1a_string(uint64_t hash, const char *value)
{
    return fnv1a_update(hash, value, strlen(value));
}

static uint64_t file_hash(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    uint64_t hash = UINT64_C(1469598103934665603);
    char buf[8192];
    while (!feof(fp)) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0)
            hash = fnv1a_update(hash, buf, n);
        if (ferror(fp)) {
            hash = 0;
            break;
        }
    }

    fclose(fp);
    return hash;
}

static int write_file_hash(const char *path, uint64_t hash)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%016llx\n", (unsigned long long)hash);
    return fclose(fp) == 0 ? 0 : -1;
}

static int write_fetched_at(const char *path)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld\n", (long)time(NULL));
    return fclose(fp) == 0 ? 0 : -1;
}

static long read_fetched_at(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    char buf[64];
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return parse_long_default(buf, 0);
}

static bool cache_is_fresh(const W4XPathSet *paths, const W4XCatalogOptions *options)
{
    if (!is_file(paths->cache_catalog_tsv_path) || options->force)
        return false;
    if (options->ttl_seconds <= 0)
        return false;

    time_t now = time(NULL);
    long fetched_at = read_fetched_at(paths->catalog_fetched_at_path);
    if (now == (time_t)-1 || fetched_at <= 0)
        return false;
    return ((long)now - fetched_at) < options->ttl_seconds;
}

static size_t count_catalog_rows(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    char line[8192];
    size_t rows = 0;
    bool first = true;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (first) {
            first = false;
            continue;
        }
        if (line[0] != '\0' && line[0] != '\n' && line[0] != '\r')
            rows++;
    }

    fclose(fp);
    return rows;
}

static const char *bounded_strstr(const char *start, const char *end,
                                  const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0)
        return start;

    for (const char *p = start; p < end; p++) {
        if ((size_t)(end - p) < needle_len)
            return NULL;
        if (memcmp(p, needle, needle_len) == 0)
            return p;
    }
    return NULL;
}

static char *dup_range(const char *start, const char *end)
{
    if (end < start)
        return NULL;

    size_t len = (size_t)(end - start);
    char *out = malloc(len + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *extract_between(const char *start, const char *end,
                             const char *prefix, const char *suffix)
{
    const char *value = bounded_strstr(start, end, prefix);
    if (value == NULL)
        return NULL;
    value += strlen(prefix);

    const char *value_end = bounded_strstr(value, end, suffix);
    if (value_end == NULL)
        return NULL;
    return dup_range(value, value_end);
}

static size_t append_utf8(char *out, size_t out_pos, unsigned long codepoint)
{
    if (codepoint <= 0x7f) {
        out[out_pos++] = (char)codepoint;
    }
    else if (codepoint <= 0x7ff) {
        out[out_pos++] = (char)(0xc0 | (codepoint >> 6));
        out[out_pos++] = (char)(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint <= 0xffff) {
        out[out_pos++] = (char)(0xe0 | (codepoint >> 12));
        out[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        out[out_pos++] = (char)(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint <= 0x10ffff) {
        out[out_pos++] = (char)(0xf0 | (codepoint >> 18));
        out[out_pos++] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
        out[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        out[out_pos++] = (char)(0x80 | (codepoint & 0x3f));
    }
    return out_pos;
}

static bool parse_numeric_entity(const char *p, const char *semi,
                                 unsigned long *codepoint)
{
    int base = 10;
    const char *digits = p + 2;
    if (digits >= semi)
        return false;

    if (*digits == 'x' || *digits == 'X') {
        base = 16;
        digits++;
    }
    if (digits >= semi)
        return false;

    unsigned long value = 0;
    for (const char *d = digits; d < semi; d++) {
        int digit = -1;
        if (*d >= '0' && *d <= '9')
            digit = *d - '0';
        else if (base == 16 && *d >= 'a' && *d <= 'f')
            digit = 10 + (*d - 'a');
        else if (base == 16 && *d >= 'A' && *d <= 'F')
            digit = 10 + (*d - 'A');
        else
            return false;
        if (digit >= base)
            return false;
        value = (value * (unsigned long)base) + (unsigned long)digit;
    }

    *codepoint = value;
    return value > 0 && value <= 0x10ffff;
}

static char *html_unescape(const char *value)
{
    size_t len = strlen(value);
    char *out = malloc(len + 1);
    if (out == NULL)
        return NULL;

    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        if (value[i] != '&') {
            out[pos++] = value[i];
            continue;
        }

        if (strncmp(&value[i], "&amp;", 5) == 0) {
            out[pos++] = '&';
            i += 4;
        }
        else if (strncmp(&value[i], "&quot;", 6) == 0) {
            out[pos++] = '"';
            i += 5;
        }
        else if (strncmp(&value[i], "&apos;", 6) == 0) {
            out[pos++] = '\'';
            i += 5;
        }
        else if (strncmp(&value[i], "&#x27;", 6) == 0) {
            out[pos++] = '\'';
            i += 5;
        }
        else if (strncmp(&value[i], "&#39;", 5) == 0) {
            out[pos++] = '\'';
            i += 4;
        }
        else if (strncmp(&value[i], "&lt;", 4) == 0) {
            out[pos++] = '<';
            i += 3;
        }
        else if (strncmp(&value[i], "&gt;", 4) == 0) {
            out[pos++] = '>';
            i += 3;
        }
        else if (i + 3 < len && value[i + 1] == '#') {
            const char *semi = strchr(&value[i], ';');
            unsigned long codepoint = 0;
            if (semi != NULL && (size_t)(semi - &value[i]) <= 12 &&
                parse_numeric_entity(&value[i], semi, &codepoint)) {
                pos = append_utf8(out, pos, codepoint);
                i = (size_t)(semi - value);
            }
            else {
                out[pos++] = value[i];
            }
        }
        else {
            out[pos++] = value[i];
        }
    }

    out[pos] = '\0';
    return out;
}

static void clean_tsv_field(char *value)
{
    for (char *p = value; *p != '\0'; p++) {
        if (*p == '\t' || *p == '\r' || *p == '\n')
            *p = ' ';
    }
}

static bool slug_is_valid(const char *slug)
{
    if (slug == NULL || slug[0] == '\0')
        return false;
    for (const char *p = slug; *p != '\0'; p++) {
        if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && *p != '-')
            return false;
    }
    return true;
}

static int clean_copy(char *dest, size_t dest_size, const char *raw,
                      const char *fallback)
{
    const char *source = raw != NULL && raw[0] != '\0' ? raw : fallback;
    char *decoded = html_unescape(source != NULL ? source : "");
    if (decoded == NULL)
        return -1;

    clean_tsv_field(decoded);
    int written = snprintf(dest, dest_size, "%s", decoded);
    free(decoded);
    return written < 0 || (size_t)written >= dest_size ? -1 : 0;
}

static uint64_t row_fingerprint(const char *slug, const char *title,
                                const char *author, const char *cart_url,
                                const char *image_url)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = fnv1a_string(hash, slug);
    hash = fnv1a_string(hash, "\t");
    hash = fnv1a_string(hash, title);
    hash = fnv1a_string(hash, "\t");
    hash = fnv1a_string(hash, author);
    hash = fnv1a_string(hash, "\t");
    hash = fnv1a_string(hash, cart_url);
    hash = fnv1a_string(hash, "\t");
    hash = fnv1a_string(hash, image_url);
    return hash;
}

static int write_play_catalog(const W4XPathSet *paths, const char *html_path,
                              const char *out_path, size_t *row_count)
{
    W4XBuffer html;
    if (read_file(html_path, &html) != 0)
        return -1;

    char tmp_out[W4X_PATH_MAX];
    if (make_tmp_path(tmp_out, sizeof(tmp_out), out_path, "play.tmp") != 0) {
        free_buffer(&html);
        return -1;
    }

    FILE *out = fopen(tmp_out, "w");
    if (out == NULL) {
        free_buffer(&html);
        return -1;
    }

    fprintf(out, "slug\ttitle\tauthor\tauthors_json\tdate\tlicense\tpage_url\tcart_url\timage_url\tmanual_url\twasm_sha256\timage_sha256\tremote_updated_at\tdescription\trow_fingerprint\n");

    const char *html_start = html.data;
    const char *html_end = html.data + html.len;
    const char *cursor = html_start;
    size_t count = 0;

    while ((cursor = bounded_strstr(cursor, html_end, "href=\"/play/")) != NULL) {
        const char *slug_start = cursor + strlen("href=\"/play/");
        const char *slug_end = bounded_strstr(slug_start, html_end, "\"");
        if (slug_end == NULL)
            break;

        const char *next = bounded_strstr(slug_end + 1, html_end, "href=\"/play/");
        const char *card_end = next != NULL ? next : html_end;
        char *slug = dup_range(slug_start, slug_end);
        if (slug == NULL) {
            fclose(out);
            free_buffer(&html);
            unlink(tmp_out);
            return -1;
        }

        if (!slug_is_valid(slug)) {
            catalog_log(paths, "skipping malformed Play page slug: %s", slug);
            free(slug);
            cursor = card_end;
            continue;
        }

        if (bounded_strstr(cursor, card_end, "class=\"screenshot\"") == NULL) {
            catalog_log(paths, "skipping Play page entry without screenshot: %s", slug);
            free(slug);
            cursor = card_end;
            continue;
        }

        char *title_raw = extract_between(cursor, card_end, "alt=\"", "\"");
        char *author_raw = extract_between(cursor, card_end,
                                           "<small class=\"avatar__subtitle\">",
                                           "</small>");
        if (title_raw == NULL || title_raw[0] == '\0')
            catalog_log(paths, "Play page entry missing title; using slug: %s", slug);
        if (author_raw == NULL || author_raw[0] == '\0')
            catalog_log(paths, "Play page entry missing author: %s", slug);

        char title[W4X_FIELD_MAX];
        char author[W4X_FIELD_MAX];
        if (clean_copy(title, sizeof(title), title_raw, slug) != 0 ||
            clean_copy(author, sizeof(author), author_raw, "") != 0) {
            free(title_raw);
            free(author_raw);
            free(slug);
            fclose(out);
            free_buffer(&html);
            unlink(tmp_out);
            return -1;
        }

        char page_url[W4X_FIELD_MAX];
        char cart_url[W4X_FIELD_MAX];
        char image_url[W4X_FIELD_MAX];
        char manual_url[W4X_FIELD_MAX];
        snprintf(page_url, sizeof(page_url), "https://wasm4.org/play/%s", slug);
        snprintf(cart_url, sizeof(cart_url), "https://wasm4.org/carts/%s.wasm", slug);
        snprintf(image_url, sizeof(image_url), "https://wasm4.org/carts/%s.png", slug);
        snprintf(manual_url, sizeof(manual_url), "https://wasm4.org/carts/%s.md", slug);
        uint64_t fingerprint = row_fingerprint(slug, title, author, cart_url, image_url);

        fprintf(out, "%s\t%s\t%s\t[]\tunknown\tCC BY-NC-SA\t%s\t%s\t%s\t%s\t-\t-\tunknown\t\t%016llx\n",
                slug, title, author, page_url, cart_url, image_url, manual_url,
                (unsigned long long)fingerprint);

        count++;
        free(title_raw);
        free(author_raw);
        free(slug);
        cursor = card_end;
    }

    int close_rc = fclose(out);
    free_buffer(&html);

    if (close_rc != 0 || count == 0) {
        unlink(tmp_out);
        if (count == 0)
            catalog_log(paths, "Play page produced no catalog entries");
        return -1;
    }

    if (rename(tmp_out, out_path) != 0) {
        unlink(tmp_out);
        return -1;
    }

    *row_count = count;
    catalog_log(paths, "catalog parsed from Play page with %zu entries", count);
    return 0;
}

static int fetch_url_to_file(const char *url, const char *path)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execlp("wget", "wget", "-q", "-O", path, url, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int update_cache_from_tsv(const W4XPathSet *paths, const char *source,
                                 W4XCatalogResult *result,
                                 W4XCatalogStatus updated_status)
{
    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), paths->cache_catalog_tsv_path, "tmp") != 0)
        return -1;

    if (copy_file(source, tmp) != 0) {
        unlink(tmp);
        return -1;
    }

    size_t new_rows = count_catalog_rows(tmp);
    if (new_rows == 0) {
        unlink(tmp);
        return -1;
    }

    uint64_t new_hash = file_hash(tmp);
    uint64_t old_hash = file_hash(paths->cache_catalog_tsv_path);
    if (old_hash != 0 && new_hash == old_hash) {
        unlink(tmp);
        result->status = W4X_CATALOG_STATUS_CURRENT;
    }
    else {
        if (rename(tmp, paths->cache_catalog_tsv_path) != 0) {
            unlink(tmp);
            return -1;
        }
        result->status = updated_status;
    }

    write_fetched_at(paths->catalog_fetched_at_path);
    write_file_hash(paths->catalog_hash_path, file_hash(paths->cache_catalog_tsv_path));
    result->row_count = new_rows;
    result->used_cache = true;
    return 0;
}

static int refresh_from_catalog_url(const W4XPathSet *paths,
                                    const W4XCatalogOptions *options,
                                    W4XCatalogResult *result)
{
    if (options->catalog_url == NULL || options->catalog_url[0] == '\0')
        return -1;

    char tmp[W4X_PATH_MAX];
    if (make_tmp_path(tmp, sizeof(tmp), paths->cache_catalog_tsv_path, "url.tmp") != 0)
        return -1;

    if (fetch_url_to_file(options->catalog_url, tmp) != 0) {
        unlink(tmp);
        catalog_log(paths, "catalog URL fetch failed: %s", options->catalog_url);
        return -1;
    }

    int rc = update_cache_from_tsv(paths, tmp, result, W4X_CATALOG_STATUS_UPDATED);
    unlink(tmp);
    if (rc == 0)
        catalog_log(paths, "catalog refreshed from URL: %s", options->catalog_url);
    return rc;
}

static int refresh_from_play(const W4XPathSet *paths,
                             const W4XCatalogOptions *options,
                             W4XCatalogResult *result)
{
    char html_tmp[W4X_PATH_MAX];
    char tsv_tmp[W4X_PATH_MAX];
    if (make_tmp_path(html_tmp, sizeof(html_tmp), paths->cache_catalog_tsv_path,
                      "play.html.tmp") != 0 ||
        make_tmp_path(tsv_tmp, sizeof(tsv_tmp), paths->cache_catalog_tsv_path,
                      "play.tsv.tmp") != 0)
        return -1;

    if (options->play_source != NULL && options->play_source[0] != '\0') {
        if (copy_file(options->play_source, html_tmp) != 0) {
            unlink(html_tmp);
            catalog_log(paths, "Play page source unavailable: %s", options->play_source);
            return -1;
        }
    }
    else if (options->play_url != NULL && options->play_url[0] != '\0') {
        if (fetch_url_to_file(options->play_url, html_tmp) != 0) {
            unlink(html_tmp);
            catalog_log(paths, "Play page fetch failed: %s", options->play_url);
            return -1;
        }
    }
    else {
        catalog_log(paths, "no Play page source available");
        return -1;
    }

    size_t parsed_rows = 0;
    if (write_play_catalog(paths, html_tmp, tsv_tmp, &parsed_rows) != 0) {
        unlink(html_tmp);
        unlink(tsv_tmp);
        return -1;
    }
    unlink(html_tmp);

    int rc = update_cache_from_tsv(paths, tsv_tmp, result, W4X_CATALOG_STATUS_UPDATED);
    unlink(tsv_tmp);
    if (rc == 0) {
        result->row_count = parsed_rows;
        catalog_log(paths, result->status == W4X_CATALOG_STATUS_CURRENT ?
                   "live catalog unchanged" : "catalog refreshed from Play page");
    }
    return rc;
}

void w4x_catalog_options_init(W4XCatalogOptions *options,
                              const W4XPathSet *paths)
{
    memset(options, 0, sizeof(*options));
    options->catalog_source = getenv("WASM4_CATALOG_SOURCE");
    if (options->catalog_source == NULL || options->catalog_source[0] == '\0')
        options->catalog_source = paths->catalog_source_path;
    options->catalog_url = getenv("WASM4_CATALOG_URL");
    options->play_source = getenv("WASM4_PLAY_SOURCE");
    options->play_url = getenv("WASM4_PLAY_URL");
    if (options->play_url == NULL || options->play_url[0] == '\0')
        options->play_url = W4X_DEFAULT_PLAY_URL;
    options->ttl_seconds = parse_long_default(getenv("WASM4_CATALOG_TTL_SECONDS"),
                                              W4X_DEFAULT_TTL_SECONDS);
}

int w4x_refresh_catalog(const W4XPathSet *paths,
                        const W4XCatalogOptions *options,
                        W4XCatalogResult *result)
{
    memset(result, 0, sizeof(*result));
    result->status = W4X_CATALOG_STATUS_MISSING;

    if (cache_is_fresh(paths, options)) {
        result->status = W4X_CATALOG_STATUS_CURRENT;
        result->used_cache = true;
        result->row_count = count_catalog_rows(paths->cache_catalog_tsv_path);
        write_file_hash(paths->catalog_hash_path, file_hash(paths->cache_catalog_tsv_path));
        catalog_log(paths, "catalog cache is fresh");
        return result->row_count > 0 ? 0 : -1;
    }

    if (options->catalog_source != NULL && is_file(options->catalog_source)) {
        if (update_cache_from_tsv(paths, options->catalog_source, result,
                                  W4X_CATALOG_STATUS_UPDATED) == 0) {
            catalog_log(paths, result->status == W4X_CATALOG_STATUS_CURRENT ?
                       "catalog source unchanged: %s" :
                       "catalog refreshed from local source: %s",
                       options->catalog_source);
            return 0;
        }
    }

    if (options->catalog_url != NULL && options->catalog_url[0] != '\0') {
        if (refresh_from_catalog_url(paths, options, result) == 0)
            return 0;
    }
    else if (refresh_from_play(paths, options, result) == 0) {
        return 0;
    }


    if (is_file(paths->cache_catalog_tsv_path)) {
        result->status = W4X_CATALOG_STATUS_STALE;
        result->used_cache = true;
        result->row_count = count_catalog_rows(paths->cache_catalog_tsv_path);
        catalog_log(paths, "using stale cached catalog");
        return result->row_count > 0 ? 0 : -1;
    }

    catalog_log(paths, "no catalog source available");
    return -1;
}

const char *w4x_catalog_status_string(W4XCatalogStatus status)
{
    switch (status) {
    case W4X_CATALOG_STATUS_CURRENT:
        return "current";
    case W4X_CATALOG_STATUS_UPDATED:
        return "updated";
    case W4X_CATALOG_STATUS_STALE:
        return "stale";
    case W4X_CATALOG_STATUS_MISSING:
    default:
        return "missing";
    }
}

static void strip_line_end(char *line)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

static char *next_tsv_field(char **cursor)
{
    char *field = *cursor;
    if (field == NULL)
        return NULL;

    char *tab = strchr(field, '\t');
    if (tab != NULL) {
        *tab = '\0';
        *cursor = tab + 1;
    }
    else {
        *cursor = NULL;
    }
    return field;
}

static void copy_field(char *dest, size_t dest_size, const char *value,
                       const char *fallback)
{
    const char *source = value != NULL && value[0] != '\0' ? value : fallback;
    if (source == NULL)
        source = "";
    snprintf(dest, dest_size, "%s", source);
}

static bool entry_is_installed(const W4XPathSet *paths, const W4XCatalogEntry *entry)
{
    char meta_path[W4X_PATH_MAX];
    int written = snprintf(meta_path, sizeof(meta_path),
                           "%s/.wasm4/%s.env", paths->rom_dir, entry->slug);
    return written > 0 && (size_t)written < sizeof(meta_path) && is_file(meta_path);
}

void w4x_catalog_refresh_installed(const W4XPathSet *paths, W4XCatalog *catalog)
{
    if (paths == NULL || catalog == NULL)
        return;

    for (size_t i = 0; i < catalog->count; i++)
        catalog->entries[i].installed = entry_is_installed(paths, &catalog->entries[i]);
}

int w4x_catalog_load_cache(const W4XPathSet *paths, W4XCatalog *catalog)
{
    memset(catalog, 0, sizeof(*catalog));

    FILE *fp = fopen(paths->cache_catalog_tsv_path, "r");
    if (fp == NULL)
        return -1;

    char line[16384];
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }

    size_t capacity = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        strip_line_end(line);
        if (line[0] == '\0')
            continue;

        char *cursor = line;
        char *fields[15] = {0};
        for (size_t i = 0; i < 15; i++)
            fields[i] = next_tsv_field(&cursor);

        if (fields[0] == NULL || !slug_is_valid(fields[0])) {
            catalog_log(paths, "skipping cached catalog row with malformed slug: %s",
                        fields[0] != NULL ? fields[0] : "");
            continue;
        }

        if (catalog->count == capacity) {
            size_t next_capacity = capacity == 0 ? 64 : capacity * 2;
            W4XCatalogEntry *next_entries = realloc(catalog->entries,
                                                    next_capacity * sizeof(*catalog->entries));
            if (next_entries == NULL) {
                fclose(fp);
                w4x_catalog_free(catalog);
                return -1;
            }
            catalog->entries = next_entries;
            capacity = next_capacity;
        }

        W4XCatalogEntry *entry = &catalog->entries[catalog->count];
        memset(entry, 0, sizeof(*entry));
        copy_field(entry->slug, sizeof(entry->slug), fields[0], "");
        copy_field(entry->title, sizeof(entry->title), fields[1], entry->slug);
        copy_field(entry->author, sizeof(entry->author), fields[2], "Unknown");
        copy_field(entry->authors_json, sizeof(entry->authors_json), fields[3], "[]");
        copy_field(entry->date, sizeof(entry->date), fields[4], "");
        copy_field(entry->license, sizeof(entry->license), fields[5], "");
        copy_field(entry->page_url, sizeof(entry->page_url), fields[6], "");
        copy_field(entry->cart_url, sizeof(entry->cart_url), fields[7], "");
        copy_field(entry->image_url, sizeof(entry->image_url), fields[8], "");
        copy_field(entry->manual_url, sizeof(entry->manual_url), fields[9], "");
        copy_field(entry->wasm_sha256, sizeof(entry->wasm_sha256), fields[10], "");
        copy_field(entry->image_sha256, sizeof(entry->image_sha256), fields[11], "");
        copy_field(entry->remote_updated_at, sizeof(entry->remote_updated_at),
                   fields[12], "");
        copy_field(entry->description, sizeof(entry->description), fields[13], "");
        if (fields[14] != NULL && fields[14][0] != '\0') {
            copy_field(entry->row_fingerprint, sizeof(entry->row_fingerprint),
                       fields[14], "");
        }
        else {
            uint64_t fingerprint = row_fingerprint(entry->slug, entry->title,
                                                   entry->author, entry->cart_url,
                                                   entry->image_url);
            snprintf(entry->row_fingerprint, sizeof(entry->row_fingerprint),
                     "%016llx", (unsigned long long)fingerprint);
        }
        entry->installed = entry_is_installed(paths, entry);
        catalog->count++;
    }

    fclose(fp);
    return catalog->count > 0 ? 0 : -1;
}

void w4x_catalog_free(W4XCatalog *catalog)
{
    if (catalog == NULL)
        return;
    free(catalog->entries);
    memset(catalog, 0, sizeof(*catalog));
}
