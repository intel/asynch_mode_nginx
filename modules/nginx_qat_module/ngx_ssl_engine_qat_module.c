/* ====================================================================
 *
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2016-2018 Intel Corporation.
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
#include <ngx_ssl_engine.h>


typedef struct {
    /* if this engine can be released during worker is shutting down */
    ngx_flag_t      releasable;
    /* sync or async (default) */
    ngx_str_t       offload_mode;

    /* event or poll (default) */
    ngx_str_t       notify_mode;

    /* inline, internal (default), external or heuristic */
    ngx_str_t       poll_mode;

    /* xxx ns */
    ngx_int_t       internal_poll_interval;

    /* xxx ms */
    ngx_int_t       external_poll_interval;

    ngx_int_t       heuristic_poll_asym_threshold;

    ngx_int_t       heuristic_poll_cipher_threshold;

    ngx_array_t    *small_pkt_offload_threshold;
} ngx_ssl_engine_qat_conf_t;


static ngx_int_t ngx_ssl_engine_qat_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_engine_qat_send_ctrl(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_engine_qat_register_handler(ngx_cycle_t *cycle);
static ngx_int_t ngx_ssl_engine_qat_release(ngx_cycle_t *cycle);
static void ngx_ssl_engine_qat_heuristic_poll(ngx_log_t *log);

static char *ngx_ssl_engine_qat_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_ssl_engine_qat_set_threshold(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

static void *ngx_ssl_engine_qat_create_conf(ngx_cycle_t *cycle);
static char *
ngx_ssl_engine_qat_releasable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_ssl_engine_qat_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_int_t ngx_ssl_engine_qat_process_init(ngx_cycle_t *cycle);
static void ngx_ssl_engine_qat_process_exit(ngx_cycle_t *cycle);


#define EXTERNAL_POLL_DEFAULT_INTERVAL              1

#define HEURISTIC_POLL_ASYM_DEFAULT_THRESHOLD       16
#define HEURISTIC_POLL_CIPHER_DEFAULT_THRESHOLD     32

#define GET_NUM_ASYM_REQUESTS_IN_FLIGHT             1
#define GET_NUM_PRF_REQUESTS_IN_FLIGHT              2
#define GET_NUM_CIPHER_PIPELINE_REQUESTS_IN_FLIGHT  3

#define INLINE_POLL     1
#define INTERNAL_POLL   2
#define EXTERNAL_POLL   3
#define HEURISTIC_POLL  4

static ENGINE          *qat_engine;

static ngx_uint_t       qat_engine_enable_inline_polling;

static ngx_uint_t       qat_engine_enable_internal_polling;


static ngx_uint_t       qat_engine_enable_external_polling;
static ngx_int_t        qat_engine_external_poll_interval;
static ngx_event_t      qat_engine_external_poll_event;
static ngx_connection_t dumb;

static ngx_uint_t   qat_engine_enable_heuristic_polling;
static ngx_int_t    qat_engine_heuristic_poll_asym_threshold;
static ngx_int_t    qat_engine_heuristic_poll_cipher_threshold;

static int  num_heuristic_poll;
static int *num_asym_requests_in_flight;
static int *num_prf_requests_in_flight;
static int *num_cipher_requests_in_flight;

/* Since any polling mode change need to restart Nginx service
 * The initial polling mode is record when Nginx master start
 * for valid configuration check during Nginx worker reload
 * 0:unset, 1:inline, 2:internal, 3:external, 4:heuristic
 */
static ngx_int_t    qat_engine_init_polling_mode = 0;

typedef struct qat_instance_status_s {
    ngx_flag_t busy;
    ngx_flag_t finished;
    ngx_int_t  checkpoint;
} qat_instance_status_t;

static qat_instance_status_t qat_instance_status;

static int  num_heuristic_poll = 0;
static int *num_asym_requests_in_flight = NULL;
static int *num_prf_requests_in_flight = NULL;
static int *num_cipher_requests_in_flight = NULL;

static ngx_str_t      ssl_engine_qat_name = ngx_string("qat");

static ngx_command_t  ngx_ssl_engine_qat_commands[] = {

    { ngx_string("qat_engine"),
      NGX_SSL_ENGINE_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_ssl_engine_qat_block,
      0,
      0,
      NULL },

    { ngx_string("qat_shutting_down_release"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_ssl_engine_qat_releasable,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, releasable),
      NULL },

    { ngx_string("qat_offload_mode"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, offload_mode),
      NULL },

    { ngx_string("qat_notify_mode"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, notify_mode),
      NULL },

    { ngx_string("qat_poll_mode"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, poll_mode),
      NULL },

    { ngx_string("qat_internal_poll_interval"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, internal_poll_interval),
      NULL },

    { ngx_string("qat_external_poll_interval"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, external_poll_interval),
      NULL },

    { ngx_string("qat_heuristic_poll_asym_threshold"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, heuristic_poll_asym_threshold),
      NULL },

    { ngx_string("qat_heuristic_poll_cipher_threshold"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, heuristic_poll_cipher_threshold),
      NULL },

    { ngx_string("qat_small_pkt_offload_threshold"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_1MORE,
      ngx_ssl_engine_qat_set_threshold,
      0,
      offsetof(ngx_ssl_engine_qat_conf_t, small_pkt_offload_threshold),
      NULL },

      ngx_null_command
};

ngx_ssl_engine_module_t  ngx_ssl_engine_qat_module_ctx = {
    &ssl_engine_qat_name,
    ngx_ssl_engine_qat_create_conf,               /* create configuration */
    ngx_ssl_engine_qat_init_conf,                 /* init configuration */

    {
        ngx_ssl_engine_qat_init,
        ngx_ssl_engine_qat_send_ctrl,
        ngx_ssl_engine_qat_register_handler,
        ngx_ssl_engine_qat_release,
        ngx_ssl_engine_qat_heuristic_poll
    }
};

ngx_module_t  ngx_ssl_engine_qat_module = {
    NGX_MODULE_V1,
    &ngx_ssl_engine_qat_module_ctx,      /* module context */
    ngx_ssl_engine_qat_commands,         /* module directives */
    NGX_SSL_ENGINE_MODULE,               /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    ngx_ssl_engine_qat_process_init,     /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    ngx_ssl_engine_qat_process_exit,     /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_ssl_engine_qat_init(ngx_cycle_t *cycle)
{
    ngx_memset(&qat_instance_status, 0, sizeof(qat_instance_status));

    return NGX_OK;
}


static ngx_int_t
ngx_ssl_engine_qat_release(ngx_cycle_t *cycle)
{
    unsigned int i;
    ngx_connection_t  *c;

    ngx_ssl_engine_qat_conf_t *seqcf;

    seqcf = ngx_ssl_engine_get_conf(cycle->conf_ctx, ngx_ssl_engine_qat_module);

    if(!seqcf->releasable || qat_instance_status.finished) {

        return NGX_OK;
    }

    c = cycle->connections;

    i = qat_instance_status.checkpoint;

    for (; i < cycle->connection_n; i++) {
        if (c[i].fd == -1) {
            continue;
        }

        if ((c[i].ssl && !c[i].ssl->handshaked) ||
            (!c[i].ssl && c[i].ssl_enabled)) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                "connections in SSL handshake phase");
            qat_instance_status.checkpoint = i;
            qat_instance_status.busy = 1;
            break;
        }

        qat_instance_status.busy = 0;

    }

    if(!qat_instance_status.busy) {
        ENGINE *e = ENGINE_by_id("qat");
        ENGINE_GEN_INT_FUNC_PTR qat_finish = ENGINE_get_finish_function(e);

        if(0 == *num_asym_requests_in_flight &&
           0 == *num_prf_requests_in_flight &&
           0 == *num_cipher_requests_in_flight &&
           1 == qat_finish(e)) {
            qat_instance_status.finished = 1;
            ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                                 "QAT engine finished");
        } else {
            ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                                 "QAT engine finished error");
        }

        ENGINE_free(e);
    }

    return NGX_OK;
}

static ngx_int_t
ngx_ssl_engine_qat_send_ctrl(ngx_cycle_t *cycle)
{
    ngx_ssl_engine_qat_conf_t *seqcf;
    ngx_ssl_engine_conf_t *secf;

    const char *engine_id = "qat";
    ENGINE     *e;
    ngx_str_t  *value;
    ngx_uint_t  i;

    e = ENGINE_by_id(engine_id);
    if (e == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "ENGINE_by_id(\"qat\") failed");
        return NGX_ERROR;
    }

    seqcf = ngx_ssl_engine_get_conf(cycle->conf_ctx, ngx_ssl_engine_qat_module);

    if (ngx_strcmp(seqcf->offload_mode.data, "async") == 0) {
        /* Need to be consistent with the directive ssl_async */
    }

    if (ngx_strcmp(seqcf->notify_mode.data, "event") == 0) {
        if (!ENGINE_ctrl_cmd(e, "ENABLE_EVENT_DRIVEN_POLLING_MODE", 0, NULL,
            NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                            "QAT Engine failed: "
                            "ENABLE_EVENT_DRIVEN_POLLING_MODE");
            ENGINE_free(e);
            return NGX_ERROR;
        }
    }


    /* check the validity of possible polling mode switch for nginx reload */

    if (qat_engine_enable_internal_polling
        && qat_engine_init_polling_mode == INLINE_POLL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "Switch from inline to internal polling is invalid, "
                      "and still use inline polling");

        qat_engine_enable_internal_polling = 0;
        qat_engine_enable_inline_polling = 1;
    }

    if (qat_engine_enable_internal_polling
        && qat_engine_init_polling_mode == EXTERNAL_POLL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "Switch from external to internal polling is invalid, "
                      "and still use external polling");

        qat_engine_enable_internal_polling = 0;
        qat_engine_enable_external_polling = 1;
        qat_engine_external_poll_interval = EXTERNAL_POLL_DEFAULT_INTERVAL;
    }

    if (qat_engine_enable_internal_polling
        && qat_engine_init_polling_mode == HEURISTIC_POLL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "Switch from heuristic to internal polling is invalid, "
                      "and still use heuristic polling");

        qat_engine_enable_internal_polling = 0;
        qat_engine_enable_external_polling = 1;
        qat_engine_enable_heuristic_polling = 1;
        qat_engine_external_poll_interval = 5;
    }

    /* check the offloaded algorithms in the inline polling mode */

    secf = ngx_ssl_engine_get_conf(cycle->conf_ctx, ngx_ssl_engine_core_module);

    if (qat_engine_enable_inline_polling) {
        if (secf->default_algorithms != NGX_CONF_UNSET_PTR) {
            value = secf->default_algorithms->elts;
            for (i = 0; i < secf->default_algorithms->nelts; i++) {
                if (ngx_strcmp(value[i].data, "RSA") != 0) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                                  "Only RSA can be offloaded to QAT "
                                  "in the inline polling mode");
                    return NGX_ERROR;
                }
            }
        } else {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "Only RSA can be offloaded to QAT "
                          "in the inline polling mode");
            return NGX_ERROR;
        }
    }

    if (qat_engine_enable_inline_polling) {
        if (!ENGINE_ctrl_cmd(e, "ENABLE_INLINE_POLLING", 0, NULL, NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT Engine failed: ENABLE_INLINE_POLLING");
            ENGINE_free(e);
            return NGX_ERROR;
        }
    }

    if (qat_engine_enable_internal_polling
        && seqcf->internal_poll_interval != NGX_CONF_UNSET) {
        if (!ENGINE_ctrl_cmd(e, "SET_INTERNAL_POLL_INTERVAL",
            (long) seqcf->internal_poll_interval, NULL, NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT Engine failed: SET_INTERNAL_POLL_INTERVAL");
            ENGINE_free(e);
            return NGX_ERROR;
        }
    }

    if (qat_engine_enable_external_polling) {
        if (!ENGINE_ctrl_cmd(e, "ENABLE_EXTERNAL_POLLING", 0, NULL, NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT Engine failed: ENABLE_EXTERNAL_POLLING");
            ENGINE_free(e);
            return NGX_ERROR;
        }
    }

    if (qat_engine_enable_external_polling || qat_engine_enable_heuristic_polling) {
        if (!ENGINE_ctrl_cmd(e, "ENABLE_HEURISTIC_POLLING", 0, NULL, NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT Engine failed: ENABLE_HEURISTIC_POLLING");
            ENGINE_free(e);
            return NGX_ERROR;
        }
    }

    if (seqcf->small_pkt_offload_threshold != NGX_CONF_UNSET_PTR) {
        value = seqcf->small_pkt_offload_threshold->elts;
        for (i = 0; i < seqcf->small_pkt_offload_threshold->nelts; i++) {
            if (!ENGINE_ctrl_cmd(e, "SET_CRYPTO_SMALL_PACKET_OFFLOAD_THRESHOLD",
                0, value[i].data, NULL, 0)) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "QAT Engine failed: "
                              "SET_CRYPTO_SMALL_PACKET_OFFLOAD_THRESHOLD");
                ENGINE_free(e);
                return NGX_ERROR;
            }
        }
    }

    /* ssl engine global variable set */
    if (qat_engine_enable_heuristic_polling) {
        ngx_ssl_engine_enable_heuristic_polling = 1;
    }

    /* record old polling mode to prevent invalid mode switch */

    if (qat_engine_enable_inline_polling) {
        qat_engine_init_polling_mode = INLINE_POLL;

    } else if (qat_engine_enable_internal_polling) {
        qat_engine_init_polling_mode = INTERNAL_POLL;

    } else {
        if (qat_engine_enable_external_polling) {
            qat_engine_init_polling_mode = EXTERNAL_POLL;
        }

        if (qat_engine_enable_heuristic_polling) {
            qat_engine_init_polling_mode = HEURISTIC_POLL;
        }
    }

    ENGINE_free(e);

    return NGX_OK;
}


static inline void
qat_engine_poll(ngx_log_t *log) {
    int poll_status = 0;

    if (!ENGINE_ctrl_cmd(qat_engine, "POLL", 0, &poll_status, NULL, 0)) {
        ngx_log_error(NGX_LOG_ALERT, log, 0, "QAT Engine failed: POLL");
    }
}


static void
qat_engine_external_poll_handler(ngx_event_t *ev)
{
    if(qat_instance_status.finished) {
        return;
    }

    if (*num_asym_requests_in_flight + *num_prf_requests_in_flight
           + *num_cipher_requests_in_flight > 0) {
        if (!qat_engine_enable_heuristic_polling) {
            qat_engine_poll(ev->log);
        } else {
            if (!num_heuristic_poll) {
                qat_engine_poll(ev->log);
            }
            num_heuristic_poll = 0;
        }
    }

    if (ngx_event_timer_rbtree.root != ngx_event_timer_rbtree.sentinel ||
        !ngx_exiting) {
        ngx_add_timer(ev, qat_engine_external_poll_interval);
    }

}


static void
qat_engine_external_poll_init(ngx_log_t* log)
{
    memset(&qat_engine_external_poll_event, 0, sizeof(ngx_event_t));

    dumb.fd = (ngx_socket_t) -1;
    qat_engine_external_poll_event.data = &dumb;

    qat_engine_external_poll_event.handler = qat_engine_external_poll_handler;
    qat_engine_external_poll_event.log = log;
    qat_engine_external_poll_event.cancelable = 0;

    ngx_add_timer(&qat_engine_external_poll_event, 100);
    qat_engine_external_poll_event.timer_set = 1;
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, log, 0, "Adding initial polling timer");
}


static ngx_int_t
ngx_ssl_engine_qat_register_handler(ngx_cycle_t *cycle)
{
    if (qat_engine_enable_external_polling) {
        qat_engine_external_poll_init(cycle->log);
    }

    return NGX_OK;
}


static void
ngx_ssl_engine_qat_heuristic_poll(ngx_log_t *log) {
    int polled_flag = 0;

    if ((int)*ngx_ssl_active <= 0) return;

    while (*num_asym_requests_in_flight + *num_prf_requests_in_flight
           + *num_cipher_requests_in_flight >= (int) *ngx_ssl_active) {
        qat_engine_poll(log);
        num_heuristic_poll ++;
        polled_flag = 1;
    }

    if (polled_flag) return;

    while (*num_asym_requests_in_flight
           >= qat_engine_heuristic_poll_asym_threshold) {
        qat_engine_poll(log);
        num_heuristic_poll ++;
        polled_flag = 1;
    }

    if (polled_flag) return;

    if (*num_cipher_requests_in_flight < *num_asym_requests_in_flight
        || *num_asym_requests_in_flight
           >= qat_engine_heuristic_poll_asym_threshold/2) {
        return;
    }

    while (*num_cipher_requests_in_flight
           >= qat_engine_heuristic_poll_cipher_threshold) {
        qat_engine_poll(log);
        num_heuristic_poll ++;
    }
}


static char *
ngx_ssl_engine_qat_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char           *rv;
    ngx_conf_t      pcf;

    pcf = *cf;
    cf->cmd_type = NGX_SSL_ENGINE_SUB_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    return NGX_CONF_OK;
}


static char *
ngx_ssl_engine_qat_set_threshold(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *p = conf;

    ngx_str_t          *value, *s;
    ngx_array_t       **a;
    ngx_uint_t          i;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NGX_CONF_UNSET_PTR) {
        *a = ngx_array_create(cf->pool, cf->args->nelts - 1, sizeof(ngx_str_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {
        s = ngx_array_push(*a);
        if (s == NULL) {
            return NGX_CONF_ERROR;
        }
        *s = value[i];
    }

    return NGX_CONF_OK;
}


static void *
ngx_ssl_engine_qat_create_conf(ngx_cycle_t *cycle)
{
    ngx_ssl_engine_qat_conf_t  *seqcf;

    seqcf = ngx_pcalloc(cycle->pool, sizeof(ngx_ssl_engine_qat_conf_t));
    if (seqcf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     seqcf->offload_mode = NULL
     *     seqcf->notify_mode = NULL
     *     seqcf->poll_mode = NULL
     */

    qat_engine_enable_inline_polling = 0;
    qat_engine_enable_internal_polling = 0;
    qat_engine_enable_external_polling = 0;
    qat_engine_enable_heuristic_polling = 0;

    seqcf->releasable = NGX_CONF_UNSET;
    seqcf->external_poll_interval = NGX_CONF_UNSET;
    seqcf->internal_poll_interval = NGX_CONF_UNSET;

    seqcf->heuristic_poll_asym_threshold = NGX_CONF_UNSET;
    seqcf->heuristic_poll_cipher_threshold = NGX_CONF_UNSET;

    seqcf->small_pkt_offload_threshold = NGX_CONF_UNSET_PTR;

    return seqcf;
}


static char *
ngx_ssl_engine_qat_releasable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t  *value;
    ngx_uint_t  i;
    char  *rv;
    ngx_ssl_engine_conf_t *secf;
    ngx_ssl_engine_qat_conf_t *seqcf = conf;

    secf = ngx_ssl_engine_get_conf(cf->cycle->conf_ctx, ngx_ssl_engine_core_module);

    if (seqcf->poll_mode.data == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->cycle->log, 0,
                      "Please specify polling mode before"
                      "qat_shutting_down_release is set");
        return NGX_CONF_ERROR;
    }


    rv = ngx_conf_set_flag_slot(cf, cmd, conf);

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    if (seqcf->releasable) {
        /* Currently qat release while worker shutting down feature
         * is unavailable when CIPHERS is offloaded to QAT.
         * Logic in below block will prevent the release if CIPHERS
         * is offloaded to QAT.
         */
        if (secf->default_algorithms == NGX_CONF_UNSET_PTR) {
            ngx_log_error(NGX_LOG_EMERG, cf->cycle->log, 0,
                          "QAT is unreleasable during worker shutting down due "
                          "to CIPHERS is offloaded");
            seqcf->releasable = 0;

        } else {
            value = secf->default_algorithms->elts;
            for (i = 0; i < secf->default_algorithms->nelts; i++) {
                if (ngx_strstr(value[i].data, "ALL") != NULL ||
                    ngx_strstr(value[i].data, "CIPHERS") != NULL) {
                    ngx_log_error(NGX_LOG_EMERG, cf->cycle->log, 0,
                                  "QAT is unreleasable during worker shutting "
                                  "down due to CIPHERS is offloaded");
                    seqcf->releasable = 0;
                }
            }
        }

        if (ngx_strcmp(seqcf->poll_mode.data, "external") != 0 &&
            ngx_strcmp(seqcf->poll_mode.data, "heuristic") != 0) {
            ngx_log_error(NGX_LOG_EMERG, cf->cycle->log, 0,
                          "QAT is releasable only external or heuristic polling "
                          "mode is set");
            seqcf->releasable = 0;
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_ssl_engine_qat_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_ssl_engine_qat_conf_t *seqcf = conf;

    /* init the conf values not set by the user */

    ngx_conf_init_str_value(seqcf->offload_mode, "async");
    ngx_conf_init_str_value(seqcf->notify_mode, "poll");
    ngx_conf_init_str_value(seqcf->poll_mode, "internal");

    ngx_conf_init_value(seqcf->releasable, 0);

    ngx_conf_init_value(seqcf->heuristic_poll_asym_threshold,
                        HEURISTIC_POLL_ASYM_DEFAULT_THRESHOLD);

    ngx_conf_init_value(seqcf->heuristic_poll_cipher_threshold,
                        HEURISTIC_POLL_CIPHER_DEFAULT_THRESHOLD);


    /* check the validity of the conf vaules */

    if (ngx_strcmp(seqcf->offload_mode.data, "sync") != 0
        && ngx_strcmp(seqcf->offload_mode.data, "async") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_offload_mode");
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(seqcf->notify_mode.data, "event") != 0
        && ngx_strcmp(seqcf->notify_mode.data, "poll") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_notify_mode");
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(seqcf->poll_mode.data, "inline") != 0
        && ngx_strcmp(seqcf->poll_mode.data, "internal") != 0
        && ngx_strcmp(seqcf->poll_mode.data, "external") != 0
        && ngx_strcmp(seqcf->poll_mode.data, "heuristic") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_poll_mode");
        return NGX_CONF_ERROR;
    }

    /* check the validity of the engine ctrl combination */

    if (ngx_strcmp(seqcf->offload_mode.data, "sync") == 0) {
        if (ngx_strcmp(seqcf->notify_mode.data, "event") == 0
            && ngx_strcmp(seqcf->poll_mode.data, "inline") == 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"sync + event + inline\" is invalid");
            return NGX_CONF_ERROR;
        }

        if (ngx_strcmp(seqcf->poll_mode.data, "external") == 0
            || ngx_strcmp(seqcf->poll_mode.data, "heuristic") == 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"sync + external/heuristic\" is invalid");
            return NGX_CONF_ERROR;
        }
    } else {
        /* async mode */

        if (ngx_strcmp(seqcf->poll_mode.data, "inline") == 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"async + inline\" is invalid");
            return NGX_CONF_ERROR;
        }

        if (ngx_strcmp(seqcf->notify_mode.data, "event") == 0
            && ngx_strcmp(seqcf->poll_mode.data, "external") == 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"async + event + external\" "
                          "is currently not supported");
            return NGX_CONF_ERROR;
        }

        if (ngx_strcmp(seqcf->notify_mode.data, "event") == 0
            && ngx_strcmp(seqcf->poll_mode.data, "heuristic") == 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"async + event + heuristic\" is invalid");
            return NGX_CONF_ERROR;
        }
    }

    if (seqcf->external_poll_interval > 1000
        || seqcf->external_poll_interval == 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "invalid external poll interval");
        return NGX_CONF_ERROR;
    }

    if (seqcf->internal_poll_interval > 10000000
        || seqcf->internal_poll_interval == 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "invalid internal poll interval");
        return NGX_CONF_ERROR;
    }

    if (seqcf->heuristic_poll_asym_threshold > 512
        || seqcf->heuristic_poll_cipher_threshold > 512) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "invalid heuristic poll threshold");
        return NGX_CONF_ERROR;
    }

    /* global variable set */

    if (ngx_strcmp(seqcf->notify_mode.data, "poll") == 0
        && ngx_strcmp(seqcf->poll_mode.data, "inline") == 0) {
        qat_engine_enable_inline_polling = 1;
    }

    if (ngx_strcmp(seqcf->notify_mode.data, "poll") == 0
        && ngx_strcmp(seqcf->poll_mode.data, "internal") == 0) {
        qat_engine_enable_internal_polling = 1;
    }

    if (ngx_strcmp(seqcf->notify_mode.data, "poll") == 0
        && ngx_strcmp(seqcf->poll_mode.data, "external") == 0) {
        qat_engine_enable_external_polling = 1;
        ngx_conf_init_value(seqcf->external_poll_interval,
                            EXTERNAL_POLL_DEFAULT_INTERVAL);
    }

    if (ngx_strcmp(seqcf->notify_mode.data, "poll") == 0
        && ngx_strcmp(seqcf->poll_mode.data, "heuristic") == 0) {
        qat_engine_enable_external_polling = 1;
        qat_engine_enable_heuristic_polling = 1;
        ngx_conf_init_value(seqcf->external_poll_interval, 5);
    }

    qat_engine_external_poll_interval = seqcf->external_poll_interval;

    qat_engine_heuristic_poll_asym_threshold
        = seqcf->heuristic_poll_asym_threshold;

    qat_engine_heuristic_poll_cipher_threshold
        = seqcf->heuristic_poll_cipher_threshold;

    return NGX_CONF_OK;
}


static ngx_int_t
qat_engine_share_info(ngx_log_t *log) {
    if (!ENGINE_ctrl_cmd(qat_engine, "GET_NUM_REQUESTS_IN_FLIGHT",
        GET_NUM_ASYM_REQUESTS_IN_FLIGHT,
        &num_asym_requests_in_flight, NULL, 0)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "QAT Engine failed: GET_NUM_REQUESTS_IN_FLIGHT");
        return NGX_ERROR;
    }

    if (!ENGINE_ctrl_cmd(qat_engine, "GET_NUM_REQUESTS_IN_FLIGHT",
        GET_NUM_PRF_REQUESTS_IN_FLIGHT,
        &num_prf_requests_in_flight, NULL, 0)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "QAT Engine failed: GET_NUM_REQUESTS_IN_FLIGHT");
        return NGX_ERROR;
    }

    if (!ENGINE_ctrl_cmd(qat_engine, "GET_NUM_REQUESTS_IN_FLIGHT",
        GET_NUM_CIPHER_PIPELINE_REQUESTS_IN_FLIGHT,
        &num_cipher_requests_in_flight, NULL, 0)) {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "QAT Engine failed: GET_NUM_REQUESTS_IN_FLIGHT");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_ssl_engine_qat_process_init(ngx_cycle_t *cycle)
{
    const char  *engine_id = "qat";

    num_heuristic_poll = 0;
    num_asym_requests_in_flight = NULL;
    num_prf_requests_in_flight = NULL;
    num_cipher_requests_in_flight = NULL;

    qat_engine = ENGINE_by_id(engine_id);
    if (qat_engine == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "ENGINE_by_id(\"qat\") failed");
        return NGX_ERROR;
    }

    if (qat_engine_share_info(cycle->log) != NGX_OK) {
        ENGINE_free(qat_engine);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_ssl_engine_qat_process_exit(ngx_cycle_t *cycle)
{
    if (qat_engine) {
        ENGINE_finish(qat_engine);
        ENGINE_free(qat_engine);
    }

    qat_engine = NULL;

    ENGINE_cleanup();
}
