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

static int reformat_null(void * ctx)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_null(g);
}

static int reformat_boolean(void * ctx, int boolean)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_bool(g, boolean);
}

static int reformat_number(void * ctx, const char * s, size_t l)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_number(g, s, l);
}

static int reformat_string(void * ctx, const unsigned char * stringVal,
                           size_t stringLen)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_string(g, stringVal, stringLen);
}

static int reformat_map_key(void * ctx, const unsigned char * stringVal,
                            size_t stringLen)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_string(g, stringVal, stringLen);
}

static int reformat_start_map(void * ctx)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_map_open(g);
}


static int reformat_end_map(void * ctx)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_map_close(g);
}

static int reformat_start_array(void * ctx)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_array_open(g);
}

static int reformat_end_array(void * ctx)
{
    yajl_gen g = (yajl_gen) ctx;
    return yajl_gen_status_ok == yajl_gen_array_close(g);
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

ngx_int_t redis4nginx_parse_json(u_char* jsonText, size_t jsonTextLen)
{
    yajl_handle hand;
    // generator config
    yajl_gen g;
    yajl_status stat;
    //unsigned char * str;
     
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