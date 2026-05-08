-- Player.lua
-- Pawn 액터에 LuaScriptComponent로 붙이는 기준입니다.
-- 목표 구조:
--   owner Pawn
--   PlayerController가 owner Pawn Possess
--   별도 CameraActor / CameraComponent를 활성 카메라로 사용
--   카메라는 owner Pawn을 Follow Target으로 사용
--   이동 입력은 Lua에서 받아 HopMovementComponent에 넣음
--
-- 중요:
--   여기서는 ControllerInputComponent의 MoveSpeed / LookSensitivity를 0으로 만들지 않습니다.
--   C++ native controller input을 끄는 것은 GameViewportClient 쪽에서 처리해야 합니다.

local Vec = FVector.new
local Rot = FRotator.new
local State = require("Game.GameState")

local DEBUG = true

local CONFIG = {
    Controller = {
        Location = Vec(0.680827, 3.482542, -0.500000),
        ControlRotation = Rot(0.0, 0.0, 0.0)
    },

    ControllerInput = {
        MoveSpeed = 0.0,
        SprintMultiplier = 2.5,
        LookSensitivity = 0.08,
        MinPitch = -89.0,
        MaxPitch = 89.0,

        -- 카메라 전환 순간 마우스 델타가 크게 튀는 것 방지
        MaxMouseDeltaPerFrame = 80.0,
        CameraSwitchLookGuardTime = 0.08,

        -- Scene JSON: "Movement Basis" : 1
        -- 0 = World, 1 = ControlRotation, 2 = ViewCamera
        MovementBasis = 1
    },

    HopMovement = {
        ReceiveControllerInput = false,
        InitialSpeed = 8.0,
        MaxSpeed = 16.0,
        HopCoefficient = 1.0,
        Acceleration = 2048.0,
        BrakingDeceleration = 4096.0,
        HopHeight = 0.150000,
        HopFrequency = 6.0,
        HopOnlyWhenMoving = true,
        ResetHopWhenIdle = true,
        Simulating = true,
        ControllerInputPriority = 10,
        UpdatedComponent = "Root",
        VisualHopComponent = "Root/0"
    },

    PawnMovement = {
        ControllerInputPriority = 0
    },

    Orientation = {
        -- 0 = None
        -- 1 = ControlRotationYaw
        -- 2 = MovementInputDirection
        -- 3 = MovementVelocityDirection
        -- 4 = MovementDirectionWithControlFallback
        -- 5 = CustomWorldDirection

        -- 추천: 실제 이동 속도 방향을 바라봅니다.
        FacingMode = 1,

        RotationSpeed = 720.0,
        YawOnly = true,
        CustomFacingDirection = Vec(1.0, 0.0, 0.0)
    },

    Camera = {
        Location = Vec(0.0, 0.0, 0.0),
        Rotation = Rot(0.0, 0.0, 0.0),

        AspectRatio = 1.2222,
        FOVRadians = 1.77777,
        NearZ = 0.1,
        FarZ = 1000.0,
        Orthographic = false,
        OrthoWidth = 10.0,

        -- Scene JSON: "View Mode" : 2
        -- 0 = Static, 1 = FirstPerson, 2 = ThirdPerson, 3 = OrthographicFollow, 4 = Custom
        ViewMode = 2,

        FollowOffset = Vec(0.0, 0.0, 0.0),
        ViewOffset = Vec(-5.0, 5.0, 5.0),

        EyeHeight = 0.5,
        BackDistance = 2.0,
        Height = 1.0,
        SideOffset = 0.0,

        FirstPersonUseControlRotation = true,
        FollowSubjectAuto = true,
        UseControlRotationYaw = true,
        UseTargetForward = true,

        EnableLookAhead = false,
        LookAheadDistance = 1.0,
        LookAheadLagSpeed = 8.0,

        StabilizeVerticalInOrthographic = true,
        VerticalDeadZone = 0.4,
        VerticalFollowStrength = 0.15,
        VerticalLagSpeed = 2.0,

        EnableSmoothing = true,
        LocationLagSpeed = 12.0,
        RotationLagSpeed = 12.0,

        FOVLagSpeed = 10.0,
        OrthoWidthLagSpeed = 10.0,

        BlendTime = 0.35,
        BlendFunction = 3,
        ProjectionSwitchMode = 1,
        BlendLocation = true,
        BlendRotation = true,

        BlendFOV = true,
        BlendOrthoWidth = true,
        BlendNearFar = false
    }
}

local Player = {
    prevGuiMouse = nil,
    initialized = false,

    ownerObject = nil,
    pawn = nil,

    controller = nil,
    controllerInput = nil,

    cameraActor = nil,
    camera = nil,

    hopMovement = nil,
    pawnMovementComponent = nil,
    orientation = nil,

    parryComp = nil,

    frame = 0,

    yaw = 0.0,
    pitch = 0.0,

    lastInputSignature = "",
    printedMove = false,

    -- Dash 에지 판정용 (직전 프레임의 MOUSE2 상태)
    prevMouse2 = false,

    -- Parry 에지 판정용 (직전 프레임의 MOUSE1 상태)
    prevMouse1 = false,
    
    -- 카메라 전환 직후 마우스 델타 튐 방지용
    cameraSwitchGuardTime = 0.0,
    dropMouseDeltaFrames = 0,

    -- 사망 연출 상태
    isDeadTriggered = false,
    deathTimer = 0.0,
    fadeStarted = false,
    vignetteStarted = false,

    -- [테스트] 카메라 셰이크 쿨다운
    cameraShakeCooldown = 0.0,

    -- 게임 상태 트래킹
    wasPlayingLastFrame = false
}

local function Log(msg)
    if DEBUG then
        print("[Player.lua] " .. msg)
    end
end

local function BoolStr(v)
    if v then return "true" end
    return "false"
end

local function Field(obj, upperName, lowerName, fallback)
    if obj == nil then
        return fallback or 0.0
    end

    local upper = obj[upperName]
    if upper ~= nil then
        return upper
    end

    local lower = obj[lowerName]
    if lower ~= nil then
        return lower
    end

    return fallback or 0.0
end

local function VecX(v) return Field(v, "X", "x", 0.0) end
local function VecY(v) return Field(v, "Y", "y", 0.0) end
local function VecZ(v) return Field(v, "Z", "z", 0.0) end

local function RotPitch(r) return Field(r, "Pitch", "pitch", 0.0) end
local function RotYaw(r) return Field(r, "Yaw", "yaw", 0.0) end
local function RotRoll(r) return Field(r, "Roll", "roll", 0.0) end

local function VecStr(v)
    if v == nil then
        return "nil"
    end

    return string.format("(%.3f, %.3f, %.3f)", VecX(v), VecY(v), VecZ(v))
end

local function Clamp(v, minValue, maxValue)
    if v < minValue then return minValue end
    if v > maxValue then return maxValue end
    return v
end

local function SafeDeltaTime(dt)
    if dt == nil or dt <= 0.0 or dt > 0.1 then
        return 1.0 / 60.0
    end
    return dt
end


local function ResolveCallableMember(value, owner)
    if type(value) ~= "function" then
        return value
    end

    local ok, result = pcall(value, owner)
    if ok then
        return result
    end

    ok, result = pcall(value)
    if ok then
        return result
    end

    return nil
end

local function IsValidHandle(handle)
    handle = ResolveCallableMember(handle, Player.ownerObject)
    if handle == nil or type(handle) == "function" then
        return false
    end

    if handle.IsValid == nil then
        return true
    end

    local ok, result = pcall(function()
        return handle:IsValid()
    end)

    return ok and result
end

local function SafeSetProperty(component, propertyName, value)
    if component == nil or component.SetProperty == nil then
        return false
    end

    local ok, result = pcall(function()
        return component:SetProperty(propertyName, value)
    end)

    if not ok then
        Log("[SetProperty 실패] " .. propertyName)
        return false
    end

    return result == true
end

local function SafeAssign(target, key, value)
    if target == nil then
        return false
    end

    local ok = pcall(function()
        target[key] = value
    end)

    if not ok then
        Log("[Assign 실패] " .. tostring(key))
        return false
    end

    return true
end

local function ResetPostProcessEffect()
    Player.isDeadTriggered = false
    Player.deathTimer = 0.0
    Player.fadeStarted = false
    Player.vignetteStarted = false
    
    -- 렌더러가 사용하는 최신 카메라로 동기화 후 프로퍼티 리셋
    if IsValidHandle(Player.controller) and Player.controller.GetActiveCamera ~= nil then
        Player.camera = Player.controller:GetActiveCamera()
    end

    if IsValidHandle(Player.camera) then
        -- [중요] 카메라 컴포넌트의 프로퍼티를 기본값으로 명시적 복구
        Player.camera.VignetteIntensity = 1.0
        Player.camera.FadeAlpha = 0.0
    end
    
    if IsValidHandle(Player.controller) then
        if Player.controller.StopVignette ~= nil then
            Player.controller:StopVignette(0.0)
        end
        if Player.controller.StartFadeOut ~= nil then
            Player.controller:StartFadeOut(0.0)
        end
    end

    -- [연출] 부활 시 닭으로 메쉬 복구
    if Player.ownerObject ~= nil and Player.ownerObject.SetStaticMesh ~= nil then
        Player.ownerObject:SetStaticMesh("Data/Chicken/Chicken.obj")
    end
    
    Log("[FX] PostProcess Effects Reset (Vignette=1.0, Fade=0.0)")
end

local function GetKey(name)
    if Input == nil or Input.GetKey == nil then
        return false
    end

    local ok, result = pcall(Input.GetKey, name)
    if not ok then
        return false
    end

    return result == true
end

local function GetKeyDown(name)
    if Input == nil or Input.GetKeyDown == nil then
        return false
    end

    local ok, result = pcall(Input.GetKeyDown, name)
    if not ok then
        return false
    end

    return result == true
end

local function GetMouseDelta()
    if Input == nil or Input.GetMouseDelta == nil then
        return { x = 0.0, y = 0.0 }
    end

    local d = Input.GetMouseDelta()
    if d == nil then
        return { x = 0.0, y = 0.0 }
    end

    return d
end

local function IsGuiUsingKeyboard()
    if Input ~= nil and Input.IsGuiUsingKeyboard ~= nil then
        return Input.IsGuiUsingKeyboard()
    end

    return false
end

local function IsGuiUsingMouse()
    if Input ~= nil and Input.IsGuiUsingMouse ~= nil then
        return Input.IsGuiUsingMouse()
    end

    return false
end

local function BuildInputState()
    if Input ~= nil and Input.IsGuiUsingMouse ~= nil then
        local guiMouse = Input.IsGuiUsingMouse()

        if guiMouse ~= Player.prevGuiMouse then
            if guiMouse then
                Log("[DIAG] Input.IsGuiUsingMouse() = true  ← 마우스가 GUI에 잡혀있음")
            else
                Log("[DIAG] Input.IsGuiUsingMouse() = false ← 마우스 GUI 캡처 해제")
            end
            Player.prevGuiMouse = guiMouse
        end
    end
    
    if IsGuiUsingKeyboard() then
        return {
            W = false,
            A = false,
            S = false,
            D = false,
            SHIFT = false,
            Dash = false,
            Parry = false,
            any = false,
            signature = "GUI_KEYBOARD_CAPTURED"
        }
    end

    local mouse2Now = GetKey("MOUSE2")
    local engineEdge2 = GetKeyDown("MOUSE2")
    local manualEdge2 = mouse2Now and (not Player.prevMouse2)
    local dashEdge = engineEdge2 or manualEdge2

    Player.prevMouse2 = mouse2Now

    -- Parry 에지 판정용 (직전 프레임의 MOUSE1 상태)
    local mouse1Now = GetKey("MOUSE1")
    local engineEdge1 = GetKeyDown("MOUSE1")
    local manualEdge1 = mouse1Now and (not Player.prevMouse1)
    local parryEdge = engineEdge1 or manualEdge1

    Player.prevMouse1 = mouse1Now

    local state = {
        W = GetKey("W") or GetKey("w"),
        A = GetKey("A") or GetKey("a"),
        S = GetKey("S") or GetKey("s"),
        D = GetKey("D") or GetKey("d"),
        SHIFT = GetKey("SHIFT") or GetKey("LSHIFT"),
        Dash = dashEdge,
        Parry = parryEdge
    }

    state.any = state.W or state.A or state.S or state.D or state.Dash or state.Parry

    state.signature =
        "W=" .. BoolStr(state.W) ..
        " A=" .. BoolStr(state.A) ..
        " S=" .. BoolStr(state.S) ..
        " D=" .. BoolStr(state.D) ..
        " SHIFT=" .. BoolStr(state.SHIFT) ..
        " Dash=" .. BoolStr(state.Dash) ..
        " Parry=" .. BoolStr(state.Parry)

    return state
end

local function DebugInputState(state)
    if state.signature ~= Player.lastInputSignature then
        Log("[INPUT] " .. state.signature)
        Player.lastInputSignature = state.signature
    end
end

local function SetupPawn()
    Player.ownerObject = owner or obj

    if Player.ownerObject == nil then
        print("[Player.lua][BOOT_FAIL] owner가 nil입니다. 이 Lua는 Pawn 액터에 붙어 있어야 합니다.")
        return false
    end

    if Player.ownerObject.AsPawn == nil then
        print("[Player.lua][BOOT_FAIL] owner:AsPawn()이 없습니다.")
        return false
    end

    Player.pawn = Player.ownerObject:AsPawn()

    if Player.pawn == nil then
        print("[Player.lua][BOOT_FAIL] owner가 APawn이 아닙니다.")
        return false
    end

    Log("[PAWN] owner Pawn 사용: Location=" .. VecStr(Player.pawn.Location))

    return true
end

local function SetupPawnMovementComponents()
    if Player.ownerObject.GetOrAddHopMovement ~= nil then
        Player.hopMovement = ResolveCallableMember(Player.ownerObject:GetOrAddHopMovement(), Player.ownerObject)
    end

    local hopGeneric = nil
    if Player.ownerObject.GetComponent ~= nil then
        hopGeneric = ResolveCallableMember(Player.ownerObject:GetComponent("hopmovement"), Player.ownerObject)
    end

    if IsValidHandle(Player.hopMovement) then
        Player.hopMovement.InitialSpeed = CONFIG.HopMovement.InitialSpeed
        Player.hopMovement.MaxSpeed = CONFIG.HopMovement.MaxSpeed
        Player.hopMovement.HopCoefficient = CONFIG.HopMovement.HopCoefficient
        Player.hopMovement.Acceleration = CONFIG.HopMovement.Acceleration
        Player.hopMovement.BrakingDeceleration = CONFIG.HopMovement.BrakingDeceleration
        Player.hopMovement.HopHeight = CONFIG.HopMovement.HopHeight
        Player.hopMovement.HopFrequency = CONFIG.HopMovement.HopFrequency
        Player.hopMovement.HopOnlyWhenMoving = CONFIG.HopMovement.HopOnlyWhenMoving
        Player.hopMovement.ResetHopWhenIdle = CONFIG.HopMovement.ResetHopWhenIdle
        Player.hopMovement.Simulating = CONFIG.HopMovement.Simulating
        Player.hopMovement.Velocity = Vec(0.0, 0.0, 0.0)

        Log("[PAWN] UHopMovementComponent 설정 완료")
    else
        Log("[PAWN_WARN] UHopMovementComponent를 얻지 못했습니다.")
    end

    if IsValidHandle(hopGeneric) then
        hopGeneric.TickEnabled = true
        SafeSetProperty(hopGeneric, "bTickEnable", true)
        SafeSetProperty(hopGeneric, "Auto Register Updated", true)
        SafeSetProperty(hopGeneric, "Updated Component", CONFIG.HopMovement.UpdatedComponent)
        SafeSetProperty(hopGeneric, "Visual Hop Component", CONFIG.HopMovement.VisualHopComponent)
        SafeSetProperty(hopGeneric, "Receive Controller Input", CONFIG.HopMovement.ReceiveControllerInput)
        SafeSetProperty(hopGeneric, "Controller Input Priority", CONFIG.HopMovement.ControllerInputPriority)
    end

    if Player.ownerObject.GetOrAddComponent ~= nil then
        Player.pawnMovementComponent = ResolveCallableMember(Player.ownerObject:GetOrAddComponent("pawnmovement"), Player.ownerObject)
    end

    if IsValidHandle(Player.pawnMovementComponent) then
        Player.pawnMovementComponent.TickEnabled = true
        SafeSetProperty(Player.pawnMovementComponent, "bTickEnable", true)
        SafeSetProperty(Player.pawnMovementComponent, "Auto Register Updated", true)
        SafeSetProperty(Player.pawnMovementComponent, "Updated Component", "")
        SafeSetProperty(Player.pawnMovementComponent, "Receive Controller Input", false)
        SafeSetProperty(Player.pawnMovementComponent, "Controller Input Priority", CONFIG.PawnMovement.ControllerInputPriority)

        Log("[PAWN] UPawnMovementComponent 설정 완료")
    else
        Log("[PAWN_WARN] UPawnMovementComponent를 얻지 못했습니다.")
    end

    if Player.ownerObject.GetOrAddPawnOrientation ~= nil then
        Player.orientation = ResolveCallableMember(Player.ownerObject:GetOrAddPawnOrientation(), Player.ownerObject)
    end

    if IsValidHandle(Player.orientation) then
        Player.orientation.FacingModeIndex = CONFIG.Orientation.FacingMode
        Player.orientation.RotationSpeed = CONFIG.Orientation.RotationSpeed
        Player.orientation.YawOnly = CONFIG.Orientation.YawOnly
        Player.orientation.CustomFacingDirection = CONFIG.Orientation.CustomFacingDirection

        Log("[PAWN] UPawnOrientationComponent 설정 완료")
    else
        Log("[PAWN_WARN] UPawnOrientationComponent를 얻지 못했습니다.")
    end
end

local function SetupCombatComponents()
    Player.parryComp = nil

    -- 먼저 이미 붙어 있는 ParryComponent를 찾습니다.
    if Player.ownerObject.Parry ~= nil then
        Player.parryComp = ResolveCallableMember(Player.ownerObject.Parry, Player.ownerObject)
    end

    -- 플레이어 기본 능력으로 Parry가 필요한 게임에서는 런타임 보정으로 붙입니다.
    -- 에디터에서 수동으로 붙인 경우에는 위 경로가 우선 사용됩니다.
    if not IsValidHandle(Player.parryComp) and Player.ownerObject.GetOrAddParry ~= nil then
        Player.parryComp = ResolveCallableMember(Player.ownerObject:GetOrAddParry(), Player.ownerObject)
    end

    -- 범용 컴포넌트 API가 있는 빌드와의 호환 경로입니다.
    if not IsValidHandle(Player.parryComp) and Player.ownerObject.GetOrAddComponent ~= nil then
        Player.parryComp = ResolveCallableMember(Player.ownerObject:GetOrAddComponent("parry"), Player.ownerObject)
    end

    if IsValidHandle(Player.parryComp) then
        Log("[PAWN] ParryComponent 획득/생성 성공")
    else
        Log("[PAWN_WARN] ParryComponent를 얻지 못했습니다. (패링 불가)")
    end
end


local function ActorHasTag(actor, tag)
    if actor == nil then
        return false
    end

    if actor.HasTag ~= nil then
        local ok, result = pcall(function()
            return actor:HasTag(tag)
        end)
        if ok and result == true then
            return true
        end
    end

    return false
end

local function IsVehicleActor(actor)
    if actor == nil then
        return false
    end

    if Game ~= nil and Game.Map ~= nil and Game.Map.IsVehicle ~= nil then
        local ok, result = pcall(function()
            return Game.Map.IsVehicle(actor)
        end)
        if ok and result == true then
            return true
        end
    end

    return ActorHasTag(actor, "Vehicle") or ActorHasTag(actor, "__RuntimeVehicle")
end

local function SetupController()
    Player.controller = nil

    if World ~= nil and World.GetPlayerController ~= nil then
        Player.controller = World.GetPlayerController(0)
    end

    if not IsValidHandle(Player.controller) and World ~= nil and World.GetOrCreatePlayerController ~= nil then
        Player.controller = World.GetOrCreatePlayerController()
    end

    if not IsValidHandle(Player.controller) and World ~= nil and World.SpawnPlayerController ~= nil then
        Player.controller = World.SpawnPlayerController(CONFIG.Controller.Location)
    end

    if not IsValidHandle(Player.controller) then
        print("[Player.lua][BOOT_FAIL] PlayerController 생성/획득 실패")
        return false
    end

    if Player.controller.Possess ~= nil then
        Player.controller:Possess(Player.pawn)
    end

    if Player.controller.GetControllerInput ~= nil then
        Player.controllerInput = Player.controller:GetControllerInput()
    end

    if not IsValidHandle(Player.controllerInput) and Player.controller.Input ~= nil then
        Player.controllerInput = Player.controller.Input
    end

    if IsValidHandle(Player.controllerInput) then
        Player.controllerInput.MoveSpeed = CONFIG.ControllerInput.MoveSpeed
        Player.controllerInput.SprintMultiplier = CONFIG.ControllerInput.SprintMultiplier
        Player.controllerInput.LookSensitivity = CONFIG.ControllerInput.LookSensitivity
        Player.controllerInput.MinPitch = CONFIG.ControllerInput.MinPitch
        Player.controllerInput.MaxPitch = CONFIG.ControllerInput.MaxPitch
        Player.controllerInput.MovementFrameIndex = CONFIG.ControllerInput.MovementBasis

        Log(
            "[CONTROLLER] Input 설정 완료: " ..
            "MoveSpeed=" .. tostring(Player.controllerInput.MoveSpeed) ..
            " LookSensitivity=" .. tostring(Player.controllerInput.LookSensitivity) ..
            " MovementBasis=" .. tostring(Player.controllerInput.MovementFrameIndex)
        )
    else
        Log("[CONTROLLER_WARN] UControllerInputComponent를 얻지 못했습니다.")
    end

    Player.yaw = RotYaw(CONFIG.Controller.ControlRotation)
    Player.pitch = RotPitch(CONFIG.Controller.ControlRotation)

    Player.controller:SetControlRotation(Rot(Player.pitch, Player.yaw, 0.0))
    Player.pawn.Rotation = Rot(0.0, Player.yaw, 0.0)

    Log("[CONTROLLER] Possess(owner Pawn) 완료")

    return true
end

local function ConfigureCameraProperties(camera)
    camera.WorldLocation = CONFIG.Camera.Location
    camera.WorldRotation = CONFIG.Camera.Rotation

    camera.AspectRatio = CONFIG.Camera.AspectRatio
    camera.FOVRadians = CONFIG.Camera.FOVRadians
    camera.NearZ = CONFIG.Camera.NearZ
    camera.FarZ = CONFIG.Camera.FarZ
    camera.Orthographic = CONFIG.Camera.Orthographic
    camera.OrthoWidth = CONFIG.Camera.OrthoWidth

    camera.ViewModeIndex = CONFIG.Camera.ViewMode
    camera.UseOwnerAsTarget = CONFIG.Camera.FollowSubjectAuto
    camera.TargetOffset = CONFIG.Camera.FollowOffset
    camera.BackDistance = CONFIG.Camera.BackDistance
    camera.Height = CONFIG.Camera.Height
    camera.SideOffset = CONFIG.Camera.SideOffset

    camera:SetTargetActor(Player.ownerObject)

    SafeSetProperty(camera, "FOV", CONFIG.Camera.FOVRadians)
    SafeSetProperty(camera, "Aspect Ratio", CONFIG.Camera.AspectRatio)
    SafeSetProperty(camera, "Near Z", CONFIG.Camera.NearZ)
    SafeSetProperty(camera, "Far Z", CONFIG.Camera.FarZ)
    SafeSetProperty(camera, "Orthographic", CONFIG.Camera.Orthographic)
    SafeSetProperty(camera, "Ortho Width", CONFIG.Camera.OrthoWidth)

    SafeSetProperty(camera, "View Mode", CONFIG.Camera.ViewMode)
    SafeSetProperty(camera, "Follow Subject Auto", CONFIG.Camera.FollowSubjectAuto)
    SafeSetProperty(camera, "Follow Offset", CONFIG.Camera.FollowOffset)

    SafeSetProperty(camera, "Eye Height", CONFIG.Camera.EyeHeight)
    SafeSetProperty(camera, "First Person Use Control Rotation", CONFIG.Camera.FirstPersonUseControlRotation)

    SafeSetProperty(camera, "Back Distance", CONFIG.Camera.BackDistance)
    SafeSetProperty(camera, "Height", CONFIG.Camera.Height)
    SafeSetProperty(camera, "Side Offset", CONFIG.Camera.SideOffset)
    SafeSetProperty(camera, "View Offset", CONFIG.Camera.ViewOffset)
    SafeSetProperty(camera, "Use Target Forward", CONFIG.Camera.UseTargetForward)
    SafeSetProperty(camera, "Use Control Rotation Yaw", CONFIG.Camera.UseControlRotationYaw)

    SafeSetProperty(camera, "Enable Look Ahead", CONFIG.Camera.EnableLookAhead)
    SafeSetProperty(camera, "Look Ahead Distance", CONFIG.Camera.LookAheadDistance)
    SafeSetProperty(camera, "Look Ahead Lag Speed", CONFIG.Camera.LookAheadLagSpeed)

    SafeSetProperty(camera, "Stabilize Vertical In Orthographic", CONFIG.Camera.StabilizeVerticalInOrthographic)
    SafeSetProperty(camera, "Vertical Dead Zone", CONFIG.Camera.VerticalDeadZone)
    SafeSetProperty(camera, "Vertical Follow Strength", CONFIG.Camera.VerticalFollowStrength)
    SafeSetProperty(camera, "Vertical Lag Speed", CONFIG.Camera.VerticalLagSpeed)

    SafeSetProperty(camera, "Enable Smoothing", CONFIG.Camera.EnableSmoothing)
    SafeSetProperty(camera, "Location Lag Speed", CONFIG.Camera.LocationLagSpeed)
    SafeSetProperty(camera, "Rotation Lag Speed", CONFIG.Camera.RotationLagSpeed)
    SafeSetProperty(camera, "FOV Lag Speed", CONFIG.Camera.FOVLagSpeed)
    SafeSetProperty(camera, "Ortho Width Lag Speed", CONFIG.Camera.OrthoWidthLagSpeed)

    SafeSetProperty(camera, "Blend Time", CONFIG.Camera.BlendTime)
    SafeSetProperty(camera, "Blend Function", CONFIG.Camera.BlendFunction)
    SafeSetProperty(camera, "Projection Switch Mode", CONFIG.Camera.ProjectionSwitchMode)
    SafeSetProperty(camera, "Blend Location", CONFIG.Camera.BlendLocation)
    SafeSetProperty(camera, "Blend Rotation", CONFIG.Camera.BlendRotation)
    SafeSetProperty(camera, "Blend FOV", CONFIG.Camera.BlendFOV)
    SafeSetProperty(camera, "Blend Ortho Width", CONFIG.Camera.BlendOrthoWidth)
    SafeSetProperty(camera, "Blend Near/Far", CONFIG.Camera.BlendNearFar)
end

local function SetupCamera()
    Player.camera = nil
    Player.cameraActor = nil

    if World ~= nil and World.GetActiveCamera ~= nil then
        Player.camera = World.GetActiveCamera()
    end

    if not IsValidHandle(Player.camera) then
        if World == nil or World.SpawnCamera == nil then
            print("[Player.lua][BOOT_FAIL] World.SpawnCamera가 없습니다.")
            return false
        end

        Player.cameraActor = World.SpawnCamera(CONFIG.Camera.Location)

        if not IsValidHandle(Player.cameraActor) then
            print("[Player.lua][BOOT_FAIL] CameraActor 생성 실패")
            return false
        end

        Player.cameraActor.Name = "LuaPlayerCamera"
        Player.cameraActor.Location = CONFIG.Camera.Location
        Player.cameraActor.Rotation = CONFIG.Camera.Rotation

        if Player.cameraActor.GetOrAddCamera ~= nil then
            Player.camera = Player.cameraActor:GetOrAddCamera()
        elseif Player.cameraActor.Camera ~= nil then
            Player.camera = Player.cameraActor.Camera
        end
    end

    if not IsValidHandle(Player.camera) then
        print("[Player.lua][BOOT_FAIL] CameraComponent 획득 실패")
        return false
    end

    ConfigureCameraProperties(Player.camera)

    -- ActiveCamera 연결.
    Player.controller:SetActiveCamera(Player.camera)

    if Player.camera.SetAsActiveCamera ~= nil then
        Player.camera:SetAsActiveCamera(Player.controller)
    end

    if World ~= nil and World.SetActiveCamera ~= nil then
        World.SetActiveCamera(Player.camera)
    end

    -- 카메라 전환 직후 발생하는 큰 마우스 델타를 버리기 위한 가드
    Player.cameraSwitchGuardTime = CONFIG.ControllerInput.CameraSwitchLookGuardTime
    Player.dropMouseDeltaFrames = 2

    Player.controller:SetControlRotation(Rot(Player.pitch, Player.yaw, 0.0))
    Player.pawn.Rotation = Rot(0.0, Player.yaw, 0.0)

    Log("[CAMERA] CameraActor/CameraComponent 설정 완료")

    return true
end

local function Bootstrap()
    if Player.initialized then
        return true
    end

    Log("[BOOT] 시작")

    if not SetupPawn() then
        return false
    end

    SetupPawnMovementComponents()
    SetupCombatComponents()

    if not SetupController() then
        return false
    end

    if not SetupCamera() then
        return false
    end

    if Input ~= nil and Input.SetMouseCaptured ~= nil then
        Input.SetMouseCaptured(true)
    end

    Player.initialized = true

    ResetPostProcessEffect()

    print("[Player.lua][BOOT_OK] 설정 완료")

    return true
end

local function UpdateLook(dt)
    if IsGuiUsingMouse() then
        return
    end

    if Player.dropMouseDeltaFrames ~= nil and Player.dropMouseDeltaFrames > 0 then
        GetMouseDelta()
        Player.dropMouseDeltaFrames = Player.dropMouseDeltaFrames - 1
        return
    end

    if Player.cameraSwitchGuardTime ~= nil and Player.cameraSwitchGuardTime > 0.0 then
        GetMouseDelta()
        Player.cameraSwitchGuardTime = Player.cameraSwitchGuardTime - dt
        return
    end

    local mouse = GetMouseDelta()
    local dx = Field(mouse, "X", "x", 0.0)
    local dy = Field(mouse, "Y", "y", 0.0)

    if dx == 0.0 and dy == 0.0 then
        return
    end

    local maxDelta = CONFIG.ControllerInput.MaxMouseDeltaPerFrame or 80.0
    dx = Clamp(dx, -maxDelta, maxDelta)
    dy = Clamp(dy, -maxDelta, maxDelta)

    local sensitivity = CONFIG.ControllerInput.LookSensitivity

    Player.yaw = Player.yaw + dx * sensitivity
    Player.pitch = Clamp(
        Player.pitch + dy * sensitivity,
        CONFIG.ControllerInput.MinPitch,
        CONFIG.ControllerInput.MaxPitch
    )

    Player.controller:SetControlRotation(Rot(Player.pitch, Player.yaw, 0.0))
    Player.pawn.Rotation = Rot(0.0, Player.yaw, 0.0)
end

local function ComputeWorldMoveDirection(inputState)
    local x = 0.0
    local y = 0.0

    if inputState.W then x = x + 1.0 end
    if inputState.S then x = x - 1.0 end
    if inputState.D then y = y + 1.0 end
    if inputState.A then y = y - 1.0 end

    if x == 0.0 and y == 0.0 then
        return Vec(0.0, 0.0, 0.0), false
    end

    local yawRad = math.rad(Player.yaw)

    local forward = Vec(math.cos(yawRad), math.sin(yawRad), 0.0)
    local right = Vec(-math.sin(yawRad), math.cos(yawRad), 0.0)

    local move = forward * x + right * y

    if move:Length() <= 0.0001 then
        return Vec(0.0, 0.0, 0.0), false
    end

    return move:Normalized(), true
end

local function UpdateMovement(dt, inputState)
    local worldDir, hasMove = ComputeWorldMoveDirection(inputState)

    if inputState.Dash then
        if IsValidHandle(Player.hopMovement) then
            Player.hopMovement:Dash()
            Log("[Player.lua] Dash Triggered!")
        end
    end

    if not hasMove then
        if IsValidHandle(Player.hopMovement) then
            Player.hopMovement.HopCoefficient = CONFIG.HopMovement.HopCoefficient
            Player.hopMovement:ClearMovementInput()
        end

        return
    end

    local sprintScale = 1.0
    if inputState.SHIFT then
        sprintScale = CONFIG.ControllerInput.SprintMultiplier
    end

    if IsValidHandle(Player.hopMovement) then
        Player.hopMovement.InitialSpeed = CONFIG.HopMovement.InitialSpeed
        Player.hopMovement.HopCoefficient = CONFIG.HopMovement.HopCoefficient * sprintScale
        Player.hopMovement.MovementInput = worldDir

        if not Player.printedMove then
            print("[Player.lua][MOVE_OK] Lua 입력 → HopMovementComponent.MovementInput 적용")
            Player.printedMove = true
        end

        return
    end

    local speed = CONFIG.ControllerInput.MoveSpeed * sprintScale
    Player.pawn.Location = Player.pawn.Location + worldDir * speed * dt
end

local function UpdateCombat(inputState)
    if inputState.Parry then
        if IsValidHandle(Player.parryComp) then
            Player.parryComp:Parry()
            Log("[Player.lua] Parry Triggered!")
        end
    end
end

function OnOverlap(otherActor)
    if not IsVehicleActor(otherActor) or Player.isDeadTriggered then
        return
    end

    if State == nil or State.IsPlaying == nil or not State.IsPlaying() then
        return
    end

    if IsValidHandle(Player.parryComp) and Player.parryComp.IsParrying ~= nil then
        local ok, isParrying = pcall(function()
            return Player.parryComp:IsParrying()
        end)
        if ok and isParrying == true then
            Log("[COLLISION] Vehicle overlap ignored while parrying")
            return
        end
    end


     Sound.PlayEffect("Crash")

    -- 사망 연출 시작
    Player.isDeadTriggered = true
    Player.deathTimer = 2.0
    Player.fadeStarted = false
    Player.vignetteStarted = false
    
    -- [연출] 사망 시 KFC 박스로 메쉬 변경
    if Player.ownerObject ~= nil and Player.ownerObject.SetStaticMesh ~= nil then
        Player.ownerObject:SetStaticMesh("Data/Chicken/KFC.obj")
    end
    
    if State ~= nil then
        State.IsDying = true
    end

    if World ~= nil and World.StartSlomo ~= nil then
        World.StartSlomo(0.2, 3.0)
    end

    if IsValidHandle(Player.controller) then
        -- 피격 시 즉각적인 붉은 Vignette 효과 (0.3초 동안 intensity 0.4로)
        if Player.controller.StartVignette ~= nil then
            Player.controller:StartVignette(0.4, Vec(0.8, 0, 0), 0.3, 0.6)
        end
        
        -- 강한 셰이크
        if Player.controller.StartCameraShake ~= nil then
            Player.controller:StartCameraShake(0.4, 0.15, 2.0, 30.0)
        end
    end

    Log("[COLLISION] Vehicle hit! Actor: " .. tostring(otherActor.Name))
end

function BeginPlay()
    Bootstrap()
end

function OnInput(deltaTime)
    Player.frame = Player.frame + 1

    if not Bootstrap() then
        return
    end

    local dt = SafeDeltaTime(deltaTime)

    -- 셰이크 쿨다운 차감
    if Player.cameraShakeCooldown > 0.0 then
        Player.cameraShakeCooldown = Player.cameraShakeCooldown - dt
    end

    -- 게임 재시작/시작 감지 (상태 전이 체크)
    local isPlayingNow = (State ~= nil and State.IsPlaying ~= nil and State.IsPlaying())
    if isPlayingNow and not Player.wasPlayingLastFrame then
        Log("[GAME] Playing State Entered. Resetting effects.")
        ResetPostProcessEffect()
    end
    Player.wasPlayingLastFrame = isPlayingNow

    -- 게임 중이 아닐 때 처리
    if not isPlayingNow then
        if Player.isDeadTriggered then
            Log("[DEATH_ABORT] Game stopped. Resetting death FX.")
            ResetPostProcessEffect()
        end
        
        if IsValidHandle(Player.hopMovement) then
            Player.hopMovement:ClearMovementInput()
        end

        if Input ~= nil and Input.SetMouseCaptured ~= nil then
            Input.SetMouseCaptured(false)
        end
        return
    end

    -- 사망 연출 처리
    if Player.isDeadTriggered then
        -- 매 프레임 최신 카메라 동기화
        if IsValidHandle(Player.controller) and Player.controller.GetActiveCamera ~= nil then
            Player.camera = Player.controller:GetActiveCamera()
        end

        if Player.deathTimer > 0 then
            Player.deathTimer = Player.deathTimer - dt
            
            -- Phase 1: Vignette 연출 (남은 시간 2.0s ~ 1.5s 구간)
            if Player.deathTimer > 1.5 then
                if not Player.vignetteStarted then
                    if IsValidHandle(Player.controller) and Player.controller.StartVignette ~= nil then
                        -- 0.5초 동안 더 강한 Vignette(0.1)로 보간
                        Player.controller:StartVignette(0.1, Vec(0.8, 0, 0), 0.5, 0.6)
                    end
                    Player.vignetteStarted = true
                end
            else
                -- Phase 2: Fade Out 시작
                if not Player.fadeStarted then
                    if IsValidHandle(Player.controller) then
                        Player.controller:StartFadeIn(1.5, 1.0, Vec(0, 0, 0))
                    end
                    Player.fadeStarted = true
                end
            end

            if Player.deathTimer <= 0 then
                Log("[DEATH] Sequence Finished")
                if State.GameOver ~= nil then
                    State.GameOver("Hit by vehicle")
                elseif Game ~= nil and Game.DispatchEvent ~= nil then
                    Game.DispatchEvent("Defeat", Player.ownerObject)
                end
            end
        end
        return 
    end

    if Input ~= nil and Input.SetMouseCaptured ~= nil then
        Input.SetMouseCaptured(true)
    end

    local inputState = BuildInputState()
    DebugInputState(inputState)

    UpdateLook(dt)
    UpdateMovement(dt, inputState)
    UpdateCombat(inputState)
end

function EndPlay()
    if Input ~= nil and Input.SetMouseCaptured ~= nil then
        Input.SetMouseCaptured(false)
    end

    if IsValidHandle(Player.hopMovement) then
        Player.hopMovement:ClearMovementInput()
    end

    print("[Player.lua][END]")
end
