#ifndef __REDIS4NGINX_MODULE__
#define __REDIS4NGINX_MODULE__

#define REDIS4NGINX_JSON_FIELD_ARG      0
#define REDIS4NGINX_COMPILIED_ARG       1
#define REDIS4NGINX_STRING_ARG          2

// hiredis
#include "hiredis/async.h"
#include "hiredis/dict.h"
// nginx
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t ngx_http_r4x_module;

typedef struct {
    ngx_uint_t type;
    union {
        ngx_str_t                   string_value;
        ngx_http_complex_value_t    *compilied;
    };
} ngx_http_r4x_directive_arg_t;

typedef struct {
    ngx_array_t                     arguments_metadata;    // metadata for redis arguments
    unsigned                        finalize:1;            // 1 - finalize request, 0 - only exec redis command
    char                            **raw_redis_argvs;
    size_t                          *raw_redis_argv_lens;
    union {
        ngx_hash_t*                 json_fields_hash;
        ngx_hash_keys_arrays_t      *hash_elements;
    };
} ngx_http_r4x_directive_t;

typedef struct {
    ngx_array_t                     directives;
} ngx_http_r4x_loc_conf_t;

typedef struct {
    ngx_str_t                       host;
    ngx_int_t                       port;
    ngx_str_t                       startup_script;
    ngx_array_t                     *startup_scripts;
} ngx_http_r4x_srv_conf_t;

typedef struct {
    unsigned                        completed:1;
    unsigned                        wait_read_body:1;
} ngx_http_r4x_request_ctx;

// Redis DB API
ngx_int_t
ngx_http_r4x_init_connection(ngx_http_r4x_srv_conf_t *serv_conf);

ngx_int_t
ngx_http_r4x_async_command(redisCallbackFn *fn, void *privdata, const char *format, ...);

ngx_int_t
ngx_http_r4x_async_command_argv(redisCallbackFn *fn, void *privdata, 
                            int argc, char **argv, const size_t *argvlen);

// HTTP utilities
void
ngx_http_r4x_send_redis_reply(ngx_http_request_t *r, redisAsyncContext *c, 
                            redisReply *reply);

// Directive utilities
ngx_int_t
ngx_http_r4x_get_directive_argument_value(ngx_http_request_t *r, 
        ngx_http_r4x_directive_arg_t *arg, char **value, size_t *len);

char *
ngx_http_r4x_compile_directive(ngx_conf_t *cf, ngx_http_r4x_loc_conf_t * loc_conf, 
        ngx_http_r4x_srv_conf_t *srv_conf, ngx_http_r4x_directive_t *directive);

// Json
ngx_int_t
ngx_http_r4x_proces_json_fields(u_char* jsonText, size_t jsonTextLen, 
        ngx_hash_t *json_fields_hash, char **argvs, size_t *lens);
        
// String utilities
void
ngx_http_r4x_hash_script(ngx_str_t *digest, ngx_str_t *script);

char *
ngx_http_r4x_string_to_c_string(ngx_str_t *str, ngx_pool_t *pool);

ngx_int_t 
ngx_http_r4x_copy_str(ngx_str_t *dest, ngx_str_t *src, size_t offset, 
        size_t len, ngx_pool_t *pool);

#endif
