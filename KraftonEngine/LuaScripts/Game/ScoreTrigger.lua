local State = require("Game.GameState")

local bUsed = false

function BeginPlay()
    State.Configure()
    bUsed = false
end

function OnOverlap(otherActor)
    if bUsed then
        return
    end

    if State.IsPlaying ~= nil
        and State.IsPlaying()
        and State.IsPlayer(otherActor) then
        bUsed = true
        State.AddScore(1)
    
        if obj ~= nil then
            obj.Visible = false
        end
    end
end
