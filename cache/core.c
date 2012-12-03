/*
 * Provides the cache core module. See LICENSE for license terms.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>
#include <lua.h>
#include <lauxlib.h>
#include "core.h"

/*
 * Lua 5.1 compatibility.
 */
#if LUA_VERSION_NUM < 502
#define lua_rawlen(L, i) lua_objlen(L, (i))
#endif

/*
 * Initial size of a buffer.
 */
#define CACHE_BUFFER_INITSIZE 4096

/*
 * Backref record.
 */
typedef struct backref_rec {
	int index;
	int cnt;
} backref_rec;

/*
 * Frees the memory associated with a cache buffer.
 */
static int buffer_free (lua_State *L) {
	cache_buffer *B;

	B = (cache_buffer *) luaL_checkudata(L, -1, CACHE_BUFFER_METATABLE);
	if (B->b != NULL) {
		free(B->b);
		B->b = NULL;
	}
	return 0;
}

/*
 * Returns the contents of a cache buffer as a string.
 */
static int buffer_tostring (lua_State *L) {
	cache_buffer *B;
	
	B = (cache_buffer *) luaL_checkudata(L, -1, CACHE_BUFFER_METATABLE);
	if (B->b != NULL) {
		lua_pushlstring(L, B->b, B->pos);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

/*
 * Ensures the requried buffer capacity.
 */
static void require (lua_State *L, cache_buffer *B, size_t cnt) {
	char *new;

	/* nothing to do? */
	if (B->pos + cnt <= B->capacity) {
		return;
	}

	/* grow exponentially */
	do {
		B->capacity = B->capacity * 2;
	} while (B->pos + cnt > B->capacity);

	/* reallocate */
	new = realloc(B->b, B->capacity);
	if (!new) {
		luaL_error(L, "encoding error: out of memory");
	}
	B->b = new;
}

/*
 * Returns whether there is space available for reading in a buffer.
 */
static void avail (lua_State *L, cache_buffer *B, size_t cnt) {
	if (B->pos + cnt > B->capacity) {
		luaL_error(L, "decding error: input ends unexpectedly");
	}
}

/*
 * Returns whether a Lua type is supported.
 */
static int supported (lua_State *L, int index) {
	switch (lua_type(L, index)) {
	case LUA_TBOOLEAN:
	case LUA_TNUMBER:
	case LUA_TSTRING:
	case LUA_TTABLE:
		return 1;

	case LUA_FUNCTION:
		return !lua_iscfunction(L, index);

	default:
		return 0;
	}
}

/*
 * Encodes the value at the specified index.
 */	
static void encode (lua_State *L, cache_buffer *B, backref_rec *br, int index) {
	double d;
	uint32_t u, nu, narr, nrec;
	size_t narr_pos, nrec_pos;

	switch (lua_type(L, index)) {
	case LUA_TBOOLEAN:
		require(L, B, 2);
		B->b[B->pos++] = (char) LUA_TBOOLEAN;
		B->b[B->pos++] = (char) lua_toboolean(L, index);	
		break;

	case LUA_TNUMBER:
		require(L, B, 1 + sizeof(d));
		B->b[B->pos++] = (char) LUA_TNUMBER;
		d = lua_tonumber(L, index);
		memcpy(&B->b[B->pos], &d, sizeof(d));
		B->pos += sizeof(d);
		break;
		
	case LUA_TSTRING:
		u = (uint32_t) lua_rawlen(L, index);
		require(L, B, 1 + sizeof(nu) + u);
		B->b[B->pos++] = (char) LUA_TSTRING;
		nu = htobe32(u);
		memcpy(&B->b[B->pos], &nu, sizeof(nu));
		B->pos += sizeof(nu);
		memcpy(&B->b[B->pos], lua_tostring(L, index), (size_t) u);
		B->pos += u;
		break;

	case LUA_TTABLE:
		/* check stack */
		luaL_checkstack(L, 2, "encoding table");

		/* test if the table has already been encoded */
		lua_pushvalue(L, index);
		lua_rawget(L, br->index);
		if (lua_isnil(L, -1)) {
			/* store table for backrefs */
			lua_pop(L, 1);
			lua_pushvalue(L, index);
			lua_pushinteger(L, ++br->cnt);
			lua_rawset(L, br->index);
		} else {
			/* encode backref */
			require(L, B, 1 + sizeof(nu));
			B->b[B->pos++] = (char) LUA_TTABLE + 64;
			u = (uint32_t) lua_tointeger(L, -1);
			nu = htobe32(u);
			memcpy(&B->b[B->pos], &nu, sizeof(nu));
			B->pos += sizeof(nu);
			lua_pop(L, 1);
			return;
		}

		/* analyze and write table */
		require(L, B, 1 + sizeof(narr) + sizeof(nrec));
		B->b[B->pos++] = (char) LUA_TTABLE;
		narr_pos = B->pos;
		B->pos += sizeof(narr);
		nrec_pos = B->pos;
		B->pos += sizeof(nrec);
		narr = 0;
		nrec = 0;
		d = 1;
		lua_pushnil(L);
		while (lua_next(L, index)) {
			if (supported(L, -2) && supported(L, -1)) {
				if (nrec == 0 && lua_isnumber(L, -2)
						&& lua_tonumber(L, -2) == d) {
					narr++;
					d++;
				} else {
					nrec++;
				}
				encode(L, B, br, lua_gettop(L) - 1);
				encode(L, B, br, lua_gettop(L));
			}	
			lua_pop(L, 1);
		}
		narr = htobe32(narr);
		memcpy(&B->b[narr_pos], &narr, sizeof(narr));
		nrec = htobe32(nrec);
		memcpy(&B->b[nrec_pos], &nrec, sizeof(nrec));
		break;

	default:
		luaL_error(L, "cannot encode %s", luaL_typename(L, index));
	}
}

/*
 * Decodes the value in the specified buffer.
 */
static void decode (lua_State *L, cache_buffer *B, backref_rec *br) {
	double d;
	uint32_t nu, u, narr, nrec;

	avail(L, B, 1);
	switch (B->b[B->pos++]) {
	case LUA_TBOOLEAN:
		avail(L, B, 1);
		lua_pushboolean(L, B->b[B->pos++]);
		break;

	case LUA_TNUMBER:
		avail(L, B, sizeof(d));
		memcpy(&d, &B->b[B->pos], sizeof(d));
		B->pos += sizeof(d);
		lua_pushnumber(L, d);
		break;

	case LUA_TSTRING:
		avail(L, B, sizeof(nu));
		memcpy(&nu, &B->b[B->pos], sizeof(nu));
		B->pos += sizeof(nu);
		u = be32toh(nu);
		avail(L, B, u);
		lua_pushlstring(L, &B->b[B->pos], u);
		B->pos += u;
		break;

	case LUA_TTABLE:
		/* get number of array and record elements */
		avail(L, B, sizeof(narr) + sizeof(nrec));
		luaL_checkstack(L, 3, "decoding table");
		memcpy(&narr, &B->b[B->pos], sizeof(narr));
		B->pos += sizeof(narr);
		narr = be32toh(narr);
		memcpy(&nrec, &B->b[B->pos], sizeof(nrec));
		B->pos += sizeof(nrec);
		nrec = be32toh(nrec);

		/* sanity check */
		avail(L, B, 2 * (narr + nrec));
		lua_createtable(L, narr, nrec);

		/* store the table for backrefs */
		lua_pushvalue(L, -1);
		lua_rawseti(L, br->index, ++br->cnt);

		/* decode table content */
		for (nrec = nrec + narr; nrec > 0; nrec--) {
			decode(L, B, br);
			decode(L, B, br);
			lua_rawset(L, -3);
		}
		break;	

	case LUA_TTABLE + 64:
		/* get backref */
		avail(L, B, sizeof(nu));
		memcpy(&nu, &B->b[B->pos], sizeof(nu));
		B->pos += sizeof(nu);
		u = be32toh(nu);
		lua_rawgeti(L, br->index, (int) u);
		if (lua_isnil(L, -1)) {
			luaL_error(L, "decoding error: bad backref");
		}
		break;

	default:
		luaL_error(L, "decoding error: unknown type");
	}
}


/*
 * Configures a cache.
 */
static int configure (lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);

	/* get driver */
	lua_getfield(L, 1, CACHE_FDRIVER);
        if (lua_isnil(L, -1)) {
                luaL_error(L, "required field '%s' missing", CACHE_FDRIVER);
        }

        /* get function */
        lua_getfield(L, -1, CACHE_FCONFIGURE);
        if (!lua_isfunction(L, -1)) {
                luaL_error(L, "function '%s' is not supported",
				CACHE_FCONFIGURE);
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
        { "configure", configure },
        { NULL, NULL }
};

/*
 * Exported functions.
 */

int cache_encode (lua_State *L) {
	backref_rec br;
	cache_buffer *B;

	luaL_checkany(L, 1);

	/* prepare backref record */
	memset(&br, 0, sizeof(backref_rec));
	lua_newtable(L);
	br.index = lua_gettop(L);

	/* prepare cache buffer */
	B = (cache_buffer *) lua_newuserdata(L, sizeof(cache_buffer));
	memset(B, 0, sizeof(cache_buffer));
	luaL_getmetatable(L, CACHE_BUFFER_METATABLE);
	lua_setmetatable(L, -2);
	B->b = malloc(CACHE_BUFFER_INITSIZE);
	if (B->b == NULL) {
		luaL_error(L, "encoding error: out of memory");
	}
	B->capacity = CACHE_BUFFER_INITSIZE;

	/* encode */
	encode(L, B, &br, 1);

	return 1;
}

int cache_decode (lua_State *L) {
	cache_buffer *B;
	backref_rec br;

	/* prepare cache buffer */
	B = (cache_buffer *) luaL_checkudata(L, 1, CACHE_BUFFER_METATABLE);
	B->pos = 0;

	/* prepare backref record */
	memset(&br, 0, sizeof(backref_rec));
	lua_newtable(L);
	br.index = lua_gettop(L);

	/* deocde */
	decode(L, B, &br);

	return 1;
}

int luaopen_cache_core (lua_State *L) {
	/* register functions */
	#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, functions);
	#else
	luaL_register(L, luaL_checkstring(L, 1), functions);
	#endif
	
	/* create buffer metatable */
	luaL_newmetatable(L, CACHE_BUFFER_METATABLE);
	lua_pushcfunction(L, buffer_free);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, buffer_tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pop(L, 1);

        return 1;
}
