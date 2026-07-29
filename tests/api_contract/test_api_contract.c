/*
 * Public API contract, checked by the compiler instead of by grepping headers.
 *
 * The gates this replaces asserted that a header *contained* a piece of text.
 * That passes when the name appears only in a comment, fails when a declaration
 * is merely reformatted, and says nothing about the signature.
 *
 * Here each promised function initialises a file-scope pointer of its expected
 * type. Using an identifier outside a call requires a declaration, so a removed
 * function is a compile error; a changed signature is an incompatible-pointer
 * error. Constants are checked with _Static_assert against their value rather
 * than their spelling.
 *
 * This translation unit is compiled with -fsyntax-only and never linked: the
 * question is what a caller can see, not what exists in a binary.
 *
 * The negative half is tests/api_contract/retired — one file per retired symbol,
 * each of which must fail to compile.
 */
#include "audio_fw_preload.h"
#include "audio_mixer.h"
#include "audio_output_timing.h"
#include "audio_pcm_timeline.h"
#include "audio_scratch_buffer.h"
#include "library.h"
#include "wifi_link.h"

/* ── library ─────────────────────────────────────────────────────────────── */

_Static_assert(LIBRARY_PATH_MAX >= 256, "track paths must hold a full USB path");
_Static_assert(LIBRARY_STR_MAX >= 64, "title/artist fields must not shrink silently");

static esp_err_t (*const c_library_init)(void) = library_init;
static void      (*const c_library_clear)(void) = library_clear;
static uint32_t  (*const c_library_generation)(void) = library_generation;
static int       (*const c_library_count)(void) = library_count;
static esp_err_t (*const c_library_get)(int, library_track_t *) = library_get;
static esp_err_t (*const c_library_get_summary)(int, uint16_t *, uint32_t *) =
    library_get_summary;
static uint32_t  (*const c_library_track_key)(const library_track_t *) = library_track_key;
static esp_err_t (*const c_library_load_anlz)(library_track_t *) = library_load_anlz;
static void      (*const c_library_sort)(int, bool) = library_sort;
static esp_err_t (*const c_library_clone_current_anlz)(anlz_metadata_t *) =
    library_clone_current_anlz;
static void      (*const c_library_free_current_anlz)(void) = library_free_current_anlz;

/* Selected-track API under its production names. The simulator implements the
 * same two, which is what lets one UI source compile for both targets. */
static void (*const c_library_set_selected)(int) = library_set_selected_track_index;
static int  (*const c_library_selected)(void) = library_selected_track_index;

/* Identity accessors added so highlight, selection and load-by-identity lookups
 * stop copying a ~2.9 KB record per candidate row. */
static esp_err_t (*const c_library_get_row_key)(int, uint32_t *) = library_get_row_key;
static int       (*const c_library_find_row_by_key)(uint32_t) = library_find_row_by_key;

/* ── bounded compressed audio cache ──────────────────────────────────────── */

_Static_assert(AUDIO_FW_CACHE_PAGE_BYTES == 32u * 1024u,
               "cache page size is part of the measured seek behaviour");
_Static_assert(AUDIO_FW_CACHE_PAGE_COUNT == 8u,
               "page count bounds per-deck PSRAM; changing it is a design decision");
_Static_assert(AUDIO_FW_CACHE_BYTES ==
                   AUDIO_FW_CACHE_PAGE_BYTES * AUDIO_FW_CACHE_PAGE_COUNT,
               "total cache size must stay the product of page size and count");
_Static_assert(AUDIO_FW_CACHE_BYTES == 256u * 1024u,
               "256 KiB per deck is the figure the PSRAM budget was signed off on");
_Static_assert(AUDIO_COMPRESSED_CACHE_MAX_PAGES >= AUDIO_FW_CACHE_PAGE_COUNT,
               "the cache must be able to hold every page the loader binds");

/* The preload slot owns its cache by value; a pointer would reintroduce the
 * ownership question the bounded cache exists to settle. */
_Static_assert(sizeof(((audio_fw_preload_t *)0)->cache) == sizeof(audio_compressed_cache_t),
               "preload slot must own its cache by value");

static bool   (*const c_cache_init)(audio_compressed_cache_t *, uint8_t *, size_t,
                                    size_t, size_t,
                                    audio_compressed_cache_read_at_fn, void *) =
    audio_compressed_cache_init;
static void   (*const c_cache_reset)(audio_compressed_cache_t *) = audio_compressed_cache_reset;
static size_t (*const c_cache_read)(audio_compressed_cache_t *, size_t, void *, size_t) =
    audio_compressed_cache_read;
static bool   (*const c_cache_prefetch)(audio_compressed_cache_t *, size_t) =
    audio_compressed_cache_prefetch;
static size_t (*const c_cache_capacity)(const audio_compressed_cache_t *) =
    audio_compressed_cache_capacity;

static bool   (*const c_preload_bind)(audio_fw_preload_t *, uint8_t *, size_t, size_t,
                                      void *, audio_compressed_cache_read_at_fn) =
    audio_fw_preload_bind_cache;
static size_t (*const c_preload_read_at)(audio_fw_preload_t *, size_t, void *, size_t) =
    audio_fw_preload_read_at;
static size_t (*const c_preload_stream_read)(audio_fw_preload_t *, void *, size_t) =
    audio_fw_preload_stream_read;
static bool   (*const c_preload_stream_seek)(audio_fw_preload_t *, int64_t, int) =
    audio_fw_preload_stream_seek;
static size_t (*const c_preload_stream_tell)(const audio_fw_preload_t *) =
    audio_fw_preload_stream_tell;

/* ── wifi_link ───────────────────────────────────────────────────────────── */

_Static_assert(sizeof(WIFI_LINK_PASSWORD) - 1u >= 8u,
               "WPA2 requires at least eight characters");

/* Silence "defined but not used" for a file whose entire purpose is to declare
 * these; -fsyntax-only does not emit them anyway. */
#define CONTRACT_USE(x) ((void)(x))
static inline void api_contract_reference_all(void)
{
    CONTRACT_USE(c_library_init);           CONTRACT_USE(c_library_clear);
    CONTRACT_USE(c_library_generation);     CONTRACT_USE(c_library_count);
    CONTRACT_USE(c_library_get);            CONTRACT_USE(c_library_get_summary);
    CONTRACT_USE(c_library_track_key);      CONTRACT_USE(c_library_load_anlz);
    CONTRACT_USE(c_library_sort);           CONTRACT_USE(c_library_clone_current_anlz);
    CONTRACT_USE(c_library_free_current_anlz);
    CONTRACT_USE(c_library_set_selected);   CONTRACT_USE(c_library_selected);
    CONTRACT_USE(c_library_get_row_key);    CONTRACT_USE(c_library_find_row_by_key);
    CONTRACT_USE(c_cache_init);             CONTRACT_USE(c_cache_reset);
    CONTRACT_USE(c_cache_read);             CONTRACT_USE(c_cache_prefetch);
    CONTRACT_USE(c_cache_capacity);
    CONTRACT_USE(c_preload_bind);           CONTRACT_USE(c_preload_read_at);
    CONTRACT_USE(c_preload_stream_read);    CONTRACT_USE(c_preload_stream_seek);
    CONTRACT_USE(c_preload_stream_tell);
}
