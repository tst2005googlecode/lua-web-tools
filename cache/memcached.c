/*
 * Provides the cache memcached module. See LICENSE for license terms.
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <protocol_binary.h>
#include <endian.h>
#include <lua.h>
#include <lauxlib.h>
#include "core.h"
#include "memcached.h"

#define CACHE_MEMCACHED_METATABLE "cache_memcached"

/*
 * Response parts.
 */
#define CACHE_MEMCACHED_EXTRAS 1
#define CACHE_MEMCACHED_KEY 2
#define CACHE_MEMCACHED_VALUE 4

/*
 * Cache record.
 */
typedef struct memcached_rec {
	int map_index;
	int encode_index;
	int decode_index;
	int sockets_index;
} memcached_rec;

/*
 * Returns a string field.
 */
static int get_function (lua_State *L, int index, const char *field,
		lua_CFunction dflt) {
	lua_getfield(L, index, field);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		lua_pushcfunction(L, dflt);
	}
	return luaL_ref(L, LUA_REGISTRYINDEX);
}

/*
 * Default map function.
 */
static int map (lua_State *L) {
	lua_pushliteral(L, "localhost");
	lua_pushliteral(L, "11211");
	return 2;
}

/*
 * Configures a memcached cache.
 */
static int configure (lua_State *L) {
	memcached_rec *m;

	luaL_checktype(L, 1, LUA_TTABLE);

	/* create a cache record */
	m = (memcached_rec *) lua_newuserdata(L, sizeof(memcached_rec));
	memset(m, 0, sizeof(memcached_rec));
	luaL_getmetatable(L, CACHE_MEMCACHED_METATABLE);
	lua_setmetatable(L, -2);

	/* set functions */
	m->map_index = get_function(L, 1, CACHE_FMAP, map);
	m->encode_index = get_function(L, 1, CACHE_FENCODE, cache_encode);
	m->decode_index = get_function(L, 1, CACHE_FDECODE, cache_decode);

	/* set servers table */
	lua_newtable(L);
	m->sockets_index = luaL_ref(L, LUA_REGISTRYINDEX);
	
	return 1;
}

/*
 * Lua functions.
 */
static const luaL_Reg functions[] = {
        { CACHE_FCONFIGURE, configure },
        { NULL, NULL }
};

/*
 * Returns the connected socket for the key at the specified index.
 */
static int get_socket (lua_State *L, memcached_rec *m, int index) {
	const char *host, *port;
	int fd, flag;
	struct addrinfo hints, *results, *rp;

	/* determine the server */
	lua_rawgeti(L, LUA_REGISTRYINDEX, m->map_index);
	lua_pushvalue(L, index);
	lua_call(L, 1, 2);
	if (!lua_isstring(L, -2) || !lua_isstring(L, -1)) {
		luaL_error(L, "map function returns bad host/port");
	}
	host = lua_tostring(L, -2);
	port = lua_tostring(L, -1);
	lua_pop(L, 2);

	/* find server, connecting as needed */
	lua_rawgeti(L, LUA_REGISTRYINDEX, m->sockets_index);
	lua_pushfstring(L, "%s:%s", host, port);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1)) {
		fd = (int) lua_tointeger(L, -1);
		lua_pop(L, 1);
	} else {
		/* remove the nil */
		lua_pop(L, 1);

		/* resolve */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(host, port, &hints, &results)) {
			luaL_error(L, "error resolving '%s:%s", host, port);
		}

		/* connect */
		for (rp = results; rp != NULL; rp = rp->ai_next) {
			/* create socket */
			fd = socket(rp->ai_family, rp->ai_socktype,
					rp->ai_protocol);
			if (fd == -1) {
				continue;
			}

			/* disable Nagle algorithm */
			if (rp->ai_protocol == IPPROTO_TCP) {
				flag = 1;
				if (setsockopt(fd, rp->ai_protocol, TCP_NODELAY,
						&flag, sizeof(flag)) == -1) {
					close(fd);
					continue;
				}
			}

			/* connect */
			if (connect(fd, rp->ai_addr, rp->ai_addrlen) == -1) {
				close(fd);
				continue;
			}

			/* success */
			break;
		}
		if (rp == NULL) {
			freeaddrinfo(results);
			luaL_error(L, "error connecting to '%s:%s'", host,
					port);
		}
		freeaddrinfo(results);

		/* store socket */
		lua_pushfstring(L, "%s:%s", host, port);
		lua_pushinteger(L, fd);
		lua_rawset(L, -3);
	}
	lua_pop(L, 1);

	return fd;	
}

/*
 * Reads a response from a memcached server.
 */
static int read_response (lua_State *L, int fd, uint16_t *status, int parts) {
	protocol_binary_response_no_extras response;
	size_t rtotal;
	ssize_t r;
	uint8_t extlen;
	uint16_t keylen;
	uint32_t bodylen;
	luaL_Buffer B;
	int nret = 0;

	/* receive */
	rtotal = 0;
	do {
		if ((r = read(fd, &response.bytes[rtotal],
				sizeof(response.bytes) - rtotal)) == -1) {
			luaL_error(L, "error reading response");
		}
		if (r == 0) {
			luaL_error(L, "socket closed");
		}
		rtotal += r;
	} while (rtotal < sizeof(response.bytes));
	if (response.message.header.response.magic != PROTOCOL_BINARY_RES) {
		luaL_error(L, "bad response");
	}

	/* status */
	if (status) {
		*status = be16toh(response.message.header.response.status);
	}

	/* extras */
	extlen = response.message.header.response.extlen;
	keylen = be16toh(response.message.header.response.keylen);
	bodylen = be32toh(response.message.header.response.bodylen);
	if (extlen) {
		luaL_buffinit(L, &B);
		rtotal = extlen;
		while (rtotal > 0) {
			r = rtotal;
			if (r > LUAL_BUFFERSIZE) {
				r = LUAL_BUFFERSIZE;
			}
			if ((r = read(fd, luaL_prepbuffer(&B), r)) == -1) {
				luaL_error(L, "error reading response");
			}
			luaL_addsize(&B, r);
			rtotal -= r;
		}
		luaL_pushresult(&B);
		if (parts & CACHE_MEMCACHED_EXTRAS) {
			nret++;
		} else {
			lua_pop(L, 1);
		}
	}

	/* key */
	if (keylen) {
		luaL_buffinit(L, &B);
		rtotal = keylen;
		while (rtotal > 0) {
			r = rtotal;
			if (r > LUAL_BUFFERSIZE) {
				r = LUAL_BUFFERSIZE;
			}
			if ((r = read(fd, luaL_prepbuffer(&B), r)) == -1) {
				luaL_error(L, "error reading response");
			}
			luaL_addsize(&B, r);
			rtotal -= r;
		}
		luaL_pushresult(&B);
		if (parts & CACHE_MEMCACHED_KEY) {
			nret++;
		} else {
			lua_pop(L, 1);
		}
	}

	/* value */
	if (bodylen > extlen + keylen) {
		luaL_buffinit(L, &B);
		rtotal = bodylen - (extlen + keylen);
		while (rtotal > 0) {
			r = rtotal;
			if (r > LUAL_BUFFERSIZE) {
				r = LUAL_BUFFERSIZE;
			}
			if ((r = read(fd, luaL_prepbuffer(&B), r)) == -1) {
				luaL_error(L, "error reading response");
			}
			luaL_addsize(&B, r);
			rtotal -= r;
		}
		luaL_pushresult(&B);
		if (parts & CACHE_MEMCACHED_VALUE) {
			nret++;
		} else {
			lua_pop(L, 1);
		}
	}

	return nret;
}

/*
 * Retrieves a value from a memcached server.
 */
static int get (lua_State *L) {
	memcached_rec *m;
	const char *key;
	size_t keylen;
	protocol_binary_request_get request;
	int fd, nret;
	struct iovec iov[2];
	uint16_t status;

	m = (memcached_rec *) luaL_checkudata(L, 1, CACHE_MEMCACHED_METATABLE);
	if (!m->sockets_index) {
		luaL_error(L, "memcached connector is closed");
	}
	key = luaL_checkstring(L, 2);
	keylen = lua_objlen(L, 2);

	/* prepare request */
	memset(&request, 0, sizeof(request));
	request.message.header.request.magic = PROTOCOL_BINARY_REQ;
	request.message.header.request.opcode = PROTOCOL_BINARY_CMD_GET;
	request.message.header.request.keylen = htobe16((uint16_t) keylen);
	request.message.header.request.bodylen = htobe32((uint32_t) keylen);

	/* send request */
	fd = get_socket(L, m, 2);
	iov[0].iov_base = &request;
	iov[0].iov_len = sizeof(request.bytes);
	iov[1].iov_base = (void *) key;
	iov[1].iov_len = (uint16_t) keylen;
	if (writev(fd, iov, 2) == -1) {
		luaL_error(L, "error sending request");
	}

	/* push decode function */
	lua_rawgeti(L, LUA_REGISTRYINDEX, m->decode_index);

	/* read response */
	nret = read_response(L, fd, &status, CACHE_MEMCACHED_VALUE);
	switch (status) {
	case PROTOCOL_BINARY_RESPONSE_SUCCESS:
		if (nret != 1) {
			luaL_error(L, "protocol error");
		}
		lua_call(L, 1, 1);
		return 1;

	case PROTOCOL_BINARY_RESPONSE_KEY_ENOENT:
		lua_pushnil(L);
		return 1;

	default:
		return luaL_error(L, "memcached error %d", (int) status);
	}
}

/*
 * Stores a value on a memcached server.
 */
static int set (lua_State *L) {
	memcached_rec *m;
	const char *key, *value;
	size_t keylen, valuelen;
	double expiration;
	protocol_binary_request_set srequest;
	protocol_binary_request_delete drequest;
	int fd;
	struct iovec iov[3];
	uint16_t status;

	m = (memcached_rec *) luaL_checkudata(L, 1, CACHE_MEMCACHED_METATABLE);
	if (!m->sockets_index) {
		luaL_error(L, "memcached connector is closed");
	}
	key = luaL_checkstring(L, 2);
	keylen = lua_objlen(L, 2);
	luaL_checkany(L, 3);
	expiration = luaL_optnumber(L, 4, 0);

	/* handle both set and delete */
	if (!lua_isnil(L, 3)) {
		/* encode */
		lua_rawgeti(L, LUA_REGISTRYINDEX, m->encode_index);
		lua_pushvalue(L, 3);
		lua_call(L, 1, 1);
		if (!lua_isstring(L, -1)) {
			luaL_error(L, "encode function returns bad encoding");
		}
		value = lua_tostring(L, -1);
		valuelen = lua_objlen(L, -1);

		/* prepare request */
		memset(&srequest, 0, sizeof(srequest));
		srequest.message.header.request.magic = PROTOCOL_BINARY_REQ;
		srequest.message.header.request.opcode =
				(uint8_t) lua_tonumber(L, lua_upvalueindex(1));
		srequest.message.header.request.extlen = 8;
		srequest.message.header.request.keylen =
				htobe16((uint16_t) keylen);
		srequest.message.header.request.bodylen =
				htobe32((uint32_t) (8 + keylen + valuelen));
		srequest.message.body.expiration =
				htobe32((uint32_t) expiration);

		/* send request */
		fd = get_socket(L, m, 2);
		iov[0].iov_base = &srequest;
		iov[0].iov_len = sizeof(srequest.bytes);
		iov[1].iov_base = (void *) key;
		iov[1].iov_len = (uint16_t) keylen;
		iov[2].iov_base = (void *) value;
		iov[2].iov_len = valuelen;
		if (writev(fd, iov, 3) == -1) {
			luaL_error(L, "error sending request");
		}
	} else {
		/* prepare request */
		memset(&drequest, 0, sizeof(drequest));
		drequest.message.header.request.magic = PROTOCOL_BINARY_REQ;
		drequest.message.header.request.opcode =
				PROTOCOL_BINARY_CMD_DELETE;
		drequest.message.header.request.keylen =
				htobe16((uint16_t) keylen);
		drequest.message.header.request.bodylen =
				htobe32((uint32_t) keylen);

		/* send request */
		fd = get_socket(L, m, 2);
		iov[0].iov_base = &drequest;
		iov[0].iov_len = sizeof(drequest.bytes);
		iov[1].iov_base = (void *) key;
		iov[1].iov_len = (uint16_t) keylen;
		if (writev(fd, iov, 2) == -1) {
			luaL_error(L, "error sending request");
		}
	}

	/* read response */
	read_response(L, fd, &status, 0);
	switch (status) {
	case PROTOCOL_BINARY_RESPONSE_SUCCESS:
		lua_pushboolean(L, 1);
		return 1;

	case PROTOCOL_BINARY_RESPONSE_KEY_ENOENT:
	case PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS:
		lua_pushboolean(L, 0);
		return 1;

	default:
		return luaL_error(L, "memcached error %d", (int) status);
	}
}

/*
 * Incrementa a value on a memcached server.
 */
static int increment (lua_State *L) {
	memcached_rec *m;
	const char *key;
	size_t keylen;
	double delta, initial, expiration;
	protocol_binary_request_incr request;
	int fd, nret;
	struct iovec iov[2];
	uint16_t status;
	uint64_t value;

	m = (memcached_rec *) luaL_checkudata(L, 1, CACHE_MEMCACHED_METATABLE);
	if (!m->sockets_index) {
		luaL_error(L, "memcached connector is closed");
	}
	key = luaL_checkstring(L, 2);
	keylen = lua_objlen(L, 2);
	delta = luaL_optnumber(L, 3, 1);
	initial = luaL_optnumber(L, 4, 1);
	expiration = luaL_optnumber(L, 5, 0);

	/* prepare request */
	memset(&request, 0, sizeof(request));
	request.message.header.request.magic = PROTOCOL_BINARY_REQ;
	request.message.header.request.opcode =
				(uint8_t) lua_tonumber(L, lua_upvalueindex(1));
	request.message.header.request.extlen = 20;
	request.message.header.request.keylen = htobe16((uint16_t) keylen);
	request.message.header.request.bodylen =
				htobe32((uint32_t) (20 + keylen));
	request.message.body.delta = htobe64((uint64_t) delta);
	request.message.body.initial = htobe64((uint64_t) initial);
	request.message.body.expiration = htobe32((uint32_t) expiration);

	/* send request */
	fd = get_socket(L, m, 2);
	iov[0].iov_base = &request;
	iov[0].iov_len = sizeof(request.bytes);
	iov[1].iov_base = (void *) key;
	iov[1].iov_len = (uint16_t) keylen;
	if (writev(fd, iov, 2) == -1) {
		luaL_error(L, "error sending request");
	}

	/* read response */
	nret = read_response(L, fd, &status, CACHE_MEMCACHED_VALUE);
	switch (status) {
	case PROTOCOL_BINARY_RESPONSE_SUCCESS:
		if (nret != 1) {
			luaL_error(L, "protocol error");
		}
		memcpy(&value, lua_tostring(L, -1), sizeof(value));
		lua_pushnumber(L, be64toh(value));
		return 1;

	case PROTOCOL_BINARY_RESPONSE_KEY_ENOENT:
		lua_pushnil(L);
		return 1;

	default:
		return luaL_error(L, "memcached error %d", (int) status);
	}
}

/*
 * Flushes all values on a memcached server.
 */
static int flush (lua_State *L) {
	memcached_rec *m;
	double expiration;
	protocol_binary_request_flush request;
	int fd;
	uint16_t status;

	m = (memcached_rec *) luaL_checkudata(L, 1, CACHE_MEMCACHED_METATABLE);
	if (!m->sockets_index) {
		luaL_error(L, "memcached connector is closed");
	}
	luaL_checkstring(L, 2);
	expiration = luaL_optnumber(L, 3, 0);

	/* prepare request */
	memset(&request, 0, sizeof(request));
	request.message.header.request.magic = PROTOCOL_BINARY_REQ;
	request.message.header.request.opcode = PROTOCOL_BINARY_CMD_FLUSH;
	request.message.header.request.extlen = 4;
	request.message.header.request.bodylen = htobe32((uint32_t) 4);
	request.message.body.expiration = htobe32((uint32_t) expiration);

	/* send request */
	fd = get_socket(L, m, 2);
	if (write(fd, &request, sizeof(request.bytes)) == -1) {
		luaL_error(L, "error sending request");
	}

	/* read response */
	read_response(L, fd, &status, 0);
	switch (status) {
	case PROTOCOL_BINARY_RESPONSE_SUCCESS:
		return 0;

	default:
		return luaL_error(L, "memcached error %d", (int) status);
	}
}

/*
 * Retrieves statistics from a memcached server.
 */
static int stat (lua_State *L) {
	memcached_rec *m;
	const char *key;
	size_t keylen;
	protocol_binary_request_stats request;
	int fd, nret;
	struct iovec iov[2];
	uint16_t status;

	m = (memcached_rec *) luaL_checkudata(L, 1, CACHE_MEMCACHED_METATABLE);
	if (!m->sockets_index) {
		luaL_error(L, "memcached connector is closed");
	}
	luaL_checkstring(L, 2);
	key = luaL_optstring(L, 3, NULL);
	keylen = lua_objlen(L, 3);

	/* prepare request */
	memset(&request, 0, sizeof(request));
	request.message.header.request.magic = PROTOCOL_BINARY_REQ;
	request.message.header.request.opcode = PROTOCOL_BINARY_CMD_STAT;
	request.message.header.request.keylen = htobe16((uint16_t) keylen);
	request.message.header.request.bodylen = htobe32((uint32_t) keylen);

	/* send request */
	fd = get_socket(L, m, 2);
	iov[0].iov_base = &request;
	iov[0].iov_len = sizeof(request.bytes);
	iov[1].iov_base = (void *) key;
	iov[1].iov_len = (uint16_t) keylen;
	if (writev(fd, iov, 2) == -1) {
		luaL_error(L, "error sending request");
	}

	/* read response */
	lua_newtable(L);
	while (1) {
		nret = read_response(L, fd, &status, CACHE_MEMCACHED_KEY 
				| CACHE_MEMCACHED_VALUE);
		switch (status) {
		case PROTOCOL_BINARY_RESPONSE_SUCCESS:
			switch (nret) {
			case 0:
				/* end of stats */
				return 1;
			
			case 2:
				lua_rawset(L, -3);
				break;

			default:
				luaL_error(L, "protocol error");
			}
			break;

		default:
			return luaL_error(L, "memcached error %d",
					(int) status);
		}
	}
}

/*
 * Closes a memcached connector.
 */
static int mclose (lua_State *L) {
	memcached_rec *m;

	m = luaL_checkudata(L, 1, CACHE_MEMCACHED_METATABLE);

	if (m->sockets_index) {
		/* close sockets */
		lua_rawgeti(L, LUA_REGISTRYINDEX, m->sockets_index);
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			close((int) lua_tointeger(L, -1));
			lua_pop(L, 1);
			lua_pushvalue(L, -1);
			lua_pushnil(L);
			lua_rawset(L, -4);
		}

		/* unref */
		luaL_unref(L, LUA_REGISTRYINDEX, m->sockets_index);
		m->sockets_index = 0;
	}
	if (m->decode_index) {
		luaL_unref(L, LUA_REGISTRYINDEX, m->decode_index);
		m->decode_index = 0;
	}
	if (m->encode_index) {
		luaL_unref(L, LUA_REGISTRYINDEX, m->encode_index);
		m->encode_index = 0;
	}
	if (m->map_index) {
		luaL_unref(L, LUA_REGISTRYINDEX, m->map_index);
		m->map_index = 0;
	}

	return 0;
}	

/*
 * Returns the string representation of a memcached connector.
 */
static int tostring (lua_State *L) {
	memcached_rec *m;
	int cnt;

	m = (memcached_rec *) luaL_checkudata(L, 1, CACHE_MEMCACHED_METATABLE);
	
	/* count sockets */
	if (m->sockets_index) {
		cnt = 0;
		lua_rawgeti(L, LUA_REGISTRYINDEX, m->sockets_index);
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			cnt++;
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	} else {
		cnt = -1;
	}

	/* push status */
	lua_pushfstring(L, "memcached connector [%d]", cnt);
	return 1;
}


/*
 * Exported functions.
 */

int luaopen_cache_memcached (lua_State *L) {
        const char *modname;

        /* register functions */
        modname = luaL_checkstring(L, 1);
        luaL_register(L, modname, functions);

	/* create cache metatable */
	luaL_newmetatable(L, CACHE_MEMCACHED_METATABLE);
	lua_pushcfunction(L, mclose);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, tostring);
	lua_setfield(L, -2, "__tostring");
	lua_newtable(L);
	lua_pushcfunction(L, get);
	lua_setfield(L, -2, CACHE_FGET);
	lua_pushnumber(L, PROTOCOL_BINARY_CMD_SET);
	lua_pushcclosure(L, set, 1);
	lua_setfield(L, -2, CACHE_FSET);
	lua_pushnumber(L, PROTOCOL_BINARY_CMD_ADD);
	lua_pushcclosure(L, set, 1);
	lua_setfield(L, -2, CACHE_FADD);
	lua_pushnumber(L, PROTOCOL_BINARY_CMD_REPLACE);
	lua_pushcclosure(L, set, 1);
	lua_setfield(L, -2, CACHE_FREPLACE);
	lua_pushnumber(L, PROTOCOL_BINARY_CMD_INCREMENT);
	lua_pushcclosure(L, increment, 1);
	lua_setfield(L, -2, CACHE_FINC);
	lua_pushnumber(L, PROTOCOL_BINARY_CMD_DECREMENT);
	lua_pushcclosure(L, increment, 1);
	lua_setfield(L, -2, CACHE_FDEC);
	lua_pushcfunction(L, flush);
	lua_setfield(L, -2, CACHE_FFLUSH);
	lua_pushcfunction(L, mclose);
	lua_setfield(L, -2, CACHE_FCLOSE);
	lua_pushcfunction(L, stat);
	lua_setfield(L, -2, "stat");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	return 1;
}
