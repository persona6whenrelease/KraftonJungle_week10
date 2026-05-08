local RowGenerator = {}

local BIOME = {
    GRASS = 0,
    ROAD = 1,
    RAILWAY = 2
}

local BIOME_NAME = {
    [0] = "GRASS",
    [1] = "ROAD",
    [2] = "RAILWAY"
}

local PREFABS = {
    GRASSTILE = "Asset/Prefab/Grass.Prefab",
    ROADTILE = "Asset/Prefab/Road.Prefab",
    RAILWAYTILE = "Asset/Prefab/Road.Prefab",
    TRAFFIC_BARRIER_B = "Asset/Prefab/TrafficBarrierB.Prefab",
    TRAFFIC_BARRIER_A = "Asset/Prefab/TrafficBarrierA.Prefab",
    INVISIBLE_SIDE_WALL = "Asset/Prefab/InvisibleSideWall.Prefab",

    ROCK = "Asset/Prefab/Rock.Prefab",
    TREE1 = "Asset/Prefab/TreeA.Prefab",
    TREE2 = "Asset/Prefab/TreeB.Prefab",
    TREE3 = "Asset/Prefab/TreeC.Prefab",
    TREE4 = "Asset/Prefab/TreeD.Prefab",
    
    CARA = "Asset/Prefab/CarA.Prefab",
    CARB = "Asset/Prefab/CarB.Prefab",
    CARC = "Asset/Prefab/CarC.Prefab",
    CARD = "Asset/Prefab/CarD.Prefab",
    MINIBUS = "Asset/Prefab/MiniBus.Prefab",
    FIRECAR = "Asset/Prefab/FireCar.Prefab",
    POLICECAR = "Asset/Prefab/PoliceCar.Prefab",
    RACINGCAR = "Asset/Prefab/RacingCar.Prefab",

    TRAIN = "Asset/Prefab/BasicCube.Prefab" -- Train 프리팹이 없다면 대체
}

RowGenerator.MapConfig = {
    SlotCount = 15,
    SlotSize = 2.0,
    RowDepth = 2.0,
    KeepRowsBehind = 25,
    KeepRowsAhead = 25
}
RowGenerator.MapConfig.MaxSlotIndex = RowGenerator.MapConfig.SlotCount - 1

local LastSafeSlot = math.floor(RowGenerator.MapConfig.SlotCount / 2)

function RowGenerator.ResetRuntimeState()
    LastSafeSlot = math.floor(RowGenerator.MapConfig.SlotCount / 2)
end

-- 2. 가중치 테이블 설정
-- 지형의 가중치
local BiomeWeights = {
    { type = BIOME.GRASS,   weight = 40 },   -- 50%
    { type = BIOME.ROAD,    weight = 60 },   -- 35%
}

-- 장애물별 등장 가중치 설정 (합이 꼭 100일 필요는 없어)
local ObstacleWeights = {
    { prefab = PREFABS.ROCK,              weight = 25 },
    { prefab = PREFABS.TRAFFIC_BARRIER_A, weight = 15 },
    { prefab = PREFABS.TREE1,             weight = 15 },
    { prefab = PREFABS.TREE2,             weight = 15 },
    { prefab = PREFABS.TREE3,             weight = 15 },
    { prefab = PREFABS.TREE4,             weight = 15 },
}

-- 차량 등장 확률 (가중치)
local VehicleWeights = {
    { type = PREFABS.CARA,      weight = 5 },
    { type = PREFABS.CARB,      weight = 5 },
    { type = PREFABS.CARC,      weight = 5 },
    { type = PREFABS.CARD,      weight = 5 },
    { type = PREFABS.MINIBUS,   weight = 20 },
    { type = PREFABS.FIRECAR,   weight = 20 },
    { type = PREFABS.POLICECAR, weight = 20 },
    { type = PREFABS.RACINGCAR, weight = 20 },
}

local VehicleSpeedMultipliers = {
    [PREFABS.CARA] = 0.90,
    [PREFABS.CARB] = 0.97,
    [PREFABS.CARC] = 1.03,
    [PREFABS.CARD] = 1.10,
}


function RowGenerator.ConfigureRows()
    Game.Map.SetRowSize(RowGenerator.MapConfig.SlotCount, RowGenerator.MapConfig.SlotSize, RowGenerator.MapConfig.RowDepth)
    Game.Map.SetRowBufferCounts(RowGenerator.MapConfig.KeepRowsBehind, RowGenerator.MapConfig.KeepRowsAhead)

    -- ActorPool warmup 정책은 Crossy C++ RowManager가 담당합니다.

end

-- 가중치 기반 독립적 선택
local function ChooseWeighted(table)
    local totalWeight = 0
    for _, item in ipairs(table) do
        totalWeight = totalWeight + item.weight
    end

    local randWeight = math.random() * totalWeight
    local weightSum = 0

    for _, item in ipairs(table) do
        weightSum = weightSum + item.weight
        if randWeight <= weightSum then
            return item
        end
    end
    return table[1]
end

local function RandomRange(minValue, maxValue)
    return minValue + (math.random() * (maxValue - minValue))
end

local function SpawnGrassObstacle(rowIndex, slot, prefab)
    local offsetX = RandomRange(-0.2, 0.2)
    local offsetY = RandomRange(-0.2, 0.2)
    local yaw = RandomRange(-15.0, 15.0)
    Game.Map.SpawnStaticObstacle(rowIndex, slot, prefab, offsetX, offsetY, yaw)
end

-- 진행도(RowIndex)에 따른 장애물 확률 증가
function RowGenerator.GetObstacleChance(rowIndex)
    -- 기본 0.1(10%)에서 시작, RowIndex가 오를수록 증가. 최대 0.7(70%)까지만.
    return math.min(0.7, 0.1 + (rowIndex * 0.001))
end

function RowGenerator.GenerateRow(rowIndex)
    -- 1. 지형 결정 (Markov Chain)
    local biome = ChooseWeighted(BiomeWeights)
    local biomeType = biome.type
    Game.Map.SetRowBiome(rowIndex, biomeType)
    -- print("Biome : " .. (BIOME_NAME[biomeType] or tostring(biomeType)))

    Game.Map.SpawnStaticObstacle(rowIndex, -1, PREFABS.INVISIBLE_SIDE_WALL)
    Game.Map.SpawnStaticObstacle(rowIndex, RowGenerator.MapConfig.SlotCount, PREFABS.INVISIBLE_SIDE_WALL)

    if biomeType == BIOME.GRASS then
        Game.Map.SpawnStaticObstacle(rowIndex, math.floor(RowGenerator.MapConfig.SlotCount / 2), PREFABS.GRASSTILE)
        Game.Map.SpawnStaticObstacle(rowIndex, 0, PREFABS.TRAFFIC_BARRIER_B)
        Game.Map.SpawnStaticObstacle(rowIndex, RowGenerator.MapConfig.MaxSlotIndex, PREFABS.TRAFFIC_BARRIER_B)
    elseif biomeType == BIOME.ROAD then
        Game.Map.SpawnStaticObstacle(rowIndex, math.floor(RowGenerator.MapConfig.SlotCount / 2), PREFABS.ROADTILE)
    elseif biomeType == BIOME.RAILWAY then
        Game.Map.SpawnStaticObstacle(rowIndex, math.floor(RowGenerator.MapConfig.SlotCount / 2), PREFABS.RAILWAYTILE)
    end

    -- 2. 안전한 경로 계산 (-1 ~ 1 슬롯 이동)
    local nextSafeSlot = LastSafeSlot + math.random(-1, 1)
    nextSafeSlot = math.max(1, math.min(RowGenerator.MapConfig.MaxSlotIndex - 1, nextSafeSlot))
    

    if biomeType == BIOME.GRASS then
        local obstacleChance = RowGenerator.GetObstacleChance(rowIndex)
        
        for slot = 0, RowGenerator.MapConfig.MaxSlotIndex do
            if slot ~= nextSafeSlot and slot ~= 0 and slot ~= RowGenerator.MapConfig.MaxSlotIndex then -- 안전 구역이 아닌 곳만 장애물 스폰
                if math.random() < obstacleChance then
                    -- 가중치에 따라 확률적으로 장애물 프리팹을 선택
                    local obstacle = ChooseWeighted(ObstacleWeights)
                    
                    SpawnGrassObstacle(rowIndex, slot, obstacle.prefab)
                end
            end
        end

-- (GenerateRow 함수 내부의 ROAD 처리 부분)
    elseif biomeType == BIOME.ROAD then
        -- UE 축에 맞춰 좌우 이동 방향을 Y축으로 명명 (1: 오른쪽, -1: 왼쪽)
        local dirY = (math.random(0, 1) == 0) and -1 or 1
        
        -- 1. 이 차선에 등장할 차량 종류를 확률로 결정
        local selectedVehicle = ChooseWeighted(VehicleWeights)
        
        local speed = 0
        local interval = 0

        -- 2. 뽑힌 차량의 종류에 따라 속도와 스폰 주기를 다르게 세팅
        if selectedVehicle.type == PREFABS.RACINGCAR or selectedVehicle.type == PREFABS.FIRECAR then
            -- 스포츠카, 경찰차: 매우 빠른 속도, 짧은 간격
            speed = 15.0 + (rowIndex * 0.05)
            interval = math.max(4.0, 10.0 - (rowIndex * 0.015))

        elseif selectedVehicle.type == PREFABS.MINIBUS or selectedVehicle.type == PREFABS.POLICECAR then
            -- 버스, 소방차: 느린 속도, 넓은 간격
            speed = 1.0 + (rowIndex * 0.004)
            interval = math.max(4.0, 10.0 - (rowIndex * 0.03))

        else
            -- 승용차 (CAR A, B, C, D): 표준 속도, 표준 간격
            speed = 2.5 + (rowIndex * 0.016)
            interval = math.max(1.0, 10.0 - (rowIndex * 0.06))
        end

        speed = speed * (VehicleSpeedMultipliers[selectedVehicle.type] or 1.0)

        -- 3. 결정된 데이터로 해당 Row에 스포너 등록 (Lua 단에서 스케줄링)
        if _G.AddDynamicSpawner then
            _G.AddDynamicSpawner(rowIndex, selectedVehicle.type, speed, interval, dirY)
        end

    elseif biomeType == BIOME.RAILWAY then
        -- 기차는 매우 빠르고 간격이 긺
        local speed = 20.0 + (rowIndex * 0.2)
        local interval = math.max(3.0, 6.0 - (rowIndex * 0.02))
        local dirY = (math.random(0, 1) == 0) and -1 or 1

        if _G.AddDynamicSpawner then
            _G.AddDynamicSpawner(rowIndex, PREFABS.TRAIN, speed, interval, dirY)
        end
    end

    LastSafeSlot = nextSafeSlot -- 다음 Row를 위해 갱신
end

return RowGenerator
