/**
 * Provides the mod_lwt HTTPD module for Apache. See LICENSE for license terms.
 */

#ifndef LWT_APACHE_INCLUDED
#define LWT_APACHE_INCLUDED

#include <apr_pools.h>
#include <httpd.h>
#include <lua.h>

#define LWT_APACHE_MODULE "httpd.core"
#define LWT_APACHE_REQUEST_REC "lwt_request_rec"
#define LWT_APACHE_REQUEST_REC_METATABLE "lwt_request_rec_metatable"
#define LWT_APACHE_APR_TABLE_METATABLE "lwt_apr_table_metatable"

#define LWT_APACHE_ARGLIMIT (1024 * 1024)
#define LWT_APACHE_FILELIMIT (50 * 1024 * 1024)

/**
 * Initializes the Lua support.
 */
void lwt_apache_init (apr_pool_t *pool);

/*
 * Sets the Lua module path.
 *
 * @param L the Lua state
 * @param path the Lua path
 * @param cpath the Lua C path
 * @param r the request
 * @return a status code
 */
apr_status_t lwt_apache_set_module_path (lua_State *L, const char *path,
		const char *cpath, request_rec *r);

/**
 * Pushes the request record onto the Lua stack and also sets it in the Lua
 * registry.
 *
 * @param L the Lua state
 * @param r the request record
 * @return a status code
 */
apr_status_t lwt_apache_push_request_rec (lua_State *L, request_rec *r);

/**
 * Decodea and pushes the request arguments onto the Lua stack.
 *
 * @param L the Lua state
 * @param r the request record
 * @return a status code
 */
apr_status_t lwt_apache_push_args (lua_State *L, request_rec *r);

/**
 * Opens the Apache library in a Lua state.
 *
 * @param L the Lua state
 * @return the number results
 */
int luaopen_apache (lua_State *L);

#endif /* LWT_APACHE_INCLUDED */
