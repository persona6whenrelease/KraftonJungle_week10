#pragma once

#include "Component/PrimitiveComponent.h"
#include "Core/PropertyTypes.h"
#include "Mesh/MeshCommonTypes.h"

class UMaterial;

class UMeshComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UMeshComponent, UPrimitiveComponent)

	UMeshComponent() = default;
	~UMeshComponent() override = default;

	void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
	UMaterial* GetMaterial(int32 ElementIndex) const;
	const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }
	const TArray<FMaterialSlot>& GetMaterialSlots() const { return MaterialSlots; }
	const FString& GetMaterialSlotName(int32 ElementIndex) const;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

protected:
	void InitializeMaterialSlots(const TArray<FMeshMaterial>& DefaultMaterials);
	void ClearMaterialSlots();
	void AppendMaterialSlotProperties(TArray<FPropertyDescriptor>& OutProps);
	void RestoreOverrideMaterialsFromSlots();

	TArray<UMaterial*> OverrideMaterials;
	TArray<FMaterialSlot> MaterialSlots;
	TArray<FString> MaterialSlotNames;
};
