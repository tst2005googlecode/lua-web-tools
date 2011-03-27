#ifndef LWT_UTIL_INCLUDED
#define LWT_UTIL_INCLUDED

#include <apr_pools.h>
#include <lua.h>

/**
 * Escapes an URL.
 *
 * @param pool the memory pool
 * @param s the URL to escape
 * @return the escaped URL
 */
const char *lwt_util_escape_url(apr_pool_t *pool, const char *s);

/**
 * Lua function providing stack traceback.
 *
 * @param L the Lua state
 * @return the number of results
 */
int lwt_util_traceback (lua_State *L);

/*
 * Works like os.time(), but supports an additional "utc" * boolean field to
 * indicate that the date and time are given in Coordindated Universal Time.
 *
 * @param L the Lua state
 * @return the number of results
 */
int lwt_util_time (lua_State *L);

#endif /* LWT_UTIL_INCLUDED */
