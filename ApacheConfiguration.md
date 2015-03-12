# Introduction #

mod\_lwt provides an Apache handler and associated configuration directives.

# Configuring the Handlers #

The handlers provided by mod\_lwt are called `lwt` and `lwt-wsapi`. These handlers handle requests to the HTTP server by invoking a Lua script. The `lwt` handler provides the native request processing of LWT, and the `lwt-wsapi` handler provides a request processing that is compatible to [WSAPI](http://keplerproject.github.com/wsapi/).

The handlers are typically configured by having the following directives either in the global server configuration or in a virtual host configuration:

```
AddHandler lwt .lua
AddHandler lwt-wsapi .ws
```

These directives associates the `lwt` and `lwt-wsapi` handlers with the `.lua` and `.ws` file extensions. Requests for files ending in `.lua` are passed to the `lwt` handler; requests for files ending in `.ws` are passed to the `lwt-wsapi` handler.

The handlers can be added independently.

# Configuration Directives #

mod\_lwt provides several configuration directives. These directives can be used both in the server configuration and in the per-directory configuration. Use of these directives in a `.htaccess` file requires `AllowOverride Options`.

| **Directive** | **Type** | **Default** | **Description** |
|:--------------|:---------|:------------|:----------------|
| `LuaPath` | `string` | Lua default | The `LuaPath` directive configures the Lua path. If the argument begins with `+`, the remainder of the argument is appended to the current Lua path; otherwise, the argument replaces the current Lua path. See the [Lua documentation](http://www.lua.org/manual/5.1/manual.html#5.3) for information on the workings of the Lua path. |
| `LuaCPath` | `string` | Lua default | Like `LuaPath`, but configures the Lua C path instead. |
| `LuaErrorOutput` | `On` or `Off` | `On` | Determines whether or not HTML-formatted diagnostic information is written to the HTTP response when a Lua syntax error or runtime error is encountered while loading or running the Lua script. This directive has no effect in the `lwt-wsapi` handler. |