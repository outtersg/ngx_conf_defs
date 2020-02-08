
/*
 * Copyright (C) Guillaume Outters
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_module.h>


char *ngx_conf_scripts(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
char *ngx_conf_script_start(ngx_conf_t *cf, ngx_str_t *open_delim,
    ngx_str_t *close_delim);
char *ngx_conf_script_end(ngx_conf_t *cf);
char *ngx_cscript_static(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_conf_script_commands[] = {

    { ngx_string("conf_scripts"),
      NGX_ANY_CONF|NGX_CONF_TAKE1|NGX_CONF_TAKE2,
      ngx_conf_scripts,
      0,
      0,
      NULL },

    { ngx_string("static"),
      NGX_ANY_CONF|NGX_CONF_TAKE2,
      ngx_cscript_static,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_module_t  ngx_conf_script_module = {
    NGX_MODULE_V1,
    NULL,                                  /* module context */
    ngx_conf_script_commands,              /* module directives */
    NGX_CONF_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


char *
ngx_conf_scripts(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char        *rv;
    ngx_str_t   *args;

    args = cf->args->elts;

    if (cf->args->nelts == 3) {
        if (!args[1].len || !args[2].len) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "conf_scripts delimiters should be non-empty");
            return NGX_CONF_ERROR;
        }

        rv = ngx_conf_script_start(cf, &args[1], &args[2]);
    } else {
        if (args[1].len != 3 || ngx_strncmp(args[1].data, "end", 3) != 0) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "conf_scripts should be passed either the "
                               "opening and closing delimiters (to start a "
                               "script section), or \"end\".");
            return NGX_CONF_ERROR;
        }

        rv = ngx_conf_script_end(cf);
    }

    return rv;
}


char *
ngx_conf_script_start(ngx_conf_t *cf, ngx_str_t *open_delim, ngx_str_t *close_delim)
{
    char                     *rv;
    ngx_conf_script_delim_t  *delim;

    if (!cf->conf_file->script_delim
        || cf->conf_file->script_delim->owner != cf->conf_file) {
        delim = ngx_palloc(cf->temp_pool, sizeof(ngx_conf_script_delim_t));
        if (!delim) {
            return NGX_CONF_ERROR;
        }
        delim->owner = cf->conf_file;
        cf->conf_file->script_delim = delim;
    } else {
        delim = cf->conf_file->script_delim;
    }

    ngx_memcpy(&delim->open, open_delim, sizeof(ngx_str_t));
    ngx_memcpy(&delim->close, close_delim, sizeof(ngx_str_t));

    return NGX_CONF_OK;
}


char *
ngx_conf_script_end(ngx_conf_t *cf)
{
    if (!cf->conf_file->script_delim) {
        return NGX_CONF_OK;
    }

    ngx_free(cf->conf_file->script_delim);
    cf->conf_file->script_delim = NULL;

    return NGX_CONF_OK;
}


char *
ngx_cscript_static(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                   *rv;
    ngx_str_t              *args;
    ngx_conf_script_vars_t *vars;

    if (!cf->vars || cf->vars->block_level < cf->block_level) {
        vars = ngx_palloc(cf->temp_pool, sizeof(ngx_conf_script_vars_t));
        if (!vars) {
            return NGX_CONF_ERROR;
        }
        /* By putting our temp string in a temp pool, better use the
         * result as soon as read or copy it to a more protected store. */
        if(ngx_array_init(&vars->vars, cf->temp_pool, 4, sizeof(ngx_conf_script_var_t)) != NGX_OK) {
            ngx_free(vars);
            return NGX_CONF_ERROR;
        }
        vars->block_level = cf->block_level;
        vars->next = cf->vars;
        cf->vars = vars;
    }

    args = cf->args->elts;

    if (ngx_conf_complex_value(cf, &args[2]) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_script_var_set(cf->vars, &args[1], &args[2])
        != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "could not set var %s", args[1].data);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
