/*
 * Provides the IS MySQL module. See LICENSE for license terms.
 */

#include <stdlib.h>
#include <string.h>
#ifdef _REENTRANT
#include <pthread.h>
#endif
#include <stdint.h>
#include <mysql.h>
#include <lua.h>
#include <lauxlib.h>
#include "core.h"
#include "mysql.h"

#define IS_MYSQL_METATABLE "is_mysql"
#define IS_MYSQL_MAXPARAM 128

/*
 * MySQL record.
 */
typedef struct mysql_rec {
	MYSQL *mysql;
	MYSQL_STMT *stmt;
	MYSQL_RES *res;
	int intransaction;
	int field_count;
	MYSQL_FIELD *fields;
	MYSQL_BIND bind[IS_MYSQL_MAXPARAM];
	double doubles[IS_MYSQL_MAXPARAM];
	unsigned long lengths[IS_MYSQL_MAXPARAM];
	my_bool nulls[IS_MYSQL_MAXPARAM];
} mysql_rec;

/*
 * Lock for libmysqlclient parts that are not reentrant.
 */
#ifdef _REENTRANT
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#endif

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
 * Returns an int field.
 */
static int get_int_field (lua_State *L, int index, const char *field,
		int dflt) {
	int result;

	lua_getfield(L, index, field);
	result = lua_isnumber(L, -1) ? (int) lua_tonumber(L, -1) : dflt;
	lua_pop(L, 1);

	return result;
}

/*
 * Raises a MySQL error in Lua.
 */
static void error (lua_State *L, mysql_rec *m) {
	luaL_error(L, "MySQL error %d (%s): %s", mysql_errno(m->mysql),
			mysql_sqlstate(m->mysql), mysql_error(m->mysql));
}

/*
 * Connects to MySQL.
 */
static int connect (lua_State *L) {
	mysql_rec *m;
	const char *host, *user, *passwd, *db, *unix_socket, *charset;
	int port;

	luaL_checktype(L, 1, LUA_TTABLE);
	host = get_string_field(L, 1, "host", NULL);
	user = get_string_field(L, 1, "user", NULL);
	passwd = get_string_field(L, 1, "password", NULL);
	db = get_string_field(L, 1, "database", NULL);
	port = get_int_field(L, 1, "port", 0);
	unix_socket = get_string_field(L, 1, "unix_socket", NULL);
	charset = get_string_field(L, 1, "charset", NULL);

	m = (mysql_rec *) lua_newuserdata(L, sizeof(mysql_rec));
	memset(m, 0, sizeof(mysql_rec));
#ifdef _REENTRANT
	if (pthread_mutex_lock(&lock) != 0) {
		lua_pushliteral(L, "Error acquiring MySQL lock");
		lua_error(L);
	} 
#endif
	m->mysql = mysql_init(NULL);
#ifdef _REENTRANT
	if (pthread_mutex_unlock(&lock) != 0) {
		lua_pushliteral(L, "Error releasing MySQL lock");
		lua_error(L);
	}
#endif
	if (!m->mysql) {
		lua_pushliteral(L, "MySQL error: out of memory");
		lua_error(L);
	}
	if (!mysql_real_connect(m->mysql, host, user, passwd, db, port,
			unix_socket, 0)) {
		lua_pushfstring(L, "MySQL error %d (%s): %s",
				mysql_errno(m->mysql), mysql_sqlstate(m->mysql),
				mysql_error(m->mysql));
		mysql_close(m->mysql);
		lua_error(L);
	}
	if (charset) {
		if (mysql_set_character_set(m->mysql, charset)) {
			error(L, m);
		}
	}
		
	luaL_getmetatable(L, IS_MYSQL_METATABLE);
	lua_setmetatable(L, -2);

	return 1;
}

/*
 * Returns a bitstring from a number.
 */
static int bitstring (lua_State *L) {
	uint32_t bits;
	size_t len, pos;
	char bitstring[4];

	bits = (uint32_t) luaL_checknumber(L, 1);
	len = 0;
	pos = 4;
	do {
		bitstring[--pos] = bits & 0xff;
		bits >>= 8;
		if (bitstring[pos] != 0) {
			len = 4 - pos;
		}
	} while (pos > 0);

	lua_pushlstring(L, &bitstring[4 - len], len);
	return 1;
}

/*
 * Closes s MySQL connection.
 */
static int close (lua_State *L) {
	mysql_rec *m;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (m->res) {
		mysql_free_result(m->res);
		m->res = NULL;
	}
	if (m->stmt) {
		mysql_stmt_close(m->stmt);
		m->stmt = NULL;
	}
	if (m->mysql) {
		mysql_close(m->mysql);
		m->mysql = NULL;
	}

	return 0;
}

/*
 * Executes a MySQL statement.
 */
static int execute (lua_State *L) {
	mysql_rec *m;
	const char *sql;
	unsigned long param_count;
	int i;
	size_t len;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->mysql) {
		lua_pushliteral(L, "connection is closed");
		lua_error(L);
	}
	sql = luaL_checkstring(L, 2);

	/* close open statement */	
	if (m->res) {
		mysql_free_result(m->res);
		m->res = NULL;
	}
	if (m->stmt) {
		mysql_stmt_close(m->stmt);
		m->stmt = NULL;
	}

	/* prepare */
	m->stmt = mysql_stmt_init(m->mysql);
	if (!m->stmt) {
		error(L, m);
	}
	if (mysql_stmt_prepare(m->stmt, sql, strlen(sql)) != 0) {
		error(L, m);
	}

	/* bind params */
	param_count = mysql_stmt_param_count(m->stmt);
	if (lua_gettop(L) - 2 != param_count) {
		luaL_error(L, "expected %d bind params, got %d", param_count,
				lua_gettop(L) - 2);
	}
	if (param_count > IS_MYSQL_MAXPARAM) {
		luaL_error(L, "maximum %d bind params, got %d",
				IS_MYSQL_MAXPARAM, param_count);
	}
	memset(m->bind, 0, param_count * sizeof(MYSQL_BIND));
	for (i = 0; i < param_count; i++) {
		switch (lua_type(L, i + 3)) {
		case LUA_TNIL:
			m->bind[i].buffer_type = MYSQL_TYPE_NULL;
			break;

		case LUA_TBOOLEAN:
			m->bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
			m->doubles[i] = lua_toboolean(L, i + 3)
					? 1.0 : 0.0;
			m->bind[i].buffer = &m->doubles[i];
			break;

		case LUA_TNUMBER:
			m->bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
			m->doubles[i] = (double) lua_tonumber(L, i + 3);
			m->bind[i].buffer = &m->doubles[i];
			break;

		case LUA_TSTRING:
			m->bind[i].buffer_type = MYSQL_TYPE_STRING;
			m->bind[i].buffer = (void *) lua_tolstring(L, i + 3,
					&len);
			m->bind[i].buffer_length = len;
			m->bind[i].length = &m->bind[i].buffer_length;
			break;

		default:
			luaL_error(L, "unsupported %s bind param %d",
					luaL_typename(L, i + 3), i + 1);
		}
	}
	if (mysql_stmt_bind_param(m->stmt, m->bind) != 0) {
		error(L, m);
	}

	/* execute */
	if (mysql_stmt_execute(m->stmt) != 0) {
		error(L, m);
	}

	/* setup fetch */
	m->res = mysql_stmt_result_metadata(m->stmt);
	if (m->res) {
		m->field_count = mysql_num_fields(m->res);
		if (m->field_count > IS_MYSQL_MAXPARAM) {
			luaL_error(L, "maximum %d bind results, got %d",
					IS_MYSQL_MAXPARAM, m->field_count);
		}
		m->fields = mysql_fetch_fields(m->res);
		memset(m->bind, 0, m->field_count * sizeof(MYSQL_BIND));
		for (i = 0; i < m->field_count; i++) {
			switch (m->fields[i].type) {
			case MYSQL_TYPE_NULL:
				m->bind[i].buffer_type = MYSQL_TYPE_NULL;
				break;

			case MYSQL_TYPE_TINY:
			case MYSQL_TYPE_SHORT:
			case MYSQL_TYPE_LONG:
			case MYSQL_TYPE_INT24:
			case MYSQL_TYPE_LONGLONG:
			case MYSQL_TYPE_DECIMAL:
			case MYSQL_TYPE_NEWDECIMAL:
			case MYSQL_TYPE_FLOAT:
			case MYSQL_TYPE_DOUBLE:
				m->bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
				m->bind[i].buffer = &m->doubles[i];
				m->bind[i].is_null = &m->nulls[i];
				break;

			case MYSQL_TYPE_BIT:
				m->bind[i].buffer_type = MYSQL_TYPE_BIT;
				m->bind[i].buffer = &m->doubles[i];
				m->bind[i].buffer_length = sizeof(double);
				m->bind[i].length = &m->lengths[i];
				m->bind[i].is_null = &m->nulls[i];
				break;

			case MYSQL_TYPE_TIMESTAMP:
			case MYSQL_TYPE_DATE:
			case MYSQL_TYPE_TIME:
			case MYSQL_TYPE_DATETIME:
			case MYSQL_TYPE_YEAR:
			case MYSQL_TYPE_STRING:
			case MYSQL_TYPE_VAR_STRING:
			case MYSQL_TYPE_BLOB:
			case MYSQL_TYPE_SET:
			case MYSQL_TYPE_ENUM:
			case MYSQL_TYPE_GEOMETRY:
				m->bind[i].buffer_type = MYSQL_TYPE_STRING;
				m->bind[i].length = &m->lengths[i];
				m->bind[i].is_null = &m->nulls[i];
				break;

			default:
				luaL_error(L, "unsupported type %d result "
						"param %d", m->fields[i].type,
						i + 1);
			}
		}
		if (mysql_stmt_bind_result(m->stmt, m->bind) != 0) {
			error(L, m);
		}

		/* no result */
		return 0;
	} else {
		/* return number of rows affected */
		lua_pushinteger(L, (lua_Integer) mysql_stmt_affected_rows(
				m->stmt));
		return 1;
	}
}

/*
 * Fetches a string column.
 */
void read_string (lua_State *L, mysql_rec *m, int i) {
	luaL_Buffer b;
	unsigned long length, offset, count;

	luaL_buffinit(L, &b);
	length = m->lengths[i];
	offset = 0;
	m->bind[i].buffer_length = LUAL_BUFFERSIZE;
	while (offset < length) {
		m->bind[i].buffer = luaL_prepbuffer(&b);
		if (mysql_stmt_fetch_column(m->stmt, &m->bind[i], i, offset)
				!= 0) {
			error(L, m);
		}
		count = length - offset;
		if (count > LUAL_BUFFERSIZE) {
			count = LUAL_BUFFERSIZE;
		}
		luaL_addsize(&b, count);
		offset += count;
	}
	luaL_pushresult(&b);
}

/*
 * Fetches a bit column.
 */
void read_bit (lua_State *L, mysql_rec *m, int i) {
	unsigned char *bits;
	uint32_t bit_value;
	int j;

	bits = (unsigned char *) &m->doubles[i];
	if (m->fields[i].length == 1) {
		lua_pushboolean(L, *bits == 1);
	} else {
		bit_value = 0;
		for (j = 0; j < m->lengths[i]; j++) {
			bit_value <<= 8;
			bit_value |= bits[j];
		}
		lua_pushnumber(L, (lua_Number) bit_value);
	}
}
	
/*
 * Reads a row.
 */
static int read (lua_State *L) {
	mysql_rec *m;
	is_read_mode_t read_mode;
	const char *read_mode_options[] = IS_READ_MODE_OPTIONS;
	int i;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->res) {
		lua_pushliteral(L, "no statement to read from");
		lua_error(L);
	}
	read_mode = luaL_checkoption(L, 2, read_mode_options[0],
			read_mode_options);
	
	switch (mysql_stmt_fetch(m->stmt)) {
	case 0:
	case MYSQL_DATA_TRUNCATED:
		switch (read_mode) {
		case IS_RNAME:
			lua_createtable(L, 0, m->field_count);
			break;

		case IS_RINDEX:
			lua_createtable(L, m->field_count, 0);
			break;
		}
		for (i = 0; i < m->field_count; i++) {
			/* ignore NULL */
			if (m->nulls[i]) {
				continue;
			}

			/* handle types */
			switch (m->bind[i].buffer_type) {
			case MYSQL_TYPE_NULL:
				lua_pushnil(L);
				break;

			case MYSQL_TYPE_DOUBLE:
				lua_pushnumber(L, m->doubles[i]);
				break;

			case MYSQL_TYPE_BIT:
				read_bit(L, m, i);
				break;

			case MYSQL_TYPE_STRING:
				read_string(L, m, i);
				break;

			default:
				/* not reached */
				lua_pushliteral(L, "internal error");
				lua_error(L);
			}

			/* store */
			switch (read_mode) {
			case IS_RNAME:
				lua_setfield(L, -2, m->fields[i].name);
				break;

			case IS_RINDEX:
				lua_rawseti(L, -2, i + 1);
				break;
			}
		}
		return 1;

	case 1:
		error(L, m);
	}

	/* done */
	if (m->res) {
		mysql_free_result(m->res);
		m->res = NULL;
	}
	if (m->stmt) {
		mysql_stmt_close(m->stmt);
		m->stmt = NULL;
	}

	return 0;
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
	mysql_rec *m;
	is_metadata_mode_t metadata_mode;
	const char *metadata_options[] = IS_METADATA_OPTIONS;
	int i;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	metadata_mode = luaL_checkoption(L, 2, metadata_options[0],
			metadata_options);
	if (m->res == NULL) {
		lua_pushliteral(L, "no statement to get metadata from");
		lua_error(L);
	}

	lua_createtable(L, m->field_count, 0);
	for (i = 0; i < m->field_count; i++) {
		switch (metadata_mode) {
		case IS_MNAME:
			lua_pushstring(L, m->fields[i].name);
			break;

		case IS_MTYPE:
			switch (m->fields[i].type) {
			case MYSQL_TYPE_TINY:
				lua_pushliteral(L, "TINYINT");
				break;

			case MYSQL_TYPE_SHORT:
				lua_pushliteral(L, "SMALLINT");
				break;

			case MYSQL_TYPE_LONG:
				lua_pushliteral(L, "INTEGER");
				break;
	
			case MYSQL_TYPE_INT24:
				lua_pushliteral(L, "MEDIUMINT");
				break;

			case MYSQL_TYPE_LONGLONG:
				lua_pushliteral(L, "BIGINT");
				break;

			case MYSQL_TYPE_DECIMAL:
				lua_pushliteral(L, "DECIMAL");
				break;

			case MYSQL_TYPE_NEWDECIMAL:
				lua_pushliteral(L, "DECIMAL");
				break;

			case MYSQL_TYPE_FLOAT:
				lua_pushliteral(L, "FLOAT");
				break;

			case MYSQL_TYPE_DOUBLE:
				lua_pushliteral(L, "DOUBLE");
				break;

			case MYSQL_TYPE_BIT:
				lua_pushliteral(L, "BIT");
				break;

			case MYSQL_TYPE_TIMESTAMP:
				lua_pushliteral(L, "TIMESTAMP");
				break;

			case MYSQL_TYPE_DATE:
				lua_pushliteral(L, "DATE");
				break;

			case MYSQL_TYPE_TIME:
				lua_pushliteral(L, "TIME");
				break;

			case MYSQL_TYPE_DATETIME:
				lua_pushliteral(L, "DATETIME");
				break;

			case MYSQL_TYPE_YEAR:
				lua_pushliteral(L, "YEAR");
				break;

			case MYSQL_TYPE_STRING:
				if (m->fields[i].flags & BINARY_FLAG) {
					lua_pushliteral(L, "BINARY");
				} else {
					lua_pushliteral(L, "CHAR");
				}
				break;

			case MYSQL_TYPE_VAR_STRING:
				if (m->fields[i].flags & BINARY_FLAG) {
					lua_pushliteral(L, "VARBINARY");
				} else {
					lua_pushliteral(L, "VARCHAR");
				}
				break;

			case MYSQL_TYPE_BLOB:
				if (m->fields[i].flags & BINARY_FLAG) {
					lua_pushliteral(L, "BLOB");
				} else {
					lua_pushliteral(L, "TEXT");
				}
				break;

			case MYSQL_TYPE_SET:
				lua_pushliteral(L, "SET");
				break;

			case MYSQL_TYPE_ENUM:
				lua_pushliteral(L, "ENUM");
				break;

			case MYSQL_TYPE_GEOMETRY:
				lua_pushliteral(L, "SPATIAL");
				break;

			case MYSQL_TYPE_NULL:
				lua_pushliteral(L, "NULL");
				break;

			default:
				lua_pushliteral(L, "UNKNOWN");
			}
			break;

		case IS_MLENGTH:
			lua_pushnumber(L, m->fields[i].length);
			break;

		case IS_MSCALE:
			lua_pushnumber(L, m->fields[i].decimals);
			break;

		case IS_MLUATYPE:
			switch (m->fields[i].type) {
			case MYSQL_TYPE_TINY:
			case MYSQL_TYPE_SHORT:
			case MYSQL_TYPE_LONG:
			case MYSQL_TYPE_INT24:
			case MYSQL_TYPE_LONGLONG:
			case MYSQL_TYPE_DECIMAL:
			case MYSQL_TYPE_NEWDECIMAL:
			case MYSQL_TYPE_FLOAT:
			case MYSQL_TYPE_DOUBLE:
				lua_pushliteral(L, "number");
				break;

			case MYSQL_TYPE_BIT:
				if (m->fields[i].length == 1) {
					lua_pushliteral(L, "boolean");
				} else {
					lua_pushliteral(L, "number");
				}
				break;

			case MYSQL_TYPE_TIMESTAMP:
			case MYSQL_TYPE_DATE:
			case MYSQL_TYPE_TIME:
			case MYSQL_TYPE_DATETIME:
			case MYSQL_TYPE_YEAR:
			case MYSQL_TYPE_STRING:
			case MYSQL_TYPE_VAR_STRING:
			case MYSQL_TYPE_BLOB:
			case MYSQL_TYPE_SET:
			case MYSQL_TYPE_ENUM:
			case MYSQL_TYPE_GEOMETRY:
				lua_pushliteral(L, "string");
				break;

			default:
				lua_pushliteral(L, "nil");
			}
			break;
		}
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

/*
 * Returns the transaction state.
 */
static int intransaction (lua_State *L) {
	const mysql_rec *m;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->mysql) {
		lua_pushliteral(L, "connection is closed");
		lua_error(L);
	}

	lua_pushboolean(L, m->intransaction);
	return 1;
}


/*
 * Executea an SQL statement for internal use.
 */
static void execute_internal (lua_State *L, mysql_rec *m, const char *sql) {
	/* close open statement */
	if (m->res) {
		mysql_free_result(m->res);
		m->res = NULL;
	}
	if (m->stmt) {
		mysql_stmt_close(m->stmt);
		m->stmt = NULL;
	}

	/* execute */
	if (mysql_query(m->mysql, sql) != 0) {
		error(L, m);
	}
}


/*
 * Begins a transaction.
 */
static int begin (lua_State *L) {
	mysql_rec *m;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->mysql) {
		lua_pushliteral(L, "connection is closed");
		lua_error(L);
	}
	if (m->intransaction) {
		lua_pushliteral(L, "transaction already started");
		lua_error(L);
	}

	execute_internal(L, m, "START TRANSACTION");
	m->intransaction = 1;

	return 0;
}

/*
 * Commits a transaction.
 */
static int commit (lua_State *L) {
	mysql_rec *m;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->mysql) {
		lua_pushliteral(L, "connection is closed");
		lua_error(L);
	}
	if (!m->intransaction) {
		lua_pushliteral(L, "no transaction");
		lua_error(L);
	}

	execute_internal(L, m, "COMMIT");
	m->intransaction = 0;

	return 0;
}

/*
 * Rollbacks a transaction.
 */
static int rollback (lua_State *L) {
	mysql_rec *m;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->mysql) {
		lua_pushliteral(L, "connection is closed");
		lua_error(L);
	}
	if (!m->intransaction) {
		lua_pushliteral(L, "no transaction");
		lua_error(L);
	}

	execute_internal(L, m, "ROLLBACK");
	m->intransaction = 0;

        return 0;
}

/*
 * Returns value generated for an AUTO_INCREMENT column.
 */
static int insert_id (lua_State *L) {
	mysql_rec *m;
	my_ulonglong insert_id;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->mysql) {
		lua_pushliteral(L, "connection is closed");
		lua_error(L);
	}

	insert_id = mysql_insert_id(m->mysql);
	
	lua_pushnumber(L, (lua_Number) insert_id);
	return 1;
}

/*
 * Executes an SQL statement directly. This is required for statements
 * which cannot be prepared.
 */
static int execute_direct (lua_State *L) {
	mysql_rec *m;
	const char *sql;
	MYSQL_RES *res;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);
	if (!m->mysql) {
		lua_pushliteral(L, "connection is closed");
		lua_error(L);
	}
        sql = luaL_checkstring(L, 2);

        /* close open statement */
	if (m->res) {
		mysql_free_result(m->res);
		m->res = NULL;
	}
	if (m->stmt) {
		mysql_stmt_close(m->stmt);
		m->stmt = NULL;
	}

        /* execute */
	if (mysql_real_query(m->mysql, sql, lua_objlen(L, 2)) != 0) {
		error(L, m);
	}
	if (mysql_field_count(m->mysql) > 0) {
		/* discard result set, if any */
		if  ((res = mysql_store_result(m->mysql)) == NULL) {
			error(L, m);
		}
		mysql_free_result(res);
		return 0;
	} else {
		/* push number of affected rows */
		lua_pushinteger(L, (lua_Integer) mysql_affected_rows(m->mysql));
		return 1;
	}
}

/*
 * Returns a string representation of a connection.
 */
static int tostring (lua_State *L) {
	mysql_rec *m;
	const char *client_info, *server_info;

	m = (mysql_rec *) luaL_checkudata(L, 1, IS_MYSQL_METATABLE);

	client_info = mysql_get_client_info();
	if (m->mysql) {
		server_info = mysql_get_server_info(m->mysql);
		lua_pushfstring(L, "MySQL connection [%s] [%s]", client_info,
				server_info);
	} else {
		lua_pushfstring(L, "MySQL connection [%s]", client_info);
	}
	return 1;
}

/*
 * Module functions.
 */
static const luaL_Reg functions[] = {
	{ IS_FCONNECT, connect },
	{ "bitstring", bitstring },
	{ NULL, NULL }
};

/*
 * Exported functions.
 */

int luaopen_is_mysql (lua_State *L) {
	const char *modname;
	
	/* create driver */
	modname = luaL_checkstring(L ,1);
	luaL_register(L, modname, functions);

	/* create metatable */
	luaL_newmetatable(L, IS_MYSQL_METATABLE);
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
	lua_pushcfunction(L, insert_id);
	lua_setfield(L, -2, "insert_id");
	lua_pushcfunction(L, execute_direct);
	lua_setfield(L, -2, "execute_direct");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, tostring);
	lua_setfield(L, -2, "__tostring");
	lua_pop(L, 1);

	return 1;
}
