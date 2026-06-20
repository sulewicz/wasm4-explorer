#ifndef WASM4_EXPLORER_H
#define WASM4_EXPLORER_H

#include <stdbool.h>
#include <stddef.h>

#ifndef W4X_VERSION
#define W4X_VERSION "0.1.0"
#endif

#define W4X_PATH_MAX 4096
#define W4X_SLUG_MAX 128
#define W4X_TITLE_MAX 256
#define W4X_AUTHOR_MAX 256
#define W4X_URL_MAX 768
#define W4X_FINGERPRINT_MAX 32
#define W4X_METADATA_MAX 1024
#define W4X_DATE_MAX 64
#define W4X_HASH_MAX 80

typedef struct W4XPathSet {
    char app_dir[W4X_PATH_MAX];
    char sd_root[W4X_PATH_MAX];
    char cache_dir[W4X_PATH_MAX];
    char catalog_dir[W4X_PATH_MAX];
    char logs_dir[W4X_PATH_MAX];
    char rom_dir[W4X_PATH_MAX];
    char catalog_source_path[W4X_PATH_MAX];
    char cache_catalog_tsv_path[W4X_PATH_MAX];
    char catalog_fetched_at_path[W4X_PATH_MAX];
    char catalog_hash_path[W4X_PATH_MAX];
    char core_path[W4X_PATH_MAX];
    char cores_info_path[W4X_PATH_MAX];
    char info_path[W4X_PATH_MAX];
    char core_info_path[W4X_PATH_MAX];
    char launcher_path[W4X_PATH_MAX];
} W4XPathSet;

typedef struct W4XRuntimeCheck {
    bool app_dir_ok;
    bool cache_dir_ok;
    bool catalog_dir_ok;
    bool logs_dir_ok;
    bool rom_dir_ok;
    bool core_ok;
    bool cores_info_ok;
    bool info_ok;
    bool core_info_ok;
    bool launcher_ok;
} W4XRuntimeCheck;

typedef enum W4XCatalogStatus {
    W4X_CATALOG_STATUS_CURRENT,
    W4X_CATALOG_STATUS_UPDATED,
    W4X_CATALOG_STATUS_STALE,
    W4X_CATALOG_STATUS_MISSING
} W4XCatalogStatus;

typedef struct W4XCatalogOptions {
    const char *catalog_source;
    const char *catalog_url;
    const char *play_source;
    const char *play_url;
    long ttl_seconds;
    bool force;
} W4XCatalogOptions;

typedef struct W4XCatalogResult {
    W4XCatalogStatus status;
    size_t row_count;
    bool used_cache;
} W4XCatalogResult;

typedef struct W4XCatalogEntry {
    char slug[W4X_SLUG_MAX];
    char title[W4X_TITLE_MAX];
    char author[W4X_AUTHOR_MAX];
    char authors_json[W4X_METADATA_MAX];
    char date[W4X_DATE_MAX];
    char license[W4X_METADATA_MAX];
    char page_url[W4X_URL_MAX];
    char cart_url[W4X_URL_MAX];
    char image_url[W4X_URL_MAX];
    char manual_url[W4X_URL_MAX];
    char wasm_sha256[W4X_HASH_MAX];
    char image_sha256[W4X_HASH_MAX];
    char remote_updated_at[W4X_DATE_MAX];
    char description[W4X_METADATA_MAX];
    char row_fingerprint[W4X_FINGERPRINT_MAX];
    bool installed;
} W4XCatalogEntry;

typedef struct W4XCatalog {
    W4XCatalogEntry *entries;
    size_t count;
} W4XCatalog;

typedef enum W4XThumbnailStatus {
    W4X_THUMBNAIL_MISSING,
    W4X_THUMBNAIL_VALID,
    W4X_THUMBNAIL_STALE
} W4XThumbnailStatus;

typedef struct W4XInstallResult {
    char cart_path[W4X_PATH_MAX];
    char image_path[W4X_PATH_MAX];
    char filename[W4X_TITLE_MAX];
    char image_rel[W4X_TITLE_MAX];
    bool reused_cart;
    bool reused_image;
    bool installed_image;
} W4XInstallResult;

typedef struct W4XLaunchResult {
    char cmd_path[W4X_PATH_MAX];
    char recent_path[W4X_PATH_MAX];
    char quick_switch_path[W4X_PATH_MAX];
    char return_marker_path[W4X_PATH_MAX];
} W4XLaunchResult;

int w4x_resolve_paths(W4XPathSet *paths, const char *app_dir_arg,
                      const char *sd_root_arg);
int w4x_ensure_runtime_dirs(const W4XPathSet *paths);
int w4x_check_runtime(const W4XPathSet *paths, W4XRuntimeCheck *check);
int w4x_runtime_missing_count(const W4XRuntimeCheck *check);
void w4x_print_runtime_check(const W4XPathSet *paths,
                             const W4XRuntimeCheck *check);
int w4x_log(const W4XPathSet *paths, const char *message);

void w4x_catalog_options_init(W4XCatalogOptions *options,
                              const W4XPathSet *paths);
int w4x_refresh_catalog(const W4XPathSet *paths,
                        const W4XCatalogOptions *options,
                        W4XCatalogResult *result);
const char *w4x_catalog_status_string(W4XCatalogStatus status);
int w4x_catalog_load_cache(const W4XPathSet *paths, W4XCatalog *catalog);
void w4x_catalog_free(W4XCatalog *catalog);
void w4x_catalog_refresh_installed(const W4XPathSet *paths, W4XCatalog *catalog);

int w4x_thumbnail_path(const W4XPathSet *paths, const W4XCatalogEntry *entry,
                       char *image_path, size_t image_path_size);
int w4x_thumbnail_fingerprint_path(const W4XPathSet *paths,
                                   const W4XCatalogEntry *entry,
                                   char *fingerprint_path,
                                   size_t fingerprint_path_size);
W4XThumbnailStatus w4x_thumbnail_status(const W4XPathSet *paths,
                                        const W4XCatalogEntry *entry,
                                        bool remove_stale);
int w4x_thumbnail_mark_valid(const W4XPathSet *paths,
                             const W4XCatalogEntry *entry);
const char *w4x_thumbnail_status_string(W4XThumbnailStatus status);

int w4x_install_catalog_entry(const W4XPathSet *paths,
                              const W4XCatalogEntry *entry,
                              W4XInstallResult *result);
int w4x_prepare_onion_launch(const W4XPathSet *paths,
                             const W4XCatalogEntry *entry,
                             const W4XInstallResult *install,
                             W4XLaunchResult *result);

int w4x_run_browser_ui(const W4XPathSet *paths, W4XCatalog *catalog,
                       const W4XCatalogOptions *catalog_options, bool headless);

#endif
