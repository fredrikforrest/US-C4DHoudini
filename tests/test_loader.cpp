// Loads the mock libHAPIL through the generated table and runs a session
// start/cook/stop cycle against it.
#include "HapiSession.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_loader <path to mock lib>\n"); return 2; }
    std::string err;
    if (!hapi::Lib::load(argv[1], err)) { std::printf("load failed: %s\n", err.c_str()); return 1; }
    const hapi::LibraryInfo& info = hapi::Lib::info();
    std::printf("loaded %s\n%s\n", info.libraryPath.c_str(), info.versionString().c_str());
    int rc = 0;
    if (info.houdiniMajor != 22 || info.houdiniBuild != 475 || info.engineMajor != 9) { std::printf("FAIL version\n"); rc = 1; }
    if (!hapi::Lib::GetEnvInt || !hapi::Lib::CookNode) { std::printf("FAIL symbols\n"); rc = 1; }
    if (hapi::Lib::GetParameters != nullptr) { std::printf("FAIL: unresolved symbol should be null\n"); rc = 1; }
    std::printf("missing symbols: %zu (expected: everything the mock does not export)\n", hapi::Lib::missingSymbols().size());

    hapi::SessionOptions opts;
    opts.autoStartServer = false;
    hapi::Session& s = hapi::Session::get();
    if (!s.start(opts, err)) { std::printf("FAIL start: %s\n", err.c_str()); rc = 1; }
    if (!s.isInitialized()) { std::printf("FAIL not initialized\n"); rc = 1; }
    hapi::CookResult cr = s.cook(1);
    if (!cr.succeeded) { std::printf("FAIL cook: %s\n", cr.message.c_str()); rc = 1; }
    if (s.getString(7) != "mock") { std::printf("FAIL getString\n"); rc = 1; }
    s.stop();
    hapi::Lib::unload();
    std::printf(rc ? "loader: FAILED\n" : "loader: all tests passed\n");
    return rc;
}
