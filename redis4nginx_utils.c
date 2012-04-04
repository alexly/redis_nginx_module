#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"
#include "sha1.h"

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
            //TODO: add array to string
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

/* Hash the scripit into a SHA1 digest. We use this as Lua function name.
 * Digest should point to a 41 bytes buffer: 40 for SHA1 converted into an
 * hexadecimal number, plus 1 byte for null term. */
void redis4nginx_hash_script(ngx_str_t *digest, ngx_str_t *script) {
    SHA1_CTX ctx;
    unsigned char hash[20];
    char *cset = "0123456789abcdef";
    int j;

    SHA1Init(&ctx);
    SHA1Update(&ctx,(unsigned char*)script->data, script->len);
    SHA1Final(hash,&ctx);

    for (j = 0; j < 20; j++) {
        digest->data[j*2] = cset[((hash[j]&0xF0)>>4)];
        digest->data[j*2+1] = cset[(hash[j]&0xF)];
    }
    
    digest->len = 40;
}

char* ngx_string_to_c_string(ngx_str_t *str, ngx_pool_t *pool)
{
    char* result = NULL;
    if(str != NULL && str->len > 0) 
    {
        result = ngx_palloc(pool == NULL ? ngx_cycle->pool : pool, str->len + 1);
        memcpy(result, str->data, str->len);
        result[str->len] = '\0';   
    }
    
    return result;
}

ngx_int_t redis4nginx_copy_str(ngx_str_t *dest, ngx_str_t *src, size_t offset, size_t len, ngx_pool_t *pool)
{
    ngx_pool_t *use_pool;
    use_pool = pool == NULL ? ngx_cycle->pool : pool;
    
    dest->data = ngx_palloc(use_pool, len);
    
    if(dest->data == NULL)
        return NGX_ERROR;
    
    memcpy(dest->data, src->data + offset, len);
    dest->len = len;
    
    return NGX_OK;
}