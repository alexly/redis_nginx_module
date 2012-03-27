#ifndef DDEBUG
#define DDEBUG 0
#endif

#include "ddebug.h"
#include "redis4nginx_module.h"

static void* redis4nginx_create_srv_conf(ngx_conf_t *cf);
static char* redis4nginx_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child);
static void* redis4nginx_create_loc_conf(ngx_conf_t *cf);
static char* redis4nginx_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_command_t  redis4nginx_commands[] = {
    {	ngx_string("redis_host"),
		NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_SRV_CONF_OFFSET,
		offsetof(redis4nginx_srv_conf_t, host),
		NULL },
    
	{	ngx_string("redis_port"),
		NGX_HTTP_SRV_CONF|NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_SRV_CONF_OFFSET,
		offsetof(redis4nginx_srv_conf_t, port),
		NULL },
        
    {   ngx_string("eval"),
        NGX_HTTP_LOC_CONF|NGX_CONF_ANY,
        redis4nginx_eval_handler_init,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL },
        
    ngx_null_command
};

static ngx_http_module_t  redis4nginx_module_ctx = {
  NULL,                          // preconfiguration
  NULL,                          // postconfiguration

  NULL,                          // create main configuration
  NULL,                          // init main configuration

  redis4nginx_create_srv_conf,  // create server configuration 
  redis4nginx_merge_srv_conf,   // merge server configuration

  redis4nginx_create_loc_conf,  // create location configuration */
  redis4nginx_merge_loc_conf    // merge location configuration */
};


ngx_module_t redis4nginx_module = {
  NGX_MODULE_V1,
  &redis4nginx_module_ctx, /* module context */
  redis4nginx_commands,   /* module directives */
  NGX_HTTP_MODULE,               /* module type */
  NULL,                          /* init master */
  NULL,                          /* init module */
  NULL,                          /* init process */
  NULL,                          /* init thread */
  NULL,                          /* exit thread */
  NULL,                          /* exit process */
  NULL,                          /* exit master */
  NGX_MODULE_V1_PADDING
};

static void* redis4nginx_create_srv_conf(ngx_conf_t *cf)
{
	redis4nginx_srv_conf_t *conf = ngx_pcalloc(cf->pool, sizeof(redis4nginx_srv_conf_t));

	conf->port = NGX_CONF_UNSET;

	return conf;
}

static char* redis4nginx_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
	redis4nginx_srv_conf_t *prev = parent;
	redis4nginx_srv_conf_t *conf = child;

	ngx_log_debug0(NGX_LOG_INFO, cf->log, 0, "mysql merge srv");

	ngx_conf_merge_str_value(conf->host,
			prev->host, NULL);

	ngx_conf_merge_value(conf->port,
			prev->port, NGX_CONF_UNSET);

	return NGX_CONF_OK;
}

static void* redis4nginx_create_loc_conf(ngx_conf_t *cf)
{
	redis4nginx_loc_conf_t *conf = ngx_pcalloc(cf->pool, sizeof(redis4nginx_loc_conf_t));

	return conf;
}

static char* redis4nginx_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
	redis4nginx_loc_conf_t *prev = parent;
	redis4nginx_loc_conf_t *conf = child;

	ngx_log_debug0(NGX_LOG_INFO, cf->log, 0, "redis4nginx merge loc");

	if (conf->query_lengths == NULL)
		conf->query_lengths = prev->query_lengths;

	if (conf->query_values == NULL)
		conf->query_values = prev->query_values;


	return NGX_CONF_OK;
}