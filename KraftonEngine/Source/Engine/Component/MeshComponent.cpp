#include "Component/MeshComponent.h"

#include <cstdlib>
#include <cstring>
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(UMeshComponent, UPrimitiveComponent)
HIDE_FROM_COMPONENT_LIST(UMeshComponent)

static const FString EmptyMaterialSlotName = "None";

void UMeshComponent::InitializeMaterialSlots(const TArray<FMeshMaterial>& DefaultMaterials)
{
	OverrideMaterials.resize(DefaultMaterials.size());
	MaterialSlots.resize(DefaultMaterials.size());
	MaterialSlotNames.resize(DefaultMaterials.size());

	for (int32 i = 0; i < static_cast<int32>(DefaultMaterials.size()); ++i)
	{
		OverrideMaterials[i] = DefaultMaterials[i].MaterialInterface;
		MaterialSlotNames[i] = DefaultMaterials[i].MaterialSlotName;

		MaterialSlots[i].Path = OverrideMaterials[i]
			? OverrideMaterials[i]->GetAssetPathFileName()
			: "None";
	}
}

void UMeshComponent::ClearMaterialSlots()
{
	OverrideMaterials.clear();
	MaterialSlots.clear();
	MaterialSlotNames.clear();
}

void UMeshComponent::SetMaterial(int32 ElementIndex, UMaterial* InMaterial)
{
	if (ElementIndex < 0 || ElementIndex >= static_cast<int32>(OverrideMaterials.size()))
	{
		return;
	}

	OverrideMaterials[ElementIndex] = InMaterial;
	if (ElementIndex < static_cast<int32>(MaterialSlots.size()))
	{
		MaterialSlots[ElementIndex].Path = InMaterial
			? InMaterial->GetAssetPathFileName()
			: "None";
	}

	MarkProxyDirty(EDirtyFlag::Material);
}

UMaterial* UMeshComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex >= 0 && ElementIndex < static_cast<int32>(OverrideMaterials.size()))
	{
		return OverrideMaterials[ElementIndex];
	}
	return nullptr;
}

const FString& UMeshComponent::GetMaterialSlotName(int32 ElementIndex) const
{
	if (ElementIndex >= 0 && ElementIndex < static_cast<int32>(MaterialSlotNames.size()))
	{
		return MaterialSlotNames[ElementIndex];
	}
	return EmptyMaterialSlotName;
}

void UMeshComponent::AppendMaterialSlotProperties(TArray<FPropertyDescriptor>& OutProps)
{
	for (int32 i = 0; i < static_cast<int32>(MaterialSlots.size()); ++i)
	{
		FPropertyDescriptor Desc;
		Desc.Name = "Element " + std::to_string(i);
		Desc.Type = EPropertyType::MaterialSlot;
		Desc.ValuePtr = &MaterialSlots[i];
		OutProps.push_back(Desc);
	}
}

void UMeshComponent::RestoreOverrideMaterialsFromSlots()
{
	for (int32 i = 0; i < static_cast<int32>(MaterialSlots.size()) && i < static_cast<int32>(OverrideMaterials.size()); ++i)
	{
		const FString& MatPath = MaterialSlots[i].Path;
		OverrideMaterials[i] = (MatPath.empty() || MatPath == "None")
			? nullptr
			: FMaterialManager::Get().GetOrCreateMaterial(MatPath);
	}
}

void UMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	AppendMaterialSlotProperties(OutProps);
}

void UMeshComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (std::strncmp(PropertyName, "Element ", 8) != 0)
	{
		return;
	}

	const int32 Index = std::atoi(&PropertyName[8]);
	if (Index < 0 || Index >= static_cast<int32>(MaterialSlots.size()))
	{
		return;
	}

	const FString NewMatPath = MaterialSlots[Index].Path;
	if (NewMatPath == "None" || NewMatPath.empty())
	{
		SetMaterial(Index, nullptr);
	}
	else if (UMaterial* LoadedMat = FMaterialManager::Get().GetOrCreateMaterial(NewMatPath))
	{
		SetMaterial(Index, LoadedMat);
	}
}
