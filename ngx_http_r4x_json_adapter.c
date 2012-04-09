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
#include "ngx_http_r4x_module.h"

#include "yajl/yajl_parse.h"
#include "yajl/yajl_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char              *value;
    size_t                  len;
} ngx_http_r4x_json_field_t;

struct { 
    ngx_str_t               last_key;    
    ngx_pool_t              *pool;
    unsigned                array_started:1;
    unsigned                map_started:1;
    ngx_array_t             fields_hashes;
    ngx_hash_keys_arrays_t  tmp_hash_keys;
    
} json_state;


// config generator
static yajl_gen g = NULL;

static ngx_int_t ngx_http_r4x_add_field_to_hash(u_char *val, size_t len)
{
    ngx_str_t *field = ngx_palloc(json_state.pool, sizeof(ngx_str_t));
    field->data     = val;
    field->len      = len;

    if(ngx_hash_add_key(&json_state.tmp_hash_keys, &json_state.last_key, 
        field, NGX_HASH_READONLY_KEY) != NGX_OK)
    {
        return NGX_ERROR;
    }
    
    ngx_str_null(&json_state.last_key);
    
    return NGX_OK;
}

static int ngx_http_r4x_process_json_null(void * ctx)
{
    if(json_state.last_key.data 
        && json_state.last_key.len
        && ngx_http_r4x_add_field_to_hash((u_char*)"NIL", sizeof("NIL")-1) == NGX_OK) 
    {
        return 1;
    }
        
    return 0;
}

static int ngx_http_r4x_process_boolean(void * ctx, int boolean)
{
    const char * val = boolean ? "true" : "false";
    
    if(json_state.last_key.data 
        && json_state.last_key.len
        && ngx_http_r4x_add_field_to_hash((u_char*)val, sizeof(val)-1) == NGX_OK)
    {
        return 1;
    }
    
    return 0;
}

static int ngx_http_r4x_process_number(void * ctx, const char * val, size_t len)
{
    if(json_state.last_key.data
        && json_state.last_key.len
        && ngx_http_r4x_add_field_to_hash((u_char*)val, len) == NGX_OK)
    {
        return 1;
    }
    
    return 0;
}

static int ngx_http_r4x_process_string(void * ctx, const unsigned char * val, size_t len)
{
    if(json_state.last_key.data
        && json_state.last_key.len
        && ngx_http_r4x_add_field_to_hash((u_char*)val, len) == NGX_OK)
    {
        return 1;
    }
    
    return 0;
}

static int ngx_http_r4x_process_key(void * ctx, const unsigned char * key, size_t key_len)
{
    json_state.last_key.data    = (u_char*)key;
    json_state.last_key.len     = key_len;
     
    return 1;
}

static int ngx_http_r4x_process_start_map(void * ctx)
{
    if(json_state.map_started) {
        //TODO: logging - redis4nginx json parser doesn't supports  nested maps in the request body
        return 0;
    }
    
    json_state.tmp_hash_keys.pool = json_state.pool;
    json_state.tmp_hash_keys.temp_pool = json_state.pool;

    if (ngx_hash_keys_array_init(&json_state.tmp_hash_keys, NGX_HASH_SMALL) != NGX_OK)
        return 0;
                
    json_state.map_started = 1;
    return 1;
}

static int ngx_http_r4x_process_end_map(void * ctx)
{
    ngx_hash_init_t     hash_init;
    ngx_hash_t*         hash;
    
    if(!json_state.map_started) {
        // TODOL logging
        return 0;
    }
        
    hash                    = ngx_array_push(&json_state.fields_hashes);
    hash_init.hash          = hash;
    hash_init.key           = ngx_hash_key; 
    hash_init.max_size      = 1024*10;
    hash_init.bucket_size   = ngx_align(64, ngx_cacheline_size);
    hash_init.name          = "json_fields_hash";
    hash_init.pool          = json_state.pool;
    hash_init.temp_pool     = NULL;

    if (ngx_hash_init(&hash_init, (ngx_hash_key_t*) json_state.tmp_hash_keys.keys.elts, 
            json_state.tmp_hash_keys.keys.nelts)!=NGX_OK){
        //TODO: error logging
        return 0;
    }
        
    json_state.map_started = 0;
    return 1;
}

static int ngx_http_r4x_process_start_array(void * ctx)
{
    if(json_state.array_started) {
        //TODO: logging - redis4nginx json parser supports nested arrays in the request body
        return 0;
    }
    
    json_state.array_started = 1;
    return 1;
}

static int ngx_http_r4x_process_end_array(void * ctx)
{
    return 1;
}

yajl_callbacks callbacks = {
    ngx_http_r4x_process_json_null,
    ngx_http_r4x_process_boolean,
    NULL,
    NULL,
    ngx_http_r4x_process_number,
    ngx_http_r4x_process_string,
    ngx_http_r4x_process_start_map,
    ngx_http_r4x_process_key,
    ngx_http_r4x_process_end_map,
    ngx_http_r4x_process_start_array,
    ngx_http_r4x_process_end_array
};
   
ngx_int_t 
ngx_http_r4x_parse_request_body_as_json(ngx_http_request_t *r)
{
    yajl_handle hand;
    yajl_status stat;
    
    json_state.pool             = r->pool;
    json_state.map_started      = 0;
    json_state.array_started    = 0;
    ngx_str_null(&json_state.last_key);
    
    ngx_array_init(&json_state.fields_hashes, json_state.pool, 5, sizeof(ngx_hash_t));
    
    if(g == NULL)
        g = yajl_gen_alloc(NULL);
        
    yajl_gen_config(g, yajl_gen_beautify, 1);
    yajl_gen_config(g, yajl_gen_validate_utf8, 1);

    // ok.  open file.  let's read and parse
    hand = yajl_alloc(&callbacks, NULL, (void *) g);
    // and let's allow comments by default
    yajl_config(hand, yajl_allow_comments, 1);
    
    yajl_gen_config(g, yajl_gen_beautify, 0);
    
    stat = yajl_parse(hand, r->request_body->buf->pos, 
        r->request_body->buf->last - r->request_body->buf->pos);
    
    if (stat != yajl_status_ok) {
        //TODO: logging  str = yajl_get_error(hand, 1, jsonText, jsonTextLen);
        return NGX_ERROR;
    }
    
    stat = yajl_complete_parse(hand);
    
    if (stat != yajl_status_ok) {
        //TODO: logging str = yajl_get_error(hand, 1, jsonText, jsonTextLen);
        return NGX_ERROR;
    }
    
    return NGX_OK;
}

ngx_array_t*  ngx_http_r4x_get_parser_json()
{
    return &json_state.fields_hashes;
}