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
#include "js0n/js0n.h"
#include "ngx_http_r4x_module.h"

static u_char ngx_http_r4x_nil_json_field[] = "nil";
static u_char ngx_http_r4x_nil_json_field_len = sizeof(ngx_http_r4x_nil_json_field);

ngx_int_t
ngx_http_r4x_parse_json_request_body(ngx_http_request_t *r, ngx_http_r4x_parsed_json* parsed)
{
    int parse_result;
    
    if(r->request_body) {
        parsed->json_body = r->request_body->buf->pos;
        parsed->json_body_len = r->request_body->buf->last - r->request_body->buf->pos;
        parsed->offsets_lengths = ngx_palloc(r->pool, parsed->json_body_len);
        
        parse_result = js0n(parsed->json_body, parsed->json_body_len, parsed->offsets_lengths);
        
        return parse_result == 0 ? NGX_OK : NGX_ERROR;
    }
    
    return NGX_ERROR;
}

ngx_int_t
ngx_http_r4x_find_by_key(ngx_http_r4x_parsed_json *parsed, ngx_str_t *key, ngx_str_t *value)
{
    ngx_uint_t i, len;
    u_char* data;    

    for(i=0;parsed->offsets_lengths[i];i+=2)
    {
        //printf("%d: at %d len %d is %.*s\n",i,res[i],res[i+1],res[i+1],json+res[i]);

        data = parsed->json_body + parsed->offsets_lengths[i];
        len = parsed->offsets_lengths[i+1];

        if(len != key->len)
        continue;

        if(ngx_strncmp(key->data, data, key->len) == 0) {
            i+=2;
            value->data = parsed->json_body + parsed->offsets_lengths[i];
            value->len = parsed->offsets_lengths[i+1];
            return NGX_OK;
        }
    }

    value->data = ngx_http_r4x_nil_json_field;
    value->len = ngx_http_r4x_nil_json_field_len;

    return NGX_OK;
}

ngx_int_t
ngx_http_r4x_find_by_index(ngx_http_r4x_parsed_json *parsed, ngx_uint_t index, ngx_str_t *value)
{
    value->data = ngx_http_r4x_nil_json_field;
    value->len = ngx_http_r4x_nil_json_field_len;
    return NGX_OK;
}