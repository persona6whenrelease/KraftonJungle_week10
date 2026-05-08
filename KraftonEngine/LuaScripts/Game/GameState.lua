local MODULE_NAME = "Game.GameState"
local Vec = FVector.new

local State = package.loaded[MODULE_NAME]
if type(State) ~= "table" then
    State = {}
    package.loaded[MODULE_NAME] = State
end

local DEFAULT_CONFIG = {
    PlayerName = "Player",
    HUDTextName = "HUD_Text",
    CreditsTextName = "Credits_Text",
    StartButtonName = "StartButton",
    RestartButtonName = "RestartButton",
    DefeatY = -1000.0,
    GameOverLocation = Vec(0.0, 0.501545, -2000.0),
    StartLocation = nil,

    -- X축으로 ScoreUnit만큼 전진할 때마다 1점
    ScoreAxis = "X",
    ScoreUnit = 2.0,

    AutoStart = false,
    Creators = {
        "KraftonEngine Team 7"
    }
}

State.Config = State.Config or {}
State.Mode = State.Mode or "Boot"
State.Score = State.Score or 0
State.BestScore = State.BestScore or 0
State.TopScores = State.TopScores or {}
State.StartScoreRow = State.StartScoreRow or 0
State.Elapsed = State.Elapsed or 0.0
State.bInitialized = State.bInitialized or false
State.bUIReady = State.bUIReady or false
State.bBestScoreLoaded = State.bBestScoreLoaded or false
State.bCreditsPrinted = State.bCreditsPrinted or false
State.CachedPlayer = State.CachedPlayer or nil
State.CachedHUDText = State.CachedHUDText or nil
State.CachedCreditsText = State.CachedCreditsText or nil

local function copy_defaults(target, defaults)
    for key, value in pairs(defaults) do
        if target[key] == nil then
            if type(value) == "table" then
                local copied = {}
                for i, item in ipairs(value) do
                    copied[i] = item
                end
                target[key] = copied
            else
                target[key] = value
            end
        end
    end
end

local function is_valid(handle)
    return handle ~= nil and handle.IsValid ~= nil and handle:IsValid()
end

local function vec_x(v)
    if v == nil then return 0.0 end
    return v.X or v.x or 0.0
end

local function vec_y(v)
    if v == nil then return 0.0 end
    return v.Y or v.y or 0.0
end

local function vec_z(v)
    if v == nil then return 0.0 end
    return v.Z or v.z or 0.0
end

local function make_vector(x, y, z)
    return FVector.new(x or 0.0, y or 0.0, z or 0.0)
end

local function clone_vector(v)
    if v == nil then
        return nil
    end
    return make_vector(vec_x(v), vec_y(v), vec_z(v))
end

local function find_actor(name)
    if name == nil or name == "" or World == nil or World.FindActorByName == nil then
        return nil
    end
    return World.FindActorByName(name)
end

local function get_possessed_player_actor()
    if World == nil or World.GetPlayerController == nil then
        return nil
    end

    local controller = World.GetPlayerController(0)
    if not is_valid(controller) or controller.GetPossessedActor == nil then
        return nil
    end

    return controller:GetPossessedActor()
end

local function get_player_controller()
    if World == nil then
        return nil
    end

    local pc = nil
    if World.GetPlayerController ~= nil then
        pc = World.GetPlayerController(0)
    end

    if not is_valid(pc) and World.GetOrCreatePlayerController ~= nil then
        pc = World.GetOrCreatePlayerController()
    end

    if is_valid(pc) then
        return pc
    end

    return nil
end

local function get_possessed_player()
    local pc = get_player_controller()
    if not is_valid(pc) then
        return nil
    end

    if pc.GetPossessedPawn ~= nil then
        local pawn = pc:GetPossessedPawn()
        if is_valid(pawn) then
            return pawn
        end
    end

    if pc.GetPossessedActor ~= nil then
        local actor = pc:GetPossessedActor()
        if is_valid(actor) then
            return actor
        end
    end

    return nil
end

local function resolve_player()
    local player = find_actor(State.Config.PlayerName)
    if is_valid(player) then
        return player
    end

    player = get_possessed_player()
    if is_valid(player) then
        return player
    end

    return nil
end

local function get_actor_name(actor)
    if is_valid(actor) and actor.Name ~= nil then
        return actor.Name
    end
    return ""
end

local function same_actor(a, b)
    if not is_valid(a) or not is_valid(b) then
        return false
    end
    if a.UUID ~= nil and b.UUID ~= nil then
        return a.UUID == b.UUID
    end
    return get_actor_name(a) ~= "" and get_actor_name(a) == get_actor_name(b)
end

local function set_visible(actor, visible)
    if is_valid(actor) then
        actor.Visible = visible
    end
end

local function get_or_create_actor(name, location)
    local actor = find_actor(name)
    if is_valid(actor) then
        return actor
    end

    actor = SpawnActor("AActor", location or make_vector(0.0, 0.0, 0.0))
    if is_valid(actor) then
        actor.Name = name
    end
    return actor
end

local function get_text_component(actor)
    if not is_valid(actor) then
        return nil
    end
    local component = actor:GetOrAddComponent("TextRender")
    if component ~= nil and component.IsValid ~= nil and component:IsValid() then
        return component
    end
    return nil
end

local function set_text(actor, text, visible)
    local component = get_text_component(actor)
    if component == nil then
        return false
    end

    component:SetProperty("Text", text)
    component:SetProperty("Visible", visible ~= false)
    return true
end

local function format_time(seconds)
    seconds = math.max(0.0, seconds or 0.0)
    local whole = math.floor(seconds)
    local minutes = math.floor(whole / 60)
    local remain = whole - minutes * 60
    return string.format("%02d:%02d", minutes, remain)
end

local function get_axis_value(location, axis)
    if location == nil then
        return nil
    end

    axis = axis or "X"

    if axis == "X" or axis == "x" then
        return vec_x(location)
    elseif axis == "Y" or axis == "y" then
        return vec_y(location)
    elseif axis == "Z" or axis == "z" then
        return vec_z(location)
    end

    return vec_x(location)
end

local function get_score_row_from_location(location)
    local axisValue = get_axis_value(location, State.Config.ScoreAxis or "X")
    if axisValue == nil then
        return nil
    end

    local unit = State.Config.ScoreUnit or 1.0
    if unit <= 0.0 then
        unit = 1.0
    end

    return math.floor(axisValue / unit)
end

local function hide_game_over_ui()
    if UI ~= nil and UI.HideGameOver ~= nil then
        UI.HideGameOver()
    end
end

local function normalize_top_scores(scores)
    local normalized = {}

    if type(scores) == "table" then
        for _, value in ipairs(scores) do
            local score = math.max(0, math.floor(value or 0))
            if score > 0 then
                normalized[#normalized + 1] = score
            end
        end
    end

    table.sort(normalized, function(a, b)
        return a > b
    end)

    while #normalized > 5 do
        table.remove(normalized)
    end

    return normalized
end

local function sync_best_score_from_top_scores()
    State.BestScore = math.max(State.BestScore or 0, State.TopScores[1] or 0)
end

local function format_top_scores()
    if #State.TopScores == 0 then
        return "기록 없음"
    end

    local lines = {}
    for i = 1, 5 do
        local score = State.TopScores[i]
        if score ~= nil then
            lines[#lines + 1] = tostring(i) .. ". " .. tostring(score)
        else
            lines[#lines + 1] = tostring(i) .. ". -"
        end
    end
    return table.concat(lines, "<br/>")
end

local function load_top_scores()
    if State.bBestScoreLoaded then
        return
    end

    State.bBestScoreLoaded = true

    if SaveGame ~= nil and SaveGame.LoadTopScores ~= nil then
        State.TopScores = normalize_top_scores(SaveGame.LoadTopScores())
        sync_best_score_from_top_scores()
        print("[Score] Loaded top scores. Best = " .. tostring(State.BestScore or 0))
    elseif SaveGame ~= nil and SaveGame.LoadBestScore ~= nil then
        local loaded = math.max(0, math.floor(SaveGame.LoadBestScore() or 0))
        if loaded > 0 then
            State.TopScores = { loaded }
        end
        sync_best_score_from_top_scores()
        print("[Score] Loaded legacy best score = " .. tostring(State.BestScore or 0))
    else
        print("[Score] SaveGame binding is not available. Scores will not persist.")
    end
end

local function save_top_scores()
    if SaveGame ~= nil and SaveGame.SaveTopScores ~= nil then
        SaveGame.SaveTopScores(State.TopScores or {})
    elseif SaveGame ~= nil and SaveGame.SaveBestScore ~= nil then
        SaveGame.SaveBestScore(State.BestScore or 0)
    end
end

local function record_final_score(score)
    local finalScore = math.max(0, math.floor(score or 0))
    if finalScore > 0 then
        State.TopScores[#State.TopScores + 1] = finalScore
    end

    State.TopScores = normalize_top_scores(State.TopScores)
    sync_best_score_from_top_scores()
    save_top_scores()
end

local function push_score_to_ui()
    if UI == nil then
        return
    end

    if UI.SetScore ~= nil then
        UI.SetScore(State.Score or 0)
    end

    if UI.SetBestScore ~= nil then
        UI.SetBestScore(State.BestScore or 0)
    end

    if UI.SetTopScoresText ~= nil then
        UI.SetTopScoresText(format_top_scores())
    end
end

local function build_credit_text(reason)
    local lines = {}
    lines[#lines + 1] = "GAME OVER"
    if reason ~= nil and reason ~= "" then
        lines[#lines + 1] = "Reason: " .. tostring(reason)
    end
    lines[#lines + 1] = "Score: " .. tostring(State.Score or 0)
    lines[#lines + 1] = "Best: " .. tostring(State.BestScore or 0)
    lines[#lines + 1] = "Time: " .. format_time(State.Elapsed or 0.0)
    lines[#lines + 1] = ""
    lines[#lines + 1] = "Top 5"
    if #State.TopScores == 0 then
        lines[#lines + 1] = "- 기록 없음"
    else
        for i = 1, math.min(5, #State.TopScores) do
            lines[#lines + 1] = tostring(i) .. ". " .. tostring(State.TopScores[i])
        end
    end
    lines[#lines + 1] = ""
    lines[#lines + 1] = "Credits"

    local creators = State.Config.Creators or DEFAULT_CONFIG.Creators
    for _, name in ipairs(creators) do
        lines[#lines + 1] = "- " .. tostring(name)
    end

    lines[#lines + 1] = ""
    lines[#lines + 1] = "Press R or Enter to restart"
    return table.concat(lines, "\n")
end

function State.Configure(config)
    State.Config = State.Config or {}
    copy_defaults(State.Config, DEFAULT_CONFIG)

    if type(config) == "table" then
        for key, value in pairs(config) do
            State.Config[key] = value
        end
    end

    copy_defaults(State.Config, DEFAULT_CONFIG)
end

function State.RefreshReferences()
    copy_defaults(State.Config, DEFAULT_CONFIG)

    State.CachedPlayer = find_actor(State.Config.PlayerName)
    if not is_valid(State.CachedPlayer) then
        State.CachedPlayer = get_possessed_player_actor()
    end
    State.CachedHUDText = get_or_create_actor(State.Config.HUDTextName, Vec(0.0, 0.0, 180.0))
    State.CachedCreditsText = get_or_create_actor(State.Config.CreditsTextName, Vec(0.0, 0.0, 230.0))

    if is_valid(State.CachedCreditsText) then
        set_visible(State.CachedCreditsText, false)
    end
end

function State.GetPlayer()
    if not is_valid(State.CachedPlayer) then
        State.CachedPlayer = find_actor(State.Config.PlayerName)
    end

    if not is_valid(State.CachedPlayer) then
        State.CachedPlayer = get_possessed_player_actor()
    end
    return State.CachedPlayer
end

function State.IsPlayer(actor)
    if not is_valid(actor) then
        return false
    end

    local player = State.GetPlayer()
    if same_actor(actor, player) then
        return true
    end

    return get_actor_name(actor) == State.Config.PlayerName
end

function State.SetPlayerMovementEnabled(enabled)
    local player = State.GetPlayer()
    if not is_valid(player) then
        print("[GameState] SetPlayerMovementEnabled: player not found")
        return
    end

    local hop = player.HopMovement
    if hop ~= nil and hop.IsValid ~= nil and hop:IsValid() then
        hop.Simulating = enabled

        if not enabled then
            hop:ClearMovementInput()
            hop:StopMovementImmediately()
        end

        return
    end

    local movement = nil
    if player.GetComponent ~= nil then
        movement = player:GetComponent("Movement")
    end

    if movement ~= nil and movement.IsValid ~= nil and movement:IsValid() then
        movement.Active = enabled
        movement.TickEnabled = enabled
    end
end

function State.SetMenuObjectsVisible(visible)
    set_visible(find_actor(State.Config.StartButtonName), visible)
    set_visible(find_actor(State.Config.RestartButtonName), visible)
end

function State.IsIntro()
    return State.Mode == "Intro" or State.Mode == "Ready" or State.Mode == "Boot"
end

function State.IsPlaying()
    return State.Mode == "Playing"
end

function State.IsGameOver()
    return State.Mode == "GameOver"
end

function State.CanStartFromWorldButton()
    -- 시작은 RML Intro UI / IntroCameraController가 담당합니다.
    -- 3D StartButton overlap으로 자동 시작되는 것을 막습니다.
    return false
end

function State.UpdateHUD()
    if State.Mode == "Intro" or State.Mode == "Ready" or State.Mode == "Boot" then
        set_visible(State.CachedHUDText, false)
        return
    end
    local text =
        "State: " .. tostring(State.Mode) ..
        "\nScore: " .. tostring(State.Score or 0) ..
        "\nBest: " .. tostring(State.BestScore or 0) ..
        "\nTime: " .. format_time(State.Elapsed or 0.0)

    if State.Mode == "Ready" then
        text = text .. "\nPress Start"
    elseif State.Mode == "GameOver" then
        text = text .. "\nPress R or touch RestartButton"
    end

    if not set_text(State.CachedHUDText, text, true) then
        print(text)
    end
end

function State.ResetToIntro()
    State.Mode = "Intro"
    State.Score = 0
    State.StartScoreRow = 0
    State.Elapsed = 0.0
    State.bCreditsPrinted = false

    hide_game_over_ui()

    State.SetPlayerMovementEnabled(false)

    -- RML Intro UI를 쓰므로 월드 Start/Restart 버튼은 숨깁니다.
    State.SetMenuObjectsVisible(false)

    set_visible(State.CachedHUDText, false)
    set_visible(State.CachedCreditsText, false)

    if UI ~= nil then
        if UI.ShowHUD ~= nil then
            UI.ShowHUD(false)
        end

        if UI.ShowIntro ~= nil then
            UI.ShowIntro(true)
        end
    end

    push_score_to_ui()
end

function State.BeginPlay()
    State.bUIReady = false
    State.Configure(State.Config)
    State.RefreshReferences()
    load_top_scores()

    if State.Config.StartLocation == nil then
        local player = State.GetPlayer()
        if is_valid(player) then
            State.Config.StartLocation = clone_vector(player.Location)
            print(
                "[GameState] Captured StartLocation X=" .. tostring(vec_x(player.Location)) ..
                " Y=" .. tostring(vec_y(player.Location)) ..
                " Z=" .. tostring(vec_z(player.Location))
            )
        else
            print("[GameState] BeginPlay: player not found yet")
        end
    end

    if State.Config.AutoStart then
        State.StartGame("AutoStart")
    else
        State.ResetToIntro()
    end

    State.bUIReady = true
    State.bInitialized = true
end

function State.StartGame(reason)
    if State.Mode == "Playing" then
        return
    end

    State.Configure(State.Config)
    State.RefreshReferences()

    State.Mode = "Playing"
    State.Score = 0
    State.StartScoreRow = 0
    State.Elapsed = 0.0
    State.bCreditsPrinted = false

    hide_game_over_ui()

    if UI ~= nil then
        if UI.ResetRun ~= nil then
            UI.ResetRun()
        end

        if UI.ShowHUD ~= nil then
            UI.ShowHUD(true)
        end
    end

    local player = State.GetPlayer()
    if is_valid(player) and State.Config.StartLocation ~= nil then
        player.Location = FVector.new(0.0, 0.501545, -0.251383)
    end

    if is_valid(player) then
        State.StartScoreRow = get_score_row_from_location(player.Location) or 0
        print(
            "[GameState] StartGame player X=" .. tostring(vec_x(player.Location)) ..
            " StartScoreRow=" .. tostring(State.StartScoreRow)
        )
    else
        print("[GameState] StartGame: player not found")
    end

    State.SetPlayerMovementEnabled(true)
    State.SetMenuObjectsVisible(false)
    set_visible(State.CachedCreditsText, false)
    push_score_to_ui()
    State.UpdateHUD()
    
    print("[Game] Start reason=" .. tostring(reason or "unknown"))
end

function State.ReturnToStartScreen(reason)
    if State.Mode ~= "Playing" then
        return
    end

    State.Mode = "Ready"
    State.Score = 0
    State.Elapsed = 0.0
    State.bCreditsPrinted = false

    local player = State.GetPlayer()
    if is_valid(player) and State.Config.StartLocation ~= nil then
        player.Location = FVector.new(0.0, 0.501545, -0.251383) 
    end

    State.SetPlayerMovementEnabled(false)
    State.SetMenuObjectsVisible(true)
    set_visible(State.CachedCreditsText, false)
    State.UpdateHUD()

    if reason ~= nil and reason ~= "" then
        print("[Game] ReturnToStartScreen: " .. tostring(reason))
    end
end

function State.StartFreshRun(reason)
    local resetReason = reason or "FreshRun"

    local resetFunc = nil

    if _G ~= nil and _G.MapManager_Reset ~= nil then
        resetFunc = _G.MapManager_Reset
    elseif MapManager_Reset ~= nil then
        resetFunc = MapManager_Reset
    end

    if resetFunc ~= nil then
        resetFunc(resetReason)
    else
        print("[GameState] StartFreshRun: _G.MapManager_Reset is nil")
    end

    State.Mode = "Ready"
    State.StartGame(resetReason)
end

function State.RestartRun()
    State.StartFreshRun("Restart")
end

function State.RestartLevel()
    if Game ~= nil and Game.Restart ~= nil then
        Game.Restart()
        return
    end
    State.RestartRun()
end

function State.GameOver(reason)
    if State.Mode == "GameOver" then
        return
    end

    State.Mode = "GameOver"

    local player = State.GetPlayer()
    if is_valid(player) and State.Config.GameOverLocation ~= nil then
        player.Location = clone_vector(State.Config.GameOverLocation)
    end

    State.SetPlayerMovementEnabled(false)
    State.SetMenuObjectsVisible(true)
    record_final_score(State.Score or 0)

    local credits = build_credit_text(reason or "Defeat")
    if not set_text(State.CachedCreditsText, credits, true) then
        print(credits)
    end

    State.UpdateHUD()
    push_score_to_ui()

    if UI ~= nil and UI.ShowGameOver ~= nil then
        UI.ShowGameOver(State.Score or 0, State.BestScore or State.Score or 0)
    end

    if not State.bCreditsPrinted then
        print(credits)
        State.bCreditsPrinted = true
    end
end

function State.SetScore(value)
    local nextScore = math.max(0, math.floor(value or 0))

    if nextScore == State.Score then
        return
    end

    State.Score = nextScore

    if State.Score > (State.BestScore or 0) then
        State.BestScore = State.Score
    end

    push_score_to_ui()
    State.UpdateHUD()
end

function State.AddScore(amount)
    State.SetScore((State.Score or 0) + (amount or 1))
end

function State.UpdateDistanceScore()
    local player = State.GetPlayer()
    if not is_valid(player) then
        print("[Score] player not found")
        return
    end

    local location = player.Location
    local currentRow = get_score_row_from_location(location)
    if currentRow == nil then
        return
    end

    local distanceScore = math.max(0, currentRow - (State.StartScoreRow or 0))

    -- 뒤로 움직여도 점수는 떨어지지 않고, 가장 멀리 간 지점 기준으로만 증가
    if distanceScore > (State.Score or 0) then
        State.SetScore(distanceScore)
        print(
            "[Score] X=" .. tostring(vec_x(location)) ..
            " Row=" .. tostring(currentRow) ..
            " StartRow=" .. tostring(State.StartScoreRow or 0) ..
            " Score=" .. tostring(State.Score or 0) ..
            " Best=" .. tostring(State.BestScore or 0)
        )
    end
end

function State.Tick(deltaTime)
    if not State.bInitialized then
        State.BeginPlay()
        return
    end

    if not State.bUIReady and UI ~= nil then
        if UI.ShowHUD ~= nil then
            UI.ShowHUD(State.Mode == "Playing")
        end
        if UI.ShowIntro ~= nil then
            UI.ShowIntro(State.Mode ~= "Playing")
        end
        push_score_to_ui()
        State.bUIReady = true
    end

    if State.Mode == "Ready" then
        -- 시작은 IntroCameraController.lua가 UI start 이벤트를 받아 처리합니다.
        -- 여기서 State.StartGame()을 직접 부르면 시작 카메라 애니메이션을 우회합니다.
        return
    end

    if State.Mode == "GameOver" then
        if Input ~= nil and Input.GetKeyDown ~= nil then
            if Input.GetKeyDown("R") or Input.GetKeyDown("ENTER") then
                State.RestartRun()
            end
        end
        return
    end

    if State.Mode ~= "Playing" then
        return
    end

    State.Elapsed = (State.Elapsed or 0.0) + (deltaTime or 0.0)

    State.UpdateDistanceScore()

    local player = State.GetPlayer()
    if is_valid(player) then
        local location = player.Location

        if location ~= nil and vec_z(location) < State.Config.DefeatY then
            State.GameOver("Fell out of stage")
            return
        end
    end

    State.UpdateHUD()
end

return State
