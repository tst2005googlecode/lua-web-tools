# Introduction #

This page provides tips and tricks ("best practices") for using Lua Web Tools.



# Web Project Structure #

The following structure works well in practice:

```
<DocumentRoot>
    modules/
       modulex.lua
       moduley.lua
    templates/
       template1.html
       template2.html
    request1.lua
    request2.lua
```

The `modules` and `templates` directories are blocked from direct web access using a `.htaccess` file such as:

```
Order Deny,Allow
Deny From All
```

The Lua path is extended to include the `modules` directory, using the LuaPath directive. Shared logic is placed in modules; page-specific logic goes into the request scripts.

```
LuaPath +<DocumentRoot>/modules
```

A typical request script is structured as follows:

```
require "httpd"
require "modulex"

request, args = ...

-- Process arguments
name = httpd.match(request.path_info, "^/(%w+)$")
if not name then return 404 end -- not found
value = tonumber(args.value)

-- Process
if request.method == "POST" then
	if not value then return 400 end -- bad request
	modulex.set_value(name, value)
end

-- Prepare data for rendering
values = modulex.read_values(name)

-- Render
httpd.set_content_type("text/html; charset=utf-8")
httpd.write_template(request.filedir .. "templates/template1.html")
```

# Hiding The .lua Extension In URLs #

An easy way to remove the need for the .lua extension in URLs is to enable handler multiview negotiation in Apache:

```
MultiviewsMatch Handlers
```

A request URI of /request1 will now match /request1.lua. Please see the [Apache HTTP Server documentation](http://httpd.apache.org/docs/2.2/mod/mod_mime.html#multiviewsmatch) for more information.

# Avoiding Compilation In Production #

In a production environment, you may want to avoid the compilation of Lua scripts and modules on each request. The best way to achieve this is to invoke `luac` as part of your deployement process. LWT loads both uncompiled and compiled Lua scripts and modules. Loading a compiled Lua script is typically more efficient.

# Generating Non-HTML Output #

While a regular web page typically uses `text/html` as its response content type, LWT can also be used to generate other types of output, such as XML or JSON.

XML-style output can be naturally created with the LWT template engine. The template engine is not tied to HTML, and works just as well for custom or standardized XML formats, such as SVG. The response content type is set by calling `httpd.set_content_type`.

JSON output is commonly used to handle AJAX requests from a web page. An easy way to generate JSON from LWT is to prepare the JSON response in a table, and then encode that table into the HTTP response, using a Lua JSON library:

```
-- Create response
response = {
	-- ...
}

-- Write response
httpd.set_content_type("application/json")
httpd.write(json.encode(response))
```