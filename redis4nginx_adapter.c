#ifndef DDEBUG
#define DDEBUG 0
#endif

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "hiredis/async.h"
#include "ddebug.h"

// context struct in the request handling cycle, holding
// the current states of the command evaluator
typedef struct redis4nginx_connection_t {
    redisAsyncContext *async;
    ngx_connection_t *conn;
    unsigned connected:1;
} redis4nginx_connection_t;

static redis4nginx_connection_t* redis4nginx_connection = NULL;

static ngx_int_t redis4nginx_add_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t redis4nginx_del_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t redis4nginx_add_connection(int fd, ngx_connection_t **c);

void ngx_redis_add_read(void *privdata)
{
    redis4nginx_connection_t *ctx = (redis4nginx_connection_t*)privdata;
    redis4nginx_add_event(ctx->conn, NGX_READ_EVENT);
}

void ngx_redis_del_read(void *privdata)
{
    redis4nginx_connection_t *ctx = (redis4nginx_connection_t*)privdata;
    redis4nginx_del_event(ctx->conn, NGX_READ_EVENT);
}

void ngx_redis_add_write(void *privdata)
{
    redis4nginx_connection_t *ctx = (redis4nginx_connection_t*)privdata;
    redis4nginx_add_event(ctx->conn, NGX_WRITE_EVENT);
}

void ngx_redis_del_write(void *privdata)
{
    redis4nginx_connection_t *ctx = (redis4nginx_connection_t*)privdata;
    redis4nginx_del_event(ctx->conn, NGX_WRITE_EVENT);
}

void redis4nginx_read_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    redis4nginx_connection_t *context  = conn->data;
    redisAsyncHandleRead(context->async);
}

void redis4nginx_write_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    redis4nginx_connection_t *context  = conn->data;
    redisAsyncHandleWrite(context->async);
}

void redis4nginx_connected_handler(const redisAsyncContext *ctx)
{
    redis4nginx_connection_t *context  = ctx->data;
    context->connected = 1;
}

void redis4nginx_disconnected_handler(const redisAsyncContext *ctx, int status)
{
    redis4nginx_connection_t *context  = ctx->data;
    context->connected = 0;
}

ngx_int_t redis4nginx_init_connection(ngx_http_request_t *r)
{     
    redis4nginx_connection_t *ctx;
        
    if(redis4nginx_connection == NULL) {
        ctx = malloc(sizeof(redis4nginx_connection_t));
        
        ctx->async = redisAsyncConnect("127.0.0.1", 6379);
        redisAsyncSetConnectCallback(ctx->async, redis4nginx_connected_handler);
        redisAsyncSetDisconnectCallback(ctx->async, redis4nginx_disconnected_handler);

        if(redis4nginx_add_connection(ctx->async->c.fd, &ctx->conn) != NGX_OK) {
            return NGX_ERROR;
        }

        ctx->conn->read->handler = redis4nginx_read_event_handler;
        ctx->conn->write->handler = redis4nginx_write_event_handler;

        ctx->async->data = ctx;
        ctx->conn->data = ctx;
        
        // Register functions to start/stop listening for events
        ctx->async->ev.addRead = ngx_redis_add_read;
        ctx->async->ev.delRead = ngx_redis_del_read;
        ctx->async->ev.addWrite = ngx_redis_add_write;
        ctx->async->ev.delWrite = ngx_redis_del_write;
        ctx->async->ev.data = ctx;
        //redis4nginx_single->async->ev.cleanup = ngx_redis_cleanup;
        
        redis4nginx_connection = ctx;
    }
    
    return NGX_OK;
}

int redis4nginx_command(redisCallbackFn *fn, void *privdata, const char *format, ...) {
    va_list ap;
    int status;
    va_start(ap,format);
    status = redisvAsyncCommand(redis4nginx_connection->async,fn,privdata,format,ap);
    va_end(ap);
    return status;
}

static ngx_int_t redis4nginx_add_event(ngx_connection_t *c, ngx_int_t event) {
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

static ngx_int_t redis4nginx_del_event(ngx_connection_t *c, ngx_int_t event) {
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

static ngx_int_t redis4nginx_add_connection(int fd, ngx_connection_t **c)
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