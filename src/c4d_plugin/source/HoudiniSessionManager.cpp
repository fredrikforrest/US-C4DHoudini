#include "HoudiniSessionManager.h"

#include "HapiLibrary.h"

namespace c4dhoudini {

namespace {
const Int32 kPrefsContainerId = 1064320;   // TODO: replace with your own registered plugin id
enum { PREF_LIB = 1, PREF_SESSION, PREF_CONNECT, PREF_PORT, PREF_SCRUB, PREF_OTL, PREF_LOG };
}

std::string ToStd(const maxon::String& s)
{
    std::string out;
    Char* c = s.GetCStringCopy(maxon::UTF8_CODEC);
    if (c)
    {
        out = c;
        DeleteMem(c);
    }
    return out;
}

SessionManager& SessionManager::get()
{
    static SessionManager m;
    return m;
}

Prefs SessionManager::prefs() const
{
    Prefs p;
    BaseContainer* wc = GetWorldContainerInstance()->GetContainerInstance(kPrefsContainerId);
    if (!wc) return p;
    p.libraryPath = wc->GetString(PREF_LIB);
    p.sessionType = wc->GetInt32(PREF_SESSION, 0);
    p.connectToExisting = wc->GetBool(PREF_CONNECT, true);
    p.port = wc->GetInt32(PREF_PORT, 9090);
    p.scrubEnvironment = wc->GetBool(PREF_SCRUB, true);
    p.otlSearchPath = wc->GetString(PREF_OTL);
    p.logFile = wc->GetString(PREF_LOG);
    return p;
}

void SessionManager::setPrefs(const Prefs& p)
{
    BaseContainer bc;
    bc.SetString(PREF_LIB, p.libraryPath);
    bc.SetInt32(PREF_SESSION, p.sessionType);
    bc.SetBool(PREF_CONNECT, p.connectToExisting);
    bc.SetInt32(PREF_PORT, p.port);
    bc.SetBool(PREF_SCRUB, p.scrubEnvironment);
    bc.SetString(PREF_OTL, p.otlSearchPath);
    bc.SetString(PREF_LOG, p.logFile);
    GetWorldContainerInstance()->SetContainer(kPrefsContainerId, bc);
}

Bool SessionManager::isUp() const
{
    return hapi::Lib::isLoaded() && hapi::Session::get().isInitialized();
}

Bool SessionManager::ensure(maxon::String* errorOut)
{
    if (isUp()) return true;
    // Do not hammer HARS start-up on every redraw after a failure; the user
    // can retry through the "Restart Session" command.
    if (m_triedOnce && !m_lastError.IsEmpty())
    {
        if (errorOut) *errorOut = m_lastError;
        return false;
    }
    m_triedOnce = true;
    m_lastError = maxon::String();

    Prefs p = prefs();
    std::string err;
    if (!hapi::Lib::isLoaded())
    {
        if (!hapi::Lib::load(ToStd(p.libraryPath), err))
        {
            m_lastError = ToC4D(err);
            ApplicationOutput("[Houdini Engine] @", m_lastError);
            if (errorOut) *errorOut = m_lastError;
            return false;
        }
        ApplicationOutput("[Houdini Engine] loaded @ (@)", ToC4D(hapi::Lib::info().libraryPath),
                          ToC4D(hapi::Lib::info().versionString()));
        if (!hapi::Lib::missingSymbols().empty())
            ApplicationOutput("[Houdini Engine] @ API entry points missing in this Houdini build (older than the headers the plugin was generated from)",
                              Int32(hapi::Lib::missingSymbols().size()));
    }

    hapi::SessionOptions so;
    switch (p.sessionType)
    {
        case 1: so.type = hapi::SessionType::Socket; break;
        case 2: so.type = hapi::SessionType::SharedMemory; break;
        case 3: so.type = hapi::SessionType::InProcess; break;
        default: so.type = hapi::SessionType::NamedPipe; break;
    }
    so.port = p.port;
    so.autoStartServer = true;
    so.scrubEnvironmentForServer = p.scrubEnvironment;
    so.otlSearchPath = ToStd(p.otlSearchPath);
    so.serverLogFile = ToStd(p.logFile);
    // Cinema 4D treats n-gons natively but the PoC triangulates >4; keep
    // Houdini from splitting polygons itself so UVs/groups stay aligned.
    so.maxVerticesPerPrimitive = -1;

    if (!hapi::Session::get().start(so, err))
    {
        m_lastError = ToC4D(err);
        ApplicationOutput("[Houdini Engine] session failed: @", m_lastError);
        if (errorOut) *errorOut = m_lastError;
        return false;
    }
    ApplicationOutput("[Houdini Engine] @", statusLine());
    return true;
}

void SessionManager::shutdown()
{
    hapi::Session::get().stop();
    hapi::Lib::unload();
    m_triedOnce = false;
}

maxon::String SessionManager::statusLine() const
{
    if (!hapi::Lib::isLoaded()) return "Houdini Engine: libHAPIL not loaded"_s;
    const hapi::Session& s = hapi::Session::get();
    maxon::String line = ToC4D(hapi::Lib::info().versionString());
    if (!s.isInitialized()) return line + ", no session"_s;
    const char* type = "pipe";
    switch (s.options().type)
    {
        case hapi::SessionType::Socket: type = "socket"; break;
        case hapi::SessionType::SharedMemory: type = "shared memory"; break;
        case hapi::SessionType::InProcess: type = "in-process"; break;
        default: break;
    }
    line += ", "_s + maxon::String(type) + " session"_s;
    if (s.serverProcessId()) line += ", HARS pid "_s + maxon::String::IntToString(Int32(s.serverProcessId()));
    return line;
}

} // namespace c4dhoudini
