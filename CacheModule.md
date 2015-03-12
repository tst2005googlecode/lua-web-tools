

# Introduction #

This page describes the functions provided by the cache module. The cache module supports the access to caches. More specifically, the following cache is currently supported:

  * memcached

## Example ##

```
-- Import
require "cache"
require "cache.memcached"

-- Configure cache
CACHE = cache.configure({ driver = cache.memcached })

-- Set some values
CACHE:set("a_boolean", true)
CACHE:set("a_number", 1.0)
CACHE:set("a_string", "abc", 3600) -- expires in one hour
CACHE:set("a_table", { a = 1, b = 2, c = 3 }, 3600)

-- Get some values
CACHE:get("a_boolean")
CACHE:get("a_number")
CACHE:get("a_string")
CACHE:get("a_table")

-- Clear
CACHE:set("a_boolean", nil)

-- Add new
CACHE:add("a_boolean", false, 3600)

-- Replace existing
CACHE:replace("a_number", 2.0, 3600)

-- Counters
value = CACHE:inc("a_counter", 1, 1, 3600)
```

# Import #

```
require "cache"
```

## memcached ##

```
require "cache.memecached"
```

# Functions #

The functions described in this section are common to all caches.

## `cache.configure (table)` ##

Creates and returns a connector to a cache using the configuration arguments supplied in `table`. `table` must contain a field `driver` that determines the type of cache to connect to. Other required fields depend on the driver and are documented in the respective sections below. The function returns a connector if it is successful, and raises an error otherwise.

## `connector:close ()` ##

Explicitly closes the connector. Connectors are closed implicitly when they are garbage collected.

## `connector:get (key)` ##

Queries the cache for the value associated with the specified string key, and returns the value stored in the cache; if there is no value for the specified key, the function returns `nil`.

## `connector:set (key, value [, timeout])` ##

Stores `value` associated with the specified string key in the cache, and returns `true`. If `value` is `nil`, the function unconditionally deletes the value associated with `key`, and returns `true` if a value was deleted, and `false` otherwise. If `timeout` is specified, it is an integer value that specifies the number of seconds the association remains in the cache; however there is no guarantee that the association will not be evicted beforehand.

## `connector:add (key, value [, timeout])` ##

Stores `value` associated with the specified string key in the cache and returns `true` unless there already is a value associated with `key`, in which case the function does nothing and returns `false`. A `nil` value and `timeout` behave in the same way as in `connector:set`.

## `connector:replace (key, value [, timeout])` ##

Stores `value` associated with the specified string key in the cache and returns `true` unless there is no value associated with `key`, in which case the function does nothing and returns `false`. A `nil` value and `timeout` behave in the same way as in `connector:set`.

## `connector:inc (key [, delta [, initial [, timeout]]])` ##

Increments the counter associated with the specified string key in the cache. `delta` is an integer value that specifies the value the counter is incremented by; if absent, it defaults to `1`. `initial` is an integer value that specifies the resulting value of the counter if the association does not yet exist in the cache; if absent, it defaults to `1`. `timeout` behaves in the same way as in `connector:set`. The function returns the resulting value of the counter.

Use `connector:inc(key, 0)` to query the value of a counter, as counters are different from regularly encoded Lua values and hence cannot be queried using `connector:get`.

## `connector:dec (key [, delta [, initial [, timeout]]])` ##

Decrements the counter associated with the specified string key in the cache. `delta` is an integer value that specifies the value the counter is decremented by; if absent, it defaults to `1`. `initial` is an integer value that specifies the resulting value of the counter if the association does not yet exist in the cache; if absent, it defaults to `1`. `timeout` behaves in the same way as in the `connector:set`. The function returns the resulting value of the counter.

## `connector:flush (key)` ##

Flushes _all associations_ from the cache instance associated with the specified string key.

# memcached Specifics #

## Timeout Behaviour ##

memcached applies the following specific timeout behavior:

  * if `timeout` is `0`, the association does not expire, i.e. the behavior is the same as if `timeout` had been omitted
  * if `timeout` is larger than `30 * 24 * 60 * 60` (i.e. 30 days), the value is interpreted as an absolute Unix domain timestamp rather than a relative number of seconds

## Configuration Arguments ##

The memcached driver supports the following configuration arguments:

| **Field** | **Optional** | **Description** |
|:----------|:-------------|:----------------|
| `driver` | no | must be `cache.memcached` |
| `map` | yes | a function receiving a string key and returning two string values specifying the host and port of the cache instance that manages the key; if absent, the cache module provides a default function that maps all keys to `localhost`, `11211`, i.e. a cache instance running on the local machine and listening on the default port |
| `encode` | yes | a function receiving a Lua value and returning a string serialization of that value; if absent, the cache module provides a default encoder for `boolean`, `number`, `string` and `table` with support for recursive table structures, and favoring decoding performance over encoding performance |
| `decode` | yes | a function receiving a string serialization and returning the Lua value of that serialization; if absent, the cache module provides a default decoder corresponding to the default encoder |

## `memcached_connector:stat (key [, type])` ##

Returns a table with statistics from the cache instance associated with the specified string key. If `type` is omitted, the function returns general-purpose statistics; if `type` is one of `items`, `sizes`, or `slabs`, the function returns the requested specific statistics.