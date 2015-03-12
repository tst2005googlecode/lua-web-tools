# Introduction #

This page describes the installation of LWT on an Ubuntu Linux system. You may have to modify these instructions and the makefiles in order to compile and install LWT on other systems.



# Checkout #

Checkout LWT from its Subversion repository:

```
svn checkout http://lua-web-tools.googlecode.com/svn/trunk lwt
cd lwt
```

# Installation of mod\_lwt #

## Prerequisites ##

Apache HTTP Server developement headers:

  * apache2-dev

Lua interpreter and header files:

  * lua5.1
  * liblua5.1-dev

To install these prerequisites:

```
$ sudo apt-get install apache2-dev lua5.1 liblua5.1-dev 
```

## Building and Installing ##

### Compile ###

```
cd mod_lwt
make
```

### Install ###

The makefile installs the module in a location determined by the Apache HTTP Server `apxs` tool. The `httpd.lua` file is installed in `/usr/local/share/lua/5.1`.

```
sudo make install
cd ..
```

# Installation of the IS Module (Optional) #

The IS module provides an API to access information systems from Lua.

## Prerequisites ##

MySQL client headers:

  * libmysqlclient-dev

SQLite3 headers:

  * libsqlite3-dev

FreeTDS headers:

  * freetds-dev

To install these dependencies:

```
$ sudo apt-get install libmysqlclient-dev libsqlite3-dev freetds-dev
```

## Building and Installing ##

### Compile ###

```
cd is
make
```

### Install ###

The makefile installs the IS module in `/usr/local/lib/lua/5.1` and `/usr/local/share/lua/5.1`.

```
sudo make install
cd ..
```

# Installation of the Cache Module (Optional) #

The cache module provides an API to access memcached from Lua.

## Prerequisites ##

memcached headers:

  * libmemcached-dev

To install this dependency:

```
$ sudo apt-get install memcached libmemcached-dev 
```

## Building and Installing ##

### Compile ###

```
cd cache
make
```

### Install ###

The makefile installs the cache module in `/usr/local/lib/lua/5.1` and `/usr/local/share/lua/5.1`.

```
sudo make install
cd ..
```

# Test #

To install mod\_lwt globally, create `/etc/apache2/mods-available/lwt.conf` with the following content:

```
<IfModule mod_lwt.c>
AddHandler lwt .lua
AddHandler lwt-wsapi .ws
</IfModule>
```

(You can also copy the supplied `lwt.conf` file from the `mod_lwt/etc` directory.)

Then activate the module and restart the Apache HTTP Server:

```
$ sudo a2enmod lwt
$ sudo service apache2 restart
```

Now the Apache HTTP Server will pass `.lua` and `.ws` files globally to mod\_lwt. Of course, like any module, mod\_lwt can also be enabled on a virtual host basis.

Create a basic test page in `/var/www/test.lua`:

```
require "httpd"

local request_fields = { "uri", "protocol", "hostname", "path", "path_info", "args",
                "method", "filename", "filedir", "user", "auth_type",
                "local_ip", "remote_ip" }

request, args = ...

httpd.set_content_type("text/plain; charset=utf8")
httpd.write("Hello Lua World\r\n")
for _, key in ipairs(request_fields) do
        httpd.write(key .." -> " .. (request[key] or "(not set)") .. "\r\n")
end
```

Test the installation by pointing your browser to `http://<host>/test.lua.`