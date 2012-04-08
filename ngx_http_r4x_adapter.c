#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

// context struct in the request handling cycle, holding
// the current states of the command evaluator
typedef struct redis4nginx_connection_ctx_t {
    redisAsyncContext *async;
    ngx_connection_t *conn;
    unsigned connected:1;
    char* host;
    ngx_int_t port;
} redis4nginx_connection_ctx_t;

static redis4nginx_connection_ctx_t* redis4nginx_conn = NULL;

static ngx_int_t redis4nginx_add_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t redis4nginx_del_event(ngx_connection_t *c, ngx_int_t event);
static ngx_int_t redis4nginx_add_connection(int fd, ngx_connection_t **c);
static void redis4nginx_connecte_event_handler(const redisAsyncContext *ctx);
static void redis4nginx_disconnect_event_handler(const redisAsyncContext *ctx, int status);
static void redis4nginx_read_event_handler(ngx_event_t *handle);
static void redis4nginx_write_event_handler(ngx_event_t *handle);
static void redis4nginx_add_read(void *privdata);
static void redis4nginx_del_read(void *privdata);
static void redis4nginx_add_write(void *privdata);
static void redis4nginx_del_write(void *privdata);
static void redis4nginx_cleanup(void *privdata);

ngx_int_t redis4nginx_init_connection(redis4nginx_srv_conf_t *serv_conf)
{    
    //TODO: connection timeout shoul be added by ngx_add_timer or native hiredis
    ngx_uint_t i;
    ngx_str_t *script;
    
    if(redis4nginx_conn == NULL)
    {    
        if(serv_conf == NULL)
            return NGX_ERROR;
        
        redis4nginx_conn = ngx_palloc(ngx_cycle->pool, sizeof(redis4nginx_connection_ctx_t));
        redis4nginx_conn->host = ngx_string_to_c_string(&serv_conf->host, NULL);              
        redis4nginx_conn->port = serv_conf->port;
        redis4nginx_conn->connected = 0;
    }

    if(redis4nginx_conn->connected == 0) 
    {
        if(redis4nginx_conn->port >=0) {
            redis4nginx_conn->async = redisAsyncConnect(
                        (const char*)redis4nginx_conn->host,
                        redis4nginx_conn->port);
        }
        else {
            redis4nginx_conn->async = redisAsyncConnectUnix((const char*)redis4nginx_conn->host);
        }

        if(redis4nginx_conn->async->err) {
            return NGX_ERROR;
        }

        redisAsyncSetConnectCallback(redis4nginx_conn->async, redis4nginx_connecte_event_handler);
        redisAsyncSetDisconnectCallback(redis4nginx_conn->async, redis4nginx_disconnect_event_handler);

        if(redis4nginx_add_connection(redis4nginx_conn->async->c.fd, &redis4nginx_conn->conn) != NGX_OK) {
            return NGX_ERROR;
        }

        redis4nginx_conn->conn->read->handler = redis4nginx_read_event_handler;
        redis4nginx_conn->conn->write->handler = redis4nginx_write_event_handler;

        redis4nginx_conn->async->data = redis4nginx_conn;
        redis4nginx_conn->conn->data = redis4nginx_conn;

        // Register functions to start/stop listening for events
        redis4nginx_conn->async->ev.addRead = redis4nginx_add_read;
        redis4nginx_conn->async->ev.delRead = redis4nginx_del_read;
        redis4nginx_conn->async->ev.addWrite = redis4nginx_add_write;
        redis4nginx_conn->async->ev.delWrite = redis4nginx_del_write;
        redis4nginx_conn->async->ev.data = redis4nginx_conn;
        redis4nginx_conn->async->ev.cleanup = redis4nginx_cleanup;

        if(serv_conf == NULL)
            return NGX_ERROR;
        
        // load all scripts to redis db
        
        if(serv_conf->startup_script.len > 0)
            redis4nginx_async_command(NULL, NULL, "eval %b 0", serv_conf->startup_script.data, serv_conf->startup_script.len);
        
        if(serv_conf->startup_scripts != NULL)  {
            script = serv_conf->startup_scripts->elts;
            
            for(i=0; i < serv_conf->startup_scripts->nelts; i++)
                redis4nginx_async_command(NULL, NULL, "eval %b 0", (&script[i])->data, (&script[i])->len);
        }
    }

    return NGX_OK;
}

ngx_int_t redis4nginx_async_command(redisCallbackFn *fn, void *privdata, const char *format, ...) 
{
    va_list ap;
    int status;
    
    va_start(ap,format);
    status = redisvAsyncCommand(redis4nginx_conn->async,fn, privdata, format, ap);
    va_end(ap);
    
    return status == REDIS_OK ?  NGX_OK : NGX_ERROR;
}

ngx_int_t redis4nginx_async_command_argv(redisCallbackFn *fn, void *privdata, int argc, char **argv, const size_t *argvlen)
{
    int status;
    status = redisAsyncCommandArgv(redis4nginx_conn->async, fn, privdata, argc, (const char**)argv, argvlen);
    
    return status == REDIS_OK ?  NGX_OK : NGX_ERROR;
}

static void redis4nginx_connecte_event_handler(const redisAsyncContext *ctx)
{
    redis4nginx_connection_ctx_t *context  = ctx->data;
    context->connected = 1;
}

static void redis4nginx_disconnect_event_handler(const redisAsyncContext *ctx, int status)
{
    redis4nginx_connection_ctx_t *context  = ctx->data;
    context->connected = 0;
}

static void redis4nginx_read_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    redis4nginx_connection_ctx_t *context  = conn->data;
    redisAsyncHandleRead(context->async);
}

static void redis4nginx_write_event_handler(ngx_event_t *handle)
{
    ngx_connection_t *conn = handle->data;
    redis4nginx_connection_ctx_t *context  = conn->data;
    redisAsyncHandleWrite(context->async);
}

static void redis4nginx_add_read(void *privdata)
{
    redis4nginx_connection_ctx_t *conn = (redis4nginx_connection_ctx_t*)privdata;
    redis4nginx_add_event(conn->conn, NGX_READ_EVENT);
}

static void redis4nginx_del_read(void *privdata)
{
    redis4nginx_connection_ctx_t *conn = (redis4nginx_connection_ctx_t*)privdata;
    redis4nginx_del_event(conn->conn, NGX_READ_EVENT);
}

static void redis4nginx_add_write(void *privdata)
{
    redis4nginx_connection_ctx_t *conn = (redis4nginx_connection_ctx_t*)privdata;
    redis4nginx_add_event(conn->conn, NGX_WRITE_EVENT);
}

static void redis4nginx_del_write(void *privdata)
{
    redis4nginx_connection_ctx_t *conn = (redis4nginx_connection_ctx_t*)privdata;
    redis4nginx_del_event(conn->conn, NGX_WRITE_EVENT);
}

static void redis4nginx_cleanup(void *privdata)
{
    redis4nginx_del_read(privdata);
    redis4nginx_del_write(privdata);
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