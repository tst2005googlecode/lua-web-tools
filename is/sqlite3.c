/*
 * Provides the IS SQLite3 module. See LICENSE for license terms.
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sqlite3.h>
#include <lua.h>
#include <lauxlib.h>
#include "core.h"
#include "sqlite3.h"

#define IS_SQLITE3_METATABLE "is_sqlite3"

/*
 * SQLite3 record.
 */
typedef struct sqlite3_rec {
	sqlite3 *db;
	sqlite3_stmt *stmt;
	int row;
	int column_count;
} sqlite3_rec;

/*
 * Returns a string field.
 */
static const char *get_string_field (lua_State *L, int index, const char *field,
		const char *dflt) {
	const char *result;

	lua_getfield(L, index, field);
	result = lua_isstring(L, -1) ? lua_tostring(L, -1) : dflt;
	lua_pop(L, 1);
	
	return result;
}

/*
 * Returns a boolean field.
 */
static int get_boolean_field (lua_State *L, int index, const char *field) {
	int result;

	lua_getfield(L, index, field);
	result = lua_toboolean(L, -1);
	lua_pop(L, 1);

	return result;
}

/*
 * Raises a SQLite3 error in Lua.
 */
static int error (lua_State *L, sqlite3_rec *s) {
	return luaL_error(L, "SQLite3 error %d: %s", sqlite3_errcode(s->db),
			sqlite3_errmsg(s->db));
}

/*
 * Connects to SQLite3.
 */
static int connect (lua_State *L) {
	sqlite3_rec *s;
	const char *filename, *vfs;
	int flags;

	luaL_checktype(L, 1, LUA_TTABLE);
	filename = get_string_field(L, 1, "filename", NULL);
	if (!filename) {
		lua_pushliteral(L, "missing table field 'filename'");
		lua_error(L);
	}
	flags = 0;
	if (!get_boolean_field(L, 1, "readonly")) {
		flags |= SQLITE_OPEN_READWRITE;
	} else {
		flags |= SQLITE_OPEN_READONLY;
	}
	if (!get_boolean_field(L, 1, "nocreate")) {
		flags |= SQLITE_OPEN_CREATE;
	}
	vfs = get_string_field(L, 1, "vfs", NULL);

	s = (sqlite3_rec *) lua_newuserdata(L, sizeof(sqlite3_rec));
	memset(s, 0, sizeof(sqlite3_rec));
	if (sqlite3_open_v2(filename, &s->db, flags, vfs) != SQLITE_OK) {
		if (s->db) {
			lua_pushfstring(L, "SQLite3 error %d: %s",
					sqlite3_errcode(s->db),
					sqlite3_errmsg(s->db));
			sqlite3_close(s->db);
		} else {
			lua_pushliteral(L, "SQLite3 error: out of memory");
		}
		lua_error(L);
	}
		
	luaL_getmetatable(L, IS_SQLITE3_METATABLE);
	lua_setmetatable(L, -2);

	return 1;
}

/*
 * Closes a SQLite3 database.
 */
static int close (lua_State *L) {
	sqlite3_rec *s;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	if (s->stmt) {
		sqlite3_finalize(s->stmt);
		s->stmt = NULL;
	}
	if (s->db) {
		sqlite3_close(s->db);
		s->db = NULL;
	}

	return 0;
}

/*
 * Executes a statement.
 */
static int execute (lua_State *L) {
	sqlite3_rec *s;
	const char *sql;
	int sql_length, param_count;
	int i;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	if (!s->db) {
		lua_pushliteral(L, "database is closed");
		lua_error(L);
	}
	sql = luaL_checkstring(L, 2);
	sql_length = lua_objlen(L, 2);

	/* finalize last statement */	
	if (s->stmt) {
		sqlite3_finalize(s->stmt);
		s->stmt = NULL;
	}

	/* prepare */
	if (sqlite3_prepare_v2(s->db, sql, sql_length + 1, &s->stmt, NULL)
			!= SQLITE_OK) {
		error(L, s);
	}
		
	/* bind params */
	param_count = sqlite3_bind_parameter_count(s->stmt);
	if (lua_gettop(L) - 2 != param_count) {
		luaL_error(L, "expected %d bind params, got %d", param_count,
				lua_gettop(L) - 2);
	}
	for (i = 0; i < param_count; i++) {
		switch (lua_type(L, i + 3)) {
		case LUA_TNIL:
			if (sqlite3_bind_null(s->stmt, i + 1) != SQLITE_OK) {
				error(L, s);
			}
			break;

		case LUA_TBOOLEAN:
			if (sqlite3_bind_int(s->stmt, i + 1, lua_toboolean(
					L, i + 3)) != SQLITE_OK) {
				error(L, s);
			}
			break;

		case LUA_TNUMBER:
			if (sqlite3_bind_double(s->stmt, i + 1, lua_tonumber(
					L, i + 3)) != SQLITE_OK) {
				error(L, s);
			}
			break;

		case LUA_TSTRING:
			if (sqlite3_bind_text(s->stmt, i + 1, lua_tostring(
					L, i + 3), lua_objlen(L, i + 3),
					SQLITE_TRANSIENT) != SQLITE_OK) {
				error(L, s);
			}
			break;

		default:
			luaL_error(L, "bind param %d is unsupported %s",
				i + 1, luaL_typename(L, i + 3));
		}
	}

	/* step */
	switch (sqlite3_step(s->stmt)) {
	case SQLITE_ROW:
		/* select statement */
		s->row = 1;
		s->column_count = sqlite3_column_count(s->stmt);
		lua_pushnil(L);
		return 1;

	case SQLITE_DONE:
		s->column_count = sqlite3_column_count(s->stmt);
		if (s->column_count == 0) {
			/* return number of rows changed */
			lua_pushnumber(L, sqlite3_changes(s->db));
			sqlite3_finalize(s->stmt);
			s->stmt = NULL;
			return 1;
		} else {
			/* empty select */
			s->row = 0;
			lua_pushnil(L);
			return 1;
		}

	default:
		return error(L, s);
	}
}

/*
 * Reads a row.
 */
static int read (lua_State *L) {
	sqlite3_rec *s;
	is_read_mode_t read_mode;
	const char *read_mode_options[] = IS_READ_MODE_OPTIONS;
	int i;
	const char *column_name;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	read_mode = luaL_checkoption(L, 2, read_mode_options[0],
			read_mode_options);
	if (!s->stmt) {
		lua_pushliteral(L, "no statement to read from");
		lua_error(L);
	}

	/* no more roww */
	if (!s->row) {
		sqlite3_finalize(s->stmt);
		s->stmt = NULL;
		lua_pushnil(L);
		return 1;
	}
	
	/* return row */
	switch (read_mode) {
	case IS_RNAME:
		lua_createtable(L, 0, s->column_count);
		break;

	case IS_RINDEX:
		lua_createtable(L, s->column_count, 0);
		break;
	}
	for (i = 0; i < s->column_count; i++) {
		/* handle types */
		switch (sqlite3_column_type(s->stmt, i)) {
		case SQLITE_INTEGER:
		case SQLITE_FLOAT:
			lua_pushnumber(L, sqlite3_column_double(s->stmt, i));
			break;

		case SQLITE_BLOB:
			lua_pushlstring(L, (const char *) sqlite3_column_blob(
					s->stmt, i), sqlite3_column_bytes(
					s->stmt, i));
			break;

		case SQLITE_TEXT:
			lua_pushlstring(L, (const char *) sqlite3_column_text(
					s->stmt, i), sqlite3_column_bytes(
					s->stmt, i));
			break;

		case SQLITE_NULL:
			lua_pushnil(L);
			break;

		default:
			lua_pushliteral(L, "unsupported column type");
			lua_error(L);
		}

		/* store */
		switch (read_mode) {
		case IS_RNAME:
			column_name = sqlite3_column_name(s->stmt, i);
			if (column_name) {
				lua_setfield(L, -2, column_name);
			} else {
				lua_rawseti(L, -2, i + 1);
			}
			break;

		case IS_RINDEX:
			lua_rawseti(L, -2, i + 1);
			break;
		}
	}

	/* next row */
	switch (sqlite3_step(s->stmt)) {
	case SQLITE_DONE:
		s->row = 0;
		break;

	case SQLITE_ROW:
		break;
	
	default:
		error(L, s);
	}

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
	sqlite3_rec *s;
	is_metadata_mode_t metadata_mode; 
	const char *metadata_options[] = IS_METADATA_OPTIONS;
	int i;
	const char *value, *pos, *pos2;
	char *uvalue, *upos;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	metadata_mode = luaL_checkoption(L, 2, metadata_options[0],
			metadata_options);
	if (!s->stmt) {
		lua_pushliteral(L, "no statement to get metadata from");
		lua_error(L);
	}

	lua_createtable(L, s->column_count, 0);
	for (i = 0; i < s->column_count; i++) {
		switch (metadata_mode) {
		case IS_MNAME:
			value = sqlite3_column_name(s->stmt, i);
			if (value) {
				lua_pushstring(L, value);
			} else {
				lua_pushliteral(L, "");
			}
			break;

		case IS_MTYPE:
			value = sqlite3_column_decltype(s->stmt, i);
			if (value) {
				pos = index(value, '(');
				if (pos) {
					lua_pushlstring(L, value, pos - value);
				} else {
					lua_pushstring(L, value);
				}
			} else {
				lua_pushliteral(L, "");
			}
			break;

		case IS_MLENGTH:
			value = sqlite3_column_decltype(s->stmt, i);
			if (value && (pos = index(value, '('))) {
				pos++;
				pos2 = pos;
				while (isspace(*pos2)) {
					pos2++;
				}
				if (isdigit(*pos2)) {
					do {
						pos2++;
					} while (isdigit(*pos2));
					lua_pushlstring(L, pos, pos2 - pos);
					lua_tonumber(L, -1);
				} else {
					lua_pushnumber(L, 0);
				}
			} else {
				lua_pushnumber(L, 0);
			}
			break;

		case IS_MSCALE:
			value = sqlite3_column_decltype(s->stmt, i);
			if (value && (pos =index(value, ','))) {
				pos++;
				pos2 = pos;
				while (isspace(*pos2)) {
					pos2++;
				}
				if (isdigit(*pos2)) {
					do {
						pos2++;
					} while (isdigit(*pos2));
					lua_pushlstring(L, pos, pos2 - pos);
					lua_tonumber(L, -1);
				} else {
					lua_pushnumber(L, 0);
				}
			} else {
				lua_pushnumber(L, 0);
			}
			break;

		case IS_MLUATYPE:
			value = sqlite3_column_decltype(s->stmt, i);
			if (value) {
				/* convert to upper case */
				uvalue = strdup(value);
				if (!uvalue) {
					lua_pushliteral(L, "out of memory");
					lua_error(L);
				}
				upos = uvalue;
				while (*upos) {
					*upos = toupper(*upos);
					upos++;
				}

				/* follow SQLite3 type affinity rules */
				if (strstr(uvalue, "INT")) {
					lua_pushliteral(L, "number");
				} else if (strstr(uvalue, "CHAR")
						|| strstr(uvalue, "CLOB") 
						|| strstr(uvalue, "TEXT")
						|| strstr(uvalue, "BLOB")) {
					lua_pushliteral(L, "string");
				} else {
					lua_pushliteral(L, "number");
				}

				free(uvalue);
			} else {
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
	const sqlite3_rec *s;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	if (!s->db) {
		lua_pushliteral(L, "database is closed");
		lua_error(L);
	}

	lua_pushboolean(L, !sqlite3_get_autocommit(s->db));
	return 1;
}

/*
 * Executes a statement.
 */
static void execute_internal (lua_State *L, sqlite3_rec *s, const char *sql) {
	/* fainlize open statement */
	if (s->stmt) {
		sqlite3_finalize(s->stmt);
		s->stmt = NULL;
	}

	/* prepare statement */
	if (sqlite3_prepare_v2(s->db, sql, -1, &s->stmt, NULL) != SQLITE_OK) {
		error(L, s);
	}

	/* step */
	if (sqlite3_step(s->stmt) != SQLITE_DONE) {
		error(L, s);
	}
}

/*
 * Sets the autocommit state.
 */
static int begin (lua_State *L) {
	sqlite3_rec *s;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	if (!s->db) {
		lua_pushliteral(L, "database is closed");
		lua_error(L);
	}
	if (!sqlite3_get_autocommit(s->db)) {
		lua_pushliteral(L, "transaction already started");
		lua_error(L);
	}

	execute_internal(L, s, "BEGIN TRANSACTION");

	return 0;
}

/*
 * Commits a transaction.
 */
static int commit (lua_State *L) {
	sqlite3_rec *s;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	if (!s->db) {
		lua_pushliteral(L, "database is closed");
		lua_error(L);
	}
	if (sqlite3_get_autocommit(s->db)) {
		lua_pushliteral(L, "no transaction");
		lua_error(L);
	}

	execute_internal(L, s, "COMMIT TRANSACTION");

	return 0;
}

/*
 * Rollbacks a transaction.
 */
static int rollback (lua_State *L) {
	sqlite3_rec *s;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	if (!s->db) {
		lua_pushliteral(L, "database is closed");
		lua_error(L);
	}
	if (sqlite3_get_autocommit(s->db)) {
		lua_pushliteral(L, "no transaction");
		lua_error(L);
	}

	execute_internal(L, s, "ROLLBACK TRANSACTION");

        return 0;
}

/*
 * Returns the rowid of the last insert operation.
 */
static int last_insert_rowid (lua_State *L) {
	sqlite3_rec *s;

	s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);
	if (!s->db) {
		lua_pushliteral(L, "database is closed");
		lua_error(L);
	}

	lua_pushnumber(L, (lua_Number) sqlite3_last_insert_rowid(s->db));
	return 1;
}

/*
 * Returns a string representation of a connection.
 */
static int tostring (lua_State *L) {
        sqlite3_rec *s;
        const char *libversion;

        s = (sqlite3_rec *) luaL_checkudata(L, 1, IS_SQLITE3_METATABLE);

	libversion = sqlite3_libversion();
	lua_pushfstring(L, "SQLite3 connection [%s]", libversion);

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
 * Exported functions.
 */

int luaopen_is_sqlite3 (lua_State *L) {
	const char *modname;
	
	/* create driver */
	modname = luaL_checkstring(L ,1);
	luaL_register(L, modname, functions);

	/* create metatable */
	luaL_newmetatable(L, IS_SQLITE3_METATABLE);
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
	lua_pushcfunction(L, last_insert_rowid);
	lua_setfield(L, -2, "last_insert_rowid");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pop(L, 1);

	return 1;
}
