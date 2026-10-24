// HapiLibrary: runtime loader for libHAPIL.
//
// The plugin never links against libHAPIL. Instead every HAPI_* entry point is
// resolved by name from whatever Houdini the user points us at. This is the
// same approach as SideFX's Maya (HoudiniApi.C), Unreal (FHoudiniApi) and
// 3ds Max plugins and is what makes a single plugin binary work across
// Houdini builds - the thing the discontinued Maxon bridge got wrong.
#pragma once

#include <HAPI/HAPI.h>

#include <string>
#include <vector>

namespace hapi {

// Where libHAPIL came from and what version it reports.
struct LibraryInfo
{
    std::string libraryPath;   // full path to libHAPIL.{dll,dylib,so}
    std::string hfs;           // Houdini install root derived from libraryPath
    std::string harsPath;      // HARS executable, if found
    int houdiniMajor = 0, houdiniMinor = 0, houdiniBuild = 0, houdiniPatch = 0;
    int engineMajor = 0, engineMinor = 0, engineApi = 0;
    std::string versionString() const;
};

// Function table. HAPI_FUNC expands to one static pointer per symbol:
//     static HAPI_Result (HAPI_CALL *CookNode)(const HAPI_Session*, HAPI_NodeId, const HAPI_CookOptions*);
// Symbols missing from an older libHAPIL stay nullptr; callers should use
// Lib::has(Lib::Foo) for anything newer than the baseline they target.
struct Lib
{
#define HAPI_FUNC(name, ret, args) \
    typedef ret (HAPI_CALL *name##Fn) args; \
    static name##Fn name;
#include "HapiFunctions.inc"
#undef HAPI_FUNC

    // Load libHAPIL from an explicit path. Empty path = search default
    // locations (HFS / HOUDINI_PATH env vars, then platform install dirs).
    static bool load(const std::string& explicitLibraryPath, std::string& error);
    static void unload();
    static bool isLoaded();
    static const LibraryInfo& info();

    // Platform search: returns candidate libHAPIL paths, most preferred first.
    static std::vector<std::string> defaultSearchPaths();

    template <typename T>
    static bool has(T fn) { return fn != nullptr; }

    // Symbols that could not be resolved (useful for diagnostics).
    static const std::vector<std::string>& missingSymbols();
};

} // namespace hapi
