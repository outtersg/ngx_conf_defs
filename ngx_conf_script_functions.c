#include <ngx_core.h>


static ngx_str_t dot = ngx_string(".");


ngx_str_t
ncs_dirname(int nargs, ngx_str_t *args)
{
    int end;

    /* remove tail slashes */
    for (end = args[0].len; end > 1 && args[0].data[--end] == '/';
            /* void */)
        { /* void */ }
    while (--end >= 0 && args[0].data[end] != '/')
    { /* void */ }

    switch (end) {
        case -1: return dot;
        case 0: ++end; break;
    }
    args[0].len = end;
    return args[0];
}


ngx_str_t
ncs_basename(int nargs, ngx_str_t *args)
{
    int start, end;

    /* remove tail slashes */
    for (start = args[0].len; --start > 0 && args[0].data[start] == '/';
        /* void */)
    { /* void */ }
    for (end = start + 1; --start >= 0 && args[0].data[start] != '/';
        /* void */ )
    { /* void */ }

    ++start;
    args[0].len = end - start;
    args[0].data += start;

    return args[0];
}


static ngx_conf_script_func_t functions[] = {

    { ngx_string("dirname"),
      NGX_CONF_TAKE1,
      ncs_dirname },

    { ngx_string("basename"),
      NGX_CONF_TAKE1|NGX_CONF_TAKE2,
      ncs_basename },

    { ngx_string(""),
      0,
      NULL }
};

ngx_conf_script_func_t *ngx_conf_script_functions = functions;
