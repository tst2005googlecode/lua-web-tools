-- Provides the LWT HTTPD module. See LICENSE for license terms.

module(..., package.seeall)

local core = require("httpd.core")

-- Imported functions from core
gpairs = pairs
pairs = core.pairs
set_status = core.set_status
set_content_type = core.set_content_type
add_header = core.add_header
escape_uri = core.escape_uri
escape_xml = core.escape_xml
escape_js = core.escape_js
defer = core.defer
input = core.input
output = core.output
debug = core.debug
notice = core.notice
err = core.err

-- Cookie weekdays and months
local COOKIE_WEEKDAYS = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
		"Sun" }
local COOKIE_MONTHS = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
		"Sep", "Oct", "Nov", "Dec" }

-- Returns a HTTP date
function date (time)
	if not time then return nil end
	local date = os.date("!*t", time)
	return string.format("%s, %.2d-%s-%.4d %.2d:%.2d:%.2d GMT",
			COOKIE_WEEKDAYS[date.wday], date.day,
			COOKIE_MONTHS[date.month], date.year, date.hour,
			date.min, date.sec)
end

-- Add Cookie
function add_cookie (name, value, expires, path, domain, secure, httponly)
	local cookie = { }
	table.insert(cookie, string.format("%s=", name))
	if value then table.insert(cookie, string.format("%s", value)) end
	if expires and expires >= 0 then
		table.insert(cookie, string.format("; expires=%s",
				date(expires)))
	end
	if path then
		table.insert(cookie, string.format("; path=%s", path))
	end
	if domain then
		table.insert(cookie, string.format("; domain=%s", domain))
	end
	if secure then table.insert(cookie, "; secure") end
	if httponly then table.insert(cookie, "; httponly") end

	add_header("Set-cookie", table.concat(cookie), true)
end
		
-- Write template
function write_template (filename, flags, file)
	return core.write_template(filename, flags, not file and output
			or file ~= true and file or nil)
end
	
-- Read
function read (...)
	return input:read(...)
end

-- Output
function write (...)
	return output:write(...)
end

-- Redirects
function redirect (request, uri, status)
	request.err_headers_out["Location"] = string.format("http://%s%s",
			request.headers_in["Host"] or request.hostname, uri)
	return status
end

-- Dumps a Lua value
function dump (v, visited)
        -- Track visited values to prevent eternal recursion
        visited = visited or { }
        if visited[v] then
                write("(recursion)")
                return
        end

        -- Dump values by type
        if type(v) == "table" then
                visited[v] = true
                write("<table><tablebody>\r\n")
                for name, value in gpairs(v) do
                        write("<tr><td>", escape_xml(name), "</td><td>")
                        dump(value, visited)
                        write("<td></tr>\r\n")
                end
                write("</tablebody></table>\r\n")
	elseif type(v) == "userdata" and string.sub(tostring(v), 1, 10)
			== "APR table " then
                write("<table><tablebody>\r\n")
                for name, value in pairs(v) do
                        write("<tr><td>", escape_xml(name), "</td><td>")
                        dump(value, visited)
                        write("<td></tr>\r\n")
                end
                write("</tablebody></table>\r\n")
        elseif type(v) == "string" then
                write("\"", escape_xml(v), "\"")
        else
                write(escape_xml(tostring(v)))
        end
end

-- Matches multiple patterns in sequence and returns the captures from the
-- first matching pattern, ignoring any empty captures
function match (s, ...)
	s = s or ""
	local matches
	for i = 1, select("#", ...) do
		matches = { string.match(s, (select(i, ...))) }
		if matches[1] then break end
	end
	if not matches[1] then return nil end
	local len = #matches
	for i = 1, len do
		if type(matches[i]) == "number" then matches[i] = nil end
	end
	return unpack(matches, 1, len)
end
