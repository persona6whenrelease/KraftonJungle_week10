local State = require("Game.GameState")
print("[GM] GameManager.lua loaded")

local Config = {
    PlayerName = "Player",
    HUDTextName = "HUD_Text",
    CreditsTextName = "Credits_Text",
    StartButtonName = "StartButton",
    RestartButtonName = "RestartButton",
    DefeatY = -1000.0,
    StartLocation = FVector.new(-0.0, 0.501545, -0.251383),
    AutoStart = false,
    Creators = {
        "KraftonEngine Team 7",
        "Programmer: replace this name",
        "Designer: replace this name"
    }
}

function BeginPlay()
    State.Configure(Config)
    State.BeginPlay()
end

function Tick(deltaTime)
    State.Tick(deltaTime)
end

_G.OnGameEvent = function(eventName, instigator)
    print("[GameManager] OnGameEvent triggered: " .. tostring(eventName))
    if eventName == "Defeat" then
        State.GameOver("Defeat")
    end
end

function EndPlay()
    print("[GameManager] EndPlay")
end
