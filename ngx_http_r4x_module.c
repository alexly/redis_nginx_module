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

#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "ngx_http_r4x_module.h"

static void* ngx_http_r4x_create_srv_conf(ngx_conf_t *cf);
static char* ngx_http_r4x_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child);
static void* ngx_http_r4x_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_r4x_exec_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_r4x_exec_return_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_r4x_init_module(ngx_cycle_t *cycle);
ngx_int_t ngx_http_r4x_exec_handler(ngx_http_request_t *r);
void ngx_http_r4x_process_redis_reply(redisAsyncContext *c, void *repl, void *privdata);

static ngx_command_t  ngx_http_r4x_commands[] = {
    {	ngx_string("redis_host"),
        NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_SRV_CONF_OFFSET,
        offsetof(ngx_http_r4x_srv_conf_t, host),
        NULL },

    {	ngx_string("redis_port"),
        NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_SRV_CONF_OFFSET,
        offsetof(ngx_http_r4x_srv_conf_t, port),
        NULL },
        
    {	ngx_string("redis_common_script"),
        NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_SRV_CONF_OFFSET,
        offsetof(ngx_http_r4x_srv_conf_t, common_script_file_name),
        NULL },

    {   ngx_string("redis_exec"),
        NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_ANY,
        ngx_http_r4x_exec_handler_init,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },
        
    {   ngx_string("redis_exec_return"),
        NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_ANY,
        ngx_http_r4x_exec_return_handler_init,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },    
    ngx_null_command
};

static ngx_http_module_t  ngx_http_r4x_module_ctx = {
  NULL,                          /* preconfiguration */
  NULL,                          /* postconfiguration */
  NULL,                          /* create main configuration */
  NULL,                          /* init main configuration */
  ngx_http_r4x_create_srv_conf,  /* create server configuration */
  ngx_http_r4x_merge_srv_conf,   /* merge server configuration */
  ngx_http_r4x_create_loc_conf,  /* create location configuration */
  NULL                           /* merge location configuration char* redis4nginx_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);*/
};


ngx_module_t ngx_http_r4x_module = {
  NGX_MODULE_V1,
  &ngx_http_r4x_module_ctx,     /* module context */
  ngx_http_r4x_commands,        /* module directives */
  NGX_HTTP_MODULE,              /* module type */
  NULL,                         /* init master */
  ngx_http_r4x_init_module,     /* init module */
  NULL,                         /* init process */
  NULL,                         /* init thread */
  NULL,                         /* exit thread */
  NULL,                         /* exit process */
  NULL,                         /* exit master */
  NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_http_r4x_init_module(ngx_cycle_t *cycle)
{
    return NGX_OK; 
}

static void* ngx_http_r4x_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_r4x_srv_conf_t *srv_conf;
    
    srv_conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_r4x_srv_conf_t));

    srv_conf->port = NGX_CONF_UNSET;
    
    return srv_conf;
}

static char* ngx_http_r4x_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_file_t                  file;
    ngx_file_info_t             fi;
    ngx_err_t                   err;
    size_t                      size;
    ssize_t                     n;
    
	ngx_http_r4x_srv_conf_t     *prev = parent;
	ngx_http_r4x_srv_conf_t     *conf = child;
        
	ngx_log_debug0(NGX_LOG_INFO, cf->log, 0, "resis4nginx merge srv");

	ngx_conf_merge_str_value(conf->host,
			prev->host, NULL);

	ngx_conf_merge_value(conf->port,
			prev->port, NGX_CONF_UNSET);
    
    ngx_memzero(&file, sizeof(ngx_file_t));
    file.name = conf->common_script_file_name;
    file.log = cf->log;
    
    file.fd = ngx_open_file(conf->common_script_file_name.data, NGX_FILE_RDONLY, 0, 0);
    if (file.fd == NGX_INVALID_FILE) {
        err = ngx_errno;
        if (err != NGX_ENOENT) {
            ngx_conf_log_error(NGX_LOG_CRIT, cf, err,
                               ngx_open_file_n " \"%s\" failed", conf->common_script_file_name.data);
        }
        return NGX_CONF_ERROR;
    }
    
    if (ngx_fd_info(file.fd, &fi) == NGX_FILE_ERROR) {
        ngx_conf_log_error(NGX_LOG_CRIT, cf, ngx_errno,
                           ngx_fd_info_n " \"%s\" failed", conf->common_script_file_name.data);
        
        return NGX_CONF_ERROR;
    }
    
    size = (size_t) ngx_file_size(&fi);
    
    conf->common_script.data = ngx_palloc(cf->pool, size);
    conf->common_script.len = size;
    
    n = ngx_read_file(&file, conf->common_script.data, size, 0);
    
    if (n == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_CRIT, cf, ngx_errno,
                           ngx_read_file_n " \"%s\" failed", conf->common_script_file_name.data);
        return NGX_CONF_ERROR;
    }

    if ((size_t) n != size) {
        ngx_conf_log_error(NGX_LOG_CRIT, cf, 0,
            ngx_read_file_n " \"%s\" returned only %z bytes instead of %z",
            conf->common_script_file_name.data, n, size);
        
        return NGX_CONF_ERROR;
    }
    
	return NGX_CONF_OK;
}

static void* ngx_http_r4x_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_r4x_loc_conf_t *conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_r4x_loc_conf_t));
    
    ngx_array_init(&conf->directives, cf->pool, 1, sizeof(ngx_http_r4x_directive_t));
    
	return conf;
}

static char *ngx_http_r4x_exec_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_r4x_loc_conf_t *loc_conf = conf;
    ngx_http_r4x_srv_conf_t *srv_conf;
    
    ngx_http_r4x_directive_t *directive = ngx_array_push(&loc_conf->directives);
    directive->process_reply = NULL;
    directive->require_json_field = 0;
    
    srv_conf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_r4x_module);
    
    return ngx_http_r4x_compile_directive(cf, loc_conf, srv_conf, directive);
}

static char *ngx_http_r4x_exec_return_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_r4x_loc_conf_t *loc_conf = conf;
    ngx_http_core_loc_conf_t *core_conf;
    ngx_http_r4x_directive_t *directive;
    ngx_http_r4x_srv_conf_t *srv_conf;
    
    core_conf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    if(core_conf->handler == NULL) {
        core_conf->handler = &ngx_http_r4x_exec_handler;
    }
    
    directive = ngx_array_push(&loc_conf->directives);
    directive->process_reply = ngx_http_r4x_process_redis_reply;
    directive->require_json_field = 0;
    
    srv_conf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_r4x_module);
    
    return ngx_http_r4x_compile_directive(cf, loc_conf, srv_conf, directive);
}

