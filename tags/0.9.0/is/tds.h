/*
 * Provides the IS TDS module. See LICENSE for license terms.
 */

#ifndef IS_TDS_INCLUDED
#define IS_TDS_INCLUDED

#include <lua.h>

/*
 * Opens the IS TDS module.
 *
 * @param L the Lua state
 * @return 1 (module)
 */
int luaopen_is_tds (lua_State *L);

#endif /* IS_TDS_INCLUDED */
