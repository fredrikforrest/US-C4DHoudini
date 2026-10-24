#include "HapiLibrary.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <sys/stat.h>
#endif

namespace hapi {

namespace {

void* g_handle = nullptr;
LibraryInfo g_info;
std::vector<std::string> g_missing;

bool fileExists(const std::string& p)
{
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

void* openLibrary(const std::string& path)
{
#if defined(_WIN32)
    // Make sibling DLLs (libHAPI, OpenImageIO, etc.) resolvable.
    std::string dir = path.substr(0, path.find_last_of("/\\"));
    SetDllDirectoryA(dir.c_str());
    return (void*)LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void closeLibrary(void* h)
{
#if defined(_WIN32)
    FreeLibrary((HMODULE)h);
#else
    dlclose(h);
#endif
}

void* symbol(void* h, const char* name)
{
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)h, name);
#else
    return dlsym(h, name);
#endif
}

std::string lastError()
{
#if defined(_WIN32)
    DWORD e = GetLastError();
    char buf[512] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e,
                   0, buf, sizeof(buf), nullptr);
    std::ostringstream os;
    os << "error " << e << ": " << buf;
    return os.str();
#else
    const char* e = dlerror();
    return e ? e : "unknown dlopen error";
#endif
}

std::string env(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

#if defined(_WIN32)
const char* kLibName = "libHAPIL.dll";
#elif defined(__APPLE__)
const char* kLibName = "libHAPIL.dylib";
#else
const char* kLibName = "libHAPIL.so";
#endif

// libHAPIL lives at a fixed place relative to $HFS on each platform.
std::string libPathForHfs(const std::string& hfs)
{
#if defined(_WIN32)
    return hfs + "/bin/" + kLibName;
#elif defined(__APPLE__)
    return hfs + "/Frameworks/Houdini.framework/Versions/Current/Libraries/" + kLibName;
#else
    return hfs + "/dsolib/" + kLibName;
#endif
}

std::string hfsForLibPath(const std::string& lib)
{
    std::string p = lib;
    for (char& c : p) if (c == '\\') c = '/';
    auto cut = [&](const char* suffix) -> bool {
        size_t pos = p.rfind(suffix);
        if (pos != std::string::npos) { p = p.substr(0, pos); return true; }
        return false;
    };
#if defined(_WIN32)
    cut("/bin/");
#elif defined(__APPLE__)
    cut("/Frameworks/Houdini.framework/");
#else
    cut("/dsolib/");
#endif
    return p;
}

std::string harsForHfs(const std::string& hfs)
{
    std::string e = env("HOUDINI_HARS_LOCATION");
    if (!e.empty() && fileExists(e)) return e;
#if defined(_WIN32)
    std::string p = hfs + "/bin/HARS.exe";
#elif defined(__APPLE__)
    std::string p = hfs + "/Frameworks/Houdini.framework/Versions/Current/Resources/bin/HARS";
#else
    std::string p = hfs + "/bin/HARS";
#endif
    return fileExists(p) ? p : std::string();
}

} // namespace

#define HAPI_FUNC(name, ret, args) Lib::name##Fn Lib::name = nullptr;
#include "HapiFunctions.inc"
#undef HAPI_FUNC

std::string LibraryInfo::versionString() const
{
    std::ostringstream os;
    os << "Houdini " << houdiniMajor << "." << houdiniMinor << "." << houdiniBuild;
    if (houdiniPatch) os << "." << houdiniPatch;
    os << " / Houdini Engine " << engineMajor << "." << engineMinor << " (API " << engineApi << ")";
    return os.str();
}

std::vector<std::string> Lib::defaultSearchPaths()
{
    std::vector<std::string> out;
    // 1. Explicit overrides.
    std::string e = env("HAPI_LIBRARY_PATH");   // our own, full path to libHAPIL
    if (!e.empty()) out.push_back(e);
    e = env("HFS");
    if (!e.empty()) out.push_back(libPathForHfs(e));
    e = env("HOUDINI_ROOT");
    if (!e.empty()) out.push_back(libPathForHfs(e));

    // 2. Platform install locations. We list the newest known majors first;
    //    a directory scan would be better in the plugin's preferences UI.
    const char* majors[] = {"22.0", "21.5", "21.0", "20.5", "20.0", "19.5"};
#if defined(_WIN32)
    // Registry: HKLM\SOFTWARE\Side Effects Software\Houdini <ver>\InstallPath
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Side Effects Software", 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        char sub[256];
        DWORD idx = 0, len = sizeof(sub);
        std::vector<std::string> subs;
        while (RegEnumKeyExA(key, idx++, sub, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            subs.push_back(sub);
            len = sizeof(sub);
        }
        // Newest first.
        for (auto it = subs.rbegin(); it != subs.rend(); ++it)
        {
            HKEY vk;
            std::string path = "SOFTWARE\\Side Effects Software\\" + *it;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &vk) == ERROR_SUCCESS)
            {
                char val[MAX_PATH] = {0};
                DWORD vlen = sizeof(val), type = 0;
                if (RegQueryValueExA(vk, "InstallPath", nullptr, &type, (LPBYTE)val, &vlen) == ERROR_SUCCESS)
                    out.push_back(libPathForHfs(val));
                RegCloseKey(vk);
            }
        }
        RegCloseKey(key);
    }
    for (const char* m : majors)
        out.push_back(std::string("C:/Program Files/Side Effects Software/Houdini ") + m + "/bin/" + kLibName);
#elif defined(__APPLE__)
    out.push_back(std::string("/Library/Frameworks/Houdini.framework/Versions/Current/Libraries/") + kLibName);
    for (const char* m : majors)
        out.push_back(std::string("/Applications/Houdini/Houdini") + m + "/Frameworks/Houdini.framework/Versions/Current/Libraries/" + kLibName);
#else
    for (const char* m : majors)
        out.push_back(std::string("/opt/hfs") + m + "/dsolib/" + kLibName);
#endif
    return out;
}

bool Lib::load(const std::string& explicitLibraryPath, std::string& error)
{
    if (g_handle) return true;

    std::vector<std::string> candidates;
    if (!explicitLibraryPath.empty())
        candidates.push_back(explicitLibraryPath);
    else
        candidates = defaultSearchPaths();

    std::string tried;
    for (const std::string& c : candidates)
    {
        if (!fileExists(c)) { tried += "\n  (missing) " + c; continue; }
        g_handle = openLibrary(c);
        if (g_handle) { g_info.libraryPath = c; break; }
        tried += "\n  (failed)  " + c + ": " + lastError();
    }
    if (!g_handle)
    {
        error = "Could not load " + std::string(kLibName) + ". Tried:" + tried;
        return false;
    }

    g_missing.clear();
#define HAPI_FUNC(name, ret, args) \
    name = (name##Fn)symbol(g_handle, "HAPI_" #name); \
    if (!name) g_missing.push_back("HAPI_" #name);
#include "HapiFunctions.inc"
#undef HAPI_FUNC

    // The bare minimum we cannot work without.
    if (!CreateThriftNamedPipeSession || !Initialize || !CookNode || !GetString)
    {
        error = "libHAPIL at " + g_info.libraryPath + " is missing core entry points; "
                "is this a supported Houdini version?";
        unload();
        return false;
    }

    if (GetEnvInt)
    {
        GetEnvInt(HAPI_ENVINT_VERSION_HOUDINI_MAJOR, &g_info.houdiniMajor);
        GetEnvInt(HAPI_ENVINT_VERSION_HOUDINI_MINOR, &g_info.houdiniMinor);
        GetEnvInt(HAPI_ENVINT_VERSION_HOUDINI_BUILD, &g_info.houdiniBuild);
        GetEnvInt(HAPI_ENVINT_VERSION_HOUDINI_PATCH, &g_info.houdiniPatch);
        GetEnvInt(HAPI_ENVINT_VERSION_HOUDINI_ENGINE_MAJOR, &g_info.engineMajor);
        GetEnvInt(HAPI_ENVINT_VERSION_HOUDINI_ENGINE_MINOR, &g_info.engineMinor);
        GetEnvInt(HAPI_ENVINT_VERSION_HOUDINI_ENGINE_API, &g_info.engineApi);
    }
    g_info.hfs = hfsForLibPath(g_info.libraryPath);
    g_info.harsPath = harsForHfs(g_info.hfs);
    return true;
}

void Lib::unload()
{
    if (!g_handle) return;
#define HAPI_FUNC(name, ret, args) name = nullptr;
#include "HapiFunctions.inc"
#undef HAPI_FUNC
    closeLibrary(g_handle);
    g_handle = nullptr;
    g_info = LibraryInfo();
}

bool Lib::isLoaded() { return g_handle != nullptr; }
const LibraryInfo& Lib::info() { return g_info; }
const std::vector<std::string>& Lib::missingSymbols() { return g_missing; }

} // namespace hapi
