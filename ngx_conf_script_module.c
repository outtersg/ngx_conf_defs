
/*
 * Copyright (C) Guillaume Outters
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_module.h>


static ngx_command_t  ngx_conf_script_commands[] = {

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
