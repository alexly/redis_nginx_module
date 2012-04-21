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
        ngx_str_t *raw_arg, ngx_http_r4x_loc_conf_t * loc_conf)
{
    ngx_http_compile_complex_value_t    ccv;
    ngx_http_r4x_directive_arg_t        *directive_arg = ngx_array_push(&directive->arguments);
    
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
            directive_arg->type = REDIS4NGINX_JSON_FIELD_NAME_ARG;
            directive->require_json_field       = 1;
            loc_conf->require_json_field        = 1;
            ngx_http_r4x_copy_str(&directive_arg->value,  raw_arg, 1, raw_arg->len - 1, cf->pool);
            break;

        default:
            directive_arg->type = REDIS4NGINX_STRING_ARG;
            ngx_http_r4x_copy_str(&directive_arg->value, raw_arg, 0, raw_arg->len, cf->pool);
            break;
    };
 
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
    
    directive->cmd_argvs = ngx_palloc(cf->pool, sizeof(const char *) * (cf->args->nelts - 1));
    directive->cmd_argv_lens = ngx_palloc(cf->pool, sizeof(size_t) * (cf->args->nelts - 1));
    
    if(ngx_array_init(&directive->arguments, 
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
        ngx_http_r4x_add_directive_argument(cf, directive, &hash, loc_conf);
                
        if(srv_conf->eval_scripts == NULL)
            srv_conf->eval_scripts = ngx_array_create(cf->pool, 10, sizeof(ngx_str_t));
        
        script = ngx_array_push(srv_conf->eval_scripts);
        ngx_http_r4x_copy_str(script, &value[2], 0, (&value[2])->len, cf->pool);
    }
    
    for (i = skip_args; i < cf->args->nelts; i++)
        if(ngx_http_r4x_add_directive_argument(cf, directive, &value[i], loc_conf) != NGX_CONF_OK)
            return NGX_CONF_ERROR;
    
    return NGX_CONF_OK;
}