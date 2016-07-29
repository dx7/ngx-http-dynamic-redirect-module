#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>
#include <hiredis/hiredis.h>

typedef struct {
    ngx_queue_t queue;
    redisContext* redis_context;
} redis_connection;

// module location config struct
typedef struct {
    ngx_flag_t enabled;
    ngx_str_t  redis_hostname;
    ngx_uint_t redis_port;
    ngx_uint_t redis_db;
    redis_connection* redis_context;
} ngx_http_dynamic_redirect_loc_conf_t;

static redis_connection* redis_connection_queue; // sentinel
static ngx_int_t ngx_http_dynamic_redirect_init(ngx_conf_t *cf);
static void* ngx_http_dynamic_redirect_create_loc_conf(ngx_conf_t *cf);
static void ngx_http_dynamic_redirect_exit_process(ngx_cycle_t *cycle);
static redisContext* ngx_http_dynamic_redirect_redis_connect(ngx_http_dynamic_redirect_loc_conf_t* conf);
static ngx_int_t ngx_http_dynamic_redirect_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_dynamic_redirect_init_process(ngx_cycle_t *cycle);
static char* ngx_http_dynamic_redirect_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);


// module directive
static ngx_command_t ngx_http_dynamic_redirect_commands[] = {
    { ngx_string("dynamic_redirect"),                                       /* the directive string, no spaces */
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG, /* flags that indicate where the directive is legal and how many arguments it takes. http://lxr.nginx.org/source/src/core/ngx_conf_file.h */
      ngx_conf_set_flag_slot,                                               /* a pointer to a function for setting up part of the module's configuration. http://lxr.nginx.org/source/src/core/ngx_conf_file.h#L329 */
      NGX_HTTP_LOC_CONF_OFFSET,                                             /* set if this value will get saved to the main, server or location configuration */
      offsetof(ngx_http_dynamic_redirect_loc_conf_t, enabled),              /* specifies which part of this configuration struct to write to */
      NULL },                                                               /* just a pointer to other crap the module might need while it's reading the configuration */
    { ngx_string("dynamic_redirect_redis_hostname"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_dynamic_redirect_loc_conf_t, redis_hostname),
      NULL },
    { ngx_string("dynamic_redirect_redis_port"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_dynamic_redirect_loc_conf_t, redis_port),
      NULL },
    { ngx_string("dynamic_redirect_redis_db"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_dynamic_redirect_loc_conf_t, redis_db),
      NULL },
      ngx_null_command
};


// module context
static ngx_http_module_t ngx_http_dynamic_redirect_module_ctx = {
    NULL,                                      /* preconfiguration */
    ngx_http_dynamic_redirect_init,            /* postconfiguration */
    NULL,                                      /* create main configuration */
    NULL,                                      /* init main configuration */
    NULL,                                      /* create server configuration */
    NULL,                                      /* merge server configuration */
    ngx_http_dynamic_redirect_create_loc_conf, /* create location configuration */
    ngx_http_dynamic_redirect_merge_loc_conf   /* merge location configuration */
};


// module definition
ngx_module_t ngx_http_dynamic_redirect_module = {
    NGX_MODULE_V1,
    &ngx_http_dynamic_redirect_module_ctx,  /* module context */
    ngx_http_dynamic_redirect_commands,     /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    ngx_http_dynamic_redirect_init_process, /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    ngx_http_dynamic_redirect_exit_process, /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


// create location config
static void *
ngx_http_dynamic_redirect_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_dynamic_redirect_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_dynamic_redirect_loc_conf_t));
    if (!conf) {
        return NGX_CONF_ERROR;
    }
    conf->enabled = NGX_CONF_UNSET;
    conf->redis_port = NGX_CONF_UNSET_UINT;
    conf->redis_db = NGX_CONF_UNSET_UINT;
    return conf;
}


// merge location config
static char *
ngx_http_dynamic_redirect_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_dynamic_redirect_loc_conf_t *prev = parent;
    ngx_http_dynamic_redirect_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_str_value(conf->redis_hostname, prev->redis_hostname, "localhost");
    ngx_conf_merge_uint_value(conf->redis_port, prev->redis_port, 6379);
    ngx_conf_merge_uint_value(conf->redis_db, prev->redis_db, 0);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_dynamic_redirect_build_redirect_header(ngx_http_request_t *r, char* location, int location_len)
{
    ngx_http_clear_location(r);
    r->headers_out.location = ngx_list_push(&r->headers_out.headers);
    if (!r->headers_out.location) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    // sending the header
    r->headers_out.status = NGX_HTTP_MOVED_PERMANENTLY;
    r->headers_out.content_length_n = 0;
    r->headers_out.location->hash = 1;
    r->headers_out.location->key.data = (u_char*) "Location";
    r->headers_out.location->key.len = sizeof("Location") - 1;
    r->headers_out.location->value.data = (u_char*) location;
    r->headers_out.location->value.len = location_len;
    r->header_only = 1;

    return NGX_OK;
}


// the handler
static ngx_int_t
ngx_http_dynamic_redirect_handler(ngx_http_request_t *r)
{
    char* url;
    char* schema = "http";
    ngx_http_dynamic_redirect_loc_conf_t* conf = ngx_http_get_module_loc_conf(r, ngx_http_dynamic_redirect_module);

    if (!conf->enabled) {
        return NGX_DECLINED;
    }

#if (NGX_HTTP_SSL)
    if (r->connection->ssl) {
      schema = "https";
    }
#endif

    asprintf(&url, "%s://%.*s%.*s",
        schema,
        (int) r->headers_in.host->value.len, r->headers_in.host->value.data,
        (int) (r->uri_end - r->uri_start), r->uri_start
    );

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "Searching for the URI [%s] on cache.", url);

    if (!ngx_http_dynamic_redirect_redis_connect(conf)) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    redisReply* reply = redisCommand(conf->redis_context->redis_context, "GET %s", url);

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Error connecting to Redis...");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (reply->type == REDIS_REPLY_STRING) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "Redirecting to [%s].", reply->str);

        if (ngx_http_dynamic_redirect_build_redirect_header(r, reply->str, reply->len) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ngx_http_send_header(r);
        ngx_http_finalize_request(r, NGX_DONE);

        freeReplyObject(reply);

        return NGX_DONE;
    }

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "URI [%s] not found on cache.", url);

    free(url);
    return NGX_DECLINED;
}


static redisContext*
ngx_http_dynamic_redirect_redis_connect(ngx_http_dynamic_redirect_loc_conf_t* conf)
{
    if (!conf->redis_context) {
        conf->redis_context = ngx_palloc(ngx_cycle->pool, sizeof(redis_connection));

        if (!conf->redis_context) {
            return NULL;
        }
        ngx_queue_insert_tail(&redis_connection_queue->queue, &conf->redis_context->queue);
    }

    if (!conf->redis_context->redis_context || conf->redis_context->redis_context->err) {
        if (conf->redis_context->redis_context) {
            redisFree(conf->redis_context->redis_context);
        }

        conf->redis_context->redis_context = redisConnect((char *) conf->redis_hostname.data, conf->redis_port);

        if (!conf->redis_context->redis_context) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Error connecting to Redis: cannot allocate redis context");
            return NULL;
        }

        redisReply* reply = redisCommand(conf->redis_context->redis_context, "SELECT %d", conf->redis_db);

        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "Error connecting to Redis: cannot select the correct database");
            return NULL;
        }

        if (reply->type == REDIS_REPLY_STRING) {
            ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0, "Redis database [%d] selected", conf->redis_db);
            freeReplyObject(reply);
        }
    }

    return conf->redis_context->redis_context;
}


static ngx_int_t
ngx_http_dynamic_redirect_init_process(ngx_cycle_t *cycle)
{
    redis_connection_queue = ngx_palloc(cycle->pool, sizeof(redis_connection));
    if (!redis_connection_queue) {
        return NGX_ERROR;
    }

    ngx_queue_init(&redis_connection_queue->queue);
    return NGX_OK;
}


static void
ngx_http_dynamic_redirect_exit_process(ngx_cycle_t *cycle)
{
    ngx_queue_t* q;
    redis_connection* item;

    while (!ngx_queue_empty(&redis_connection_queue->queue)) {
        q = ngx_queue_head(&redis_connection_queue->queue);
        item = ngx_queue_data(q, redis_connection, queue);
        redisFree(item->redis_context);
        ngx_queue_remove(q);
    }
}


// handler installation
static ngx_int_t
ngx_http_dynamic_redirect_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (!h) {
        return NGX_ERROR;
    }

    *h = ngx_http_dynamic_redirect_handler;

    return NGX_OK;
}
