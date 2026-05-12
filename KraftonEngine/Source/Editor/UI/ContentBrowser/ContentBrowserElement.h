#pragma once
#include "Core/ClassTypes.h"
#include "Editor/UI/ContentBrowser/ContentBrowserContext.h"
#include "ContentItem.h"
#include <d3d11.h>
#include <shellapi.h>
#include <wrl/client.h>


class ContentBrowserElement : public std::enable_shared_from_this<ContentBrowserElement>
{
public:
	virtual ~ContentBrowserElement() = default;

	bool RenderSelectSpace(ContentBrowserContext& Context);
	virtual void Render(ContentBrowserContext& Context);
	virtual void RenderDetail() {};

	void SetContent(FContentItem InContent) { ContentItem = InContent; }
	std::wstring GetFileName() { return ContentItem.Path.filename(); }
	void StartRename(ContentBrowserContext& Context);

protected:
	virtual const char* GetDragItemType() { return "ParkSangHyeok"; }
	FString ToContentPath(const std::filesystem::path& Path);

protected:
	virtual FString GetDefaultIconPath() { return FallBackIconPath; }
	virtual Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetElementIcon(ContentBrowserContext& Context);
	void EnsureIcon(ContentBrowserContext& Context);

protected:
	virtual void OnLeftClicked(ContentBrowserContext& Context) { (void)Context; };
	virtual void OnDoubleLeftClicked(ContentBrowserContext& Context) { ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
	virtual void OnDrag(ContentBrowserContext& Context) { (void)Context; }
	virtual void OnRightClicked(ContentBrowserContext& Context);

private:
	FString EllipsisText(const FString& text, float maxWidth);

protected:
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Icon;
	FContentItem ContentItem;
	bool bIsSelected = false;

private:
	FString FallBackIconPath = "Asset/Editor/Icons/StartMerge_42x.png";
};

class ExpandableElement : public ContentBrowserElement
{
public:
	virtual void Render(ContentBrowserContext& Context) override;
	virtual void OnRightClicked(ContentBrowserContext& Context) override;

private:
	void DrawExpandButton(ContentBrowserContext& Context);
	void DrawExpandedPanel(ContentBrowserContext& Context);
	void DrawInternalElements(ContentBrowserContext& Context);

protected:
	bool bExpanded = false;
	TArray<std::shared_ptr<ContentBrowserElement>> InternalElements;
};

class DirectoryElement final : public ContentBrowserElement
{
public:
	virtual FString GetDefaultIconPath() override { return "Asset/Editor/Icons/Folder_Base_256x.png"; }
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override;
};

class SceneElement final : public ContentBrowserElement
{
public:
	virtual FString GetDefaultIconPath() override { return "Asset/Editor/Icons/World_64x.png"; }
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override;
};

class ObjectElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "ObjectContentItem"; }
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetElementIcon(ContentBrowserContext& Context) override;
};

class ImportedStaticMeshElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "StaticMeshContentItem"; }
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetElementIcon(ContentBrowserContext& Context) override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override { (void)Context; }
};

class ImportedSkeletalMeshElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "SkeletalMeshContentItem"; }
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetElementIcon(ContentBrowserContext& Context) override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override { (void)Context; }
};

class ImportableElement : public ExpandableElement
{
public:
	virtual void Render(ContentBrowserContext& Context) override;
	virtual void OnRightClicked(ContentBrowserContext& Context) override;
	bool IsImported();

protected:
	virtual void Import(ContentBrowserContext& Context) = 0;
	virtual bool HasImportedBinary() const { return false; }

private:
	bool bIsImported = false;
};

class FBXElement final : public ImportableElement
{
public:
	virtual const char* GetDragItemType() override { return "FBXContentItem"; }
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetElementIcon(ContentBrowserContext& Context) override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override;

protected:
	void Import(ContentBrowserContext& Context) override;
	bool HasImportedBinary() const override;
};

class PNGElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "PNGElement"; }
	virtual FString GetDefaultIconPath() override { return ToContentPath(ContentItem.Path); }
};

#include "Editor/UI/EditorMaterialInspector.h"
class MaterialElement final : public ContentBrowserElement
{
public:
	virtual void OnLeftClicked(ContentBrowserContext& Context) override;
	virtual const char* GetDragItemType() override { return "MaterialContentItem"; }
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetElementIcon(ContentBrowserContext& Context) override;
	virtual void RenderDetail() override;

private:
	FEditorMaterialInspector MaterialInspector;
};

class PrefabElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "PrefabContentItem"; }
};

class LuaScriptElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "LuaScriptContentItem"; }
};

class CurveElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "CurveContentItem"; }
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override;
};
