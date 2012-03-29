#ifndef __NGX_REDIS_MODULE__
#define __NGX_REDIS_MODULE__

// hiredis headers
#include "hiredis/async.h"

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t redis4nginx_module;

typedef struct {
	ngx_str_t host;
	ngx_int_t port;

} redis4nginx_srv_conf_t;

typedef struct {
    ngx_array_t *queries; /* for redis2_query */
} redis4nginx_loc_conf_t;

// EVAL
ngx_int_t redis4nginx_eval_handler(ngx_http_request_t *r);
void redis4nginx_eval_callback(redisAsyncContext *c, void *repl, void *privdata);
char *redis4nginx_eval_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

// Send json response and finalize request
void redis4nginx_send_json(ngx_http_request_t *r, redisAsyncContext *c, redisReply *reply);

#endif
