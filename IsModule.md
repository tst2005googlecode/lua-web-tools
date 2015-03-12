

# Introduction #

This page describes the functions provided by the IS module. This IS module supports the access to information systems in an SQL injection safe way. More specifically, the following information systems are currently supported:

  * MySQL
  * TDS (Sybase, SQL Server via FreeTDS)
  * SQLite3

## Example ##

```
-- Import
require "is"
require "is.mysql"

-- Define database
DB = {
    driver = is.mysql,
    user = "me",
    password = "secret",
    database = "mydatabase",
    charset = "utf8"
}

-- Connect
conn = is.connect(DB)

-- Create table
conn:execute("CREATE TABLE IF NOT EXISTS DUMMY (I INTEGER NOT NULL AUTO_INCREMENT PRIMARY KEY, V INTEGER NOT NULL)")

-- Insert rows
for i = 1, 10 do
    conn:execute("INSERT INTO DUMMY (V) VALUES (?)", i)
end

-- Read
conn:execute("SELECT * FROM DUMMY WHERE V > ?", 5)
sum = 0
for row in conn:rows() do
    sum = sum + row.V
end
conn:execute("SELECT COUNT(*) FROM DUMMY WHERE V > ?", 5)
row = conn:read("index")
cnt = row[1]
```

# Import #

```
require "is"
```

## MySQL ##

```
require "is.mysql"
```

## TDS ##

```
require "is.tds"
```

## SQLite3 ##

```
require "is.sqlite3"
```

# Functions #

The functions described in this section are common to all information systems.

## `is.date (time)` ##

Returns a string in the `YYYY-MM-DD HH:MM:SS` format using UTC corresponding to the Unix timestamp `time`; returns `nil` if `time` is `nil`.

## `is.time (date)` ##

Returns a Unix timestamp corresponding the string `date` in the `YYYY-MM-DD HH:MM:SS` format using UTC; returns `nil` if `date` is `nil`.

## `is.connect (table)` ##

Creates and returns a connection to an information system using the connection arguments supplied in `table`. `table` must contain a field `driver` that determines the type of information system to connect to. Other required fields depend on the driver and are documented in the respective sections below. The function returns a connection if it is successful, and raises an error otherwise.

## `connection:close ()` ##

Explicitly closes the connection. Connections are closed implcitly when they are garbage collected.

## `connection:execute (sql, ...)` ##

Prepares the supplied SQL statement which may contain binding variables represented by `?`, and then binds the values following the SQL statement to these variables. If the SQL statement modifies rows (such as in `INSERT`, `DELETE`, `UPDATE`), the function returns the number of rows affected; if the SQL statement produces rows (such as in `SELECT`), the function returns no value.

## `connection:read ([read_mode])` ##

Reads the next row produced by the previously executed SQL statement and returns that row as a table, or `nil` if there are no more rows. If `read_mode` is absent or `name`, the table keys are column names; if `read_mode` is `index`, the table keys are column numbers.

## `connection:rows ([read_mode])` ##

Returns three values so that the construction

```
for row in connection:rows ([read_mode]) do body end
```

iterates over all rows produced by the previously executed SQL statement. The `read_mode` parameter has the same meaning as in `connection:read()`.

## `connection:metadata (meta_data)` ##

Returns meta data from a previously executed SQL statement that produces rows. The function returns a table where keys are column numbers (1-based) and values depend on `meta_data` as follows:

| **`meta_data`** | **Description** |
|:----------------|:----------------|
| `name` | column name|
| `type` | column data type expressed in the type system of the information system |
| `length` | column length, size or precision |
| `scale` | column scale |
| `luatype` | mapped Lua type, such as `string`, `number`, or `boolean` |

The function must not be invoked after `connection:read()` or the iterator function returned by `conection:rows()` have returned `nil`.

## `connection:intransaction ()` ##

Returns whether there is an open transation on the connection.

## `connection:begin ()` ##

Begins a transaction on the connection. The function raises an error if there already is an open transaction.

## `connection:commit ()` ##

Commits an open transaction on the connection. The function raises an error if there is no open transaction.

## `connection:rollback ()` ##

Rolls back an open transaction on the connection. The function raises an error if there is no open transaction.

# MySQL Specific Functions #

## Type Mapping ##

The following type mapping is specific to MySQL:

  * `bit(1)` is mapped to and from `boolean`
  * `bit(n)` is mapped to and from a number suitable for Lua bit libraries (1 < `n` <= 32)

## Connection Arguments ##

The MySQL driver supports the following connection arguments:

| **Field** | **Optional** | **Description** |
|:----------|:-------------|:----------------|
| `driver` | no | must be `is.mysql` |
| `host` | yes | the host to connect to |
| `user` | yes | the user to connect as |
| `password` | yes | the password to use |
| `database` | yes | the database to use |
| `port` | yes | the port to connect to |
| `unix_socket` | yes | the Unix socket to connect to |
| `charset` | yes | the charset to use for the connection |

## `mysql_connection:insert_id ()` ##

Returns the auto increment number generated by the previously executed `INSERT` statement on the connection.

## `mysql_connection:execute_direct (sql)` ##

Executes an SQL statement without preparing it first. This function must be used for a very limited set of SQL statements, such as `LOCK TABLES` for which MySQL does not support preparing. If the SQL statement modifies rows, the function returns the number of rows affected; if the SQL statement produces rows, the result set is discarded, and the function returns no value.

# TDS Specific Functions #

## Type Mapping ##

The following type mapping is specific to TDS:

  * `bit` is mapped to and from `boolean`
  * `date`, `time`, `datetime` and `smalldatetime` are mapped to the `YYYY-MM-DD HH:MM:SS` string format

## Connection Arguments ##

The TDS driver supports the following connection arguments:

| **Field** | **Optional** | **Description** |
|:----------|:-------------|:----------------|
| `driver` | no | must be `is.tds` |
| `server` | no | the server to connect to; this is a logical server name that is defined in the FreeTDS configuration |
| `user` | no | the user to connect as |
| `password` | yes | the password to use |
| `database` | yes | the database to use |
| `application` | yes | the name of the connecting application |
| `workstation` | yes | the name of the connecting workstation |
| `charset` | yes | the charset to use for the connection |

## `tds_connection:messages ()` ##

Returns a table value with number indexes containing the messages returned by the server for the last function called on the connection.

# SQLite3 Specific Functions #

## Type Mapping ##

SQLite 3 uses a [dynamic type system](http://www.sqlite.org/datatype3.html) with type affinity. The dynamic type of values may be different from the static type returned by `connection:metadata()`.

## Connection Arguments ##

The SQLite3 driver supports the following connection arguments:

| **Field** | **Optional** | **Description** |
|:----------|:-------------|:----------------|
| `driver` | no | must be `is.sqlite3` |
| `filename` | no | the file containing the database |
| `readonly` | yes | whether to open the database in read-only mode; default: read-write |
| `nocreate` | yes | whether to raise an error if the database does not exists; default: create it |
| `vfs` | yes | the name of the virtual file system to use |

## `sqlite3_connection:last_insert_rowid ()` ##

Returns the row ID generated by the previously executed `INSERT` statement on the connection.