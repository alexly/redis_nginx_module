#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

static u_char json_content_type[] = "application/json";

void redis4nginx_send_json(ngx_http_request_t *r, redisAsyncContext *c, redisReply *reply)
{   
    ngx_int_t    rc;
    ngx_buf_t   *b;
    ngx_chain_t  out;
    
    // set the 'Content-type' header
    r->headers_out.content_type_len = sizeof(json_content_type) - 1;
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