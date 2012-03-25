#ifndef __NGX_REDIS_MODULE__
#define __NGX_REDIS_MODULE__

// hiredis headers
#include "hiredis/async.h"

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t redis4nginx_module;

typedef struct {
    /* elements of the following arrays are of type
     * ngx_http_echo_cmd_t */
    ngx_array_t     *handler_cmds;
} redis4nginx_loc_conf_t;

// Connect to redis db
ngx_int_t redis4nginx_init_connection();
int redis4nginx_command(redisCallbackFn *fn, void *privdata, const char *format, ...);

// Send json response and finalize request
void redis4nginx_send_json(redisAsyncContext *c, void *repl, void *privdata);
void redis4nginx_get(ngx_http_request_t *r);

#endif
