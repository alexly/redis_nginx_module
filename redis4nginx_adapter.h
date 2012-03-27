#ifndef __REDIS4NGINX_ADAPTER__
#define __REDIS4NGINX_ADAPTER__

// hiredis headers
#include "hiredis/async.h"

// Connect to redis db
ngx_int_t redis4nginx_init_connection(ngx_str_t* host, ngx_int_t port);

// Execute redis command
int redis4nginx_async_command(redisCallbackFn *fn, void *privdata, const char *format, ...);

#endif
