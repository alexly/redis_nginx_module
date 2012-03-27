#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"
#include "redis4nginx_adapter.h"

ngx_int_t redis4nginx_eval_handler(ngx_http_request_t *r)
{     
    //redis4nginx_loc_conf_t *elcf;
    redis4nginx_srv_conf_t* escf;
    
    escf = ngx_http_get_module_srv_conf(r, redis4nginx_module);
    
    //elcf = ngx_http_get_module_loc_conf(r, redis4nginx_module);

    redis4nginx_init_connection(&escf->host, escf->port);
    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET)) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    redis4nginx_async_command(redis4nginx_eval_callback, r, "get key");
    r->main->count++;
    
    return NGX_DONE;
}

void redis4nginx_eval_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_http_request_t *r = privdata;
    redisReply* rr = repl;
    
    redis4nginx_send_json(r, c, rr);
}

char *redis4nginx_eval_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{     
	redis4nginx_loc_conf_t *mslcf = conf;
	ngx_http_core_loc_conf_t *mlcf;
	ngx_str_t *query;
	ngx_uint_t n;
	ngx_http_script_compile_t sc;
	ngx_str_t *value;
	mlcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
	mlcf->handler = &redis4nginx_eval_handler;
    
	value = cf->args->elts;
	query = &value[1];
    
    n = ngx_http_script_variables_count(query);

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    
	sc.cf = cf;
	sc.source = query;
	sc.lengths = &mslcf->query_lengths;
	sc.values = &mslcf->query_values;
	sc.variables = n;
	sc.complete_lengths = 1;
	sc.complete_values = 1;

	//if (ngx_http_script_compile(&sc) != NGX_OK)
		//return NGX_CONF_ERROR;
            
    return NGX_CONF_OK;
}