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

redis4nginx_ctx* redis4nginx_get_ctx(ngx_http_request_t *r, ngx_array_t *cmd_arguments, ngx_uint_t addional_args)
{
    redis4nginx_ctx *ctx;
    ngx_uint_t i, argv_count;
    ngx_http_complex_value_t *compiled_values;
    ngx_str_t value;
    
    ctx = ngx_http_get_module_ctx(r, redis4nginx_module);
    if (ctx == NULL) {
        ctx = ngx_palloc(r->pool, sizeof(redis4nginx_ctx));
        
        if (ctx == NULL) {
            return NULL;
        }
        
        argv_count = cmd_arguments->nelts;
        compiled_values = cmd_arguments->elts;

        ctx->argvs = ngx_palloc(r->pool, sizeof(const char *) * (argv_count + addional_args));
        ctx->argv_lens = ngx_palloc(r->pool, sizeof(size_t) * (argv_count + addional_args));

        if(argv_count > 0) 
        {
            for (i = 0; i <= argv_count - 1; i++) {
                if (ngx_http_complex_value(r, &compiled_values[i], &value) != NGX_OK)
                    return NULL;

                ctx->argvs[i + addional_args] = (char *)value.data;
                ctx->argv_lens[i + addional_args] = value.len;
            }
        }

        ctx->args_count = argv_count + addional_args;
        
        ngx_http_set_ctx(r, ctx, redis4nginx_module);
    }
        
    return ctx;
}

/* Hash the scripit into a SHA1 digest. We use this as Lua function name.
 * Digest should point to a 41 bytes buffer: 40 for SHA1 converted into an
 * hexadecimal number, plus 1 byte for null term. */
void redis4nginx_hash_script(char *digest, ngx_str_t *script) {
    SHA1_CTX ctx;
    unsigned char hash[20];
    char *cset = "0123456789abcdef";
    int j;

    SHA1Init(&ctx);
    SHA1Update(&ctx,(unsigned char*)script->data, script->len);
    SHA1Final(hash,&ctx);

    for (j = 0; j < 20; j++) {
        digest[j*2] = cset[((hash[j]&0xF0)>>4)];
        digest[j*2+1] = cset[(hash[j]&0xF)];
    }
    digest[40] = '\0';
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