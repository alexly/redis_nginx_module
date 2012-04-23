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
static void* ngx_http_r4x_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_r4x_exec_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_r4x_exec_return_handler_init(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_r4x_init_module(ngx_cycle_t *cycle);

static char* ngx_http_r4x_load_common_script(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char* ngx_http_r4x_set_redis_master_node(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char* ngx_http_r4x_add_redis_slave_node(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

ngx_int_t ngx_http_r4x_exec_handler(ngx_http_request_t *r);
void ngx_http_r4x_process_redis_reply(redisAsyncContext *c, void *repl, void *privdata);

static ngx_command_t  ngx_http_r4x_commands[] = {       
    {	ngx_string("redis_master_node"),
        NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_http_r4x_set_redis_master_node,
        NGX_HTTP_SRV_CONF_OFFSET,
        0,
        NULL },
        
    {	ngx_string("redis_slave_node"),
        NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_http_r4x_add_redis_slave_node,
        NGX_HTTP_SRV_CONF_OFFSET,
        0,
        NULL },
        
    {	ngx_string("redis_common_script"),
        NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
        ngx_http_r4x_load_common_script,
        NGX_HTTP_SRV_CONF_OFFSET,
        0,
        NULL },

    {   ngx_string("redis_read_cmd"),
        NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_ANY,
        ngx_http_r4x_exec_handler_init,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },
        
    {   ngx_string("redis_read_cmd_ret"),
        NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_ANY,
        ngx_http_r4x_exec_return_handler_init,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },
        
    {   ngx_string("redis_write_cmd"),
        NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_ANY,
        ngx_http_r4x_exec_handler_init,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },
        
    {   ngx_string("redis_write_cmd_ret"),
        NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_ANY,
        ngx_http_r4x_exec_return_handler_init,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },
    ngx_null_command
};

static ngx_http_module_t  ngx_http_r4x_module_ctx = {
  NULL,                             /* preconfiguration */
  NULL,                             /* postconfiguration */
  NULL,                             /* create main configuration */
  NULL,                             /* init main configuration */
  ngx_http_r4x_create_srv_conf,     /* create server configuration */
  NULL,                             /* merge server configuration */
  ngx_http_r4x_create_loc_conf,     /* create location configuration */
  NULL                              /* merge location configuration char* redis4nginx_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);*/
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
    
    srv_conf->cluster_initialized = 0;
            
    return srv_conf;
}

static char* ngx_http_r4x_load_common_script(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_r4x_srv_conf_t     *srv_conf;
    ngx_str_t                   file_name, *value;
    
    srv_conf = conf;
    
    value = cf->args->elts;
    file_name = value[1];
    
    return ngx_http_r4x_read_conf_file(cf, &file_name, &srv_conf->common_script);
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
    ngx_http_core_loc_conf_t *core_conf;
    ngx_http_r4x_directive_t *directive;
    ngx_http_r4x_srv_conf_t *srv_conf;
    ngx_http_r4x_loc_conf_t *loc_conf = conf;
    
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

