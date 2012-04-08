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

static ngx_str_t  evalsha_command_name = ngx_string("evalsha");

static char *
ngx_http_r4x_add_directive_argument(ngx_conf_t *cf, ngx_http_r4x_directive_t *directive, 
        ngx_str_t *raw_arg, ngx_uint_t index)
{
    ngx_http_compile_complex_value_t    ccv;
    ngx_http_r4x_directive_arg_t         *directive_arg = ngx_array_push(&directive->arguments_metadata);
    ngx_uint_t                          *dict_value;
    
    switch(raw_arg->data[0])
    {
        case '$': // nginx variable            
            directive_arg->type = REDIS4NGINX_COMPILIED_ARG;
            directive_arg->compilied = ngx_palloc(cf->pool, 
                    sizeof(ngx_http_complex_value_t));

            ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

            ccv.cf = cf;
            ccv.value = raw_arg;
            ccv.complex_value = directive_arg->compilied;

            if (ngx_http_compile_complex_value(&ccv) != NGX_OK)
                return NGX_CONF_ERROR;
            break;
            
        case '@': // json field(from request body)
            
            directive_arg->type = REDIS4NGINX_JSON_FIELD_ARG;
            dict_value = ngx_palloc(cf->pool, sizeof(ngx_uint_t));
            *((int*)dict_value) = index;    
            ngx_http_r4x_copy_str(&directive_arg->string_value,  raw_arg, 1, raw_arg->len - 1, cf->pool);
            
            // prepare hash table
            if(directive->hash_elements == NULL) {
                directive->hash_elements = ngx_palloc(cf->pool, sizeof(ngx_hash_keys_arrays_t));
                
                directive->hash_elements = ngx_pcalloc(cf->temp_pool, sizeof(ngx_hash_keys_arrays_t));
                directive->hash_elements->pool = cf->pool;
                directive->hash_elements->temp_pool = cf->pool;
    
                if (ngx_hash_keys_array_init(directive->hash_elements, NGX_HASH_SMALL)
                    != NGX_OK)
                {
                    return NGX_CONF_ERROR;
                }
            }
            
            if(ngx_hash_add_key(directive->hash_elements, &directive_arg->string_value, 
                    dict_value, NGX_HASH_READONLY_KEY) != NGX_OK)
            {
                return NGX_CONF_ERROR;
            }
               
            break;

        default:
            directive_arg->type = REDIS4NGINX_STRING_ARG;
            ngx_http_r4x_copy_str(&directive_arg->string_value, 
                    raw_arg, 0, raw_arg->len, cf->pool);
            break;
    };
 
    return NGX_CONF_OK;
}

static char *
ngx_http_r4x_finalize_compile_directive(ngx_conf_t *cf, ngx_http_r4x_directive_t *directive)
{
    ngx_hash_init_t     hash_init;
    ngx_hash_t*         hash;
    
    if(directive->hash_elements != NULL) {
        hash                    = (ngx_hash_t*) ngx_pcalloc(cf->pool, sizeof(ngx_hash_t));
        hash_init.hash          = hash;
        hash_init.key           = ngx_hash_key; 
        hash_init.max_size      = 1024*10;
        hash_init.bucket_size   = ngx_align(64, ngx_cacheline_size);
        hash_init.name          = "json_fields_json";
        hash_init.pool          = cf->pool;
        hash_init.temp_pool     = NULL;
        
        if (ngx_hash_init(&hash_init, (ngx_hash_key_t*) directive->hash_elements->keys.elts, 
                directive->hash_elements->keys.nelts)!=NGX_OK){
            return NGX_CONF_ERROR;
        }
        
        //TODO: free directive->hash_elements
        directive->json_fields_hash = hash;
    }
    
    return NGX_CONF_OK;
}

char *
ngx_http_r4x_compile_directive(ngx_conf_t *cf, ngx_http_r4x_loc_conf_t * loc_conf, 
        ngx_http_r4x_srv_conf_t *srv_conf, ngx_http_r4x_directive_t *directive)
{
    ngx_str_t                          *value, *script, hash;
    ngx_uint_t                          i;
    unsigned skip_args = 1;
    
    value = cf->args->elts;
    
    directive->raw_redis_argvs = ngx_palloc(cf->pool, sizeof(const char *) * (cf->args->nelts - 1));
    directive->raw_redis_argv_lens = ngx_palloc(cf->pool, sizeof(size_t) * (cf->args->nelts - 1));
    
    if(ngx_array_init(&directive->arguments_metadata, 
            cf->pool, cf->args->nelts - 1,  sizeof(ngx_http_r4x_directive_arg_t)) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    
    if(ngx_strcmp(value[1].data, "eval") == 0)
    {    
        // skip eval beacause actual we use evalsha, and lua script, beacause it should not be compiled
        skip_args = 3;
        hash.data = ngx_palloc(cf->pool, 40);
        ngx_http_r4x_hash_script(&hash, &value[2]);
        
        // evalsha command
        ngx_http_r4x_add_directive_argument(cf, directive, &evalsha_command_name, 0);
        // sha1 script
        ngx_http_r4x_add_directive_argument(cf, directive, &hash, 1);
                
        if(srv_conf->startup_scripts == NULL)
            srv_conf->startup_scripts = ngx_array_create(cf->pool, 10, sizeof(ngx_str_t));
        
        script = ngx_array_push(srv_conf->startup_scripts);
        ngx_http_r4x_copy_str(script, &value[2], 0, (&value[2])->len, cf->pool);
    }
    
    for (i = skip_args; i < cf->args->nelts; i++)
        if(ngx_http_r4x_add_directive_argument(cf, directive, &value[i], i-1) != NGX_CONF_OK)
            return NGX_CONF_ERROR;
    
    return ngx_http_r4x_finalize_compile_directive(cf, directive);
}

ngx_int_t 
ngx_http_r4x_get_directive_argument_value(ngx_http_request_t *r, 
        ngx_http_r4x_directive_arg_t *arg, char **out, size_t *len)
{
    ngx_str_t value;
    
    switch(arg->type)
    {
        case REDIS4NGINX_JSON_FIELD_ARG:
            if(*len <=0) {
                *out = "nil";
                *len = 3;
            }
            
            break;
        case REDIS4NGINX_COMPILIED_ARG:
            if (ngx_http_complex_value(r, arg->compilied, &value) != NGX_OK)
                return NGX_ERROR;
            
            *out = (char*)value.data;
            *len = value.len;
            break;
        case REDIS4NGINX_STRING_ARG:
            *out = (char*)arg->string_value.data;
            *len = arg->string_value.len;
            break;
    };
    
    return NGX_OK;
}
