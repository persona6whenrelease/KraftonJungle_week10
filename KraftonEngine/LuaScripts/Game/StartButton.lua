local State = require("Game.GameState")

function BeginPlay()
    State.Configure()
end

function OnOverlap(otherActor)
    if State.CanStartFromWorldButton ~= nil
        and State.CanStartFromWorldButton()
        and State.IsPlayer(otherActor) then
        if State.StartFreshRun ~= nil then
            State.StartFreshRun("WorldStartButton")
        else
            State.StartGame("WorldStartButton")
        end
    end
end