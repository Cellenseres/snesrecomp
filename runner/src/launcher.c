/*
 * launcher.c - ROM discovery, verification, and cached path selection.
 *
 * Executable-relative path policy lives in host_paths.c. This file owns only
 * the ROM picker and the shared resolution flow used by the CRC32, SHA-256,
 * and permissive multi-hash public entry points.
 */
#include "launcher.h"
#include "crc32.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <commdlg.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "comdlg32.lib")
#  endif
#endif

/* ---- rom.cfg persistence ---- */

static void get_rom_cfg_path(char *out, size_t max_len) {
    if (!snesrecomp_exe_dir_path("rom.cfg", out, max_len))
        snprintf(out, max_len, "rom.cfg");
}

static void rom_cfg_read(char *path_out, size_t max_len) {
    char cfg_path[512];
    get_rom_cfg_path(cfg_path, sizeof(cfg_path));
    FILE *f = fopen(cfg_path, "r");
    if (!f) {
        path_out[0] = '\0';
        return;
    }
    if (!fgets(path_out, (int)max_len, f)) path_out[0] = '\0';
    fclose(f);
    size_t len = strlen(path_out);
    while (len > 0 && (path_out[len - 1] == '\n' ||
                       path_out[len - 1] == '\r'))
        path_out[--len] = '\0';
}

static void rom_cfg_write(const char *rom_path) {
    char cfg_path[512];
    get_rom_cfg_path(cfg_path, sizeof(cfg_path));
    FILE *f = fopen(cfg_path, "w");
    if (!f) return;
    fprintf(f, "%s\n", rom_path);
    fclose(f);
}

/* ---- File picker ---- */

#ifndef _WIN32
/* Run one shell-wrapped native chooser and read its selected path. */
static int run_picker_cmd(const char *cmd, char *out, size_t max_len) {
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    char buf[1024];
    buf[0] = '\0';
    char *got = fgets(buf, sizeof(buf), p);
    int rc = pclose(p);
    if (!got) return 0;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    if (rc != 0 || n == 0 || n >= max_len) return 0;
    memcpy(out, buf, n + 1);
    return 1;
}
#endif

static int pick_rom_file(char *out, size_t max_len) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    out[0] = '\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter =
        "SNES ROMs (*.sfc;*.smc)\0*.sfc;*.smc\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)max_len;
    ofn.lpstrTitle = "Select SNES ROM";
    /* The common dialog must not undo the executable-directory anchor. */
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? 1 : 0;
#else
    out[0] = '\0';
    static const char *const pickers[] = {
        "command -v zenity >/dev/null 2>&1 && "
        "zenity --file-selection --title='Select SNES ROM' "
        "--file-filter='SNES ROMs (.sfc .smc) | *.sfc *.smc *.SFC *.SMC' "
        "--file-filter='All files | *' 2>/dev/null",
        "command -v kdialog >/dev/null 2>&1 && "
        "kdialog --getopenfilename \"${HOME:-/}\" "
        "'*.sfc *.smc *.SFC *.SMC|SNES ROMs' 2>/dev/null",
        "command -v qarma >/dev/null 2>&1 && "
        "qarma --file-selection --title='Select SNES ROM' 2>/dev/null",
        "command -v osascript >/dev/null 2>&1 && "
        "osascript -e 'POSIX path of "
        "(choose file with prompt \"Select SNES ROM\")' 2>/dev/null",
    };
    for (size_t i = 0; i < sizeof(pickers) / sizeof(pickers[0]); i++)
        if (run_picker_cmd(pickers[i], out, max_len))
            return 1;
    fprintf(stderr,
            "[Launcher] No ROM specified and no graphical file chooser found "
            "(install zenity or kdialog), and no cached rom.cfg.\n"
            "Pass the ROM path as the first argument.\n");
    return 0;
#endif
}

/* ---- ROM image and verification ---- */

typedef struct RomImage {
    uint8_t *storage;
    const uint8_t *payload;
    size_t payload_size;
} RomImage;

/* Load a ROM once and expose its headerless payload. A file whose size is 512
 * bytes beyond a 1 KiB boundary is treated as carrying an SMC copier header,
 * matching the launcher's historical behavior. */
static int rom_image_load(const char *path, RomImage *image) {
    memset(image, 0, sizeof(*image));
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[Launcher] Cannot open '%s'\n", path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long raw_size = ftell(f);
    if (raw_size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    image->storage = (uint8_t *)malloc((size_t)raw_size);
    if (!image->storage) {
        fclose(f);
        return 0;
    }
    size_t read = fread(image->storage, 1, (size_t)raw_size, f);
    fclose(f);
    if (read != (size_t)raw_size) {
        free(image->storage);
        memset(image, 0, sizeof(*image));
        return 0;
    }

    size_t header_size = ((size_t)raw_size % 1024 == 512) ? 512 : 0;
    image->payload = image->storage + header_size;
    image->payload_size = (size_t)raw_size - header_size;
    return 1;
}

static void rom_image_free(RomImage *image) {
    free(image->storage);
    memset(image, 0, sizeof(*image));
}

static void hash_to_hex(const uint8_t hash[32], char hex[65]) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        hex[i * 2] = digits[hash[i] >> 4];
        hex[i * 2 + 1] = digits[hash[i] & 0x0f];
    }
    hex[64] = '\0';
}

static int verify_rom_crc32(const char *path, uint32_t expected_crc) {
    if (expected_crc == 0) return 1;

    RomImage image;
    if (!rom_image_load(path, &image)) return 0;
    uint32_t actual = crc32_compute(image.payload, image.payload_size);
    rom_image_free(&image);
    if (actual == expected_crc) return 1;

    char msg[256];
    snprintf(msg, sizeof(msg),
             "ROM CRC32 mismatch.\n\nExpected: %08X\nGot:      %08X\n\n"
             "Please select the correct ROM file.",
             expected_crc, actual);
    fprintf(stderr, "[Launcher] %s\n", msg);
#ifdef _WIN32
    MessageBoxA(NULL, msg, "Wrong ROM", MB_ICONWARNING | MB_OK);
#endif
    return 0;
}

static int verify_rom_sha256(const char *path,
                             const uint8_t *expected_sha256) {
    if (!expected_sha256) return 1;

    RomImage image;
    if (!rom_image_load(path, &image)) return 0;
    uint8_t actual[32];
    sha256_compute(image.payload, image.payload_size, actual);
    rom_image_free(&image);
    if (memcmp(actual, expected_sha256, sizeof(actual)) == 0) return 1;

    char expected_hex[65];
    char actual_hex[65];
    hash_to_hex(expected_sha256, expected_hex);
    hash_to_hex(actual, actual_hex);
    char msg[512];
    snprintf(msg, sizeof(msg),
             "ROM SHA-256 mismatch.\n\nExpected:\n%s\n\nGot:\n%s\n\n"
             "Please select the correct ROM file.",
             expected_hex, actual_hex);
    fprintf(stderr, "[Launcher] %s\n", msg);
#ifdef _WIN32
    MessageBoxA(NULL, msg, "Wrong ROM", MB_ICONWARNING | MB_OK);
#endif
    return 0;
}

/* Return the first matching digest index, or -1 when the ROM is unreadable or
 * unknown. On an unknown digest, log the computed hash for maintainers. */
static int rom_sha256_match(const char *path,
                            const uint8_t (*hashes)[32], size_t n_hashes) {
    RomImage image;
    if (!rom_image_load(path, &image)) return -1;
    uint8_t actual[32];
    sha256_compute(image.payload, image.payload_size, actual);
    rom_image_free(&image);

    for (size_t i = 0; i < n_hashes; i++)
        if (memcmp(actual, hashes[i], sizeof(actual)) == 0)
            return (int)i;

    char hex[65];
    hash_to_hex(actual, hex);
    fprintf(stderr,
            "[Launcher] ROM SHA-256 %s matches no known hash.\n", hex);
    return -1;
}

static int rom_is_readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* ---- Shared resolution policy ---- */

typedef int (*RomCandidateValidator)(const char *path, int positional,
                                     const void *context);

static int has_positional_rom(int argc, char **argv) {
    return argc >= 2 && argv && argv[1] &&
           argv[1][0] != '-' && argv[1][0] != '\0';
}

static void copy_path_fallback(const char *path,
                               char *out_path, size_t max_len) {
    strncpy(out_path, path, max_len - 1);
    out_path[max_len - 1] = '\0';
}

static int resolve_rom_common(int argc, char **argv,
                              char *out_path, size_t max_len,
                              RomCandidateValidator validate,
                              const void *context) {
    if (!out_path || max_len == 0) return 0;
    out_path[0] = '\0';

    if (has_positional_rom(argc, argv)) {
        if (!snesrecomp_abspath(argv[1], out_path, max_len))
            copy_path_fallback(argv[1], out_path, max_len);
        /* Positional validators preserve the historical warning-and-continue
         * behavior for explicit command-line overrides. */
        if (!validate(out_path, 1, context)) return 0;
        rom_cfg_write(out_path);
        printf("[Launcher] ROM: %s\n", out_path);
        return 1;
    }

    rom_cfg_read(out_path, max_len);
    for (;;) {
        if (out_path[0] == '\0') {
            if (!pick_rom_file(out_path, max_len)) {
                fprintf(stderr, "[Launcher] No ROM selected - exiting.\n");
                out_path[0] = '\0';
                return 0;
            }
        }
        if (validate(out_path, 0, context)) {
            rom_cfg_write(out_path);
            printf("[Launcher] ROM: %s\n", out_path);
            return 1;
        }
        out_path[0] = '\0';
    }
}

typedef struct CrcPolicy {
    uint32_t expected;
} CrcPolicy;

static int validate_crc(const char *path, int positional,
                        const void *context) {
    const CrcPolicy *policy = (const CrcPolicy *)context;
    if (verify_rom_crc32(path, policy->expected)) return 1;
    if (!positional) return 0;
    fprintf(stderr,
            "[Launcher] Warning: CRC mismatch for '%s' - continuing anyway\n",
            path);
    return 1;
}

typedef struct ShaPolicy {
    const uint8_t *expected;
} ShaPolicy;

static int validate_sha(const char *path, int positional,
                        const void *context) {
    const ShaPolicy *policy = (const ShaPolicy *)context;
    if (verify_rom_sha256(path, policy->expected)) return 1;
    if (!positional) return 0;
    fprintf(stderr,
            "[Launcher] Warning: SHA-256 mismatch for '%s' - "
            "continuing anyway\n", path);
    return 1;
}

typedef struct MultiShaPolicy {
    const uint8_t (*hashes)[32];
    size_t count;
} MultiShaPolicy;

static int validate_sha_multi(const char *path, int positional,
                              const void *context) {
    const MultiShaPolicy *policy = (const MultiShaPolicy *)context;
    if (!positional && !rom_is_readable(path)) {
        fprintf(stderr,
                "[Launcher] '%s' is not readable - pick again.\n", path);
        return 0;
    }
    if (policy->count &&
        rom_sha256_match(path, policy->hashes, policy->count) < 0) {
        fprintf(stderr,
                "[Launcher] Warning: '%s' is not a recognized ROM for this "
                "build - loading anyway; the game may misbehave.\n", path);
    }
    return 1;
}

/* ---- Public ---- */

int snesrecomp_launcher_resolve_rom(int argc, char **argv,
                                    char *out_path, size_t max_len,
                                    uint32_t expected_crc) {
    const CrcPolicy policy = {expected_crc};
    return resolve_rom_common(argc, argv, out_path, max_len,
                              validate_crc, &policy);
}

int snesrecomp_launcher_resolve_rom_sha256(int argc, char **argv,
                                           char *out_path, size_t max_len,
                                           const uint8_t *expected_sha256) {
    const ShaPolicy policy = {expected_sha256};
    return resolve_rom_common(argc, argv, out_path, max_len,
                              validate_sha, &policy);
}

int snesrecomp_launcher_resolve_rom_sha256_multi(
        int argc, char **argv, char *out_path, size_t max_len,
        const uint8_t (*hashes)[32], size_t n_hashes) {
    const MultiShaPolicy policy = {hashes, n_hashes};
    return resolve_rom_common(argc, argv, out_path, max_len,
                              validate_sha_multi, &policy);
}
