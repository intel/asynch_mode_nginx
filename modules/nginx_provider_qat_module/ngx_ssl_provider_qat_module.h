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

#ifndef _NGX_SSL_PROVIDER_QAT_MODULE_H_INCLUDED_
#define _NGX_SSL_PROVIDER_QAT_MODULE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/* Core nginx cannot call into a dynamic provider's vtable, so there is no
 * action dispatch table here: the module invokes its own lifecycle functions
 * and this struct carries only the conf callbacks. */

#define NGX_SSL_PROVIDER_MODULE_ID    0x50524F56  /* "PROV" */
#define NGX_SSL_PROVIDER_CONF         0x02000000


typedef struct {
    ngx_str_t                    *name;

    void                       *(*create_conf)(ngx_cycle_t *cycle);
    char                       *(*init_conf)(ngx_cycle_t *cycle, void *conf);
} ngx_ssl_provider_module_t;


extern ngx_module_t     ngx_qat_provider_module;


#define ngx_provider_ctx_get_conf(conf_ctx, module)                          \
    (*(ngx_get_conf(conf_ctx, ngx_qat_provider_module)))[module.ctx_index];

#define ngx_provider_cycle_get_conf(cycle, module)                           \
    ((cycle)->conf_ctx[ngx_qat_provider_module.index] ?                      \
        (*(ngx_get_conf((cycle)->conf_ctx, ngx_qat_provider_module)))        \
            [module.ctx_index] :                                             \
        NULL)


#endif /* _NGX_SSL_PROVIDER_QAT_MODULE_H_INCLUDED_ */
