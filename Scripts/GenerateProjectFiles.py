"""
GenerateProjectFiles.py — Auto-generate Visual Studio project files for KraftonEngine.

This generator preserves the engine/game split:

    KraftonEngine.vcxproj  -> application target: Engine + Editor/ObjViewer/GameClient
    CrossyGame.vcxproj     -> static library target: Source/Games/Crossy only

The main project references CrossyGame only for GameClient|x64, so regenerating project
files no longer pulls Source/Games/Crossy back into the engine/application project.

Usage:
    python Scripts/GenerateProjectFiles.py
"""

from __future__ import annotations

import hashlib
import os
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

# ──────────────────────────────────────────────
# Constants
# ──────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent.parent

PROJECT_NAME = "KraftonEngine"
CROSSY_PROJECT_NAME = "CrossyGame"

# Supports both layouts:
#   repo/KraftonEngine/Source/...
#   repo/Source/...
def resolve_project_dir() -> Path:
    nested = ROOT / PROJECT_NAME
    if (nested / "Source").exists():
        return nested
    if (ROOT / "Source").exists():
        return ROOT
    return nested

PROJECT_DIR = resolve_project_dir()
PROJECT_REL_DIR = PROJECT_DIR.relative_to(ROOT) if PROJECT_DIR != ROOT else Path(".")

PROJECT_GUID = "{55068e81-c0a0-49f9-ab7b-54aea968722b}"
CROSSY_PROJECT_GUID = "{a6df3d49-15ce-49c6-b594-d9b89de3c83b}"
ROOT_NAMESPACE = "Week2"
CROSSY_ROOT_NAMESPACE = "CrossyGame"

SOLUTION_GUID = "{4EBC5DD2-CECA-4722-9D19-87C7CB5F481B}"
VS_PROJECT_TYPE = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"

CONFIGURATIONS = [
    ("Debug", "Win32"),
    ("Release", "Win32"),
    ("Debug", "x64"),
    ("Release", "x64"),
    ("ObjViewDebug", "x64"),
    ("Demo", "x64"),
    ("GameClient", "x64"),
]

CROSSY_LINK_CONFIGURATIONS = {
    ("Debug", "x64"),
    ("Release", "x64"),
    ("GameClient", "x64"),
}

# Per-configuration overrides for the application project
CONFIG_PROPS = {
    "ObjViewDebug": {
        "release_like": True,
        "with_editor": False,
        "is_obj_viewer": True,
        "is_game_client": False,
    },
    "Demo": {
        "release_like": True,
        "extra_defines": ["STATS=0"],
    },
    "GameClient": {
        "release_like": True,
        "with_editor": False,
        "is_obj_viewer": False,
        "is_game_client": True,
        "extra_defines": ["STATS=0"],
    },
}

# Crossy is built as a game runtime module. Keep its defines stable across configs;
# the executable decides whether it links/loads this module.
CROSSY_CONFIG_PROPS = {
    cfg: {
        "release_like": CONFIG_PROPS.get(cfg, {}).get("release_like", cfg == "Release"),
        "with_editor": False,
        "is_obj_viewer": False,
        "is_game_client": True,
        "extra_defines": ["STATS=0"],
    }
    for cfg, _ in CONFIGURATIONS
}

# Directories to recursively scan for the main application project.
ENGINE_SCAN_DIRS = ["Source", "ThirdParty"]
ENGINE_EXCLUDE_PREFIXES = [
    "Source\\Games\\Crossy",
    # Legacy pool files were renamed to ActorPoolSystem.
    # Keep them out of regenerated projects even if stale files remain on disk.
    "Source\\Engine\\Runtime\\ObjectPoolSystem.cpp",
    "Source\\Engine\\Runtime\\ObjectPoolSystem.h",
]

# Files removed by the refactor. Delete them before scanning so local stale copies
# cannot be pulled back into the generated project or cause duplicate definitions.
OBSOLETE_FILES = [
    "Source\\Engine\\Runtime\\ObjectPoolSystem.cpp",
    "Source\\Engine\\Runtime\\ObjectPoolSystem.h",
]

# Directories to recursively scan for the Crossy static library project.
CROSSY_SCAN_DIRS = ["Source\\Games\\Crossy"]

# Directories to scan for shader files. Shaders stay with the main project.
SHADER_DIRS = ["Shaders"]

SOURCE_EXTS = {".cpp", ".c", ".cc", ".cxx"}
HEADER_EXTS = {".h", ".hpp", ".hxx", ".inl"}
SHADER_EXTS = {".hlsl", ".hlsli"}
NATVIS_EXTS = {".natvis"}
NONE_EXTS = {".natstepfilter", ".config", ".props"}

ROOT_FILES = ["main.cpp"]
ROOT_NONE_FILES = ["VcpkgLua.props"]

# Include paths for the application project.
INCLUDE_PATHS = [
    "Source\\Engine",
    "Source",
    "ThirdParty",
    "ThirdParty\\ImGui",
    "Source\\Editor",
    "Source\\ObjViewer",
    "Source\\GameClient",
    ".",
]

# Include paths for the Crossy module. Do not add Source/Games/Crossy as a
# required include path; includes should continue to go through Source/, e.g.
#   #include "Games/Crossy/CrossyGameModule.h"
CROSSY_INCLUDE_PATHS = [
    "Source\\Engine",
    "Source",
    "ThirdParty",
    "ThirdParty\\ImGui",
    ".",
]

LIBRARY_PATHS: list[str] = []

# ──────────────────────────────────────────────
# SFML configuration — application project only
# ──────────────────────────────────────────────
SFML_MODULES = ["audio", "window", "system"]
SFML_LIB_BASE = "$(ProjectDir)ThirdParty\\SFML\\lib"
SFML_BIN_BASE = "$(ProjectDir)ThirdParty\\SFML\\bin"
SFML_CONFIG_FLAVOR = {
    "Debug": "Debug",
    "Release": "Release",
    "ObjViewDebug": "Release",
    "Demo": "Release",
    "GameClient": "Release",
}
SFML_PLATFORMS = {"x64"}

# Lua / vcpkg property sheet
USE_VCPKG_LUA_PROPS = True
VCPKG_LUA_PROPS_FILE = "VcpkgLua.props"
VCPKG_LUA_PROPS = f"$(MSBuildProjectDirectory)\\{VCPKG_LUA_PROPS_FILE}"
VCPKG_LUA_PLATFORMS = {"x64"}
GENERATE_VCPKG_LUA_PROPS = True

# NuGet packages — application project only
NUGET_PACKAGES = [
    ("directxtk_desktop_win10", "2025.10.28.2"),
]

NS = "http://schemas.microsoft.com/developer/msbuild/2003"


# ──────────────────────────────────────────────
# Data structures
# ──────────────────────────────────────────────
@dataclass(frozen=True)
class ProjectReference:
    include: str
    guid: str
    condition: str | None = None


# ──────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────
def normalize_rel(path: Path) -> str:
    return str(path).replace("/", "\\")


def should_exclude(rel_str: str, exclude_prefixes: Iterable[str]) -> bool:
    rel_norm = rel_str.replace("/", "\\")
    return any(
        rel_norm == prefix or rel_norm.startswith(prefix.rstrip("\\") + "\\")
        for prefix in exclude_prefixes
    )


def scan_files(
    project_dir: Path,
    *,
    scan_dirs: list[str],
    exclude_prefixes: list[str] | None = None,
    include_shaders: bool = False,
    root_files: list[str] | None = None,
    root_none_files: list[str] | None = None,
    include_resources: bool = False,
) -> dict[str, list[str]]:
    """Scan directories and collect files grouped by MSBuild item type."""
    exclude_prefixes = exclude_prefixes or []
    root_files = root_files or []
    root_none_files = root_none_files or []

    result = {
        "ClCompile": [],
        "ClInclude": [],
        "FxCompile": [],
        "ResourceCompile": [],
        "Natvis": [],
        "None": [],
    }

    for scan_dir in scan_dirs:
        full_dir = project_dir / Path(scan_dir.replace("\\", os.sep))
        if not full_dir.exists():
            continue

        for dirpath, _, filenames in os.walk(full_dir):
            for fname in sorted(filenames):
                full = Path(dirpath) / fname
                rel = full.relative_to(project_dir)
                rel_str = normalize_rel(rel)
                if should_exclude(rel_str, exclude_prefixes):
                    continue

                ext = full.suffix.lower()
                if ext in SOURCE_EXTS:
                    result["ClCompile"].append(rel_str)
                elif ext in HEADER_EXTS:
                    result["ClInclude"].append(rel_str)
                elif ext in NATVIS_EXTS:
                    result["Natvis"].append(rel_str)
                elif ext in NONE_EXTS:
                    result["None"].append(rel_str)

    if include_shaders:
        for shader_dir in SHADER_DIRS:
            full_dir = project_dir / shader_dir
            if not full_dir.exists():
                continue

            for dirpath, _, filenames in os.walk(full_dir):
                for fname in sorted(filenames):
                    full = Path(dirpath) / fname
                    rel = full.relative_to(project_dir)
                    rel_str = normalize_rel(rel)
                    ext = full.suffix.lower()
                    if ext in SHADER_EXTS:
                        result["FxCompile"].append(rel_str)

    for root_file in root_files:
        full = project_dir / root_file
        if full.exists():
            result["ClCompile"].append(root_file.replace("/", "\\"))

    for root_none_file in root_none_files:
        full = project_dir / root_none_file
        if full.exists():
            result["None"].append(root_none_file.replace("/", "\\"))

    if include_resources:
        for f in sorted(project_dir.glob("*.rc")):
            result["ResourceCompile"].append(f.name)

    for key in result:
        result[key] = sorted(set(result[key]))

    return result


def get_filter(rel_path: str) -> str:
    parts = rel_path.replace("/", "\\").rsplit("\\", 1)
    return parts[0] if len(parts) > 1 else ""


def collect_all_filters(files: dict[str, list[str]]) -> set[str]:
    filters = set()

    for file_list in files.values():
        for f in file_list:
            filt = get_filter(f)
            if not filt:
                continue
            parts = filt.split("\\")
            for i in range(1, len(parts) + 1):
                filters.add("\\".join(parts[:i]))

    return filters


def sfml_flavor_for(cfg: str, plat: str) -> str | None:
    if plat not in SFML_PLATFORMS:
        return None
    return SFML_CONFIG_FLAVOR.get(cfg)


def sfml_lib_names(flavor: str) -> list[str]:
    suffix = "-d" if flavor == "Debug" else ""
    return [f"sfml-{m}{suffix}.lib" for m in SFML_MODULES]


def sfml_post_build_command(flavor: str) -> str:
    src = f"{SFML_BIN_BASE}\\{flavor}\\*.dll"
    return f'xcopy /Y /D /I "{src}" "$(OutDir)"'


def project_include_path(path: Path) -> str:
    rel = path.relative_to(ROOT)
    return normalize_rel(rel)


# ──────────────────────────────────────────────
# XML helpers
# ──────────────────────────────────────────────
def indent_xml(elem, level=0):
    i = "\n" + "  " * level

    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "

        if not elem.tail or not elem.tail.strip():
            elem.tail = i

        for child in elem:
            indent_xml(child, level + 1)

        if not child.tail or not child.tail.strip():
            child.tail = i
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = i

    if level == 0:
        elem.tail = "\n"


def write_xml(root_elem, filepath: Path, bom: bool = False):
    indent_xml(root_elem)
    tree = ET.ElementTree(root_elem)

    with open(filepath, "w", encoding="utf-8", newline="\r\n") as f:
        if bom:
            f.write("\ufeff")
        f.write('<?xml version="1.0" encoding="utf-8"?>\n')
        tree.write(f, encoding="unicode", xml_declaration=False)


# ──────────────────────────────────────────────
# Vcpkg property sheet
# ──────────────────────────────────────────────
def generate_vcpkg_lua_props():
    props_path = PROJECT_DIR / VCPKG_LUA_PROPS_FILE

    content = r'''<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <VcpkgTriplet>x64-windows</VcpkgTriplet>
    <VcpkgInstalledRoot>$(MSBuildProjectDirectory)\..\vcpkg_installed\$(VcpkgTriplet)</VcpkgInstalledRoot>

    <!-- Debug uses vcpkg debug libs/dlls. All release-like custom configs use release. -->
    <VcpkgLibDir Condition="'$(Configuration)'=='Debug'">$(VcpkgInstalledRoot)\debug\lib</VcpkgLibDir>
    <VcpkgBinDir Condition="'$(Configuration)'=='Debug'">$(VcpkgInstalledRoot)\debug\bin</VcpkgBinDir>
    <VcpkgLibDir Condition="'$(VcpkgLibDir)'==''">$(VcpkgInstalledRoot)\lib</VcpkgLibDir>
    <VcpkgBinDir Condition="'$(VcpkgBinDir)'==''">$(VcpkgInstalledRoot)\bin</VcpkgBinDir>

    <!-- vcpkg's RmlUi port currently installs lowercase rmlui.lib/rmlui.dll. -->
    <RmlUiLibraryName Condition="Exists('$(VcpkgLibDir)\rmlui.lib')">rmlui.lib</RmlUiLibraryName>
    <RmlUiLibraryName Condition="'$(RmlUiLibraryName)'=='' And Exists('$(VcpkgLibDir)\RmlUi.lib')">RmlUi.lib</RmlUiLibraryName>
    <RmlUiLibraryName Condition="'$(RmlUiLibraryName)'=='' And Exists('$(VcpkgLibDir)\RmlCore.lib')">RmlCore.lib</RmlUiLibraryName>

    <!-- RmlUi code is compiled only when the import library is actually present. -->
    <WithRmlUiDefine Condition="'$(RmlUiLibraryName)'!=''">WITH_RMLUI=1</WithRmlUiDefine>
    <WithRmlUiDefine Condition="'$(RmlUiLibraryName)'==''">WITH_RMLUI=0</WithRmlUiDefine>
  </PropertyGroup>

  <ItemDefinitionGroup Condition="'$(Platform)'=='x64'">
    <ClCompile>
      <AdditionalIncludeDirectories>
        $(VcpkgInstalledRoot)\include\luajit;
        $(VcpkgInstalledRoot)\include;
        %(AdditionalIncludeDirectories)
      </AdditionalIncludeDirectories>
      <PreprocessorDefinitions>
        SOL_ALL_SAFETIES_ON=1;
        SOL_LUAJIT=1;
        $(WithRmlUiDefine);
        %(PreprocessorDefinitions)
      </PreprocessorDefinitions>
    </ClCompile>

    <Link>
      <AdditionalLibraryDirectories>
        $(VcpkgLibDir);
        %(AdditionalLibraryDirectories)
      </AdditionalLibraryDirectories>
      <AdditionalDependencies>
        lua51.lib;
        $(RmlUiLibraryName);
        %(AdditionalDependencies)
      </AdditionalDependencies>
    </Link>
  </ItemDefinitionGroup>

  <!-- Do not use PostBuildEvent here. The .vcxproj also emits an SFML PostBuildEvent;
       this standalone target runs in addition to it and copies DLLs to the actual exe dir. -->
  <Target Name="CopyVcpkgRuntimeDlls"
          AfterTargets="Build"
          Condition="'$(Platform)'=='x64' And Exists('$(VcpkgBinDir)')">
    <ItemGroup>
      <VcpkgRuntimeDll Include="$(VcpkgBinDir)\*.dll" />
    </ItemGroup>
    <Message Importance="High"
             Text="Copying vcpkg runtime DLLs from $(VcpkgBinDir) to $(TargetDir)" />
    <Copy SourceFiles="@(VcpkgRuntimeDll)"
          DestinationFolder="$(TargetDir)"
          SkipUnchangedFiles="true" />
  </Target>
</Project>
'''

    with open(props_path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(content)


# ──────────────────────────────────────────────
# .vcxproj generation
# ──────────────────────────────────────────────
def generate_vcxproj(
    *,
    name: str,
    guid: str,
    root_namespace: str,
    files: dict[str, list[str]],
    output_path: Path,
    configuration_type: str,
    config_props: dict[str, dict],
    include_paths: list[str],
    int_dir_prefix: str | None = None,
    application_project: bool = False,
    static_library_project: bool = False,
    include_sfml: bool = False,
    include_nuget: bool = False,
    project_references: list[ProjectReference] | None = None,
):
    project_references = project_references or []

    proj = ET.Element("Project", DefaultTargets="Build", xmlns=NS)

    # ProjectConfigurations
    ig = ET.SubElement(proj, "ItemGroup", Label="ProjectConfigurations")
    for cfg, plat in CONFIGURATIONS:
        pc = ET.SubElement(ig, "ProjectConfiguration", Include=f"{cfg}|{plat}")
        ET.SubElement(pc, "Configuration").text = cfg
        ET.SubElement(pc, "Platform").text = plat

    # Globals
    pg = ET.SubElement(proj, "PropertyGroup", Label="Globals")
    ET.SubElement(pg, "VCProjectVersion").text = "17.0"
    ET.SubElement(pg, "Keyword").text = "Win32Proj"
    ET.SubElement(pg, "ProjectGuid").text = guid
    ET.SubElement(pg, "RootNamespace").text = root_namespace
    ET.SubElement(pg, "WindowsTargetPlatformVersion").text = "10.0"

    ET.SubElement(proj, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props")

    # Configuration properties
    for cfg, plat in CONFIGURATIONS:
        props = config_props.get(cfg, {})
        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"
        pg = ET.SubElement(proj, "PropertyGroup", Condition=cond, Label="Configuration")
        is_release = props.get("release_like", cfg == "Release")

        ET.SubElement(pg, "ConfigurationType").text = configuration_type
        ET.SubElement(pg, "UseDebugLibraries").text = "false" if is_release else "true"
        ET.SubElement(pg, "PlatformToolset").text = "v143"
        if is_release:
            ET.SubElement(pg, "WholeProgramOptimization").text = "true"
        ET.SubElement(pg, "CharacterSet").text = "Unicode"

    ET.SubElement(proj, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.props")
    ET.SubElement(proj, "ImportGroup", Label="ExtensionSettings")
    ET.SubElement(proj, "ImportGroup", Label="Shared")

    # Property sheets
    for cfg, plat in CONFIGURATIONS:
        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"
        ig = ET.SubElement(proj, "ImportGroup", Label="PropertySheets", Condition=cond)

        ET.SubElement(
            ig,
            "Import",
            Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props",
            Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')",
            Label="LocalAppDataPlatform",
        )

        if USE_VCPKG_LUA_PROPS and plat in VCPKG_LUA_PLATFORMS:
            ET.SubElement(
                ig,
                "Import",
                Project=VCPKG_LUA_PROPS,
                Condition=f"exists('{VCPKG_LUA_PROPS}')",
            )

    ET.SubElement(proj, "PropertyGroup", Label="UserMacros")

    include_path_value = ";".join(include_paths) + ";$(IncludePath)"
    base_library_path = ";".join(LIBRARY_PATHS) + ";$(LibraryPath)" if LIBRARY_PATHS else "$(LibraryPath)"

    for cfg, plat in CONFIGURATIONS:
        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"
        pg = ET.SubElement(proj, "PropertyGroup", Condition=cond)

        ET.SubElement(pg, "OutDir").text = "$(ProjectDir)Bin\\$(Configuration)\\"
        if int_dir_prefix:
            ET.SubElement(pg, "IntDir").text = f"$(ProjectDir)Build\\{int_dir_prefix}\\$(Configuration)\\"
        else:
            ET.SubElement(pg, "IntDir").text = "$(ProjectDir)Build\\$(Configuration)\\"
        ET.SubElement(pg, "IncludePath").text = include_path_value

        library_path_value = base_library_path
        flavor = sfml_flavor_for(cfg, plat) if include_sfml else None
        if flavor is not None:
            library_path_value = f"{SFML_LIB_BASE}\\{flavor};{base_library_path}"
        ET.SubElement(pg, "LibraryPath").text = library_path_value

        if application_project:
            ET.SubElement(pg, "LocalDebuggerWorkingDirectory").text = "$(ProjectDir)"

    # ItemDefinitionGroups
    for cfg, plat in CONFIGURATIONS:
        props = config_props.get(cfg, {})
        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"
        idg = ET.SubElement(proj, "ItemDefinitionGroup", Condition=cond)
        cl = ET.SubElement(idg, "ClCompile")

        ET.SubElement(cl, "WarningLevel").text = "Level3"

        is_release = props.get("release_like", cfg == "Release")
        is_win32 = plat == "Win32"
        is_x64 = plat == "x64"

        if is_release:
            ET.SubElement(cl, "FunctionLevelLinking").text = "true"
            ET.SubElement(cl, "IntrinsicFunctions").text = "true"

        ET.SubElement(cl, "SDLCheck").text = "true"

        base_defs = []
        if is_win32:
            base_defs.append("WIN32")
        base_defs.append("NDEBUG" if is_release else "_DEBUG")
        base_defs.append("_CONSOLE" if application_project else "_LIB")
        base_defs.append(f"WITH_EDITOR={1 if props.get('with_editor', True) else 0}")
        base_defs.append(f"IS_OBJ_VIEWER={1 if props.get('is_obj_viewer', False) else 0}")
        base_defs.append(f"IS_GAME_CLIENT={1 if props.get('is_game_client', False) else 0}")
        if application_project:
            base_defs.append(f"WITH_CROSSY_GAME_MODULE={1 if (cfg, plat) in CROSSY_LINK_CONFIGURATIONS else 0}")
        base_defs.extend(props.get("extra_defines", []))
        base_defs.append("%(PreprocessorDefinitions)")
        ET.SubElement(cl, "PreprocessorDefinitions").text = ";".join(base_defs)

        ET.SubElement(cl, "ConformanceMode").text = "true"
        ET.SubElement(cl, "AdditionalOptions").text = "/bigobj /utf-8 %(AdditionalOptions)"
        ET.SubElement(cl, "ExceptionHandling").text = "Async"
        ET.SubElement(cl, "MultiProcessorCompilation").text = "true"
        if is_x64:
            ET.SubElement(cl, "LanguageStandard").text = "stdcpp20"

        if static_library_project:
            ET.SubElement(idg, "Lib")
        else:
            link = ET.SubElement(idg, "Link")
            subsystem = props.get("subsystem", "Windows" if is_x64 else "Console")
            ET.SubElement(link, "SubSystem").text = subsystem
            ET.SubElement(link, "GenerateDebugInformation").text = "true"

            sfml_flavor = sfml_flavor_for(cfg, plat) if include_sfml else None
            if sfml_flavor is not None:
                libs = sfml_lib_names(sfml_flavor)
                ET.SubElement(link, "AdditionalDependencies").text = ";".join(libs) + ";%(AdditionalDependencies)"

                pbe = ET.SubElement(idg, "PostBuildEvent")
                ET.SubElement(pbe, "Command").text = sfml_post_build_command(sfml_flavor)
                ET.SubElement(pbe, "Message").text = f"Copying SFML {sfml_flavor} DLLs to $(OutDir)"

    # Items
    for item_name in ["ClCompile", "ClInclude", "FxCompile", "ResourceCompile", "Natvis", "None"]:
        item_files = files.get(item_name, [])
        if not item_files:
            continue
        ig = ET.SubElement(proj, "ItemGroup")
        for f in item_files:
            elem = ET.SubElement(ig, item_name, Include=f)
            if item_name == "FxCompile":
                for cfg, plat in CONFIGURATIONS:
                    if plat == "x64":
                        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"
                        ET.SubElement(elem, "ExcludedFromBuild", Condition=cond).text = "true"

    # Project references before targets is the conventional location and ensures
    # CrossyGame.lib is available to the GameClient application link step.
    if project_references:
        ig = ET.SubElement(proj, "ItemGroup")
        for ref in project_references:
            kwargs = {"Include": ref.include}
            if ref.condition:
                kwargs["Condition"] = ref.condition
            pr = ET.SubElement(ig, "ProjectReference", **kwargs)
            ET.SubElement(pr, "Project").text = ref.guid.lower()
            ET.SubElement(pr, "LinkLibraryDependencies").text = "true"
            ET.SubElement(pr, "UseLibraryDependencyInputs").text = "false"
            ET.SubElement(pr, "ReferenceOutputAssembly").text = "false"

    ET.SubElement(proj, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.targets")

    # NuGet package imports — application project only
    if include_nuget and NUGET_PACKAGES:
        ext_targets = ET.SubElement(proj, "ImportGroup", Label="ExtensionTargets")
        for pkg_id, pkg_ver in NUGET_PACKAGES:
            targets_path = f"packages\\{pkg_id}.{pkg_ver}\\build\\native\\{pkg_id}.targets"
            ET.SubElement(
                ext_targets,
                "Import",
                Project=targets_path,
                Condition=f"Exists('{targets_path}')",
            )

        ensure = ET.SubElement(proj, "Target", Name="EnsureNuGetPackageBuildImports", BeforeTargets="PrepareForBuild")
        pg = ET.SubElement(ensure, "PropertyGroup")
        ET.SubElement(pg, "ErrorText").text = (
            "This project references NuGet package(s) that are missing on this computer. "
            "Use NuGet Package Restore to download them. For more information, see "
            "http://go.microsoft.com/fwlink/?LinkID=322105. The missing file is {0}."
        )
        for pkg_id, pkg_ver in NUGET_PACKAGES:
            targets_path = f"packages\\{pkg_id}.{pkg_ver}\\build\\native\\{pkg_id}.targets"
            ET.SubElement(
                ensure,
                "Error",
                Condition=f"!Exists('{targets_path}')",
                Text=f"$([System.String]::Format('$(ErrorText)', '{targets_path}'))",
            )
    else:
        ET.SubElement(proj, "ImportGroup", Label="ExtensionTargets")

    write_xml(proj, output_path)


# ──────────────────────────────────────────────
# .vcxproj.filters
# ──────────────────────────────────────────────
def generate_filters(files: dict[str, list[str]], *, project_name: str, output_path: Path, bom: bool = True):
    proj = ET.Element("Project", ToolsVersion="4.0", xmlns=NS)
    all_filters = collect_all_filters(files)

    if all_filters:
        ig = ET.SubElement(proj, "ItemGroup")
        for filt in sorted(all_filters):
            f_elem = ET.SubElement(ig, "Filter", Include=filt)
            h = hashlib.md5(f"{project_name}:{filt}".encode()).hexdigest()
            uid = f"{{{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}}}"
            ET.SubElement(f_elem, "UniqueIdentifier").text = uid

    for item_name in ["FxCompile", "ClCompile", "ClInclude", "ResourceCompile", "None", "Natvis"]:
        item_files = files.get(item_name, [])
        if not item_files:
            continue
        ig = ET.SubElement(proj, "ItemGroup")
        for f in item_files:
            filt = get_filter(f)
            elem = ET.SubElement(ig, item_name, Include=f)
            if filt:
                ET.SubElement(elem, "Filter").text = filt

    write_xml(proj, output_path, bom=bom)


# ──────────────────────────────────────────────
# .sln
# ──────────────────────────────────────────────
def sln_project_path(vcxproj_name: str) -> str:
    if PROJECT_REL_DIR == Path("."):
        return vcxproj_name
    return normalize_rel(PROJECT_REL_DIR / vcxproj_name)


def generate_sln():
    lines: list[str] = []
    lines.append("")
    lines.append("Microsoft Visual Studio Solution File, Format Version 12.00")
    lines.append("# Visual Studio Version 17")
    lines.append("VisualStudioVersion = 17.14.37012.4 d17.14")
    lines.append("MinimumVisualStudioVersion = 10.0.40219.1")

    engine_guid = PROJECT_GUID.upper()
    crossy_guid = CROSSY_PROJECT_GUID.upper()

    lines.append(
        f'Project("{VS_PROJECT_TYPE}") = "{PROJECT_NAME}", '
        f'"{sln_project_path(PROJECT_NAME + ".vcxproj")}", "{engine_guid}"'
    )
    lines.append("EndProject")

    lines.append(
        f'Project("{VS_PROJECT_TYPE}") = "{CROSSY_PROJECT_NAME}", '
        f'"{sln_project_path(CROSSY_PROJECT_NAME + ".vcxproj")}", "{crossy_guid}"'
    )
    lines.append("EndProject")

    lines.append("Global")
    lines.append("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    for cfg, plat in CONFIGURATIONS:
        sln_plat = "x86" if plat == "Win32" else plat
        lines.append(f"\t\t{cfg}|{sln_plat} = {cfg}|{sln_plat}")
    lines.append("\tEndGlobalSection")

    lines.append("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    for cfg, plat in CONFIGURATIONS:
        sln_plat = "x86" if plat == "Win32" else plat
        lines.append(f"\t\t{engine_guid}.{cfg}|{sln_plat}.ActiveCfg = {cfg}|{plat}")
        lines.append(f"\t\t{engine_guid}.{cfg}|{sln_plat}.Build.0 = {cfg}|{plat}")

    # CrossyGame is visible in the solution for all configurations, but it is
    # built automatically only for configurations that can load game runtime modules:
    # GameClient|x64, Debug|x64, and Release|x64. ObjViewer/Demo and Win32 configs
    # do not build it by default.
    for cfg, plat in CONFIGURATIONS:
        sln_plat = "x86" if plat == "Win32" else plat
        lines.append(f"\t\t{crossy_guid}.{cfg}|{sln_plat}.ActiveCfg = {cfg}|{plat}")
        if (cfg, plat) in CROSSY_LINK_CONFIGURATIONS:
            lines.append(f"\t\t{crossy_guid}.{cfg}|{sln_plat}.Build.0 = {cfg}|{plat}")

    lines.append("\tEndGlobalSection")
    lines.append("\tGlobalSection(SolutionProperties) = preSolution")
    lines.append("\t\tHideSolutionNode = FALSE")
    lines.append("\tEndGlobalSection")
    lines.append("\tGlobalSection(ExtensibilityGlobals) = postSolution")
    lines.append(f"\t\tSolutionGuid = {SOLUTION_GUID}")
    lines.append("\tEndGlobalSection")
    lines.append("EndGlobal")
    lines.append("")

    sln_path = ROOT / f"{PROJECT_NAME}.sln"
    with open(sln_path, "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write("\n".join(lines))


# ──────────────────────────────────────────────
# Cleanup
# ──────────────────────────────────────────────
def delete_obsolete_files():
    removed = []
    for rel in OBSOLETE_FILES:
        path = PROJECT_DIR / Path(rel.replace("\\", os.sep))
        if path.exists():
            path.unlink()
            removed.append(normalize_rel(path.relative_to(PROJECT_DIR)))
    return removed


# ──────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────
def main():
    removed = delete_obsolete_files()
    if removed:
        print("Removed obsolete files:")
        for rel in removed:
            print(f"  {rel}")

    if GENERATE_VCPKG_LUA_PROPS and USE_VCPKG_LUA_PROPS:
        print("Generating vcpkg property sheet...")
        generate_vcpkg_lua_props()
        print(f"  {PROJECT_DIR / VCPKG_LUA_PROPS_FILE}")

    print(f"Scanning application project files in {PROJECT_DIR}...")
    engine_files = scan_files(
        PROJECT_DIR,
        scan_dirs=ENGINE_SCAN_DIRS,
        exclude_prefixes=ENGINE_EXCLUDE_PREFIXES,
        include_shaders=True,
        root_files=ROOT_FILES,
        root_none_files=ROOT_NONE_FILES,
        include_resources=True,
    )
    print(f"  {PROJECT_NAME} ClCompile:  {len(engine_files['ClCompile'])} files")
    print(f"  {PROJECT_NAME} ClInclude:  {len(engine_files['ClInclude'])} files")
    print(f"  {PROJECT_NAME} FxCompile:  {len(engine_files['FxCompile'])} files")
    print(f"  {PROJECT_NAME} RC:         {len(engine_files['ResourceCompile'])} files")
    print(f"  {PROJECT_NAME} Natvis:     {len(engine_files['Natvis'])} files")
    print(f"  {PROJECT_NAME} None:       {len(engine_files['None'])} files")

    print(f"Scanning {CROSSY_PROJECT_NAME} module files in {PROJECT_DIR}...")
    crossy_files = scan_files(
        PROJECT_DIR,
        scan_dirs=CROSSY_SCAN_DIRS,
        include_shaders=False,
        root_files=[],
        root_none_files=[],
        include_resources=False,
    )
    print(f"  {CROSSY_PROJECT_NAME} ClCompile: {len(crossy_files['ClCompile'])} files")
    print(f"  {CROSSY_PROJECT_NAME} ClInclude: {len(crossy_files['ClInclude'])} files")

    print("Generating project files...")

    generate_vcxproj(
        name=PROJECT_NAME,
        guid=PROJECT_GUID,
        root_namespace=ROOT_NAMESPACE,
        files=engine_files,
        output_path=PROJECT_DIR / f"{PROJECT_NAME}.vcxproj",
        configuration_type="Application",
        config_props=CONFIG_PROPS,
        include_paths=INCLUDE_PATHS,
        application_project=True,
        include_sfml=True,
        include_nuget=True,
        project_references=[
            ProjectReference(
                include=f"{CROSSY_PROJECT_NAME}.vcxproj",
                guid=CROSSY_PROJECT_GUID,
                condition=" Or ".join(
                    f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"
                    for cfg, plat in sorted(CROSSY_LINK_CONFIGURATIONS)
                ),
            )
        ],
    )
    print(f"  {PROJECT_DIR / (PROJECT_NAME + '.vcxproj')}")

    generate_filters(
        engine_files,
        project_name=PROJECT_NAME,
        output_path=PROJECT_DIR / f"{PROJECT_NAME}.vcxproj.filters",
        bom=True,
    )
    print(f"  {PROJECT_DIR / (PROJECT_NAME + '.vcxproj.filters')}")

    generate_vcxproj(
        name=CROSSY_PROJECT_NAME,
        guid=CROSSY_PROJECT_GUID,
        root_namespace=CROSSY_ROOT_NAMESPACE,
        files=crossy_files,
        output_path=PROJECT_DIR / f"{CROSSY_PROJECT_NAME}.vcxproj",
        configuration_type="StaticLibrary",
        config_props=CROSSY_CONFIG_PROPS,
        include_paths=CROSSY_INCLUDE_PATHS,
        int_dir_prefix=CROSSY_PROJECT_NAME,
        static_library_project=True,
        include_sfml=False,
        include_nuget=False,
    )
    print(f"  {PROJECT_DIR / (CROSSY_PROJECT_NAME + '.vcxproj')}")

    generate_filters(
        crossy_files,
        project_name=CROSSY_PROJECT_NAME,
        output_path=PROJECT_DIR / f"{CROSSY_PROJECT_NAME}.vcxproj.filters",
        bom=True,
    )
    print(f"  {PROJECT_DIR / (CROSSY_PROJECT_NAME + '.vcxproj.filters')}")

    generate_sln()
    print(f"  {ROOT / (PROJECT_NAME + '.sln')}")

    print("Done!")


if __name__ == "__main__":
    main()
