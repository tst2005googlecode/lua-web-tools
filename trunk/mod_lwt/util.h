/*
 * Provides the mod_lwt utility functions. See LICENSE for license terms.
 */

#ifndef LWT_UTIL_INCLUDED
#define LWT_UTIL_INCLUDED

#include <apr_pools.h>
#include <lua.h>

/**
 * Escapes an URI.
 *
 * @param pool the memory pool
 * @param s the URI to escape
 * @return the escaped URI
 */
const char *lwt_util_escape_uri(apr_pool_t *pool, const char *s);

/**
 * Lua function providing stack traceback.
 *
 * @param L the Lua state
 * @return the number of results
 */
int lwt_util_traceback (lua_State *L);

/*
 * Returns the Unix domain timestamp of a date and time expressed in
 * GMT.
 *
 * @param L the Lua state
 * @return the number of results
 */
int lwt_util_timegm (lua_State *L);

#endif /* LWT_UTIL_INCLUDED */
