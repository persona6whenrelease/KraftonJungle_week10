-- MapManager.lua
-- 투명한 맵 관리자 액터에 부착되는 컴포넌트 스크립트

local RowGenerator = require("Game/RowGenerator")

local MapManager = {
    playerPawn = nil,

    RowDepth = RowGenerator.MapConfig.RowDepth,
    PreloadRows = 40,
    CurrentPlayerRow = 0,
    HighestGeneratedRow = -1,

    ActiveSpawners = {},
    ActiveVehicles = {}
}

local function find_player_pawn()
    if World ~= nil and World.GetPlayerController ~= nil then
        local controller = World.GetPlayerController(0)
        if controller then
            local pawn = controller:GetPossessedPawn()
            if pawn then
                MapManager.playerPawn = pawn
                return pawn
            end
        end
    end

    return nil
end

local function clear_runtime_tables()
    MapManager.CurrentPlayerRow = 0
    MapManager.HighestGeneratedRow = -1
    MapManager.ActiveSpawners = {}
    MapManager.ActiveVehicles = {}
end

local function configure_generator()
    if RowGenerator.ResetRuntimeState ~= nil then
        RowGenerator.ResetRuntimeState()
    end

    if RowGenerator.ConfigureRows ~= nil then
        RowGenerator.ConfigureRows()
        print("[MapManager] RowGenerator 초기 설정 성공")
    else
        print("[MapManager] RowGenerator.ConfigureRows is nil")
    end
end

local function generate_initial_rows()
    for i = 0, MapManager.PreloadRows do
        RowGenerator.GenerateRow(i)
        MapManager.HighestGeneratedRow = i
    end

    print("[MapManager] 초기 맵 생성 완료. 총 " .. tostring(MapManager.PreloadRows + 1) .. "칸")
end

local function reset_map_runtime(reason)
    print("[MapManager] reset_map_runtime begin reason=" .. tostring(reason or "unknown"))

    clear_runtime_tables()

    if ResetMap ~= nil then
        ResetMap()
    else
        print("[MapManager] ResetMap is nil")
    end

    configure_generator()
    generate_initial_rows()
    find_player_pawn()

    print("[MapManager] 맵 리셋 완료 reason=" .. tostring(reason or "unknown"))
end

-- 중요:
-- GameState.lua는 별도 Lua environment에서 실행됩니다.
-- 따라서 MapManager_Reset을 그냥 선언하면 GameState에서 보이지 않습니다.
-- 반드시 _G에 등록해야 합니다.
_G.MapManager_Reset = reset_map_runtime

_G.AddDynamicSpawner = function(rowIndex, prefab, speed, interval, dirY)
    table.insert(MapManager.ActiveSpawners, {
        RowIndex = rowIndex,
        Prefab = prefab,
        Speed = speed,
        Interval = interval,
        Timer = interval,
        DirY = dirY
    })
end

function BeginPlay()
    -- BeginPlay 때도 다시 등록합니다.
    -- Lua component environment가 바뀌거나 재로드되어도 GameState에서 접근 가능하게 하기 위함입니다.
    _G.MapManager_Reset = reset_map_runtime

    clear_runtime_tables()
    configure_generator()

    local pawn = find_player_pawn()
    if pawn then
        print("[MapManager] Pawn 연결 성공")
    else
        print("[MapManager] Pawn 연결 실패")
    end

    generate_initial_rows()
end

function Tick(deltaTime)
    if MapManager.playerPawn == nil then
        find_player_pawn()
        return
    end

    local playerForwardAxis = MapManager.playerPawn.Location.X
    local playerRowIndex = math.floor(playerForwardAxis / MapManager.RowDepth)

    if playerRowIndex > MapManager.CurrentPlayerRow then
        local step = playerRowIndex - MapManager.CurrentPlayerRow
        MapManager.CurrentPlayerRow = playerRowIndex

        for i = 1, step do
            MapManager.HighestGeneratedRow = MapManager.HighestGeneratedRow + 1
            RowGenerator.GenerateRow(MapManager.HighestGeneratedRow)
        end

        if MoveForward ~= nil then
            MoveForward(MapManager.CurrentPlayerRow)
        end

        local threshold = MapManager.CurrentPlayerRow - RowGenerator.MapConfig.KeepRowsBehind

        for i = #MapManager.ActiveSpawners, 1, -1 do
            if MapManager.ActiveSpawners[i].RowIndex < threshold then
                table.remove(MapManager.ActiveSpawners, i)
            end
        end
    end

    for _, spawner in ipairs(MapManager.ActiveSpawners) do
        spawner.Timer = spawner.Timer - deltaTime

        if spawner.Timer <= 0 then
            spawner.Timer = spawner.Interval

            if SpawnDynamicVehicle ~= nil then
                local vehicle = SpawnDynamicVehicle(
                    spawner.RowIndex,
                    spawner.Prefab,
                    spawner.Speed,
                    spawner.DirY
                )

                if vehicle then
                    -- 차량의 수명을 화면 너비 / 속도로 계산해서 배열에 담기
                    local lifeTime = 100000000.0 / spawner.Speed
                    table.insert(MapManager.ActiveVehicles, {
                        Vehicle = vehicle,
                        Timer = lifeTime
                    })
                end
            end
        end
    end

    for i = #MapManager.ActiveVehicles, 1, -1 do
        local vInfo = MapManager.ActiveVehicles[i]

        if vInfo ~= nil then
            vInfo.Timer = vInfo.Timer - deltaTime

            if vInfo.Timer <= 0 then
                if ReleaseActor ~= nil and vInfo.Vehicle ~= nil then
                    ReleaseActor(vInfo.Vehicle)
                end

                table.remove(MapManager.ActiveVehicles, i)
            end
        else
            table.remove(MapManager.ActiveVehicles, i)
        end
    end
end

function EndPlay()
    clear_runtime_tables()

    if _G.MapManager_Reset == reset_map_runtime then
        _G.MapManager_Reset = nil
    end

    MapManager.playerPawn = nil
end