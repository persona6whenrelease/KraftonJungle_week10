function BeginPlay()
    print("[BeginPlay] UUID = " .. obj.UUID)
    obj:PrintLocation()

end

function EndPlay()
    print("[EndPlay] " .. obj.UUID)
    obj:PrintLocation()
end

function OnOverlap(OtherActor)
    OtherActor:PrintLocation()
end
