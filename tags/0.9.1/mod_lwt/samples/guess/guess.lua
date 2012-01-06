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
