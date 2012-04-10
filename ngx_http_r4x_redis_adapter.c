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

static ngx_http_r4x_redis_node_t* sing_node = NULL;

static ngx_int_t ngx_http_r4x_add_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t ngx_http_r4x_del_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t ngx_http_r4x_add_connection(int fd, ngx_connection_t **c);
static void ngx_http_r4x_connecte_event_handler(const redisAsyncContext *ctx);
static void ngx_http_r4x_disconnecte_event_handler(const redisAsyncContext *ctx, int status);
static void ngx_http_r4x_read_event_handler(ngx_event_t *handle);
static void ngx_http_r4x_write_event_handler(ngx_event_t *handle);
static void ngx_http_r4x_add_read(void *privdata);
static void ngx_http_r4x_del_read(void *privdata);
static void ngx_http_r4x_add_write(void *privdata);
static void ngx_http_r4x_del_write(void *privdata);
static void ngx_http_r4x_cleanup(void *privdata);

ngx_int_t ngx_http_r4x_init_connection(ngx_http_r4x_srv_conf_t *serv_conf)
{    
    //TODO: connection timeout shoul be added by ngx_add_timer or native hiredis
    ngx_uint_t i;
    ngx_str_t *script;
    
    if(sing_node == NULL)
    {    
        if(serv_conf == NULL)
            return NGX_ERROR;
        
        sing_node = ngx_palloc(ngx_cycle->pool, sizeof(ngx_http_r4x_redis_node_t));
        sing_node->host = ngx_http_r4x_string_to_c_string(&serv_conf->host, NULL);              
        sing_node->port = serv_conf->port;
        sing_node->connected = 0;
    }

    if(sing_node->connected == 0) 
    {
        if(sing_node->port >=0) {
            sing_node->async = redisAsyncConnect(
                        (const char*)sing_node->host,
                        sing_node->port);
        }
        else {
            sing_node->async = redisAsyncConnectUnix((const char*)sing_node->host);
        }

        if(sing_node->async->err) {
            return NGX_ERROR;
        }

        redisAsyncSetConnectCallback(sing_node->async, ngx_http_r4x_connecte_event_handler);
        redisAsyncSetDisconnectCallback(sing_node->async, ngx_http_r4x_disconnecte_event_handler);

        if(ngx_http_r4x_add_connection(sing_node->async->c.fd, &sing_node->conn) != NGX_OK) {
            return NGX_ERROR;
        }

        sing_node->conn->read->handler = ngx_http_r4x_read_event_handler;
        sing_node->conn->write->handler = ngx_http_r4x_write_event_handler;

        sing_node->async->data = sing_node;
        sing_node->conn->data = sing_node;

        // Register functions to start/stop listening for events
        sing_node->async->ev.addRead = ngx_http_r4x_add_read;
        sing_node->async->ev.delRead = ngx_http_r4x_del_read;
        sing_node->async->ev.addWrite = ngx_http_r4x_add_write;
        sing_node->async->ev.delWrite = ngx_http_r4x_del_write;
        sing_node->async->ev.data = sing_node;
        sing_node->async->ev.cleanup = ngx_http_r4x_cleanup;

        if(serv_conf == NULL)
            return NGX_ERROR;
        
        // load all scripts to redis db
        
        if(serv_conf->startup_script.len > 0)
            ngx_http_r4x_async_command(NULL, NULL, "eval %b 0", serv_conf->startup_script.data, serv_conf->startup_script.len);
        
        if(serv_conf->startup_scripts != NULL)  {
            script = serv_conf->startup_scripts->elts;
            
            for(i=0; i < serv_conf->startup_scripts->nelts; i++)
                ngx_http_r4x_async_command(NULL, NULL, "eval %b 0", (&script[i])->data, (&script[i])->len);
        }
    }

    return NGX_OK;
}

ngx_int_t ngx_http_r4x_async_command(redisCallbackFn *fn, void *privdata, const char *format, ...) 
{
    va_list ap;
    int status;
    
    va_start(ap,format);
    status = redisvAsyncCommand(sing_node->async,fn, privdata, format, ap);
    va_end(ap);
    
    return status == REDIS_OK ?  NGX_OK : NGX_ERROR;
}

ngx_int_t ngx_http_r4x_async_command_argv(redisCallbackFn *fn, void *privdata, int argc, char **argv, const size_t *argvlen)
{
    int status;
    status = redisAsyncCommandArgv(sing_node->async, fn, privdata, argc, (const char**)argv, argvlen);
    
    return status == REDIS_OK ?  NGX_OK : NGX_ERROR;
}

static void ngx_http_r4x_connecte_event_handler(const redisAsyncContext *ctx)
{
    ngx_http_r4x_redis_node_t *node  = ctx->data;
    node->connected = 1;
}

static void ngx_http_r4x_disconnecte_event_handler(const redisAsyncContext *ctx, int status)
{
    ngx_http_r4x_redis_node_t *node  = ctx->data;
    node->connected = 0;
}

static void ngx_http_r4x_read_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    ngx_http_r4x_redis_node_t *node  = conn->data;
    redisAsyncHandleRead(node->async);
}

static void ngx_http_r4x_write_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    ngx_http_r4x_redis_node_t *node  = conn->data;
    redisAsyncHandleWrite(node->async);
}

static void ngx_http_r4x_add_read(void *privdata)
{
    ngx_http_r4x_redis_node_t *node = privdata;
    ngx_http_r4x_add_event(node->conn, NGX_READ_EVENT);
}

static void ngx_http_r4x_del_read(void *privdata)
{
    ngx_http_r4x_redis_node_t *node = privdata;
    ngx_http_r4x_del_event(node->conn, NGX_READ_EVENT);
}

static void ngx_http_r4x_add_write(void *privdata)
{
    ngx_http_r4x_redis_node_t *node = privdata;
    ngx_http_r4x_add_event(node->conn, NGX_WRITE_EVENT);
}

static void ngx_http_r4x_del_write(void *privdata)
{
    ngx_http_r4x_redis_node_t *node = privdata;
    ngx_http_r4x_del_event(node->conn, NGX_WRITE_EVENT);
}

static void ngx_http_r4x_cleanup(void *privdata)
{
    ngx_http_r4x_del_read(privdata);
    ngx_http_r4x_del_write(privdata);
}

static ngx_int_t ngx_http_r4x_add_event(ngx_connection_t *c, ngx_int_t event) {
    ngx_int_t flags;
    
    flags = (ngx_event_flags & NGX_USE_CLEAR_EVENT) ?
                        NGX_CLEAR_EVENT: // kqueue, epoll
                        NGX_LEVEL_EVENT; // select, poll, /dev/poll
          
    if(event == NGX_READ_EVENT) {
        // eventport event type has no meaning: oneshot only
        return ngx_add_event(c->read, NGX_READ_EVENT, flags);
    }
    
    else {
        return ngx_add_event(c->write, NGX_WRITE_EVENT, flags);
    }
}

static ngx_int_t ngx_http_r4x_del_event(ngx_connection_t *c, ngx_int_t event) {
    ngx_int_t flags;
    
    flags = (ngx_event_flags & NGX_USE_CLEAR_EVENT) ?
                        NGX_CLEAR_EVENT: // kqueue, epoll
                        NGX_LEVEL_EVENT; // select, poll, /dev/poll
          
    if(event == NGX_READ_EVENT) {
        // eventport event type has no meaning: oneshot only
        return ngx_del_event(c->read, NGX_READ_EVENT, flags);
    }
    
    else {
        return ngx_del_event(c->write, NGX_WRITE_EVENT, flags);
    }
}

static ngx_int_t ngx_http_r4x_add_connection(int fd, ngx_connection_t **c)
{
    ngx_event_t       *rev, *wev;

    if (!c)
        return NGX_ERROR;

    if (!(*c = ngx_get_connection(fd, ngx_cycle->log)))
        return NGX_ERROR;

    if (ngx_nonblocking(fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_socket_errno,
                ngx_nonblocking_n " failed");
        ngx_free_connection(*c);
        return NGX_ERROR;
    }

    rev = (*c)->read;
    wev = (*c)->write;

    rev->log = ngx_cycle->log;
    wev->log = ngx_cycle->log;

    (*c)->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

#if (NGX_THREADS)

    /* TODO: lock event when call completion handler */

    rev->lock = &(*c)->lock;
    wev->lock = &(*c)->lock;
    rev->own_lock = &(*c)->lock;
    wev->own_lock = &(*c)->lock;

#endif

    // rtsig
    return ngx_add_conn(*c);
}
