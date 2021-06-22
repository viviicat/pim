local Cvar = require("cvar")

local vector_test = {}

function vector_test:start()
    self.sun_vector = Cvar.get("r_sun_col")
    Cvar.set("r_sun_col", self.sun_vector)
end

function vector_test:update()
  local total = 0
  local totalLen = 0

  for i=1, 10000 do
    total = total + self.sun_vector.x
    total = total + self.sun_vector.y
    total = total + self.sun_vector.z
    
    totalLen = totalLen + self.sun_vector.lengthsq3
  end
  
  Log.info(total, " ", totalLen)
end

Game.start_update(vector_test);