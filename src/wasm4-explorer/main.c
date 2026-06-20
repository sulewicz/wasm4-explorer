#include "explorer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Options {
    const char *app_dir;
    const char *sd_root;
    W4XCatalogOptions catalog;
    bool check_only;
    bool refresh_catalog;
    bool catalog_ttl_set;
    bool thumbnail_window;
    bool invalidate_stale_thumbnails;
    bool headless;
    const char *install_slug;
    const char *launch_slug;
    size_t thumbnail_start;
    size_t thumbnail_count;
} Options;

static void print_usage(const char *argv0)
{
    printf("Usage: %s [--app-dir DIR] [--sd-root DIR] [--check] [--refresh-catalog] [--install-slug SLUG] [--launch-slug SLUG] [--headless]\n", argv0);
}

static int parse_options(int argc, char *argv[], Options *options)
{
    memset(options, 0, sizeof(*options));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("wasm4-explorer-native %s\n", W4X_VERSION);
            exit(0);
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
        if (strcmp(argv[i], "--check") == 0) {
            options->check_only = true;
            continue;
        }
        if (strcmp(argv[i], "--refresh-catalog") == 0) {
            options->refresh_catalog = true;
            continue;
        }
        if (strcmp(argv[i], "--force-refresh") == 0) {
            options->catalog.force = true;
            continue;
        }
        if (strcmp(argv[i], "--thumbnail-window") == 0 && i + 2 < argc) {
            options->thumbnail_window = true;
            options->thumbnail_start = (size_t)strtoul(argv[++i], NULL, 10);
            options->thumbnail_count = (size_t)strtoul(argv[++i], NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--invalidate-stale") == 0) {
            options->invalidate_stale_thumbnails = true;
            continue;
        }
        if (strcmp(argv[i], "--headless") == 0) {
            options->headless = true;
            continue;
        }
        if (strcmp(argv[i], "--install-slug") == 0 && i + 1 < argc) {
            options->install_slug = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--launch-slug") == 0 && i + 1 < argc) {
            options->launch_slug = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--catalog-source") == 0 && i + 1 < argc) {
            options->catalog.catalog_source = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--catalog-url") == 0 && i + 1 < argc) {
            options->catalog.catalog_url = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--play-source") == 0 && i + 1 < argc) {
            options->catalog.play_source = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--play-url") == 0 && i + 1 < argc) {
            options->catalog.play_url = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--catalog-ttl") == 0 && i + 1 < argc) {
            options->catalog.ttl_seconds = strtol(argv[++i], NULL, 10);
            options->catalog_ttl_set = true;
            continue;
        }
        if (strcmp(argv[i], "--app-dir") == 0 && i + 1 < argc) {
            options->app_dir = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--sd-root") == 0 && i + 1 < argc) {
            options->sd_root = argv[++i];
            continue;
        }

        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    return 0;
}

static int refresh_catalog_and_print(const W4XPathSet *paths,
                                     const W4XCatalogOptions *options)
{
    W4XCatalogResult catalog;
    int rc = w4x_refresh_catalog(paths, options, &catalog);
    printf("catalog_status=%s rows=%zu cache=%s path=%s\n",
           w4x_catalog_status_string(catalog.status),
           catalog.row_count,
           catalog.used_cache ? "yes" : "no",
           paths->cache_catalog_tsv_path);
    return rc;
}

static int print_thumbnail_window(const W4XPathSet *paths, const Options *options)
{
    W4XCatalog catalog;
    if (w4x_catalog_load_cache(paths, &catalog) != 0) {
        fprintf(stderr, "No cached catalog available for thumbnail window\n");
        return 1;
    }

    size_t end = options->thumbnail_start + options->thumbnail_count;
    if (end > catalog.count)
        end = catalog.count;

    for (size_t i = options->thumbnail_start; i < end; i++) {
        W4XCatalogEntry *entry = &catalog.entries[i];
        W4XThumbnailStatus status = w4x_thumbnail_status(
            paths, entry, options->invalidate_stale_thumbnails);
        printf("%zu\t%s\t%s\n", i, entry->slug,
               w4x_thumbnail_status_string(status));
    }

    w4x_catalog_free(&catalog);
    return 0;
}

static int install_slug_and_print(const W4XPathSet *paths, const char *slug,
                                  bool prepare_launch)
{
    W4XCatalog catalog;
    if (w4x_catalog_load_cache(paths, &catalog) != 0) {
        fprintf(stderr, "No cached catalog available for install\n");
        return 1;
    }

    W4XCatalogEntry *entry = NULL;
    for (size_t i = 0; i < catalog.count; i++) {
        if (strcmp(catalog.entries[i].slug, slug) == 0) {
            entry = &catalog.entries[i];
            break;
        }
    }

    if (entry == NULL) {
        fprintf(stderr, "Catalog slug not found: %s\n", slug);
        w4x_catalog_free(&catalog);
        return 1;
    }

    W4XInstallResult result;
    int rc = w4x_install_catalog_entry(paths, entry, &result);
    if (rc == 0) {
        printf("installed_path=%s filename=%s image=%s reused_cart=%s reused_image=%s\n",
               result.cart_path,
               result.filename,
               result.image_rel,
               result.reused_cart ? "yes" : "no",
               result.reused_image ? "yes" : "no");
        if (prepare_launch) {
            W4XLaunchResult launch;
            rc = w4x_prepare_onion_launch(paths, entry, &result, &launch);
            if (rc == 0) {
                printf("cmd_path=%s recent_path=%s quick_switch=%s return_marker=%s\n",
                       launch.cmd_path, launch.recent_path,
                       launch.quick_switch_path, launch.return_marker_path);
            }
            else {
                fprintf(stderr, "Launch handoff failed for slug: %s\n", slug);
            }
        }
    }
    else {
        fprintf(stderr, "Install failed for slug: %s\n", slug);
    }

    w4x_catalog_free(&catalog);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char *argv[])
{
    Options options;
    int parse_rc = parse_options(argc, argv, &options);
    if (parse_rc != 0)
        return parse_rc;

    W4XPathSet paths;
    if (w4x_resolve_paths(&paths, options.app_dir, options.sd_root) != 0) {
        fprintf(stderr, "Unable to resolve WASM-4 Explorer paths\n");
        return 1;
    }

    if (w4x_ensure_runtime_dirs(&paths) != 0) {
        fprintf(stderr, "Unable to create WASM-4 Explorer runtime directories\n");
        return 1;
    }

    W4XCatalogOptions catalog_defaults;
    w4x_catalog_options_init(&catalog_defaults, &paths);
    if (options.catalog.catalog_source == NULL)
        options.catalog.catalog_source = catalog_defaults.catalog_source;
    if (options.catalog.catalog_url == NULL)
        options.catalog.catalog_url = catalog_defaults.catalog_url;
    if (options.catalog.play_source == NULL)
        options.catalog.play_source = catalog_defaults.play_source;
    if (options.catalog.play_url == NULL)
        options.catalog.play_url = catalog_defaults.play_url;
    if (!options.catalog_ttl_set)
        options.catalog.ttl_seconds = catalog_defaults.ttl_seconds;

    if (options.refresh_catalog)
        return refresh_catalog_and_print(&paths, &options.catalog) == 0 ? 0 : 1;

    if (options.thumbnail_window)
        return print_thumbnail_window(&paths, &options);

    if (options.install_slug != NULL)
        return install_slug_and_print(&paths, options.install_slug, false);

    if (options.launch_slug != NULL)
        return install_slug_and_print(&paths, options.launch_slug, true);

    W4XRuntimeCheck check;
    if (w4x_check_runtime(&paths, &check) != 0) {
        fprintf(stderr, "Unable to check WASM-4 Explorer runtime\n");
        return 1;
    }

    int missing = w4x_runtime_missing_count(&check);
    if (options.check_only) {
        w4x_print_runtime_check(&paths, &check);
        return missing == 0 ? 0 : 1;
    }

    if (missing != 0) {
        w4x_print_runtime_check(&paths, &check);
        (void)w4x_log(&paths, "native Explorer scaffold found missing runtime inputs");
        return 1;
    }

    W4XCatalogResult catalog;
    if (w4x_refresh_catalog(&paths, &options.catalog, &catalog) != 0) {
        fprintf(stderr, "No WASM-4 catalog available. Connect Wi-Fi and retry.\n");
        (void)w4x_log(&paths, "native Explorer found no usable catalog cache");
        return 1;
    }

    W4XCatalog loaded_catalog;
    if (w4x_catalog_load_cache(&paths, &loaded_catalog) != 0) {
        fprintf(stderr, "Unable to load WASM-4 catalog cache\n");
        return 1;
    }

    (void)w4x_log(&paths, "native Explorer scaffold launched");
    int ui_rc = w4x_run_browser_ui(&paths, &loaded_catalog, &options.catalog,
                                   options.headless);
    w4x_catalog_free(&loaded_catalog);
    return ui_rc == 0 ? 0 : 1;
}
