/*
 * Copyright (c) 2011-2012, Alexander Lyalin <alexandr.lyalin@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "ngx_http_r4x_module.h"

static void 
ngx_http_r4x_exec_return_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_http_r4x_request_ctx *ctx;
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    ctx = ngx_http_get_module_ctx(r, ngx_http_r4x_module);
    
    if(!ctx->completed) 
    {
        ctx->completed = 1;
        
        if(ctx->wait_read_body)
            r->main->count--;
        
        if(rr == NULL) // TODO: logging connection to redis db is lost
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        else
            ngx_http_r4x_send_redis_reply(r, c, rr);
    }
}

static ngx_int_t 
ngx_http_r4x_process_directive(ngx_http_request_t *r, ngx_http_r4x_directive_t *directive)
{
    ngx_uint_t i;
    ngx_int_t rc;
    ngx_http_r4x_directive_arg_t *directive_arg;
    
    directive_arg = directive->arguments_metadata.elts;
        
    if(directive->arguments_metadata.nelts > 0)
    {
        for (i = 0; i <= directive->arguments_metadata.nelts - 1; i++)
        {
            
            if(ngx_http_r4x_get_directive_argument_value(r, &directive_arg[i], 
                    &directive->raw_redis_argvs[i], &directive->raw_redis_argv_lens[i]) != NGX_OK)
                return NGX_ERROR;
        }
        
        if(directive->json_fields_hash != NULL && r->request_body != NULL) {
            
            rc = ngx_http_r4x_proces_json_fields(r->request_body->buf->pos, 
                    r->request_body->buf->last - r->request_body->buf->pos, 
                    directive->json_fields_hash, 
                    directive->raw_redis_argvs, directive->raw_redis_argv_lens);
                    
            if(rc == NGX_AGAIN)
            {
                // Process array
            }
            else if(rc != NGX_OK)
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            
        }
    }
    
    if(ngx_http_r4x_async_command_argv(directive->finalize ?  ngx_http_r4x_exec_return_callback : NULL, 
                                    r,  directive->arguments_metadata.nelts, 
                                    directive->raw_redis_argvs, directive->raw_redis_argv_lens) != NGX_OK)
    {
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

static void
ngx_http_r4x_run_directives(ngx_http_request_t *r)
{
    ngx_http_r4x_loc_conf_t *loc_conf;
    ngx_http_r4x_directive_t *directive;
    ngx_uint_t i, directive_count;
    
    loc_conf = ngx_http_get_module_loc_conf(r, ngx_http_r4x_module);

    directive = loc_conf->directives.elts;
    directive_count = loc_conf->directives.nelts;
    
    if(directive_count > 0) 
    {
        for (i = 0; i <= directive_count - 1; i++)
        {
            if(ngx_http_r4x_process_directive(r, &directive[i]) != NGX_OK)
            {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
        }
    }

    r->main->count++;
}

ngx_int_t 
ngx_http_r4x_exec_handler(ngx_http_request_t *r)
{
    ngx_http_r4x_request_ctx *ctx = ngx_http_get_module_ctx(r, ngx_http_r4x_module);
    ngx_int_t rc;
    
    if(ctx == NULL) {
        ctx = ngx_palloc(r->pool, sizeof(ngx_http_r4x_request_ctx));
        ctx->completed = 0;
        ngx_http_set_ctx(r, ctx, ngx_http_r4x_module);
    }
    
    // connect to redis db, only if connection is lost
    if(ngx_http_r4x_init_connection(ngx_http_get_module_srv_conf(r, ngx_http_r4x_module)) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
            
    // we response to 'GET'
    if ((r->method & NGX_HTTP_GET)) {
        
        // discard request body, since we don't need it here 
        if(ngx_http_discard_request_body(r) != NGX_OK)
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        
        ctx->wait_read_body = 0;
        
        ngx_http_r4x_run_directives(r);
        
        return NGX_DONE;
    }
    
    // we response to 'POST'
    if ((r->method & NGX_HTTP_POST)) {
        //ngx_http_internal_redirect
        
        r->request_body_in_single_buf = 1;
        ctx->wait_read_body = 1;
        
        rc = ngx_http_read_client_request_body(r, ngx_http_r4x_run_directives);
        
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE)
            return rc;
        
        return NGX_DONE;
    }
     
     return NGX_HTTP_NOT_ALLOWED;
}
