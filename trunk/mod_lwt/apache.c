/**
 * Provides the mod_lwt HTTPD module for Apache. See LICENSE for license terms.
 */

#include <time.h>
#include <apr_hash.h>
#include <apr_strings.h>
#include <apr_lib.h>
#include <httpd.h>
#include <http_core.h>
#include <http_protocol.h>
#include <http_log.h>
#include <util_script.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "util.h"
#include "template.h"
#include "apache.h"


/*
 * LWT request state.
 */
typedef struct lwt_request_rec {
	request_rec *r;
	int in_ready;
} lwt_request_rec;

/*
 * A field handler pushes a field from the request record.
 */
typedef int (*request_rec_fh) (lua_State *L, request_rec *r);

/*
 * A hash of field handlers.
 */
static apr_hash_t *request_rec_fhs;

/*
 * Provides the index metamethod for request recs.
 */
static int request_rec_index (lua_State *L) {
	lwt_request_rec *lr;
	const char *field;
	request_rec_fh fh;

	lr = (lwt_request_rec *) luaL_checkudata(L, 1,
			LWT_APACHE_REQUEST_REC_METATABLE);
	field = luaL_checkstring(L, 2);
	fh = (request_rec_fh) apr_hash_get(request_rec_fhs, field,
			strlen(field));
	if (!fh) {
		lua_pushnil(L);
		return 1;
	}
	return fh(L, lr->r);
}

/*
 * Returns the string representation of a request record.
 */
static int request_rec_tostring (lua_State *L) {
	lwt_request_rec *lr;

	lr = (lwt_request_rec *) luaL_checkudata(L, 1,
			LWT_APACHE_REQUEST_REC_METATABLE);
	lua_pushfstring(L, "request (%p)", lr->r);

	return 1;
}

static int uri_fh (lua_State *L, request_rec *r) {
	const char *begin, *end;

	/* NULL request line */
	if (r->the_request == NULL) {
		lua_pushnil(L);
		return 1;
	}
	
	/* find URI */
	begin = r->the_request; 
	while (*begin && !apr_isspace(*begin)) {
		begin++;
	}
	while (apr_isspace(*begin)) {
		begin++;
	}
	end = begin;
	while (*end && !apr_isspace(*end)) {
		end++;
	}

	/* push */
	lua_pushlstring(L, begin, end - begin);
	return 1;
}

static int protocol_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, r->protocol);
	return 1;
}

static int hostname_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, r->hostname);
	return 1;
}

static int path_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, r->uri);
	return 1;
}

static int path_info_fh (lua_State *L, request_rec *r) {
	if (r->path_info != NULL) {
		lua_pushstring(L, r->path_info);
	} else {
		lua_pushliteral(L, "");
	}
	return 1;
}

static int args_fh (lua_State *L, request_rec *r) {
	if (r->args != NULL) {
		lua_pushstring(L, r->args);
	} else {
		lua_pushstring(L, "");
	}
	return 1;
}	

static int method_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, r->method);
	return 1;
}

static int headers_in_fh (lua_State *L, request_rec *r) {
	void *userdata;
	
	userdata = lua_newuserdata(L, sizeof(apr_table_t *));
	*((apr_table_t **) userdata) = r->headers_in;
	luaL_getmetatable(L, LWT_APACHE_APR_TABLE_METATABLE);
        lua_setmetatable(L, -2);
	return 1;
}

static int headers_out_fh (lua_State *L, request_rec *r) {
	void *userdata;
	
	userdata = lua_newuserdata(L, sizeof(apr_table_t *));
	*((apr_table_t **) userdata) = r->headers_out;
	luaL_getmetatable(L, LWT_APACHE_APR_TABLE_METATABLE);
        lua_setmetatable(L, -2);
	return 1;
}

static int err_headers_out_fh (lua_State *L, request_rec *r) {
	void *userdata;
	
	userdata = lua_newuserdata(L, sizeof(apr_table_t *));
	*((apr_table_t **) userdata) = r->err_headers_out;
	luaL_getmetatable(L, LWT_APACHE_APR_TABLE_METATABLE);
        lua_setmetatable(L, -2);
	return 1;
}

static int filename_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, r->filename);
	return 1;
}

static int filedir_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, apr_pstrndup(r->pool, r->filename,
			strlen(r->filename) - strlen(apr_filepath_name_get(
			r->filename))));
	return 1;
}

static int user_fh (lua_State *L, request_rec *r) {
	if (r->user != NULL) {
		lua_pushstring(L, r->user);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int auth_type_fh (lua_State *L, request_rec *r) {
	if (r->ap_auth_type != NULL) {
		lua_pushstring(L, r->ap_auth_type);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int local_ip_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, r->connection->local_ip);
	return 1;
}

static int remote_ip_fh (lua_State *L, request_rec *r) {
	lua_pushstring(L, r->connection->remote_ip);
	return 1;
}

/*
 * Adds a request record field handler.
 */
static void add_request_rec_fh (const char *field, request_rec_fh fh) {
	apr_hash_set(request_rec_fhs, field, strlen(field), fh);
}

/*
 * Initializes the request record field handlers.
 */
static void init_request_rec_fh (apr_pool_t *pool) {
	request_rec_fhs = apr_hash_make(pool);
	add_request_rec_fh("uri", uri_fh);
	add_request_rec_fh("protocol", protocol_fh);
	add_request_rec_fh("hostname", hostname_fh);
	add_request_rec_fh("path", path_fh);
	add_request_rec_fh("path_info", path_info_fh);
	add_request_rec_fh("args", args_fh);
	add_request_rec_fh("method", method_fh);
	add_request_rec_fh("headers_in", headers_in_fh);
	add_request_rec_fh("headers_out", headers_out_fh);
	add_request_rec_fh("err_headers_out", err_headers_out_fh);
	add_request_rec_fh("filename", filename_fh);
	add_request_rec_fh("filedir", filedir_fh);
	add_request_rec_fh("user", user_fh);
	add_request_rec_fh("auth_type", auth_type_fh);
	add_request_rec_fh("local_ip", local_ip_fh);
	add_request_rec_fh("remote_ip", remote_ip_fh);
}

/*
 * Returns the LWT request record from a Lua state.
 */
static lwt_request_rec *get_lwt_request_rec (lua_State *L) {
	lwt_request_rec *lr;

	lua_getfield(L, LUA_REGISTRYINDEX, LWT_APACHE_REQUEST_REC);
	if (!lua_isuserdata(L, -1)) {
		lua_pop(L, 1);
		return NULL;
	}
	lr = (lwt_request_rec *) luaL_checkudata(L, -1,
			LWT_APACHE_REQUEST_REC_METATABLE);
	lua_pop(L, 1);
	return lr;
}

/*
 * Returns the request record from a Lua state.
 */
static request_rec *get_request_rec (lua_State *L) {
	lwt_request_rec *lr;

	lr = get_lwt_request_rec(L);
	if (!lr) {
		lua_pushliteral(L, "no request record");
		lua_error(L);
	}
	return lr->r;
}
		
/*
 * Provides the index metamethod for APR tables.
 */
static int apr_table_index (lua_State *L) {
	apr_table_t *t;
	const char *key;
	const char *value;

	t = *((apr_table_t **) luaL_checkudata(L, 1,
			LWT_APACHE_APR_TABLE_METATABLE));
	key = luaL_checkstring(L, 2);
	value = apr_table_get(t, key);
	if (value != NULL) {
		lua_pushstring(L, value);
		return 1;
	} else {
		return 0;
	}
}

/*
 * Provides the newindex metamethod for APR tables.
 */
static int apr_table_newindex (lua_State *L) {
	apr_table_t *t;
	const char *key;
	const char *value;

	t = *((apr_table_t **) luaL_checkudata(L, 1,
			LWT_APACHE_APR_TABLE_METATABLE));
	key = luaL_checkstring(L, 2);
	if (lua_isstring(L, 3)) {
		value = lua_tostring(L, 3);
		apr_table_set(t, key, value);
	} else if (lua_isnil(L, 3)) {
		apr_table_unset(t, key);
	} else {
		luaL_typerror(L, 3, "string");
	}

	return 0;
}

/*
 * Returns the string representation of an APR table.
 */
static int apr_table_tostring (lua_State *L) {
	apr_table_t *t;

	t = *((apr_table_t **) luaL_checkudata(L, 1,
			LWT_APACHE_APR_TABLE_METATABLE));
	lua_pushfstring(L, "APR table (%p)", t);

	return 1;
}

/*
 * Provides the iterator for APR tables.
 */
static int next (lua_State *L) {
	apr_table_t *t;
	lua_Integer index;
	const apr_array_header_t *a;
	const apr_table_entry_t *e;

	/* use an index stored in an upvalue to track the iteration */
	t = *((apr_table_t **) luaL_checkudata(L, 1,
			LWT_APACHE_APR_TABLE_METATABLE));
	a = apr_table_elts(t);
	index = lua_tointeger(L, lua_upvalueindex(1));
	if (index < 0 || index >= a->nelts) {
		lua_pushnil(L);
		return 1;
	}
	e = (apr_table_entry_t *) a->elts;
	lua_pushstring(L, e[index].key);
	lua_pushstring(L, e[index].val);
	index++;
	lua_pushinteger(L, index);
	lua_replace(L, lua_upvalueindex(1));
	return 2;		
}
	
/*
 * Provides the iterator for APR tables.
 */
static int pairs (lua_State *L) {
	luaL_checkudata(L, 1, LWT_APACHE_APR_TABLE_METATABLE);
	lua_pushinteger(L, 0);
	lua_pushcclosure(L, next, 1);	
	lua_pushvalue(L, 1);
	return 2;
}

/*
 * Sets the status of a request.
 */
static int set_status (lua_State *L) {
	lua_Integer status;
	request_rec *r;

	status = luaL_checkinteger(L, 1);
	if (status < 100 || status > 599) {
		luaL_error(L, "invalid status (expected [100,599], got %d)",
				status);
	}
	r = get_request_rec(L);
	r->status = status;

	return 0;
}
/*
 * Sets the content type.
 */
static int set_content_type (lua_State *L) {
	const char *content_type;
	request_rec *r;

	content_type = luaL_checkstring(L, 1);
	r = get_request_rec(L);
	ap_set_content_type(r, content_type);

	return 0;
}


/*
 * Adds a header.
 */
static int add_header (lua_State *L) {
	const char *name, *value;
	int err;
	request_rec *r;

	name = luaL_checkstring(L, 1);
	value = luaL_checkstring(L, 2);
	err = lua_toboolean(L, 3);
	r = get_request_rec(L);
	apr_table_add(err ? r->err_headers_out : r->headers_out, name, value);

	return 0;
}

/*
 * Cookie weekdays.
 */
const char *cookie_weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri",
		"Sat" };

/*
 * Cookie months.
 */
const char *cookie_months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/*
 * Sets a cookie.
 */
static int add_cookie (lua_State *L) {
	const char *name, *value, *path, *domain;
	time_t expires;
	int secure, httponly;
	request_rec *r;
	char *cookie;
	size_t size, p;
	struct tm t;

	name = luaL_checkstring(L, 1);
	value = luaL_optstring(L, 2, NULL);
	expires = luaL_optinteger(L, 3, -1);
	path = luaL_optstring(L, 4, NULL);
	domain = luaL_optstring(L, 5, NULL);
	secure = lua_toboolean(L, 6);
	httponly = lua_toboolean(L, 7);
	r = get_request_rec(L);

	/* calculate the number of bytes required */
	size = 128;
	size += strlen(name);
	if (value != NULL) {
		size += strlen(value);
	}
	if (path != NULL) {
		size += strlen(path);
	}
	if (domain != NULL) {
		size += strlen(domain);
	}

	/* build cookie */
	cookie = (char *) apr_palloc(r->pool, size);
	p = 0;
	p += sprintf(cookie + p, "%s=", name);
	if (value != NULL) {
		p += sprintf(cookie + p, "%s", value);
	}
	if (expires >= 0) {
		if (gmtime_r(&expires, &t) == NULL) {
			luaL_error(L, "error encoding time");
		}
		p += sprintf(cookie + p, "; expires=%s, %.2d-%s-%.4d "
				"%.2d:%.2d:%.2d GMT",
				cookie_weekdays[t.tm_wday], t.tm_mday,
				cookie_months[t.tm_mon], 1900 + t.tm_year,
				t.tm_hour, t.tm_min, t.tm_sec);
	}
	if (path != NULL) {
		p += sprintf(cookie + p, "; path=%s", path);
	}
	if (domain != NULL) {
		p += sprintf(cookie + p, "; domain=%s", domain);
	}
	if (secure) {
		p += sprintf(cookie + p, "; secure");
	}
	if (httponly) {
		p += sprintf(cookie + p, "; httponly");
	}

	/* set cookie */
	apr_table_addn(r->headers_out, "Set-Cookie", cookie);

	return 0;
}

/*
 * Writes a template.
 */
static int write_template (lua_State *L) {
	const char *filename, *flags;
	int return_output;
	request_rec *r;
	FILE *f;
	char *s;
	size_t len;
	apr_status_t status;
	apr_array_header_t *t;
	const char *err;

	filename = luaL_checkstring(L, 1);
	flags = luaL_optstring(L, 2, NULL);
	return_output = lua_isnoneornil(L, 3);
	r = get_request_rec(L);

	/* acquire file pointer */
	if (return_output) {
		f = open_memstream(&s, &len); 
		if (!f) {
			luaL_error(L, "Error opening memory stream");
		}
	} else {
		f = *(FILE **) luaL_checkudata(L, 3, LUA_FILEHANDLE);
	}

	/* parse and render */
	if ((status = lwt_template_parse(filename, L, flags, r->pool, &t, &err))
			!= APR_SUCCESS) {
		if (return_output) {
			fclose(f);
			free(s);
		}
		luaL_error(L, "Error parsing template: %s", err);
	}
	if ((status = lwt_template_render(t, L, r->pool, f, &err))
			!= APR_SUCCESS) {
		if (return_output) {
			fclose(f);
			free(s);
		}
		luaL_error(L, "Error rendering template: %s", err);
	}

	if (return_output) {
		fclose(f);
		lua_pushlstring(L, s, len);
		return 1;
	} else {
		return 0;
	}
}

/*
 * Escapes XML reseved characters in a string.
 */
static int escape_xml (lua_State *L) {
	const char *s;
	request_rec *r;

	s = luaL_checkstring(L, 1);
	r = get_request_rec(L);

	lua_pushstring(L, ap_escape_html(r->pool, s));

	return 1;
}

/*
 * Escapes URI reserved and unsafe characters in a string.
 */
static int escape_uri (lua_State *L) {
	const char *s;
	request_rec *r;
	
	s = luaL_checkstring(L, 1);
	r = get_request_rec(L);

	lua_pushstring(L, lwt_util_escape_uri(r->pool, s));

	return 1;
}

/*
 * LWT functions
 */
static const luaL_Reg functions[] = {
	{ "pairs", pairs },
	{ "set_status", set_status },
	{ "set_content_type", set_content_type },
	{ "add_header", add_header },
	{ "add_cookie", add_cookie },
	{ "write_template", write_template },
	{ "escape_xml", escape_xml },
	{ "escape_uri", escape_uri },
	{ NULL, NULL }
};

/*
 * Logs a debug message.
 */
static int log_apache (lua_State *L) {
	const char *s;
	request_rec *r;
	lua_Integer level;

	s = luaL_checkstring(L, 1);
	r = get_request_rec(L);
	level = lua_tointeger(L, lua_upvalueindex(1));

	ap_log_rerror(APLOG_MARK, level, 0, r, "%s", s);

	return 0;
}

/*
 * Registers log.
 */
static void register_log (lua_State *L) {
	lua_pushinteger(L, APLOG_DEBUG);
	lua_pushcclosure(L, log_apache, 1);
	lua_setfield(L, -2, "debug");
	lua_pushinteger(L, APLOG_NOTICE);
	lua_pushcclosure(L, log_apache, 1);
	lua_setfield(L, -2, "notice");
	lua_pushinteger(L, APLOG_ERR);
	lua_pushcclosure(L, log_apache, 1);
	lua_setfield(L, -2, "err");
}

/*
 * Cookie read function for Apache requests.
 */
static ssize_t cookie_read (void *cookie, char *buf, size_t size) {
	lua_State *L;
	lwt_request_rec *lr;
	long read;

	L = (lua_State *) cookie;
	lr = get_lwt_request_rec(L);
	if (!lr) {
		errno = EBADFD;
		return -1;
	}
	
	/* prepare for reading is this has not happened yet */
	if (!lr->in_ready) {
		if (ap_setup_client_block(lr->r, REQUEST_CHUNKED_DECHUNK)
				!= OK) {
			errno = EBADFD;
			return -1;
		}
		if (!ap_should_client_block(lr->r)) {
			errno = EBADFD;
			return -1;
		}
		lr->in_ready = 1;
	}

	read = ap_get_client_block(lr->r, buf, size);
	if (read == -1) {
		errno = EIO;
	}
	return read;
}

/*
 * Cookie IO functions for Apache request input.
 */
static cookie_io_functions_t in_io_functions = {
	cookie_read,
	NULL,
	NULL,
	NULL
};

/*
 * Cookie write function for Apache requests.
 */
static ssize_t cookie_write (void *cookie, const char *buf, size_t size) {
	lua_State *L;
	lwt_request_rec *lr;

	L = (lua_State *) cookie;
	lr = get_lwt_request_rec(L);
	if (!lr) {
		errno = EBADFD;
		return -1;
	}
	
	return ap_rwrite(buf, size, lr->r);
}

/*
 * Cookie IO functions for Apache request output.
 */
static cookie_io_functions_t out_io_functions = {
	NULL,
	cookie_write,
	NULL,
	NULL
};

/*
 * Closes the Lua filehandle of a cookie file.
 */
static int filehandle_close (lua_State *L) {
	FILE **fp;	
	FILE *f;

	fp = (FILE **) luaL_checkudata(L, 1, LUA_FILEHANDLE);
	f = *fp;
	if (fclose(f)) {
		int save_errno = errno;
		lua_pushnil(L);
		lua_pushfstring(L, "%s", strerror(save_errno));
		lua_pushinteger(L, save_errno);
		return 3;
	}
	*fp = NULL;
	
	lua_pushboolean(L, 1);
	return 1;
}

/*
 * Registers the request and response file handles in the Lua state.
 */
static void register_filehandles (lua_State *L) {
	FILE *f;

	/* create file environment */
	lua_newtable(L);
	lua_pushcfunction(L, filehandle_close);
	lua_setfield(L, -2, "__close");

	/* register request file */
	f = fopencookie(L, "r", in_io_functions);
	*((FILE **) lua_newuserdata(L, sizeof(FILE *))) = f;
	luaL_getmetatable(L, LUA_FILEHANDLE); 
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -2);
	lua_setfenv(L, -2);
	lua_setfield(L, -3, "input");

	/* register response file */
	f = fopencookie(L, "w", out_io_functions);
	setvbuf(f, NULL, _IONBF, 0);
	*((FILE **) lua_newuserdata(L, sizeof(FILE *))) = f;
	luaL_getmetatable(L, LUA_FILEHANDLE); 
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -2);
	lua_setfenv(L, -2);
	lua_setfield(L, -3, "output");

	/* pop file environment */
	lua_pop(L, 1);
}

/*
 * Reads the request body.
 */
static apr_status_t read_request_body (char **body, apr_size_t limit,
		request_rec *r) {
	apr_size_t bufsize, new_bufsize, pos, read;
	char *buf, *new_buf;

	if (ap_setup_client_block(r, REQUEST_CHUNKED_DECHUNK) != OK) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Error preparing to read request body");
		return APR_EGENERAL;
	}
	if (ap_should_client_block(r)) {
		bufsize = 8192;
		buf = (char *) apr_palloc(r->pool, bufsize);
		pos = 0;
		do {
			read = ap_get_client_block(r, buf + pos, bufsize - pos);
			switch (read) {
			case 0:
				buf[pos] = '\0';
				break;

			case -1:
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
						"Error reading request body");
				return APR_EGENERAL;
			
			default:
				pos += read;
				if (pos == bufsize) {
					new_bufsize = bufsize * 2;
					if (new_bufsize > LWT_APACHE_ARGLIMIT) {
						ap_log_rerror(APLOG_MARK,
								APLOG_ERR, 0, r,
								"Request body "
								"too large");
					}
					new_buf = apr_palloc(r->pool,
							new_bufsize);
					memcpy(new_buf, buf, bufsize);
					bufsize = new_bufsize;
					buf = new_buf;
				}
			}
		} while (read != 0);
	}

	*body = buf;
	return APR_SUCCESS;
}

/*
 * Deoodes URL encoded arguments.
 */
static apr_status_t decode_urlencoded (apr_table_t *args, char *urlencoded_args,
		request_rec *r) {
	char *tok, *last, *pos;
	const char *sep = "&";

	/* decode arguments */
	tok = apr_strtok(urlencoded_args, sep, &last);
	while (tok != NULL) {
		/* convert + to space (historical) */
		pos = tok;
		while (*pos) {
			if (*pos == '+') {
				*pos = ' ';
			}
			pos++;
		}	

		/* split on '=' */
		pos = strchr(tok, '=');
		if (pos != NULL) {
			*pos = '\0';
			pos++;
			ap_unescape_url(tok);
			ap_unescape_url(pos);
		} else {
			ap_unescape_url(tok);
			pos = "";
		}

		/* add in table */
		apr_table_addn(args, tok, pos);

		/* next token */
		tok = apr_strtok(NULL, sep, &last);
	}

	return APR_SUCCESS;
}

/*
 * Multipart processing state.
 */
typedef struct multipart_rec {
	request_rec *r;
	apr_file_t *F;
	size_t fsize;
	char *buf;
	size_t bpos, bmark, blimit, bcapacity;
	char *line;
	size_t lpos, llimit, lcapacity;
	char *boundary;
	size_t xpos, xlimit;
	char *value;
	size_t vpos, vcapacity;
	size_t asize;
} multipart_rec;

/*
 * Reada a chunk of data from the request.
 */
static apr_status_t multipart_read (multipart_rec *m) {
	m->blimit = ap_get_client_block(m->r, m->buf + m->bpos,
			m->bcapacity - m->bpos);
	if (m->blimit == -1) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, m->r,
				"Error reading request body");
		return APR_EGENERAL;
	}
	m->blimit += m->bpos;

	return APR_SUCCESS;
}

/*
 * Reada a line from the request.
 */
static apr_status_t multipart_readline (multipart_rec *m) {
	int cr;
	apr_status_t status;

	cr = 0;
	m->lpos = 0;
	while (m->lpos < m->lcapacity) {
		/* fill buffer */
		if (m->bpos == m->blimit) {
			m->bpos = 0;
			if ((status = multipart_read(m)) != APR_SUCCESS) {
				return status;
			}
			if (m->blimit == 0) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, m->r,
						"Unexpected end of input");
				return APR_EGENERAL;
			}
		}

		/* append */
		m->line[m->lpos] = m->buf[m->bpos];

		/* find CRLF */
		if (cr && m->line[m->lpos] == '\n') {
			m->lpos--;
			m->line[m->lpos] = '\0';
			m->llimit = m->lpos;
			m->bpos++;
			return APR_SUCCESS;
		}
		cr = m->line[m->lpos] == '\r';

		/* next */
		m->lpos++;
		m->bpos++;
	}

	/* line too long */
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, m->r, "Line too long");
	return APR_EGENERAL;
}

/*
 * Returns the name of the header.
 */
static char *multipart_headername (multipart_rec *m) {
	char *pos;

	pos = index(m->line, ':');
	if (!pos) {
		return NULL;
	}
	return apr_pstrndup(m->r->pool, m->line, pos - m->line);
}

/*
 * Returns the value of the header.
 */
static char *multipart_headervalue (multipart_rec *m) {
	char *pos, *pos2;

	/* skip header name */
	pos = index(m->line, ':');
	if (!pos) {
		return NULL;
	}
	pos++;
	while (isspace(*pos)) {
		pos++;
	}

	/* scan header value */
	pos2 = pos;
	while (*pos2 && *pos2 != ';' && *pos2 != ',') {
		pos2++;
	}
	
	return apr_pstrndup(m->r->pool, pos, pos2 - pos);
} 

/*
 * Returns a field of the header.
 */
static char *multipart_headerfield (multipart_rec *m, const char *field) {
	char *pos, *mark, *result, *rpos;
	size_t fieldsize;

	/* skip header name */
	pos = index(m->line, ':');
	if (!pos) {
		return NULL;
	}
	pos++;
	while (isspace(*pos)) {
		pos++;
	}

	/* skip header value */
	while (*pos && *pos != ';' && *pos != ',') {
		pos++;
	}
	if (!*pos) {
		return NULL;
	}
	pos++;

	/* find field */
	fieldsize = strlen(field);
	while (*pos) {
		while (isspace(*pos)) {
			pos++;
		}
		if (strncmp(field, pos, fieldsize) == 0
				&& pos[fieldsize] == '=') {
			pos += fieldsize + 1;
			if (*pos == '"') {
				/* quoted string */
				pos++;
				mark = pos;
				while (*pos) {
					if (*pos == '\\' && pos[1] != '\0') {
						pos += 2;
						continue;
					}
					if (*pos == '"') {
						break;
					}
					pos++;
				}
				result = apr_palloc(m->r->pool, pos - mark + 1);
				rpos = result;
				while (mark < pos) {
					if (*mark == '\\') {
						mark++;
						continue;
					}
					*rpos++ = *mark++;
				}
				*rpos = '\0';
				return result;
			} else {
				/* regular */
				mark = pos;
				while (*pos) {
					if (*pos == ';' || *pos == ',') {
						break;
					}
					pos++;
				}
				return apr_pstrndup(m->r->pool, mark,
						pos - mark);
			}
		} else {
			/* skip field */
			while (*pos && *pos != '=' && *pos != ';'
					&& *pos != ',') {
				pos++;
			}
			if (*pos == '=') {
				pos++;
			}
			if (*pos == '"') {
				/* quoted string */
				pos++;
				while (*pos) {
					if (*pos == '\\' && pos[1] != '\0') {
						pos += 2;
						continue;
					}
					if (*pos == '"') {
						pos++;
						break;
					}
					pos++;
				}
			}

			/* regular fields, badly quoted strings */
			while (*pos && *pos != ';' && *pos != ',') {
				pos++;
			}

			/* skip final field delimiter, if any */
			if (*pos) {
				pos++;
			}
		}
	}

	/* not found */
	return NULL;
}

/*
 * Processes the marked multipart data in the buffer.
 */
static apr_status_t multipart_process (multipart_rec *m) {
	apr_size_t cnt;
	apr_status_t status;

	cnt = m->bpos - m->bmark;
	if (m->F) {
		if (m->fsize + cnt > LWT_APACHE_FILELIMIT) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, m->r,
					"POST arguments too large");
			return APR_EGENERAL;
		}
		if ((status = apr_file_write(m->F, m->buf + m->bmark, &cnt))
				!= APR_SUCCESS) {
			return status;
		}
		m->fsize += cnt;
	} else {
		if (cnt + 1 > m->vcapacity - m->vpos ||
				m->asize + cnt > LWT_APACHE_ARGLIMIT) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, m->r,
					"POST arguments too large");
			return APR_EGENERAL;
		}
		memcpy(m->value + m->vpos, m->buf + m->bmark, cnt);
		m->vpos += cnt;
		m->value[m->vpos] = '\0';
		m->asize += cnt;
	}	

	return APR_SUCCESS;
}

/*
 * Scans multipart data until a boundary is found.
 */
static apr_status_t multipart_scan (multipart_rec *m) {
	apr_status_t status;

	/* loop until the boundary is fully matched */
	m->bmark = m->bpos;
	m->xpos = 0;
	while (1) {
		while (m->bpos < m->blimit) {
			if (m->buf[m->bpos] == m->boundary[m->xpos]) {
				m->xpos++;
				if (m->xpos == m->xlimit) {
					m->bpos++;
					break;
				}
			} else {
				m->xpos = 0;
				if (m->buf[m->bpos] == m->boundary[m->xpos]) {
					m->xpos++;
				}
			}
			m->bpos++;
		}
		if (m->xpos == 0) {
			/* no boundary match */
			if ((status = multipart_process(m)) != APR_SUCCESS) {
				return status;
			}
			m->bmark = 0;
			m->bpos = 0;
		} else if (m->xpos < m->xlimit) {
			/* partial boundary match */
			m->bpos -= m->xpos;
			if ((status = multipart_process(m)) != APR_SUCCESS) {					return status;
			}
			memmove(m->buf, m->buf + m->bpos, m->xpos);
			m->bmark = 0;
			m->bpos = m->xpos;
		} else {
			/* full boundary match */
			m->bpos -= m->xpos;
			if ((status = multipart_process(m)) != APR_SUCCESS) {
				return status;
			}
			return APR_SUCCESS;
		}
	
		/* read */
		if ((status = multipart_read(m)) != APR_SUCCESS) {
			return status;
		}
		if (m->bpos == m->blimit) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, m->r,
					"Unexpected end of request body");
			return APR_EGENERAL;
		}
	}
}

/*
 * Reads a multipart/form-data post.
 */
static apr_status_t read_multipart (apr_table_t *args, request_rec *r) {
	const char *content_type;
	multipart_rec *m;
	apr_status_t status;
	char *headername, *headervalue, *name, *filename;
	const char *tempdir;

	/* anything to process? */
	if (ap_setup_client_block(r, REQUEST_CHUNKED_DECHUNK) != OK) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
				"Error preparing to read request body");
		return APR_EGENERAL;
	}
	if (!ap_should_client_block(r)) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "No request body");
		return APR_EGENERAL;
	}

	/* prepare multipart rec */
	m = apr_pcalloc(r->pool, sizeof(multipart_rec));
	m->r = r;
	m->bcapacity = 8192;
	m->buf = apr_palloc(r->pool, m->bcapacity);
	m->lcapacity = 1024;
	m->line = apr_palloc(r->pool, m->lcapacity);
	m->vcapacity = LWT_APACHE_ARGLIMIT;
	m->value = apr_palloc(r->pool, m->vcapacity);
	
	/* get boundary */
	content_type = apr_table_get(r->headers_in, "Content-Type");
	snprintf(m->line, m->lcapacity, "Content-Type: %s", content_type);
	m->boundary = multipart_headerfield(m, "boundary");
	if (!m->boundary) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "No boundary");
		return APR_EGENERAL;
	}
	m->boundary = apr_pstrcat(r->pool, "\r\n--", m->boundary, NULL);
	m->xlimit = strlen(m->boundary);

	/* read initial boundary */
	if ((status = multipart_readline(m)) != APR_SUCCESS) {
		return status;
	}

	/* process parts */
	while (strncmp(m->line, &m->boundary[2], m->xlimit - 2) == 0) {
		/* find final boundary */
		if (strcmp(&m->line[m->xlimit - 2], "--") == 0) {
			break;
		}

		/* process headers */
		if ((status = multipart_readline(m)) != APR_SUCCESS) {
			return status;
		}	
		headervalue = NULL;
		name = NULL;
		filename = NULL;
		while (m->llimit > 0) {
			headername = multipart_headername(m);
			if (strcasecmp(headername, "Content-Disposition")
					== 0) {
				headervalue = multipart_headervalue(m);
				name = multipart_headerfield(m, "name");
				filename = multipart_headerfield(m, "filename");
			}

			/* read mext jeader */
			if ((status = multipart_readline(m)) != APR_SUCCESS) {
				return status;
			}
		}
		if (!headervalue || strcasecmp(headervalue, "form-data") != 0) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
					"No form data");
			return APR_EGENERAL;
		}
		if (!name) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "No name");
			return APR_EGENERAL;
		}

		/* increase argument size by one line capacity to prevent */
		/* a denial of service attack with many small arguments */
		if (m->asize + m->lcapacity > LWT_APACHE_ARGLIMIT) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
					"POST arguments too large");
			return APR_EGENERAL;
		}
		m->asize += m->lcapacity;

		/* setup */
		if (filename) {
			/* store in a temp file */
			if ((status = apr_temp_dir_get(&tempdir, r->pool))
					!= APR_SUCCESS) {
				return status;
			}
			if ((status = apr_filepath_merge(&filename, tempdir,
					"lwt-XXXXXX", 0, r->pool))
					!= APR_SUCCESS) {
				return status;
			}
			if ((status = apr_file_mktemp(&m->F, filename,
					APR_FOPEN_CREATE | APR_FOPEN_WRITE
					| APR_FOPEN_DELONCLOSE, r->pool))
					!= APR_SUCCESS) {
				return status;
			}
			apr_pool_cleanup_register(r->pool, m->F, (apr_status_t
				(*)(void *)) apr_file_close, NULL);
		} else {
			/* store in value buffer */
			m->vpos = 0;
		}

		/* scan and process */
		if ((status = multipart_scan(m)) != APR_SUCCESS) {
			return status;
		}

		/* finish */
		if (filename) {
			apr_table_addn(args, name, filename);
			m->F = NULL;
		} else {
			apr_table_add(args, name, m->value);
		}

		/* read next boundary */
		m->bpos += 2;
		if ((status = multipart_readline(m)) != APR_SUCCESS) {
			return status;
		}
	}

	return APR_SUCCESS;
}

/*
 * Exported functions
 */

void lwt_apache_init (apr_pool_t *pool) {
	init_request_rec_fh(pool);
}

apr_status_t lwt_apache_set_module_path (lua_State *L, const char *path,
		const char *cpath, request_rec *r) {
	if (path || cpath) {
		lua_getglobal(L, LUA_LOADLIBNAME);
		if (!lua_istable(L, -1)) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
					"Cannot set module path; missing "
					"'package' module");
			return APR_EGENERAL;
		}
		if (path) {
			lua_pushstring(L, path);
			lua_setfield(L, -2, "path");
		}
		if (cpath) {
			lua_pushstring(L, cpath);
			lua_setfield(L, -2, "cpath");
		}
		lua_pop(L, 1);
	}

	return APR_SUCCESS;
}

apr_status_t lwt_apache_push_request_rec (lua_State *L,
		request_rec *r) {
	lwt_request_rec *lr;

	lr = (lwt_request_rec *) lua_newuserdata(L, sizeof(lwt_request_rec));
	memset(lr, 0, sizeof(lwt_request_rec));
	lr->r = r;
	luaL_getmetatable(L, LWT_APACHE_REQUEST_REC_METATABLE);
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	lua_setfield(L, LUA_REGISTRYINDEX, LWT_APACHE_REQUEST_REC);

	return APR_SUCCESS;
}

apr_status_t lwt_apache_push_args (lua_State *L, request_rec *r) {
	apr_table_t *args;
	char *urlencoded_args;
	apr_status_t status;
	const char *content_type, *content_type_noparam;

	/* extract args */
	args = apr_table_make(r->pool, 4);
	if (r->args != NULL) {
		if (strlen(r->args) > LWT_APACHE_ARGLIMIT) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
					"GET arguments too large");
			return APR_EGENERAL;
		}
		urlencoded_args = apr_pstrdup(r->pool, r->args);
		if ((status = decode_urlencoded(args, urlencoded_args, r))
				!= APR_SUCCESS) {
			return status;
		}
	}
	content_type = apr_table_get(r->headers_in, "Content-type");
	if (content_type) {
		content_type_noparam = ap_field_noparam(r->pool, content_type);
		if (strcasecmp("application/x-www-form-urlencoded",
				content_type_noparam) == 0) {
			if ((status = read_request_body(&urlencoded_args,
					LWT_APACHE_ARGLIMIT, r))
					!= APR_SUCCESS) {
				return status;
			}
			get_lwt_request_rec(L)->in_ready = 1;
			if ((status = decode_urlencoded(args, urlencoded_args,
					r)) != APR_SUCCESS) {
				return status;
			}
		} else if (strcasecmp("multipart/form-data",
				content_type_noparam) == 0) {
			if ((status = read_multipart(args, r)) != APR_SUCCESS) {
				return status;
			}
			get_lwt_request_rec(L)->in_ready = 1;
		}
	}

	/* push arguments */
	*((apr_table_t **) lua_newuserdata(L, sizeof(apr_table_t *))) = args;
	luaL_getmetatable(L, LWT_APACHE_APR_TABLE_METATABLE);
	lua_setmetatable(L, -2);

	return APR_SUCCESS;
}

void lwt_apache_push_env (lua_State *L, request_rec *r) {
	/* populate environment */
	ap_add_common_vars(r);
	ap_add_cgi_vars(r);

	/* push it */
	*((apr_table_t **) lua_newuserdata(L, sizeof(apr_table_t *)))
			= r->subprocess_env;
	luaL_getmetatable(L, LWT_APACHE_APR_TABLE_METATABLE);
	lua_setmetatable(L, -2);
}

int luaopen_apache (lua_State *L) {
	/* register module, functions and file handles */
	luaL_register(L, LWT_APACHE_MODULE, functions);
	register_filehandles(L);
	register_log(L);

	/* create metatable for APR tables */
	luaL_newmetatable(L, LWT_APACHE_APR_TABLE_METATABLE);
	lua_pushcfunction(L, apr_table_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, apr_table_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_pushcfunction(L, apr_table_tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pop(L, 1);

	/* create metatables for request rec */
	luaL_newmetatable(L, LWT_APACHE_REQUEST_REC_METATABLE);
	lua_pushcfunction(L, request_rec_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, request_rec_tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pop(L, 1);

	return 1;
}
