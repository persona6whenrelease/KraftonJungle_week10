local State = require("Game.GameState")

function BeginPlay()
    State.Configure()
end

function OnOverlap(otherActor)
    if State.IsGameOver ~= nil
        and State.IsGameOver()
        and State.IsPlayer(otherActor) then
        State.RestartRun()
    end
end