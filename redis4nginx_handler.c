#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

void redis4nginx_exec_return_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    redis4nginx_ctx *ctx;
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    ctx = ngx_http_get_module_ctx(r, redis4nginx_module);
    
    if(!ctx->completed) 
    {
        ctx->completed = 1;
        
        if(rr == NULL) // TODO: logging connection to redis db is lost
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        else
            redis4nginx_send_redis_reply(r, c, rr);
    }
}

ngx_int_t redis4nginx_process_directive(ngx_http_request_t *r, redis4nginx_directive_t *directive)
{
    ngx_uint_t i;
    redis4nginx_directive_arg_t *directive_arg;
    ngx_str_t value;
    char **argvs;
    size_t *argv_lens;
    
    directive_arg = directive->arguments.elts;
    argvs = ngx_palloc(r->pool, sizeof(const char *) * (directive->arguments.nelts));
    argv_lens = ngx_palloc(r->pool, sizeof(size_t) * (directive->arguments.nelts));
        
    if(directive->arguments.nelts > 0)
    {
        for (i = 0; i <= directive->arguments.nelts - 1; i++)
        {
            
            if(redis4nginx_get_directive_argument_value(r, &directive_arg[i], &value) != NGX_OK)
                return NGX_ERROR;

            argvs[i] = (char *)value.data;
            argv_lens[i] = value.len;
        }
    }
    
    if(redis4nginx_async_command_argv(directive->finalize ?  redis4nginx_exec_return_callback : NULL, 
                                    r,  directive->arguments.nelts, argvs, argv_lens) != NGX_OK)
    {
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

ngx_int_t redis4nginx_exec_handler(ngx_http_request_t *r)
{     
    redis4nginx_loc_conf_t *loc_conf;
    redis4nginx_ctx *ctx;
    redis4nginx_directive_t *directive;
    ngx_uint_t i, directive_count;
        
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);
    
    ctx = ngx_http_get_module_ctx(r, redis4nginx_module);
    
    directive = loc_conf->directives.elts;
    directive_count = loc_conf->directives.nelts;
    
    if(ctx == NULL) {
        ctx = ngx_palloc(r->pool, sizeof(redis4nginx_ctx));
        ctx->completed = 0;
        ngx_http_set_ctx(r, ctx, redis4nginx_module);
    }
    
    // connect to redis db, only if connection is lost
    if(redis4nginx_init_connection(ngx_http_get_module_srv_conf(r, redis4nginx_module)) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
            
    // we response to 'GET' and 'HEAD' requests only 
    //if (!(r->method & NGX_HTTP_GET))
        //return NGX_HTTP_NOT_ALLOWED;

    // discard request body, since we don't need it here 
    //if(ngx_http_discard_request_body(r) != NGX_OK)
        //return NGX_HTTP_INTERNAL_SERVER_ERROR;
         
    
    //ngx_http_read_client_request_body(r, NULL);
    
    if(directive_count > 0) 
    {
        for (i = 0; i <= directive_count - 1; i++)
        {
            if(redis4nginx_process_directive(r, &directive[i]) != NGX_OK)
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    r->main->count++;
    return NGX_DONE; 
}