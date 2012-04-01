#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

static void redis4nginx_command_callback(redisAsyncContext *c, void *repl, void *privdata);

ngx_int_t redis4nginx_command_handler(ngx_http_request_t *r)
{     
    redis4nginx_loc_conf_t *loc_conf;
    redis4nginx_ctx *ctx;
    
    // connect to redis db, only if connection is lost
    if(redis4nginx_init_connection(ngx_http_get_module_srv_conf(r, redis4nginx_module)) != NGX_OK)
        return NGX_ERROR;
    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET))
        return NGX_HTTP_NOT_ALLOWED;
    
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);

    // create request context
    ctx = redis4nginx_get_ctx(r, &loc_conf->cmd_arguments, 0);
    
    if(ctx == NULL)
        return NGX_ERROR;
        
    redis4nginx_async_command_argv(redis4nginx_command_callback, r, 
                                ctx->args_count, ctx->argvs, ctx->argv_lens);
    r->main->count++;
    
    return NGX_DONE;
}

static void redis4nginx_command_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    if(rr == NULL) {
        // connection to redis db is lost
        // TODO: logging
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
    else {
        redis4nginx_send_redis_reply(r, c, rr);
    }
}