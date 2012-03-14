#ifndef __NGX_REDIS_MODULE__
#define __NGX_REDIS_MODULE__

// hiredis headers
#include "hiredis/hiredis.h"
#include "hiredis/async.h"

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

// context struct in the request handling cycle, holding
// the current states of the command evaluator
typedef struct redis4nginx_ctx_t {
    redisAsyncContext *async;
    ngx_connection_t *conn;
    unsigned connected:1;
} redis4nginx_ctx_t;

typedef struct {
    /* elements of the following arrays are of type
     * ngx_http_echo_cmd_t */

    ngx_str_t *lua_script;
} redis4nginx_loc_conf_t;

redis4nginx_ctx_t *redis4nginx_create_ctx(ngx_http_request_t *r);
int redis4nginx_command(redis4nginx_ctx_t *ctx, redisCallbackFn *fn, void *privdata, const char *format, ...);

#endif
