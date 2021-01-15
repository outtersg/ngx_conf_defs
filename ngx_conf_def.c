
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

#define T_END   '$'
#define T_ALPHA 'A'
#define T_NUM   '0'
#define T_FUNC  'f'
#define T_VAR   'v'
/* Parenthesis for arithmetic priority, e.g. 3 * ( 1 + 2 ) */
#define T_ARPAR 'p'
/* Undetermined parenthesis, before knowing if for a function's parameters, or in arithmetic */
#define T_PAR '('

typedef struct {
    u_char type;
    int n_ops;
    ngx_str_t text;
} ngx_conf_ccv_token_t;

int ngx_conf_ccv_compile(ngx_conf_ccv_t *ccv);
int ngx_conf_ccv_init(ngx_conf_ccv_t *ccv, ngx_conf_t *cf, ngx_str_t *value,
    ngx_uint_t n);
int ngx_conf_ccv_run(ngx_conf_ccv_t *ccv);
int ngx_conf_ccv_resolve_expr(ngx_conf_ccv_t *ccv, ngx_str_t *expr);
int ngx_conf_ccv_order_tokens(ngx_conf_ccv_token_t *tokens, int start,
    int end, u_char closer);
int ngx_conf_ccv_resolve_var(ngx_conf_ccv_t *ccv, ngx_str_t *expr);
void ngx_conf_ccv_destroy(ngx_conf_ccv_t *ccv);
ngx_str_t *ngx_conf_script_var_find(ngx_conf_script_vars_t *vars,
    ngx_str_t *name);
ngx_int_t ngx_conf_script_var_pos(ngx_conf_script_vars_t *vars,
    ngx_str_t *name);
#define ngx_array_get(type, a, pos) (((type *)(a)->elts)[pos])


static u_char charclass[256];


void *
ngx_conf_script_init(ngx_cycle_t *cycle)
{
    int pos;
    for (pos = 256; --pos > ' ';) {
        charclass[pos] = T_ALPHA;
    }
    while(--pos >= 0) {
        charclass[pos] = 0;
    }

    for (pos = '0'; pos <= '9'; ++pos) {
        charclass[pos] = T_NUM;
    }

    charclass['('] = T_PAR;
    charclass[')'] = ')';
    charclass[','] = ',';

    return charclass;
}


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
    ngx_int_t pos, end;
    ngx_uint_t *lengths = (ngx_uint_t *)alloca(expr->len * sizeof(ngx_uint_t));
    ngx_conf_ccv_token_t *tokens;

    /* get token lengths */
    for (pos = -1; ++pos < expr->len;) {
        switch (charclass[expr->data[pos]]) {
            case T_ALPHA:
            case T_NUM:
                for (end = pos;
                    ++end < expr->len
                    && (charclass[expr->data[end]] == T_ALPHA || charclass[expr->data[end]] == T_NUM);
                    /* void */ ) {
                    lengths[end] = 0;
                }
                lengths[pos] = end - pos;
                pos = end - 1;
                break;
            case 0:
                lengths[pos] = 0;
                break;
            default:
                lengths[pos] = 1;
                break;
        }
    }

    /* get tokens */
    for (end = 0, pos = -1; ++pos < expr->len;) {
        if (lengths[pos])
            ++end;
    }
    tokens = (ngx_conf_ccv_token_t *)alloca(end * sizeof(ngx_conf_ccv_token_t));
    for (end = 0, pos = -1; ++pos < expr->len;) {
        if (lengths[pos]) {
            tokens[end].type = charclass[expr->data[pos]];
            tokens[end].n_ops = 0;
            tokens[end].text.data = &expr->data[pos];
            tokens[end].text.len = lengths[pos];
            ++end;
        }
    }

    /* reorder tokens in polish notation */
    pos = ngx_conf_ccv_order_tokens(tokens, 0, end, T_END);
    if (pos < end) {
    	ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0,
    		"cannot resolve {{ %V }}: unexpected token at position %d", expr, tokens[pos].text.data - expr->data);
    	return NGX_ERROR;
    }

    return ngx_conf_ccv_resolve_var(ccv, expr);
}


int
ngx_conf_ccv_order_tokens(ngx_conf_ccv_token_t *tokens, int start,
    int end, u_char closer)
{
    int pos, pos2, prio;
    int pos_max, prio_max;
    ngx_conf_ccv_token_t token;

    for (pos = start, pos_max = -1, prio_max = -1;
            pos < end && tokens[pos].type != closer;
            /* void */ )
    {
        /* Parenthesis are immediately reduced */
        if (tokens[pos].type == T_PAR) {
            if ((pos2 = ngx_conf_ccv_order_tokens(tokens, pos + 1, end, ')')) < 0)
                return pos2;
            if (pos2 >= end || tokens[pos2].type != ')') {
                /* @todo diagno. */
                fprintf(stderr, "# Missing closing parenthesis\n");
                return -1;
            }
            tokens[pos2].type = 0;
            ++pos2;
            if (pos > start && tokens[pos - 1].type == T_ALPHA) {
                --pos;
                tokens[pos].type = T_FUNC;
                tokens[pos].n_ops = pos2 - pos;
                tokens[pos + 1].type = 0;
                ngx_conf_ccv_tokens_to_list(tokens, pos + 2, pos2 - 1);
            } else {
                tokens[pos].type = T_ARPAR;
                tokens[pos].n_ops = pos2 - pos;
            }
            /* pos does not increment, so our modified block will get reevaluated */
            continue;
        }
        switch (tokens[pos].type) {
            case 0:
                /* Erased tokens are kept in place to avoid renumbering
                 * all other tokens; just ignore it. */
                continue;
            case ')':
                /* @todo Error. We should have been detected as == closer */
                fprintf(stderr, "# ) without (\n");
                return -1;
            case ',':
                prio = 10;
                break;
            default:
                prio = 0;
                break;
        }
        if (prio > prio_max) {
            prio_max = prio;
            pos_max = pos;
        }

        pos += tokens[pos].n_ops ? tokens[pos].n_ops : 1;
    }

    if (pos < end) {
        end = pos;
    } else {
        closer = 0;
    }

    /* empty contents */
    if (prio_max < 0) {
        return pos;
    }

    if (prio_max == 0) {
        if (tokens[pos_max].type == T_ALPHA)
            tokens[pos_max].type = T_VAR;
        if (!tokens[pos_max].n_ops)
            tokens[pos_max].n_ops = 1;
        return pos_max + tokens[pos_max].n_ops;
    }

    /* operators */
    if (pos_max < end - 1)
        if ((pos2 = ngx_conf_ccv_order_tokens(tokens, pos_max + 1, end, closer)) < 0)
            return pos2;
    else
        pos2 = end;
    if (pos_max > start)
        if ((pos = ngx_conf_ccv_order_tokens(tokens, start, pos_max, T_END)) < 0)
            return pos;
    /* @todo Ensure an operator has always exactly one operand at left and one at right. */
    tokens[pos_max].n_ops = pos2 - start;
    /* polish notation */
    if (pos_max > start) {
        token = tokens[pos_max];
        memmove(&tokens[start + 1], &tokens[start], (pos_max - start) * sizeof(ngx_conf_ccv_token_t));
        tokens[start] = token;
    }

    return pos2;
}


int
ngx_conf_ccv_tokens_to_list(ngx_conf_ccv_token_t *tokens, int start,
    int end)
{
    int pos;
    u_char comma_next = 1;

    for (pos = start; pos < end; /* void */ ) {
        if (!tokens[pos].type)
            continue;
        if (comma_next) {
            if (tokens[pos].type != ',') {
                return -1;
            }
            tokens[pos].type = 0;
            ++pos;
        } else {
            /* having a left-side of , then skipping to right-side */
            pos += tokens[pos].n_ops;
        }
        comma_next ^= 1;
    }

    return 0;
}


int
ngx_conf_ccv_resolve_var(ngx_conf_ccv_t *ccv, ngx_str_t *expr)
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
                &var[1],
                var,
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
        str_comp_res = ngx_strncmp(name->data, comp->data, length_comp_res >= 0 ? comp->len : name->len);
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
