-- Provides the LWT IS module. See LICENSE for license terms.

module(..., package.seeall)

-- Imported functions from core
require "is.core"
connect = is.core.connect
timegm = is.core.timegm

-- Trims a string
function trim (s)
	if not s then return nil end
	return (string.gsub(s, "^%s*(.-)%s*$", "%1"))
end

-- Returns a datetime formatted for the database
function date (time)
	if not time then return nil end
	return os.date("!%Y-%m-%d %H:%M:%S", time)
end

-- Returns a time from a database datetime
function time (date)
	if not date then return nil end

	-- Return time based on datetime
	local table = {
		year = tonumber(date:sub(1, 4)),
		month = tonumber(date:sub(6, 7)),
		day = tonumber(date:sub(9, 10)),
		hour = tonumber(date:sub(12, 13)),
		min = tonumber(date:sub(15, 16)),
		sec = tonumber(date:sub(18, 19)),
	}
	return timegm(table)
end

-- Returns a time from a database date
function date_time (date)
	if not date then return nil end
	local table = {
		year = tonumber(date:sub(1, 4)),
		month = tonumber(date:sub(6, 7)),
		day = tonumber(date:sub(9, 10))
	}
	return os.time(table)
end
