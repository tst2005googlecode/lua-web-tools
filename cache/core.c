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
 * Initial size of a buffer.
 */
#define CACHE_BUFFER_INITSIZE 4096

/*
 * Buffer metatable.
 */
#define CACHE_BUFFER_METATABLE "cache_buffer"


/*
 * Buffer record.
 */
typedef struct buf_rec {
	char *b;
	size_t pos;
	size_t capacity;
	int table_index;
	int table_count;
} buf_rec;

/*
 * Frees the memory associated with a buffer record.
 */
static int buf_free (lua_State *L) {
	buf_rec *B;

	B = (buf_rec *) luaL_checkudata(L, -1, CACHE_BUFFER_METATABLE);
	if (B->b) {
		free(B->b);
		B->b = NULL;
	}
	return 0;
}

/*
 * Ensures the requried buffer capacity.
 */
static void require (lua_State *L, buf_rec *B, size_t cnt) {
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
static void avail (lua_State *L, buf_rec *B, size_t cnt) {
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

	default:
		return 0;
	}
}

/*
 * Encodes the value at the specified index.
 */	
static void encode (lua_State *L, buf_rec *B, int index) {
	double d;
	uint32_t u, nu, narr, nrec;

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
		u = (uint32_t) lua_objlen(L, index);
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
		lua_rawget(L, B->table_index);
		if (lua_isnil(L, -1)) {
			/* index table for possible backreferences */
			lua_pop(L, 1);
			lua_pushvalue(L, index);
			lua_pushinteger(L, ++B->table_count);
			lua_rawset(L, B->table_index);
		} else {
			/* encode backreference */
			require(L, B, 1 + sizeof(nu));
			B->b[B->pos++] = (char) LUA_TTABLE + 64;
			u = (uint32_t) lua_tointeger(L, -1);
			nu = htobe32(u);
			memcpy(&B->b[B->pos], &nu, sizeof(nu));
			B->pos += sizeof(nu);
			lua_pop(L, 1);
			return;
		}

		/* analyze table */
		narr = 0;
		nrec = 0;
		d = 1;
		lua_pushnil(L);
		while (lua_next(L, index)) {
			if (supported(L, -2) && supported(L, -1)) {
				if (lua_isnumber(L, -2)
						&& lua_tonumber(L, -2) == d) {
					narr++;
					d++;
				} else {
					nrec++;
				}
			}
			lua_pop(L, 1);
		}

		/* write table */
		require(L, B, 1 + sizeof(narr) + sizeof(nrec));
		B->b[B->pos++] = (char) LUA_TTABLE;
		narr = htobe32(narr);
		memcpy(&B->b[B->pos], &narr, sizeof(narr));
		B->pos += sizeof(narr);
		nrec = htobe32(nrec);
		memcpy(&B->b[B->pos], &nrec, sizeof(nrec));
		B->pos += sizeof(nrec);
		lua_pushnil(L);
		while (lua_next(L, index)) {
			if (supported(L, -2) && supported(L, -1)) {
				encode(L, B, lua_gettop(L) - 1);
				encode(L, B, lua_gettop(L));
			}	
			lua_pop(L, 1);
		}
		break;

	default:
		luaL_error(L, "cannot encode %s", luaL_typename(L, index));
	}
}

/*
 * Decodes the value in the specified buffer.
 */
static void decode (lua_State *L, buf_rec *B) {
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
		avail(L, B, 2 * (narr * nrec));
		lua_createtable(L, narr, nrec);

		/* store the table for backreferences */
		lua_pushvalue(L, -1);
		lua_rawseti(L, B->table_index, ++B->table_count);

		/* decode table content */
		for (nrec = nrec + narr; nrec > 0; nrec--) {
			decode(L, B);
			decode(L, B);
			lua_rawset(L, -3);
		}
		break;	

	case LUA_TTABLE + 64:
		/* get backreference */
		avail(L, B, sizeof(nu));
		memcpy(&nu, &B->b[B->pos], sizeof(nu));
		B->pos += sizeof(nu);
		u = be32toh(nu);
		lua_rawgeti(L, B->table_index, (int) u);
		if (lua_isnil(L, -1)) {
			luaL_error(L, "decoding error: bad backrerference");
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
	buf_rec *B;

	luaL_checkany(L, 1);

	/* create buffer */
	B = (buf_rec *) lua_newuserdata(L, sizeof(buf_rec));
	memset(B, 0, sizeof(buf_rec));
	luaL_getmetatable(L, CACHE_BUFFER_METATABLE);
	lua_setmetatable(L, -2);
	B->b = malloc(CACHE_BUFFER_INITSIZE);
	if (!B->b) {
		luaL_error(L, "out of memory");
	}
	B->capacity = CACHE_BUFFER_INITSIZE;

	/* create backreference table */
	lua_newtable(L);
	B->table_index = lua_gettop(L);

	/* encode */
	encode(L, B, 1);

	/* push result */
	lua_pushlstring(L, B->b, B->pos);

	/* free buffer */
	free(B->b);
	B->b = NULL;

	return 1;
}

int cache_decode (lua_State *L) {
	const char *b;
	size_t s;
	buf_rec B;

	b = luaL_checkstring(L, 1);
	s = lua_objlen(L, 1);
	lua_settop(L, 1);

	/* init buffer */
	memset(&B, 0, sizeof(B));
	B.b = (char *) b;
	B.capacity = s;

	/* create backreference table */
	lua_newtable(L);
	B.table_index = lua_gettop(L);

	/* deocde */
	decode(L, &B);

	return 1;
}

int luaopen_cache_core (lua_State *L) {
        const char *modname;

        /* register functions */
        modname = luaL_checkstring(L, 1);
        luaL_register(L, modname, functions);
	
	/* create buffer metatable */
	luaL_newmetatable(L, CACHE_BUFFER_METATABLE);
	lua_pushcfunction(L, buf_free);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

        return 1;
}
