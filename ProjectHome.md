LWT (Lua Web Tools) allows you to develop web applications in Lua, and run them directly in the Apache HTTP server. The core functionality of LWT is provided by an Apache module, mod\_lwt. In addition, LWT provides optional Lua modules for accessing databases and caches.



# Features #

  * **Apache module**. LWT provides an Apache HTTP server module that handles requests by invoking Lua scripts. The module provides the core functionality for creating web applications.
  * **Template engine.** LWT provides a template engine that blends Lua with HTML/XML. The engine supports substitutions and logic in a way that is natural to both Lua and HTML/XML. This makes it easy to develop web applications with a clean separation of UI logic in Lua and layout in HTML.
  * **Full request control.** LWT provides full control over web requests, including request status, content type, input and output. This faciliates the creation of JSON web services, and other uses of HTTP.
  * **File uploads.** LWT supports HTTP file uploads from a web browser.
  * **WSAPI.** LWT supports the WSAPI standard, and can be used to run WSAPI applications directly in the Apache HTTP server.
  * **Databases.** The IS module supports the access to information system in an SQL injection safe way. Currently, the module supports MySQL, Sybase, SQL Server and SQLite.
  * **Caching.** The cache module supports the use of caches, currently memcached.

The IS and cache modules are designed to work nicely along mod\_lwt, but are optional and independent Lua modules.

# Design Goals #

LWT is built on the following design goals:

  * **Direct Apache Integration.** LWT integrates Lua directly into the Apache HTTP Server.
  * **Scalability.** LWT is stateless, and creates a new Lua state for each request. This ensures high scalability on the web tier. If state management is required, this can be done in the way most suitable to the respective application.
  * **Simplicity.** LWT is compact, and will remain this way. If additional features can be reasonably implemented in independent Lua modules, such features are not going to make it into LWT.

# Example #

The following Lua script and HTML template implement a simple number guessing game. Head to the [wiki](http://code.google.com/p/lua-web-tools/wiki/TableOfContents) for more formal documentation.

The Lua script:

```
require "httpd"
require "cache"
require "cache.memcached"

-- Configure cache
local CACHE = cache.configure({ driver = cache.memcached })

-- Get the request and arguments
request, args = ...

-- Get game ID
game_id = tonumber(args.game_id) or CACHE:inc("game_id", 1, 1)

-- Get or create game state
local game_key = string.format("game-%d", game_id)
game = CACHE:get(game_key) or { }

-- Process request
if args.guess and game.number then
        local guess = tonumber(args.guess)
        if guess then
                if guess < game.number then
                        outcome = "low"
                elseif guess > game.number then
                        outcome = "high"
                else
                        outcome = "found"
                end
                table.insert(game.history, { guess = guess, outcome = outcome })
        end
elseif args.new or not game.number then
        game.number = (os.time() * 65599) % 1000 + 1
        game.history = { }
end

-- Store game state
CACHE:set(game_key, game, 3600)

-- Render
httpd.set_content_type("text/html; charset=utf-8");
httpd.write_template(request.filedir .. "guess.html")
```

The HTML template:

```
<!DOCTYPE HTML>
<html>
<head>
        <meta charset="UTF-8" />
        <title>Guess the Number</title>
</head>

<body onload="document.getElementById('guess').focus();">
        <h1>Guess the Number</h1>

        <l:if cond="#game.history > 0">
        <h2>Guess History</h2>
        <ul>
                <l:for names="_, entry" in="ipairs(game.history)">
                <li>${entry.guess} &ndash; ${entry.outcome == "low" and "too low" or entry.outcome == "high" and "too high" or "found"}</li>
                </l:for>
        </ul>
        </l:if>

        <form method="post">
        <input type="hidden" name="game_id" value="${game_id}" />
        <l:if cond="outcome ~= 'found'">
        <h2>Make a Guess</h2>
        <input id="guess" type="text" name="guess" size="4" /> (1 &ndash; 1000) <input type="submit" value="Guess" />
        <l:else />
        <h2>Congratulations</h2>
        <p>You found the number with ${#game.history} ${#game.history ~= 1 and "guesses" or "guess"}.</p>
        <input type="submit" name="new" value="New Game" />
        </l:if>
        </form>
</body>

</html>
```

# Requirements #

LWT 0.9 is based on Lua 5.1/5.2 and the Apache HTTP Server 2.2.

# License #

LWT is licensed under the MIT license which at the time of this writing is the same license as the one of Lua.