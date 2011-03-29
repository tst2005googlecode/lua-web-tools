/*
 * Provides the IS core module. See LICENSE for license terms.
 */

#include <string.h>
#include <time.h>
#include <lauxlib.h>
#include "core.h"

/*
 * Connects to an information system.
 */
static int is_connect (lua_State *L) {
        luaL_checktype(L, 1, LUA_TTABLE);

        /* get driver */
        lua_getfield(L, -1, IS_FDRIVER);
        if (lua_isnil(L, -1)) {
		luaL_error(L, "required field '%s' is missing", IS_FDRIVER);
        }

        /* get function */
        lua_getfield(L, -1, IS_FCONNECT);
        if (!lua_isfunction(L, -1)) {
                luaL_error(L, "function '%s' is not supported", IS_FCONNECT);
        }

        /* call */
        lua_insert(L, 1); /* function */
	lua_pop(L, 1); /* driver */
        lua_call(L, lua_gettop(L) - 1, 1);

	return 1;
}

/*
 * Returns the value of an int field.
 */
static int getintfield (lua_State *L, int index, const char *key, int d) {
	int result;

	lua_getfield(L, index, key);
	if (lua_isnumber(L, -1)) {
		result = lua_tointeger(L, -1);
	} else {
		if (d < 0) {
			return luaL_error(L, "field %s missing in table", key);
		} else {
			result = d;
		}
	}
	lua_pop(L, 1);

	return result;
}

/*
 * Returns a timestamp based on a GMT time.
 */
static int is_timegm (lua_State *L) {
	time_t t;
	struct tm tm;

        if (lua_isnoneornil(L, 1)) {
                t = time(NULL);
        } else {
		luaL_checktype(L, 1, LUA_TTABLE);
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = getintfield(L, 1, "year", -1) - 1900;
		tm.tm_mon = getintfield(L, 1, "month", -1) - 1;
		tm.tm_mday = getintfield(L, 1, "day", -1);
		tm.tm_hour = getintfield(L, 1, "hour", 12);
		tm.tm_min = getintfield(L, 1, "min", 0);
		tm.tm_sec = getintfield(L, 1, "sec", 0);
		lua_getfield(L, 1, "isdst");
		if (lua_isnil(L, 1)) {
			tm.tm_isdst = -1;
                } else {
			tm.tm_isdst = lua_toboolean(L, -1);
                }
		lua_pop(L, 1);
		t = timegm(&tm);
		if (t == (time_t) -1) {
			lua_pushnil(L);
			return 1;
		}
	}

	lua_pushnumber(L, t);
	return 1;
}

/*
 * Lua functions.
 */
static const luaL_Reg functions[] = {
        { "connect", is_connect },
	{ "timegm", is_timegm },
        { NULL, NULL }
};

/*
 * Exported functions.
 */

int luaopen_is_core (lua_State *L) {
	const char *modname;

	/* register functions */	
	modname = luaL_checkstring(L, 1);
	luaL_register(L, modname, functions);	

	return 1;
}
