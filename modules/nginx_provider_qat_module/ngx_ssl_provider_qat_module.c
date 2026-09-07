/* ====================================================================
 *
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2026 Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * ====================================================================
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#if (NGX_HTTP_SSL)
#include <ngx_http.h>
#endif
#if (NGX_STREAM_SSL)
#include <ngx_stream.h>
#endif
#include <openssl/provider.h>
#include <openssl/params.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "ngx_ssl_provider_qat_module.h"

/* Default property query preferring qatprovider for symmetric ciphers. The
 * leading '?' makes it a preference, allowing fallback to default provider
 * for unsupported algorithms without requiring openssl.cnf changes. */
#define QAT_PROV_DEFAULT_PROPERTY_QUERY  "?provider=qatprovider"

/* Provider polling hooks supplied by the nginx SSL core. */
extern ngx_uint_t   ngx_ssl_provider_enable_hotpath_polling;
extern void       (*ngx_ssl_provider_hotpath_poll)(ngx_log_t *log);


/* Provider parameters exchanged through OSSL_PROVIDER_get_params(). */
#define QAT_PROV_PARAM_ENABLE_EXTERNAL_POLLING      "qat_enable_external_polling"
#define QAT_PROV_PARAM_ENABLE_HEURISTIC_POLLING     "qat_enable_heuristic_polling"
/* Event-driven polling is not exposed by qatprovider (provider-only concept,
 * nginx never wires QAT completion fds into its event loop), so there is no
 * corresponding param to read back or push here. */
#define QAT_PROV_PARAM_ENABLE_SW_FALLBACK           "qat_enable_sw_fallback"
#define QAT_PROV_PARAM_INTERNAL_POLL_INTERVAL       "qat_internal_poll_interval"
#define QAT_PROV_PARAM_POLL                         "qat_poll"
#define QAT_PROV_PARAM_HEARTBEAT_POLL               "qat_heartbeat_poll"
#define QAT_PROV_PARAM_NUM_ASYM_REQUESTS_IN_FLIGHT  "qat_num_asym_requests_in_flight"
#define QAT_PROV_PARAM_NUM_KDF_REQUESTS_IN_FLIGHT   "qat_num_kdf_requests_in_flight"
#define QAT_PROV_PARAM_NUM_CIPHER_REQUESTS_IN_FLIGHT "qat_num_cipher_requests_in_flight"
#define QAT_PROV_PARAM_NUM_ASYM_MB_ITEMS_IN_QUEUE   "qat_num_asym_mb_items_in_queue"
#define QAT_PROV_PARAM_NUM_KDF_MB_ITEMS_IN_QUEUE    "qat_num_kdf_mb_items_in_queue"
#define QAT_PROV_PARAM_NUM_SYM_MB_ITEMS_IN_QUEUE    "qat_num_sym_mb_items_in_queue"
#define QAT_PROV_PARAM_SMALL_PKT_OFFLOAD_THRESHOLD  "qat_small_pkt_offload_threshold"
/* Heuristic-poll thresholds. qatprovider stores and echoes these so adopt mode
 * (openssl.cnf as single source of truth) can carry them back to nginx. The
 * string literals must match the QAT_PROV_PARAM_* names in qatprovider. */
#define QAT_PROV_PARAM_HW_ASYM_THRESHOLD            "qat_hw_asym_threshold"
#define QAT_PROV_PARAM_HW_SYM_THRESHOLD             "qat_hw_sym_threshold"
/* Single QAT_SW multibuff threshold: governs BOTH the SW asym and SW sym
 * queues (qatengine never increments a separate SW-sym counter), hence the
 * neutral name with no asym/sym suffix. */
#define QAT_PROV_PARAM_SW_THRESHOLD                 "qat_sw_threshold"
/* Write-only trigger: 1 starts qatprovider's deferred HW init. Pushed last so
 * the polling flags apply first. The literal string is a fixed qatprovider
 * wire contract shared with qat_provider.h; keep it byte-identical. */
#define QAT_PROV_PARAM_INIT_PROVIDER                "qat_init_provider"
/* Read-only sentinel: 1 when qatprovider was configured from openssl.cnf.
 * Drives the adopt-vs-configure decision below (openssl.cnf > nginx.conf). The
 * string literal is a fixed qatprovider wire contract; keep it byte-identical. */
#define QAT_PROV_PARAM_CONFIGURED_FROM_OPENSSL_CNF  "qat_configured_from_cnf"


typedef struct {
    ngx_flag_t      releasable;
    ngx_flag_t      enable_sw_fallback;
    ngx_str_t       offload_mode;
    ngx_str_t       notify_mode;
    ngx_str_t       poll_mode;
    /* Polling interval; unit/range depend on poll_mode (internal: 1..1e7 us,
     * external/heuristic: 1..1000 ms). Validated in init_conf. */
    ngx_int_t       poll_interval;
    ngx_int_t       qat_hw_asym_threshold;
    ngx_int_t       qat_hw_sym_threshold;
    ngx_int_t       qat_sw_threshold;
    ngx_str_t       small_pkt_offload_threshold;
} ngx_ssl_provider_qat_conf_t;


static ngx_int_t ngx_ssl_provider_qat_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_provider_qat_set_params(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_provider_qat_register_handler(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_provider_qat_release(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_provider_qat_heuristic_poll(ngx_log_t *log);
static void ngx_ssl_provider_qat_heartbeat_poll(ngx_log_t *log);
static void ngx_ssl_provider_qat_hotpath_poll(ngx_log_t *log);
static void ngx_ssl_provider_qat_cipher_seen(EVP_CIPHER *cipher, void *data);

static char *ngx_qat_provider_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_qat_provider_init_conf(ngx_cycle_t *cycle, void *conf);
#if (NGX_HTTP_SSL)
static ngx_int_t ngx_ssl_provider_qat_preconfiguration(ngx_conf_t *cf);
#endif
#if (NGX_STREAM_SSL)
static ngx_int_t ngx_ssl_provider_qat_stream_preconfiguration(ngx_conf_t *cf);
#endif

static void *ngx_ssl_provider_qat_create_conf(ngx_cycle_t *cycle);
static char *ngx_ssl_provider_qat_releasable(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static char *ngx_ssl_provider_qat_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_int_t ngx_ssl_provider_qat_init_module(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_provider_qat_process_init(ngx_cycle_t *cycle);
static void ngx_ssl_provider_qat_process_exit(ngx_cycle_t *cycle);


#define EXTERNAL_POLL_DEFAULT_INTERVAL              1
#define HEURISTIC_POLL_DEFAULT_INTERVAL             1

#define QAT_HW_ASYM_DEFAULT_THRESHOLD               48
#define QAT_HW_SYM_DEFAULT_THRESHOLD                24
#define QAT_SW_DEFAULT_THRESHOLD                    8

/* Max consecutive heuristic ticks a sub-threshold batch may be deferred before
 * it is polled anyway, bounding the tail latency of a batch that never grows. */
#define QAT_PROVIDER_HEURISTIC_MAX_DEFER            2

#define INTERNAL_POLL   1
#define EXTERNAL_POLL   2
#define HEURISTIC_POLL  3

#define HEARTBEAT_POLL_TIMEOUT  1000

/* Bound final worker-exit polling. */
#define QAT_PROVIDER_EXIT_DRAIN_MAX  100

/* Bound graceful-release timer ticks. */
#define QAT_PROVIDER_SHUTDOWN_DRAIN_MAX  3000

/* Size of the provider parameter array. */
#define MAX_PROVIDER_PARAMS  16


static ngx_uint_t       qat_provider_enable_internal_polling;
static ngx_uint_t       qat_provider_enable_external_polling;
static ngx_int_t        qat_provider_poll_interval;

/* qat_shutting_down_release, mirrored from the conf at worker init so the poll
 * handlers can drive the graceful drain without a per-tick conf lookup. */
static ngx_flag_t       qat_provider_releasable;

/* Enable the heartbeat timer only for SW fallback with external polling. */
static ngx_uint_t   qat_provider_enable_heartbeat_polling;

static ngx_uint_t   qat_provider_enable_heuristic_polling;

/* Polling mode committed at master start, used to reject illegal live-reload
 * mode switches. 0:unset, 1:internal, 2:external, 3:heuristic */
static ngx_int_t    qat_provider_init_polling_mode = 0;

/* Records whether nginx.conf supplied QAT mode directives. */
static ngx_uint_t   qat_provider_modes_from_nginx_conf;

/* OPENSSL_CONF path snapshotted in the master (init_conf) while the environment
 * is intact. nginx wipes the worker environment, so the worker cannot read it;
 * the inherited value lets the config-source log name the exact file. */
static char         qat_provider_openssl_conf_path[NGX_MAX_PATH];

/* Per-worker QAT drain state for qat_shutting_down_release. */
typedef struct qat_provider_status_s {
    ngx_flag_t busy;       /* handshake in flight, defer drain */
    ngx_flag_t finished;   /* drain complete, polling stopped */
    ngx_int_t  checkpoint; /* resume index for connection scan */
} qat_provider_status_t;

static qat_provider_status_t  qat_provider_status;

/* Graceful-release timer tick counter, bounded by QAT_PROVIDER_SHUTDOWN_DRAIN_MAX
 * so worker exit can never hang on a wedged drain. Reset per worker in init. */
static ngx_uint_t       qat_provider_shutdown_ticks;


/* Handle of the qatprovider, resolved at worker init. */
static OSSL_PROVIDER    *qat_prov = NULL;

/* Module-owned qatprovider reference released during worker exit. */
static OSSL_PROVIDER    *qat_prov_self_loaded = NULL;

/* Module-owned default-provider reference released during worker exit. */
static OSSL_PROVIDER    *default_prov_self_loaded = NULL;

/* Counted reference for early qatprovider preload when not active in openssl.cnf.
 * Priming qatprovider before http{} builds the SSL_CTX ensures symmetric cipher
 * selection takes effect on initial start. Released in process_exit. */
static OSSL_PROVIDER    *qat_prov_preloaded = NULL;

/* Set once a process has primed the provider before SSL_CTX construction. */
static ngx_flag_t        qat_prov_preload_done;

/* Event-loop driven polling state for external / heuristic / heartbeat modes. */
static ngx_event_t       qat_provider_external_poll_event;
static ngx_event_t       qat_provider_heuristic_poll_event;
static ngx_event_t       qat_provider_heartbeat_poll_event;

/* Placeholder connection required by nginx timer events. */
static ngx_connection_t  qat_provider_timer_conn;

/* In-flight request counters, refreshed from qatprovider before each drain
 * decision. qat_get_params serves them from live internal counters, so a
 * refreshed value reflects current device state, not a stale snapshot. */
static int  num_asym_requests_in_flight;
static int  num_kdf_requests_in_flight;
static int  num_cipher_requests_in_flight;
static int  num_asym_mb_items_in_queue;
static int  num_kdf_mb_items_in_queue;
static int  num_sym_mb_items_in_queue;

/* Heuristic batching state and configured threshold values. */
static int        num_heuristic_poll;
/* Thresholds for HW asymmetry, HW symmetry, and shared SW batching. */
static ngx_int_t  qat_provider_qat_hw_asym_threshold;
static ngx_int_t  qat_provider_qat_hw_sym_threshold;
static ngx_int_t  qat_provider_qat_sw_threshold;


/* ===================================================================
 * Locate the already-loaded qatprovider among all loaded providers.
 * =================================================================== */

struct ngx_qat_provider_find_ctx {
    const char     *name;
    OSSL_PROVIDER  *prov;
};

static int
ngx_qat_provider_find_cb(OSSL_PROVIDER *prov, void *arg)
{
    struct ngx_qat_provider_find_ctx  *ctx = arg;
    const char                        *name;

    name = OSSL_PROVIDER_get0_name(prov);

    if (name != NULL && ngx_strcmp(name, ctx->name) == 0) {
        ctx->prov = prov;
        return 0;  /* stop iteration */
    }

    return 1;      /* continue */
}

static OSSL_PROVIDER *
ngx_ssl_provider_qat_find(void)
{
    struct ngx_qat_provider_find_ctx  fctx = { "qatprovider", NULL };

    OSSL_PROVIDER_do_all(NULL, ngx_qat_provider_find_cb, &fctx);

    return fctx.prov;
}


static ngx_int_t
ngx_ssl_provider_qat_preload(ngx_log_t *log)
{
    OSSL_PROVIDER  *prov;
    unsigned char   rnd;
    ngx_flag_t      qat_cipher_seen = 0;

    if (ngx_test_config || ngx_process == NGX_PROCESS_SIGNALLER) {
        return NGX_OK;
    }

    if (qat_prov_preload_done) {
        return NGX_OK;
    }

    /* Keep OpenSSL's default provider available alongside qatprovider. */
    if (!OSSL_PROVIDER_available(NULL, "default")) {
        default_prov_self_loaded = OSSL_PROVIDER_load(NULL, "default");
        if (default_prov_self_loaded == NULL) {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                          "QAT provider: failed to load the OpenSSL default "
                          "provider (preload)");
            return NGX_ERROR;
        }
    }

    /* Resolve qatprovider, loading it when openssl.cnf did not activate it. */
    prov = ngx_ssl_provider_qat_find();
    if (prov == NULL) {
        prov = OSSL_PROVIDER_load(NULL, "qatprovider");
        if (prov == NULL) {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                          "QAT provider: qatprovider could not be loaded "
                          "(preload); check the QAT error printed above (e.g. an "
                          "invalid qat_poll_mode in openssl.cnf) and ensure "
                          "qatprovider.so is in the OpenSSL modules directory "
                          "(OPENSSL_MODULES)");
            return NGX_ERROR;
        }
        qat_prov_preloaded = prov;
    }

    /* Prime provider cipher discovery before SSL_CTX creation. */
    EVP_CIPHER_do_all_provided(NULL, ngx_ssl_provider_qat_cipher_seen,
                               &qat_cipher_seen);

    if (!qat_cipher_seen) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, log, 0,
                       "QAT provider: no cipher implementation exposed during "
                       "generic preload enumeration");
    }

    /* Instantiate the primary CTR-DRBG now, with the provider init already
     * latched, so SSL_CTX_new() reuses this instance instead of re-fetching the
     * DRBG under the qat-biased default query. */
    (void) RAND_bytes(&rnd, sizeof(rnd));

    /* Bias the TLS record cipher to qatprovider. Harmless if the provider
     * already set the same query itself; the optional '?' lets algorithms
     * qatprovider does not implement fall back to the default provider. */
    if (!EVP_set_default_properties(NULL, QAT_PROV_DEFAULT_PROPERTY_QUERY)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "QAT provider: failed to set default property query "
                      "\"%s\" (preload)", QAT_PROV_DEFAULT_PROPERTY_QUERY);
        return NGX_ERROR;
    }

    qat_prov_preload_done = 1;

    ngx_log_error(NGX_LOG_NOTICE, log, 0,
                  "QAT provider: primed qatprovider at config parse "
                  "(cipher-fetch latch + DRBG warm) and set default property "
                  "query \"%s\" before SSL_CTX build",
                  QAT_PROV_DEFAULT_PROPERTY_QUERY);

    return NGX_OK;
}


static void
ngx_ssl_provider_qat_cipher_seen(EVP_CIPHER *cipher, void *data)
{
    ngx_flag_t              *seen = data;
    const OSSL_PROVIDER     *provider;

    provider = EVP_CIPHER_get0_provider(cipher);

    if (provider != NULL
        && ngx_strcmp((u_char *) OSSL_PROVIDER_get0_name(provider),
                      (u_char *) "qatprovider") == 0)
    {
        *seen = 1;
    }
}


#if (NGX_HTTP_SSL)
/* http{} preconfiguration hook. Runs before http{} parses SSL_CTX configurations,
 * ensuring qatprovider is preloaded regardless of nginx.conf block order or
 * when QAT config originates solely from openssl.cnf. */
static ngx_int_t
ngx_ssl_provider_qat_preconfiguration(ngx_conf_t *cf)
{
    if (ngx_ssl_provider_qat_preload(cf->log) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}
static ngx_http_module_t  ngx_ssl_provider_qat_http_module_ctx = {
    ngx_ssl_provider_qat_preconfiguration,  /* preconfiguration */
    NULL,                                   /* postconfiguration */

    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */

    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */

    NULL,                                   /* create location configuration */
    NULL                                    /* merge location configuration */
};


ngx_module_t  ngx_ssl_provider_qat_http_module = {
    NGX_MODULE_V1,
    &ngx_ssl_provider_qat_http_module_ctx,
    NULL,
    NGX_HTTP_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};
#endif


#if (NGX_STREAM_SSL)
/* stream{} preconfiguration hook. Mirrors the http{} hook above: without it, a
 * stream{}+stream_ssl_module build configured purely from openssl.cnf never
 * primes qatprovider before its first SSL_CTX is built. */
static ngx_int_t
ngx_ssl_provider_qat_stream_preconfiguration(ngx_conf_t *cf)
{
    if (ngx_ssl_provider_qat_preload(cf->log) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}
static ngx_stream_module_t  ngx_ssl_provider_qat_stream_module_ctx = {
    ngx_ssl_provider_qat_stream_preconfiguration,  /* preconfiguration */
    NULL,                                          /* postconfiguration */

    NULL,                                          /* create main configuration */
    NULL,                                          /* init main configuration */

    NULL,                                          /* create server configuration */
    NULL                                           /* merge server configuration */
};


ngx_module_t  ngx_ssl_provider_qat_stream_module = {
    NGX_MODULE_V1,
    &ngx_ssl_provider_qat_stream_module_ctx,
    NULL,
    NGX_STREAM_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};
#endif


/* Refresh in-flight request counters from qatprovider. */
static ngx_int_t
ngx_ssl_provider_qat_refresh_counters(void)
{
    /* Read into fresh stack locals and commit to the globals only on a
     * successful query, so a failed get_params keeps the last known-good
     * counters (preventing the drain from finishing early). */
    static const char *const  keys[] = {
        QAT_PROV_PARAM_NUM_ASYM_REQUESTS_IN_FLIGHT,
        QAT_PROV_PARAM_NUM_KDF_REQUESTS_IN_FLIGHT,
        QAT_PROV_PARAM_NUM_CIPHER_REQUESTS_IN_FLIGHT,
        QAT_PROV_PARAM_NUM_ASYM_MB_ITEMS_IN_QUEUE,
        QAT_PROV_PARAM_NUM_KDF_MB_ITEMS_IN_QUEUE,
        QAT_PROV_PARAM_NUM_SYM_MB_ITEMS_IN_QUEUE,
    };
    enum { NPARAMS = sizeof(keys) / sizeof(keys[0]) };

    OSSL_PARAM  params[NPARAMS + 1];
    int         values[NPARAMS] = { 0 };
    ngx_uint_t  i;

    if (qat_prov == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < NPARAMS; i++) {
        params[i] = OSSL_PARAM_construct_int((char *) keys[i], &values[i]);
    }
    params[NPARAMS] = OSSL_PARAM_construct_end();

    if (!OSSL_PROVIDER_get_params(qat_prov, params)) {
        return NGX_ERROR;
    }

    num_asym_requests_in_flight  = values[0];
    num_kdf_requests_in_flight   = values[1];
    num_cipher_requests_in_flight = values[2];
    num_asym_mb_items_in_queue   = values[3];
    num_kdf_mb_items_in_queue    = values[4];
    num_sym_mb_items_in_queue    = values[5];

    return NGX_OK;
}

static ngx_int_t
ngx_ssl_provider_qat_total_in_flight(void)
{
    return num_asym_requests_in_flight + num_kdf_requests_in_flight
         + num_cipher_requests_in_flight + num_asym_mb_items_in_queue
         + num_kdf_mb_items_in_queue + num_sym_mb_items_in_queue;
}


/* ===================================================================
 * Trigger a single polling cycle on qatprovider.
 * =================================================================== */
static void
ngx_ssl_provider_qat_poll(ngx_log_t *log)
{
    /* Use -1 to distinguish an unserviced poll from a reported failure. */
    int         poll_status = -1;
    OSSL_PARAM  params[2];

    if (qat_prov == NULL) {
        return;
    }

    /* No ngx_exiting gate: shutdown and exit drains are the only way an
     * external/heuristic completion can finish. */

    params[0] = OSSL_PARAM_construct_int(QAT_PROV_PARAM_POLL, &poll_status);
    params[1] = OSSL_PARAM_construct_end();

    /* Treat only query failure or an explicit zero status as an error. */
    if (!OSSL_PROVIDER_get_params(qat_prov, params) || poll_status == 0) {
        ngx_log_error(NGX_LOG_ALERT, log, 0,
                      "QAT provider: POLL failed (status=%d)", poll_status);
    } else if (poll_status < 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, log, 0,
                       "QAT provider: poll not serviced by provider "
                       "(provider-internal polling); nothing to drain");
    } else {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0,
                       "QAT provider: poll serviced, status=%d", poll_status);
    }
}


/* Poll provider completions from nginx's connection hot path. */
static void
ngx_ssl_provider_qat_hotpath_poll(ngx_log_t *log)
{
    if (qat_prov == NULL || qat_provider_status.finished) {
        return;
    }

    ngx_ssl_provider_qat_poll(log);
}


/* Poll provider health for SW fallback. */
static void
ngx_ssl_provider_qat_heartbeat_poll(ngx_log_t *log)
{
    int         hb_status = 0;
    OSSL_PARAM  params[2];

    if (qat_prov == NULL || qat_provider_status.finished) {
        return;
    }

    params[0] = OSSL_PARAM_construct_int(QAT_PROV_PARAM_HEARTBEAT_POLL,
                                         &hb_status);
    params[1] = OSSL_PARAM_construct_end();

    /* Report provider health failures and fallback events. */
    if (!OSSL_PROVIDER_get_params(qat_prov, params)) {
        ngx_log_error(NGX_LOG_ALERT, log, 0,
                      "QAT provider: HEARTBEAT_POLL failed");
    } else if (hb_status != 0) {
        ngx_log_error(NGX_LOG_WARN, log, 0,
                      "QAT provider: heartbeat reported a device-health event, "
                      "status=%d (SW fallback may engage)", hb_status);
    } else {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0,
                       "QAT provider: heartbeat poll done, status=%d",
                       hb_status);
    }
}



static ngx_command_t  ngx_qat_provider_commands[] = {
    { ngx_string("qat_provider"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_qat_provider_block,
      0, 0, NULL },
    ngx_null_command
};

static ngx_str_t ngx_ssl_provider_qat_module_name = ngx_string("qatprovider");

static ngx_ssl_provider_module_t  ngx_ssl_provider_qat_module_ctx = {
    &ngx_ssl_provider_qat_module_name,
    ngx_ssl_provider_qat_create_conf,
    ngx_ssl_provider_qat_init_conf
};

static ngx_core_module_t  ngx_qat_provider_module_ctx = {
    ngx_string("qat_provider"),
    NULL,
    ngx_qat_provider_init_conf
};

ngx_module_t  ngx_qat_provider_module = {
    NGX_MODULE_V1,
    &ngx_qat_provider_module_ctx,
    ngx_qat_provider_commands,
    NGX_CORE_MODULE,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};


static ngx_command_t  ngx_ssl_provider_qat_commands[] = {

    { ngx_string("qat_shutting_down_release"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_ssl_provider_qat_releasable,
      0, offsetof(ngx_ssl_provider_qat_conf_t, releasable), NULL },

    { ngx_string("qat_sw_fallback"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, enable_sw_fallback), NULL },

    { ngx_string("qat_offload_mode"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, offload_mode), NULL },

    { ngx_string("qat_notify_mode"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, notify_mode), NULL },

    { ngx_string("qat_poll_mode"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, poll_mode), NULL },

    { ngx_string("qat_poll_interval"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, poll_interval), NULL },

    { ngx_string("qat_hw_asym_threshold"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, qat_hw_asym_threshold),
      NULL },

    { ngx_string("qat_hw_sym_threshold"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, qat_hw_sym_threshold),
      NULL },

    { ngx_string("qat_sw_threshold"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, qat_sw_threshold),
      NULL },

    { ngx_string("qat_small_pkt_offload_threshold"),
      NGX_SSL_PROVIDER_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0, offsetof(ngx_ssl_provider_qat_conf_t, small_pkt_offload_threshold),
      NULL },

    ngx_null_command
};

ngx_module_t  ngx_ssl_provider_qat_module = {
    NGX_MODULE_V1,
    &ngx_ssl_provider_qat_module_ctx,
    ngx_ssl_provider_qat_commands,
    NGX_SSL_PROVIDER_MODULE_ID,
    NULL, ngx_ssl_provider_qat_init_module,
    ngx_ssl_provider_qat_process_init,
    NULL, NULL, ngx_ssl_provider_qat_process_exit, NULL,
    NGX_MODULE_V1_PADDING
};


static char *
ngx_qat_provider_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char           *rv;
    void         ***ctx;
    ngx_uint_t      i;
    ngx_uint_t      ngx_ssl_provider_max_module;
    ngx_conf_t      pcf;
    ngx_ssl_provider_module_t *m;

    if (*(void **) conf) {
        return "is duplicate";
    }

    ngx_ssl_provider_max_module = ngx_count_modules(cf->cycle,
                                                  NGX_SSL_PROVIDER_MODULE_ID);

    ctx = ngx_pcalloc(cf->pool, sizeof(void **));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cf->pool, ngx_ssl_provider_max_module * sizeof(void *));
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(void **) conf = ctx;

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_SSL_PROVIDER_MODULE_ID) {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->create_conf) {
            (*ctx)[cf->cycle->modules[i]->ctx_index] =
                                                     m->create_conf(cf->cycle);
            if ((*ctx)[cf->cycle->modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_SSL_PROVIDER_MODULE_ID;
    cf->cmd_type = NGX_SSL_PROVIDER_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_SSL_PROVIDER_MODULE_ID) {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;

        if (m->init_conf) {
            rv = m->init_conf(cf->cycle,
                              (*ctx)[cf->cycle->modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}


/* Synthesize a context only for an already cnf-activated provider. */
static char *
ngx_qat_provider_init_conf(ngx_cycle_t *cycle, void *conf)
{
    char                       *rv;
    void                     ***ctx;
    ngx_uint_t                  i;
    ngx_uint_t                  ngx_ssl_provider_max_module;
    ngx_ssl_provider_module_t  *m;

    if (conf != NULL) {
        return NGX_CONF_OK;
    }

    if (ngx_test_config || ngx_process == NGX_PROCESS_SIGNALLER
        || ngx_ssl_provider_qat_find() == NULL)
    {
        return NGX_CONF_OK;
    }

    ngx_ssl_provider_max_module = ngx_count_modules(cycle,
                                                  NGX_SSL_PROVIDER_MODULE_ID);

    ctx = ngx_pcalloc(cycle->pool, sizeof(void **));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cycle->pool, ngx_ssl_provider_max_module
                                    * sizeof(void *));
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->type != NGX_SSL_PROVIDER_MODULE_ID) {
            continue;
        }

        m = cycle->modules[i]->ctx;

        if (m->create_conf) {
            (*ctx)[cycle->modules[i]->ctx_index] = m->create_conf(cycle);
            if ((*ctx)[cycle->modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    cycle->conf_ctx[ngx_qat_provider_module.index] = ctx;

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->type != NGX_SSL_PROVIDER_MODULE_ID) {
            continue;
        }

        m = cycle->modules[i]->ctx;

        if (m->init_conf) {
            rv = m->init_conf(cycle, (*ctx)[cycle->modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                cycle->conf_ctx[ngx_qat_provider_module.index] = NULL;
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}


static void *
ngx_ssl_provider_qat_create_conf(ngx_cycle_t *cycle)
{
    ngx_ssl_provider_qat_conf_t *spqcf;

    spqcf = ngx_pcalloc(cycle->pool, sizeof(ngx_ssl_provider_qat_conf_t));
    if (spqcf == NULL) {
        return NULL;
    }

    qat_provider_enable_internal_polling = 0;
    qat_provider_enable_external_polling = 0;
    qat_provider_enable_heuristic_polling = 0;
    qat_provider_enable_heartbeat_polling = 0;

    spqcf->releasable = NGX_CONF_UNSET;
    spqcf->enable_sw_fallback = NGX_CONF_UNSET;
    spqcf->poll_interval = NGX_CONF_UNSET;
    spqcf->qat_hw_asym_threshold = NGX_CONF_UNSET;
    spqcf->qat_hw_sym_threshold = NGX_CONF_UNSET;
    spqcf->qat_sw_threshold = NGX_CONF_UNSET;

    return spqcf;
}


/* Validate the configured offload, notify, and polling modes. */
static ngx_int_t
ngx_ssl_provider_qat_check_mode_combo(ngx_log_t *log, const char *source,
    const char *offload, const char *notify, const char *poll)
{
    if (ngx_strcmp(offload, "sync") == 0) {

        if (ngx_strcmp(poll, "external") == 0
            || ngx_strcmp(poll, "heuristic") == 0)
        {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                "QAT provider: invalid mode combination from %s: "
                "sync + %s is not allowed - external/heuristic polling "
                "requires qat_offload_mode async", source, poll);
            return NGX_ERROR;
        }

    } else {
        /* async */

        if (ngx_strcmp(notify, "event") == 0
            && ngx_strcmp(poll, "external") == 0)
        {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                "QAT provider: invalid mode combination from %s: "
                "async + event + external is not supported", source);
            return NGX_ERROR;
        }

        if (ngx_strcmp(notify, "event") == 0
            && ngx_strcmp(poll, "heuristic") == 0)
        {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                "QAT provider: invalid mode combination from %s: "
                "async + event + heuristic is not allowed", source);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static char *
ngx_ssl_provider_qat_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_ssl_provider_qat_conf_t *spqcf = conf;
    const char                  *env;

    /* Runs before defaults are applied so the adopt path can report an
     * ignored block. */
    qat_provider_modes_from_nginx_conf =
        (spqcf->offload_mode.data != NULL)
        || (spqcf->notify_mode.data != NULL)
        || (spqcf->poll_mode.data != NULL)
        || (spqcf->poll_interval != NGX_CONF_UNSET);

    /* Snapshot OPENSSL_CONF in the master for the config-source log; nginx
     * clears the worker environment before set_params runs. */
    env = getenv("OPENSSL_CONF");
    if (env != NULL && *env != '\0') {
        ngx_cpystrn((u_char *) qat_provider_openssl_conf_path,
                    (u_char *) env, NGX_MAX_PATH);
    } else {
        qat_provider_openssl_conf_path[0] = '\0';
    }

    ngx_conf_init_str_value(spqcf->offload_mode, "async");
    ngx_conf_init_str_value(spqcf->notify_mode, "poll");
    ngx_conf_init_str_value(spqcf->poll_mode, "internal");
    ngx_conf_init_value(spqcf->releasable, 0);
    ngx_conf_init_value(spqcf->qat_hw_asym_threshold,
                        QAT_HW_ASYM_DEFAULT_THRESHOLD);
    ngx_conf_init_value(spqcf->qat_hw_sym_threshold,
                        QAT_HW_SYM_DEFAULT_THRESHOLD);
    ngx_conf_init_value(spqcf->qat_sw_threshold,
                        QAT_SW_DEFAULT_THRESHOLD);

    /* Configuration validation */
    if (ngx_strcmp(spqcf->offload_mode.data, "sync") != 0
        && ngx_strcmp(spqcf->offload_mode.data, "async") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_offload_mode");
        return NGX_CONF_ERROR;
    }

    /* nginx never wires QAT completion fds into its event loop, so reject
     * event-driven polling here. */
    if (ngx_strcmp(spqcf->notify_mode.data, "event") == 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "qat_notify_mode event (event-driven polling) is not "
                      "supported by nginx; use qat_notify_mode poll");
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(spqcf->notify_mode.data, "poll") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_notify_mode");
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(spqcf->poll_mode.data, "internal") != 0
        && ngx_strcmp(spqcf->poll_mode.data, "external") != 0
        && ngx_strcmp(spqcf->poll_mode.data, "heuristic") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_poll_mode");
        return NGX_CONF_ERROR;
    }

    /* Reject invalid mode combinations up front. The same check also runs on
     * the openssl.cnf adopt path in set_params. */
    if (ngx_ssl_provider_qat_check_mode_combo(cycle->log, "nginx.conf",
            (char *) spqcf->offload_mode.data,
            (char *) spqcf->notify_mode.data,
            (char *) spqcf->poll_mode.data) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    /* qat_shutting_down_release requires external or heuristic polling. */
    if (spqcf->releasable) {
        if (ngx_strcmp(spqcf->poll_mode.data, "external") != 0
            && ngx_strcmp(spqcf->poll_mode.data, "heuristic") != 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT is releasable only when external or heuristic "
                          "polling mode is set");
            return NGX_CONF_ERROR;
        }

        /* QAT release during worker shutdown is unsupported when CIPHERS are
         * offloaded. qatprovider offloads every algorithm (CIPHERS included),
         * so release is always unsupported here. */
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "QAT is unreleasable during worker shutting down "
                      "due to CIPHERS being offloaded");
        spqcf->releasable = 0;
    }

    /* Set polling mode flags */
    if (ngx_strcmp(spqcf->notify_mode.data, "poll") == 0
        && ngx_strcmp(spqcf->poll_mode.data, "internal") == 0) {
        qat_provider_enable_internal_polling = 1;
    }

    if (ngx_strcmp(spqcf->notify_mode.data, "poll") == 0
        && ngx_strcmp(spqcf->poll_mode.data, "external") == 0) {
        qat_provider_enable_external_polling = 1;
        ngx_conf_init_value(spqcf->poll_interval,
                            EXTERNAL_POLL_DEFAULT_INTERVAL);
    }

    if (ngx_strcmp(spqcf->notify_mode.data, "poll") == 0
        && ngx_strcmp(spqcf->poll_mode.data, "heuristic") == 0) {
        qat_provider_enable_heuristic_polling = 1;
        ngx_conf_init_value(spqcf->poll_interval,
                            HEURISTIC_POLL_DEFAULT_INTERVAL);
    }

    /* Validate qat_poll_interval against the active poll_mode (internal:
     * microseconds 1..1e7; external/heuristic: milliseconds 1..1000). */
    if (spqcf->poll_interval != NGX_CONF_UNSET) {
        if (ngx_strcmp(spqcf->poll_mode.data, "internal") == 0) {
            if (spqcf->poll_interval < 1
                || spqcf->poll_interval > 10000000) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "qat_poll_interval out of range for internal "
                              "polling (1..10000000 microseconds)");
                return NGX_CONF_ERROR;
            }
        } else {
            /* external or heuristic: nginx timer, milliseconds */
            if (spqcf->poll_interval < 1
                || spqcf->poll_interval > 1000) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "qat_poll_interval out of range for external/"
                              "heuristic polling (1..1000 milliseconds)");
                return NGX_CONF_ERROR;
            }
        }
    }

    /* Thresholds must be 1..512: 0 polls every tick and a larger value never
     * triggers a batched drain. */
    if (spqcf->qat_hw_asym_threshold < 1 || spqcf->qat_hw_asym_threshold > 512
        || spqcf->qat_hw_sym_threshold < 1 || spqcf->qat_hw_sym_threshold > 512
        || spqcf->qat_sw_threshold < 1
        || spqcf->qat_sw_threshold > 512)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "invalid heuristic poll threshold "
                      "(qat_hw_asym/qat_hw_sym/qat_sw_threshold "
                      "must be 1..512)");
        return NGX_CONF_ERROR;
    }

    /* Switching into internal polling on a live reload is rejected; the
     * committed mode is kept. */
    {
        ngx_int_t  requested_mode = 0;

        if (qat_provider_enable_internal_polling) {
            requested_mode = INTERNAL_POLL;
        } else if (qat_provider_enable_external_polling) {
            requested_mode = EXTERNAL_POLL;
        } else if (qat_provider_enable_heuristic_polling) {
            requested_mode = HEURISTIC_POLL;
        }

        if (requested_mode == INTERNAL_POLL
            && (qat_provider_init_polling_mode == EXTERNAL_POLL
                || qat_provider_init_polling_mode == HEURISTIC_POLL))
        {
            qat_provider_enable_internal_polling = 0;

            switch (qat_provider_init_polling_mode) {
            case EXTERNAL_POLL:
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "Switch from external to internal polling is "
                              "invalid, and still use external polling");
                qat_provider_enable_external_polling = 1;
                spqcf->poll_interval = EXTERNAL_POLL_DEFAULT_INTERVAL;
                break;

            case HEURISTIC_POLL:
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "Switch from heuristic to internal polling is "
                              "invalid, and still use heuristic polling");
                qat_provider_enable_heuristic_polling = 1;
                spqcf->poll_interval = HEURISTIC_POLL_DEFAULT_INTERVAL;
                break;
            }

            requested_mode = qat_provider_init_polling_mode;
        }

        qat_provider_init_polling_mode = requested_mode;
    }

    qat_provider_poll_interval = spqcf->poll_interval;

    /* Commit the heuristic-poll thresholds for the hot path. */
    qat_provider_qat_hw_asym_threshold = spqcf->qat_hw_asym_threshold;
    qat_provider_qat_hw_sym_threshold  = spqcf->qat_hw_sym_threshold;
    qat_provider_qat_sw_threshold = spqcf->qat_sw_threshold;

    /* Exactly one polling mode must be active. Normally guaranteed (flags reset
     * in create_conf, one branch above sets one); assert it to catch a future
     * regression early. */
    {
        ngx_uint_t  modes = qat_provider_enable_internal_polling
                          + qat_provider_enable_external_polling
                          + qat_provider_enable_heuristic_polling;
        if (modes != 1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "exactly one QAT polling mode must be enabled "
                          "(internal/external/heuristic)");
            return NGX_CONF_ERROR;
        }
    }

    /* Preload qatprovider during qat_provider{} block parse to set default
     * properties before http{} builds SSL_CTX, enabling symmetric offload
     * on initial start when configured from nginx.conf. */
    if (ngx_ssl_provider_qat_preload(cycle->log) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_ssl_provider_qat_init(ngx_cycle_t *cycle)
{
    ngx_memset(&qat_provider_status, 0, sizeof(qat_provider_status));
    num_heuristic_poll = 0;
    qat_provider_shutdown_ticks = 0;
    return NGX_OK;
}

static ngx_int_t
ngx_ssl_provider_qat_set_params(ngx_cycle_t *cycle)
{
    ngx_ssl_provider_qat_conf_t  *spqcf;
    OSSL_PARAM                    params[MAX_PROVIDER_PARAMS];
    int                           idx = 0;
    int                           enable_ext = 0, enable_heur = 0;
    int                           enable_swfb = 0;
    int                           push_swfb = 0;
    int                           enable_init = 1;
    int                           poll_interval = 0;
    ngx_int_t                     adopted = 0;
    /* Polling config read back from qatprovider in adopt mode, shared by the
     * read-back and alignment steps below. */
    int                           q_ext = 0, q_heur = 0;
    int                           q_swfb = 0, q_interval = 0;
    /* Heuristic-poll thresholds read back from qatprovider in adopt mode. */
    int                           q_hw_asym = 0, q_hw_sym = 0;
    int                           q_sw = 0;

    spqcf = ngx_provider_cycle_get_conf(cycle, ngx_ssl_provider_qat_module);
    if (spqcf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "conf of provider_qat_module is null");
        return NGX_ERROR;
    }

    /* Mirror the graceful-release flag for the poll handlers' shutdown path. */
    qat_provider_releasable = (spqcf->releasable == 1) ? 1 : 0;

    /* Resolve the qatprovider. Preferred path: it was already activated
     * through openssl.cnf and we just locate the live handle. */
    qat_prov = ngx_ssl_provider_qat_find();

    /* Keep OpenSSL's default provider available alongside qatprovider. */
    if (!OSSL_PROVIDER_available(NULL, "default")) {
        default_prov_self_loaded = OSSL_PROVIDER_load(NULL, "default");
        if (default_prov_self_loaded == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT provider: failed to load the OpenSSL default "
                          "provider; non-offloaded algorithms would be "
                          "unavailable");
            return NGX_ERROR;
        }
    }

    if (qat_prov == NULL) {
        /* Load qatprovider when openssl.cnf did not activate it. */
        qat_prov = OSSL_PROVIDER_load(NULL, "qatprovider");
        if (qat_prov == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT provider: qatprovider not found and could not "
                          "be loaded; ensure qatprovider.so is in the OpenSSL "
                          "modules directory");
            return NGX_ERROR;
        }

        qat_prov_self_loaded = qat_prov;

        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                      "QAT provider: loaded qatprovider directly "
                      "(not configured via openssl.cnf)");

    } else {
        /* Located the cnf-activated handle. The do_all() callback only borrows
         * the pointer, so take our own counted reference (load on an active
         * provider just bumps its refcount) and release it at worker exit. */
        qat_prov = OSSL_PROVIDER_load(NULL, "qatprovider");
        if (qat_prov == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT provider: failed to reference qatprovider");
            return NGX_ERROR;
        }

        qat_prov_self_loaded = qat_prov;
    }

    /* Query whether qatprovider took its polling config from openssl.cnf,
     * which wins over nginx.conf. */
    {
        OSSL_PARAM  sentinel[2];
        int         configured_from_cnf = 0;

        sentinel[0] = OSSL_PARAM_construct_int(
                          QAT_PROV_PARAM_CONFIGURED_FROM_OPENSSL_CNF,
                          &configured_from_cnf);
        sentinel[1] = OSSL_PARAM_construct_end();

        if (OSSL_PROVIDER_get_params(qat_prov, sentinel)) {
            adopted = configured_from_cnf ? 1 : 0;

        } else {
            /* Non-fatal: a provider lacking this param defaults to configure
             * mode. Warn so an openssl.cnf-configured provider being overridden
             * is not silent. */
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                "QAT provider: configured-from-openssl.cnf sentinel query "
                "failed; assuming nginx.conf (configure) mode - if qatprovider "
                "was configured from openssl.cnf its settings may be "
                "overridden by the nginx.conf qat_provider block");
        }
    }

    /* Log the effective configuration source so the operator can see which
     * file drives the QAT provider for this worker - important when both
     * openssl.cnf and the nginx.conf qat_provider block are present. */
    {
        const char *cnf = qat_provider_openssl_conf_path;
        ngx_uint_t  have_cnf = (cnf != NULL && *cnf != '\0');
        const char *effective = adopted
            ? (have_cnf ? cnf : "openssl.cnf (OpenSSL default location)")
            : "nginx.conf (qat_provider block)";

        if (adopted) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                "QAT provider: configuration source = openssl.cnf [%s]",
                have_cnf ? cnf : "OpenSSL default location");
        } else {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                "QAT provider: configuration source = nginx.conf "
                "(qat_provider block)");
        }

        /* Compulsory debug trace naming the exact file in use. Emitted from the
         * worker, where the configured error_log (incl. debug) is active; the
         * master parse-time log level is not yet applied in init_conf. */
        ngx_log_debug3(NGX_LOG_DEBUG_CORE, cycle->log, 0,
            "QAT provider: file-in-use trace - effective = %s; "
            "OPENSSL_CONF candidate = %s; nginx.conf qat mode directives = %s",
            effective,
            have_cnf ? cnf : "<unset - default location>",
            qat_provider_modes_from_nginx_conf ? "present" : "absent");

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
            "QAT provider: effective config file = %s, overridden file = %s",
            effective,
            adopted ? "nginx.conf qat_provider block"
                    : "none (openssl.cnf did not configure qatprovider)");

        /* effective is consumed only by the ngx_log_debug* traces above, which
         * compile to nothing in non-debug builds; keep it referenced so the
         * no-debug build does not trip -Werror=unused-variable. */
        (void) effective;
    }

    if (adopted) {
        /* Read the active polling config back from qatprovider; the enum sizes
         * the array so adding or removing a param cannot overflow it. */
        enum {
            QAT_ADOPT_EXT, QAT_ADOPT_HEUR, QAT_ADOPT_SWFB,
            QAT_ADOPT_INTERVAL, QAT_ADOPT_HW_ASYM, QAT_ADOPT_HW_SYM,
            QAT_ADOPT_SW, QAT_ADOPT_NPARAMS
        };
        OSSL_PARAM  q[QAT_ADOPT_NPARAMS + 1];

        q[QAT_ADOPT_EXT] = OSSL_PARAM_construct_int(
                   QAT_PROV_PARAM_ENABLE_EXTERNAL_POLLING, &q_ext);
        q[QAT_ADOPT_HEUR] = OSSL_PARAM_construct_int(
                   QAT_PROV_PARAM_ENABLE_HEURISTIC_POLLING, &q_heur);
        q[QAT_ADOPT_SWFB] = OSSL_PARAM_construct_int(
                   QAT_PROV_PARAM_ENABLE_SW_FALLBACK, &q_swfb);
        q[QAT_ADOPT_INTERVAL] = OSSL_PARAM_construct_int(
                   QAT_PROV_PARAM_INTERNAL_POLL_INTERVAL, &q_interval);
        q[QAT_ADOPT_HW_ASYM] = OSSL_PARAM_construct_int(
                   QAT_PROV_PARAM_HW_ASYM_THRESHOLD, &q_hw_asym);
        q[QAT_ADOPT_HW_SYM] = OSSL_PARAM_construct_int(
                   QAT_PROV_PARAM_HW_SYM_THRESHOLD, &q_hw_sym);
        q[QAT_ADOPT_SW] = OSSL_PARAM_construct_int(
                   QAT_PROV_PARAM_SW_THRESHOLD, &q_sw);
        q[QAT_ADOPT_NPARAMS] = OSSL_PARAM_construct_end();

        /* The integers start at 0, so qatprovider does not override its own
         * state on this query (it only adopts a written value when non-zero)
         * and just writes its current values back. */
        if (!OSSL_PROVIDER_get_params(qat_prov, q)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT provider: OSSL_PROVIDER_get_params (adopt) "
                          "failed");
            return NGX_ERROR;
        }

        /* In adopt mode, openssl.cnf is authoritative for all polling modes,
         * including internal polling. Retain adopt mode so openssl.cnf overrides
         * any nginx.conf block settings. */
        if (!q_ext && !q_heur) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                "QAT provider: openssl.cnf pinned internal polling; adopting it "
                "over the nginx.conf qat_provider block (openssl.cnf is "
                "authoritative for all polling modes)");
        }
    }

    if (adopted) {
        /* Align the polling globals to the adopted config; qatprovider reports
         * heuristic as a refinement of external polling. Values openssl.cnf
         * cannot express fall back to the module defaults. */
        if (q_heur) {
            qat_provider_enable_heuristic_polling = 1;
            qat_provider_enable_external_polling = 0;
            qat_provider_enable_internal_polling = 0;
            qat_provider_poll_interval = HEURISTIC_POLL_DEFAULT_INTERVAL;

        } else if (q_ext) {
            qat_provider_enable_external_polling = 1;
            qat_provider_enable_heuristic_polling = 0;
            qat_provider_enable_internal_polling = 0;
            qat_provider_poll_interval = EXTERNAL_POLL_DEFAULT_INTERVAL;

        } else {
            qat_provider_enable_internal_polling = 1;
            qat_provider_enable_external_polling = 0;
            qat_provider_enable_heuristic_polling = 0;
            qat_provider_poll_interval =
                (q_interval > 0) ? (ngx_int_t) q_interval : NGX_CONF_UNSET;
        }

        /* Record the committed mode so a later live reload that tries to switch
         * into internal polling is rejected, as in configure mode. */
        qat_provider_init_polling_mode =
            qat_provider_enable_internal_polling ? INTERNAL_POLL :
            qat_provider_enable_external_polling ? EXTERNAL_POLL :
                                                   HEURISTIC_POLL;

        /* qatprovider does not expose the event-driven setting, so notify_mode
         * is always poll in adopt mode. */
        ngx_str_set(&spqcf->notify_mode, "poll");
        spqcf->enable_sw_fallback = q_swfb ? 1 : 0;

        /* Apply an in-range echo from openssl.cnf, otherwise the module
         * default. */
        qat_provider_qat_hw_asym_threshold =
            (q_hw_asym >= 1 && q_hw_asym <= 512)
                ? (ngx_int_t) q_hw_asym : QAT_HW_ASYM_DEFAULT_THRESHOLD;
        qat_provider_qat_hw_sym_threshold =
            (q_hw_sym >= 1 && q_hw_sym <= 512)
                ? (ngx_int_t) q_hw_sym : QAT_HW_SYM_DEFAULT_THRESHOLD;
        qat_provider_qat_sw_threshold =
            (q_sw >= 1 && q_sw <= 512)
                ? (ngx_int_t) q_sw : QAT_SW_DEFAULT_THRESHOLD;

        /* Validate the adopted combination; offload_mode cannot be read back
         * and every supported poll mode requires async. */
        {
            const char *adopted_poll =
                qat_provider_enable_internal_polling ? "internal" :
                qat_provider_enable_external_polling ? "external" :
                                                       "heuristic";
            const char *adopted_offload = "async";

            if (ngx_ssl_provider_qat_check_mode_combo(cycle->log,
                    "openssl.cnf", adopted_offload,
                    (char *) spqcf->notify_mode.data,
                    adopted_poll) != NGX_OK)
            {
                return NGX_ERROR;
            }
        }

        /* openssl.cnf is authoritative, so warn once when the nginx.conf block
         * is populated and therefore ignored. */
        if (qat_provider_modes_from_nginx_conf
            || (spqcf->small_pkt_offload_threshold.data != NULL
                && spqcf->small_pkt_offload_threshold.len > 0)) {
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                "QAT provider: qatprovider is configured from openssl.cnf; the "
                "entire nginx.conf qat_provider block (qat_offload_mode, "
                "qat_notify_mode, qat_poll_mode, qat_poll_interval, "
                "qat_hw_asym/qat_hw_sym/qat_sw_threshold, "
                "qat_small_pkt_offload_threshold) is ignored. openssl.cnf is "
                "the single source of truth; parameters it cannot express use "
                "the module's built-in defaults");
        }

        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                      "QAT provider: adopted configuration from openssl.cnf "
                      "(external=%d, heuristic=%d, internal=%d, "
                      "sw_fallback=%d)",
                      qat_provider_enable_external_polling,
                      qat_provider_enable_heuristic_polling,
                      qat_provider_enable_internal_polling,
                      q_swfb ? 1 : 0);
    }

    /* Push the resolved polling config to qatprovider and trigger HW init. The
     * event-driven flag is never pushed since notify_mode is always poll. */

    /* Report the effective thresholds and which file supplied them. */
    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
        "QAT provider: effective heuristic thresholds "
        "(hw_asym=%i hw_sym=%i sw=%i) source=%s",
        qat_provider_qat_hw_asym_threshold,
        qat_provider_qat_hw_sym_threshold,
        qat_provider_qat_sw_threshold,
        adopted ? "openssl.cnf" : "nginx.conf");

    /* qatprovider treats heuristic as a special case of external polling. */
    if (qat_provider_enable_external_polling
        || qat_provider_enable_heuristic_polling) {
        enable_ext = 1;
    }

    if (qat_provider_enable_heuristic_polling) {
        enable_heur = 1;
    }

    /* Push SW fallback only when nginx.conf configured it, so an unset value
     * does not wipe one applied by openssl.cnf. */
    if (spqcf->enable_sw_fallback != NGX_CONF_UNSET) {
        enable_swfb = spqcf->enable_sw_fallback ? 1 : 0;
        push_swfb = 1;
    }

    if (qat_provider_enable_internal_polling
        && qat_provider_poll_interval != NGX_CONF_UNSET) {
        poll_interval = (int) qat_provider_poll_interval;
    }

    params[idx++] = OSSL_PARAM_construct_int(
        QAT_PROV_PARAM_ENABLE_EXTERNAL_POLLING, &enable_ext);
    params[idx++] = OSSL_PARAM_construct_int(
        QAT_PROV_PARAM_ENABLE_HEURISTIC_POLLING, &enable_heur);
    if (push_swfb) {
        params[idx++] = OSSL_PARAM_construct_int(
            QAT_PROV_PARAM_ENABLE_SW_FALLBACK, &enable_swfb);
    }

    if (poll_interval > 0) {
        params[idx++] = OSSL_PARAM_construct_int(
            QAT_PROV_PARAM_INTERNAL_POLL_INTERVAL, &poll_interval);
    }

    /* Pushed as a UTF8 string, and only in configure mode so an adopted
     * openssl.cnf value is not overridden. */
    if (!adopted
        && spqcf->small_pkt_offload_threshold.data != NULL
        && spqcf->small_pkt_offload_threshold.len > 0) {
        params[idx++] = OSSL_PARAM_construct_utf8_string(
            QAT_PROV_PARAM_SMALL_PKT_OFFLOAD_THRESHOLD,
            (char *) spqcf->small_pkt_offload_threshold.data,
            spqcf->small_pkt_offload_threshold.len);
    }

    /* Trigger deferred HW init in the same push so QAT is up before any
     * handshake and the armed timer can drain completions. */
    params[idx++] = OSSL_PARAM_construct_int(
        QAT_PROV_PARAM_INIT_PROVIDER, &enable_init);

    params[idx++] = OSSL_PARAM_construct_end();

    if (!OSSL_PROVIDER_get_params(qat_prov, params)) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "QAT provider: OSSL_PROVIDER_get_params (set_params) "
                      "failed");
        return NGX_ERROR;
    }

    /* Enable heartbeat polling for SW-fallback health detection: only when
     * qat_sw_fallback is on with external or heuristic polling. */
    if ((qat_provider_enable_external_polling
         || qat_provider_enable_heuristic_polling)
        && enable_swfb) {
        qat_provider_enable_heartbeat_polling = 1;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                  "QAT provider: params set (external=%d, heuristic=%d, "
                  "sw_fallback=%d, heartbeat=%d, "
                  "interval=%d)",
                  enable_ext, enable_heur,
                  enable_swfb, qat_provider_enable_heartbeat_polling,
                  poll_interval);

    /* Prefer qatprovider for all implemented algorithms to route symmetric
     * ciphers to QAT HW, falling back to default provider for unexported
     * algorithms ('?' prefix). */
    if (!EVP_set_default_properties(NULL, QAT_PROV_DEFAULT_PROPERTY_QUERY)) {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                      "QAT provider: failed to set default property query \"%s\";"
                      " symmetric offload (AES-GCM/CCM/SM4) would not reach "
                      "QAT HW", QAT_PROV_DEFAULT_PROPERTY_QUERY);
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                  "QAT provider: default property query set to \"%s\" "
                  "(qatprovider preferred, default provider fallback)",
                  QAT_PROV_DEFAULT_PROPERTY_QUERY);

    return NGX_OK;
}

static ngx_int_t
ngx_ssl_provider_qat_heuristic_poll(ngx_log_t *log)
{
    ngx_int_t  total;
    ngx_int_t  threshold;

    if (qat_provider_status.finished) {
        return 0;
    }

    /* A failed counter read cannot be trusted, so poll once and report a
     * non-zero count to keep the fast cadence. */
    if (ngx_ssl_provider_qat_refresh_counters() != NGX_OK) {
        ngx_ssl_provider_qat_poll(log);
        total = ngx_ssl_provider_qat_total_in_flight();
        return total > 0 ? total : 1;
    }

    total = ngx_ssl_provider_qat_total_in_flight();
    if (total <= 0) {
        num_heuristic_poll = 0;
        return total;
    }

    /* Poll only once a worthwhile batch has accumulated. The threshold follows
     * the in-flight mix, and a smaller batch is deferred so it can grow. */
    if (num_asym_requests_in_flight > 0) {
        threshold = qat_provider_qat_hw_asym_threshold;
    } else if (num_asym_mb_items_in_queue > 0
               || num_sym_mb_items_in_queue > 0) {
        /* Both QAT_SW multibuff queues (asym and sym) share the single
         * qat_sw_threshold knob. */
        threshold = qat_provider_qat_sw_threshold;
    } else {
        threshold = qat_provider_qat_hw_sym_threshold;
    }

    if (total >= threshold) {
        ngx_ssl_provider_qat_poll(log);
        num_heuristic_poll = 0;
        return total;
    }

    /* Batch still below threshold: defer to let it grow, bounded so a small
     * tail that never reaches the threshold is still flushed within a few
     * ticks. */
    if (++num_heuristic_poll >= QAT_PROVIDER_HEURISTIC_MAX_DEFER) {
        ngx_ssl_provider_qat_poll(log);
        num_heuristic_poll = 0;
    }

    return total;
}


/* Poll provider completions using heuristic batching. */
static void
ngx_ssl_provider_qat_hotpath_poll_heuristic(ngx_log_t *log)
{
    if (qat_prov == NULL || qat_provider_status.finished) {
        return;
    }

    (void) ngx_ssl_provider_qat_heuristic_poll(log);
}


/* ===================================================================
 * Event-loop polling handlers.
 * =================================================================== */

/* Drive final polling and release work while the worker exits. */
static void
ngx_ssl_provider_qat_shutdown_tick(ngx_event_t *ev)
{
    if (qat_provider_releasable) {
        /* The release scan only reads counters, so poll every tick to make the
         * drain progress toward finished. */
        ngx_ssl_provider_qat_poll(ev->log);

        (void) ngx_ssl_provider_qat_release((ngx_cycle_t *) ngx_cycle);

        if (!qat_provider_status.finished) {
            if (++qat_provider_shutdown_ticks >= QAT_PROVIDER_SHUTDOWN_DRAIN_MAX) {
                /* Force-finish so worker exit can proceed; residual requests
                 * are abandoned, hence the ALERT. */
                ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                    "QAT provider: graceful release did not complete after "
                    "%ui poll ticks; forcing shutdown and abandoning any "
                    "in-flight QAT requests",
                    (ngx_uint_t) QAT_PROVIDER_SHUTDOWN_DRAIN_MAX);
                qat_provider_status.finished = 1;
                return;
            }
            ngx_add_timer(ev, HEURISTIC_POLL_DEFAULT_INTERVAL);
        }
        return;
    }

    ngx_ssl_provider_qat_poll(ev->log);
}

static void
ngx_ssl_provider_qat_external_handler(ngx_event_t *ev)
{
    if (qat_provider_status.finished) {
        return;
    }

    if (ngx_exiting) {
        ngx_ssl_provider_qat_shutdown_tick(ev);
        return;
    }

    /* Poll unconditionally: the call is a cheap no-op when nothing is
     * outstanding, and the in-flight snapshot can read zero while a request is
     * still on the device. */
    ngx_ssl_provider_qat_poll(ev->log);

    ngx_add_timer(ev, qat_provider_poll_interval);
}

static void
ngx_ssl_provider_qat_heuristic_handler(ngx_event_t *ev)
{
    if (qat_provider_status.finished) {
        return;
    }

    if (ngx_exiting) {
        ngx_ssl_provider_qat_shutdown_tick(ev);
        return;
    }

    /* With hot-path polling registered this timer is only a drain backstop for
     * completions that arrive without a following connection event. */
    (void) ngx_ssl_provider_qat_heuristic_poll(ev->log);

    /* No idle backoff: the backstop must not throttle draining under sustained
     * load, and the poll is a cheap no-op when the worker is idle. */
    ngx_add_timer(ev, qat_provider_poll_interval);
}

static void
ngx_ssl_provider_qat_heartbeat_handler(ngx_event_t *ev)
{
    if (qat_provider_status.finished) {
        return;
    }

    ngx_ssl_provider_qat_heartbeat_poll(ev->log);

    if (!ngx_exiting) {
        ngx_add_timer(ev, HEARTBEAT_POLL_TIMEOUT);
    }
}

static ngx_int_t
ngx_ssl_provider_qat_register_handler(ngx_cycle_t *cycle)
{
    qat_provider_timer_conn.fd = (ngx_socket_t) -1;

    /* cancelable = 0 on every QAT poll timer below: they must survive
     * ngx_event_cancel_timers() during graceful shutdown so the device keeps
     * being polled until all in-flight QAT requests have drained. */
    if (qat_provider_enable_external_polling) {
        ngx_memzero(&qat_provider_external_poll_event, sizeof(ngx_event_t));
        qat_provider_external_poll_event.data = &qat_provider_timer_conn;
        qat_provider_external_poll_event.handler =
            ngx_ssl_provider_qat_external_handler;
        qat_provider_external_poll_event.log = cycle->log;
        qat_provider_external_poll_event.cancelable = 0;
        ngx_add_timer(&qat_provider_external_poll_event,
                      qat_provider_poll_interval);

        /* External drains unconditionally on the hot path; the timer above
         * stays as a backstop. External and heuristic are mutually exclusive. */
        ngx_ssl_provider_hotpath_poll = ngx_ssl_provider_qat_hotpath_poll;
        ngx_ssl_provider_enable_hotpath_polling = 1;
    }

    if (qat_provider_enable_heuristic_polling) {
        ngx_memzero(&qat_provider_heuristic_poll_event, sizeof(ngx_event_t));
        qat_provider_heuristic_poll_event.data = &qat_provider_timer_conn;
        qat_provider_heuristic_poll_event.handler =
            ngx_ssl_provider_qat_heuristic_handler;
        qat_provider_heuristic_poll_event.log = cycle->log;
        qat_provider_heuristic_poll_event.cancelable = 0;

        /* The periodic timer is the drain backstop for heuristic polling. Arm
         * the first tick from the configured qat_poll_interval so operators can
         * tune the fast tick. */
        ngx_add_timer(&qat_provider_heuristic_poll_event,
                      qat_provider_poll_interval);

        /* Heuristic uses the batched hot-path poll, so it polls less often than
         * external. Registered per worker once the provider is initialized. */
        ngx_ssl_provider_hotpath_poll = ngx_ssl_provider_qat_hotpath_poll_heuristic;
        ngx_ssl_provider_enable_hotpath_polling = 1;
    }

    /* Arm the heartbeat poll timer (1s); enabled only when qat_sw_fallback is
     * on with external/heuristic polling. */
    if (qat_provider_enable_heartbeat_polling) {
        ngx_memzero(&qat_provider_heartbeat_poll_event, sizeof(ngx_event_t));
        qat_provider_heartbeat_poll_event.data = &qat_provider_timer_conn;
        qat_provider_heartbeat_poll_event.handler =
            ngx_ssl_provider_qat_heartbeat_handler;
        qat_provider_heartbeat_poll_event.log = cycle->log;
        qat_provider_heartbeat_poll_event.cancelable = 0;
        ngx_add_timer(&qat_provider_heartbeat_poll_event,
                      HEARTBEAT_POLL_TIMEOUT);
    }

    return NGX_OK;
}

static ngx_int_t
ngx_ssl_provider_qat_release(ngx_cycle_t *cycle)
{
    ngx_uint_t                    i;
    ngx_connection_t             *c;
    ngx_ssl_provider_qat_conf_t  *spqcf;

    spqcf = ngx_provider_cycle_get_conf(cycle, ngx_ssl_provider_qat_module);
    if (spqcf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "conf of provider_qat_module is null");
        return NGX_ERROR;
    }

    if (!spqcf->releasable || qat_provider_status.finished) {
        return NGX_OK;
    }

    /* Resume the scan from the checkpoint and defer the drain while a
     * handshake might still submit new QAT ops. The in-flight counter checked
     * below is the authoritative completion gate. */
    c = cycle->connections;

    /* Default to not-busy before scanning. If the scan starts past the end of
     * the connection table (checkpoint >= connection_n) the loop body never
     * runs, and busy must not retain a stale value from a previous tick. */
    qat_provider_status.busy = 0;

    for (i = qat_provider_status.checkpoint; i < cycle->connection_n; i++) {
        if (c[i].fd == (ngx_socket_t) -1) {
            continue;
        }

        if ((c[i].ssl && !c[i].ssl->handshaked)
            || (!c[i].ssl && c[i].ssl_enabled))
        {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                          "QAT provider: connections in SSL handshake phase");
            qat_provider_status.checkpoint = i;
            qat_provider_status.busy = 1;
            break;
        }
    }

    if (!qat_provider_status.busy) {
        /* Only conclude the drain finished on a successful counter refresh; a
         * failed provider query must not be mistaken for zero in-flight. */
        if (ngx_ssl_provider_qat_refresh_counters() == NGX_OK
            && ngx_ssl_provider_qat_total_in_flight() == 0)
        {
            qat_provider_status.finished = 1;
            ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                           "QAT provider finished");
        } else {
            ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                           "QAT provider has in-flight requests, deferring "
                           "release");
        }
    }

    return NGX_OK;
}


static char *
ngx_ssl_provider_qat_releasable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    /* Cross-directive validation (releasable vs. poll_mode) happens in
     * ngx_ssl_provider_qat_init_conf, independent of directive order. */
    return ngx_conf_set_flag_slot(cf, cmd, conf);
}


static ngx_int_t
ngx_ssl_provider_qat_init_module(ngx_cycle_t *cycle)
{
    ngx_ssl_provider_qat_conf_t  *spqcf;

    spqcf = ngx_provider_cycle_get_conf(cycle, ngx_ssl_provider_qat_module);
    if (spqcf == NULL) {
        /* No qat_provider {} block configured. */
        return NGX_OK;
    }

    if (ngx_test_config || ngx_process == NGX_PROCESS_SIGNALLER) {
        return NGX_OK;
    }

    /* Bring QAT up in the master before workers fork, so qatengine's post-fork
     * child handler re-inits QAT and spawns its polling thread before nginx
     * pins the worker, letting it float across all cores like the engine. */
    if (ngx_ssl_provider_qat_init(cycle) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_ssl_provider_qat_set_params(cycle) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_ssl_provider_qat_process_init(ngx_cycle_t *cycle)
{
    ngx_ssl_provider_qat_conf_t  *spqcf;

    spqcf = ngx_provider_cycle_get_conf(cycle, ngx_ssl_provider_qat_module);
    if (spqcf == NULL) {
        /* No qat_provider {} block configured for this worker. */
        return NGX_OK;
    }

    /* Worker init drives the lifecycle directly: reset state, push config,
     * prime counters and arm the handlers. QAT is already up from the master,
     * so the INIT_PROVIDER push is an idempotent no-op. */
    if (ngx_ssl_provider_qat_init(cycle) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_ssl_provider_qat_set_params(cycle) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_ssl_provider_qat_refresh_counters();

    if (ngx_ssl_provider_qat_register_handler(cycle) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_ssl_provider_qat_process_exit(ngx_cycle_t *cycle)
{
    ngx_ssl_provider_qat_conf_t  *spqcf;
    ngx_uint_t                    i;

    spqcf = ngx_provider_cycle_get_conf(cycle, ngx_ssl_provider_qat_module);
    if (spqcf == NULL) {
        return;
    }

    /* Safety-net drain for when graceful release is off or did not complete. */
    if (qat_prov != NULL && !qat_provider_status.finished) {
        for (i = 0; i < QAT_PROVIDER_EXIT_DRAIN_MAX; i++) {
            if (ngx_ssl_provider_qat_refresh_counters() != NGX_OK) {
                break;
            }
            if (ngx_ssl_provider_qat_total_in_flight() == 0) {
                break;
            }
            ngx_ssl_provider_qat_poll(cycle->log);
        }
        qat_provider_status.finished = 1;
    }

    /* Balance the counted references this module took; OpenSSL keeps its own
     * for a cnf-activated provider. */
    if (qat_prov_self_loaded != NULL) {
        OSSL_PROVIDER_unload(qat_prov_self_loaded);
        qat_prov_self_loaded = NULL;
    }
    if (qat_prov_preloaded != NULL) {
        OSSL_PROVIDER_unload(qat_prov_preloaded);
        qat_prov_preloaded = NULL;
    }
    if (default_prov_self_loaded != NULL) {
        OSSL_PROVIDER_unload(default_prov_self_loaded);
        default_prov_self_loaded = NULL;
    }

    qat_prov = NULL;
}
