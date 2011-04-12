/*
 * Provides the cache core module. See LICENSE for license terms.
 */

#ifndef CACHE_CORE_INCLUDED
#define CACHE_CORE_INCLUDED

#include <lua.h>


/* cache fields */
#define CACHE_FDRIVER "driver"
#define CACHE_FMAP "map"
#define CACHE_FENCODE "encode"
#define CACHE_FDECODE "decode"
#define CACHE_FCONFIGURE "configure"
#define CACHE_FGET "get"
#define CACHE_FSET "set"
#define CACHE_FADD "add"
#define CACHE_FREPLACE "replace"
#define CACHE_FINC "inc"
#define CACHE_FDEC "dec"
#define CACHE_FFLUSH "flush"
#define CACHE_FCLOSE "close"

/*
 * Encodes a value. The function raises a Lua error if the encoding fails.
 *
 * @param L the Lua state
 * @return the number of results
 */ 
int cache_encode (lua_State *L);

/*
 * Decodes a value. The function raises a Lua error if the decoding fails.
 *
 * @param L the Lua state
 * @return the number of results
 */
int cache_decode (lua_State *L);

/**
 * Opens the cache module.
 *
 * @param L the Lua state
 * #return the number of results
 */ 
int luaopen_cache_core (lua_State *L);

#endif /* CACHE_CORE_INCLUDED */
