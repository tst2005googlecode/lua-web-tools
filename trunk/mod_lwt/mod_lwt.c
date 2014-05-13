/*
 * Provides the mod_lwt Apache module. See LICENSE for license terms.
 */

#include <setjmp.h>
#include <apr_strings.h>
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

/*
 * Handlers.
 */
#define MOD_LWT_HANDLER "lwt"
#define MOD_LWT_HANDLER_WSAPI "lwt-wsapi"

/*
 * Error output.
 */
#define MOD_LWT_ERROROUTPUT_OFF 1
#define MOD_LWT_ERROROUTPUT_ON 2

/*
 * Defaults.
 */
#define MOD_LWT_DEFAULT_MAXARGS 256
#define MOD_LWT_DEFAULT_ARGSLIMIT (1 * 1024 * 1024)
#define MOD_LWT_DEFAULT_FILELIMIT (8 * 1024 * 1024)
#define MOD_LWT_DEFAULT_MEMORYLIMIT (64 * 1024 * 1024)

/*
 * Pool keys.
 */
#define MOD_LWT_POOL_LUASTATE "mod_lwt-luastate"

/*
 * Error status.
 */
#define MOD_LWT_ERROR 0x8000
#define MOD_LWT_MASK 0x7fff

/**
 * LWT configuration.
 */
typedef struct lwt_conf_t {
	apr_pool_t *p;
	const char *dir;
	int erroroutput;	
	const char *path;
	const char *cpath;
	const char* handler;
	const char* prehook;
	const char* posthook;
	int maxargs;
	apr_off_t argslimit;
	apr_off_t filelimit;
	apr_off_t memorylimit;
} lwt_conf_t;

/**
 * LWT statistics.
 */
typedef struct lwt_stat_t {
	struct timespec realtime;
	struct timespec cputime;
	apr_pool_t *pool;
	apr_size_t alloc;
	apr_size_t limit;
} lwt_stat_t;

/*
 * Forward declaration of module.
 */
extern module lwt_module;

/*
 * Initializes an LWT configuration.
 */
static void init_conf (lwt_conf_t *conf) {
	conf->maxargs = -1;
	conf->argslimit = -1;
	conf->filelimit = -1;
	conf->memorylimit = -1;
}

/*
 * Creates an LWT server configuration.
 */
static void *create_server_conf (apr_pool_t *p, server_rec *s) {
	lwt_conf_t *conf = apr_pcalloc(p, sizeof(lwt_conf_t));
	conf->p = p;
	init_conf(conf);
	return conf;
}

/*
 * Creates an LWT directory configuration.
 */
static void *create_dir_conf (apr_pool_t *p, char *dir) {
	lwt_conf_t *conf = apr_pcalloc(p, sizeof(lwt_conf_t));
	conf->p = p;
	conf->dir = dir;
	init_conf(conf);
	return conf;
}

/**
 * Merges two LWT configuration.
 */
static void *merge_conf (apr_pool_t *p, void *base, void *add) {
	lwt_conf_t *base_conf, *add_conf, *merged_conf;

	base_conf = (lwt_conf_t *) base;
	add_conf = (lwt_conf_t *) add;
	merged_conf = (lwt_conf_t *) apr_palloc(p, sizeof(lwt_conf_t));

	merged_conf->erroroutput = add_conf->erroroutput ? add_conf->erroroutput
			: base_conf->erroroutput;
	merged_conf->path = add_conf->path ? add_conf->path : base_conf->path;
	merged_conf->cpath = add_conf->cpath ? add_conf->cpath
			: base_conf->cpath;
	merged_conf->handler = add_conf->handler ? add_conf->handler 
			: base_conf->handler;
	merged_conf->prehook = add_conf->prehook ? add_conf->prehook 
			: base_conf->prehook;
	merged_conf->posthook = add_conf->posthook ? add_conf->posthook
			: base_conf->posthook;
	merged_conf->maxargs = add_conf->maxargs >= 0 ? add_conf->maxargs
			: base_conf->maxargs;
	merged_conf->argslimit = add_conf->argslimit >= 0 ?
			add_conf->argslimit : base_conf->argslimit;
	merged_conf->filelimit = add_conf->filelimit >= 0 ?
			add_conf->filelimit : base_conf->filelimit;
	merged_conf->memorylimit = add_conf->memorylimit >= 0 ?
			add_conf->memorylimit : base_conf->memorylimit;

	return merged_conf;
}

/*
 * Roots a filepath.
 */
static apr_status_t filepath_root (lwt_conf_t *conf, const char **rootpath,
		const char *path) {
	const char **filepath = &path;
	apr_status_t status;
	status = apr_filepath_root(rootpath, filepath, APR_FILEPATH_NATIVE,
			conf->p);
	if (status == APR_SUCCESS) {
		*rootpath = path;
	} else if (status == APR_ERELATIVE && conf->dir) {
		status = apr_filepath_merge((char **) rootpath, conf->dir,
				path, APR_FILEPATH_NATIVE, conf->p);
	}
	return status;
}

/*
 * Roots a Lua path.
 */
static apr_status_t luapath_root (lwt_conf_t *conf, char **rootpath,
		const char *path) {
	char *tok, *last;
	const char *roottok;
	apr_array_header_t *toks = apr_array_make(conf->p, 0, sizeof(char *));
	int prefix = 0;
	apr_status_t status;
	if (*path == '+') {
		*((const char **) apr_array_push(toks)) = "+";
		path++;
		prefix++;
	}
	for (tok = apr_strtok(apr_pstrdup(conf->p, path), ";", &last); tok;
			tok = apr_strtok(NULL, ";", &last)) {
		if (toks->nelts - prefix > 0) {
			*((const char **) apr_array_push(toks)) = ";";
		}
		if ((status = filepath_root(conf, &roottok, tok))
				!= APR_SUCCESS) {
			return status;
		}
		*((const char **) apr_array_push(toks)) = roottok;
	}
	*rootpath = apr_array_pstrcat(conf->p, toks, 0);
	return APR_SUCCESS;
}

/*
 * Parses a limit value.
 */
static apr_status_t limit (const char *arg, apr_off_t *value) {
	apr_status_t status;
	char *end;
	if ((status = apr_strtoff(value, arg, &end, 10)) != APR_SUCCESS) {
		return status;
	}
	if (*value < 0) {
		return APR_EGENERAL;
	}
	switch (*end) {
	case 'K':
		*value *= 1024;
		end++;
		break;
	case 'M':
		*value *= 1024 * 1024;
		end++;
		break;
	case 'G':
		*value *= 1024 * 1024 * 1024;
		end++;
		break;
	}
	if (*end) {
		return APR_EGENERAL;
	}
	return APR_SUCCESS;
}

/*
 * Sets the Lua error output in an LWT configuration.
 */
static const char *set_luaerroroutput (cmd_parms *cmd, void *conf, int flag) {
	((lwt_conf_t *) conf)->erroroutput = flag ? MOD_LWT_ERROROUTPUT_ON
			: MOD_LWT_ERROROUTPUT_OFF;
	return NULL;
}

/*
 * Sets the Lua path in an LWT configuration.
 */
static const char *set_luapath (cmd_parms *cmd, void *conf, const char *arg) {
	char *value;
	if (luapath_root(conf, &value, arg) != APR_SUCCESS) {
		return "LuaPath requires a Lua path";
	}
	((lwt_conf_t *) conf)->path = value;
	return NULL;
}

/*
 * Sets the Lua C path in an LWT configuration.
 */
static const char *set_luacpath (cmd_parms *cmd, void *conf, const char *arg) {
	char *value;
	if (luapath_root(conf, &value, arg) != APR_SUCCESS) {
		return "LuaCPath requires a Lua path";
	}
	((lwt_conf_t *) conf)->cpath = value;
	return NULL;
}

/*
 * Sets the Lua handler in an LWT configuration.
 */
static const char *set_luahandler (cmd_parms *cmd, void *conf,
		const char *arg) {
	const char *value;
	if (filepath_root(conf, &value, arg) != APR_SUCCESS) {
		return "LuaHandler requires a file path";
	}
	((lwt_conf_t *) conf)->handler = value;
	return NULL;
}

/*
 * Sets the Lua pre hook in an LWT configuration.
 */
static const char *set_luaprehook (cmd_parms *cmd, void *conf,
		const char *arg) {
	const char *value;
	if (filepath_root(conf, &value, arg) != APR_SUCCESS) {
		return "LuaPreHook requires a file path";
	}
	((lwt_conf_t *) conf)->prehook = value;
	return NULL;
}

/*
 * Sets the Lua post hook in an LWT configuration.
 */
static const char *set_luaposthook (cmd_parms *cmd, void *conf,
		const char *arg) {
	const char *value;
	if (filepath_root(conf, &value, arg) != APR_SUCCESS) {
		return "LuaPostHook requires a file path";
	}
	((lwt_conf_t *) conf)->posthook = value;
	return NULL;
}	
	
/*
 * Sets the maximum number of arguments in an LWT configuration.
 */
static const char *set_luamaxargs (cmd_parms *cmd, void *conf,
		const char *arg) {
	int value;
	char *end;
	errno = 0;
	value = strtol(arg, &end, 10);
	if (errno != 0 || *end || value < 0) {
		return "LuaMaxArgs requires a non-negative integer";
	}
	((lwt_conf_t *) conf)->maxargs = value;
	return NULL;
}

/*
 * Sets the args limit in an LWT configuration.
 */
static const char *set_luaargslimit (cmd_parms *cmd, void *conf,
		const char *arg) {
	apr_off_t value;
	if (limit(arg, &value) != APR_SUCCESS) {
		return "LuaArgsLimit requires a non-negative integer";
	}
	((lwt_conf_t *) conf)->argslimit = value;
	return NULL;
}
	
/*
 * Sets the file limit in an LWT configuration.
 */
static const char *set_luafilelimit (cmd_parms *cmd, void *conf,
		const char *arg) {
	apr_off_t value;
	if (limit(arg, &value) != APR_SUCCESS) {
		return "LuaFileLimit requires a non-negative integer";
	}
	((lwt_conf_t *) conf)->filelimit = value;
	return NULL;
}

/*
 * Sets the Lua memory limit in an LWT configuration.
 */
static const char *set_luamemorylimit (cmd_parms *cmd, void *conf,
		const char *arg) {
	apr_off_t value;
	if (limit(arg, &value) != APR_SUCCESS) {
		return "LuaMemoryLimit requires a non-negative integer";
	}
	((lwt_conf_t *) conf)->memorylimit = value;
	return NULL;
}
	
/*
 * LWT configuration directives.
 */
static const command_rec commands[] = {
	AP_INIT_FLAG("LuaErrorOutput", set_luaerroroutput, NULL, OR_OPTIONS,
			"whether to render Lua errors, On or Off"),
	AP_INIT_TAKE1("LuaPath", set_luapath, NULL, OR_OPTIONS,
			"a Lua module path"),
	AP_INIT_TAKE1("LuaCPath", set_luacpath, NULL, OR_OPTIONS,
			"a Lua module path"),
	AP_INIT_TAKE1("LuaHandler", set_luahandler, NULL, OR_OPTIONS,
			"a file path"),
	AP_INIT_TAKE1("LuaPreHook", set_luaprehook, NULL, OR_OPTIONS,
			"a file path"),
	AP_INIT_TAKE1("LuaPostHook", set_luaposthook, NULL, OR_OPTIONS,
			"a file path"),
	AP_INIT_TAKE1("LuaMaxArgs", set_luamaxargs, NULL, OR_OPTIONS,
			"a non-negative integer"),
	AP_INIT_TAKE1("LuaArgsLimit", set_luaargslimit, NULL, OR_OPTIONS,
			"a non-negative integer"),
	AP_INIT_TAKE1("LuaFileLimit", set_luafilelimit, NULL, OR_OPTIONS,
			"a non-negative integer"),
	AP_INIT_TAKE1("LuaMemoryLimit", set_luamemorylimit, NULL, OR_OPTIONS,
			"a non-negative integer"),
	{ NULL }
};

/*
 * Sets time fields in statistics.
 */
static void stat_gettime (lwt_stat_t *stat) {
	clock_gettime(CLOCK_REALTIME, &stat->realtime);
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stat->cputime);
}

/*
 * Logs the request statistics.
 */
static void log_request (lwt_stat_t *stat, request_rec *r) {
	lwt_stat_t now;

	stat_gettime(&now);
	ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "Request statistics "
			"[filename=%s] [realtime=%.3f s] [cputime=%.3f s] "
			"[memory=%.3f M]", r->filename,
			(now.realtime.tv_sec +
			((double) now.realtime.tv_nsec) / 1000000000) -
			(stat->realtime.tv_sec +
			((double) stat->realtime.tv_nsec) / 1000000000),
			(now.cputime.tv_sec +
			((double) now.cputime.tv_nsec) / 1000000000) -
			(stat->cputime.tv_sec +
			((double) stat->cputime.tv_nsec) / 1000000000),
			((double) stat->alloc) / (1024 * 1024));
}

/*
 * Provides the Lua allocator function implemented in terms of APR pools.
 */
static void *lua_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
	lwt_stat_t *stat = ud;
	void *block;

	if (nsize != 0) {
		if (ptr != NULL) {
			if (nsize <= osize) {
				block = ptr;
			} else {
				if (stat->alloc + nsize <= stat->limit) {
					block = apr_palloc(stat->pool, nsize);
					memcpy(block, ptr, osize);
					stat->alloc += nsize;
				} else {
					block = NULL;
				}
			}
		} else {
			if (stat->alloc + nsize <= stat->limit) {
				block = apr_palloc(stat->pool, nsize);
				stat->alloc += nsize;
			} else {
				block = NULL;
			}
		}
	} else {
		block = NULL;
	}

	return block;
}

/* returns request statistics */
static int stat_request (lua_State *L) {
	lwt_stat_t *stat, now;

	lua_getallocf(L, (void **) &stat);
	stat_gettime(&now);
	lua_newtable(L);
	lua_pushnumber(L, (now.realtime.tv_sec + ((double) now.realtime.tv_nsec)
			/ 1000000000) - (stat->realtime.tv_sec +
			((double) stat->realtime.tv_nsec) / 1000000000));
	lua_setfield(L, -2, "realtime");
	lua_pushnumber(L, (now.cputime.tv_sec + ((double) now.cputime.tv_nsec)
			/ 1000000000) - (stat->cputime.tv_sec +
			((double) stat->cputime.tv_nsec) / 1000000000));
	lua_setfield(L, -2, "cputime");
	lua_pushnumber(L, stat->alloc);
	lua_setfield(L, -2, "memory");
	return 1;
}

/*
 * Performs cleanup processing on a Lua state.
 */
static apr_status_t lua_cleanup (void *ud) {
	lua_State *L = ud;
	lua_atpanic(L, NULL);
	lua_close(L);
	return APR_SUCCESS;
}

/*
 * Provides the Lua panic function.
 */
static __thread jmp_buf lua_panicbuf;
static int lua_panic (lua_State *L) {
	longjmp(lua_panicbuf, -1);
}

/*
 * Returns the Lua error message.
 */
static const char *lua_errormsg (lua_State *L) {
	if (lua_gettop(L) > 0) {
		if (lua_isstring(L, -1)) {
			return lua_tostring(L, -1);
		} else {
			return "(error object is not a string)";
		}
	} else {
		return "(no error object)";
	}
}

/**
 * Loads a Lua chunk.
 */
static int loadfile (request_rec *r, lwt_conf_t *conf, lua_State *L,
		const char *filename) {
	const char *errormsg;

	/* load chunk */
	switch (luaL_loadfile(L, filename)) {
	case 0:
		return OK;

	case LUA_ERRSYNTAX:
		errormsg = lua_errormsg(L);
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua syntax error loading '%s': %s",
				filename, errormsg);
		if (conf->erroroutput != MOD_LWT_ERROROUTPUT_OFF) {
			ap_rputs("<!DOCTYPE HTML>\r\n", r);
			ap_rputs("<html>\r\n", r);
			ap_rputs("<head><title>Lua Compilation Error</title>"
					"</head>\r\n", r);
			ap_rputs("<body>\r\n", r);
			ap_rputs("<h1>Lua Compilation Error</h1>\r\n", r);
			ap_rprintf(r, "<p>Error compiling '%s'.</p>\r\n",
					ap_escape_html(r->pool, filename));
			ap_rprintf(r, "<pre>%s</pre>\r\n",
					ap_escape_html(r->pool, errormsg));
			ap_rputs("</body>\r\n", r);
			ap_rputs("</html>\r\n", r);
			r->status = HTTP_INTERNAL_SERVER_ERROR;
			return OK | MOD_LWT_ERROR;
		}
		return HTTP_INTERNAL_SERVER_ERROR;

	case LUA_ERRMEM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua memory allocation error loading '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;

	#if LUA_VERSION_NUM >= 502
	case LUA_ERRGCMM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua gc metamethod error loading '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;
	#endif

	case LUA_ERRFILE:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua file read error loading '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;

	default:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Unknown Lua error loading '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;
	}
}
		
/*
 * Loads and runs a Lua chunk.
 */
static int dofile (request_rec *r, lwt_conf_t *conf, lua_State *L,
		const char *filename) {
	const char *errormsg;
	int status;

	/* push handler */
	if (conf->handler) {
		lua_pushvalue(L, 2);
	}

	/* load chunk */
	if ((status = loadfile(r, conf, L, filename)) != OK) {
		return status;
	}

	/* push request and args */
	lua_pushvalue(L, 3);
	lua_pushvalue(L, 4);

	/* run chunk */
	switch (lua_pcall(L, conf->handler ? 3 : 2, 1, 1)) {
	case 0:
		if (lua_isnil(L, -1)) {
			/* OK */
			lua_pop(L, 1);
			return OK;
		} else if (lua_isnumber(L, -1)) {
			/* handler returns HTTP status */
			status = (int) lua_tointeger(L, -1);
			if (status < 100 || status > 599) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Lua handler '%s' returns "
						"illegal status %d",
						filename, status);
				return HTTP_INTERNAL_SERVER_ERROR;
			}
			return status;
		} else {
			/* handler returns illegal type */
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
					"Lua handler '%s' returns illegal "
					"%s status", filename, luaL_typename(L,
					-1));
			return HTTP_INTERNAL_SERVER_ERROR;
		}
			
	case LUA_ERRRUN:
		errormsg = lua_errormsg(L);
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua runtime error running '%s': %s",
				filename, errormsg);
		if (conf->erroroutput != MOD_LWT_ERROROUTPUT_OFF) {
			ap_rputs("<!DOCTYPE HTML>\r\n", r);
			ap_rputs("<html>\r\n", r);
			ap_rputs("<head><title>Lua Runtime Error</title>"
					"</head>\r\n", r);
			ap_rputs("<body>\r\n", r);
			ap_rputs("<h1>Lua Runtime Error</h1>\r\n", r);
			ap_rprintf(r, "<p>Error running '%s'.</p>\r\n",
					ap_escape_html(r->pool, filename));
			ap_rprintf(r, "<pre>%s</pre>\r\n", ap_escape_html(
					r->pool, errormsg));
			ap_rputs("</body>\r\n", r);
			ap_rputs("</html>\r\n", r);
			r->status = HTTP_INTERNAL_SERVER_ERROR;
			return OK | MOD_LWT_ERROR;
		} else {
			return HTTP_INTERNAL_SERVER_ERROR;
		}
		
	case LUA_ERRMEM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua memory allocation error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;

	case LUA_ERRERR:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua error handler error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;

	#if LUA_VERSION_NUM >= 502
	case LUA_ERRGCMM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua gc metamethod error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;
	#endif

	default:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Unknown Lua error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;
	}
}

/*
 * Runs a WSAPI request.
 */
static int dowsapi (request_rec *r, lua_State *L, const char *filename) {
	apr_status_t status;

	/* load the WSAPI module */
	lua_getglobal(L, "require");
	if (!lua_isfunction(L, -1)) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Cannot load WSAPI "
				"connector; missing 'require' function");
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	lua_pushliteral(L, "httpd.wsapi");
	if (lua_pcall(L, 1, 1, 1) != 0 || !lua_istable(L, -1)) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Cannot load WSAPI "
				"connector; module loading failed");
			return HTTP_INTERNAL_SERVER_ERROR;
	}
	lua_getfield(L, -1, "run");
	if (!lua_isfunction(L, -1)) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Cannot run WSAPI "
				"connector; missing 'run' function");
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	/* set request record (including environment) in the Lua state */
	if ((status = lwt_apache_push_request_rec(L, r)) != APR_SUCCESS) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	/* invoke */
	switch (lua_pcall(L, 1, 0, 1)) {
	case 0:
		return OK;

	case LUA_ERRRUN:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
				"Lua runtime error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;

	case LUA_ERRMEM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Lua memory allocation error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;

	case LUA_ERRERR:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
				"Lua error handler error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;

	#if LUA_VERSION_NUM >= 502
	case LUA_ERRGCMM:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
				"Lua gc metamethod error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;
	#endif

	default:
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Unknown Lua error running '%s': %s",
				filename, lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;
	}
}

/**
 * Handles LWT requests.
 */
static int handler (request_rec *r) {
	lwt_stat_t *stat;
	int handler, handler_wsapi;
	lwt_conf_t *server_conf, *dir_conf, *conf;
	lua_State *L;
	apr_status_t status;
	int result;

	/* mark for statistics */
	stat = apr_pcalloc(r->pool, sizeof(lwt_stat_t));
	stat_gettime(stat);

	/* are we concerned about this request? */
	if (!r->handler) {
		return DECLINED;
	}
	handler = strcmp(r->handler, MOD_LWT_HANDLER) == 0;
	handler_wsapi = strcmp(r->handler, MOD_LWT_HANDLER_WSAPI) == 0;
	if (!handler && !handler_wsapi) {
		return DECLINED;
	}

	/* file exists? */
	if (apr_stat(&r->finfo, r->filename, APR_FINFO_SIZE, r->pool)
			!= APR_SUCCESS) {
		return HTTP_NOT_FOUND;
	}

        /* get configuration */
	server_conf = (lwt_conf_t *) ap_get_module_config(
			r->server->module_config, &lwt_module);
	dir_conf = ap_get_module_config(r->per_dir_config, &lwt_module);
	conf = merge_conf(r->pool, server_conf, dir_conf);
	if (conf->maxargs < 0) {
		conf->maxargs = MOD_LWT_DEFAULT_MAXARGS;
	}
	if (conf->argslimit < 0) {
		conf->argslimit = MOD_LWT_DEFAULT_ARGSLIMIT;
	}
	if (conf->filelimit < 0) {
		conf->filelimit = MOD_LWT_DEFAULT_FILELIMIT;
	}
	if (conf->memorylimit < 0) {
		conf->memorylimit = MOD_LWT_DEFAULT_MEMORYLIMIT;
	}

	/* set default content type */
	ap_set_content_type(r, "text/html");

	/* create Lua state */
	stat->pool = r->pool;
	stat->limit = conf->memorylimit;
	L = lua_newstate(lua_alloc, stat);
	if (L == NULL) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Cannot create Lua state");
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	apr_pool_userdata_setn((const void *) L, MOD_LWT_POOL_LUASTATE,
			lua_cleanup, r->pool);
	if (setjmp(lua_panicbuf)) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Lua panic: %s",
				lua_errormsg(L));
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	lua_atpanic(L, lua_panic);

	/* register modules */
	luaL_openlibs(L);
	#if LUA_VERSION_NUM >= 502
	luaL_requiref(L, LWT_APACHE_MODULE, luaopen_apache, 0);
	#else
	lua_pushcfunction(L, luaopen_apache);
	lua_pushstring(L, LWT_APACHE_MODULE);
	lua_call(L, 1, 1);
	#endif
	lua_pushcfunction(L, stat_request);
	lua_setfield(L, -2, "stat");
	lua_pop(L, 1);

        /* apply configuration */
	if ((status = lwt_apache_set_module_path(L, conf->path, conf->cpath, r))
			!= APR_SUCCESS) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	/* push error function for traceback */
	lua_pushcfunction(L, lwt_util_traceback);

	/* handle both regular and WSAPI requests */
	if (handler) {
		/* push handler or nil */
		if (conf->handler) {
			if ((result = loadfile(r, conf, L, conf->handler))
					!= OK) {
				return result;
			}
		} else {
			lua_pushnil(L);
		}

		/* push request record and args */
		if (lwt_apache_push_request_rec(L, r) != APR_SUCCESS
				|| lwt_apache_push_args(L, r, conf->maxargs,
				conf->argslimit, conf->filelimit)
				!= APR_SUCCESS) {
			return HTTP_BAD_REQUEST;
		}

		/* pre-hook, request, post-hook */
		if (conf->prehook && ((result = dofile(r, conf, L,
				conf->prehook)) != OK)) {
			lwt_apache_clear_deferred(L, 0);
			return result & MOD_LWT_MASK;
		}
		if (lwt_apache_is_abort(L)) {
			return OK;
		}
		if ((result = dofile(r, conf, L, r->filename)) != OK) {
			lwt_apache_clear_deferred(L, 0);
			return result & MOD_LWT_MASK;
		}
		if (lwt_apache_is_abort(L)) {
			return OK;
		}
		if (conf->posthook && ((result = dofile(r, conf, L,
				conf->posthook)) != OK)) {
			lwt_apache_clear_deferred(L, 0);
			return result & MOD_LWT_MASK;
		}
		return result;
	} else {
		result = dowsapi(r, L, r->filename);
		if (result != OK) {
			lwt_apache_clear_deferred(L, 0);
		}
		return result;
	}
}

/**
 * Runs deferred functions.
 */
static int deferred (request_rec *r) {
	lua_State *L;
	size_t count;
	int i, index;

	/* Get Lua state */
	if (apr_pool_userdata_get((void **) &L, MOD_LWT_POOL_LUASTATE, r->pool)
			!= APR_SUCCESS || !L) {
		return DECLINED;
	}
	if (setjmp(lua_panicbuf)) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Lua panic: %s",
                                lua_errormsg(L));
		return OK;
	}

	/* Get deferred functions */
	for (i = 0; i < 2; i++) {
		if (lwt_apache_push_deferred(L, i == 0) != APR_SUCCESS) {
			continue;
		}
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			continue;
		}
		#if LUA_VERSION_NUM >= 502
		count = lua_rawlen(L, -1);
		#else
		count = lua_objlen(L, -1);
		#endif

		/* Call deferred functions */
		for (index = 1; index <= count; index++) {
			lua_rawgeti(L, -1, index);
			switch (lua_pcall(L, 0, 1, 1)) {
			case 0:
				break;

			case LUA_ERRRUN:
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Lua runtime error "
						"in deferred function: %s",
						lua_errormsg(L));
				break;

			case LUA_ERRMEM:
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Lua memory allocation error "
						"in deferred function: %s",
						lua_errormsg(L));
				break;

			case LUA_ERRERR:
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Lua error handler error "
						"in deferreda function: %s",
						lua_errormsg(L));
				break;

			#if LUA_VERSION_NUM >= 502
			case LUA_ERRGCMM:
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Lua GC metamethod error "
						"in deferred function: %s",
						lua_errormsg(L));
				break;
			#endif

			default:
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Unknown Lua error "
						"in deferred function: %s",
						lua_errormsg(L));
			}
			lua_pop(L, 1);

			#if LUA_VERSION_NUM >= 502
			count = lua_rawlen(L, -1);
			#else
			count = lua_objlen(L, -1);
			#endif
		}
	}

	return OK;
}

/**
 * Logs stats.
 */
static int stat_log (request_rec *r) {
	lua_State *L;
	lwt_stat_t *stat;

	/* Get Lua state */
	if (apr_pool_userdata_get((void **) &L, MOD_LWT_POOL_LUASTATE, r->pool)
			!= APR_SUCCESS || !L) {
		return DECLINED;
	}

	/* log request */
	lua_getallocf(L, (void **) &stat);
	log_request(stat, r);

	return OK;
}
	
/**
 * Initializes the LWT module.
 */
static void init (apr_pool_t *pool) {
	lwt_apache_init(pool);
	lwt_template_init(pool);
	ap_hook_handler(handler, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_log_transaction(deferred, NULL, NULL, APR_HOOK_LAST);
	ap_hook_log_transaction(stat_log, NULL, NULL, APR_HOOK_REALLY_LAST);
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
