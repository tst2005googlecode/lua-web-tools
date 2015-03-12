# Overview #

The `lwt-wsapi` handler provided by mod\_lwt handles requests to the HTTP server by invoking a Lua script using [WSAPI](http://keplerproject.github.com/wsapi/). More formally, the handler performs the following steps:

  1. Test if the request is supposed to be handled by `lwt-wsapi`. If not, decline to handle the request.
  1. Test if the Lua script file exists. If not, return a HTTP 404 (Not Found) status code.
  1. Default the response content type to `text/html`.
  1. Create a new Lua state.
  1. Open the standard Lua libraries in the Lua state.
  1. Open the `httpd.core` library in the Lua state. (This is an internal library that is exposed to scripts by the `httpd` module.)
  1. Set the `package.path` and `package.cpath` variables according to the configuration directives applicable to the script.
  1. Open the `httpd.wsapi` library in the Lua state.
  1. Invoke the `httpd.wsapi.run` function, passing the _request_ and _env_ values as detailed below. This function then invokes `wsapi.common.run` to let WSAPI handle the request.

# Example #

This example shows a simple WSAPI Lua script:

```
module(..., package.seeall)

function run(wsapi_env)
	local headers = { ["Content-type"] = "text/html" }

	local function hello_text()
		coroutine.yield("<html><body>")
		coroutine.yield("<p>Hello Wsapi!</p>")
		coroutine.yield("<p>PATH_INFO: " .. wsapi_env.PATH_INFO .. "</p>")
		coroutine.yield("<p>SCRIPT_NAME: " .. wsapi_env.SCRIPT_NAME .. "</p>")
		coroutine.yield("</body></html>")
	end

	return 200, headers, coroutine.wrap(hello_text)
end
```


# The `request` Value #

The request value is identical to the request value passed by the [lwt handler](RequestProcessing.md).

# The `env` Value #

The `env` value is a table-like userdata value (APR table) and contains the subprocess environement (aka CGI environment) of the current request.

The fields of the `env` value can be accessed by name (e.g. `env.field`) or by iterating over the arguments using the `httpd.pairs` function.