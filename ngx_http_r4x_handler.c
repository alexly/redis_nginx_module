#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

void 
redis4nginx_exec_return_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    redis4nginx_ctx *ctx;
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    ctx = ngx_http_get_module_ctx(r, redis4nginx_module);
    
    if(!ctx->completed) 
    {
        ctx->completed = 1;
        
        if(ctx->wait_read_body)
            r->main->count--;
        
        if(rr == NULL) // TODO: logging connection to redis db is lost
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        else
            redis4nginx_send_redis_reply(r, c, rr);
    }
}

ngx_int_t 
redis4nginx_process_directive(ngx_http_request_t *r, redis4nginx_directive_t *directive)
{
    ngx_uint_t i;
    redis4nginx_directive_arg_t *directive_arg;
    
    directive_arg = directive->arguments_metadata.elts;
        
    if(directive->arguments_metadata.nelts > 0)
    {
        for (i = 0; i <= directive->arguments_metadata.nelts - 1; i++)
        {
            
            if(redis4nginx_get_directive_argument_value(r, &directive_arg[i], 
                    &directive->raw_redis_argvs[i], &directive->raw_redis_argv_lens[i]) != NGX_OK)
                return NGX_ERROR;
        }
        
        if(directive->json_fields_hash != NULL && r->request_body != NULL) {
            
            if(redis4nginx_proces_json_fields(r->request_body->buf->pos, 
                    r->request_body->buf->last - r->request_body->buf->pos, 
                    directive->json_fields_hash, 
                    directive->raw_redis_argvs, directive->raw_redis_argv_lens) != NGX_OK)
            {
                return NGX_ERROR;
            }
        }
    }
    
    if(redis4nginx_async_command_argv(directive->finalize ?  redis4nginx_exec_return_callback : NULL, 
                                    r,  directive->arguments_metadata.nelts, 
                                    directive->raw_redis_argvs, directive->raw_redis_argv_lens) != NGX_OK)
    {
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

static void
redis4nginx_run_directives(ngx_http_request_t *r)
{
    redis4nginx_loc_conf_t *loc_conf;
    redis4nginx_directive_t *directive;
    ngx_uint_t i, directive_count;
    
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);

    directive = loc_conf->directives.elts;
    directive_count = loc_conf->directives.nelts;
    
    if(directive_count > 0) 
    {
        for (i = 0; i <= directive_count - 1; i++)
        {
            if(redis4nginx_process_directive(r, &directive[i]) != NGX_OK)
            {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
        }
    }

    r->main->count++;
}

ngx_int_t 
redis4nginx_exec_handler(ngx_http_request_t *r)
{
    redis4nginx_ctx *ctx = ngx_http_get_module_ctx(r, redis4nginx_module);
    ngx_int_t rc;
    
    if(ctx == NULL) {
        ctx = ngx_palloc(r->pool, sizeof(redis4nginx_ctx));
        ctx->completed = 0;
        ngx_http_set_ctx(r, ctx, redis4nginx_module);
    }
    
    // connect to redis db, only if connection is lost
    if(redis4nginx_init_connection(ngx_http_get_module_srv_conf(r, redis4nginx_module)) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
            
    // we response to 'GET'
    if ((r->method & NGX_HTTP_GET)) {
        
        // discard request body, since we don't need it here 
        if(ngx_http_discard_request_body(r) != NGX_OK)
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        
        ctx->wait_read_body = 0;
        
        redis4nginx_run_directives(r);
        
        return NGX_DONE;
    }
    
    // we response to 'POST'
    if ((r->method & NGX_HTTP_POST)) {
        //ngx_http_internal_redirect
        
        r->request_body_in_single_buf = 1;
        ctx->wait_read_body = 1;
        
        rc = ngx_http_read_client_request_body(r, redis4nginx_run_directives);
        
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE)
            return rc;
        
        return NGX_DONE;
    }
     
     return NGX_HTTP_NOT_ALLOWED;
}