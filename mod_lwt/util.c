#include <time.h>
#include <ctype.h>
#include <apr_time.h>
#include <lauxlib.h>
#include <lualib.h>
#include "util.h"

/*
 * Returns an integer field from a Lua table.
 */
static int getintfield (lua_State *L, int index, const char *key, int d) {
	int result;
	
	lua_getfield(L, index, key);
	if (lua_isnumber(L, -1)) {
		result = lua_tointeger(L, -1);
	} else {
		if (d < 0) {
			return luaL_error(L, "field % missing in table", key);
		} else {
			result = d;
		}
	}
	lua_pop(L, 1);
  
	return result;
}

/*
 * Hexadecimal digits for URLs.
 */
static const char url_hexdigits[] = { '0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

/*
 * Exported functions.
 */

const char *lwt_util_escape_url (apr_pool_t *pool, const char *s) {
	size_t cnt, esc_cnt, ps, pn;
	char *n;
	
	/* count reserved and unsafe characters */
	cnt = 0;
	esc_cnt = 0;
	while (s[cnt] != '\0') {
		/* RFC 3986 */
		if (!isalnum(s[cnt]) && s[cnt] != '-' && s[cnt] != '.'
				&& s[cnt] != '_' && s[cnt] != '~') {
			/* reserved */
			esc_cnt++;
		}
		cnt++;
	}

	/* make escaped string */
	n = (char *) apr_palloc(pool, cnt + esc_cnt * 2 + 1);
	pn = 0;
	for (ps = 0; ps < cnt; ps++) {
		if (isalnum(s[ps]) || s[ps] == '~' || s[ps] == '.'
				|| s[ps] == '_' || s[ps] == '~') {
			n[pn++] = s[ps];
		} else {
			n[pn++] = '%';
			n[pn++] = url_hexdigits[((unsigned char) s[ps]) / 16]; 
			n[pn++] = url_hexdigits[((unsigned char) s[ps]) % 16];
		}
	}
	n[pn] = '\0';

	return n;
}

int lwt_util_traceback (lua_State *L) {
	/* get the traceback function from the debug module */
	lua_getfield(L, LUA_GLOBALSINDEX, LUA_DBLIBNAME);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return 1;
	}

	/* call */
	lua_pushvalue(L, 1);
	lua_pushinteger(L, 2);
	lua_call(L, 2, 1);

	return 1;
}

int lwt_util_time (lua_State *L) {
	time_t t;
	struct apr_time_exp_t te;
	apr_time_t tu;
	apr_status_t status;
  
	if (lua_isnoneornil(L, 1)) {
		t = time(NULL);
		lua_pushnumber(L, t);
		return 1;
	} else {
		luaL_checktype(L, 1, LUA_TTABLE);
		memset(&te, 0, sizeof(te));
		te.tm_year = getintfield(L, 1, "year", -1) - 1900;
		te.tm_mon = getintfield(L, 1, "month", -1) - 1;
		te.tm_mday = getintfield(L, 1, "day", -1);
		te.tm_hour = getintfield(L, 1, "hour", 12);
		te.tm_min = getintfield(L, 1, "min", 0);
		te.tm_sec = getintfield(L, 1, "sec", 0);
		lua_getfield(L, 1, "isdst");
		if (lua_isnil(L, 1)) {
				te.tm_isdst = -1;
		} else {
				te.tm_isdst = lua_toboolean(L, -1);
		}
		lua_pop(L, 1);
		lua_getfield(L, 1, "utc");
		if (lua_toboolean(L, -1)) {
			status = apr_time_exp_gmt_get(&tu, &te);
		} else {
			status = apr_time_exp_get(&tu, &te);
		}
		lua_pop(L, 1);
		if (status != APR_SUCCESS) {
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, tu / 1000000);
		return 1;
	}
}
