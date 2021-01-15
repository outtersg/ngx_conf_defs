
/*
 * Copyright (C) Guillaume Outters
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONF_DEF_H_INCLUDED_
#define _NGX_CONF_DEF_H_INCLUDED_


#include <ngx_core.h>


typedef struct {
    ngx_str_t open;
    ngx_str_t close;
    void *owner;
} ngx_conf_script_delim_t;


typedef struct {
    ngx_str_t name;
    ngx_str_t val;
} ngx_conf_script_var_t;


typedef struct ngx_conf_script_vars {
    ngx_array_t vars;
    ngx_uint_t block_level;
    struct ngx_conf_script_vars *next;
} ngx_conf_script_vars_t;


void * ngx_conf_script_init(ngx_cycle_t *cycle);

int ngx_conf_script_var_set(ngx_conf_script_vars_t *vars,
    ngx_str_t *name, ngx_str_t *val);
int ngx_conf_complex_value(ngx_conf_t *cf, ngx_str_t *string);

void ngx_conf_script_block_start(ngx_conf_t *cf);
void ngx_conf_script_block_done(ngx_conf_t *cf);

typedef struct {
    ngx_str_t             name;
    ngx_uint_t            type;
    ngx_str_t             (*func)(int nargs, ngx_str_t *args);
} ngx_conf_script_func_t;
extern ngx_conf_script_func_t *ngx_conf_script_functions;


#endif /* _NGX_CONF_DEF_H_INCLUDED_ */
