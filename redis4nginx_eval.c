#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "sha1.h"
#include "redis4nginx_module.h"
#include "redis4nginx_adapter.h"

static void redis4nginx_eval_callback(redisAsyncContext *c, void *repl, void *privdata);

/* Hash the scripit into a SHA1 digest. We use this as Lua function name.
 * Digest should point to a 41 bytes buffer: 40 for SHA1 converted into an
 * hexadecimal number, plus 1 byte for null term. */
void hash_script(char *digest, ngx_str_t *script) {
    SHA1_CTX ctx;
    unsigned char hash[20];
    char *cset = "0123456789abcdef";
    int j;

    SHA1Init(&ctx);
    SHA1Update(&ctx,(unsigned char*)script->data, script->len);
    SHA1Final(hash,&ctx);

    for (j = 0; j < 20; j++) {
        digest[j*2] = cset[((hash[j]&0xF0)>>4)];
        digest[j*2+1] = cset[(hash[j]&0xF)];
    }
    digest[40] = '\0';
}


ngx_int_t redis4nginx_eval_handler(ngx_http_request_t *r)
{     
    redis4nginx_loc_conf_t *loc_conf;
    redis4nginx_srv_conf_t *serv_conf;
    ngx_uint_t i, argv_count;
    ngx_http_complex_value_t *compiled_values;
    ngx_str_t value;
    char **argv;
    size_t *argvlen;
    
    serv_conf = ngx_http_get_module_srv_conf(r, redis4nginx_module);
    
    loc_conf = ngx_http_get_module_loc_conf(r, redis4nginx_module);

    // connect to redis db, only if connection is lost
    if(redis4nginx_init_connection(&serv_conf->host, serv_conf->port) != NGX_OK)
        return NGX_HTTP_GATEWAY_TIME_OUT;
    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET))
        return NGX_HTTP_NOT_ALLOWED;

    argv_count = loc_conf->cmd_arguments.nelts;
    compiled_values = loc_conf->cmd_arguments.elts;
    
    argv = ngx_palloc(r->pool, sizeof(const char *) * (argv_count + 2));
    argvlen = ngx_palloc(r->pool, sizeof(size_t) * (argv_count + 2));
     
    argv[0] = "eval";
    argvlen[0] = 4;

    argv[1] = (char*)loc_conf->script.data;
    argvlen[1] =  loc_conf->script.len;
    
    if(argv_count > 0) 
    {
        for (i = 0; i <= argv_count - 1; i++) {
            if (ngx_http_complex_value(r, &compiled_values[i], &value) != NGX_OK)
                return NGX_ERROR;

            argv[i+2] = (char *)value.data;
            argvlen[i+2] = value.len;
        }   
    }
        
    redis4nginx_async_command_argv(redis4nginx_eval_callback, r, argv_count + 2, (const char**)argv, argvlen);
    r->main->count++;
    
    return NGX_DONE;
}

static void redis4nginx_eval_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_http_request_t *r = privdata;
    redisReply *rr = repl;
    
    redis4nginx_send_redis_reply(r, c, rr);
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
    hash_script(loc_conf->hashed_script, &loc_conf->script);
    
    if(ngx_array_init(&loc_conf->cmd_arguments, 
                        cf->pool, 
                        cf->args->nelts - 2, //without redis_command and lua script
                        sizeof(ngx_http_complex_value_t)) != NGX_OK) 
    {
        return NGX_CONF_ERROR;
    }
    
    return compile_complex_values(cf, &loc_conf->cmd_arguments, 2, cf->args->nelts);
    
}
