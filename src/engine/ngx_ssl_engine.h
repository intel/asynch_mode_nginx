/* ====================================================================
 *
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2016-2025 Intel Corporation.
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

#ifndef _NGX_ENGINE_H_INCLUDED_
#define _NGX_ENGINE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#ifdef OPENSSL_IS_BORINGSSL
/* Export essential interfaces for external calling */
typedef struct {
    int       (*wait_for_async)(void *ssl);
    int       (*ssl_want_async)(const void *ssl);
    int       (*get_changed_fds)(void *ssl, int *addfd,
                size_t *numaddfds, int *delfd, size_t *numdelfds);
    int       (*set_default_string)(const char *def_list);
    ngx_int_t (*async_ctx_finish)(void *ssl);
    ngx_int_t (*save_async_flag)(void *ssl, void *ssl_async);
} ngx_bssl_qat_ex_actions_t;
#endif

typedef struct {
    ngx_int_t  (*init)(ngx_cycle_t *cycle);
    ngx_int_t  (*send_ctrl)(ngx_cycle_t *cycle);
    ngx_int_t  (*register_handler)(ngx_cycle_t *cycle);
    ngx_int_t  (*release)(ngx_cycle_t *cycle);
    void       (*heuristic_poll)(ngx_log_t *log);
#ifdef OPENSSL_IS_BORINGSSL
    ngx_bssl_qat_ex_actions_t ex_actions ;
#endif
} ngx_ssl_engine_actions_t;

extern ngx_uint_t                       ngx_use_ssl_engine;
extern ngx_ssl_engine_actions_t         ngx_ssl_engine_actions;
extern ngx_uint_t                       ngx_ssl_engine_enable_heuristic_polling;
extern ngx_flag_t                       ngx_ssl_engine_reload_processed;

#define ngx_ssl_engine_init             ngx_ssl_engine_actions.init
#define ngx_ssl_engine_send_ctrl        ngx_ssl_engine_actions.send_ctrl
#define ngx_ssl_engine_register_handler ngx_ssl_engine_actions.register_handler
#define ngx_ssl_engine_release          ngx_ssl_engine_actions.release
#define ngx_ssl_engine_heuristic_poll   ngx_ssl_engine_actions.heuristic_poll

#ifdef OPENSSL_IS_BORINGSSL
/* No engine, no async mode for BoringSSL enabled */
#define ngx_ssl_engine_support_async    ngx_use_ssl_engine
#define ngx_ssl_engine_ex_get_changed_async_fds \
    ngx_ssl_engine_actions.ex_actions.get_changed_fds
#define ngx_ssl_engine_ex_async_ctx_exit \
    ngx_ssl_engine_actions.ex_actions.async_ctx_finish
#define ngx_ssl_waiting_for_async(c) \
    ngx_ssl_engine_actions.ex_actions.wait_for_async(c->ssl->connection)
#define SSL_want_async ngx_ssl_engine_actions.ex_actions.ssl_want_async
#define ngx_ssl_engine_ex_set_async_mode \
    ngx_ssl_engine_actions.ex_actions.save_async_flag
#define ngx_ssl_engine_ex_set_def_str    \
    ngx_ssl_engine_actions.ex_actions.set_default_string
#else
/* Empty functions for OpenSSL */
#define ngx_ssl_engine_support_async
#define ngx_ssl_engine_ex_get_changed_async_fds SSL_get_changed_async_fds
#define ngx_ssl_engine_ex_async_ctx_exit(ssl)
/* Defined in OpenSSL libraries
 * #define ngx_ssl_waiting_for_async(c)
 * #define SSL_want_async
 */
#define ngx_ssl_waiting_for_async(c) SSL_want_async((c)->ssl->connection)

#define ngx_ssl_engine_ex_set_async_mode(ssl,mode)
#endif

#define NGX_SSL_ENGINE_MODULE           0x55555555
#define NGX_SSL_ENGINE_CONF             0x02000000
#define NGX_SSL_ENGINE_SUB_CONF         0x04000000

typedef struct {
    ngx_str_t        ssl_engine_id;
    ngx_array_t     *default_algorithms;
} ngx_ssl_engine_conf_t;


typedef struct {
    ngx_str_t                  *name;

    void                     *(*create_conf)(ngx_cycle_t *cycle);
    char                     *(*init_conf)(ngx_cycle_t *cycle, void *conf);

    ngx_ssl_engine_actions_t    actions ;
} ngx_ssl_engine_module_t;


extern ngx_module_t     ngx_ssl_engine_module;
extern ngx_module_t     ngx_ssl_engine_core_module;


#define ngx_engine_ctx_get_conf(conf_ctx, module)                          \
    (*(ngx_get_conf(conf_ctx, ngx_ssl_engine_module))) [module.ctx_index];

#define ngx_engine_cycle_get_conf(cycle, module)                           \
    (cycle->conf_ctx[ngx_ssl_engine_module.index] ?                        \
        (*(ngx_get_conf(cycle->conf_ctx, ngx_ssl_engine_module)))          \
            [module.ctx_index]:                                            \
        NULL)

char * ngx_ssl_engine_unload_check(ngx_cycle_t *cycle);

#endif /* _NGX_ENGINE_H_INCLUDED_ */
