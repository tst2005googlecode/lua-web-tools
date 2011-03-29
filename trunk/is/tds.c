/*
 * Provides the IS TDS module. See LICENSE for license terms.
 */

#include <stdlib.h>
#include <string.h>
#ifdef _REENTRANT
#include <pthread.h>
#endif
#include <lua.h>
#include <lauxlib.h>
#include <sqlfront.h>
#include <sybdb.h>
#include <syberror.h>
#include "core.h"
#include "tds.h"

#define IS_TDS_METATABLE "is_tds"
#define IS_TDS_ERROR "is_tds_error"
#define IS_TDS_MESSAGES "is_tds_messages"
#define IS_TDS_BCAPACITY 8192

/*
 * TDS record.
 */
typedef struct tds_rec {
	DBPROCESS *db;
	int intransaction;
	int has_result;
	int numcols;
	char buffer[IS_TDS_BCAPACITY];
} tds_rec;

/*
 * Returns a string field.
 */
static const char *get_string_field (lua_State *L, int index,
		const char *field) {
	const char *result;

	lua_getfield(L, index, field);
	result = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
	lua_pop(L, 1);
	
	return result;
}

/*
 * TDS message handler.
 */
static int msg_handler (DBPROCESS *db, DBINT msgno, int msgstate, int severity,
		char *msgtext, char *srvname, char *procname, int line) {
	lua_State *L;
	int cnt = 0;

	/* get the associated Lua state */
	L = (lua_State *) dbgetuserdata(db);
	if (!L) {
		return 0;
	}

	/* push message table */
	lua_getfield(L, LUA_REGISTRYINDEX, IS_TDS_MESSAGES);

	/* build and add message */
	lua_pushfstring(L, "TDS message %d (%d): %s", msgno, msgstate, msgtext);
	cnt++;
	if (strlen(srvname) > 0) {
		lua_pushfstring(L, "\ton %s\n", srvname);
		cnt++;
	}
	if (strlen(procname) > 0) {
		lua_pushfstring(L, "\tin '%s\n'", procname);
		cnt++;
	}
	if (line > 0) {
		lua_pushfstring(L, "\tline %d\n", line);
		cnt++;
	}
	lua_concat(L, cnt);
	lua_rawseti(L, -2, lua_objlen(L, -2) + 1);

	/* pop message table */
	lua_pop(L, 1);

	return 0;
}

/*
 * Clears messages.
 */
static void clear (lua_State *L) {
	int cnt;

	lua_getfield(L, LUA_REGISTRYINDEX, IS_TDS_MESSAGES);
	cnt = lua_objlen(L, -1);
	while (cnt > 0) {
		lua_pushnil(L);
		lua_rawseti(L, -2, cnt);
		cnt--;
	}
	lua_pop(L, 1);
}
	
/*
 * TDS error handler.
 */
static int err_handler (DBPROCESS *db, int severity, int dberr, int oserr,
		char *dberrstr, char *oserrstr) {
	lua_State *L;
	int cnt = 0;

	/* get the associated Lua state */
	L = (lua_State *) dbgetuserdata(db);
	if (!L) {
		return INT_CANCEL;
	}

	/* build and set error */
	lua_pushfstring(L, "TDS error %d: %s", dberr, dberrstr);
	cnt++;
	if (oserr != 0 && oserr != DBNOERR) {
		lua_pushliteral(L, "\n");
		cnt++;
		lua_pushfstring(L, "OS error %d: %s", oserr, oserrstr);
		cnt++;
	}
	lua_concat(L, cnt);
	lua_setfield(L, LUA_REGISTRYINDEX, IS_TDS_ERROR);

	return INT_CANCEL;
}

/*
 * Raises a TDS error in Lua.
 */
static int error (lua_State *L) {
	int cnt = 0;
	int i;

	/* get message table */
	lua_getfield(L, LUA_REGISTRYINDEX, IS_TDS_MESSAGES);

	/* get error */
	lua_getfield(L, LUA_REGISTRYINDEX, IS_TDS_ERROR);
	cnt++;

	/* get messages */
	for (i = 0; i < lua_objlen(L, -2 - 2 * i); i++) {
		lua_pushliteral(L, "\n");
		cnt++;
		lua_rawgeti(L, -2 - 2 * i - 1, i + 1);
		cnt++;
	}

	/* concat */
	lua_concat(L, cnt);

	return luaL_error(L, "%s", lua_tostring(L, -1));
}

/*
 * Connects to a TDS server.
 */
static int connect (lua_State *L) {
	const char *server, *user, *password, *database, *application,
			*workstation, *charset;
	LOGINREC *login;
	tds_rec *t;

	luaL_checktype(L, 1, LUA_TTABLE);
	server = get_string_field(L, 1, "server");
	if (!server) {
		luaL_error(L, "missing table field 'server'");
	}
	user = get_string_field(L, 1, "user");
	if (!user) {
		luaL_error(L, "missing table field 'user'");
	}
	password = get_string_field(L, 1, "password");
	database = get_string_field(L, 1, "database");
	application = get_string_field(L, 1, "application");
	workstation = get_string_field(L, 1, "workstation");
	charset = get_string_field(L, 1, "charset");

	/* prepare login */
	if ((login = dblogin()) == NULL) {
		luaL_error(L, "TDS error: out of memory");
	}
	DBSETLUSER(login, user);
	if (password) {
		DBSETLPWD(login, password);
	}
	if (application) {
		DBSETLAPP(login, application);
	}
	if (workstation) {
		DBSETLHOST(login, workstation);
	}
	if (charset) {
		DBSETLCHARSET(login, charset);
	}

	/* allocate TDS record */
	t = (tds_rec *) lua_newuserdata(L, sizeof(tds_rec));
	memset(t, 0, sizeof(tds_rec));
	luaL_getmetatable(L, IS_TDS_METATABLE);
	lua_setmetatable(L, -2);

	/* connect */
	if ((t->db = dbopen(login, server)) == NULL) {
		luaL_error(L, "TDS error: connection to %s failed", server);
	}
	dbsetuserdata(t->db, (BYTE *) L);

	/* select database */
	if (database) {
		if (dbuse(t->db, database) == FAIL) {
			error(L);
		}
	}

	return 1;
}

/*
 * Closes a TDS connection.
 */
static int close (lua_State *L) {
	tds_rec *t;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	if (t->db) {
		dbclose(t->db);
		t->db = NULL;
	}

	return 0;
}

/*
 * Executes a statement.
 */
static int execute (lua_State *L) {
	tds_rec *t;
	const char *sql, *param;
	size_t bpos;
	int param_count, param_index;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	if (!t->db) {
		luaL_error(L, "database is closed");
	}
	sql = luaL_checkstring(L, 2);

	/* cancel as needed */
	if (t->has_result) {
		dbcancel(t->db);
		t->has_result = 0;
	}

	/* build SQL */
	param_count = lua_gettop(L) - 2;
	param_index = 0;
	bpos = 0;
	while (*sql) {
		if (*sql == '?') {
			/* handle substutiion */
			if (param_index == param_count) {
				luaL_error(L, "insufficient bind params");
			}
			switch (lua_type(L, param_index + 3)) {
			case LUA_TNIL:
				bpos += snprintf(&t->buffer[bpos],
						IS_TDS_BCAPACITY - bpos,
						"NULL");
				break;

			case LUA_TBOOLEAN:
				bpos += snprintf(&t->buffer[bpos],
						IS_TDS_BCAPACITY - bpos, "%d",
						lua_toboolean(L, param_index
						+ 3));
				break;

			case LUA_TNUMBER:
				bpos += snprintf(&t->buffer[bpos],
						IS_TDS_BCAPACITY - bpos, "%g",
						lua_tonumber(L, param_index
						+ 3));
				break;

			case LUA_TSTRING:
				param = lua_tostring(L, param_index + 3);
				t->buffer[bpos++] = '\'';
				while (*param) {
					if (bpos > IS_TDS_BCAPACITY - 4) {
						t->buffer[bpos] = '\0';
						if (dbcmd(t->db, t->buffer)
								== FAIL) {
							error(L);
						}
						bpos = 0;
					}
					if (*param == '\'') {
						t->buffer[bpos++] = '\'';
						t->buffer[bpos++] = '\'';
					} else {
						t->buffer[bpos++] = *param;
					}
					param++;
				}
				t->buffer[bpos++] = '\'';
				break;

			default:
				luaL_error(L, "unsuppoted %s bind param "
						"%d", luaL_typename(L,
						param_index + 3), param_index
						+ 1);
			}
			param_index++;
		} else {
			t->buffer[bpos++] = *sql;
		}

		/* append as needed */
		if (bpos > IS_TDS_BCAPACITY - 128) {
			t->buffer[bpos] = '\0';
			if (dbcmd(t->db, t->buffer) == FAIL) {
				error(L);
			}
			bpos = 0;
		}

		/* next char */
		sql++;
	}
	t->buffer[bpos] = '\0';
	if (dbcmd(t->db, t->buffer) == FAIL) {
		error(L);
	}
	if (param_index < param_count) {
		luaL_error(L, "extra bind params");
	}

	/* execute */
	if (dbsqlexec(t->db) == FAIL) {
		error(L);
	}

	/* get result */
	switch (dbresults(t->db)) {
	case SUCCEED:
		t->has_result = 1;
		t->numcols = dbnumcols(t->db);
		return 0;

	case FAIL:
		return error(L);
		break;

	case NO_MORE_RESULTS:
		/* return number of rows affected */
		lua_pushnumber(L, dbcount(t->db));
		return 1;

	default:
		return luaL_error(L, "internal error");
	}	
}

/*
 * Reads a row.
 */
static int read (lua_State *L) {
	tds_rec *t;
	is_read_mode_t read_mode;
	const char *read_mode_options[] = IS_READ_MODE_OPTIONS;
	int i;
	BYTE *data;
	double double_value;
	DBDATEREC date_value;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	if (!t->has_result) {
		luaL_error(L, "no statement to read from");
	}
	read_mode = luaL_checkoption(L, 2, read_mode_options[0],
			read_mode_options);
	
	/* read row */
	switch (dbnextrow(t->db)) {
	case REG_ROW:
		/* create row */
		switch (read_mode) {
		case IS_RNAME:
			lua_createtable(L, 0, t->numcols);
			break;

		case IS_RINDEX:
			lua_createtable(L, t->numcols, 0);
			break;
		}

		/* fill in row */
		for (i = 0; i < t->numcols; i++) {
			data = dbdata(t->db, i + 1);
			if (data == NULL) {
				/* NULL */
				continue;
			}

			/* handle types */
			switch (dbcoltype(t->db, i + 1)) {
			case SYBBIT:
				lua_pushboolean(L, *data);
				break;

			case SYBINT1:
			case SYBINT2:
			case SYBINT4:
			case SYBINT8:
			case SYBREAL:
			case SYBFLT8:
			case SYBNUMERIC:
			case SYBDECIMAL:
			case SYBMONEY4:
			case SYBMONEY:
				if (dbconvert(t->db, dbcoltype(t->db, i + 1),
						data, dbdatlen(t->db, i + 1),
						SYBFLT8, (BYTE *) &double_value,
						sizeof(double_value)) == -1) {
					error(L);
				}
				lua_pushnumber(L, double_value);
				break;

			case SYBCHAR:
			case SYBVARCHAR:
			case SYBTEXT:
			case SYBBINARY:
			case SYBVARBINARY:
			case SYBIMAGE:
				lua_pushlstring(L, (const char *) data,
						dbdatlen(t->db, i + 1));
				break;

			case SYBDATETIME4:
			case SYBDATETIME:
				dbdatecrack(t->db, &date_value, (DBDATETIME *)
						data);
				snprintf(t->buffer, IS_TDS_BCAPACITY,
						"%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
						date_value.dateyear,
						date_value.datemonth + 1,
						date_value.datedmonth,
						date_value.datehour,
						date_value.dateminute,
						date_value.datesecond);
				lua_pushlstring(L, t->buffer, 19);
				/*
				if (dbconvert(t->db, dbcoltype(t->db, i + 1),
						data, dbdatlen(t->db, i + 1),
						SYBCHAR, (BYTE *) t->buffer, -1)
						== FAIL) {
					error(L);
				}
				lua_pushstring(L, t->buffer);
				*/
				break;

			default:
				luaL_error(L, "unsupported column type %d",
						dbcoltype(t->db, i + 1));
			}

			/* store */
			switch (read_mode) {
			case IS_RNAME:
				lua_setfield(L, -2, dbcolname(t->db, i + 1));
				break;

			case IS_RINDEX:
				lua_rawseti(L, -2, i + 1);
				break;
			}
		}
		break;

	case NO_MORE_ROWS:
		switch (dbresults(t->db)) {
		case SUCCEED:
		case FAIL:
			/* cancel remaining results */
			dbcancel(t->db);
			break;

		case NO_MORE_RESULTS:
			/* regular case */
			break;
		}
		t->has_result = 0;
		lua_pushnil(L);
		break;

	case BUF_FULL:
		luaL_error(L, "TDS error: buffer full");

	case FAIL:
		error(L);

	default:
		luaL_error(L, "COMPUTE not supported");
	}
	
	/* return row or nil */
	return 1;
}

/*
 * Reads the next row.
 */
static int next (lua_State *L) {
	lua_pop(L, 1);
	lua_pushvalue(L, lua_upvalueindex(1));
	return read(L);
}
	
/*
 * Provides an iterator for rows.
 */
static int rows (lua_State *L) {
	if (lua_isstring(L, 2)) {
		lua_pushvalue(L, 2);
	} else {
		lua_pushnil(L);
	}
	lua_pushcclosure(L, next, 1);
	lua_pushvalue(L, 1);
	lua_pushnil(L);
	return 3;
}

/*
 * Retrieves metadata.
 */
static int metadata (lua_State *L) {
	tds_rec *t;
	is_metadata_mode_t metadata_mode; 
	const char *metadata_options[] = IS_METADATA_OPTIONS;
	int i;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	metadata_mode = luaL_checkoption(L, 2, metadata_options[0],
			metadata_options);
	if (!t->has_result) {
		luaL_error(L, "no statement to get metadata from");
	}

	lua_createtable(L, t->numcols, 0);
	for (i = 0; i < t->numcols; i++) {
		switch (metadata_mode) {
		case IS_MNAME:
			lua_pushstring(L, dbcolname(t->db, i + 1));
			break;

		case IS_MTYPE:
			lua_pushstring(L, dbprtype(dbcoltype(t->db, i + 1)));
			break;

		case IS_MLENGTH:
			switch (dbcoltype(t->db, i + 1)) {
			case SYBNUMERIC:
			case SYBDECIMAL:
				lua_pushnumber(L, dbcoltypeinfo(t->db, i + 1)
						->precision);
				break;

			default:
				lua_pushnumber(L, dbcollen(t->db, i + 1));
			}
			break;

		case IS_MSCALE:
			switch (dbcoltype(t->db, i + 1)) {
			case SYBNUMERIC:
			case SYBDECIMAL:
				lua_pushnumber(L, dbcoltypeinfo(t->db, i + 1)
						->scale);
				break;

			default:
				lua_pushnumber(L, 0);
			}
			break;

		case IS_MLUATYPE:
			switch (dbcoltype(t->db, i + 1)) {
			case SYBBIT:
				lua_pushliteral(L, "boolean");
				break;

			case SYBINT1:
			case SYBINT2:
			case SYBINT4:
			case SYBINT8:
			case SYBREAL:
			case SYBFLT8:
			case SYBNUMERIC:
			case SYBDECIMAL:
			case SYBMONEY4:
			case SYBMONEY:
				lua_pushliteral(L, "number");
				break;

			case SYBCHAR:
			case SYBVARCHAR:
			case SYBTEXT:
			case SYBBINARY:
			case SYBVARBINARY:
			case SYBIMAGE:
			case SYBDATETIME4:
			case SYBDATETIME:
				lua_pushliteral(L, "string");
				break;

			default:
				lua_pushliteral(L, "");
			}
			break;
		}
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

/*
 * Returns whether a transaction has been started.
 */
static int intransaction (lua_State *L) {
	const tds_rec *t;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	if (!t->db) {
		luaL_error(L, "database is closed");
	}

	lua_pushboolean(L, t->intransaction);
	return 1;
}

/*
 * Executes a statement.
 */
static void execute_internal (lua_State *L, tds_rec *t, const char *sql) {
	/* cancel pending result, if any */
	if (t->has_result) {
		dbcancel(t->db);
		t->has_result = 0;
	}

	/* set and execute SQL */
	if (dbcmd(t->db, sql) == FAIL) {
		error(L);
	}
	if (dbsqlexec(t->db) == FAIL) {
		error(L);
	}

	/* check outcome */
	switch (dbresults(t->db)) {
	case SUCCEED:
		dbcancel(t->db);
		break;

	case FAIL:
		error(L);

	case NO_MORE_RESULTS:
		/* regular case */
		break;
	}
}

/*
 * Sets the autocommit state.
 */
static int begin (lua_State *L) {
	tds_rec *t;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	if (!t->db) {
		luaL_error(L, "database is closed");
	}
	if (t->intransaction) {
		luaL_error(L, "transaction already started");
	}

	execute_internal(L, t, "BEGIN TRANSACTION");
	t->intransaction = 1;

	return 0;
}

/*
 * Commits a transaction.
 */
static int commit (lua_State *L) {
	tds_rec *t;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	if (!t->db) {
		luaL_error(L, "database is closed");
	}
	if (!t->intransaction) {
		luaL_error(L, "no transaction");
	}

	execute_internal(L, t, "COMMIT TRANSACTION");
	t->intransaction = 0;

	return 0;
}

/*
 * Rollbacks a transaction.
 */
static int rollback (lua_State *L) {
	tds_rec *t;

	clear(L);
	t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	if (!t->db) {
		luaL_error(L, "database is closed");
	}
	if (!t->intransaction) {
		luaL_error(L, "no transaction");
	}

	execute_internal(L, t, "ROLLBACK TRANSACTION");
	t->intransaction = 0;

        return 0;
}

/*
 * Returns the messages from the last operation.
 */
static int messages (lua_State *L) {
	int cnt, i;

	/* get the messages table */
	lua_getfield(L, LUA_REGISTRYINDEX, IS_TDS_MESSAGES);

	/* create a safe copy */
	cnt = lua_objlen(L, -1);
	lua_createtable(L, cnt, 0);
	for (i = 0; i < cnt; i++) {
		lua_rawgeti(L, -2, i + 1);
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

/*
 * Returns a string representation of a connection.
 */
static int tostring (lua_State *L) {
        tds_rec *t;
        const char *version;
	int pid;

        t = (tds_rec *) luaL_checkudata(L, 1, IS_TDS_METATABLE);
	version = dbversion();
	if (t->db) {
		pid = dbspid(t->db);
		lua_pushfstring(L, "TDS connection [%s] [%d]", version, pid);
	} else {
		lua_pushfstring(L, "TDS connection [%s]", version);
	}

        return 1;
}

/*
 * Module functions.
 */
static const luaL_Reg functions[] = {
	{ "connect", connect },
	{ NULL, NULL }
};

/*
 * Initialized flag.
 */
static int dbinitialized = 0;

#ifdef _REENTRANT
/*
 * Initialization mutex.
 */
static pthread_mutex_t dbinitmutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * Exported functions.
 */

int luaopen_is_tds (lua_State *L) {
	const char *modname;
	
	/* create driver */
	modname = luaL_checkstring(L ,1);
	luaL_register(L, modname, functions);

	/* create metatable */
	luaL_newmetatable(L, IS_TDS_METATABLE);
	lua_pushcfunction(L, close);
	lua_setfield(L, -2, "__gc"); 
	lua_newtable(L);
	lua_pushcfunction(L, close);
	lua_setfield(L, -2, IS_FCLOSE);
	lua_pushcfunction(L, execute);
	lua_setfield(L, -2, IS_FEXECUTE);
	lua_pushcfunction(L, read);
	lua_setfield(L, -2, IS_FREAD);
	lua_pushcfunction(L, metadata);
	lua_setfield(L, -2, IS_FMETADATA);
	lua_pushcfunction(L, rows);
	lua_setfield(L, -2, IS_FROWS);
	lua_pushcfunction(L, intransaction);
	lua_setfield(L, -2, IS_FINTRANSACTION);
	lua_pushcfunction(L, begin);
	lua_setfield(L, -2, IS_FBEGIN);
	lua_pushcfunction(L, commit);
	lua_setfield(L, -2, IS_FCOMMIT);
	lua_pushcfunction(L, rollback);
	lua_setfield(L, -2, IS_FROLLBACK);
	lua_pushcfunction(L, messages);
	lua_setfield(L, -2, "messages");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pop(L, 1);

	/* initialize error */
	lua_pushliteral(L, "TDS error: no error");
	lua_setfield(L, LUA_REGISTRYINDEX, IS_TDS_ERROR);

	/* create messages table */
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, IS_TDS_MESSAGES);

	/* initialize DB library */
	#ifdef _REENTRANT
        if (pthread_mutex_lock(&dbinitmutex) != 0) {
                luaL_error(L, "error acquiring init mutex");
        }
	#endif
	if (!dbinitialized) {
		if (dbinit() == FAIL) {
			luaL_error(L, "error initializing DB library");
		}
		dberrhandle(err_handler);
		dbmsghandle(msg_handler);
		dbinitialized = 1;
	}
	#ifdef _REENTRANT
        if (pthread_mutex_unlock(&dbinitmutex) != 0) {
                luaL_error(L, "error releasing init mutex");
        }
	#endif

	return 1;
}
