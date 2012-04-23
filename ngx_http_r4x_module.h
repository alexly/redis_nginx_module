/*
 * Copyright (c) 2011-2012, Alexander Lyalin <alexandr.lyalin@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _REDIS_4_NGINX_MODULE_INCLUDED_
#define _REDIS_4_NGINX_MODULE_INCLUDED_

// hiredis
#include "hiredis/async.h"
#include "hiredis/dict.h"
// nginx
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t ngx_http_r4x_module;

typedef enum {
    REDIS4NGINX_JSON_FIELD_NAME_ARG,
    REDIS4NGINX_JSON_FIELD_INDEX_ARG,
    REDIS4NGINX_COMPILIED_ARG,
    REDIS4NGINX_STRING_ARG
} ngx_http_r4x_argument_type_t;

typedef struct {
    ngx_http_r4x_argument_type_t        type;
    union {
        ngx_uint_t                      index;
        ngx_str_t                       value;
        ngx_http_complex_value_t        *compilied;
    };
} ngx_http_r4x_directive_arg_t;

typedef struct {
    u_char                              *json_body;
    size_t                              json_body_len;
    unsigned short                      *offsets_lengths;
    ngx_uint_t                          *offsets_lengths_count;
} ngx_http_r4x_parsed_json;

typedef struct {
    ngx_array_t                         arguments;    // metadata for redis arguments
    char                                **cmd_argvs;
    size_t                              *cmd_argv_lens;
    redisCallbackFn                     *process_reply;
    unsigned                            require_json_field:1;
    unsigned                            read_only:1;
} ngx_http_r4x_directive_t;

typedef struct {
    redisAsyncContext                   *context;
    ngx_connection_t                    *conn;
    unsigned                            connected:1;
    unsigned                            master_node:1;
    char*                               host;
    ngx_int_t                           port;
    ngx_str_t                           *common_script;
    ngx_array_t                         *eval_scripts;
} ngx_http_r4x_redis_node_t;

typedef struct {
    ngx_array_t                         directives;
    unsigned                            require_json_field:1;
} ngx_http_r4x_loc_conf_t;

typedef struct {
    ngx_http_r4x_redis_node_t           *master;
    ngx_array_t                         *slaves;
    unsigned                            cluster_initialized:1;
    ngx_str_t                           common_script;
    ngx_array_t                         *eval_scripts;
} ngx_http_r4x_srv_conf_t;

typedef struct {
    unsigned                            completed:1;
    unsigned                            wait_read_body:1;
} ngx_http_r4x_request_ctx;


ngx_int_t ngx_http_r4x_init_connection(ngx_http_r4x_redis_node_t *node);
ngx_int_t ngx_http_r4x_async_command(ngx_http_r4x_redis_node_t *node, redisCallbackFn *fn, void *privdata, const char *format, ...);
ngx_int_t ngx_http_r4x_async_command_argv(ngx_http_r4x_redis_node_t *node, redisCallbackFn *fn, void *privdata, int argc, char **argv, const size_t *argvlen);
ngx_int_t ngx_http_r4x_get_read_write_node(ngx_http_request_t *r, ngx_http_r4x_redis_node_t **node);
ngx_int_t ngx_http_r4x_get_read_only_node(ngx_http_request_t *r, ngx_http_r4x_redis_node_t **node);
void ngx_http_r4x_send_redis_reply(ngx_http_request_t *r, redisAsyncContext *c, redisReply *reply);
char * ngx_http_r4x_compile_directive(ngx_conf_t *cf, ngx_http_r4x_loc_conf_t * lcf, ngx_http_r4x_srv_conf_t *scf, ngx_http_r4x_directive_t *d);
ngx_int_t ngx_http_r4x_parse_json_request_body(ngx_http_request_t *r, ngx_http_r4x_parsed_json* parsed);
ngx_int_t ngx_http_r4x_find_by_key(ngx_http_r4x_parsed_json *parsed, ngx_str_t *key, ngx_str_t *value);
ngx_int_t ngx_http_r4x_find_by_index(ngx_http_r4x_parsed_json *parsed, ngx_uint_t index, ngx_str_t *value);
void ngx_http_r4x_sha1(ngx_str_t *digest, ngx_str_t *script);
char* ngx_http_r4x_create_cstr_by_ngxstr(ngx_pool_t *pool, ngx_str_t *source, size_t offset, size_t len);
ngx_int_t ngx_http_r4x_copy_ngxstr(ngx_pool_t *pool, ngx_str_t *dest,  ngx_str_t *src, size_t offset, size_t len);
char* ngx_http_r4x_read_conf_file(ngx_conf_t *cf, ngx_str_t *file_path, ngx_str_t *buff);

#endif
