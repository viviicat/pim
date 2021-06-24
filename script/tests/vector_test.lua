local Cvar = require("cvar")
local F4 = require("f4")

local vector_test = {}

function vector_test:start()
    self.sun_vector = Cvar.get("r_sun_col")
    self.another_vector = F4.new(50, 42, 53)
    Cvar.set("r_sun_col", self.sun_vector)
end

function vector_test:update()
  local total = 0
  local totalLen = 0

  for i=1, 100000 do
    total = total + self.sun_vector.x
    total = total + self.sun_vector.y
    total = total + self.sun_vector.z

    totalLen = totalLen + F4.lengthsq3(self.sun_vector)
  end
  
  Log.info(total, " ", totalLen, " ", self.sun_vector)
end

Game.start_update(vector_test);