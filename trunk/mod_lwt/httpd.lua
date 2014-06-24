-- Provides the LWT HTTPD module. See LICENSE for license terms.

module(..., package.seeall)

local core = require("httpd.core")

-- Imported functions from core
gpairs = pairs
pairs = core.pairs
set_abort = core.set_abort
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
stat = core.stat

-- HTTP date
local WEEKDAYS = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }
local MONTHS = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
}
local MONTH_NUMBERS
local TIME = "%s+(%d%d):(%d%d):(%d%d)%s+"
local RFC1123_DATE = "^%s*%a+,%s+(%d+)%s+(%a+)%s+(%d+)" .. TIME .. "GMT%s*$"
local RFC850_DATE = "^%s*%a+,%s+(%d+)%-(%a+)%-(%d+)" .. TIME .. "GMT%s*$"
local ASCTIME_DATE = "^%s*%a+%s+(%a+)%s+(%d+)" .. TIME .. "(%d+)%s*$"

-- Returns a HTTP date
function date (time)
	if time == nil then return nil end
	local date = os.date("!*t", time)
	return string.format("%s, %.2d %s %.4d %.2d:%.2d:%.2d GMT",
			WEEKDAYS[date.wday], date.day, MONTHS[date.month],
			date.year, date.hour, date.min, date.sec)
end

-- Parses an HTTP date
function time (date)
	if date == nil then return nil end

	-- Match formats in order of preference
	local day, month, year, hour, min, sec = string.match(date,
			RFC1123_DATE)
	if not day then
		day, month, year, hour, min, sec = string.match(date,
				RFC850_DATE)
		if not day then
			month, day, hour, min, sec, year = string.match(date,
					ASCTIME_DATE)
			if not month then
				return nil
			end
		end
	end

	-- Decode
	year = tonumber(year)
	if year < 100 then
		if year >= 70 then
			year = year + 1900
		else
			year = year + 2000
		end
	end
	if not MONTH_NUMBERS then
		MONTH_NUMBERS = { }
		for i, month in ipairs(MONTHS) do
			MONTH_NUMBERS[string.lower(month)] = i
		end
	end
	month = MONTH_NUMBERS[string.lower(month)]
	if not month then
		return nil
	end
	day = tonumber(day)
	hour, min, sec = tonumber(hour), tonumber(min), tonumber(sec)

	-- Convert
	return core.time({
		year = year,
		month = month,
		day = day,
		hour = hour,
		min = min,
		sec = sec
	})
end

-- Cookie patterns
local COOKIE_NAME = "[^%z\001-\031\127-\255%(%)<>@,;:\\\"/%[%]%?={} \t]+"
local COOKIE_VALUE = "[]-~!#-+--:<-[]+"
local COOKIE_PAIR = "(" .. COOKIE_NAME .. ")=\"?(" .. COOKIE_VALUE .. ")\"?"

-- Adds a cookie
function add_cookie (name, value, expires, path, domain, secure, httponly)
	-- Check name and value
	if string.match(name, COOKIE_NAME) ~= name then
		error("bad name")
	end
	if string.match(value, COOKIE_VALUE) ~= value then
		error("bad value")
	end

	-- Make cookie
	local cookie = { }
	table.insert(cookie, string.format("%s=", name))
	if value then
		table.insert(cookie, string.format("%s", value))
	end
	if expires and expires >= 0 then
		table.insert(cookie, string.format("; Expires=%s",
				date(expires)))
	end
	if path then
		table.insert(cookie, string.format("; Path=%s", path))
	end
	if domain then
		table.insert(cookie, string.format("; Domain=%s", domain))
	end
	if secure then
		table.insert(cookie, "; Secure")
	end
	if httponly then
		table.insert(cookie, "; HttpOnly")
	end
	cookie = table.concat(cookie)

	-- Add header
	add_header("Set-Cookie", cookie, true)
end

-- Returns a cookie value
function cookie (request, name)
	for header, value in pairs(request.headers_in) do
		if string.lower(header) == "cookie" then
			for n, v in string.gmatch(value, COOKIE_PAIR) do
				if n == name then
					return v
				end
			end
		end
	end
	return nil
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
