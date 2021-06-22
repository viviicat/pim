local Cvar = require("cvar")

local vector_test = {}

function vector_test:update()
  local total = 0

  for i=1, 10000 do
    local sun_vector = Cvar.get("r_sun_col")
    total = total + sun_vector:x()
    total = total + sun_vector:y()
    total = total + sun_vector:z()
  end
  
end

Game.start_update(vector_test);