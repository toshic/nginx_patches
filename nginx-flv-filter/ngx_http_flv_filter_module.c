
/*
 * Copyright (C) Anton Kortunov <toshic.toshic@gmail.com>
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    off_t        start;
    off_t        offset;
} ngx_http_flv_filter_ctx_t;

typedef struct {
    ngx_uint_t          type;
} ngx_http_flv_filter_conf_t;

static u_char  ngx_flv_header[] = "FLV\x1\x5\0\0\0\x9\0\0\0\0";


static ngx_int_t ngx_http_flv_parse(ngx_http_request_t *r,
    ngx_http_flv_filter_ctx_t *ctx);

static ngx_int_t ngx_http_flv_filter_init(ngx_conf_t *cf);

static void * ngx_http_flv_filter_create_conf(ngx_conf_t *cf);
static char * ngx_http_flv_filter_merge_conf(ngx_conf_t *cf, void *parent, void *child);

#define NGX_FLV_FILTER_OFF      0
#define NGX_FLV_FILTER_CACHED   1
#define NGX_FLV_FILTER_ON       2

static ngx_conf_enum_t  ngx_http_flv_filter_types[] = {
    { ngx_string("off"), NGX_FLV_FILTER_OFF},
#if (NGX_HTTP_CACHE)
    { ngx_string("cached"), NGX_FLV_FILTER_CACHED},
#endif
    { ngx_string("on"), NGX_FLV_FILTER_ON},
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_http_flv_filter_commands[] = {

    { ngx_string("flv_filter"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_flv_filter_conf_t, type),
      &ngx_http_flv_filter_types},

      ngx_null_command
};


static ngx_http_module_t  ngx_http_flv_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_flv_filter_init,              /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_flv_filter_create_conf,       /* create location configuration */
    ngx_http_flv_filter_merge_conf         /* merge location configuration */
};


ngx_module_t  ngx_http_flv_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_flv_filter_module_ctx,       /* module context */
    ngx_http_flv_filter_commands,          /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;


static ngx_int_t
ngx_http_flv_header_filter(ngx_http_request_t *r)
{
    ngx_http_flv_filter_ctx_t  *ctx;
    ngx_http_flv_filter_conf_t *ffcf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http flv filter header filter");

    ffcf = ngx_http_get_module_loc_conf(r, ngx_http_flv_filter_module);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http flv filter header filter: type: %d", ffcf->type);

    switch(ffcf->type) {
        case NGX_FLV_FILTER_ON:
            break;

        case NGX_FLV_FILTER_CACHED:
            if (r->upstream && (r->upstream->cache_status == NGX_HTTP_CACHE_HIT)) {
                break;
            }

        default: /* NGX_FLV_FILTER_OFF */
            return ngx_http_next_header_filter(r);
    }

    if (r->http_version < NGX_HTTP_VERSION_10
        || r->headers_out.status != NGX_HTTP_OK
        || r != r->main
        || r->headers_out.content_length_n == -1)
    {
        return ngx_http_next_header_filter(r);
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_flv_filter_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    switch (ngx_http_flv_parse(r, ctx)) {

    case NGX_OK:
        ngx_http_set_ctx(r, ctx, ngx_http_flv_filter_module);

        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n += sizeof(ngx_flv_header) - 1 - ctx->start;

        if (r->headers_out.content_length) {
            r->headers_out.content_length->hash = 0;
            r->headers_out.content_length = NULL;
        }
        break;

    case NGX_ERROR:
        return NGX_ERROR;

    default: /* NGX_DECLINED */
        break;
    }

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_flv_parse(ngx_http_request_t *r, ngx_http_flv_filter_ctx_t *ctx)
{
    off_t              start, content_length;
    ngx_str_t          value;

    content_length = r->headers_out.content_length_n;
    start = 0;

    if (r->args.len) {
        if (ngx_http_arg(r, (u_char *) "start", 5, &value) == NGX_OK) {

            start = ngx_atoof(value.data, value.len);

            if (start == NGX_ERROR || start >= content_length || start <= 0) {
                return NGX_DECLINED;
            }
        } else {
            return NGX_DECLINED;
        }
    } else {
        return NGX_DECLINED;
    }

    ctx->start = start;
    ctx->offset = 0;

    return NGX_OK;
}

static ngx_int_t
ngx_http_flv_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_flv_filter_ctx_t  *ctx;
    off_t                       start, last;
    ngx_buf_t                  *buf;
    ngx_chain_t                *out, *cl, **ll, header;
    ngx_http_flv_filter_conf_t *ffcf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http flv filter body filter");

    if (in == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    ffcf = ngx_http_get_module_loc_conf(r, ngx_http_flv_filter_module);

    if (ffcf->type == NGX_FLV_FILTER_OFF) {
        return ngx_http_next_body_filter(r, in);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_flv_filter_module);

    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    out = NULL;
    ll = &out;

    /* Add streaming header if needed */
    if (ctx->offset == 0 && ctx->start > 0) {
        buf = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
        if (buf == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        buf->pos = ngx_flv_header;
        buf->last = ngx_flv_header + sizeof(ngx_flv_header) - 1;
        buf->memory = 1;

        header.buf = buf;
        header.next = NULL;

        *ll = &header;
        ll = &header.next;
    }

    for (cl = in; cl; cl = cl->next) {

        buf = cl->buf;

        start = ctx->offset;
        last = ctx->offset + ngx_buf_size(buf);

        ctx->offset += ngx_buf_size(buf);

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http flv filter body buf: %O-%O", start, last);

        if (ngx_buf_special(buf)) {
            *ll = cl;
            ll = &cl->next;
            continue;
        }

        if (ctx->start >= last) {

            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http flv filter body skip");

            if (buf->in_file) {
                buf->file_pos = buf->file_last;
            }

            buf->pos = buf->last;
            buf->sync = 1;

            continue;
        }

        if (ctx->start > start) {

            if (buf->in_file) {
                buf->file_pos += ctx->start - start;
            }

            if (ngx_buf_in_memory(buf)) {
                buf->pos += (size_t) (ctx->start - start);
            }
        }

        *ll = cl;
        ll = &cl->next;
    }

    if (out == NULL) {
        return NGX_OK;
    }

    return ngx_http_next_body_filter(r, out);
}

static ngx_int_t
ngx_http_flv_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_flv_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_flv_body_filter;

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "ngx_http_flv_filter_init ");
    return NGX_OK;
}

static void *
ngx_http_flv_filter_create_conf(ngx_conf_t *cf)
{
    ngx_http_flv_filter_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_flv_filter_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->type = NGX_CONF_UNSET_UINT;

    return conf;
}

static char *
ngx_http_flv_filter_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_flv_filter_conf_t *prev = parent;
    ngx_http_flv_filter_conf_t *conf = child;

    ngx_conf_merge_uint_value(conf->type, prev->type, NGX_FLV_FILTER_OFF);

    return NGX_CONF_OK;
}

