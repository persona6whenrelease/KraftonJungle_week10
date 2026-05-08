local State = require("Game.GameState")

function BeginPlay()
    State.Configure()
end

function OnOverlap(otherActor)
    if State.IsPlaying ~= nil
        and State.IsPlaying()
        and State.IsPlayer(otherActor) then
        State.IsDying = true
        State.GameOver("Touched defeat zone")
    end
end