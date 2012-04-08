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

struct {
    ngx_hash_t      *json_fields_hash;
    char                    **argvs;
    size_t                  *lens;
    unsigned                key_found:1;
    ngx_uint_t              value_index;
    unsigned                array_json:1;
} parser_ctx;

// config generator
static yajl_gen g = NULL;

void ngx_http_r4x_set_json_field(const char * s, size_t l)
{
    parser_ctx.argvs[parser_ctx.value_index] = (char*)s;
    parser_ctx.lens[parser_ctx.value_index] = l;
    parser_ctx.key_found = 0;
}

static int ngx_http_r4x_process_json_null(void * ctx)
{
    return 1;
}

static int ngx_http_r4x_process__boolean(void * ctx, int boolean)
{
    const char * val = boolean ? "true" : "false";
    
    if(parser_ctx.key_found) {
        ngx_http_r4x_set_json_field(val, sizeof(val));
    }
    return 1;
}

static int ngx_http_r4x_process_number(void * ctx, const char * val, size_t len)
{
    if(parser_ctx.key_found) {
        ngx_http_r4x_set_json_field(val, len);
    }
    return 1;
}

static int ngx_http_r4x_process_string(void * ctx, const unsigned char * string_val, size_t len)
{
    if(parser_ctx.key_found) {
        ngx_http_r4x_set_json_field((const char*)string_val, len);
    }
    return 1;
}

static int ngx_http_r4x_process_key(void * ctx, const unsigned char * string_val, size_t len)
{
    ngx_uint_t          hash_key;
    ngx_uint_t*         find;
    hash_key    =       ngx_hash_strlow((u_char*)string_val, (u_char*)string_val, len);
    
    find = (ngx_uint_t*) ngx_hash_find(parser_ctx.json_fields_hash,  hash_key, (u_char*) string_val, len);
    
    if(find) {
        parser_ctx.value_index = *find;
        parser_ctx.key_found = 1;
    }

    return 1;
}

static int ngx_http_r4x_process_start_map(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_map_open(g);
}


static int ngx_http_r4x_process_end_map(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_map_close(g);
}

static int ngx_http_r4x_process_start_array(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_array_open(g);
}

static int ngx_http_r4x_process_end_array(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_array_close(g);
}

yajl_callbacks callbacks = {
    ngx_http_r4x_process_json_null,
    ngx_http_r4x_process__boolean,
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
ngx_http_r4x_proces_json_fields(u_char* jsonText, size_t jsonTextLen, 
        ngx_hash_t *json_fields_hash, char **argvs, size_t *lens)
{
    yajl_handle hand;
    yajl_status stat;
    
    if(g == NULL)
        g = yajl_gen_alloc(NULL);
    
    parser_ctx.argvs = argvs;
    parser_ctx.lens = lens;
    parser_ctx.json_fields_hash = json_fields_hash;
    
    yajl_gen_config(g, yajl_gen_beautify, 1);
    yajl_gen_config(g, yajl_gen_validate_utf8, 1);

    // ok.  open file.  let's read and parse
    hand = yajl_alloc(&callbacks, NULL, (void *) g);
    // and let's allow comments by default
    yajl_config(hand, yajl_allow_comments, 1);
    
    yajl_gen_config(g, yajl_gen_beautify, 0);
    
    stat = yajl_parse(hand, jsonText, jsonTextLen);
    
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
