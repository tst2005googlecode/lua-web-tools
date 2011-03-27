#ifndef IS_MYSQL_INCLUDED
#define IS_MYSQL_INCLUDED

#include <lua5.1/lua.h>

/*
 * Opens the IS MySQL module.
 *
 * @param L the Lua state
 * @return 1 (module)
 */
int luaopen_is_mysql (lua_State *L);

#endif /* IS_MYSQL_INCLUDED */
