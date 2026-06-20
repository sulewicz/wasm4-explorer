#include "explorer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0 || src == NULL)
        return -1;

    int written = snprintf(dest, dest_size, "%s", src);
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

static int path_parent(char *path)
{
    strip_trailing_slashes(path);
    char *slash = strrchr(path, '/');
    if (slash == NULL)
        return -1;
    if (slash == path) {
        path[1] = '\0';
        return 0;
    }
    *slash = '\0';
    return 0;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
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

int w4x_resolve_paths(W4XPathSet *paths, const char *app_dir_arg,
                      const char *sd_root_arg)
{
    if (paths == NULL)
        return -1;

    memset(paths, 0, sizeof(*paths));

    const char *app_dir = app_dir_arg;
    if (app_dir == NULL || app_dir[0] == '\0')
        app_dir = getenv("APP_DIR");

    char cwd[W4X_PATH_MAX];
    if (app_dir == NULL || app_dir[0] == '\0') {
        if (getcwd(cwd, sizeof(cwd)) == NULL)
            return -1;
        app_dir = cwd;
    }

    if (copy_string(paths->app_dir, sizeof(paths->app_dir), app_dir) != 0)
        return -1;
    strip_trailing_slashes(paths->app_dir);

    const char *sd_root = sd_root_arg;
    if (sd_root == NULL || sd_root[0] == '\0')
        sd_root = getenv("WASM4_SD_ROOT");

    if (sd_root != NULL && sd_root[0] != '\0') {
        if (copy_string(paths->sd_root, sizeof(paths->sd_root), sd_root) != 0)
            return -1;
        strip_trailing_slashes(paths->sd_root);
    }
    else {
        if (copy_string(paths->sd_root, sizeof(paths->sd_root), paths->app_dir) != 0)
            return -1;
        if (path_parent(paths->sd_root) != 0 || path_parent(paths->sd_root) != 0)
            return -1;
    }

    return path_join(paths->cache_dir, sizeof(paths->cache_dir), paths->app_dir, "cache") ||
           path_join(paths->catalog_dir, sizeof(paths->catalog_dir), paths->app_dir, "catalog") ||
           path_join(paths->logs_dir, sizeof(paths->logs_dir), paths->app_dir, "logs") ||
           path_join(paths->rom_dir, sizeof(paths->rom_dir), paths->sd_root, "Roms/WASM4") ||
           path_join(paths->catalog_source_path, sizeof(paths->catalog_source_path),
                     paths->catalog_dir, "catalog.tsv") ||
           path_join(paths->cache_catalog_tsv_path, sizeof(paths->cache_catalog_tsv_path),
                     paths->cache_dir, "catalog.tsv") ||
           path_join(paths->catalog_fetched_at_path, sizeof(paths->catalog_fetched_at_path),
                     paths->cache_dir, "catalog.fetched_at") ||
           path_join(paths->catalog_hash_path, sizeof(paths->catalog_hash_path),
                     paths->cache_dir, "catalog.cache_sha256") ||
           path_join(paths->core_path, sizeof(paths->core_path), paths->sd_root,
                     "RetroArch/.retroarch/cores/wasm4_libretro.so") ||
           path_join(paths->cores_info_path, sizeof(paths->cores_info_path), paths->sd_root,
                     "RetroArch/.retroarch/cores/wasm4_libretro.info") ||
           path_join(paths->info_path, sizeof(paths->info_path), paths->sd_root,
                     "RetroArch/.retroarch/info/wasm4_libretro.info") ||
           path_join(paths->core_info_path, sizeof(paths->core_info_path), paths->sd_root,
                     "RetroArch/.retroarch/core_info/wasm4_libretro.info") ||
           path_join(paths->launcher_path, sizeof(paths->launcher_path), paths->sd_root,
                     "Emu/WASM4/launch.sh");
}

int w4x_ensure_runtime_dirs(const W4XPathSet *paths)
{
    if (paths == NULL)
        return -1;
    if (mkdir_p(paths->cache_dir) != 0)
        return -1;
    char images_dir[W4X_PATH_MAX];
    if (path_join(images_dir, sizeof(images_dir), paths->cache_dir, "images") != 0)
        return -1;
    if (mkdir_p(images_dir) != 0)
        return -1;
    if (mkdir_p(paths->catalog_dir) != 0)
        return -1;
    if (mkdir_p(paths->logs_dir) != 0)
        return -1;
    if (mkdir_p(paths->rom_dir) != 0)
        return -1;
    return 0;
}

int w4x_check_runtime(const W4XPathSet *paths, W4XRuntimeCheck *check)
{
    if (paths == NULL || check == NULL)
        return -1;

    memset(check, 0, sizeof(*check));
    check->app_dir_ok = is_dir(paths->app_dir);
    check->cache_dir_ok = is_dir(paths->cache_dir);
    check->catalog_dir_ok = is_dir(paths->catalog_dir);
    check->logs_dir_ok = is_dir(paths->logs_dir);
    check->rom_dir_ok = is_dir(paths->rom_dir);
    check->core_ok = is_file(paths->core_path);
    check->cores_info_ok = is_file(paths->cores_info_path);
    check->info_ok = is_file(paths->info_path);
    check->core_info_ok = is_file(paths->core_info_path);
    check->launcher_ok = is_file(paths->launcher_path);
    return 0;
}

int w4x_runtime_missing_count(const W4XRuntimeCheck *check)
{
    if (check == NULL)
        return 1;

    int missing = 0;
    missing += !check->app_dir_ok;
    missing += !check->cache_dir_ok;
    missing += !check->catalog_dir_ok;
    missing += !check->logs_dir_ok;
    missing += !check->rom_dir_ok;
    missing += !check->core_ok;
    missing += !check->cores_info_ok;
    missing += !check->info_ok;
    missing += !check->core_info_ok;
    missing += !check->launcher_ok;
    return missing;
}

static void print_status(const char *label, bool ok, const char *path)
{
    printf("%s=%s %s\n", label, ok ? "ok" : "missing", path);
}

void w4x_print_runtime_check(const W4XPathSet *paths,
                             const W4XRuntimeCheck *check)
{
    if (paths == NULL || check == NULL)
        return;

    printf("app_dir=%s\n", paths->app_dir);
    printf("sd_root=%s\n", paths->sd_root);
    print_status("cache_dir", check->cache_dir_ok, paths->cache_dir);
    print_status("catalog_dir", check->catalog_dir_ok, paths->catalog_dir);
    print_status("logs_dir", check->logs_dir_ok, paths->logs_dir);
    print_status("rom_dir", check->rom_dir_ok, paths->rom_dir);
    print_status("core", check->core_ok, paths->core_path);
    print_status("cores_info", check->cores_info_ok, paths->cores_info_path);
    print_status("info", check->info_ok, paths->info_path);
    print_status("core_info", check->core_info_ok, paths->core_info_path);
    print_status("launcher", check->launcher_ok, paths->launcher_path);
}

int w4x_log(const W4XPathSet *paths, const char *message)
{
    if (paths == NULL || message == NULL)
        return -1;

    char log_path[W4X_PATH_MAX];
    if (path_join(log_path, sizeof(log_path), paths->logs_dir, "native-explorer.log") != 0)
        return -1;

    FILE *fp = fopen(log_path, "a");
    if (fp == NULL)
        return -1;

    fprintf(fp, "%s\n", message);
    fclose(fp);
    return 0;
}
