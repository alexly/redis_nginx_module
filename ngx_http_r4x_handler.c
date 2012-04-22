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
ngx_http_r4x_process_redis_reply(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_http_r4x_request_ctx *ctx;
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    ctx = ngx_http_get_module_ctx(r, ngx_http_r4x_module);
    
    if(rr == NULL) // TODO: logging connection to redis db is lost
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    
    if(!ctx->completed) 
    {
        ctx->completed = 1;
        
        if(ctx->wait_read_body)
            r->main->count--;
        
        ngx_http_r4x_send_redis_reply(r, c, rr);
    }
}

static ngx_int_t 
ngx_http_r4x_process_directive(ngx_http_request_t *r, ngx_http_r4x_directive_t *directive, 
        ngx_http_r4x_parsed_json *parsed, ngx_http_r4x_redis_node_t *node)
{
    ngx_uint_t                      i;
    ngx_str_t                       temp;
    ngx_http_r4x_directive_arg_t    *directive_arg;
    
    directive_arg = directive->arguments.elts;
    
    if(directive->arguments.nelts > 0)
    {
        // prepare none json arguments
        for (i = 0; i <= directive->arguments.nelts - 1; i++)
        {            
            switch(directive_arg[i].type)
            {
                case REDIS4NGINX_JSON_FIELD_INDEX_ARG:
                    if(ngx_http_r4x_find_by_index(parsed, directive_arg[i].index, &temp) != NGX_OK)
                        return NGX_ERROR;
                    break;
                case REDIS4NGINX_JSON_FIELD_NAME_ARG:
                    if(ngx_http_r4x_find_by_key(parsed, &directive_arg[i].value, &temp) != NGX_OK)
                        return NGX_ERROR;
                    break;
                case REDIS4NGINX_COMPILIED_ARG:
                    if (ngx_http_complex_value(r, directive_arg[i].compilied, &temp) != NGX_OK)
                        return NGX_ERROR;
                    break;
                case REDIS4NGINX_STRING_ARG:
                    temp.data = directive_arg[i].value.data;
                    temp.len = directive_arg[i].value.len;
                    break;
            };
            
            directive->cmd_argvs[i] = (char*)temp.data;
            directive->cmd_argv_lens[i] = temp.len;
        }
    }
    
    if(ngx_http_r4x_async_command_argv(node, directive->process_reply, r,  directive->arguments.nelts, 
                                    directive->cmd_argvs, directive->cmd_argv_lens) != NGX_OK)
    {
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

static void
ngx_http_r4x_foreach_directives(ngx_http_request_t *r)
{
    ngx_http_r4x_loc_conf_t     *loc_conf;
    ngx_http_r4x_directive_t    *directive;
    ngx_uint_t                  i, directive_count;
    ngx_http_r4x_parsed_json    parsed_json;
    ngx_http_r4x_srv_conf_t     *srv_conf;
    
    srv_conf = ngx_http_get_module_srv_conf(r, ngx_http_r4x_module);
    
    loc_conf = ngx_http_get_module_loc_conf(r, ngx_http_r4x_module);

    directive = loc_conf->directives.elts;
    directive_count = loc_conf->directives.nelts;
    
    if(loc_conf->require_json_field) {
        if(ngx_http_r4x_parse_json_request_body(r, &parsed_json) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    }
    
    if(directive_count > 0)
    {
        for (i = 0; i <= directive_count - 1; i++)
        {
            if(ngx_http_r4x_process_directive(r, &directive[i], &parsed_json, srv_conf->master) != NGX_OK)
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
    ngx_int_t                   rc;
    ngx_http_r4x_srv_conf_t     *srv_conf;
    ngx_http_r4x_request_ctx    *ctx = ngx_http_get_module_ctx(r, ngx_http_r4x_module);
    
    srv_conf = ngx_http_get_module_srv_conf(r, ngx_http_r4x_module);
    
    if(ctx == NULL) {
        ctx = ngx_palloc(r->pool, sizeof(ngx_http_r4x_request_ctx));
        ctx->completed = 0;
        ngx_http_set_ctx(r, ctx, ngx_http_r4x_module);
    }
    
    // connect to redis db, only if connection is lost
    if(ngx_http_r4x_init_connection(srv_conf->master) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
            
    // we response to 'GET'
    if ((r->method & NGX_HTTP_GET)) {
        
        // discard request body, since we don't need it here 
        if(ngx_http_discard_request_body(r) != NGX_OK)
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        
        ctx->wait_read_body = 0;
        
        ngx_http_r4x_foreach_directives(r);
        
        return NGX_DONE;
    }
    
    // we response to 'POST'
    if ((r->method & NGX_HTTP_POST)) {
        //ngx_http_internal_redirect
        
        r->request_body_in_single_buf = 1;
        ctx->wait_read_body = 1;
        
        rc = ngx_http_read_client_request_body(r, ngx_http_r4x_foreach_directives);
        
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE)
            return rc;
        
        return NGX_DONE;
    }
     
     return NGX_HTTP_NOT_ALLOWED;
}
