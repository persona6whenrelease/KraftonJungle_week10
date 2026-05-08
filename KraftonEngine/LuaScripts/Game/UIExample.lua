-- UIExample.lua
-- 원하는 게임 매니저 Lua 파일에서 Include 하거나 필요한 부분만 옮겨 쓰세요.

local score = 0
local best = 0
local coins = 0
local lane = 1
local combo = 1

local function RefreshHud()
    Game.UI.SetScore(score)
    Game.UI.SetBestScore(best)
    Game.UI.SetCoins(coins)
    Game.UI.SetLane(lane)
    Game.UI.SetCombo(combo)
end

function StartRun()
    score = 0
    coins = 0
    lane = 1
    combo = 1

    Game.UI.ResetRun()
    Game.UI.SetStatus("앞으로 이동해서 도로를 건너세요!")
    RefreshHud()
end

function EndRun()
    if score > best then
        best = score
    end

    Game.UI.SetBestScore(best)
    Game.UI.ShowGameOver(score, best)
end

function AddScore(amount)
    score = score + amount
    if score > best then
        best = score
    end
    RefreshHud()
end

function AddCoin(amount)
    coins = coins + amount
    RefreshHud()
end

function SetLane(value)
    lane = value
    RefreshHud()
end

function SetCombo(value)
    combo = value
    RefreshHud()
end

-- 이 파일은 예제용입니다. 실제 GameOver/Intro 버튼 처리는
-- IntroCameraController.lua의 OnUIEvent가 담당합니다.
-- 여기서 Game.UI.SetEventHandler를 등록하면 전역 UI 핸들러를 덮어써서
-- restart/start 이벤트가 MapManager_Reset까지 도달하지 않을 수 있습니다.
-- 필요하면 아래 함수들을 다른 게임 매니저에서 직접 호출하세요.
