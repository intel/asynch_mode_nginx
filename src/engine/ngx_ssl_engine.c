/* ====================================================================
 *
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2016-2019 Intel Corporation.
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


static char *ngx_ssl_engine_block(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_ssl_engine_process_init(ngx_cycle_t *cycle);
static void ngx_ssl_engine_master_exit(ngx_cycle_t *cycle);

static char *ngx_ssl_engine_use(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_ssl_engine_default_algorithms(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

static void *ngx_ssl_engine_core_create_conf(ngx_cycle_t *cycle);

static void ngx_ssl_engine_table_cleanup(ENGINE *e);

/* indicating whether to use ssl engine: 0 or 1 */
ngx_uint_t                  ngx_use_ssl_engine;

ngx_ssl_engine_actions_t    ngx_ssl_engine_actions;
ngx_uint_t                  ngx_ssl_engine_enable_heuristic_polling;


static ngx_command_t  ngx_ssl_engine_commands[] = {

    { ngx_string("ssl_engine"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_ssl_engine_block,
      0,
      0,
      NULL },

      ngx_null_command
};

static ngx_core_module_t  ngx_ssl_engine_module_ctx = {
    ngx_string("ssl_engine"),
    NULL,
    NULL
};

ngx_module_t  ngx_ssl_engine_module = {
    NGX_MODULE_V1,
    &ngx_ssl_engine_module_ctx,            /* module context */
    ngx_ssl_engine_commands,               /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_ssl_engine_process_init,           /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    ngx_ssl_engine_master_exit,            /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_str_t ssl_engine_core_name = ngx_string("ssl_engine_core");

static ngx_command_t  ngx_ssl_engine_core_commands[] = {

    { ngx_string("use_engine"),
      NGX_SSL_ENGINE_CONF|NGX_CONF_TAKE1,
      ngx_ssl_engine_use,
      0,
      0,
      NULL },

    { ngx_string("default_algorithms"),
      NGX_SSL_ENGINE_CONF|NGX_CONF_1MORE,
      ngx_ssl_engine_default_algorithms,
      0,
      offsetof(ngx_ssl_engine_conf_t, default_algorithms),
      NULL },

      ngx_null_command
};

ngx_ssl_engine_module_t  ngx_ssl_engine_core_module_ctx = {
    &ssl_engine_core_name,
    ngx_ssl_engine_core_create_conf,        /* create configuration */
    NULL,                                   /* init configuration */

    { NULL, NULL, NULL, NULL, NULL }
};

ngx_module_t  ngx_ssl_engine_core_module = {
    NGX_MODULE_V1,
    &ngx_ssl_engine_core_module_ctx,        /* module context */
    ngx_ssl_engine_core_commands,           /* module directives */
    NGX_SSL_ENGINE_MODULE,                  /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


/* OpenSSL will register all dynamic engines into its global engine tables
 * To support dynamicaly update engine configuraion such as default_algorithm
 * It needs to unregister all algorithms before applying new configuration
 * Especially for remove some algorithms from previous configuation
 */
static void
ngx_ssl_engine_table_cleanup(ENGINE *e)
{
    ENGINE_unregister_ciphers(e);
    ENGINE_unregister_digests(e);
    ENGINE_unregister_pkey_meths(e);
    ENGINE_unregister_DSA(e);
    ENGINE_unregister_EC(e);
    ENGINE_unregister_RSA(e);
    ENGINE_unregister_DH(e);
}

static ngx_int_t
ngx_ssl_engine_set(ngx_cycle_t *cycle)
{
    ngx_ssl_engine_conf_t *secf;

    ENGINE      *e;
    ngx_str_t   *value;
    ngx_uint_t   i;


    if (ngx_ssl_engine_send_ctrl(cycle) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "ssl engine send ctrl failed");
        return NGX_ERROR;
    }

    secf = ngx_ssl_engine_get_conf(cycle->conf_ctx, ngx_ssl_engine_core_module);

    e = ENGINE_by_id((const char *) secf->ssl_engine_id.data);

    if (e == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "ENGINE_by_id(\"%V\") failed", &secf->ssl_engine_id);
        return NGX_ERROR;
    }

    /* Cleanup OpenSSL engine tables */
    ngx_ssl_engine_table_cleanup(e);

    if (!ENGINE_init(e)) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "engine init failed");
        ENGINE_free(e);
        return NGX_ERROR;
    }

    if (secf->default_algorithms != NGX_CONF_UNSET_PTR) {
        value = secf->default_algorithms->elts;
        for (i = 0; i < secf->default_algorithms->nelts; i++) {
            if (!ENGINE_set_default_string(e, (const char *) value[i].data)) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "ENGINE_set_default_string failed");
                ENGINE_free(e);
                return NGX_ERROR;
            }
        }
    } else {
        if (!ENGINE_set_default(e, ENGINE_METHOD_ALL)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "ENGINE_set_default failed");
            ENGINE_free(e);
            return NGX_ERROR;
        }
    }

    ENGINE_free(e);
    return NGX_OK;
}


static char *
ngx_ssl_engine_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char           *rv;
    void         ***ctx;
    ngx_uint_t      i;
    ngx_uint_t      ngx_ssl_engine_max_module;
    ngx_conf_t      pcf;

    ngx_ssl_engine_module_t     *m;

    if (*(void **) conf) {
        return "is duplicate";
    }

    /* count the number of the engine modules and set up their indices */

    ngx_ssl_engine_max_module = ngx_count_modules(cf->cycle,
                                                  NGX_SSL_ENGINE_MODULE);

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cf->pool, ngx_ssl_engine_max_module * sizeof(void *));
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(void **) conf = ctx;

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_SSL_ENGINE_MODULE) {
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
    cf->module_type = NGX_SSL_ENGINE_MODULE;
    cf->cmd_type = NGX_SSL_ENGINE_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_SSL_ENGINE_MODULE) {
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

    /* ssl engine set must before parsing http conf */
    if (ngx_use_ssl_engine && !cf->cycle->no_ssl_init) {
        if (ngx_ssl_engine_set(cf->cycle) != NGX_OK) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "ssl engine set failed");
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_ssl_engine_process_init(ngx_cycle_t *cycle)
{
    if (ngx_use_ssl_engine) {
        if (ngx_ssl_engine_register_handler(cycle) != NGX_OK) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "ssl engine register handler failed");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static void
ngx_ssl_engine_master_exit(ngx_cycle_t *cycle)
{
#if OPENSSL_VERSION_NUMBER < 0x10100003L
    EVP_cleanup();
#endif
    ENGINE_cleanup();
}


static char *
ngx_ssl_engine_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_ssl_engine_conf_t  *secf = conf;

    ngx_int_t                 m;
    ngx_str_t                *value;
    ngx_ssl_engine_module_t  *module;

    if (secf->ssl_engine_id.len != 0) {
        return "is duplicate";
    }

    value = cf->args->elts;

    for (m = 0; cf->cycle->modules[m]; m++) {
        if (cf->cycle->modules[m]->type != NGX_SSL_ENGINE_MODULE) {
            continue;
        }

        module = cf->cycle->modules[m]->ctx;
        if (module->name->len == value[1].len) {
            if (ngx_strcmp(module->name->data, value[1].data) == 0) {

                secf->ssl_engine_id = value[1];

                ngx_use_ssl_engine = 1;
                ngx_ssl_engine_actions = module->actions;

                return NGX_CONF_OK;
            }
        }
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid engine type \"%V\"", &value[1]);

    return NGX_CONF_ERROR;
}


static char *
ngx_ssl_engine_default_algorithms(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
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
ngx_ssl_engine_core_create_conf(ngx_cycle_t *cycle)
{
    ngx_ssl_engine_conf_t  *secf;

    secf = ngx_pcalloc(cycle->pool, sizeof(ngx_ssl_engine_conf_t));
    if (secf == NULL) {
        return NULL;
    }

    /* ngx_pcalloc makes secf->ssl_engine_id.len = 0 */
    ngx_use_ssl_engine = 0;
    ngx_ssl_engine_enable_heuristic_polling = 0;
    secf->default_algorithms = NGX_CONF_UNSET_PTR;

    return secf;
}
