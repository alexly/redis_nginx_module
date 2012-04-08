#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

#include "yajl/yajl_parse.h"
#include "yajl/yajl_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct {
    redis4nginx_dict_t      *json_fields_hash;
    char                    **argvs;
    size_t                  *lens;
    unsigned                key_found:1;
    ngx_uint_t              value_index;
} parser_ctx;

void redis4nginx_set_json_field(const char * s, size_t l)
{
    parser_ctx.argvs[parser_ctx.value_index] = (char*)s;
    parser_ctx.lens[parser_ctx.value_index] = l;
    parser_ctx.key_found = 0;
}

static int reformat_null(void * ctx)
{
    return 1;
}

static int reformat_boolean(void * ctx, int boolean)
{
    const char * val = boolean ? "true" : "false";
    
    if(parser_ctx.key_found) {
        redis4nginx_set_json_field(val, sizeof(val));
    }
    return 1;
}

static int reformat_number(void * ctx, const char * val, size_t len)
{
    if(parser_ctx.key_found) {
        redis4nginx_set_json_field(val, len);
    }
    return 1;
}

static int reformat_string(void * ctx, const unsigned char * string_val, size_t len)
{
    if(parser_ctx.key_found) {
        redis4nginx_set_json_field((const char*)string_val, len);
    }
    return 1;
}

static int reformat_map_key(void * ctx, const unsigned char * string_val, size_t len)
{
#ifdef USE_NGX_HASH_TABLE
    ngx_uint_t          hash_key;
    ngx_uint_t*         find;
    hash_key    =       ngx_hash_strlow((u_char*)string_val, (u_char*)string_val, len);
    
    find = (ngx_uint_t*) ngx_hash_find(parser_ctx.json_fields_hash,  hash_key, (u_char*) string_val, len);
    
    if(find) {
        parser_ctx.value_index = *find;
        parser_ctx.key_found = 1;
    }
#else
    dictEntry *find;
    ngx_str_t key;
    ngx_uint_t *field_index;
    key.data = (u_char*)string_val;
    key.len = len;
    
    find = dictFind(parser_ctx.json_fields_hash, &key);
    
    if(find != NULL) {
        field_index = (ngx_uint_t*)dictGetEntryVal(find);
        parser_ctx.value_index = *field_index;
        parser_ctx.key_found = 1;
    }   
#endif

    return 1;
}

static int reformat_start_map(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_map_open(g);
}


static int reformat_end_map(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_map_close(g);
}

static int reformat_start_array(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_array_open(g);
}

static int reformat_end_array(void * ctx)
{
    //yajl_gen g = (yajl_gen) ctx;
    return 1;//yajl_gen_status_ok == yajl_gen_array_close(g);
}

yajl_callbacks callbacks = {
    reformat_null,
    reformat_boolean,
    NULL,
    NULL,
    reformat_number,
    reformat_string,
    reformat_start_map,
    reformat_map_key,
    reformat_end_map,
    reformat_start_array,
    reformat_end_array
};
    
ngx_int_t 
redis4nginx_proces_json_fields(u_char* jsonText, size_t jsonTextLen, 
        redis4nginx_dict_t *json_fields_hash, char **argvs, size_t *lens)
{
    yajl_handle hand;
    // generator config
    yajl_gen g;
    yajl_status stat;
     
    parser_ctx.argvs = argvs;
    parser_ctx.lens = lens;
    parser_ctx.json_fields_hash = json_fields_hash;
    
    g = yajl_gen_alloc(NULL);
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