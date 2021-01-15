#include <ngx_core.h>


static ngx_conf_script_func_t functions[] = {

    { ngx_string(""),
      0,
      NULL }
};

ngx_conf_script_func_t *ngx_conf_script_functions = functions;
