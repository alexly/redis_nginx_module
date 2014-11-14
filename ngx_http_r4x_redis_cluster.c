/*
 * Copyright (c) 2011-2015, Alexander Lyalin <alexandr.lyalin@gmail.com>
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

static ngx_int_t ngx_http_r4x_lazy_configure_redis_cluster_nodes(ngx_http_r4x_srv_conf_t *srv_conf)
{
    ngx_uint_t                      i;
    ngx_http_r4x_redis_node_t       *slave;
    
    if(srv_conf->master == NULL) {
        //TODO: logging "master node does't specified";
        return NGX_ERROR;
    }
    
    if(!srv_conf->cluster_initialized) {
        
        srv_conf->master->common_script = &srv_conf->common_script;
        srv_conf->master->eval_scripts          = srv_conf->eval_scripts;

        if(srv_conf->slaves != NULL) {

            slave = srv_conf->slaves->elts;
            for (i = 0; i <= srv_conf->slaves->nelts - 1; i++) {
                slave[i].common_script  = &srv_conf->common_script;
                slave[i].eval_scripts   = srv_conf->eval_scripts;
            }
        }

        srv_conf->cluster_initialized = 1;
    
    }
            
    return NGX_OK;
}

ngx_int_t ngx_http_r4x_get_read_write_node(ngx_http_request_t *r, ngx_http_r4x_redis_node_t **node)
{
    ngx_http_r4x_srv_conf_t         *srv_conf;
    
    srv_conf    = ngx_http_get_module_srv_conf(r, ngx_http_r4x_module);
    
    if(ngx_http_r4x_lazy_configure_redis_cluster_nodes(srv_conf) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    
    *node = srv_conf->master;
    
    if(ngx_http_r4x_init_connection(*node) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    
    return NGX_OK;
}

ngx_int_t ngx_http_r4x_get_read_only_node(ngx_http_request_t *r, ngx_http_r4x_redis_node_t **node)
{
    ngx_http_r4x_srv_conf_t         *srv_conf;
    ngx_http_r4x_redis_node_t       *tmp;
    static volatile ngx_uint_t      slave_node_index;
        
    srv_conf    = ngx_http_get_module_srv_conf(r, ngx_http_r4x_module);
    
    if(ngx_http_r4x_lazy_configure_redis_cluster_nodes(srv_conf) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    
    if(!srv_conf->slaves || srv_conf->slaves->nelts == 0) {
        *node = srv_conf->master;
    }
    else {
        slave_node_index++;

        if(slave_node_index >= srv_conf->slaves->nelts)
            slave_node_index = 0;

        tmp = srv_conf->slaves->elts;

        *node = &tmp[slave_node_index];
    }
        
    if(ngx_http_r4x_init_connection(*node) != NGX_OK)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    
    return NGX_OK;
}

static char*
ngx_http_r4x_configure_redis_node(ngx_conf_t *cf, ngx_http_r4x_redis_node_t *node)
{
    ngx_str_t       url, *value;
    u_char          *port, *last;
    
    value = cf->args->elts;
    url = value[1];
    
    //host = url->data;
    last = url.data + url.len;
    
    node->connected = 0;
    
    //TODO: add support IPV6
    
    // unix domain socket?
    if (ngx_strncasecmp(url.data, (u_char *) "unix:", 5) == 0) {
        node->port = -1;
        node->host = ngx_http_r4x_create_cstr_by_ngxstr(cf->pool, &url, 0, url.len);
    }
    else {
        port = ngx_strlchr(url.data, last, ':');
        
        if(port == NULL) {
            node->port = 80;
            node->host = ngx_http_r4x_create_cstr_by_ngxstr(cf->pool, &url, 0, url.len);
        }
        else {
            node->port = ngx_atoi(port+1, (last - port) -1);
            node->host = ngx_http_r4x_create_cstr_by_ngxstr(cf->pool, &url, 0, port - url.data);
        }
    }

    return NGX_CONF_OK;
}

char* ngx_http_r4x_set_redis_master_node(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_r4x_srv_conf_t     *srv_conf = conf;
    
    if(srv_conf->master != NULL) {
        return "redis master node already specified";
    }
    
    srv_conf->master = ngx_palloc(cf->pool, sizeof(ngx_http_r4x_redis_node_t));
    srv_conf->master->master_node = 1;
    
    return ngx_http_r4x_configure_redis_node(cf, srv_conf->master);
}

char* ngx_http_r4x_add_redis_slave_node(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_r4x_redis_node_t       *node;
    ngx_http_r4x_srv_conf_t         *srv_conf = conf;
    
    if(srv_conf->slaves == NULL) {
        srv_conf->slaves = ngx_array_create(cf->pool, 4, sizeof(ngx_http_r4x_redis_node_t));
    }
    
    node = ngx_array_push(srv_conf->slaves);
    
    return ngx_http_r4x_configure_redis_node(cf, node);
}
