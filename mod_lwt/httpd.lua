-- Provides the LWT HTTPD module. See LICENSE for license terms.

module(..., package.seeall)

-- Imported functions from core
gpairs = pairs
pairs = httpd.core.pairs
set_content_type = httpd.core.set_content_type
add_cookie = httpd.core.add_cookie
escape_xml = httpd.core.escape_xml
escape_url = httpd.core.escape_url
input = httpd.core.input
output = httpd.core.output

-- Write template
function write_template (filename, flags, file)
	return httpd.core.write_template(filename, flags, not file and output
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
			request.hostname, uri)
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
                        write("<tr><td>", escape_html(name), "</td><td>")
                        dump(value, visited)
                        write("<td></tr>\r\n")
                end
                write("</tablebody></table>\r\n")
	elseif type(v) == "userdata" and string.sub(tostring(v), 1, 10)
			== "APR table " then
                write("<table><tablebody>\r\n")
                for name, value in pairs(v) do
                        write("<tr><td>", escape_html(name), "</td><td>")
                        dump(value, visited)
                        write("<td></tr>\r\n")
                end
                write("</tablebody></table>\r\n")
        elseif type(v) == "string" then
                httpd.write("\"", escape_html(v), "\"")
        else
                httpd.write(escape_html(tostring(v)))
        end
end

-- Matches multiple patterns in sequence and returns the captures from the
-- first matching pattern, ignoring any empty captures
function match (s, ...)
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
