/*
 * Provices the cache memcached module. See LICENSE for license terms.
 */

#ifndef CACHE_MEMCACHED_INCLUDED
#define CACHE_MEMCACHED_INCLUDED

#include <lua.h>

/*
 * Opens the memcached module.
 *
 * @param L the Lua state
 * @return the number of results.
 */
int luaopen_cache_memcached (lua_State *L);

#endif /* CACHE_MEMCACHED_INCLUDED */
