__AUTHOR__ = "Dimaxa"
__TITLE__  = "One Hit per blob for 2x2 (like beach volley)"

lasthit = NO_PLAYER

-- function OnBallHitsPlayer
--		IMPLEMENTEDBY rules.lua
--		called when a valid collision between a player and the ball happens.
--		params: player - the player that hit the ball
--		return: none
function OnBallHitsPlayer(player)
	if touches(player) > 3 then
		lasthit = NO_PLAYER
		mistake(player, opponent(player), 1)
		return
	end

	if player == lasthit then
		lasthit = NO_PLAYER		
		mistake(player, opponent(player), 1)
	else
		lasthit = player
	end		
end

-- function OnBallHitsWall
--		IMPLEMENTEDBY rules.lua
--		called when a valid collision between the ball and a wall happens.
--		params: player - the player on whos side the ball hit a wall
--		return: none
function OnBallHitsWall(player)
	lasthit = NO_PLAYER
end

-- function OnBallHitsNet
--		IMPLEMENTEDBY rules.lua
--		called when a valid collision between the ball and a net happens.
--		params: player - the player on whos side the ball hits a net (NO_PLAYER for net top)
--		return: none
function OnBallHitsNet(player)
	lasthit = NO_PLAYER
end

-- function OnBallHitsGround
--		IMPLEMENTEDBY rules.lua
--		called when the ball hits the ground.
--		params: player - the player on whos side the ball hits the ground
--		return: none
function OnBallHitsGround(player)
	lasthit = NO_PLAYER
	mistake(player, opponent(player), 1)
end