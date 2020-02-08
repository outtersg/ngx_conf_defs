ngx_conf_script
===============

Adds config-time interpreted templating to config files.

This avoids all kind of hacks in order nginx to run as a web app server (where nginx config is the mix of the main nginx config with the application-provided server fragments):
- having the path where the app will be deployed hardcoded, so that it knows how to refer to other include(s) and ssl_certificate_key(s) provided by the app
- using external tools to fill an app-provided template with nginx absolute pathes
- relying on set to define static strings (which require a runtime evaluation)

Syntax
------

/var/www/myapp/nginx.conf:
```nginx
server
{
	conf_scripts < >;
	include <.>/defs.conf;                   # Include /var/www/myapp/defs.conf
	static appname <basename(.)>;            # Define <appname> to be "myapp"
	
	server_name <appname>.local;             # Set server name to myapp.local
	
	include <.>/<apptype>-additions[.]conf;  # Include /var/www/myapp/php-additions.conf
}
```
/var/www/myapp/defs.conf:
```nginx
static apptype php;
```

### conf_scripts

Defines the opening and closing marks for config scripts.

There is NO default value; in effect, calling conf_scripts once is mandatory to use config scripting (as such, this module cannot break existing configuration, because without conf_scripts no interpretation will occur).

The defined marks are effective until one of those conditions:
- another conf_scripts occurs
- the end of the containing {} block
- the end of the containing file
As such, a contained block inherits its parent block's marks, and included files start with their includer's marks.

Any character can be used in the opening and closing marks, and they can have any length (greater than 0). Opening and closing need not have the same length.
Choose wisely to avoid conflicts with your commands' syntax. Calling multiple conf_scripts in the same block allows starting with e.g. < and >, and then going to ..o( and )o.. when entering a section of named capture regexes (where < > could be accidentally interpreted instead of being passed as is to the regex engine). 

Base syntax chars ({, }, ", ;) can be used, but then they SHALL be quoted in the define, and makes quoting mandatory in commands using them too: thus this is considered a poor decision.

### .

. is a special config script variable expanded to the current file's full path.

### static _label_ _value_

Defines the current value for a config script variable.
Its scope is the current block and its subblocks (until another define occurs).

### define <_label_> _value_

_Not implemented_

Defines the current value for a config script variable.
Its scope is the current block and its subblocks (until another define occurs).

Note that by design choice, the variable name should be surrounded by the last conf_script-defined marks.
This is coherent with set (where the $ has to be prefixed) and with cpp #define; this would allow a simpler, alternate templating engine to handle such defines without modifying the config files.
On the other hand, this prevents dynamic variable names (as with static).

But this internally defines the non-marked variable, so:
```nginx
conf_scripts < >;
define <toto> snippet;
conf_scripts [[ ]];
include [[toto]].conf;
```
will work as expected.

### Functions

_Not implemented_
