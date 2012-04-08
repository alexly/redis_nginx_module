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

//#define USE_NGX_HASH_TABLE

#ifdef USE_NGX_HASH_TABLE
typedef ngx_hash_t redis4nginx_dict_t;
#else
extern dictType derective_arg_callback_dict;
typedef dict redis4nginx_dict_t;
#endif
extern ngx_module_t redis4nginx_module;

typedef struct {
    ngx_array_t     arguments_metadata;     // metadata for redis arguments
    unsigned        finalize:1;            // 1 - finalize request, 0 - only exec redis command
    char            **raw_redis_argvs;
    size_t          *raw_redis_argv_lens;
    union {
        redis4nginx_dict_t*     json_fields_hash;
        ngx_hash_keys_arrays_t  *hash_elements;
    };
} redis4nginx_directive_t;

typedef struct {
    ngx_uint_t type;
    union {
        ngx_str_t                   string_value;
        ngx_http_complex_value_t    *compilied;
    };
} redis4nginx_directive_arg_t;

typedef struct {
    ngx_array_t         directives;
} redis4nginx_loc_conf_t;

typedef struct {
    ngx_str_t       host;
    ngx_int_t       port;
    ngx_str_t       startup_script;
    ngx_array_t     *startup_scripts;
} redis4nginx_srv_conf_t;

typedef struct {
    unsigned        completed:1;
    unsigned        wait_read_body:1;
} redis4nginx_ctx;

typedef ngx_int_t (redis_4nginx_process_directive)(ngx_http_request_t*, redis4nginx_directive_t*);

// Connect to redis db
ngx_int_t redis4nginx_init_connection(redis4nginx_srv_conf_t *serv_conf);

// Execute redis command with format command
ngx_int_t redis4nginx_async_command(redisCallbackFn *fn, void *privdata, const char *format, ...);

// Execute redis command
ngx_int_t redis4nginx_async_command_argv(redisCallbackFn *fn, void *privdata, 
        int argc, char **argv, const size_t *argvlen);

// Send json response and finalize request
void redis4nginx_send_redis_reply(ngx_http_request_t *r, redisAsyncContext *c, redisReply *reply);

// Compute sha1 hash
void redis4nginx_hash_script(ngx_str_t *digest, ngx_str_t *script);
char * ngx_string_to_c_string(ngx_str_t *str, ngx_pool_t *pool);

ngx_int_t redis4nginx_get_directive_argument_value(ngx_http_request_t *r, 
        redis4nginx_directive_arg_t *arg, char **value, size_t *len);

char * redis4nginx_compile_directive(ngx_conf_t *cf, redis4nginx_loc_conf_t * loc_conf, 
        redis4nginx_srv_conf_t *srv_conf, redis4nginx_directive_t *directive);

ngx_int_t redis4nginx_copy_str(ngx_str_t *dest, ngx_str_t *src, size_t offset, size_t len, ngx_pool_t *pool);

ngx_int_t redis4nginx_proces_json_fields(u_char* jsonText, size_t jsonTextLen, 
        redis4nginx_dict_t *json_fields_hash, char **argvs, size_t *lens);
        
#endif
