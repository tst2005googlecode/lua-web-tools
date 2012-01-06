-- Provides the LWT IS module. See LICENSE for license terms.

module(..., package.seeall)

local core = require "is.core"

-- Imported functions from core
connect = core.connect
timegm = core.timegm

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
