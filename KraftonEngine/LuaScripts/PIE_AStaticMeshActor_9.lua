function BeginPlay()
    print("[BeginPlay] UUID = " .. obj.UUID)
    obj:PrintLocation()
    
    WarmUpActorPool("AStaticMeshActor", 100)

end

function EndPlay()
    print("[EndPlay] " .. obj.UUID)
    obj:PrintLocation()
end

function OnOverlap(OtherActor)
    OtherActor:PrintLocation()
end
