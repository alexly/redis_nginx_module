#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

static ngx_str_t  evalsha_command_name = ngx_string("evalsha");

char *redis4nginx_add_directive_argument(ngx_conf_t *cf, redis4nginx_directive_t *directive, ngx_str_t *raw_arg)
{
    ngx_http_compile_complex_value_t    ccv;
    redis4nginx_directive_arg_t         *directive_arg = ngx_array_push(&directive->arguments);
    
    switch(raw_arg->data[0])
    {
        case '$': // nginx variable            
            directive_arg->type = REDIS4NGINX_COMPILIED_ARG;
            directive_arg->compilied = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));

            ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

            ccv.cf = cf;
            ccv.value = raw_arg;
            ccv.complex_value = directive_arg->compilied;

            if (ngx_http_compile_complex_value(&ccv) != NGX_OK)
                return NGX_CONF_ERROR;
            break;
        case '@': // json field(from request body)
        {
            directive_arg->type = REDIS4NGINX_JSON_FIELD_ARG;
            redis4nginx_copy_str(&directive_arg->string_value, raw_arg, 1, raw_arg->len - 1, cf->pool);
            break;
        }
        default:
            directive_arg->type = REDIS4NGINX_STRING_ARG;
            redis4nginx_copy_str(&directive_arg->string_value, raw_arg, 0, raw_arg->len, cf->pool);
            break;
    };
 
    return NGX_CONF_OK;
}

char *redis4nginx_compile_directive_arguments(ngx_conf_t *cf, redis4nginx_loc_conf_t * loc_conf, 
                        redis4nginx_srv_conf_t *srv_conf, redis4nginx_directive_t *directive)
{
    ngx_str_t                          *value, *script, hash;
    ngx_uint_t                          i;
    unsigned skip_args = 1;
    
    value = cf->args->elts;
    
    if(ngx_array_init(&directive->arguments, cf->pool, cf->args->nelts - 1,  sizeof(redis4nginx_directive_arg_t)) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    
    if(ngx_strcmp(value[1].data, "eval") == 0)
    {    
        // skip eval beacause actual we use evalsha, and lua script, beacause it should not be compiled
        skip_args = 3;
        hash.data = ngx_palloc(cf->pool, 40);
        redis4nginx_hash_script(&hash, &value[2]);
        
        // evalsha command
        redis4nginx_add_directive_argument(cf, directive, &evalsha_command_name);
        // sha1 script
        redis4nginx_add_directive_argument(cf, directive, &hash);
                
        if(srv_conf->startup_scripts == NULL)
            srv_conf->startup_scripts = ngx_array_create(cf->pool, 10, sizeof(ngx_str_t));
        
        script = ngx_array_push(srv_conf->startup_scripts);
        redis4nginx_copy_str(script, &value[2], 0, (&value[2])->len, cf->pool);
    }
    
    for (i = skip_args; i < cf->args->nelts; i++)
        if(redis4nginx_add_directive_argument(cf, directive, &value[i]) != NGX_CONF_OK)
            return NGX_CONF_ERROR;
    
    return NGX_CONF_OK;
}

ngx_int_t redis4nginx_get_directive_argument_value(ngx_http_request_t *r, redis4nginx_directive_arg_t *arg, ngx_str_t* out)
{
    switch(arg->type)
    {
        case REDIS4NGINX_JSON_FIELD_ARG:
            return NGX_ERROR;//todo: fix
            break;
        case REDIS4NGINX_COMPILIED_ARG:
            if (ngx_http_complex_value(r, arg->compilied, out) != NGX_OK)
                return NGX_ERROR;
            break;
        case REDIS4NGINX_STRING_ARG:
            out->data = arg->string_value.data;
            out->len = arg->string_value.len;
            break;
    };
    
    return NGX_OK;
}