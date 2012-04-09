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

void 
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
ngx_http_r4x_prepare_and_run_directive(ngx_http_request_t *r, ngx_http_r4x_directive_t *directive)
{
    ngx_uint_t                      i;
    ngx_array_t                     *json_fields_hashes = NULL;
    ngx_hash_t                      *json_fields_hash   = NULL;
        
    if(directive->arguments_metadata.nelts > 0)
    {
        if(r->request_body != NULL) {
            
            // parse request body
            if(ngx_http_r4x_parse_request_body_as_json(r) != NGX_OK)
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            
            // get parsed json fields
            json_fields_hashes = ngx_http_r4x_get_parser_json();
        }

        if(json_fields_hashes != NULL) {
            json_fields_hash = json_fields_hashes->elts;

            for (i = 0; i <= json_fields_hashes->nelts - 1; i++)
            {                
                if(ngx_http_r4x_run_directive(r, directive, &json_fields_hash[i]) != NGX_OK)
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                
                if(!directive->require_json_field)
                    break;
            }   
        }
        else {
            if(ngx_http_r4x_run_directive(r, directive, NULL) != NGX_OK)
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }
    
    return NGX_OK;
}

static void
ngx_http_r4x_directives_foreach(ngx_http_request_t *r)
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
            if(ngx_http_r4x_prepare_and_run_directive(r, &directive[i]) != NGX_OK)
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
        
        ngx_http_r4x_directives_foreach(r);
        
        return NGX_DONE;
    }
    
    // we response to 'POST'
    if ((r->method & NGX_HTTP_POST)) {
        //ngx_http_internal_redirect
        
        r->request_body_in_single_buf = 1;
        ctx->wait_read_body = 1;
        
        rc = ngx_http_read_client_request_body(r, ngx_http_r4x_directives_foreach);
        
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE)
            return rc;
        
        return NGX_DONE;
    }
     
     return NGX_HTTP_NOT_ALLOWED;
}
