#ifndef __NGX_REDIS_MODULE__
#define __NGX_REDIS_MODULE__

// hiredis
#include "hiredis/async.h"
// nginx
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t redis4nginx_module;

typedef struct {
    ngx_str_t host;
    ngx_int_t port;
    ngx_str_t startup_script;
} redis4nginx_srv_conf_t;

typedef struct {
    ngx_array_t cmd_arguments; // arguments for redis_command/redis_eval
    ngx_str_t script; // lua script, only for redis_eval
    char hashed_script[40]; // SHA1 hash for lua script
} redis4nginx_loc_conf_t;

typedef struct {
    char **argvs;
    size_t *argv_lens;
    size_t args_count;
} redis4nginx_ctx;

// Connect to redis db
ngx_int_t redis4nginx_init_connection(redis4nginx_srv_conf_t *serv_conf);

// Execute redis command with format command
ngx_int_t redis4nginx_async_command(redisCallbackFn *fn, void *privdata, const char *format, ...);

// Execute redis command
ngx_int_t redis4nginx_async_command_argv(redisCallbackFn *fn, void *privdata, int argc, char **argv, const size_t *argvlen);

// Check the reason
ngx_int_t is_noscript_error(redisReply *reply);

// Send json response and finalize request
void redis4nginx_send_redis_reply(ngx_http_request_t *r, redisAsyncContext *c, redisReply *reply);

// Compile command arguments
char * compile_complex_values(ngx_conf_t *cf, ngx_array_t *output, ngx_uint_t start_compiled_arg, ngx_uint_t num_compiled_arg);

// Create request context and fill it by command arguments
redis4nginx_ctx* redis4nginx_get_ctx(ngx_http_request_t *r, ngx_array_t *cmd_arguments, ngx_uint_t addional_args);

// Compute sha1 hash
void redis4nginx_hash_script(char *digest, ngx_str_t *script);

char* ngx_string_to_c_string(ngx_str_t *str, ngx_pool_t *pool);

#endif
