#ifndef IS_SQLITE3_INCLUDED
#define IS_SQLITE3_INCLUDED

#include <lua.h>

/*
 * Opens the IS SQLite3 module.
 *
 * @param L the Lua state
 * @return 1 (module)
 */
int luaopen_is_sqlite3 (lua_State *L);

#endif /* IS_SQLITE3_INCLUDED */
