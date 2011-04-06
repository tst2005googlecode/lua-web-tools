/*
 * Provides the mod_lwt Apache module. See LICENSE for license terms.
 */

#include <httpd.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_config.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "util.h"
#include "template.h"
#include "apache.h"

/**
 * LWT configuration.
 */
typedef struct lwt_conf_t {
	const char *path;
	const char *cpath;
	int erroroutput;	
} lwt_conf_t;

/*
 * Error output.
 */
#define MOD_LWT_ERROROUTPUT_OFF 1
#define MOD_LWT_ERROROUTPUT_ON 2

/**
 * LWT statistics.
 */
typedef struct lwt_stat_t {
	struct timespec realtime;
	struct timespec cputime;
} lwt_stat_t;

/*
 * Forward declaration of module.
 */
extern module lwt_module;

/*
 * Creates an LWT directory configuration.
 */
static void *create_dir_conf (apr_pool_t *p, char *dir) {
	return apr_pcalloc(p, sizeof(lwt_conf_t));
}

/*
 * Creates an LWT server configuration.
 */
static void *create_server_conf (apr_pool_t *p, server_rec *s) {
	return apr_pcalloc(p, sizeof(lwt_conf_t));
}

/**
 * Merges two LWT configuration.
 */
static void *merge_conf (apr_pool_t *p, void *base, void *add) {
	lwt_conf_t *base_conf, *add_conf, *merged_conf;

	base_conf = (lwt_conf_t *) base;
	add_conf = (lwt_conf_t *) add;
	merged_conf = (lwt_conf_t *) apr_palloc(p, sizeof(lwt_conf_t));

	merged_conf->path = add_conf->path ? add_conf->path : base_conf->path;
	merged_conf->cpath = add_conf->cpath ? add_conf->cpath
			: base_conf->cpath;
	merged_conf->erroroutput = add_conf->erroroutput ? add_conf->erroroutput
			: base_conf->erroroutput;

	return merged_conf;
}

/*
 * Sets the Lua path in an LWT configuration.
 */
static const char *set_luapath (cmd_parms *cmd, void *conf, const char *arg) {
	((lwt_conf_t *) conf)->path = arg;
	return NULL;
}

/*
 * Sets the Lua C path in an LWT configuration.
 */
static const char *set_luacpath (cmd_parms *cmd, void *conf, const char *arg) {
	((lwt_conf_t *) conf)->cpath = arg;
	return NULL;
}

/*
 * Sets the Lua error output in an LWT configuration.
 */
static const char *set_luaerroroutput(cmd_parms *cmd, void *conf, int flag) {
	((lwt_conf_t *) conf)->erroroutput = flag ? MOD_LWT_ERROROUTPUT_ON
			: MOD_LWT_ERROROUTPUT_OFF;
	return NULL;
}

/*
 * LWT configuration directives.
 */
static const command_rec commands[] = {
	AP_INIT_TAKE1("LuaPath", set_luapath, NULL, OR_OPTIONS, "Lua Path"),
	AP_INIT_TAKE1("LuaCPath", set_luacpath, NULL, OR_OPTIONS, "Lua C Path"),
	AP_INIT_FLAG("LuaErrorOutput", set_luaerroroutput, NULL, OR_OPTIONS,
			"Lua Error Output"),
	{ NULL }
};

/*
 * Marks the current request state for statistics.
 */
static void mark_request (lwt_stat_t *mark, request_rec *r) {
	clock_gettime(CLOCK_REALTIME, &mark->realtime);
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &mark->cputime);
}

/*
 * Logs the request statistics.
 */
static void log_request (lwt_stat_t *mark, request_rec *r) {
	lwt_stat_t now;

	mark_request(&now, r);
	ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Request statistics "
			"[realtime=%.3f s] [cputime=%.3f s]",
			(now.realtime.tv_sec +
			((double) now.realtime.tv_nsec) / 1000000000) -
			(mark->realtime.tv_sec +
			((double) mark->realtime.tv_nsec) / 1000000000),
			(now.cputime.tv_sec +
			((double) now.cputime.tv_nsec) / 1000000000) -
			(mark->cputime.tv_sec +
			((double) mark->cputime.tv_nsec) / 1000000000));
}

/*
 * Provides the LUA allocator function implemented in terms of APR pools.
 */
static void *lua_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
	apr_pool_t *pool;
	void *block;

	pool = (apr_pool_t *) ud;
	if (nsize != 0) {
		if (osize != 0) {
			if (nsize <= osize) {
				block = ptr;
			} else {
				block = apr_palloc(pool, nsize);
				memcpy(block, ptr, osize);
			}
		} else {
			block = apr_palloc(pool, nsize);
		}
	} else {
		block = NULL;
	}

	return block;
}

/*
 * Performs cleanup processing on a Lua state.
 */
static apr_status_t lua_cleanup (void *ud) {
        lua_State *L;

        L = (lua_State *) ud;
        lua_close(L);
        return APR_SUCCESS;
}

/**
 * Handles LWT requests.
 */
static int handler (request_rec *r) {
	lwt_stat_t mark;
	lwt_conf_t *server_conf, *dir_conf, *conf;
	lua_State *L;
	const char *error_message;
	int status;

	/* mark for statistics */
	mark_request(&mark, r);

	/* are we concerned about this request? */
	if (!r->handler || strcmp(r->handler, "lwt") != 0) {
		return DECLINED;
	}

	/* file exists? */
	if (apr_stat(&r->finfo, r->filename, APR_FINFO_SIZE, r->pool)
			!= APR_SUCCESS) {
		return HTTP_NOT_FOUND;
	}

	/* set default content type */
	ap_set_content_type(r, "text/html");

	/* create Lua state */
	L = lua_newstate(lua_alloc, r->pool);
	if (L == NULL) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Cannot create Lua state");
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	apr_pool_cleanup_register(r->pool, (void *) L, lua_cleanup,
			apr_pool_cleanup_null);

	/* register modules */
	luaL_openlibs(L);
	lua_pushcfunction(L, luaopen_apache);
	lua_pushstring(L, LWT_APACHE_MODULE);
	lua_call(L, 1, 0);

        /* apply configuration */
	server_conf = (lwt_conf_t *) ap_get_module_config(
			r->server->module_config, &lwt_module);
	dir_conf = ap_get_module_config(r->per_dir_config, &lwt_module);
	conf = merge_conf(r->pool, server_conf, dir_conf);
	if ((status = lwt_apache_set_module_path(L, conf->path, conf->cpath, r))
			!= APR_SUCCESS) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	/* push error function for traceback */
	lua_pushcfunction(L, lwt_util_traceback);

	/* load chunk */
	switch (luaL_loadfile(L, r->filename)) {
	case LUA_ERRSYNTAX:
		error_message = lua_tostring(L, -1);
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua syntax error compiling '%s': %s",
				r->filename, error_message);
		if (conf->erroroutput == MOD_LWT_ERROROUTPUT_ON) {
			ap_rputs("<!DOCTYPE HTML>\r\n", r);
			ap_rputs("<html>\r\n", r);
			ap_rputs("<head><title>Lua Compilation Error</title>"
					"</head>\r\n", r);
			ap_rputs("<body>\r\n", r);
			ap_rputs("<h1>Lua Compilation Error</h1>\r\n", r);
			ap_rprintf(r, "<p>Error compiling '%s'.</p>\r\n",
					ap_escape_html(r->pool, r->filename));
			ap_rprintf(r, "<pre>%s</pre>\r\n",
					ap_escape_html(r->pool, error_message));
			ap_rputs("</body>\r\n", r);
			ap_rputs("</html>\r\n", r);
			r->status = HTTP_INTERNAL_SERVER_ERROR;
			return OK;
		} else {
			return HTTP_INTERNAL_SERVER_ERROR;
		}

	case LUA_ERRMEM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua memory allocation error compiling '%s'",
				r->filename);
		return HTTP_INTERNAL_SERVER_ERROR;

	case LUA_ERRFILE:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua file read error compiling '%s'",
				r->filename);
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	/* set request record and args in the Lua state */
	if ((status = lwt_apache_push_request_rec(L, r)) != APR_SUCCESS) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	if ((status = lwt_apache_push_args(L, r)) != APR_SUCCESS) {
		return HTTP_BAD_REQUEST;
	}

	/* run chunk */
	switch (lua_pcall(L, 2, 1, 1)) {
	case 0:
		if (lua_isnumber(L, -1)) {
			/* handler returns status */
			status = (int) lua_tonumber(L, -1);
			if (status < 100 || status > 599) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Lua handler '%s' returns "
						"illegal status %d",
						r->filename, status);
				return HTTP_INTERNAL_SERVER_ERROR;
			}
			log_request(&mark, r);
			return status;
		} else if (lua_isnil(L, -1)) {
			/* handler returns nil; assume OK */
			log_request(&mark, r);
			return OK;
		} else {
			/* handler returns illegal type */
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
					"Lua handler '%s' returns illegal "
					"%s status", r->filename,
					lua_typename(L, -1));
			log_request(&mark, r);
			return HTTP_INTERNAL_SERVER_ERROR;
		}
			
	case LUA_ERRRUN:
		error_message = lua_tostring(L, -1);
		if (error_message == NULL) {
			error_message = "(NULL)";
		}
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua runtime error running '%s': %s",
				r->filename, error_message);
		if (conf->erroroutput == MOD_LWT_ERROROUTPUT_ON) {
			ap_rputs("<!DOCTYPE HTML>\r\n", r);
			ap_rputs("<html>\r\n", r);
			ap_rputs("<head><title>Lua Runtime Error</title>"
					"</head>\r\n", r);
			ap_rputs("<body>\r\n", r);
			ap_rputs("<h1>Lua Runtime Error</h1>\r\n", r);
			ap_rprintf(r, "<p>Error running '%s'.</p>\r\n",
					ap_escape_html(r->pool, r->filename));
			ap_rprintf(r, "<pre>%s</pre>\r\n",
					ap_escape_html(r->pool, error_message));
			ap_rputs("</body>\r\n", r);
			ap_rputs("</html>\r\n", r);
			r->status = HTTP_INTERNAL_SERVER_ERROR;
			return OK;
		} else {
			return HTTP_INTERNAL_SERVER_ERROR;
		}
		
	case LUA_ERRMEM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua memory allocation error running '%s'",
				r->filename);
		return HTTP_INTERNAL_SERVER_ERROR;

	case LUA_ERRERR:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua error handler error running '%s'",
				r->filename);
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	/* should never be reached */
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
			"Unknown Lua error running '%s'", r->filename);
	return HTTP_INTERNAL_SERVER_ERROR;
}

/**
 * Initializes the LWT module.
 */
static void init (apr_pool_t *pool) {
	lwt_apache_init(pool);
	lwt_template_init(pool);
	ap_hook_handler(handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/**
 * LWT module declaration.
 */
module AP_MODULE_DECLARE_DATA lwt_module = {
	STANDARD20_MODULE_STUFF,
	create_dir_conf,
	merge_conf,
	create_server_conf,
	merge_conf,
	commands,
	init
};
