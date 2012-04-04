#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

void redis4nginx_exec_return_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    if(rr == NULL) {
        // TODO: logging connection to redis db is lost
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
    else {
        redis4nginx_send_redis_reply(r, c, rr);
    }
}

ngx_int_t redis4nginx_exec_handler(ngx_http_request_t *r)
{     
    redis4nginx_loc_conf_t *loc_conf;
    
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);
    
    // connect to redis db, only if connection is lost
    if(redis4nginx_init_connection(ngx_http_get_module_srv_conf(r, redis4nginx_module)) != NGX_OK)
        return NGX_ERROR;
    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET))
        return NGX_HTTP_NOT_ALLOWED;

    if(redis4nginx_interate_directives(r, &loc_conf->directives, redis4nginx_process_directive) != NGX_OK)
        return NGX_ERROR;
    
    if(redis4nginx_process_directive(r, loc_conf->final_directive) != NGX_OK)
        return NGX_ERROR;

    r->main->count++;
    return NGX_DONE; 
}