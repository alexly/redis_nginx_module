#include "redis4nginx_adapter.h"

static redis4nginx_ctx_t* redis4nginx_single = NULL;

static ngx_int_t redis4nginx_add_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t redis4nginx_del_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t redis4nginx_add_connection(int fd, ngx_connection_t **c);

void ngx_redis_add_read(void *privdata)
{
    redis4nginx_add_event(redis4nginx_single->conn, NGX_READ_EVENT);
}

void ngx_redis_del_read(void *privdata)
{
    redis4nginx_del_event(redis4nginx_single->conn, NGX_READ_EVENT);
}

void ngx_redis_add_write(void *privdata)
{
    redis4nginx_add_event(redis4nginx_single->conn, NGX_WRITE_EVENT);
}

void ngx_redis_del_write(void *privdata)
{
    redis4nginx_del_event(redis4nginx_single->conn, NGX_WRITE_EVENT);
}

void redis4nginx_read_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    redis4nginx_ctx_t *context  = conn->data;
    redisAsyncHandleRead(context->async);
}

void redis4nginx_write_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    redis4nginx_ctx_t *context  = conn->data;
    redisAsyncHandleWrite(context->async);
}

void redis4nginx_connected_handler(const redisAsyncContext *ctx)
{
    redis4nginx_ctx_t *context  = ctx->data;
    context->connected = 1;
}

void redis4nginx_disconnected_handler(const redisAsyncContext *ctx, int status)
{
    redis4nginx_ctx_t *context  = ctx->data;
    context->connected = 0;
}

redis4nginx_ctx_t *redis4nginx_create_ctx(ngx_http_request_t *r)
{
    redis4nginx_ctx_t *ctx;
    
    if(redis4nginx_single == NULL) {
        redis4nginx_single = malloc(sizeof(redis4nginx_ctx_t));
        
        redis4nginx_single->async = redisAsyncConnect("127.0.0.1", 6379);
        redisAsyncSetConnectCallback(redis4nginx_single->async, redis4nginx_connected_handler);
        redisAsyncSetDisconnectCallback(redis4nginx_single->async, redis4nginx_disconnected_handler);

        if(redis4nginx_add_connection(redis4nginx_single->async->c.fd, &redis4nginx_single->conn) != NGX_OK) {
            return NULL;
        }

        redis4nginx_single->conn->read->handler = redis4nginx_read_event_handler;
        redis4nginx_single->conn->write->handler = redis4nginx_write_event_handler;

        redis4nginx_single->async->data = redis4nginx_single;
        redis4nginx_single->conn->data = redis4nginx_single;
        
        // Register functions to start/stop listening for events
        redis4nginx_single->async->ev.addRead = ngx_redis_add_read;
        redis4nginx_single->async->ev.delRead = ngx_redis_del_read;
        redis4nginx_single->async->ev.addWrite = ngx_redis_add_write;
        redis4nginx_single->async->ev.delWrite = ngx_redis_del_write;
        //redis4nginx_single->async->ev.cleanup = ngx_redis_cleanup;
        redis4nginx_single->async->ev.data = redis4nginx_single;
    }
    
    ctx = ngx_pcalloc(r->pool, sizeof(redis4nginx_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
        
    ctx->async = redis4nginx_single->async;
    ctx->conn = redis4nginx_single->conn;
    ctx->connected = redis4nginx_single->connected;
    
    return ctx;
}

int redis4nginx_command(redis4nginx_ctx_t *ctx, redisCallbackFn *fn, void *privdata, const char *format, ...) {
    va_list ap;
    int status;
    va_start(ap,format);
    status = redisvAsyncCommand(ctx->async,fn,privdata,format,ap);
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