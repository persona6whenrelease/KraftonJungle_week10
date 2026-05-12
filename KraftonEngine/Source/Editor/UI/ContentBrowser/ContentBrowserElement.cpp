#include "ContentBrowserElement.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/AActor.h"
#include "Materials/MaterialManager.h"
#include "Mesh/FBX/FBXSceneAsset.h"
#include "Mesh/MeshManager.h"
#include "Object/Object.h"
#include "Platform/Paths.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Snapshot/SnapShotRenderer.h"
#include "Resource/ResourceManager.h"
#include "Runtime/Engine.h"

namespace
{
	constexpr uint32 ContentBrowserSnapshotSize = 128;
	const char* MaterialPreviewSpherePath = "Data/BasicShape/Sphere.OBJ";

	std::filesystem::path ToProjectAssetPath(const FString& AssetPath)
	{
		std::filesystem::path Path(FPaths::ToWide(AssetPath));
		if (Path.is_absolute())
		{
			return Path;
		}
		return std::filesystem::path(FPaths::RootDir()) / Path;
	}

	bool PackageFileExists(const FString& PackagePath)
	{
		std::wstring DiskPath;
		FString ResolveError;
		if (!FPaths::TryResolvePackagePath(PackagePath, DiskPath, &ResolveError))
		{
			DiskPath = FPaths::ToWide(PackagePath);
		}

		const std::filesystem::path Path(DiskPath);
		return std::filesystem::exists(Path) && std::filesystem::is_regular_file(Path);
	}

	//Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> TakeOwnedSnapshot(ID3D11ShaderResourceView* Snapshot)
	//{
	//	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Result;
	//	Result.Attach(Snapshot);
	//	return Result;
	//}

	FD3DDevice* GetContentBrowserD3DDevice(ContentBrowserContext& Context)
	{
		if (Context.EditorEngine)
		{
			return &Context.EditorEngine->GetRenderer().GetFD3DDevice();
		}

		if (GEngine)
		{
			return &GEngine->GetRenderer().GetFD3DDevice();
		}

		return nullptr;
	}

	bool EnsureSnapshotRenderer(FD3DDevice& Device)
	{
		FSnapShotRenderer& Renderer = FSnapShotRenderer::Get();
		if (!Renderer.IsInitialized())
		{
			return Renderer.Initialize(Device, ContentBrowserSnapshotSize, ContentBrowserSnapshotSize);
		}

		if (Renderer.GetWidth() != ContentBrowserSnapshotSize || Renderer.GetHeight() != ContentBrowserSnapshotSize)
		{
			Renderer.Resize(ContentBrowserSnapshotSize, ContentBrowserSnapshotSize);
		}
		return true;
	}

	ID3D11ShaderResourceView* SnapshotActor(ContentBrowserContext& Context, AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		FD3DDevice* Device = GetContentBrowserD3DDevice(Context);
		if (!Device || !EnsureSnapshotRenderer(*Device))
		{
			return nullptr;
		}

		FSnapShotRenderer& Renderer = FSnapShotRenderer::Get();
		Renderer.DrawActor(Actor);
		return Renderer.GetSnapShot();
	}

	ID3D11ShaderResourceView* BuildStaticMeshSnapshot(ContentBrowserContext& Context, UStaticMesh* StaticMesh, UMaterial* Material = nullptr)
	{
		if (!StaticMesh)
		{
			return nullptr;
		}

		AActor* Actor = UObjectManager::Get().CreateObject<AActor>();
		if (!Actor)
		{
			return nullptr;
		}

		UStaticMeshComponent* MeshComponent = Actor->AddComponent<UStaticMeshComponent>();
		MeshComponent->SetStaticMesh(StaticMesh);
		if (Material)
		{
			MeshComponent->SetMaterial(0, Material);
		}

		ID3D11ShaderResourceView* Snapshot = SnapshotActor(Context, Actor);
		UObjectManager::Get().DestroyObject(Actor);
		return Snapshot;
	}

	ID3D11ShaderResourceView* BuildStaticMeshSnapshot(ContentBrowserContext& Context, const FString& MeshPath)
	{
		FD3DDevice* Device = GetContentBrowserD3DDevice(Context);
		ID3D11Device* D3DDevice = Device ? Device->GetDevice() : nullptr;
		UStaticMesh* StaticMesh = FMeshManager::LoadStaticMesh(MeshPath, D3DDevice);
		return BuildStaticMeshSnapshot(Context, StaticMesh);
	}

	ID3D11ShaderResourceView* BuildSkeletalMeshSnapshot(ContentBrowserContext& Context, USkeletalMesh* SkeletalMesh)
	{
		if (!SkeletalMesh)
		{
			return nullptr;
		}

		AActor* Actor = UObjectManager::Get().CreateObject<AActor>();
		if (!Actor)
		{
			return nullptr;
		}

		USkeletalMeshComponent* MeshComponent = Actor->AddComponent<USkeletalMeshComponent>();
		MeshComponent->SetSkeletalMesh(SkeletalMesh);

		ID3D11ShaderResourceView* Snapshot = SnapshotActor(Context, Actor);
		UObjectManager::Get().DestroyObject(Actor);
		return Snapshot;
	}

	ID3D11ShaderResourceView* BuildSkeletalMeshSnapshot(ContentBrowserContext& Context, const FString& MeshPath)
	{
		USkeletalMesh* SkeletalMesh = FMeshManager::LoadSkeletalMesh(MeshPath);
		return BuildSkeletalMeshSnapshot(Context, SkeletalMesh);
	}

	ID3D11ShaderResourceView* BuildMaterialSnapshot(ContentBrowserContext& Context, const FString& MaterialPath)
	{
		FD3DDevice* Device = GetContentBrowserD3DDevice(Context);
		ID3D11Device* D3DDevice = Device ? Device->GetDevice() : nullptr;
		UStaticMesh* SphereMesh = FMeshManager::LoadStaticMesh(MaterialPreviewSpherePath, D3DDevice);
		UMaterial* Material = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
		return BuildStaticMeshSnapshot(Context, SphereMesh, Material);
	}

	ID3D11ShaderResourceView* BuildFbxSceneSnapshot(ContentBrowserContext& Context, UFBXSceneAsset* SceneAsset)
	{
		if (!SceneAsset)
		{
			return nullptr;
		}

		AActor* Actor = UObjectManager::Get().CreateObject<AActor>();
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* RootComponent = Actor->AddComponent<USceneComponent>();
		Actor->SetRootComponent(RootComponent);

		for (const FFBXSceneComponentDesc& Desc : SceneAsset->GetSceneComponents())
		{
			USceneComponent* AddedSceneComponent = nullptr;

			if (Desc.Type == EFBXSceneComponentType::StaticMesh)
			{
				UStaticMesh* StaticMesh = SceneAsset->FindStaticMeshBySourceMeshId(Desc.SourceMeshId);
				if (!StaticMesh)
				{
					continue;
				}

				UStaticMeshComponent* MeshComponent = Actor->AddComponent<UStaticMeshComponent>();
				MeshComponent->SetStaticMesh(StaticMesh);
				AddedSceneComponent = MeshComponent;
			}
			else if (Desc.Type == EFBXSceneComponentType::SkeletalMesh)
			{
				USkeletalMesh* SkeletalMesh = SceneAsset->FindSkeletalMeshBySourceSkeletonId(Desc.SourceSkeletonId);
				if (!SkeletalMesh)
				{
					continue;
				}

				USkeletalMeshComponent* MeshComponent = Actor->AddComponent<USkeletalMeshComponent>();
				MeshComponent->SetSkeletalMesh(SkeletalMesh);
				AddedSceneComponent = MeshComponent;
			}

			if (!AddedSceneComponent)
			{
				continue;
			}

			AddedSceneComponent->AttachToComponent(RootComponent);
			//AddedSceneComponent->SetRelativeLocation(Desc.RelativeTransform.GetLocation());
			//AddedSceneComponent->SetRelativeRotation(Desc.RelativeTransform.ToQuat());
			//AddedSceneComponent->SetRelativeScale(Desc.RelativeTransform.GetScale());
		}

		ID3D11ShaderResourceView* Snapshot = SnapshotActor(Context, Actor);
		UObjectManager::Get().DestroyObject(Actor);
		return Snapshot;
	}

	void AddUniqueMaterialPath(
		const FMeshMaterial& Material,
		TSet<FString>& AddedMaterialPaths,
		TArray<FString>& OutMaterialPaths)
	{
		if (!Material.MaterialInterface)
		{
			return;
		}

		const FString& MaterialPath = Material.MaterialInterface->GetAssetPathFileName();
		if (MaterialPath.empty() || MaterialPath == "None")
		{
			return;
		}

		if (AddedMaterialPaths.find(MaterialPath) != AddedMaterialPaths.end())
		{
			return;
		}
		AddedMaterialPaths.insert(MaterialPath);
		OutMaterialPaths.push_back(MaterialPath);
	}

	void AddMaterialElement(
		const FString& MaterialPath,
		TArray<std::shared_ptr<ContentBrowserElement>>& OutElements)
	{
		const std::filesystem::path ProjectMaterialPath = ToProjectAssetPath(MaterialPath);

		FContentItem MaterialItem;
		MaterialItem.Path = ProjectMaterialPath;
		MaterialItem.Name = ProjectMaterialPath.filename().wstring();
		MaterialItem.bIsDirectory = false;

		std::shared_ptr<ContentBrowserElement> Element =
			std::static_pointer_cast<ContentBrowserElement>(std::make_shared<MaterialElement>());
		Element->SetContent(std::move(MaterialItem));
		OutElements.push_back(std::move(Element));
	}
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ContentBrowserElement::GetElementIcon(ContentBrowserContext& Context)
{
	(void)Context;
	return FResourceManager::Get().FindLoadedTexture(GetDefaultIconPath());
}

void ContentBrowserElement::EnsureIcon(ContentBrowserContext& Context)
{
	if (!Icon)
	{
		Icon = GetElementIcon(Context);
	}

	if (!Icon)
	{
		Icon = FResourceManager::Get().FindLoadedTexture(FallBackIconPath);
	}
}

bool ContentBrowserElement::RenderSelectSpace(ContentBrowserContext& Context)
{
	EnsureIcon(Context);

	FString Name = FPaths::ToUtf8(ContentItem.Name);
	ImGui::PushID(Name.c_str());

	bIsSelected = Context.SelectedElement.get() == this;

	bool bIsClicked = ImGui::Selectable("##Element", bIsSelected, 0, Context.ContentSize);

	ImVec2 Min = ImGui::GetItemRectMin();
	ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	ImFont* font = ImGui::GetFont();
	float fontSize = ImGui::GetFontSize();
	Max.y -= fontSize;
	Max.x -= fontSize * 0.5f;
	Min.x += fontSize * 0.5f;
	if (Icon)
	{
		DrawList->AddImage(Icon.Get(), Min, Max);
	}

	ImVec2 TextPos(Min.x, Max.y);

	if (bIsSelected && Context.bIsRenaming)
	{
		ImVec2 SavedScreenPos = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(TextPos);
		ImGui::PushItemWidth(Context.ContentSize.x);
		if (Context.bRenameFocusNeeded)
		{
			ImGui::SetKeyboardFocusHere();
			Context.bRenameFocusNeeded = false;
		}
		bool bConfirmed = ImGui::InputText("##RenameInput", Context.RenameBuffer, sizeof(Context.RenameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		bool bDeactivated = ImGui::IsItemDeactivated();
		ImGui::PopItemWidth();
		ImGui::SetCursorScreenPos(SavedScreenPos);

		if (bConfirmed)
		{
			std::wstring NewName = FPaths::ToWide(FString(Context.RenameBuffer));
			if (!NewName.empty() && NewName != ContentItem.Path.filename().wstring())
			{
				std::filesystem::path NewPath = ContentItem.Path.parent_path() / NewName;
				std::error_code ec;
				std::filesystem::rename(ContentItem.Path, NewPath, ec);
				if (!ec)
				{
					ContentItem.Path = NewPath;
					ContentItem.Name = NewName;
					Context.bIsNeedRefresh = true;
				}
			}
			Context.bIsRenaming = false;
		}
		else if (bDeactivated)
		{
			Context.bIsRenaming = false;
		}
	}
	else
	{
		FString Text = EllipsisText(FPaths::ToUtf8(ContentItem.Name), Context.ContentSize.x);
		DrawList->AddText(TextPos, ImGui::GetColorU32(ImGuiCol_Text), Text.c_str());
	}

	ImGui::PopID();

	return bIsClicked;
}

void ContentBrowserElement::Render(ContentBrowserContext& Context)
{
	if (RenderSelectSpace(Context))
	{
		Context.SelectedElement = shared_from_this();
		bIsSelected = true;
		OnLeftClicked(Context);
	}

	bool bDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	if (bDoubleClicked && !Context.bIsRenaming)
	{
		OnDoubleLeftClicked(Context);
	}

	if (ImGui::BeginPopupContextItem())
	{
		OnRightClicked(Context);
		ImGui::EndPopup();
	}

	if (!Context.bIsRenaming && ImGui::BeginDragDropSource())
	{
		RenderSelectSpace(Context);
		ImGui::SetDragDropPayload(GetDragItemType(), &ContentItem, sizeof(ContentItem));
		OnDrag(Context);
		ImGui::EndDragDropSource();
	}
}

void ContentBrowserElement::StartRename(ContentBrowserContext& Context)
{
	Context.bIsRenaming = true;
	Context.bRenameFocusNeeded = true;
	FString CurrentName = FPaths::ToUtf8(ContentItem.Path.filename().wstring());
	strncpy_s(Context.RenameBuffer, sizeof(Context.RenameBuffer), CurrentName.c_str(), _TRUNCATE);
}

FString ContentBrowserElement::EllipsisText(const FString& text, float maxWidth)
{
	ImFont* font = ImGui::GetFont();
	float fontSize = ImGui::GetFontSize();

	if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
		return text;

	const char* ellipsis = "...";
	float ellipsisWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ellipsis).x;

	std::string result = text;

	while (!result.empty())
	{
		result.pop_back();

		float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, result.c_str()).x;
		if (w + ellipsisWidth <= maxWidth)
		{
			result += ellipsis;
			break;
		}
	}

	return result;
}

void ContentBrowserElement::OnRightClicked(ContentBrowserContext& Context)
{
	bIsSelected = true;
	if (ImGui::MenuItem("Rename"))
	{
		Context.SelectedElement = shared_from_this();
		StartRename(Context);
	}
}

void DirectoryElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	Context.CurrentPath = ContentItem.Path;
	Context.PendingRevealPath = ContentItem.Path;
	Context.bIsNeedRefresh = true;
}

FString ContentBrowserElement::ToContentPath(const std::filesystem::path& Path)
{
	const std::filesystem::path NormalizedPath = Path.lexically_normal();
	const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
	const std::filesystem::path RelativePath = NormalizedPath.lexically_relative(RootPath);

	if (!RelativePath.empty())
	{
		bool bParentReference = false;
		for (const std::filesystem::path& Part : RelativePath)
		{
			if (Part == L"..")
			{
				bParentReference = true;
				break;
			}
		}

		if (!bParentReference)
		{
			return FPaths::ToUtf8(RelativePath.generic_wstring());
		}
	}

	return FPaths::ToUtf8(NormalizedPath.generic_wstring());
}

#include "Serialization/SceneSaveManager.h"
void SceneElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	std::filesystem::path ScenePath = ContentItem.Path;
	FString FilePath = FPaths::ToUtf8(ScenePath.wstring());
	UEditorEngine* EditorEngine = Context.EditorEngine;
	EditorEngine->LoadSceneFromPath(FilePath);
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ObjectElement::GetElementIcon(ContentBrowserContext& Context)
{
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Result;
	Result.Attach(BuildStaticMeshSnapshot(Context, FPaths::ToUtf8(ContentItem.Path.wstring())));
	return Result;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ImportedStaticMeshElement::GetElementIcon(ContentBrowserContext& Context)
{
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Result;
	Result.Attach(BuildStaticMeshSnapshot(Context, FPaths::ToUtf8(ContentItem.Path.wstring())));
	return Result;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ImportedSkeletalMeshElement::GetElementIcon(ContentBrowserContext& Context)
{
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Result;
	Result.Attach(BuildSkeletalMeshSnapshot(Context, FPaths::ToUtf8(ContentItem.Path.wstring())));
	return Result;
}

void FBXElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		return;
	}

	Context.EditorEngine->OpenSkeletalMeshViewerAsset(FPaths::ToUtf8(ContentItem.Path.wstring()));
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> FBXElement::GetElementIcon(ContentBrowserContext& Context)
{
	if (!HasImportedBinary())
	{
		return ContentBrowserElement::GetElementIcon(Context);
	}

	const FString FbxPath = FPaths::ToUtf8(ContentItem.Path.wstring());
	UFBXSceneAsset* SceneAsset = FMeshManager::LoadFbxScene(FbxPath);

	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Result;
	Result.Attach(BuildFbxSceneSnapshot(Context, SceneAsset));
	return Result;
}

bool FBXElement::HasImportedBinary() const
{
	const FString FbxPath = FPaths::ToUtf8(ContentItem.Path.wstring());
	const FString CachePath = FMeshManager::GetFbxSceneCacheFilePath(FbxPath);
	return PackageFileExists(CachePath);
}

void FBXElement::Import(ContentBrowserContext& Context)
{
	(void)Context;
	const FString FbxPath = FPaths::ToUtf8(ContentItem.Path.wstring());
	UFBXSceneAsset* SceneAsset = FMeshManager::LoadFbxScene(FbxPath);
	if (!SceneAsset)
	{
		return;
	}

	TSet<FString> AddedMaterialPaths;
	TArray<FString> MaterialPaths;

	for (const FFBXSceneComponentDesc& Desc : SceneAsset->GetSceneComponents())
	{
		const bool bStaticMesh = Desc.Type == EFBXSceneComponentType::StaticMesh;
		const bool bSkeletalMesh = Desc.Type == EFBXSceneComponentType::SkeletalMesh;
		if (!bStaticMesh && !bSkeletalMesh)
		{
			continue;
		}

		const int32 SourceId = bStaticMesh ? Desc.SourceMeshId : Desc.SourceSkeletonId;
		if (SourceId < 0)
		{
			continue;
		}

		const FString Prefix = bStaticMesh ? "#Mesh_" : "#Skeleton_";
		const FString FallbackName = bStaticMesh ? "Mesh_" : "Skeleton_";
		const FString ItemName = Desc.Name.empty()
			? FallbackName + std::to_string(SourceId)
			: Desc.Name;

		FContentItem ImportedItem;
		ImportedItem.Path = FPaths::ToWide(FbxPath + Prefix + std::to_string(SourceId));
		ImportedItem.Name = FPaths::ToWide(ItemName);
		ImportedItem.bIsDirectory = false;

		std::shared_ptr<ContentBrowserElement> Element = bStaticMesh
			? std::static_pointer_cast<ContentBrowserElement>(std::make_shared<ImportedStaticMeshElement>())
			: std::static_pointer_cast<ContentBrowserElement>(std::make_shared<ImportedSkeletalMeshElement>());
		Element->SetContent(std::move(ImportedItem));
		InternalElements.push_back(std::move(Element));

		if (bStaticMesh)
		{
			const TArray<UStaticMesh*>& StaticMeshes = SceneAsset->GetStaticMeshes();
			if (Desc.StaticMeshAssetIndex >= 0 &&
				Desc.StaticMeshAssetIndex < static_cast<int32>(StaticMeshes.size()) &&
				StaticMeshes[Desc.StaticMeshAssetIndex])
			{
				for (const FStaticMaterial& Material : StaticMeshes[Desc.StaticMeshAssetIndex]->GetStaticMaterials())
				{
					AddUniqueMaterialPath(Material, AddedMaterialPaths, MaterialPaths);
				}
			}
		}
		else
		{
			const TArray<USkeletalMesh*>& SkeletalMeshes = SceneAsset->GetSkeletalMeshes();
			if (Desc.SkeletalMeshAssetIndex >= 0 &&
				Desc.SkeletalMeshAssetIndex < static_cast<int32>(SkeletalMeshes.size()) &&
				SkeletalMeshes[Desc.SkeletalMeshAssetIndex])
			{
				for (const FMeshMaterial& Material : SkeletalMeshes[Desc.SkeletalMeshAssetIndex]->GetMaterials())
				{
					AddUniqueMaterialPath(Material, AddedMaterialPaths, MaterialPaths);
				}
			}
		}
	}

	for (const FString& MaterialPath : MaterialPaths)
	{
		AddMaterialElement(MaterialPath, InternalElements);
	}

	//bExpanded = !InternalElements.empty();
}

void MaterialElement::OnLeftClicked(ContentBrowserContext& Context)
{
	MaterialInspector = { ContentItem.Path };
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> MaterialElement::GetElementIcon(ContentBrowserContext& Context)
{
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Result;
	Result.Attach(BuildMaterialSnapshot(Context, ToContentPath(ContentItem.Path)));
	return Result;
}

void MaterialElement::RenderDetail()
{
	MaterialInspector.Render();
}

void CurveElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	ContentBrowserElement::OnDoubleLeftClicked(Context);

	if (Context.EditorEngine)
	{
		Context.EditorEngine->OpenCurveAsset(FPaths::ToUtf8(ContentItem.Path.wstring()));
	}
}

void ExpandableElement::Render(ContentBrowserContext& Context)
{
	ContentBrowserElement::Render(Context);

	DrawExpandButton(Context);
	Context.AdvanceContentGridSlot();

	if (bExpanded)
	{
		DrawExpandedPanel(Context);
	}
}

void ExpandableElement::OnRightClicked(ContentBrowserContext& Context)
{
	if (ImGui::MenuItem(bExpanded ? "Collapse" : "Expand"))
	{
		bExpanded = !bExpanded;
	}
}

void ExpandableElement::DrawExpandButton(ContentBrowserContext& Context)
{
	ImVec2 TileMin = ImGui::GetItemRectMin();
	ImVec2 TileMax = ImGui::GetItemRectMax();

	const float ButtonSize = 18.0f;

	ImGui::SetCursorScreenPos(ImVec2(
		TileMax.x - ButtonSize - 2.0f,
		TileMin.y + 2.0f
	));

	FString Name = FPaths::ToUtf8(ContentItem.Name) + "ExpandButton";
	ImGui::PushID(Name.c_str());

	if (ImGui::SmallButton(bExpanded ? "v" : ">"))
	{
		bExpanded = !bExpanded;

		//if (bExpanded)
		//{
		//	OnExpanded(Context);
		//}
		//else
		//{
		//	OnCollapsed(Context);
		//}
	}

	ImGui::PopID();
}

void ExpandableElement::DrawExpandedPanel(ContentBrowserContext& Context)
{
	if (InternalElements.empty())
	{
		return;
	}

	DrawInternalElements(Context);
}

void ExpandableElement::DrawInternalElements(ContentBrowserContext& Context)
{
	for (int32 Index = 0; Index < static_cast<int32>(InternalElements.size()); ++Index)
	{
		Context.MoveToContentGridSlot();
		InternalElements[Index]->Render(Context);
		if (!Context.bContentGridSlotConsumed)
		{
			Context.AdvanceContentGridSlot();
		}
	}
}

void ImportableElement::Render(ContentBrowserContext& Context)
{
	if(InternalElements.size() <= 0 && bIsImported)
		Import(Context);

	ExpandableElement::Render(Context);
}

void ImportableElement::OnRightClicked(ContentBrowserContext& Context)
{
	ContentBrowserElement::OnRightClicked(Context);

	if (IsImported())
	{
		ExpandableElement::OnRightClicked(Context);
	}
	else if (ImGui::MenuItem("Import"))
		{
			InternalElements.clear();
			Import(Context);

			bIsImported = HasImportedBinary();
			Icon.Reset();
		}
}

bool ImportableElement::IsImported()
{
	if (bIsImported)
		return true;

	bIsImported = HasImportedBinary();
	return bIsImported;
}
