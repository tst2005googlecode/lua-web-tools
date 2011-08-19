/*
 * Provides the mod_lwt utility functions. See LICENSE for license terms.
 */

#include <time.h>
#include <ctype.h>
#include <apr_time.h>
#include <lauxlib.h>
#include <lualib.h>
#include "util.h"

/*
 * Hexadecimal digits for URIs.
 */
static const char uri_hexdigits[] = { '0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

/*
 * Exported functions.
 */

const char *lwt_util_escape_uri (apr_pool_t *pool, const char *s) {
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
		if (isalnum(s[ps]) || s[ps] == '-' || s[ps] == '.'
				|| s[ps] == '_' || s[ps] == '~') {
			n[pn++] = s[ps];
		} else {
			n[pn++] = '%';
			n[pn++] = uri_hexdigits[((unsigned char) s[ps]) / 16]; 
			n[pn++] = uri_hexdigits[((unsigned char) s[ps]) % 16];
		}
	}
	n[pn] = '\0';

	return n;
}

const char *lwt_util_escape_js (apr_pool_t *pool, const char *s) {
	size_t cnt, esc_cnt, ps, pn;
	char *n;
	
	/* count reserved and unsafe characters */
	cnt = 0;
	esc_cnt = 0;
	while (s[cnt] != '\0') {
		switch (s[cnt]) {
			case '\b':
			case '\t':
			case '\n':
			case '\v':
			case '\f':
			case '\r':
			case '\"':
			case '\'':
			case '\\':
				esc_cnt++;
		}
		cnt++;
	}

	/* make escaped string */
	n = (char *) apr_palloc(pool, cnt + esc_cnt + 1);
	pn = 0;
	for (ps = 0; ps < cnt; ps++) {
		switch (s[ps]) {
			case '\b':
				n[pn++] = '\\';
				n[pn++] = 'b';
				break;

			case '\t':
				n[pn++] = '\\';
				n[pn++] = 't';
				break;

			case '\n':
				n[pn++] = '\\';
				n[pn++] = 'n';
				break;

			case '\v':
				n[pn++] = '\\';
				n[pn++] = 'v';
				break;

			case '\f':
				n[pn++] = '\\';
				n[pn++] = 'f';
				break;

			case '\r':
				n[pn++] = '\\';
				n[pn++] = 'r';
				break;

			case '\"':
				n[pn++] = '\\';
				n[pn++] = '\"';
				break;

			case '\'':
				n[pn++] = '\\';
				n[pn++] = '\'';
				break;

			case '\\':
				n[pn++] = '\\';
				n[pn++] = '\\';
				break;

			default:
				n[pn++] = s[ps];
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
