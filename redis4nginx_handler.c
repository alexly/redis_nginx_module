#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "redis4nginx_module.h"

char *redis4nginx_entry_point(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
void eval_callback(redisAsyncContext *c, void *repl, void *privdata);
void * redis4nginx_create_conf(ngx_conf_t *cf);

static ngx_command_t  redis4nginx_commands[] = {

  { ngx_string("eval"),
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    redis4nginx_entry_point,
    0,
    0,
    NULL },

    ngx_null_command
};

//static u_char ngx_hello_string[] = "Hello, world!";

static ngx_http_module_t  redis4nginx_module_ctx = {
  NULL,                          /* preconfiguration */
  NULL,                          /* postconfiguration */

  NULL,                          /* create main configuration */
  NULL,                          /* init main configuration */

  NULL,                          /* create server configuration */
  NULL,                          /* merge server configuration */

  redis4nginx_create_conf,       /* create location configuration */
  NULL                           /* merge location configuration */
};


ngx_module_t redis4nginx_module = {
  NGX_MODULE_V1,
  &redis4nginx_module_ctx, /* module context */
  redis4nginx_commands,   /* module directives */
  NGX_HTTP_MODULE,               /* module type */
  NULL,                          /* init master */
  NULL,                          /* init module */
  NULL,                          /* init process */
  NULL,                          /* init thread */
  NULL,                          /* exit thread */
  NULL,                          /* exit process */
  NULL,                          /* exit master */
  NGX_MODULE_V1_PADDING
};

static ngx_int_t redis4nginx_handler(ngx_http_request_t *r)
{  
    redis4nginx_ctx_t *ctx;
    
    ctx = ngx_http_get_module_ctx(r, redis4nginx_module);
    if (ctx == NULL) {
        ctx = redis4nginx_create_ctx(r);
        if (ctx == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ngx_http_set_ctx(r, ctx, redis4nginx_module);
    }
    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET)) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    
    //ngx_redis_command(ctx, NULL, (char*)"end-1", "SET key test");
    redis4nginx_command(ctx, eval_callback, r, "get key");

    r->main->count++;
    
    return NGX_AGAIN;
}

void eval_callback(redisAsyncContext *c, void *repl, void *privdata)
{
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;
    redisReply *reply = repl;
    ngx_http_request_t *r = privdata;
    
    // set the 'Content-type' header
    r->headers_out.content_type_len = sizeof("text/html") - 1;
    r->headers_out.content_type.len = sizeof("text/html") - 1;
    ngx_str_set(&r->headers_out.content_type, "text/html");
 
    // allocate a buffer for your response body
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
 
    // attach this buffer to the buffer chain
    out.buf = b;
    out.next = NULL;
 
    // adjust the pointers of the buffer
    b->pos = (u_char*)reply->str;
    b->last = (u_char*)reply->str + sizeof(reply->str) - 1;
    b->memory = 1;    // this buffer is in memory
    b->last_buf = 1;  // this is the last buffer in the buffer chain
 
    // set the status line
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = sizeof(reply->str) - 1;
 
    // send the headers of your response
    rc = ngx_http_send_header(r);
 
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
 
    // send the buffer chain of your response
    rc = ngx_http_output_filter(r, &out);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    
    ngx_http_finalize_request(r, NGX_DONE);
}

void * redis4nginx_create_conf(ngx_conf_t *cf)
{
    redis4nginx_loc_conf_t        *conf;

    ngx_str_t *str =  cf->args->elts;
    ngx_str_t script =  str[0];
    
    conf = ngx_pcalloc(cf->pool, sizeof(redis4nginx_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    
    conf->lua_script = &script;
    
    /* set by ngx_pcalloc
     *  conf->handler_cmds = NULL
     *  conf->before_body_cmds = NULL
     *  conf->after_body_cmds = NULL
     *  conf->seen_leading_output = 0
     *  conf->seen_trailing_output = 0
     */

    return conf;
}

char *redis4nginx_entry_point(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{  
    ngx_http_core_loc_conf_t  *clcf;
    
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = redis4nginx_handler;
    
    return NGX_CONF_OK;
}