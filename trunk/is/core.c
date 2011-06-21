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
 * Lua functions.
 */
static const luaL_Reg functions[] = {
        { "connect", is_connect },
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
