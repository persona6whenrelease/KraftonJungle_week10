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

	void SetIcon(ID3D11ShaderResourceView* InIcon);
	void AttachIcon(ID3D11ShaderResourceView* InIcon);
	void SetContent(FContentItem InContent) { ContentItem = InContent; }

	std::wstring GetFileName() { return ContentItem.Path.filename(); }

	void StartRename(ContentBrowserContext& Context);

protected:
	FString EllipsisText(const FString& text, float maxWidth);
	virtual const char* GetDragItemType() { return "ParkSangHyeok"; }
	virtual void BuildIcon(ContentBrowserContext& Context);
	virtual FString GetDefaultIconPath() const;
	void SetIconFromPackagePath(const FString& PackagePath);
	void EnsureIcon(ContentBrowserContext& Context);

	virtual void OnLeftClicked(ContentBrowserContext& Context) { (void)Context; };
	virtual void OnDoubleLeftClicked(ContentBrowserContext& Context) { ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
	virtual void OnDrag(ContentBrowserContext& Context) { (void)Context; }
	virtual void OnRightClicked(ContentBrowserContext& Context);

protected:
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Icon;
	FContentItem ContentItem;
	bool bIsSelected = false;
};

class ExpandableElement : public ContentBrowserElement
{
public:
	virtual void Render(ContentBrowserContext& Context) override;
	
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
	FString GetDefaultIconPath() const override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override;
};

class SceneElement final : public ContentBrowserElement
{
public:
	FString GetDefaultIconPath() const override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override;
};

class ObjectElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "ObjectContentItem"; }
	void BuildIcon(ContentBrowserContext& Context) override;
	FString GetDefaultIconPath() const override;
};

class ImportedStaticMeshElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "StaticMeshContentItem"; }
	void BuildIcon(ContentBrowserContext& Context) override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override { (void)Context; }
};

class ImportedSkeletalMeshElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "SkeletalMeshContentItem"; }
	void BuildIcon(ContentBrowserContext& Context) override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override { (void)Context; }
};

class ImportableElement : public ExpandableElement
{
public:
	virtual void OnRightClicked(ContentBrowserContext& Context) override;
	bool IsImported() const { return bIsImported; }

protected:
	virtual void Import(ContentBrowserContext& Context) = 0;

private:
	bool bIsImported = false;
};

class FBXElement final : public ImportableElement
{
public:
	virtual const char* GetDragItemType() override { return "FBXContentItem"; }
	void BuildIcon(ContentBrowserContext& Context) override;
	void OnDoubleLeftClicked(ContentBrowserContext& Context) override;

protected:
	void Import(ContentBrowserContext& Context) override;
};

class PNGElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "PNGElement"; }
	void BuildIcon(ContentBrowserContext& Context) override;
};

#include "Editor/UI/EditorMaterialInspector.h"
class MaterialElement final : public ContentBrowserElement
{
public:
	virtual void OnLeftClicked(ContentBrowserContext& Context) override;
	virtual const char* GetDragItemType() override { return "MaterialContentItem"; }
	void BuildIcon(ContentBrowserContext& Context) override;
	FString GetDefaultIconPath() const override;
	virtual void RenderDetail() override;

private:
	FEditorMaterialInspector MaterialInspector;
};

class PrefabElement final : public ContentBrowserElement
{
public:
	virtual const char* GetDragItemType() override { return "PrefabContentItem"; }
	FString GetDefaultIconPath() const override;
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
