#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "redis4nginx_module.h"

char *redis4nginx_entry_point(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
void eval_callback(redisAsyncContext *c, void *repl, void *privdata);
void * redis4nginx_create_conf(ngx_conf_t *cf);
ngx_int_t redis4nginx_handler(ngx_http_request_t *r);

static ngx_command_t  redis4nginx_commands[] = {

  { ngx_string("eval"),
    NGX_HTTP_LOC_CONF|NGX_CONF_ANY,
    redis4nginx_entry_point,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(redis4nginx_loc_conf_t, handler_cmds),
    NULL },

    ngx_null_command
};

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

ngx_int_t redis4nginx_handler(ngx_http_request_t *r)
{     
    redis4nginx_loc_conf_t *elcf;
    
    redis4nginx_init_connection();
            
    elcf = ngx_http_get_module_loc_conf(r, redis4nginx_module);
    if(elcf == NULL)
        return NGX_DECLINED;

    
    // we response to 'GET' and 'HEAD' requests only 
    if (!(r->method & NGX_HTTP_GET)) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    
    //ngx_redis_command(ctx, NULL, (char*)"end-1", "SET key test");

    redis4nginx_get(r);
    
    return NGX_AGAIN;
}

static u_char json_content_type[] = "application/json";

void redis4nginx_get(ngx_http_request_t *r)
{
    redis4nginx_command(redis4nginx_send_json, r, "get key");
    r->main->count++;
}

void redis4nginx_send_json(redisAsyncContext *c, void *repl, void *privdata)
{
    redisReply *reply = repl;
    ngx_http_request_t *r = privdata;
    
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;
    
    // set the 'Content-type' header
    r->headers_out.content_type_len = sizeof(json_content_type) - 1;
    r->headers_out.content_type.len = sizeof(json_content_type) - 1;
    ngx_str_set(&r->headers_out.content_type, json_content_type);
 
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
    b->last = (u_char*)reply->str + reply->len;
    b->memory = 1;    // this buffer is in memory
    b->last_buf = 1;  // this is the last buffer in the buffer chain
 
    // set the status line
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = reply->len;
 
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
    
    conf = ngx_pcalloc(cf->pool, sizeof(redis4nginx_loc_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    return conf;
}

char *redis4nginx_entry_point(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{  
    ngx_http_core_loc_conf_t  *clcf;
    
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = redis4nginx_handler;
    
    return NGX_CONF_OK;
}