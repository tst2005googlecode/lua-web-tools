# Overview #

The `lwt` handler provided by mod\_lwt handles requests to the HTTP server by invoking a Lua script. More formally, the handler performs the following steps:

  1. Test if the request is supposed to be handled by `lwt`. If not, decline to handle the request.
  1. Test if the Lua script file exists. If not, return a HTTP 404 (Not Found) status code.
  1. Default the response content type to `text/html`.
  1. Create a new Lua state.
  1. Open the standard Lua libraries in the Lua state.
  1. Open the `httpd.core` library in the Lua state. (This is an internal library that is exposed to scripts by the `httpd` module.)
  1. Set the `package.path` and `package.cpath` variables according to the configuration directives applicable to the script.
  1. Load the Lua script to run. Return a HTTP 500 (Internal Server Error) status code if a syntax error, an out of memory error or an IO error is encountered.
  1. Invoke the script, passing the _request_ and _args_ values as detailed below. Return a HTTP 500 (Internal Server Error) status code if a runtime error, an out of memory error or error handler error is encountered.
  1. If the script returns no value or `nil`, assume the request was handled successfully, and respond with a default HTTP 200 (OK) status code, or the specific status code set by the `httpd.set_status` function; if the script returns a number between 100 and 599, use that number as the HTTP status code; assume a HTTP 500 (Internal Server Error) status code for all other return values. Any return value other than none or `nil` defers processing to the Apache error document handling.

Note that a new Lua state is created for each request and that mod\_lwt is thus **completely stateless**. This is a deliberate design decision. If you need to maintain state between requests, use tools such as HTTP arguments, cookies, caches and databases in a combination that is suitable to your requirements.

# Example #

This example shows a simple Lua script for mod\_lwt.

```
require "httpd" -- Import the httpd module

request, args = ... -- Get the request and args values passed to the script

name = args.name or "Unknown" -- Process arguments

httpd.set_content_type("text/plain") -- Set content type
httpd.write("Hello, " .. name .. "!\r\n")
httpd.write("Welcome to " .. request.hostname .. "!\r\n")
```

For the sake of simplicity, this example does not use the mod\_lwt template engine.

# The `request` Value #

The `request` value is passed as the first argument to a Lua script. It is a table-like userdata value, and it contains information about the current request. The following table shows the fields of the `request` value:

| **Field** | **Description**|
|:----------|:---------------|
| `uri` |  The URI of the request, e.g. `/test.lua/additional/path/segments?name=value&name2=value2`. |
| `protocol` | The protocol used in the request, e.g. `HTTP/1.1`. |
| `hostname` | The host of the request, e.g. `www.mydomain.com`. |
| `path` | The path portion of the URI, e.g. `/test.lua` |
| `path_info` | Any path information following the requested Lua script, including the beginning slash, e.g. `/additonal/path/segments`. |
| `args` | The arguments portion of the URI, e.g. `name=value&name2=value2`. |
| `method` | The request method, e.g. `GET` or `POST`. |
| `headers_in` | A table-like userdata value containing the request input headers. The table can be iterated with the `httpd.pairs` function (or the regular `pairs` function as of Lua 5.2). |
| `headers_out` | A table-like userdata value containing the request output headers. The table can be iterated with the `httpd.pairs` function (or the regular `pairs` function as of Lua 5.2). These headers are only transmitted for successful request, i.e. requests returning no value or `nil`. For example, a `Location` header used in a redirect should _not_ go here as these headers would not be transmitted if the script returns a 3xx redirect HTTP status code. |
| `err_headers_out` | A table-like userdata value containing the request output headers. The table can be iterated with the `httpd.pairs` function (or the regular `pairs` function as of Lua 5.2).  These headers are transmitted regardless of the outcome of the request. For example, a `Location` header used in a redirect should go here. |
| `filename` | The qualified filename of the requested Lua script. |
| `filedir` | The qualified directory where the requested Lua script resides, including a trailing slash. |
| `user` | The authenticated user. |
| `auth_type` | The type of authentication, e.g. `Basic`. |
| `local_ip` | The local IP address (responding HTTP server). |
| `remote_ip` | The remote IP address (requesting HTTP client). |

Note that depending on the request and server configuration, some fields may be absent and evaluate as `nil` when queried.

Also note that the `request` value is a userdata value that cannot be iterated with the Lua `pairs` or the mod\_lwt `httpd.pairs` functions.

# The `args` Value #

The `args` value is passed as the second argument to a Lua script. It is a table-like userdata value (APR table) and contains the arguments passed with the current request. More specifically, mod\_lwt decodes the following types of arguments in the specified order:

  1. arguments from the URI
  1. arguments from the body with a content type of `application/x-www-form-urlencoded` (regular form post), or `multipart/form-data` (typically used for file uploads)

The fields of the `args` value can be accessed by name (e.g. `args.field`) or by iterating over the arguments using the `httpd.pairs` function (or the regular `pairs` function as of Lua 5.2). The iteration method must be used if there are multiple values with the same name in the arguments.

For uploaded files, the `args` value contains the temporary file name of the uploaded file. Note that these temporary files are deleted at the end of the request.

For body content types other than the ones indicated, no processing is done by mod\_lwt, and the Lua script can process the request body directly.