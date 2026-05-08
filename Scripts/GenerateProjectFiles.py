"""
GenerateProjectFiles.py — Auto-generate .vcxproj, .vcxproj.filters
for KraftonEngine from the on-disk folder structure.

Usage:
    python Scripts/GenerateProjectFiles.py
"""

import hashlib
import os
import xml.etree.ElementTree as ET
from pathlib import Path

# ──────────────────────────────────────────────
# Constants
# ──────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent.parent

PROJECT_NAME = "KraftonEngine"
PROJECT_DIR = ROOT / PROJECT_NAME
PROJECT_GUID = "{55068e81-c0a0-49f9-ab7b-54aea968722b}"
ROOT_NAMESPACE = "Week2"

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

# Per-configuration overrides
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

# Directories to recursively scan for source files
SCAN_DIRS = ["Source", "ThirdParty"]

# Directories to scan for shader files
SHADER_DIRS = ["Shaders"]

# File extensions to include
SOURCE_EXTS = {".cpp", ".c", ".cc", ".cxx"}
HEADER_EXTS = {".h", ".hpp", ".hxx", ".inl"}
SHADER_EXTS = {".hlsl", ".hlsli"}
NATVIS_EXTS = {".natvis"}
NONE_EXTS = {".natstepfilter", ".config", ".props"}

RC_EXTS = {".rc"}

# Root-level files to include
ROOT_FILES = ["main.cpp"]

# Root-level None files to include in the project view
ROOT_NONE_FILES = [
    "VcpkgLua.props",
]

# Include paths
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

# Library paths (common to all configurations)
LIBRARY_PATHS = []

# ──────────────────────────────────────────────
# SFML configuration
# ──────────────────────────────────────────────
# SFML modules in use (no graphics/network because the engine uses its own
# DirectX-based renderer and doesn't need SFML networking).
SFML_MODULES = ["audio", "window", "system"]

# Base paths inside the project directory
SFML_LIB_BASE = "$(ProjectDir)ThirdParty\\SFML\\lib"
SFML_BIN_BASE = "$(ProjectDir)ThirdParty\\SFML\\bin"

# Maps a project Configuration name to the SFML build flavor folder
# ("Debug" or "Release"). Configurations not in this map are skipped (no
# SFML link / DLL copy). Win32 configs are intentionally excluded — SFML is
# x64-only in this project.
SFML_CONFIG_FLAVOR = {
    "Debug":         "Debug",
    "Release":       "Release",
    "ObjViewDebug":  "Release",
    "Demo":          "Release",
    "GameClient":    "Release",
}

# Platforms eligible for SFML linkage
SFML_PLATFORMS = {"x64"}

# Lua / vcpkg property sheet
USE_VCPKG_LUA_PROPS = True
VCPKG_LUA_PROPS_FILE = "VcpkgLua.props"
VCPKG_LUA_PROPS = f"$(MSBuildProjectDirectory)\\{VCPKG_LUA_PROPS_FILE}"
VCPKG_LUA_PLATFORMS = {"x64"}
GENERATE_VCPKG_LUA_PROPS = True

# NuGet packages
NUGET_PACKAGES = [
    ("directxtk_desktop_win10", "2025.10.28.2"),
]

NS = "http://schemas.microsoft.com/developer/msbuild/2003"


# ──────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────
def scan_files(project_dir: Path) -> dict[str, list[str]]:
    """Scan directories and collect files grouped by type."""
    result = {
        "ClCompile": [],
        "ClInclude": [],
        "FxCompile": [],
        "ResourceCompile": [],
        "Natvis": [],
        "None": [],
    }

    for scan_dir in SCAN_DIRS:
        full_dir = project_dir / scan_dir
        if not full_dir.exists():
            continue

        for dirpath, _, filenames in os.walk(full_dir):
            for fname in sorted(filenames):
                full = Path(dirpath) / fname
                rel = full.relative_to(project_dir)
                rel_str = str(rel).replace("/", "\\")
                ext = full.suffix.lower()

                if ext in SOURCE_EXTS:
                    result["ClCompile"].append(rel_str)
                elif ext in HEADER_EXTS:
                    result["ClInclude"].append(rel_str)
                elif ext in NATVIS_EXTS:
                    result["Natvis"].append(rel_str)
                elif ext in NONE_EXTS:
                    result["None"].append(rel_str)

    for shader_dir in SHADER_DIRS:
        full_dir = project_dir / shader_dir
        if not full_dir.exists():
            continue

        for dirpath, _, filenames in os.walk(full_dir):
            for fname in sorted(filenames):
                full = Path(dirpath) / fname
                rel = full.relative_to(project_dir)
                rel_str = str(rel).replace("/", "\\")
                ext = full.suffix.lower()

                if ext in SHADER_EXTS:
                    result["FxCompile"].append(rel_str)

    for root_file in ROOT_FILES:
        full = project_dir / root_file
        if full.exists():
            result["ClCompile"].append(root_file.replace("/", "\\"))

    for root_none_file in ROOT_NONE_FILES:
        full = project_dir / root_none_file
        if full.exists():
            result["None"].append(root_none_file.replace("/", "\\"))

    for f in sorted(project_dir.glob("*.rc")):
        result["ResourceCompile"].append(f.name)

    for key in result:
        result[key] = sorted(set(result[key]))

    return result


def get_filter(rel_path: str) -> str:
    """Return the filter path from a relative path."""
    parts = rel_path.replace("/", "\\").rsplit("\\", 1)
    return parts[0] if len(parts) > 1 else ""


def collect_all_filters(files: dict[str, list[str]]) -> set[str]:
    """Collect all unique filter paths including parent paths."""
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


# ──────────────────────────────────────────────
# SFML helpers
# ──────────────────────────────────────────────
def sfml_flavor_for(cfg: str, plat: str) -> str | None:
    """Return 'Debug' or 'Release' for a given (cfg, plat), or None if SFML
    should not be applied to this configuration."""
    if plat not in SFML_PLATFORMS:
        return None
    return SFML_CONFIG_FLAVOR.get(cfg)


def sfml_lib_names(flavor: str) -> list[str]:
    """Return the .lib filenames for a given flavor.
    Debug builds use the '-d' suffix per SFML naming convention."""
    suffix = "-d" if flavor == "Debug" else ""
    return [f"sfml-{m}{suffix}.lib" for m in SFML_MODULES]


def sfml_post_build_command(flavor: str) -> str:
    """Return the xcopy command that copies all SFML DLLs (and their
    dependencies like OpenAL32.dll, libsndfile-1.dll) from the matching
    bin\\<flavor>\\ folder to $(OutDir).

    xcopy flags:
      /Y  : overwrite existing files without prompting
      /D  : copy only files whose source is newer than the destination
      /I  : if destination doesn't exist and multiple files, treat as dir
    The trailing '*' on $(OutDir) forces xcopy to treat it as a directory
    (avoids the 'File or Directory?' interactive prompt).
    """
    src = f"{SFML_BIN_BASE}\\{flavor}\\*.dll"
    return f'xcopy /Y /D /I "{src}" "$(OutDir)"'


# ──────────────────────────────────────────────
# XML Generation
# ──────────────────────────────────────────────
def indent_xml(elem, level=0):
    """Add indentation to XML tree."""
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


def write_xml(root_elem, filepath: Path, bom=False):
    """Write XML tree to file with declaration."""
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
    """Generate the shared vcpkg property sheet used by all x64 configs.

    This intentionally keeps vcpkg/RmlUi in a property sheet instead of
    emitting it into each configuration block. The .vcxproj imports this sheet
    for every x64 configuration, and the CopyVcpkgRuntimeDlls target avoids
    fighting with the per-config SFML PostBuildEvent generated below.
    """
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
# .vcxproj
# ──────────────────────────────────────────────
def generate_vcxproj(files: dict[str, list[str]]):
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
    ET.SubElement(pg, "ProjectGuid").text = PROJECT_GUID
    ET.SubElement(pg, "RootNamespace").text = ROOT_NAMESPACE
    ET.SubElement(pg, "WindowsTargetPlatformVersion").text = "10.0"

    ET.SubElement(proj, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props")

    # Configuration properties
    for cfg, plat in CONFIGURATIONS:
        props = CONFIG_PROPS.get(cfg, {})
        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"

        pg = ET.SubElement(proj, "PropertyGroup", Condition=cond, Label="Configuration")

        is_release = props.get("release_like", cfg == "Release")

        ET.SubElement(pg, "ConfigurationType").text = "Application"
        ET.SubElement(pg, "UseDebugLibraries").text = "false" if is_release else "true"
        ET.SubElement(pg, "PlatformToolset").text = "v143"

        if is_release:
            ET.SubElement(pg, "WholeProgramOptimization").text = "true"

        ET.SubElement(pg, "CharacterSet").text = "Unicode"

    ET.SubElement(proj, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.props")
    ET.SubElement(proj, "ImportGroup", Label="ExtensionSettings")
    ET.SubElement(proj, "ImportGroup", Label="Shared")

    # PropertySheets
    for cfg, plat in CONFIGURATIONS:
        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"

        ig = ET.SubElement(
            proj,
            "ImportGroup",
            Label="PropertySheets",
            Condition=cond,
        )

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

    # OutDir, IntDir, IncludePath, LibraryPath, WorkingDirectory
    include_path_value = ";".join(INCLUDE_PATHS) + ";$(IncludePath)"

    # Build a base library path (common to all configs). SFML's per-config
    # path is appended below inside the configuration loop.
    if LIBRARY_PATHS:
        base_library_path = ";".join(LIBRARY_PATHS) + ";$(LibraryPath)"
    else:
        base_library_path = "$(LibraryPath)"

    for cfg, plat in CONFIGURATIONS:
        cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"

        pg = ET.SubElement(proj, "PropertyGroup", Condition=cond)

        ET.SubElement(pg, "OutDir").text = "$(ProjectDir)Bin\\$(Configuration)\\"
        ET.SubElement(pg, "IntDir").text = "$(ProjectDir)Build\\$(Configuration)\\"
        ET.SubElement(pg, "IncludePath").text = include_path_value

        # Per-config LibraryPath: prepend SFML lib folder for eligible configs.
        # We hardcode the flavor (Debug/Release) instead of using
        # $(Configuration), because configs like ObjViewDebug/Demo/GameClient
        # have no matching folder under SFML\lib\.
        flavor = sfml_flavor_for(cfg, plat)
        if flavor is not None:
            sfml_lib_path = f"{SFML_LIB_BASE}\\{flavor}"
            library_path_value = f"{sfml_lib_path};{base_library_path}"
        else:
            library_path_value = base_library_path

        ET.SubElement(pg, "LibraryPath").text = library_path_value
        ET.SubElement(pg, "LocalDebuggerWorkingDirectory").text = "$(ProjectDir)"

    # ItemDefinitionGroups
    for cfg, plat in CONFIGURATIONS:
        props = CONFIG_PROPS.get(cfg, {})
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
        base_defs.append("_CONSOLE")
        base_defs.append(f"WITH_EDITOR={1 if props.get('with_editor', True) else 0}")
        base_defs.append(f"IS_OBJ_VIEWER={1 if props.get('is_obj_viewer', False) else 0}")
        base_defs.append(f"IS_GAME_CLIENT={1 if props.get('is_game_client', False) else 0}")
        base_defs.extend(props.get("extra_defines", []))
        base_defs.append("%(PreprocessorDefinitions)")

        ET.SubElement(cl, "PreprocessorDefinitions").text = ";".join(base_defs)

        ET.SubElement(cl, "ConformanceMode").text = "true"
        ET.SubElement(cl, "AdditionalOptions").text = "/bigobj /utf-8 %(AdditionalOptions)"
        ET.SubElement(cl, "ExceptionHandling").text = "Async"
        ET.SubElement(cl, "MultiProcessorCompilation").text = "true"

        if is_x64:
            ET.SubElement(cl, "LanguageStandard").text = "stdcpp20"

        link = ET.SubElement(idg, "Link")

        subsystem = props.get("subsystem", "Windows" if is_x64 else "Console")

        ET.SubElement(link, "SubSystem").text = subsystem
        ET.SubElement(link, "GenerateDebugInformation").text = "true"

        # ── SFML linkage (per-config) ─────────────────────────────
        # For x64 configs that map to a SFML flavor, append SFML's .lib
        # files to AdditionalDependencies and emit a PostBuildEvent that
        # copies the matching DLLs (and their dependencies such as
        # OpenAL32.dll, libsndfile-1.dll) next to the built .exe.
        sfml_flavor = sfml_flavor_for(cfg, plat)
        if sfml_flavor is not None:
            libs = sfml_lib_names(sfml_flavor)
            # %(AdditionalDependencies) preserves any libs inherited from
            # property sheets / Microsoft defaults (e.g. kernel32.lib).
            deps_text = ";".join(libs) + ";%(AdditionalDependencies)"
            ET.SubElement(link, "AdditionalDependencies").text = deps_text

            pbe = ET.SubElement(idg, "PostBuildEvent")
            ET.SubElement(pbe, "Command").text = sfml_post_build_command(sfml_flavor)
            ET.SubElement(pbe, "Message").text = (
                f"Copying SFML {sfml_flavor} DLLs to $(OutDir)"
            )

    # ClCompile items
    ig = ET.SubElement(proj, "ItemGroup")

    for f in files["ClCompile"]:
        ET.SubElement(ig, "ClCompile", Include=f)

    # ClInclude items
    ig = ET.SubElement(proj, "ItemGroup")

    for f in files["ClInclude"]:
        ET.SubElement(ig, "ClInclude", Include=f)

    # FxCompile items
    if files["FxCompile"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["FxCompile"]:
            elem = ET.SubElement(ig, "FxCompile", Include=f)

            for cfg, plat in CONFIGURATIONS:
                if plat == "x64":
                    cond = f"'$(Configuration)|$(Platform)'=='{cfg}|{plat}'"
                    ET.SubElement(elem, "ExcludedFromBuild", Condition=cond).text = "true"

    # ResourceCompile items
    if files["ResourceCompile"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["ResourceCompile"]:
            ET.SubElement(ig, "ResourceCompile", Include=f)

    # Natvis items
    if files["Natvis"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["Natvis"]:
            ET.SubElement(ig, "Natvis", Include=f)

    # None items
    if files["None"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["None"]:
            ET.SubElement(ig, "None", Include=f)

    ET.SubElement(proj, "Import", Project="$(VCTargetsPath)\\Microsoft.Cpp.targets")

    # NuGet package imports
    if NUGET_PACKAGES:
        ext_targets = ET.SubElement(proj, "ImportGroup", Label="ExtensionTargets")

        for pkg_id, pkg_ver in NUGET_PACKAGES:
            targets_path = f"packages\\{pkg_id}.{pkg_ver}\\build\\native\\{pkg_id}.targets"

            ET.SubElement(
                ext_targets,
                "Import",
                Project=targets_path,
                Condition=f"Exists('{targets_path}')",
            )

        ensure = ET.SubElement(
            proj,
            "Target",
            Name="EnsureNuGetPackageBuildImports",
            BeforeTargets="PrepareForBuild",
        )

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

    write_xml(proj, PROJECT_DIR / f"{PROJECT_NAME}.vcxproj")


# ──────────────────────────────────────────────
# .vcxproj.filters
# ──────────────────────────────────────────────
def generate_filters(files: dict[str, list[str]]):
    proj = ET.Element("Project", ToolsVersion="4.0", xmlns=NS)

    all_filters = collect_all_filters(files)

    if all_filters:
        ig = ET.SubElement(proj, "ItemGroup")

        for filt in sorted(all_filters):
            f_elem = ET.SubElement(ig, "Filter", Include=filt)
            h = hashlib.md5(f"{PROJECT_NAME}:{filt}".encode()).hexdigest()
            uid = f"{{{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}}}"
            ET.SubElement(f_elem, "UniqueIdentifier").text = uid

    if files["FxCompile"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["FxCompile"]:
            filt = get_filter(f)
            elem = ET.SubElement(ig, "FxCompile", Include=f)

            if filt:
                ET.SubElement(elem, "Filter").text = filt

    if files["ClCompile"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["ClCompile"]:
            filt = get_filter(f)
            elem = ET.SubElement(ig, "ClCompile", Include=f)

            if filt:
                ET.SubElement(elem, "Filter").text = filt

    if files["ClInclude"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["ClInclude"]:
            filt = get_filter(f)
            elem = ET.SubElement(ig, "ClInclude", Include=f)

            if filt:
                ET.SubElement(elem, "Filter").text = filt

    if files["ResourceCompile"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["ResourceCompile"]:
            filt = get_filter(f)
            elem = ET.SubElement(ig, "ResourceCompile", Include=f)

            if filt:
                ET.SubElement(elem, "Filter").text = filt

    if files["None"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["None"]:
            filt = get_filter(f)
            elem = ET.SubElement(ig, "None", Include=f)

            if filt:
                ET.SubElement(elem, "Filter").text = filt

    if files["Natvis"]:
        ig = ET.SubElement(proj, "ItemGroup")

        for f in files["Natvis"]:
            filt = get_filter(f)
            elem = ET.SubElement(ig, "Natvis", Include=f)

            if filt:
                ET.SubElement(elem, "Filter").text = filt

    write_xml(proj, PROJECT_DIR / f"{PROJECT_NAME}.vcxproj.filters", bom=True)


# ──────────────────────────────────────────────
# .sln
# ──────────────────────────────────────────────
def generate_sln():
    lines = []

    lines.append("")
    lines.append("Microsoft Visual Studio Solution File, Format Version 12.00")
    lines.append("# Visual Studio Version 17")
    lines.append("VisualStudioVersion = 17.14.37012.4 d17.14")
    lines.append("MinimumVisualStudioVersion = 10.0.40219.1")

    guid_upper = PROJECT_GUID.upper()

    lines.append(
        f'Project("{VS_PROJECT_TYPE}") = "{PROJECT_NAME}", '
        f'"{PROJECT_NAME}\\{PROJECT_NAME}.vcxproj", "{guid_upper}"'
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
        lines.append(f"\t\t{guid_upper}.{cfg}|{sln_plat}.ActiveCfg = {cfg}|{plat}")
        lines.append(f"\t\t{guid_upper}.{cfg}|{sln_plat}.Build.0 = {cfg}|{plat}")

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
# Main
# ──────────────────────────────────────────────
def main():
    if GENERATE_VCPKG_LUA_PROPS and USE_VCPKG_LUA_PROPS:
        print("Generating vcpkg property sheet...")
        generate_vcpkg_lua_props()
        print(f"  {VCPKG_LUA_PROPS_FILE}")

    print(f"Scanning project files in {PROJECT_DIR}...")

    files = scan_files(PROJECT_DIR)

    print(f"  ClCompile:  {len(files['ClCompile'])} files")
    print(f"  ClInclude:  {len(files['ClInclude'])} files")
    print(f"  FxCompile:  {len(files['FxCompile'])} files")
    print(f"  RC:         {len(files['ResourceCompile'])} files")
    print(f"  Natvis:     {len(files['Natvis'])} files")
    print(f"  None:       {len(files['None'])} files")

    print("Generating project files...")

    generate_vcxproj(files)
    print(f"  {PROJECT_NAME}.vcxproj")

    generate_filters(files)
    print(f"  {PROJECT_NAME}.vcxproj.filters")

    generate_sln()
    print(f"  {PROJECT_NAME}.sln")

    print("Done!")


if __name__ == "__main__":
    main()