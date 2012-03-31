#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

//static u_char json_content_type[] = "application/json";

static u_char null_value[] = "NULL";
static u_char array_value[] = "REDIS_REPLY_ARRAY";

static ngx_int_t parse_redis_reply(ngx_http_request_t *r, redisReply *reply, ngx_buf_t *buf) 
{
    switch(reply->type)
    {
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_ERROR:
            buf->pos = (u_char*)reply->str;
            buf->last = (u_char*)reply->str + reply->len; 
            r->headers_out.content_length_n = reply->len;
            break;
        case REDIS_REPLY_INTEGER:
        {
            char* str = ngx_palloc(r->pool, 64);
            int len;
            len = sprintf(str, "%lld", reply->integer);
            buf->pos = (u_char*)str;
            buf->last = (u_char*)str + len; 
            r->headers_out.content_length_n = len;            
            break;
        }
        case REDIS_REPLY_NIL:  
            buf->pos = (u_char*)null_value;
            buf->last = (u_char*)null_value + sizeof(null_value); 
            r->headers_out.content_length_n = sizeof(null_value);            
            break;
        case REDIS_REPLY_ARRAY:
            buf->pos = (u_char*)array_value;
            buf->last = (u_char*)array_value + sizeof(array_value); 
            r->headers_out.content_length_n = sizeof(array_value); 
            break;
    };
    
    return NGX_OK;
}

void redis4nginx_send_redis_reply(ngx_http_request_t *r, redisAsyncContext *c, redisReply *reply)
{   
    ngx_int_t    rc;
    ngx_buf_t   *buf;
    ngx_chain_t  out;
     
    // allocate a buffer for your response body
    buf = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (buf == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
 
    // attach this buffer to the buffer chain
    out.buf = buf;
    out.next = NULL;
 
    // set the status line
    r->headers_out.status = NGX_HTTP_OK;
    buf->memory = 1;    // this buffer is in memory
    buf->last_buf = 1;  // this is the last buffer in the buffer chain
    
    if(parse_redis_reply(r, reply, buf) != NGX_OK)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    
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

char * compile_complex_values(ngx_conf_t *cf, ngx_array_t *output, ngx_uint_t start_compiled_arg, ngx_uint_t num_compiled_arg)
{
    ngx_str_t                          *value;
    ngx_uint_t                          i;
    ngx_http_complex_value_t           *cv;
    ngx_http_compile_complex_value_t    ccv;
    
    value = cf->args->elts;
    
    for (i = start_compiled_arg; i < num_compiled_arg; i++)
    {
        cv = ngx_array_push(output);
        if (cv == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[i];
        ccv.complex_value = cv;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }
    
    return NGX_CONF_OK;
}