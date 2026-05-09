#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class USkeletalMeshComponent;

/**
 * FSkeletalMeshSceneProxy - Scene proxy for USkeletalMeshComponent.
 * Handles CPU skinned vertex data and uploads to GPU dynamic buffer.
 */
class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
    FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
    virtual ~FSkeletalMeshSceneProxy() override;

    // FPrimitiveSceneProxy interface
    void UpdateMesh() override;
    void UpdateMaterial() override;

    // Getters for internal buffers (DrawCommandBuilder might need these)
    const FDynamicVertexBuffer& GetInternalVB() const { return InternalVB; }
    const FIndexBuffer& GetInternalIB() const { return InternalIB; }

private:
    USkeletalMeshComponent* GetSkeletalMeshComponent() const;

    /** Dynamic vertex buffer for CPU skinned vertices. */
    FDynamicVertexBuffer InternalVB;

    /** Static index buffer (mirrored from asset). */
    FIndexBuffer InternalIB;
};
