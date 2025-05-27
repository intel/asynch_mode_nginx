/* ====================================================================
 *
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2021-2022 Intel Corporation.
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
#include <ngx_ssl_engine.h>
#include <ngx_log.h>
#ifdef OPENSSL_IS_BORINGSSL
#include "qat_bssl.h"
#endif /* OPENSSL_IS_BORINGSSL */
#include <ngx_http_ssl_module.h>

typedef struct {
    ngx_str_t       engine_id;

    ngx_flag_t      enable_sw_fallback;
    /* sync or async (default) */
    ngx_str_t       offload_mode;

    /* event or poll (default) */
    ngx_str_t       notify_mode;

    /* inline, internal (default), external or heuristic */
    ngx_str_t       poll_mode;

    /* xxx ns */
    ngx_int_t       internal_poll_interval;
} ngx_bssl_qat_conf_t;


static ngx_int_t ngx_bssl_qat_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_bssl_qat_send_ctrl(ngx_cycle_t *cycle);
static ngx_int_t ngx_bssl_qat_release(ngx_cycle_t *cycle);
static ngx_int_t ngx_bssl_qat_register_handler(ngx_cycle_t *cycle);

static char *ngx_bssl_qat_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void *ngx_bssl_qat_create_conf(ngx_cycle_t *cycle);
static char *ngx_bssl_qat_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_int_t ngx_bssl_qat_process_init(ngx_cycle_t *cycle);
static void ngx_bssl_qat_process_exit(ngx_cycle_t *cycle);


#define INLINE_POLL     1
#define INTERNAL_POLL   2

static ngx_uint_t       qat_engine_enable_inline_polling;

static ngx_uint_t       qat_engine_enable_internal_polling;

/* Since any polling mode change need to restart Nginx service
 * The initial polling mode is record when Nginx master start
 * for valid configuration check during Nginx worker reload
 * 0:unset, 1:inline, 2:internal, 3:external, 4:heuristic
 */
static ngx_int_t    qat_engine_init_polling_mode = 0;

#ifdef OPENSSL_IS_BORINGSSL
typedef ngx_flag_t async_enable;
#define NGX_GET_SSL_ASYNCH_ENABLE_FLAG(ssl)        \
    (BSSL_GET_ASYNC_CFG_FM_EXDATA(SSL, ssl))
#define NGX_SET_SSL_ASYNCH_ENABLE_FLAG(ssl, flag)  \
    BSSL_SET_ASYNC_CFG_TO_EXDATA(SSL, ssl, flag)
BSSL_DEFINE_ASYNC_CFG_INIT_EXDATA(SSL);

BSSL_DEFINE_ASYNC_CTX_INIT_EXDATA(SSL);

static ngx_int_t ngx_bssl_qat_set_srv_privkey_meth(ngx_cycle_t *cycle);
static int ngx_bssl_qat_overwrite_default_method(SSL_CTX *ctx);
static ngx_int_t ngx_bssl_qat_init_async_ctx(SSL *ssl);
static int ngx_bssl_qat_get_changed_async_fds(void *ssl, int *addfd,
    size_t *numaddfds, int *delfd, size_t *numdelfds);
static int ngx_bssl_qat_set_default_string(const char *def_list);
static ngx_int_t ngx_bssl_qat_exit_async_ctx(void *ssl);
static ngx_int_t ngx_bssl_qat_set_async_flag(void *ssl, void *ssl_async);
static int ngx_bssl_qat_wait_for_async(void *ssl);
static int ngx_bssl_qat_want_async(const void *ssl);

static enum ssl_private_key_result_t
ngx_bssl_qat_private_key_sign(SSL *ssl, uint8_t *out, size_t *out_len,
    size_t max_out, uint16_t signature_algorithm, const uint8_t *in,
    size_t in_len);
static enum ssl_private_key_result_t
ngx_bssl_qat_private_key_decrypt(SSL *ssl, uint8_t *out, size_t *out_len,
    size_t max_out, const uint8_t *in, size_t in_len);
static enum ssl_private_key_result_t
ngx_bssl_qat_private_key_complete(SSL* ssl, uint8_t* out, size_t* out_len,
    size_t max_out);

static const SSL_PRIVATE_KEY_METHOD
ngx_bssl_qat_priv_key_method = {
    ngx_bssl_qat_private_key_sign,
    ngx_bssl_qat_private_key_decrypt,
    ngx_bssl_qat_private_key_complete,
};
#else /* OPENSSL_IS_BORINGSSL */
#define bssl_qat_send_ctrl_cmd ENGINE_ctrl_cmd
#endif /* OPENSSL_IS_BORINGSSL */

//the same name with OpenSSL, so that the config file is compatible.
static ngx_str_t ngx_bssl_qat_module_name = ngx_string("qatengine");

//remove ssl_qat on, it is replaced by use_engine qat.
static ngx_command_t  ngx_bssl_qat_commands[] = {
    { ngx_string("qat_engine"),
      NGX_SSL_ENGINE_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_bssl_qat_block,
      0,
      0,
      NULL },

    { ngx_string("qat_sw_fallback"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_bssl_qat_conf_t, enable_sw_fallback),
      NULL },

    { ngx_string("qat_offload_mode"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_bssl_qat_conf_t, offload_mode),
      NULL },

    { ngx_string("qat_notify_mode"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_bssl_qat_conf_t, notify_mode),
      NULL },

    { ngx_string("qat_poll_mode"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_bssl_qat_conf_t, poll_mode),
      NULL },

    { ngx_string("qat_internal_poll_interval"),
      NGX_SSL_ENGINE_SUB_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_bssl_qat_conf_t, internal_poll_interval),
      NULL },


      ngx_null_command
};


ngx_ssl_engine_module_t  ngx_bssl_qat_module_ctx = {
    &ngx_bssl_qat_module_name,
    ngx_bssl_qat_create_conf,                /* create configuration */
    ngx_bssl_qat_init_conf,                  /* init configuration */

    {
        ngx_bssl_qat_init,
        ngx_bssl_qat_send_ctrl,
        ngx_bssl_qat_register_handler,
        ngx_bssl_qat_release,
#ifndef OPENSSL_IS_BORINGSSL
        NULL
#else /* OPENSSL_IS_BORINGSSL */
        NULL,
        {
            ngx_bssl_qat_wait_for_async,
            ngx_bssl_qat_want_async,
            ngx_bssl_qat_get_changed_async_fds,
            ngx_bssl_qat_set_default_string,
            ngx_bssl_qat_exit_async_ctx,
            ngx_bssl_qat_set_async_flag,
        }
#endif /* OPENSSL_IS_BORINGSSL */
    }
};

ngx_module_t  ngx_bssl_qat_module = {
    NGX_MODULE_V1,
    &ngx_bssl_qat_module_ctx,          /* module context */
    ngx_bssl_qat_commands,             /* module directives */
    NGX_SSL_ENGINE_MODULE,             /* module type */
    NULL,                              /* init master */
    NULL,                              /* init module */
    ngx_bssl_qat_process_init,         /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    ngx_bssl_qat_process_exit,         /* exit process */
    NULL,                              /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_bssl_qat_init(ngx_cycle_t *cycle)
{
    return NGX_OK;
}


static ngx_int_t
ngx_bssl_qat_release(ngx_cycle_t *cycle)
{
#ifdef OPENSSL_IS_BORINGSSL
    ENGINE_unload_qat();
#endif /* OPENSSL_IS_BORINGSSL */
    return NGX_OK;
}


static ngx_int_t
ngx_bssl_qat_send_ctrl(ngx_cycle_t *cycle)
{
    ngx_bssl_qat_conf_t *seqcf;
    ngx_ssl_engine_conf_t *secf;

    ENGINE     *e = NULL;
    ngx_str_t  *value;
    ngx_uint_t  i;

    seqcf = ngx_engine_cycle_get_conf(cycle, ngx_bssl_qat_module);
    if (seqcf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                     "conf of engine_core_module is null");
        return NGX_ERROR;
    }

    if (ngx_strcmp(seqcf->offload_mode.data, "async") == 0) {
        /* Need to be consistent with the directive ssl_async */
    }

    /* Currently, init debug log maybe essential before ENGINE_load_qat() */
#ifdef OPENSSL_IS_BORINGSSL
    bssl_qat_send_ctrl_cmd(e, BSSL_QAT_INIT_DEBUG_LOG, 0, NULL, NULL, 0);
#endif /* OPENSSL_IS_BORINGSSL */

    /* check the validity of possible polling mode switch for nginx reload */

    if (qat_engine_enable_internal_polling
        && qat_engine_init_polling_mode == INLINE_POLL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "Switch from inline to internal polling is invalid, "
                      "and still use inline polling");

        qat_engine_enable_internal_polling = 0;
        qat_engine_enable_inline_polling = 1;
    }

    /* check the offloaded algorithms in the inline polling mode */

    secf = ngx_engine_cycle_get_conf(cycle, ngx_ssl_engine_core_module);
    if (secf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                     "conf of engine_core_module is null");
        return NGX_ERROR;
    }

    /* Only RSA can be paired with inline polling. */
    if (qat_engine_enable_inline_polling) {
        if (secf->default_algorithms != NGX_CONF_UNSET_PTR) {
            value = secf->default_algorithms->elts;
            for (i = 0; i < secf->default_algorithms->nelts; i++) {
                if (ngx_strcmp(value[i].data, "RSA") != 0) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                                "Only RSA can be paired with inline polling.");
                    return NGX_ERROR;
                }
            }
        } else {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                            "Deault algorithm should be RSA and EC.");
            return NGX_ERROR;
        }
    }

    if (qat_engine_enable_inline_polling) {
        if (!bssl_qat_send_ctrl_cmd(e, "ENABLE_INLINE_POLLING", 0, NULL, NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT Engine failed: ENABLE_INLINE_POLLING");
            return NGX_ERROR;
        }
    }

    /* Send enable SW fallback command to QAT */
    if (seqcf->enable_sw_fallback
        && seqcf->enable_sw_fallback != NGX_CONF_UNSET) {
        if (!bssl_qat_send_ctrl_cmd(e, "ENABLE_SW_FALLBACK", 0, NULL, NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT Engine failed: ENABLE_SW_FALLBACK");
            return NGX_ERROR;
        }
    }

    if (qat_engine_enable_internal_polling
        && seqcf->internal_poll_interval != NGX_CONF_UNSET) {
        if (!bssl_qat_send_ctrl_cmd(e, "SET_INTERNAL_POLL_INTERVAL",
            (long) seqcf->internal_poll_interval, NULL, NULL, 0)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "QAT Engine failed: SET_INTERNAL_POLL_INTERVAL");
            return NGX_ERROR;
        }
    }

    /* record old polling mode to prevent invalid mode switch */

    if (qat_engine_enable_inline_polling) {
        qat_engine_init_polling_mode = INLINE_POLL;

    } else if (qat_engine_enable_internal_polling) {
        qat_engine_init_polling_mode = INTERNAL_POLL;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_bssl_qat_register_handler(ngx_cycle_t *cycle)
{
#ifdef OPENSSL_IS_BORINGSSL
    return ngx_bssl_qat_set_srv_privkey_meth(cycle);
#else
    return NGX_OK;
#endif
}


static char *
ngx_bssl_qat_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
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


static void *
ngx_bssl_qat_create_conf(ngx_cycle_t *cycle)
{
    ngx_bssl_qat_conf_t  *seqcf;

    seqcf = ngx_pcalloc(cycle->pool, sizeof(ngx_bssl_qat_conf_t));
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

    seqcf->enable_sw_fallback = NGX_CONF_UNSET;
    seqcf->internal_poll_interval = NGX_CONF_UNSET;

    return seqcf;

}


static char *
ngx_bssl_qat_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_bssl_qat_conf_t *seqcf = conf;
    ngx_ssl_engine_conf_t * corecf =
        ngx_engine_cycle_get_conf(cycle, ngx_ssl_engine_core_module);
    if (corecf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                     "conf of engine_core_module is null");
        return NGX_CONF_ERROR;
    }


    if (0 != corecf->ssl_engine_id.len) {
        ngx_conf_init_str_value(seqcf->engine_id, corecf->ssl_engine_id.data);
    } else {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "No engine id found.");
        return NGX_CONF_ERROR;
    }

    ngx_conf_init_str_value(seqcf->offload_mode, "async");
    ngx_conf_init_str_value(seqcf->notify_mode, "poll");
    ngx_conf_init_str_value(seqcf->poll_mode, "internal");

    /* check the validity of the conf values */

    if (ngx_strcmp(seqcf->offload_mode.data, "sync") != 0
        && ngx_strcmp(seqcf->offload_mode.data, "async") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_offload_mode");
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(seqcf->poll_mode.data, "inline") != 0
        && ngx_strcmp(seqcf->poll_mode.data, "internal") != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "wrong type for qat_poll_mode");
        return NGX_CONF_ERROR;
    }

    /* check the validity of the engine ctrl combination */

    if (ngx_strcmp(seqcf->offload_mode.data, "sync") == 0) {
        /* sync mode */
    } else {
        /* async mode */

        if (ngx_strcmp(seqcf->poll_mode.data, "inline") == 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"async + inline\" is invalid");
            return NGX_CONF_ERROR;
        }
    }

    if (seqcf->internal_poll_interval > 10000000
        || seqcf->internal_poll_interval == 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                    "invalid internal poll interval");
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

    return NGX_CONF_OK;

}


static ngx_int_t
ngx_bssl_qat_process_init(ngx_cycle_t *cycle)
{
    ngx_bssl_qat_conf_t *conf =
        ngx_engine_cycle_get_conf(cycle, ngx_bssl_qat_module);
    if (conf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                     "conf of engine_core_module is null");
        return NGX_ERROR;
    }

    if (0 == (const char *) conf->engine_id.len) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                     "engine id not found");
        return NGX_ERROR;
    }

    return NGX_OK;
}


#ifdef OPENSSL_IS_BORINGSSL
static ngx_int_t
ngx_bssl_qat_set_srv_privkey_meth(ngx_cycle_t *cycle)
{
    ngx_uint_t                   s;
    ngx_http_ssl_srv_conf_t     *sscf;
    ngx_http_core_srv_conf_t   **cscf;
    ngx_http_core_main_conf_t   *cmcf;

    cmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_core_module);
    if (!cmcf) {
        return NGX_OK;
    }
    cscf = cmcf->servers.elts;

    ENGINE_load_qat();
    if (!ENGINE_QAT_PTR_GET()) {
        return NGX_ERROR;
    }
   /* Set ssl.ctx for each server block */
    for (s = 0; s < cmcf->servers.nelts; s++) {
        sscf = cscf[s]->ctx->srv_conf[ngx_http_ssl_module.ctx_index];
        if (sscf == NULL || sscf->ssl.ctx == NULL) {
            continue;
        }
        ngx_bssl_qat_overwrite_default_method(sscf->ssl.ctx);
    }

    return NGX_OK;
}
#endif /* OPENSSL_IS_BORINGSSL */


static void
ngx_bssl_qat_process_exit(ngx_cycle_t *cycle)
{
#ifdef OPENSSL_IS_BORINGSSL
    ENGINE_unload_qat();
#endif /* OPENSSL_IS_BORINGSSL */
}


#ifdef OPENSSL_IS_BORINGSSL
static int
ngx_bssl_qat_overwrite_default_method(SSL_CTX *ctx)
{
    EVP_PKEY *pkey = SSL_CTX_get0_privatekey(ctx);
    if (!pkey || bssl_private_key_method_update(pkey)) {
        return 1;
    }
    /* Change private key method of ssl_ctx */
    SSL_CTX_set_private_key_method(ctx, &ngx_bssl_qat_priv_key_method);

    return 0;
}


static ngx_int_t
ngx_bssl_qat_init_async_ctx(SSL *ssl)
{
    async_ctx *ctx;
    ngx_flag_t *ssl_async;

    /* Check if async is configured */
    ssl_async = NGX_GET_SSL_ASYNCH_ENABLE_FLAG(ssl);
    if (ssl_async == NULL || *ssl_async == 0) { /* ssl_async not enabled */
        /* Not start async job */
        return NGX_OK;
    }

    if (qat_engine_enable_inline_polling) {
        ngx_connection_t *c = ngx_queue_data(ssl_async, ngx_connection_t, asynch);
        if (c && c->log) {
            ngx_ssl_error(NGX_LOG_ERR, c->log, 0, "async (HTTP module) + inline is invalid.");
        }
        return NGX_ERROR;
    }

    ctx = bssl_qat_async_start_job();
    if (!ctx || BSSL_SET_ASYNC_CTX_TO_EXDATA(SSL, ssl, ctx)) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static int
ngx_bssl_qat_get_changed_async_fds(void *ssl, int *addfd,
    size_t *numaddfds, int *delfd, size_t *numdelfds)
{
    async_ctx *ctx = BSSL_GET_ASYNC_CTX_FM_EXDATA(SSL, ssl);
    if (!ctx) {
        *numaddfds = 0;
        *numdelfds = 0;
        return 1;
    }

    return bssl_qat_async_ctx_get_changed_fds(ctx, addfd, numaddfds, delfd,
                                              numdelfds);
}


static int
ngx_bssl_qat_set_default_string(const char *def_list)
{
    return bssl_qat_set_default_string(def_list);
}


static ngx_int_t
ngx_bssl_qat_exit_async_ctx(void *ssl)
{
    async_ctx *ctx = BSSL_GET_ASYNC_CTX_FM_EXDATA(SSL, ssl);
    if (!ctx) {
        return NGX_ERROR;
    }
    bssl_qat_async_finish_job(ctx);
    BSSL_SET_ASYNC_CTX_TO_EXDATA(SSL, ssl, NULL);

    return NGX_OK;
}

/* Referred to SSL_waiting_for_async() from OpenSSL */
static int
ngx_bssl_qat_wait_for_async(void *ssl)
{
    async_ctx *ctx = BSSL_GET_ASYNC_CTX_FM_EXDATA(SSL, ssl);
    if (!ctx) {
         return 0; /* no request is processing */
    }

    return 1; /* request is processing */
}

static ngx_int_t
ngx_bssl_qat_set_async_flag(void *ssl, void *ssl_async)
{
    if (NGX_SET_SSL_ASYNCH_ENABLE_FLAG(ssl, ssl_async)) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/* Referred to SSL_want_async from OpenSSL */
static int
ngx_bssl_qat_want_async(const void *ssl)
{
    async_ctx *ctx = BSSL_GET_ASYNC_CTX_FM_EXDATA(SSL, ssl);
    if (!ctx || ASYNC_job_is_stopped(ctx)) {
         return 0; /* async_ctx was freed or stopped, no request in flight */
    }

    return 1; /* request is processing */
}


static enum ssl_private_key_result_t
ngx_bssl_qat_private_key_sign(SSL *ssl, uint8_t *out, size_t *out_len,
    size_t max_out, uint16_t signature_algorithm, const uint8_t *in,
    size_t in_len)
{
    const EVP_MD *md;
    EVP_MD_CTX *ctx;
    EVP_PKEY_CTX *pctx;
    EVP_PKEY *pkey;

    if (NULL == (pkey = SSL_get_privatekey(ssl))) {
        return ssl_private_key_failure;
    }

    if (EVP_PKEY_id(pkey) !=
        SSL_get_signature_algorithm_key_type(signature_algorithm)) {
        return ssl_private_key_failure;
    }

    /* Determine the hash */
    ctx = EVP_MD_CTX_create();
    if (ctx == NULL) {
        return ssl_private_key_failure;
    }

    md = SSL_get_signature_algorithm_digest(signature_algorithm);

    if (!EVP_DigestSignInit(ctx, &pctx, md, NULL, pkey)) {
        goto sign_fail;
    }

    // Configure additional signature parameters.
    if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
        if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
            !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1
            /* salt len = hash len */)) {
            goto sign_fail;
        }
    }

    if (ngx_bssl_qat_init_async_ctx(ssl)) {
        goto sign_fail;
    }

    // Just get key length, not  signing
    if (!EVP_DigestSign(ctx, NULL, out_len, in, in_len)) {
        goto sign_fail;
    }

    if (!EVP_DigestSign(ctx, out, out_len, in, in_len)) {
        goto sign_fail;
    }

    /* Free EVP_MD_CTX and EVP_PKEY_CTX object */
    EVP_MD_CTX_destroy(ctx);
    return ngx_bssl_qat_private_key_complete(ssl, out, out_len, max_out);

sign_fail:
    /* Free EVP_MD_CTX and EVP_PKEY_CTX object */
    EVP_MD_CTX_destroy(ctx);
    return ssl_private_key_failure;
}


static enum ssl_private_key_result_t
ngx_bssl_qat_private_key_decrypt(SSL *ssl, uint8_t *out, size_t *out_len,
    size_t max_out, const uint8_t *in, size_t in_len)
{
    EVP_PKEY *pkey = NULL;
    RSA *rsa = NULL;

    /* Check if the SSL instance has correct data attached to it */
    if (NULL == (pkey = SSL_get_privatekey(ssl))) {
        return ssl_private_key_failure;
    }

    if (NULL == (rsa = EVP_PKEY_get0_RSA(pkey))) {
        return ssl_private_key_failure;
    }

    if (ngx_bssl_qat_init_async_ctx(ssl)) {
        return ssl_private_key_failure;
    }

    /* Referred the decrypt flow in ssl_private_key_decrypt of BoringSSL */
    if (!RSA_decrypt(rsa, out_len, out,
                    RSA_size(rsa), in, in_len, RSA_NO_PADDING)) {
        return ssl_private_key_failure;
    }

    return ngx_bssl_qat_private_key_complete(ssl, out, out_len, max_out);
}


static enum ssl_private_key_result_t
ngx_bssl_qat_private_key_complete(SSL *ssl, uint8_t *out, size_t *out_len,
    size_t max_out)
{
    async_ctx *ctx;
    ngx_flag_t *ssl_async;

    ssl_async = NGX_GET_SSL_ASYNCH_ENABLE_FLAG(ssl);
    if (ssl_async && *ssl_async) { /* ssl_async enabled */
        /* Clean ssl async flag in ssl exdata, moved to ngx_ssl_shutdown */
        /* NGX_SET_SSL_ASYNCH_ENABLE_FLAG(ssl, NULL);*/

        ctx = BSSL_GET_ASYNC_CTX_FM_EXDATA(SSL, ssl);
        if (!ctx) {
            return ssl_private_key_failure;
        }

        if (ASYNC_job_is_running(ctx)) {
            /* Re-set ssl async flag for retry */
            /* NGX_SET_SSL_ASYNCH_ENABLE_FLAG(ssl, ssl_async); */
            return ssl_private_key_retry;
        }

        if (bssl_qat_async_ctx_copy_result(ctx, out, out_len, max_out)) {
            return ssl_private_key_failure;
        }
    }

    return ssl_private_key_success;
}
#endif /* OPENSSL_IS_BORINGSSL */