#ifndef IS_INCLIDED
#define IS_INCLUDED

#include <lua.h>

/* IS fields */
#define IS_FDRIVER "driver"
#define IS_FCONNECT "connect"
#define IS_FCLOSE "close"
#define IS_FEXECUTE "execute"
#define IS_FMETADATA "metadata"
#define IS_FREAD "read"
#define IS_FROWS "rows"
#define IS_FINTRANSACTION "intransaction"
#define IS_FBEGIN "begin"
#define IS_FCOMMIT "commit"
#define IS_FROLLBACK "rollback"

/*
 * Read mode enum.
 */
typedef enum is_read_mode {
	IS_RNAME,
	IS_RINDEX
} is_read_mode_t;

/*
 * Read mode strings.
 */
#define IS_READ_MODE_OPTIONS { "name", "index", NULL }

/*
 * Metadata mode enum.
 */
typedef enum is_metadata_mode {
        IS_MNAME,
        IS_MTYPE,
        IS_MLENGTH,
        IS_MSCALE,
        IS_MLUATYPE
} is_metadata_mode_t;

/*
 * Metadata mode strings.
 */
#define IS_METADATA_OPTIONS { "name", "type", "length", "scale", "luatype", \
		NULL };


/*
 * Opens the IS module.
 *
 * @param L the Lua state
 * @return 1 (module)
 */
int luaopen_is (lua_State *L);

#endif /* IS_INCLUDED */
