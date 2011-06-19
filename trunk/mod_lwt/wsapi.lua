require "httpd"
require "wsapi.common"

module(..., package.seeall)

-- Loads an runs a WSAPI script
local function do_wsapi (wsapi_env)
	local path, file, modname, ext = wsapi.common.find_module(
			wsapi_env, wsapi_env.request.filename, "lwt.wsapi")
	if not path then
		error({ 404, string.format("Resource '%s' not found",
				wsapi_env.SCRIPT_NAME) })
	end
	if ext == "ws" then ext = "lua" end
	wsapi.app_path = path
	local app = wsapi.common.load_wsapi(path, file, modname, ext)
	wsapi_env.APP_PATH = path
	return app(wsapi_env)
end

-- Concatenates write arguments
local function concat (...)
	local cnt = select("#", ...)
	if cnt == 1 then return tostring(...) end
	local parts = { }
	for i = 1, cnt do table.insert(parts, tostring(select(i, ...))) end
	return table.concat(parts)
end

-- Adds a header
local function add_header (name, value)
	local lower_name = string.lower(name)
	if lower_name == "status" then
		httpd.set_status(tonumber(string.match(value, "^%d+")))
	elseif lower_name == "content-type" then
		httpd.set_content_type(value)
	else
		httpd.add_header(name, value)
	end
	return ""
end

-- Run a request via WSAPI
function run (request, env)
	local output, header = { }, true
	function output:write (...)
		if header then
			local s = concat(...)
			if output.remainder then s = output.remainder .. s end
			s = string.gsub(s, "([%w%-]+):%s+([^\r\n]*)\r\n",
					add_header)
			if string.sub(s, 1, 2) == "\r\n" then
				header = false
				httpd.write(string.sub(s, 3))
			elseif #s == 0 then
				output.remainder = nil
			else
				output.remainder = s
			end
		else
			httpd.write(...)
		end
	end

	local err = { }
	function err:write (...)
		httpd.err(concat(...))
	end

	local function wsapi_env (k)
		return env[k]
	end

	local t = {
		input = httpd.input,
		output = output,
		error = err,
		env = wsapi_env,
		request = request
	}
	wsapi.common.run(do_wsapi, t)
end
