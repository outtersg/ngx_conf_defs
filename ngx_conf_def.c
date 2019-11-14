
/*
 * Copyright (C) Guillaume Outters
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_conf_def.h>


#define NGX_CONF_SCRIPT_DELIM_LEN 2

#define NGX_CONF_TYPE_TEXT   0
#define NGX_CONF_TYPE_EXPR   1


/* TODO: mutualize with ngx_http_script for parsing / running the mix of
 * strings and variables. */

typedef struct {
    ngx_str_t        *value;
    ngx_conf_t       *cf;
    ngx_array_t       parts;
    ngx_array_t       part_types;
} ngx_conf_ccv_t;

int ngx_conf_ccv_compile(ngx_conf_ccv_t *ccv);
int ngx_conf_ccv_init(ngx_conf_ccv_t *ccv, ngx_conf_t *cf, ngx_str_t *value,
    ngx_uint_t n);
int ngx_conf_ccv_run(ngx_conf_ccv_t *ccv);
int ngx_conf_ccv_resolve_expr(ngx_conf_ccv_t *ccv, ngx_str_t *expr);
void ngx_conf_ccv_destroy(ngx_conf_ccv_t *ccv);


int
ngx_conf_complex_value(ngx_conf_t *cf, ngx_str_t *string)
{
    ngx_uint_t      i, nv;
    ngx_conf_ccv_t  ccv;

    nv = 0;

    for (i = 0; i < string->len - 1; ++i) {
        if (string->data[i] == '{' && string->data[i + 1] == '{') {
    		++nv;
    	}
    }

    if (nv == 0) {
        return NGX_OK;
    }

    if (ngx_conf_ccv_init(&ccv, cf, string, 2 * nv + 1) != NGX_OK) {
    	goto e_ccv;
    }

    if (ngx_conf_ccv_compile(&ccv) != NGX_OK) {
    	goto e_compile;
    }

    if (ngx_conf_ccv_run(&ccv) != NGX_OK) {
    	goto e_run;
    }

    ngx_conf_ccv_destroy(&ccv);

    return NGX_OK;

e_run:
e_compile:
    ngx_conf_ccv_destroy(&ccv);
e_ccv:
    return NGX_ERROR;
}


int
ngx_conf_ccv_init(ngx_conf_ccv_t *ccv, ngx_conf_t *cf, ngx_str_t *value,
    ngx_uint_t n)
{
    ccv->value = value;
    ccv->cf = cf;

    if (ngx_array_init(&ccv->parts, cf->pool, n, sizeof(ngx_str_t)) != NGX_OK) {
        goto e_alloc_parts;
    }
    if (ngx_array_init(&ccv->part_types, cf->pool, n, sizeof(ngx_uint_t))
        != NGX_OK)
    {
        goto e_alloc_part_types;
    }

    return NGX_OK;

    ngx_array_destroy(&ccv->part_types);
e_alloc_part_types:
    ngx_array_destroy(&ccv->parts);
e_alloc_parts:
    return NGX_ERROR;
}


void
ngx_conf_ccv_destroy(ngx_conf_ccv_t *ccv)
{
    ngx_array_destroy(&ccv->parts);
    ngx_array_destroy(&ccv->part_types);
}


int
ngx_conf_ccv_compile(ngx_conf_ccv_t *ccv)
{
    ngx_uint_t      i, current_part_start, current_part_end;
    ngx_uint_t      current_part_type;

    ccv->parts.nelts          = 0;
    ccv->part_types.nelts     = 0;
    current_part_type    = NGX_CONF_TYPE_TEXT;

    for (current_part_start = 0; current_part_start < ccv->value->len;
    	/* void */ )
    {
    	switch (current_part_type) {
    	
    	case NGX_CONF_TYPE_TEXT:
    	
    		for (i = current_part_start;
    			i < ccv->value->len
    			&& (ccv->value->data[i] != '{' || ccv->value->data[i + 1] != '{');
    			/* void */ )
    		{
    			++i;
    		}
    		
    		if (i > current_part_start) {
    			((ngx_str_t *) ccv->parts.elts)[ccv->parts.nelts].data =
    				&ccv->value->data[current_part_start];
    			((ngx_str_t *) ccv->parts.elts)[ccv->parts.nelts].len =
    				i - current_part_start;
    			++ccv->parts.nelts;
    			((ngx_uint_t *) ccv->part_types.elts)[ccv->part_types.nelts] =
    				current_part_type;
    			++ccv->part_types.nelts;
    		}
    		if (i < ccv->value->len) {
    			current_part_type = NGX_CONF_TYPE_EXPR;
    		}
    		current_part_start = i;
    		
    		break;
    		
    	case NGX_CONF_TYPE_EXPR:
    	
    		for (i = current_part_start + NGX_CONF_SCRIPT_DELIM_LEN;
    			i < ccv->value->len - 1; ++i)
    		{
    			if (ccv->value->data[i] == '}') {
    				if (ccv->value->data[i + 1] == '}') {
    					break;
    				} else {
    					ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
    						"forbidden \"}\" in \"%V\" at character %d",
    						ccv->value, current_part_start + 1);
    					goto e_script_parse;
    				}
    			} else if (ccv->value->data[i] == '{') {
    				ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
    					"forbidden character \"%c\" in \"%V\""
    					" at character %d",
    					ccv->value, current_part_start + 1);
    				goto e_script_parse;
    			}
    		}
    		if (i >= ccv->value->len - 1) {
    			ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
    			   "unbalanced {{ in \"%V\" at character %d",
    			   ccv->value, current_part_start + 1);
    			goto e_script_parse;
    		}
    		
    		current_part_start += NGX_CONF_SCRIPT_DELIM_LEN;
    		while (current_part_start < ccv->value->len
    			&& ccv->value->data[current_part_start] == ' ')
    		{
    			++current_part_start;
    		}
    		for (current_part_end = i;
    			current_part_end > current_part_start
    				&& ccv->value->data[current_part_end - 1] == ' ';
    			--current_part_end)
    		{
    			/* void */
    		}
    		
    		if (current_part_end <= current_part_start) {
    			ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
    			   "invalid variable name in \"%V\" at character %d",
    			   ccv->value, current_part_start + 1);
    			goto e_script_parse;
    		}
    		
    		((ngx_str_t *) ccv->parts.elts)[ccv->parts.nelts].data =
    			&ccv->value->data[current_part_start];
    		((ngx_str_t *) ccv->parts.elts)[ccv->parts.nelts].len =
    			current_part_end - current_part_start;
    		++ccv->parts.nelts;
    		((ngx_uint_t *) ccv->part_types.elts)[ccv->part_types.nelts] =
    			current_part_type;
    		++ccv->part_types.nelts;
    		
    		current_part_start = i + NGX_CONF_SCRIPT_DELIM_LEN;
    		current_part_type = NGX_CONF_TYPE_TEXT;
    		
    		break;
    	}
    }

    return NGX_OK;

e_script_parse:

    return NGX_ERROR;
}


int
ngx_conf_ccv_run(ngx_conf_ccv_t *ccv)
{
    ngx_uint_t      i;
    ngx_str_t      *val;
    size_t          len;
    unsigned char  *ptr;

    len = 0;

    for (i = 0; i < ccv->part_types.nelts; ++i) {
    	switch (((ngx_uint_t *) ccv->part_types.elts)[i]) {
    	
    	case NGX_CONF_TYPE_TEXT:
    		val = &((ngx_str_t *) ccv->parts.elts)[i];
    		len += val->len;
    		break;
    		
    	case NGX_CONF_TYPE_EXPR:
    		val = &(((ngx_str_t *) ccv->parts.elts)[i]);
    		if (ngx_conf_ccv_resolve_expr(ccv, val) != NGX_OK) {
    			return NGX_ERROR;
    		}
    		len += val->len;
    		break;
    	}
    }

    ptr = ngx_pnalloc(ccv->cf->pool, len + 1);
    if (ptr == NULL) {
        return NGX_ERROR;
    }

    ccv->value->len = len;
    ccv->value->data = ptr;

    for (i = 0; i < ccv->part_types.nelts; ++i) {
    	switch (((ngx_uint_t *) ccv->part_types.elts)[i]) {
    	
    	case NGX_CONF_TYPE_TEXT:
    	case NGX_CONF_TYPE_EXPR:
    		val = &((ngx_str_t *) ccv->parts.elts)[i];
    		ptr = ngx_copy(ptr, val->data, val->len);
    		break;
    	}
    }

    ccv->value->data[ccv->value->len] = 0;

    return NGX_OK;
}


int
ngx_conf_ccv_resolve_expr(ngx_conf_ccv_t *ccv, ngx_str_t *expr)
{
    if (expr->len == 1 && ngx_strncmp(expr->data, ".", 1) == 0) {
    	expr->len = ccv->cf->conf_file->file.name.len;
    	expr->data = ccv->cf->conf_file->file.name.data;
        for (expr->len = ccv->cf->conf_file->file.name.len;
             expr->data[--expr->len] != '/';
             /* void */ )
        { /* void */ }
    	return NGX_OK;
    } else {
    	/* TODO: find the value of the last "define" of this context for this
    	 * variable name. */
    	ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
    		"not implemented: cannot resolve {{ %V }}", expr);
    	return NGX_ERROR;
    }
}
