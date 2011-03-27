/**
 * Provides the LWT template functions.
 */

#ifndef LWT_TEMPLATE_INCLUDED
#define LWT_TEMPLATE_INCLUDED

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
 * @param r the request record
 * @param filename the file name
 * @param flags the flags
 * @param t is assigned the parsed template on success (unless NULL)
 * @param err is assigned the error message in case of an error (unless NULL)
 * @return APR_SUCCESS if the template is successfully rendered, and an error
 * status otherwise
 */
apr_status_t lwt_template_parse (request_rec *r, lua_State *L,
		const char *filename, const char *flags,
		apr_array_header_t **t, const char **err);

/**
 * Renders a prepared template.
 *
 * @param r the request record
 * @param L the Lua state
 * @param t the prepared template
 * @param err is assigned the error message in case of an error (unless NULL)
 * @return APR_SUCCESS if the template is successfully rendered, and an error
 * status otherwise
 */
apr_status_t lwt_template_render (request_rec *r, lua_State *L,
		apr_array_header_t *t, const char **err);

/**
 * Dumps a prepared template.
 *
 * @param r the request record
 * @param L the Lua state
 * @param t the prepared template
 * @param err is assigned the error message in case of an error (unless NULL)
 * @return APR_SUCCESS if the template is successfully dumped, and an error
 * status otherwise
 */
apr_status_t lwt_template_dump (request_rec *r, lua_State *L,
		apr_array_header_t *t, const char **err);

#endif /* LWT_TEMPLATE_INCLUDED */
