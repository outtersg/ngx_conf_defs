
/*
 * Copyright (C) Guillaume Outters
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_conf_def.h>


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
ngx_str_t *ngx_conf_script_var_find(ngx_conf_script_vars_t *vars,
    ngx_str_t *name);
ngx_int_t ngx_conf_script_var_pos(ngx_conf_script_vars_t *vars,
    ngx_str_t *name);
#define ngx_array_get(type, a, pos) (*(type *)&(a)->elts[pos])


int
ngx_conf_complex_value(ngx_conf_t *cf, ngx_str_t *string)
{
    ngx_uint_t      i, nv;
    ngx_conf_ccv_t  ccv;
    u_char         *delim_ptr;
    u_char         *delim_end;

    if (!cf->conf_file->script_delim) {
        return NGX_OK;
    }

    nv = 0;

    delim_ptr = cf->conf_file->script_delim->open.data;
    delim_end = &delim_ptr[cf->conf_file->script_delim->open.len];
    for (i = 0; i < string->len - 1; ++i) {
        if (string->data[i] == *delim_ptr) {
            do {
                if (++delim_ptr == delim_end) {
                    ++nv;
                    break;
                }
                ++i;
            } while (string->data[i] == *delim_ptr);
            delim_ptr = cf->conf_file->script_delim->open.data;
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


ngx_uint_t
ngx_conf_to_past_delim(ngx_str_t *string, ngx_uint_t *pos, ngx_str_t * delim)
{
    ngx_uint_t      i = *pos;
    u_char         *delim_pos = delim->data;
    ngx_uint_t       delim_remaining;

    while (i < string->len) {
        if (string->data[i] == *delim_pos) {
            *pos = i;
            for (delim_remaining = delim->len; /* void */ ; /* void */ ) {
                ++delim_pos;
                ++i;
                if (--delim_remaining == 0) {
                    return i;
                }
                if (i >= string->len) {
                    break;
                }
                if (string->data[i] != *delim_pos) {
                    break;
                }
            }
        } else {
            ++i;
        }
    }

    *pos = i;

    return i;
}


int
ngx_conf_ccv_compile(ngx_conf_ccv_t *ccv)
{
    ngx_uint_t      i, current_part_start, current_part_end, next_part_start;
    ngx_uint_t      prev_part_end;
    ngx_uint_t      current_part_type;

    ccv->parts.nelts          = 0;
    ccv->part_types.nelts     = 0;
    current_part_type    = NGX_CONF_TYPE_TEXT;

    for (current_part_start = 0; current_part_start < ccv->value->len;
    	/* void */ )
    {
    	switch (current_part_type) {
    	
    	case NGX_CONF_TYPE_TEXT:

            current_part_end = current_part_start;
            next_part_start = ngx_conf_to_past_delim(ccv->value,
                &current_part_end, &ccv->cf->conf_file->script_delim->open);
    		
            if (current_part_end > current_part_start) {
    			((ngx_str_t *) ccv->parts.elts)[ccv->parts.nelts].data =
    				&ccv->value->data[current_part_start];
    			((ngx_str_t *) ccv->parts.elts)[ccv->parts.nelts].len =
                    current_part_end - current_part_start;
    			++ccv->parts.nelts;
    			((ngx_uint_t *) ccv->part_types.elts)[ccv->part_types.nelts] =
    				current_part_type;
    			++ccv->part_types.nelts;
    		}
            if (next_part_start > current_part_end) {
    			current_part_type = NGX_CONF_TYPE_EXPR;
    		}
            current_part_start = next_part_start;
    		
    		break;
    		
    	case NGX_CONF_TYPE_EXPR:
    	
            prev_part_end = current_part_end;
            current_part_end = current_part_start;
            next_part_start = ngx_conf_to_past_delim(ccv->value,
                &current_part_end, &ccv->cf->conf_file->script_delim->close);
            if (next_part_start == current_part_end) {
    			ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
                   "unbalanced %V in \"%V\" at character %d",
                   &ccv->cf->conf_file->script_delim->close, ccv->value,
                   prev_part_end + 1);
    			goto e_script_parse;
    		}
    		
    		while (current_part_start < ccv->value->len
    			&& ccv->value->data[current_part_start] == ' ')
    		{
    			++current_part_start;
    		}
            for ( /* void */ ;
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
    		
            current_part_start = next_part_start;
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
        ngx_conf_script_vars_t *vars;
        ngx_str_t              *val;
        for (vars = ccv->cf->vars; vars; vars = vars->next) {
            val = ngx_conf_script_var_find(vars, expr);
            if (val) {
                expr->data = val->data;
                expr->len = val->len;
                return NGX_OK;
            }
        }
        /* TODO: if not found, return the original string (it maybe a string
         * that coincidentally used our delimiter. Make it parametrizable:
         * silent, warn, error */
    	ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
    		"not implemented: cannot resolve {{ %V }}", expr);
    	return NGX_ERROR;
    }
}


int
ngx_conf_script_var_set(ngx_conf_script_vars_t *vars, ngx_str_t *name,
    ngx_str_t *val)
{
    ngx_int_t              pos;
    ngx_conf_script_var_t *var;
    pos = ngx_conf_script_var_pos(vars, name);
    if (pos < 0) {
        pos = -1 - pos;
        var = (ngx_conf_script_var_t *)ngx_array_push(&vars->vars);
        if (!var) {
            return NGX_ERROR;
        }
        if (pos < vars->vars.nelts - 1) {
            var = &ngx_array_get(ngx_conf_script_var_t, &vars->vars, pos);
            ngx_memcpy(
                var,
                &var[1],
                (vars->vars.nelts - 1 - pos) * sizeof(ngx_conf_script_var_t));
        }
    } else {
        var = &ngx_array_get(ngx_conf_script_var_t, &vars->vars, pos);
    }
    var->name.data = name->data;
    var->name.len = name->len;
    var->val.data = val->data;
    var->val.len = val->len;

    return NGX_OK;
}


ngx_str_t *
ngx_conf_script_var_find(ngx_conf_script_vars_t *vars, ngx_str_t *name)
{
    ngx_int_t pos;
    pos = ngx_conf_script_var_pos(vars, name);
    return pos >= 0 ? &(ngx_array_get(ngx_conf_script_var_t, &vars->vars, pos).val) : NULL;
}


ngx_int_t
ngx_conf_script_var_pos(ngx_conf_script_vars_t *vars, ngx_str_t *name)
{
    /* As conf_script vars are not expected to be numerous, we do a
     * simple dichotomy instead of a full hash. */
    ngx_int_t start, middle, end;
    ngx_str_t * comp;
    ngx_int_t str_comp_res, length_comp_res;
    for (start = 0, end = vars->vars.nelts; end > start;) {
        middle = (start + end) / 2;
        comp = &(ngx_array_get(ngx_conf_script_var_t, &vars->vars, middle).name);
        length_comp_res = name->len - comp->len;
        str_comp_res = ngx_strncmp(name->data, comp->data, length_comp_res >= 0 ? name->len : comp->len);
        if (str_comp_res == 0) {
            if (length_comp_res == 0) {
                return middle;
            }
            str_comp_res += length_comp_res;
        }
        if (str_comp_res < 0) {
            start = middle + 1;
        } else {
            end = middle;
        }
    }
    return -1 - start;
}


void
ngx_conf_script_block_start(ngx_conf_t *cf)
{
    ++cf->cycle->conf_block_level;
}


void
ngx_conf_script_block_done(ngx_conf_t *cf)
{
    ngx_conf_script_vars_t *vars;

    --cf->cycle->conf_block_level;
    while (cf->vars && cf->vars->block_level > cf->cycle->conf_block_level) {
        vars = cf->vars;
        cf->vars = cf->vars->next;
        /* Do not clean array_destroy(&vars->vars) nor free(vars).
         * Having been allocated on the cf's temp pool, they may have
         * been reused now. */
    }
}
