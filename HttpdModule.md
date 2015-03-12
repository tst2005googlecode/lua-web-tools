# Introduction #

This page describes the functions provided by the HTTPD module.



# Import #

```
require "httpd"
```

# Functions #

## `httpd.pairs (apr_table)` ##

Returns three values so that the construction

```
for key, value in httpd.pairs(t) do body end
```

will iterate over all key-value pairs in the passed APR table. This function is used with the header tables of the `request` value and with the `args` value.

When using Lua 5.2, you can also use the regular `pairs` function instead of `httpd.pairs`.

## `httpd.set_status (status)` ##

Sets the HTTP response status to `status`, a number between 100 and 599.

Calling this function is different from returning a status from a script. If a script calls this function and then returns no value or `nil`, the requested is considered as handled successfully, and the output from the script is fully processed; if a script returns a HTTP status, processing is defered to the Apache error document handling. Use this function if a script does generate output and requires a status other than 200 (OK); return a status if a script generates no output, such as in a 204 (No Content) or 404 (Not Found) situation.

## `httpd.set_content_type (content_type)` ##

Sets the HTTP response content type to the specified value.

## `httpd.add_header (name, value [, err_header])` ##

Adds a HTTP response header with the specified name. `value` is a string representing the value of the header. If `err_header` is `true` when evaluated as a boolean value, the header is added to the error output headers which are transmitted regardless of the outcome of the script; else the header is added to the regular output headers which are only transmitted if the request is handled successfully, i.e. if the script returns no value or `nil`.

Calling this function is different from index assigning to the `request.headers_out` and `request.err_headers_out` tables. This function _adds_ a new header regardless of whether a header with the same name already exists; index assigning to the headers tables _replaces_ a header with the same name.

## `httpd.add_cookie (name [, value [, expires [, path [, domain [, secure [, httponly]]]]]])` ##

Adds a cookie with the specified name. `value` is a string representing the value of the cookie; if `value` is absent, the value of the cookie is the empty string. `expires` is a number representing the expiry of the cookie as a Unix timestamp; if `expires` is absent or negative, the cookie is valid for the session only. `path` and `domain` are string values representing the path and domain of the cookie respectively; if they are absent, the cookie is added without an explicit path or domain. If `secure` and `httponly` are `true` when evaluated as boolean values, they respectively indicate that the cookie must only be transmitted over secure connections and that the cookie is unavailable to client-side scripting; otherwise the cookie is added without these security restrictions.

## `httpd.write_template (filename [, flags [, file]])` ##

Writes a template from a file designated by `filename`. `flags` is a string with each character modifying the processing of the template as follows:

  * `p`: parses the template; if absent, the template is written as is
  * `u`: escapes URI reserved characters in substitutions
  * `x`: escapes XML reserved characters in substitutions
  * `j`: escapes JavaScript string literal reserved characters in substitutions
  * `n`: writes an empty string for `nil` values in substitution expressions
  * `e`: writes an empty string for substitution expressions with a runtime error; if absent, a runtime error is propagated to the caller

If `flags` is absent, it defaults to `px`.

`file` is the file descriptor the rendered template is written into; if `file` is absent, it defaults to `httpd.output`; if `file` is `true`, the template is rendered into memory and returned as a string.

If a template is parsed, the following instructions are processed by the template engine:

| **Instruction** | **Description** |
|:----------------|:----------------|
| `<l:if cond="cond_0"> template_0 <l:elseif cond="cond_1" /> template_1 ... <l:elseif cond="cond_n" /> template_n <l:else /> template_else </l:if>` | Evaluates `cond_0` and processes `template_0` if true, otherwise continues to evaluate `cond_1` to `cond_n` processing one of `template_1` to `template_n` if the respective condition is true, otherwise processes `template_else`. The use of the `l:elseif` and `l:else` elements is optional. |
| `<l:for names="name_1, name_2, ... , name_n" in="iterator_exp"> template </l:for>` | Equivalent to the construction `for name_1, name_2, ..., name_n in iterator_exp do p(template) end` where `p(template)` represents the processing of `template`. |
| `<l:set names="name_1, name_2, ..., name_n" expressions="exp_1, exp_2, ..., exp_n" />` | Equivalent to the construction `name_1, name_2, ..., name_n = exp_1, exp_2, ..., exp_n`. |
| `<l:include filename="filename_exp" flags="flags" />` | Processes the template from the file referenced by `filename_exp`. `flags` are optional and have the same meaning and default as in `httpd.write_template`. |
| `${exp}` | Substitutes the value of `exp`. If the value is not a string or number, substitutes `(type)` where `type` is the type of the value. Substitutions are processed following the current flags, ignoring `p`. |
| `$[flags]{exp}` | Equivalent to `${exp}` with `flags` used instead of the current flags. |

## `httpd.escape_uri (string)` ##

Escapes URI reserved characters in a string and returns the escaped string. More specifically, alphanumeric characters, `-`, `.`, `_`, and `~` are considered unreserved characters, and all other characters are considered reserved characters according to RFC 3986. Reserved characters are escaped by a percent sign followed by two hexadecimal (uppercase) digits representing the value of the escaped character.

## `httpd.escape_xml (string)` ##

Escapes XML reserved characters in a string and returns the escaped string. More specifically, the following characters are escaped:

| **Character** | **Escape** |
|:--------------|:-----------|
| & | &quot; |
| `"` | `&quot;` |
| `<` | `&lt;` |
| `>` | `&gt;` |

## `httpd.escape_js (string)` ##

Escapes JavaScript string literal reserved characters in a string and returns the escaped string. More specifically, the following characters are escaped:

| **Character** | **Escape** |
|:--------------|:-----------|
| Backspace | `\b` |
| Horizontal tab | `\t` |
| Newline | `\n` |
| Vertical tab | `\v` |
| Form feed | `\f` |
| Carriage return | `\r` |
| Double quote | `\"` |
| Single quote | `\'` |
| Backslash | `\\` |

## `httpd.input` ##

A file descriptor for reading from the HTTP request body. The file descriptor behaves like a regular Lua file descriptor.

## `httpd.output` ##

A file descriptor for writing to the HTTP response body. The file descriptor behaves like a regular Lua file descriptor.

## `httpd.read (...)` ##

Equivalent to `httpd.input:read(...)`.

## `httpd.write (...)` ##

Equivalent to `httpd.output:write(...)`.

## `httpd.debug (message)` ##

Logs a message to the HTTP server log with DEBUG level.

## `httpd.notice (message)` ##

Logs a message to the HTTP server log with NOTICE level.

## `httpd.err (message)` ##

Logs a message to the HTTP server log with ERR level.

## `httpd.redirect (request, uri, status)` ##

Sets a  `Location` header with a value of `http://{host}{uri}` and returns `status`. The host is determined from the request. The idiom for using this function is

```
return httpd.redirect(request, "/next.lua", status)
```

where `status` is an appropriate 3xx status code.

## `httpd.dump (value)` ##

Writes `value` to the HTTP response body, formatted in HTML. The function handles Lua tables, APR tables and recursion.

## `httpd.match (path_info, ...)` ##

Matches `path_info` against the patterns following it and returns the captured values of the first matching pattern. Empty captures in the matching pattern, i.e. `()`, are returned as `nil`. The idiom for using this function is

```
a, b, c = httpd.match(request.path_info,
        "^/(%w+)/(%w+)/(%d+)$",
        "^/(%w+)/(%w+)$",
        "^/()(%w+)$")
```

where the return values are parameters extracted from the request URI using the supplied templates.