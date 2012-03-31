#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"
#include "redis4nginx_adapter.h"

static char evalsha_command[] = "evalsha";
static char eval_command[] = "eval";

static void redis4nginx_eval_comleted(redisAsyncContext *c, void *repl, void *privdata);

ngx_int_t redis4nginx_eval_handler(ngx_http_request_t *r)
{     
    redis4nginx_loc_conf_t *loc_conf;
    redis4nginx_srv_conf_t *serv_conf;
    redis4nginx_ctx *ctx;
    
    serv_conf = ngx_http_get_module_srv_conf(r, redis4nginx_module);
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);

    // connect to redis db, only if connection is lost
    if(redis4nginx_init_connection(&serv_conf->host, serv_conf->port) != NGX_OK)
        return NGX_HTTP_GATEWAY_TIME_OUT;
    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET))
        return NGX_HTTP_NOT_ALLOWED;

    // create request context, arg 2 - reserve two places for EVAL and body of the LUA script 
    ctx = redis4nginx_get_ctx(r, &loc_conf->cmd_arguments, 2);
    if(ctx == NULL)
        return NGX_ERROR;
    
    ctx->argvs[0] = evalsha_command;
    ctx->argv_lens[0] = sizeof(evalsha_command) -1;

    ctx->argvs[1] = loc_conf->hashed_script;//(char*)loc_conf->script.data;
    ctx->argv_lens[1] = 40; //loc_conf->script.len;
        
    redis4nginx_async_command_argv(redis4nginx_eval_comleted, r, ctx->args_count, ctx->argvs, ctx->argv_lens);
    r->main->count++;
    
    return NGX_DONE;
}

static void redis4nginx_eval_comleted(redisAsyncContext *c, void *repl, void *privdata)
{
    redis4nginx_loc_conf_t *loc_conf;
    redis4nginx_ctx *ctx;
    ngx_http_request_t *r = privdata;
    redisReply *rr = repl;
    
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);
    ctx = redis4nginx_get_ctx(r, &loc_conf->cmd_arguments, 2);
    
    //TODO: if(ctx == NULL){}
    
    if(rr->type == REDIS_REPLY_ERROR) {
        if(strcmp(rr->str, "NOSCRIPT No matching script. Please use EVAL.") == 0) {
            ctx->argvs[0] = eval_command;
            ctx->argv_lens[0] = sizeof(eval_command) -1;

            ctx->argvs[1] = (char*)loc_conf->script.data;
            ctx->argv_lens[1] = loc_conf->script.len;
    
            redis4nginx_async_command_argv(redis4nginx_eval_comleted, r, ctx->args_count, ctx->argvs, ctx->argv_lens);
        }         
    }
    else {
        redis4nginx_send_redis_reply(r, c, rr);
    }
}

char *redis4nginx_eval_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    redis4nginx_loc_conf_t *loc_conf = conf;
    ngx_http_core_loc_conf_t *core_conf;
    ngx_str_t * script;

	core_conf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
	core_conf->handler = &redis4nginx_eval_handler;
    
    script = cf->args->elts;
    loc_conf->script.data = script[1].data;
    loc_conf->script.len = script[1].len;
    
    // compute sha1 hash
    redis4nginx_hash_script(loc_conf->hashed_script, &loc_conf->script);
    
    if(ngx_array_init(&loc_conf->cmd_arguments, 
                        cf->pool, 
                        cf->args->nelts - 2, //without redis_command and lua script
                        sizeof(ngx_http_complex_value_t)) != NGX_OK) 
    {
        return NGX_CONF_ERROR;
    }
    
    return compile_complex_values(cf, &loc_conf->cmd_arguments, 2, cf->args->nelts);
    
}
