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

void ngx_http_r4x_script_load_completed(redisAsyncContext *c, void *repl, void *privdata)
{
    redisReply* rr = repl;
    if(!rr || rr->type == REDIS_REPLY_ERROR) {
        //TODO: logging
    }
}

ngx_int_t ngx_http_r4x_init_connection(ngx_http_r4x_redis_node_t *node)
{    
    //TODO: connection timeout shoul be added by ngx_add_timer or native hiredis
    ngx_uint_t              i;
    ngx_str_t               *script;
    
    if(node->connected == 0) 
    {
        node->context = node->port >=0 ? 
            redisAsyncConnect( (const char*)node->host, node->port) :
            redisAsyncConnectUnix((const char*)node->host);

        if(node->context->err)
            return NGX_ERROR;

        redisAsyncSetConnectCallback(node->context, ngx_http_r4x_connecte_event_handler);
        redisAsyncSetDisconnectCallback(node->context, ngx_http_r4x_disconnecte_event_handler);

        if(ngx_http_r4x_add_connection(node->context->c.fd, &node->conn) != NGX_OK) {
            return NGX_ERROR;
        }

        node->conn->read->handler = ngx_http_r4x_read_event_handler;
        node->conn->write->handler = ngx_http_r4x_write_event_handler;

        node->context->data = node;
        node->conn->data = node;

        // Register functions to start/stop listening for events
        node->context->ev.addRead = ngx_http_r4x_add_read;
        node->context->ev.delRead = ngx_http_r4x_del_read;
        node->context->ev.addWrite = ngx_http_r4x_add_write;
        node->context->ev.delWrite = ngx_http_r4x_del_write;
        node->context->ev.data = node;
        node->context->ev.cleanup = ngx_http_r4x_cleanup;
        
        // load all scripts to redis db        
        if(node->common_script != NULL && node->common_script->len > 0)
            ngx_http_r4x_async_command(node, ngx_http_r4x_script_load_completed, NULL, "eval %b 0", 
                    node->common_script->data, node->common_script->len);
        
        if(node->eval_scripts != NULL)  {
            script = node->eval_scripts->elts;
            
            for(i=0; i < node->eval_scripts->nelts; i++)
                // TODO: change eval to load script
                ngx_http_r4x_async_command(node, ngx_http_r4x_script_load_completed, NULL, "eval %b 0", (&script[i])->data, (&script[i])->len);
        }
    }

    return NGX_OK;
}

ngx_int_t ngx_http_r4x_async_command(ngx_http_r4x_redis_node_t *node, redisCallbackFn *fn, 
        void *privdata, const char *format, ...) 
{
    va_list ap;
    int status;
    
    va_start(ap,format);
    status = redisvAsyncCommand(node->context,fn, privdata, format, ap);
    va_end(ap);
    
    return status == REDIS_OK ?  NGX_OK : NGX_ERROR;
}

ngx_int_t ngx_http_r4x_async_command_argv(ngx_http_r4x_redis_node_t *node, redisCallbackFn *fn, 
        void *privdata, int argc, char **argv, const size_t *argvlen)
{
    int status;
    status = redisAsyncCommandArgv(node->context, fn, privdata, argc, (const char**)argv, argvlen);
    
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
    redisAsyncHandleRead(node->context);
}

static void ngx_http_r4x_write_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    ngx_http_r4x_redis_node_t *node  = conn->data;
    redisAsyncHandleWrite(node->context);
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
