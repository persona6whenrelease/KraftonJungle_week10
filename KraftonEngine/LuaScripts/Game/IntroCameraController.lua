-- IntroCameraController.lua

local State = require("Game.GameState")

-- UI callback / LuaJIT native crash 방어용.
-- JIT가 없는 빌드에서는 조용히 무시됩니다.
if jit ~= nil and jit.off ~= nil then
    pcall(jit.off, true, true)
end

-- =========================================================
-- Intro camera settings
-- =========================================================

-- 화면 중앙으로 보여줄 지점이 X축으로 왕복하는 범위입니다.
-- sweepX는 카메라 위치가 아니라 "화면 중앙 타겟 X"입니다.
local SWEEP_RANGE  = 20.0
local SWEEP_SPEED  = 2.0

local TARGET_Y     = 0.0
local TARGET_Z     = 0.0

-- 인트로 ortho 카메라 높이
local CAM_Z        = 30.0

-- Orthographic 가로 폭입니다.
local ORTHO_WIDTH  = 40.0

-- 45도 우하단 시점.
-- 방향이 반대면 VIEW_YAW만 135.0, 45.0, -135.0 중 하나로 바꿔보시면 됩니다.
local VIEW_PITCH   = 45.0
local VIEW_YAW     = -45.0

-- Perspective bridge용 FOV.
-- 엔진 CameraComponent.FOV는 radian 기준입니다.
-- 1.0471975512 = 60도
local BRIDGE_FOV_RAD = 1.0471975512

-- 엔진 기본 카메라 AspectRatio와 맞춥니다.
local CAMERA_ASPECT  = 16.0 / 9.0

-- 게임 시작 시 게임 카메라로 넘어가는 블렌드 시간입니다.
local START_BLEND_TIME = 1.2

-- =========================================================
-- Runtime state
-- =========================================================

local pc           = nil
local introActor   = nil
local introCam     = nil
local gameCam      = nil

local sweepX       = 0.0
local direction    = 1.0


local bIntroActive = false
local bStarted     = false
local pendingStart = false

-- Cinematic specific state
local cinematicPhase = 1 -- 1: Spin, 2: Fade Out, 3: Fade In, 0: Idle/UI
local cinematicTimer = 0.0
local cinematicYaw   = VIEW_YAW
local SPIN_DURATION = 3.0
local FADE_OUT_DURATION = 0.5
local FADE_IN_DURATION = 0.2


-- =========================================================
-- Helpers
-- =========================================================

local function V(x, y, z)
    return FVector.new(x, y, z)
end

local function R(pitch, yaw, roll)
    return FRotator.new(pitch, yaw, roll)
end

local function is_valid(h)
    return h ~= nil and h.IsValid ~= nil and h:IsValid()
end

local function safe_call(label, fn)
    local ok, err = pcall(fn)

    if not ok then
        print("[IntroCam][ERROR] " .. tostring(label) .. ": " .. tostring(err))
        return false
    end

    return true
end

local function clear_ui_handler()
    if Game ~= nil and Game.UI ~= nil and Game.UI.ClearEventHandler ~= nil then
        safe_call("Game.UI.ClearEventHandler", function()
            Game.UI.ClearEventHandler()
        end)
    end
end

local function hide_game_over_ui()
    if Game ~= nil and Game.UI ~= nil and Game.UI.HideGameOver ~= nil then
        Game.UI.HideGameOver()
    end
end

local function deg_to_rad(deg)
    return deg * 3.1415926535 / 180.0
end

local function get_forward_from_angles(pitch, yaw)
    local pitchRad = deg_to_rad(pitch)
    local yawRad   = deg_to_rad(yaw)

    local cp = math.cos(pitchRad)

    return {
        x = cp * math.cos(yawRad),
        y = cp * math.sin(yawRad),
        z = -math.sin(pitchRad)
    }
end

-- VIEW_PITCH / VIEW_YAW 방향으로 target을 바라보도록
-- ortho 카메라 위치를 역산합니다.
-- 이 함수는 "카메라 높이 CAM_Z 고정"입니다.
local function get_ortho_camera_location_for_target(targetX, targetY, targetZ, yaw)
    yaw = yaw or VIEW_YAW
    local f = get_forward_from_angles(VIEW_PITCH, VIEW_YAW)

    local distance = (CAM_Z - targetZ) / (-f.z)

    return V(
        targetX - f.x * distance,
        targetY - f.y * distance,
        CAM_Z
    )
end

-- OrthoWidth와 perspective FOV가 target plane에서 비슷한 화면 크기를 갖도록
-- perspective 카메라 거리를 계산합니다.
local function get_bridge_distance_matching_ortho_width()
    -- 엔진 ortho는 OrthoWidth를 가로폭으로 쓰고,
    -- perspective FOV는 세로 FOV입니다.
    local orthoHeight = ORTHO_WIDTH / CAMERA_ASPECT

    return (orthoHeight * 0.5) / math.tan(BRIDGE_FOV_RAD * 0.5)
end

-- VIEW_PITCH / VIEW_YAW 방향으로 target을 바라보도록
-- perspective bridge 카메라 위치를 역산합니다.
-- 이 함수는 "화면 크기 매칭 거리"를 기준으로 합니다.
local function get_perspective_bridge_location_for_target(targetX, targetY, targetZ)
    local f = get_forward_from_angles(VIEW_PITCH, VIEW_YAW)
    local distance = get_bridge_distance_matching_ortho_width()

    return V(
        targetX - f.x * distance,
        targetY - f.y * distance,
        targetZ - f.z * distance
    )
end

local function set_camera_property(cam, name, value)
    if not is_valid(cam) then return false end
    if cam.SetProperty == nil then return false end

    return safe_call("SetProperty " .. tostring(name), function()
        cam:SetProperty(name, value)
    end)
end


local function get_spin_camera_location_for_target(targetX, targetY, targetZ, yaw)
    yaw = yaw or VIEW_YAW
    local pitch = 10.0
    local f = get_forward_from_angles(pitch, yaw)
    local distance = 2.0
    return V(
        targetX - f.x * distance,
        targetY - f.y * distance,
        targetZ - f.z * distance + 0.5
    )
end

local function apply_intro_spin_transform(targetX, yaw)
    if not is_valid(introCam) then return end

    introCam.ViewMode      = "Static"
    introCam.Orthographic  = false
    introCam.FOVRadians    = BRIDGE_FOV_RAD
    introCam.AspectRatio   = CAMERA_ASPECT
    introCam.OrthoWidth    = ORTHO_WIDTH
    
    local pitch = 15.0
    introCam.WorldLocation = get_spin_camera_location_for_target(targetX, TARGET_Y, TARGET_Z, yaw)
    introCam.WorldRotation = R(pitch, yaw, 0.0)
end

local function apply_intro_ortho_transform(targetX, yaw)
    yaw = yaw or VIEW_YAW
    if not is_valid(introCam) then return end

    introCam.ViewMode      = "Static"
    introCam.Orthographic  = true
    introCam.OrthoWidth    = ORTHO_WIDTH
    introCam.AspectRatio   = CAMERA_ASPECT
    introCam.WorldLocation = get_ortho_camera_location_for_target(targetX, TARGET_Y, TARGET_Z, yaw)
    introCam.WorldRotation = R(VIEW_PITCH, yaw or VIEW_YAW, 0.0)
end

-- 스타트 순간에 현재 활성 introCam을 perspective-equivalent 상태로 바꿉니다.
-- 이후 pc:SetActiveCamera(introCam)을 한 번 호출해서 PlayerCameraManager.CurrentView를
-- 이 perspective 상태로 스냅한 뒤, gameCam으로 블렌드합니다.
local function apply_intro_perspective_bridge_transform(targetX)
    if not is_valid(introCam) then return end

    introCam.ViewMode      = "Static"
    introCam.Orthographic  = false
    introCam.FOVRadians    = BRIDGE_FOV_RAD
    introCam.AspectRatio   = CAMERA_ASPECT
    introCam.OrthoWidth    = ORTHO_WIDTH
    introCam.WorldLocation = get_perspective_bridge_location_for_target(targetX, TARGET_Y, TARGET_Z)
    introCam.WorldRotation = R(VIEW_PITCH, yaw or VIEW_YAW, 0.0)

    set_camera_property(introCam, "Enable Smoothing", false)
end

local function configure_intro_camera(cam)
    if not is_valid(cam) then return end

    cam.ViewMode      = "Static"
    cam.Orthographic  = true
    cam.OrthoWidth    = ORTHO_WIDTH
    cam.AspectRatio   = CAMERA_ASPECT

    -- 인트로 카메라는 직접 Tick에서 움직입니다.
    set_camera_property(cam, "Enable Smoothing", false)

    apply_intro_ortho_transform(0.0)
end

local function configure_game_camera_transition(cam)
    if not is_valid(cam) then return end

    -- PlayerCameraManager는 전환 대상 카메라의 TransitionSettings를 참고합니다.
    set_camera_property(cam, "Blend Time", START_BLEND_TIME)

    -- 0 = Linear, 1 = EaseIn, 2 = EaseOut, 3 = EaseInOut
    set_camera_property(cam, "Blend Function", 3)

    -- 이제 StartGame 직전에 introCam을 perspective로 스냅하므로,
    -- gameCam 전환은 perspective -> perspective 전환입니다.
    -- 따라서 projection switch는 시작으로 둬도 튐이 적습니다.
    set_camera_property(cam, "Projection Switch Mode", 0)

    set_camera_property(cam, "Blend Location", true)
    set_camera_property(cam, "Blend Rotation", true)
    set_camera_property(cam, "Blend FOV", true)
    set_camera_property(cam, "Blend Ortho Width", true)
    set_camera_property(cam, "Blend Near/Far", false)

    -- 게임 카메라 자체 추적도 부드럽게 유지합니다.
    set_camera_property(cam, "Enable Smoothing", true)
    set_camera_property(cam, "Location Lag Speed", 8.0)
    set_camera_property(cam, "Rotation Lag Speed", 8.0)
    set_camera_property(cam, "FOV Lag Speed", 8.0)
    set_camera_property(cam, "Ortho Width Lag Speed", 8.0)
end

-- =========================================================
-- Camera setup
-- =========================================================

local function setup_intro_camera()
    introActor = World.FindActorByName("IntroCamera")

    if not is_valid(introActor) then
        print("[IntroCam] IntroCamera not found. Spawning runtime camera.")
        introActor = World.SpawnCamera(get_ortho_camera_location_for_target(0.0, TARGET_Y, TARGET_Z))
    end

    if not is_valid(introActor) then
        print("[IntroCam] IntroCamera actor invalid")
        return nil
    end

    local cam = introActor.Camera

    if not is_valid(cam) then
        cam = introActor:GetOrAddCamera()
    end

    if not is_valid(cam) then
        print("[IntroCam] Intro camera component invalid")
        return nil
    end

    return cam
end

local function cache_game_camera()
    if not is_valid(pc) then
        return
    end

    local active = pc:GetActiveCamera()

    if is_valid(active) and active ~= introCam then
        gameCam = active
        return
    end

    active = World.GetActiveCamera()
    if is_valid(active) and active ~= introCam then
        gameCam = active
    end
end

local function init_intro_camera()
    print("[IntroCam] BeginPlay called")

    pc = World.GetPlayerController(0)
    if not is_valid(pc) then
        pc = World.GetOrCreatePlayerController()
    end

    if not is_valid(pc) then
        print("[IntroCam] PlayerController invalid")
        return
    end

    -- AutoWire가 이미 잡아둔 캐릭터/게임 카메라를 저장합니다.
    gameCam = pc:GetActiveCamera()

    if not is_valid(gameCam) then
        gameCam = World.GetActiveCamera()
    end

    if not is_valid(gameCam) then
        print("[IntroCam] Gameplay camera invalid")
        return
    end

    introCam = setup_intro_camera()
    if not is_valid(introCam) then
        return
    end

    sweepX = 0.0
    direction = 1.0
    bIntroActive = true
    bStarted = false
    pendingStart = false

    cinematicPhase = 1
    cinematicTimer = 0.0
    cinematicYaw = VIEW_YAW

    configure_intro_camera(introCam)
    configure_game_camera_transition(gameCam)

    -- 인트로 진입은 즉시 인트로 카메라로 고정합니다.
    pc:SetActiveCamera(introCam)

    if World ~= nil and World.SetActiveCamera ~= nil then
        World.SetActiveCamera(introCam)
    end

    hide_game_over_ui()

    if Game ~= nil and Game.UI ~= nil and Game.UI.ShowHUD ~= nil then
        Game.UI.ShowHUD(false)
    end

    if Game ~= nil and Game.UI ~= nil and Game.UI.ShowIntro ~= nil then
        Game.UI.ShowIntro(false) -- Wait for cinematic
    end

    print("[IntroCam] Intro camera activated")
end

local function reset_to_intro()
    sweepX = 0.0
    direction = 1.0
    bIntroActive = true
    bStarted = false
    pendingStart = false

    cinematicPhase = 1
    cinematicTimer = 0.0
    cinematicYaw = VIEW_YAW

    if not is_valid(pc) then
        pc = World.GetPlayerController(0)
        if not is_valid(pc) then
            pc = World.GetOrCreatePlayerController()
        end
    end

    if not is_valid(introCam) then
        introCam = setup_intro_camera()
    end

    hide_game_over_ui()

    if Game ~= nil and Game.UI ~= nil and Game.UI.ShowHUD ~= nil then
        Game.UI.ShowHUD(false)
    end

    if Game ~= nil and Game.UI ~= nil and Game.UI.ShowIntro ~= nil then
        Game.UI.ShowIntro(false) -- Wait for cinematic
    end

    if is_valid(introCam) then
        configure_intro_camera(introCam)

        if is_valid(pc) then
            pc:SetActiveCamera(introCam)
        end

        if World ~= nil and World.SetActiveCamera ~= nil then
            World.SetActiveCamera(introCam)
        end
    end

    if is_valid(gameCam) then
        configure_game_camera_transition(gameCam)
    end
end

-- =========================================================
-- Start game
-- =========================================================

function StartGame()
    if bStarted then
        print("[IntroCam] StartGame ignored: already started")
        return
    end

    print("[IntroCam] StartGame entered from Tick")

    bStarted = true
    pendingStart = false
    bIntroActive = false

    if not is_valid(pc) then
        print("[IntroCam] StartGame failed: pc invalid")
        return
    end

    if not is_valid(introCam) then
        print("[IntroCam] StartGame failed: introCam invalid")
        return
    end

    if not is_valid(gameCam) then
        cache_game_camera()
    end

    if not is_valid(gameCam) then
        print("[IntroCam] StartGame failed: gameCam invalid")
        return
    end

    hide_game_over_ui()

    -- 중요:
    -- ortho -> perspective를 PlayerCameraManager 블렌드에 맡기지 않습니다.
    -- 먼저 현재 introCam 자체를 perspective-equivalent 상태로 바꿉니다.
    -- target plane 기준으로 OrthoWidth와 FOV가 비슷하게 보이도록 위치를 다시 계산합니다.
    apply_intro_perspective_bridge_transform(sweepX)

    -- 중요:
    -- CurrentView가 아직 지난 프레임의 ortho view일 수 있으므로,
    -- introCam을 한 번 active camera로 "무블렌드 스냅"해서
    -- PlayerCameraManager 내부 CurrentView를 perspective bridge 상태로 갱신합니다.
    -- 이 스냅은 introCam이 ortho와 최대한 비슷한 perspective 구도라서 눈에 덜 띕니다.
    pc:SetActiveCamera(introCam)

    -- 이제부터는 perspective bridge -> gameplay perspective 전환입니다.
    -- 이 구간은 projection bool이 바뀌지 않아서 훨씬 부드럽습니다.
    configure_game_camera_transition(gameCam)
    pc:SetActiveCameraWithBlend(gameCam)

    -- 실제 게임 상태 시작.
    -- Intro -> Start도 새 런이므로 맵/스포너/플레이 상태를 모두 초기화합니다.
    if State.StartFreshRun ~= nil then
        State.StartFreshRun("IntroStart")
    else
        State.StartGame("IntroStart")
    end

    print("[IntroCam] StartGame: perspective bridge -> game camera blend requested")
end

-- =========================================================
-- UI
-- =========================================================

local function request_start()
    if bStarted or pendingStart then
        print("[IntroCam] duplicate start ignored")
        return
    end

    -- RmlUi click callback 안에서는 카메라/월드 상태를 바로 바꾸지 않습니다.
    -- 다음 Tick에서 StartGame을 실행합니다.
    pendingStart = true
end

local function handle_ui_event(eventName)
    print("[IntroCam] UI event = " .. tostring(eventName))

    if eventName == "start" then
        request_start()
        return
    end

    if eventName == "restart" then
        bStarted = false
        pendingStart = false
        bIntroActive = false

        hide_game_over_ui()
        State.RestartRun()
        return
    end

    if eventName == "main_menu" then
        bStarted = false
        pendingStart = false
        bIntroActive = true

        hide_game_over_ui()

        local function finish_main_menu_reset()
            if State.ResetToIntro ~= nil then
                State.ResetToIntro()
            end

            reset_to_intro()
        end

        local asyncResetFunc = nil
        local resetFunc = nil

        if _G ~= nil and _G.MapManager_ResetAsync ~= nil then
            asyncResetFunc = _G.MapManager_ResetAsync
        elseif MapManager_ResetAsync ~= nil then
            asyncResetFunc = MapManager_ResetAsync
        end

        if _G ~= nil and _G.MapManager_Reset ~= nil then
            resetFunc = _G.MapManager_Reset
        elseif MapManager_Reset ~= nil then
            resetFunc = MapManager_Reset
        end

        if asyncResetFunc ~= nil then
            asyncResetFunc("MainMenu", function()
                finish_main_menu_reset()
            end)
            return
        end

        if resetFunc ~= nil then
            resetFunc("MainMenu")
        end

        finish_main_menu_reset()
        return
    end

    if eventName == "continue" then
        if State.UpdateHUD ~= nil then
            State.UpdateHUD()
        end
        return
    end

    if eventName == "exit" then
        -- Standalone 종료는 C++ 콜백이 처리합니다.
        return
    end
end

local function bind_ui()
    clear_ui_handler()

    if Game ~= nil and Game.UI ~= nil and Game.UI.SetEventHandler ~= nil then
        Game.UI.SetEventHandler(function(eventName)
            handle_ui_event(eventName)
        end)

        print("[IntroCam] UI handler bound")
    else
        print("[IntroCam] UI binding not available")
    end
end

-- 최신 robust UI 라우터를 쓰는 경우에도 받을 수 있게 둡니다.
function OnUIEvent(eventName)
    handle_ui_event(eventName)
end

function OnHotReload()
    print("[IntroCam] OnHotReload")

    clear_ui_handler()
    bind_ui()

    if bIntroActive and is_valid(introCam) then
        apply_intro_ortho_transform(sweepX)
    end
end

-- =========================================================
-- Engine entry points
-- =========================================================

function BeginPlay()
    clear_ui_handler()
    init_intro_camera()
    bind_ui()
end

function Tick(dt)
    if bIntroActive and not bStarted and not pendingStart and cinematicPhase == 0 then
        if Input ~= nil and Input.GetKeyDown ~= nil then
            if Input.GetKeyDown("ENTER") or Input.GetKeyDown("SPACE") then
                request_start()
            end
        end
    end

    if pendingStart then
        pendingStart = false
        StartGame()
    end

    if not bIntroActive then return end
    if not is_valid(introCam) then return end

    if cinematicPhase == 1 then
        cinematicTimer = cinematicTimer + dt
        local t = cinematicTimer / SPIN_DURATION
        cinematicYaw = VIEW_YAW + t * 360.0
        apply_intro_spin_transform(0.0, cinematicYaw)

        if cinematicTimer >= SPIN_DURATION then
            cinematicPhase = 2
            cinematicTimer = 0.0
            if is_valid(pc) and pc.StartFadeIn ~= nil then
                pc:StartFadeIn(FADE_OUT_DURATION, 1.0, FVector.new(0,0,0))
            end
        end
    elseif cinematicPhase == 2 then
        cinematicTimer = cinematicTimer + dt
        apply_intro_spin_transform(0.0, VIEW_YAW + 360.0)

        if cinematicTimer >= FADE_OUT_DURATION then
            cinematicPhase = 3
            cinematicTimer = 0.0
            apply_intro_ortho_transform(0.0, VIEW_YAW)
            
            -- 암전된 상태에서 맵 생성
            if _G.MapManager_GenerateInitial ~= nil then
                _G.MapManager_GenerateInitial()
            end
            
            if Game ~= nil and Game.UI ~= nil and Game.UI.ShowIntro ~= nil then
                Game.UI.ShowIntro(true)
            end
            
            if is_valid(pc) and pc.StartFadeOut ~= nil then
                pc:StartFadeOut(FADE_IN_DURATION)
            end
        end
    elseif cinematicPhase == 3 then
        cinematicTimer = cinematicTimer + dt
        apply_intro_ortho_transform(0.0, VIEW_YAW)

        if cinematicTimer >= FADE_IN_DURATION then
            cinematicPhase = 0
            cinematicTimer = 0.0
        end
    elseif cinematicPhase == 0 then
        sweepX = sweepX + direction * SWEEP_SPEED * dt

        if sweepX > SWEEP_RANGE then
            sweepX = SWEEP_RANGE
            direction = -1.0
        elseif sweepX < -SWEEP_RANGE then
            sweepX = -SWEEP_RANGE
            direction = 1.0
        end

        apply_intro_ortho_transform(sweepX, VIEW_YAW)
    end
end

function EndPlay()
    print("[IntroCam] EndPlay")

    clear_ui_handler()

    bIntroActive = false
    bStarted = false
    pendingStart = false

    introCam = nil
    gameCam = nil
    introActor = nil
    pc = nil
end