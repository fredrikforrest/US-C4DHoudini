#include "HapiSession.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <process.h>
#  define HAPI_GETPID _getpid
#else
#  include <unistd.h>
#  define HAPI_GETPID getpid
#endif

namespace hapi {

namespace {

// HARS is spawned by libHAPIL itself (HAPI_StartThrift*Server); it finds the
// executable through PATH or HOUDINI_HARS_LOCATION. We export the latter from
// the libHAPIL location we already resolved so users never have to touch PATH.
void exportHarsLocation()
{
    const LibraryInfo& li = Lib::info();
    if (li.harsPath.empty()) return;
#if defined(_WIN32)
    _putenv_s("HOUDINI_HARS_LOCATION", li.harsPath.c_str());
#else
    setenv("HOUDINI_HARS_LOCATION", li.harsPath.c_str(), 0);
#endif
}

// The Maya plugin found that LD_LIBRARY_PATH / PYTHONPATH / DYLD_* leaking
// from the host into HARS makes it pick up the host's Python or Qt and die
// on start-up. Cinema 4D ships its own Python, so we scrub those while
// spawning the server and restore them afterwards.
struct ScopedCleanEnv
{
    std::vector<std::pair<std::string, std::string>> saved;
    explicit ScopedCleanEnv(bool enabled)
    {
        if (!enabled) return;
        const char* names[] = {"LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH",
                               "PYTHONPATH", "PYTHONHOME", "QT_PLUGIN_PATH"};
        for (const char* n : names)
        {
            const char* v = std::getenv(n);
            if (!v) continue;
            saved.emplace_back(n, v);
#if defined(_WIN32)
            _putenv_s(n, "");
#else
            unsetenv(n);
#endif
        }
    }
    ~ScopedCleanEnv()
    {
        for (auto& kv : saved)
        {
#if defined(_WIN32)
            _putenv_s(kv.first.c_str(), kv.second.c_str());
#else
            setenv(kv.first.c_str(), kv.second.c_str(), 1);
#endif
        }
    }
};

std::string defaultPipeName()
{
    std::ostringstream os;
    os << "hapi_c4d_" << HAPI_GETPID();
    return os.str();
}

} // namespace

PythonLock::PythonLock(const HAPI_Session& s) : session(s)
{
    if (Lib::PythonThreadInterpreterLock) Lib::PythonThreadInterpreterLock(&session, true);
}
PythonLock::~PythonLock()
{
    if (Lib::PythonThreadInterpreterLock) Lib::PythonThreadInterpreterLock(&session, false);
}

Session& Session::get()
{
    static Session s;
    return s;
}

const char* Session::resultName(HAPI_Result r)
{
    switch (r)
    {
        case HAPI_RESULT_SUCCESS: return "SUCCESS";
        case HAPI_RESULT_FAILURE: return "FAILURE";
        case HAPI_RESULT_ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
        case HAPI_RESULT_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case HAPI_RESULT_CANT_LOADFILE: return "CANT_LOADFILE";
        case HAPI_RESULT_PARM_SET_FAILED: return "PARM_SET_FAILED";
        case HAPI_RESULT_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case HAPI_RESULT_CANT_LOAD_GEO: return "CANT_LOAD_GEO";
        case HAPI_RESULT_CANT_GENERATE_PRESET: return "CANT_GENERATE_PRESET";
        case HAPI_RESULT_CANT_LOAD_PRESET: return "CANT_LOAD_PRESET";
        case HAPI_RESULT_ASSET_DEF_ALREADY_LOADED: return "ASSET_DEF_ALREADY_LOADED";
        case HAPI_RESULT_NO_LICENSE_FOUND: return "NO_LICENSE_FOUND";
        case HAPI_RESULT_DISALLOWED_NC_LICENSE_FOUND: return "DISALLOWED_NC_LICENSE_FOUND";
        case HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_C_LICENSE: return "DISALLOWED_NC_ASSET_WITH_C_LICENSE";
        case HAPI_RESULT_DISALLOWED_NC_ASSET_WITH_LC_LICENSE: return "DISALLOWED_NC_ASSET_WITH_LC_LICENSE";
        case HAPI_RESULT_DISALLOWED_LC_ASSET_WITH_C_LICENSE: return "DISALLOWED_LC_ASSET_WITH_C_LICENSE";
        case HAPI_RESULT_DISALLOWED_HENGINEINDIE_W_3PARTY_PLUGIN: return "DISALLOWED_HENGINEINDIE_W_3PARTY_PLUGIN";
        case HAPI_RESULT_SHARED_MEMORY_BUFFER_OVERFLOW: return "SHARED_MEMORY_BUFFER_OVERFLOW";
        case HAPI_RESULT_INVALID_SHARED_MEMORY_BUFFER: return "INVALID_SHARED_MEMORY_BUFFER";
        case HAPI_RESULT_ASSET_INVALID: return "ASSET_INVALID";
        case HAPI_RESULT_NODE_INVALID: return "NODE_INVALID";
        case HAPI_RESULT_USER_INTERRUPTED: return "USER_INTERRUPTED";
        case HAPI_RESULT_INVALID_SESSION: return "INVALID_SESSION";
        default: return "UNKNOWN";
    }
}

bool Session::isLicenseError(HAPI_Result r)
{
    return r >= HAPI_RESULT_NO_LICENSE_FOUND && r <= HAPI_RESULT_DISALLOWED_HENGINEINDIE_W_3PARTY_PLUGIN;
}

bool Session::check(HAPI_Result result, const char* what) const
{
    if (result == HAPI_RESULT_SUCCESS) return true;
    std::ostringstream os;
    os << what << " failed: HAPI_RESULT_" << resultName(result) << " (" << int(result) << ")";
    std::string detail = lastCallError();
    if (!detail.empty()) os << "\n" << detail;
    if (result == HAPI_RESULT_INVALID_SESSION)
    {
        std::string ce = connectionError();
        if (!ce.empty()) os << "\nConnection: " << ce;
    }
    m_lastError = os.str();
    return false;
}

void Session::buildCookOptions()
{
    if (Lib::CookOptions_Create)
        m_cookOptions = Lib::CookOptions_Create();
    else
        std::memset(&m_cookOptions, 0, sizeof(m_cookOptions));

    const SessionOptions& o = m_options;
    m_cookOptions.splitGeosByGroup = o.splitGeosByGroup;
    m_cookOptions.splitGeosByAttribute = o.splitGeosByAttribute;
    m_cookOptions.maxVerticesPerPrimitive = o.maxVerticesPerPrimitive;
    m_cookOptions.refineCurveToLinear = o.refineCurveToLinear;
    m_cookOptions.curveRefineLOD = o.curveRefineLOD;
    m_cookOptions.clearErrorsAndWarnings = false;
    m_cookOptions.cookTemplatedGeos = o.cookTemplatedGeos;
    m_cookOptions.splitPointsByVertexAttributes = o.splitPointsByVertexAttributes;
    m_cookOptions.packedPrimInstancingMode = o.packedPrimInstancingMode;
    m_cookOptions.handleBoxPartTypes = o.handleBoxPartTypes;
    m_cookOptions.handleSpherePartTypes = o.handleSpherePartTypes;
    m_cookOptions.checkPartChanges = o.checkPartChanges;
    m_cookOptions.cacheMeshTopology = o.cacheMeshTopology;
    m_cookOptions.preferOutputNodes = o.preferOutputNodes;
    m_cookOptions.extraFlags = 0;

    // Group / attribute names used for splitting have to be registered as
    // custom strings on the server; done lazily after the session is up.
}

bool Session::createSession(std::string& error)
{
    HAPI_SessionInfo info = Lib::SessionInfo_Create ? Lib::SessionInfo_Create() : HAPI_SessionInfo{};
    HAPI_ThriftServerOptions server = Lib::ThriftServerOptions_Create ? Lib::ThriftServerOptions_Create()
                                                                      : HAPI_ThriftServerOptions{};
    server.autoClose = m_options.autoCloseServer;
    server.timeoutMs = m_options.serverTimeoutMs;
    server.verbosity = HAPI_STATUSVERBOSITY_WARNINGS;
    server.sharedMemoryBufferType = HAPI_THRIFT_SHARED_MEMORY_RING_BUFFER;
    server.sharedMemoryBufferSize = m_options.sharedMemoryBufferMB;
    info.sharedMemoryBufferType = server.sharedMemoryBufferType;
    info.sharedMemoryBufferSize = server.sharedMemoryBufferSize;

    const char* log = m_options.serverLogFile.empty() ? nullptr : m_options.serverLogFile.c_str();
    std::string pipe = m_options.pipeName.empty() ? defaultPipeName() : m_options.pipeName;
    HAPI_Result r = HAPI_RESULT_FAILURE;

    switch (m_options.type)
    {
        case SessionType::InProcess:
            if (!Lib::CreateInProcessSession) { error = "In-process sessions unsupported by this libHAPIL"; return false; }
            r = Lib::CreateInProcessSession(&m_session, &info);
            break;

        case SessionType::NamedPipe:
            // Connect first: a Houdini started with "-hess=pipe:<name>"
            // (Session Sync) or a HARS left over from a previous run may
            // already be listening. Only spawn HARS when that fails.
            if (Lib::ClearConnectionError) Lib::ClearConnectionError();
            r = Lib::CreateThriftNamedPipeSession(&m_session, pipe.c_str(), &info);
            if (r != HAPI_RESULT_SUCCESS && m_options.autoStartServer)
            {
                exportHarsLocation();
                ScopedCleanEnv cleanEnv(m_options.scrubEnvironmentForServer);
                r = Lib::StartThriftNamedPipeServer(&server, pipe.c_str(), &m_serverPid, log);
                if (!check(r, "HAPI_StartThriftNamedPipeServer")) { error = m_lastError; return false; }
                r = Lib::CreateThriftNamedPipeSession(&m_session, pipe.c_str(), &info);
            }
            break;

        case SessionType::Socket:
            if (Lib::ClearConnectionError) Lib::ClearConnectionError();
            r = Lib::CreateThriftSocketSession(&m_session, m_options.host.c_str(), m_options.port, &info);
            if (r != HAPI_RESULT_SUCCESS && m_options.autoStartServer)
            {
                exportHarsLocation();
                ScopedCleanEnv cleanEnv(m_options.scrubEnvironmentForServer);
                r = Lib::StartThriftSocketServer(&server, m_options.port, &m_serverPid, log);
                if (!check(r, "HAPI_StartThriftSocketServer")) { error = m_lastError; return false; }
                r = Lib::CreateThriftSocketSession(&m_session, m_options.host.c_str(), m_options.port, &info);
            }
            break;

        case SessionType::SharedMemory:
        {
            if (!Lib::CreateThriftSharedMemorySession)
            {
                error = "Shared memory sessions need Houdini 20.5 or newer; falling back is up to the caller";
                return false;
            }
            std::string shm = m_options.sharedMemoryName.empty() ? pipe : m_options.sharedMemoryName;
            if (Lib::ClearConnectionError) Lib::ClearConnectionError();
            r = Lib::CreateThriftSharedMemorySession(&m_session, shm.c_str(), &info);
            if (r != HAPI_RESULT_SUCCESS && m_options.autoStartServer)
            {
                exportHarsLocation();
                ScopedCleanEnv cleanEnv(m_options.scrubEnvironmentForServer);
                r = Lib::StartThriftSharedMemoryServer(&server, shm.c_str(), &m_serverPid, log);
                if (!check(r, "HAPI_StartThriftSharedMemoryServer")) { error = m_lastError; return false; }
                r = Lib::CreateThriftSharedMemorySession(&m_session, shm.c_str(), &info);
            }
            break;
        }
    }

    if (!check(r, "HAPI_Create*Session"))
    {
        error = m_lastError;
        return false;
    }
    m_valid = true;
    return true;
}

bool Session::initialize(std::string& error)
{
    buildCookOptions();

    auto cstr = [](const std::string& s) -> const char* { return s.empty() ? nullptr : s.c_str(); };
    HAPI_Result r = Lib::Initialize(&m_session, &m_cookOptions,
                                    m_options.useCookingThread,
                                    m_options.cookingThreadStackSizeMB,
                                    cstr(m_options.houdiniEnvironmentFiles),
                                    cstr(m_options.otlSearchPath),
                                    cstr(m_options.dsoSearchPath),
                                    cstr(m_options.imageDsoSearchPath),
                                    cstr(m_options.audioDsoSearchPath));
    // ALREADY_INITIALIZED happens when attaching to a running Houdini
    // (Session Sync) or a HARS someone else started. That is fine.
    if (r != HAPI_RESULT_SUCCESS && r != HAPI_RESULT_ALREADY_INITIALIZED)
    {
        check(r, "HAPI_Initialize");
        error = m_lastError;
        if (isLicenseError(r))
            error += "\nNo usable Houdini Engine license was found. Every machine that cooks needs one "
                     "(Houdini Engine, Indie or a full Houdini FX license).";
        return false;
    }
    m_initialized = true;

    // Tell Houdini who we are (shows up in licensing / crash logs).
    if (Lib::SetServerEnvString)
        Lib::SetServerEnvString(&m_session, HAPI_ENV_CLIENT_NAME, "cinema4d");

    // Register split names now that the server is up.
    if (Lib::SetCustomString)
    {
        if (m_options.splitGeosByGroup && !m_options.splitGroupName.empty())
            Lib::SetCustomString(&m_session, m_options.splitGroupName.c_str(), &m_cookOptions.splitGroupSH);
        if (m_options.splitGeosByAttribute && !m_options.splitAttributeName.empty())
            Lib::SetCustomString(&m_session, m_options.splitAttributeName.c_str(), &m_cookOptions.splitAttrSH);
    }
    return true;
}

bool Session::start(const SessionOptions& options, std::string& error)
{
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    if (isValid() && m_initialized) return true;
    if (!Lib::isLoaded())
    {
        error = "libHAPIL is not loaded; call hapi::Lib::load() first";
        return false;
    }
    stop();
    m_options = options;
    if (!createSession(error)) return false;
    if (!initialize(error)) { stop(); return false; }
    return true;
}

void Session::stop()
{
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    if (m_valid)
    {
        if (m_initialized && Lib::Cleanup) Lib::Cleanup(&m_session);
        if (Lib::CloseSession) Lib::CloseSession(&m_session);
    }
    m_valid = false;
    m_initialized = false;
    m_serverPid = 0;
    m_time = -1.0;
}

bool Session::isValid() const
{
    if (!m_valid || !Lib::IsSessionValid) return false;
    return Lib::IsSessionValid(&m_session) == HAPI_RESULT_SUCCESS;
}

bool Session::isInitialized() const { return m_valid && m_initialized; }

std::string Session::getString(HAPI_StringHandle h) const
{
    if (!m_valid || h < 0) return std::string();
    int len = 0;
    if (Lib::GetStringBufLength(&m_session, h, &len) != HAPI_RESULT_SUCCESS || len <= 1)
        return std::string();
    std::string s(size_t(len), '\0');
    if (Lib::GetString(&m_session, h, &s[0], len) != HAPI_RESULT_SUCCESS)
        return std::string();
    s.resize(std::strlen(s.c_str()));
    return s;
}

std::vector<std::string> Session::getStrings(const std::vector<HAPI_StringHandle>& handles) const
{
    std::vector<std::string> out(handles.size());
    if (handles.empty() || !m_valid) return out;

    // Batched path: one round trip instead of two per string. Important over
    // a pipe when an HDA has hundreds of parameters.
    if (Lib::GetStringBatchSize && Lib::GetStringBatch)
    {
        int size = 0;
        if (Lib::GetStringBatchSize(&m_session, handles.data(), int(handles.size()), &size) == HAPI_RESULT_SUCCESS && size > 0)
        {
            std::vector<char> buf(size_t(size) + 1, '\0');
            if (Lib::GetStringBatch(&m_session, buf.data(), size) == HAPI_RESULT_SUCCESS)
            {
                // Null separated, in the order of the handle array. Duplicate
                // handles are only returned once by HAPI, hence the map.
                size_t pos = 0, idx = 0;
                std::vector<std::string> unique;
                while (pos < size_t(size) && idx < handles.size())
                {
                    const char* s = buf.data() + pos;
                    size_t l = std::strlen(s);
                    unique.push_back(std::string(s, l));
                    pos += l + 1;
                    ++idx;
                }
                if (unique.size() == handles.size())
                {
                    out = unique;
                    return out;
                }
                // Fall through to the slow path if HAPI de-duplicated.
            }
        }
    }
    for (size_t i = 0; i < handles.size(); ++i) out[i] = getString(handles[i]);
    return out;
}

std::string Session::lastCallError() const
{
    if (!m_valid || !Lib::GetStatusStringBufLength) return std::string();
    int len = 0;
    if (Lib::GetStatusStringBufLength(&m_session, HAPI_STATUS_CALL_RESULT, HAPI_STATUSVERBOSITY_ERRORS, &len) != HAPI_RESULT_SUCCESS || len <= 1)
        return std::string();
    std::string s(size_t(len), '\0');
    Lib::GetStatusString(&m_session, HAPI_STATUS_CALL_RESULT, &s[0], len);
    s.resize(std::strlen(s.c_str()));
    return s;
}

std::string Session::cookResultText(HAPI_StatusVerbosity verbosity) const
{
    if (!m_valid) return std::string();
    int len = 0;
    if (Lib::GetStatusStringBufLength(&m_session, HAPI_STATUS_COOK_RESULT, verbosity, &len) != HAPI_RESULT_SUCCESS || len <= 1)
        return std::string();
    std::string s(size_t(len), '\0');
    Lib::GetStatusString(&m_session, HAPI_STATUS_COOK_RESULT, &s[0], len);
    s.resize(std::strlen(s.c_str()));
    return s;
}

std::string Session::nodeCookResultText(HAPI_NodeId node, HAPI_StatusVerbosity verbosity) const
{
    if (!m_valid || !Lib::ComposeNodeCookResult) return cookResultText(verbosity);
    int len = 0;
    if (Lib::ComposeNodeCookResult(&m_session, node, verbosity, &len) != HAPI_RESULT_SUCCESS || len <= 1)
        return std::string();
    std::string s(size_t(len), '\0');
    Lib::GetComposedNodeCookResult(&m_session, &s[0], len);
    s.resize(std::strlen(s.c_str()));
    return s;
}

std::string Session::connectionError() const
{
    if (!Lib::GetConnectionErrorLength) return std::string();
    int len = 0;
    if (Lib::GetConnectionErrorLength(&len) != HAPI_RESULT_SUCCESS || len <= 1) return std::string();
    std::string s(size_t(len), '\0');
    Lib::GetConnectionError(&s[0], len, true);
    s.resize(std::strlen(s.c_str()));
    return s;
}

CookResult Session::cook(HAPI_NodeId node,
                         const std::function<bool()>& shouldAbort,
                         const std::function<void(int, int)>& progress)
{
    CookResult res;
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    if (!isInitialized()) { res.message = "No Houdini Engine session"; return res; }

    // Houdini's Python interpreter lock: without it, HDAs with Python
    // callbacks can deadlock against the host (Maya plugin, Util::PythonInterpreterLock).
    PythonLock pyLock(m_session);
    if (!check(Lib::CookNode(&m_session, node, &m_cookOptions), "HAPI_CookNode"))
    {
        res.message = m_lastError;
        return res;
    }

    // Poll the cook state. With use_cooking_thread the server cooks in the
    // background and HAPI_CookNode returns immediately.
    int state = HAPI_STATE_STARTING_COOK;
    int sleepMs = 1;
    for (;;)
    {
        if (Lib::GetStatus(&m_session, HAPI_STATUS_COOK_STATE, &state) != HAPI_RESULT_SUCCESS)
        {
            res.message = "HAPI_GetStatus failed: " + lastCallError();
            return res;
        }
        if (state <= HAPI_STATE_MAX_READY_STATE) break;

        if (progress && Lib::GetCookingCurrentCount && Lib::GetCookingTotalCount)
        {
            int cur = 0, total = 0;
            Lib::GetCookingCurrentCount(&m_session, &cur);
            Lib::GetCookingTotalCount(&m_session, &total);
            progress(cur, total);
        }
        if (shouldAbort && shouldAbort())
        {
            if (Lib::Interrupt) Lib::Interrupt(&m_session);
            res.aborted = true;
            res.message = "Cook interrupted";
            return res;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        if (sleepMs < 16) sleepMs *= 2;   // back off: 1,2,4,8,16ms
    }

    switch (state)
    {
        case HAPI_STATE_READY:
            res.succeeded = true;
            break;
        case HAPI_STATE_READY_WITH_COOK_ERRORS:
            // Houdini still produced output; surface the messages but use it.
            res.succeeded = true;
            res.hadCookErrors = true;
            res.message = nodeCookResultText(node, HAPI_STATUSVERBOSITY_WARNINGS);
            break;
        case HAPI_STATE_READY_WITH_FATAL_ERRORS:
        default:
            res.succeeded = false;
            res.message = cookResultText(HAPI_STATUSVERBOSITY_ERRORS);
            if (res.message.empty()) res.message = "Cook failed with fatal errors";
            break;
    }
    return res;
}

bool Session::setTime(double seconds)
{
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    if (!isInitialized()) return false;
    if (m_time == seconds) return false;
    if (!check(Lib::SetTime(&m_session, seconds), "HAPI_SetTime")) return false;
    m_time = seconds;
    return true;
}

bool Session::setTimeline(double fps, double startSeconds, double endSeconds)
{
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    if (!isInitialized() || !Lib::SetTimelineOptions) return false;
    HAPI_TimelineOptions t = Lib::TimelineOptions_Create ? Lib::TimelineOptions_Create() : HAPI_TimelineOptions{};
    t.fps = fps;
    t.startTime = startSeconds;
    t.endTime = endSeconds;
    return check(Lib::SetTimelineOptions(&m_session, &t), "HAPI_SetTimelineOptions");
}

} // namespace hapi
