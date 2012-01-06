/**
 * Provides the mod_lwt template functions. See LICENSE for license terms.
 */

#ifndef MOD_LWT_TEMPLATE_INCLUDED
#define MOD_LWT_TEMPLATE_INCLUDED

#include <apr_tables.h>
#include <httpd.h>
#include <lua.h>

/**
 * Initializes the template processing.
 *
 * @param pool the pool
 */
void lwt_template_init (apr_pool_t *pool);

/**
 * Prepares a template for rendering.
 *
 * @param filename the file name
 * @param L the Lua state
 * @param flags the flags
 * @param pool a pool for allocations
 * @param t is assigned the parsed template on success (unless NULL)
 * @param err is assigned the error message in case of an error (unless NULL)
 * @return APR_SUCCESS if the template is successfully rendered, and an error
 * status otherwise
 */
apr_status_t lwt_template_parse (const char *filename, lua_State *L,
		const char *flags, apr_pool_t *pool, apr_array_header_t **t,
		const char **err);

/**
 * Renders a prepared template.
 *
 * @param t the prepared template
 * @param L the Lua state
 * @param pool a pool for allocations
 * @param f the output file pointer
 * @param err is assigned the error message in case of an error (unless NULL)
 * @return APR_SUCCESS if the template is successfully rendered, and an error
 * status otherwise
 */
apr_status_t lwt_template_render (apr_array_header_t *t, lua_State *L,
		apr_pool_t *pool, FILE *f, const char **err);

/**
 * Dumps a prepared template.
 *
 * @param t the prepared template
 * @param L the Lua state
 * @param f the output file pointer
 * @param err is assigned the error message in case of an error (unless NULL)
 * @return APR_SUCCESS if the template is successfully dumped, and an error
 * status otherwise
 */
apr_status_t lwt_template_dump (apr_array_header_t *t, lua_State *L, FILE *f,
		const char **err);

#endif /* MOD_LWT_TEMPLATE_INCLUDED */
