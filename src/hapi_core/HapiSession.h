// HapiSession: owns the single Houdini Engine session used by the plugin.
//
// Design decisions (all borrowed from the SideFX plugins):
//  * One global session shared by every HDA in the scene, created lazily.
//  * Out-of-process by default (Thrift named pipe to a HARS child process):
//    a Houdini crash cannot take Cinema 4D down, and Houdini's Python/Qt/
//    OpenSSL never collide with Cinema's. In-process is available for
//    benchmarking only.
//  * A single mutex serialises HAPI calls. HAPI itself is not thread safe
//    and Cinema 4D evaluates generators from worker threads.
//  * Cooking is asynchronous on the server; we poll HAPI_GetStatus so the
//    host can show progress and abort (BaseThread::TestBreak in C4D).
#pragma once

#include "HapiLibrary.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace hapi {

enum class SessionType
{
    NamedPipe,     // HARS over a named pipe (default; Unreal/Unity/Max default)
    Socket,        // HARS over TCP; also used to attach to a running Houdini (Session Sync)
    SharedMemory,  // HARS over shared memory (fastest IPC, Houdini 20.5+)
    InProcess      // libHAPIL inside the host process (no crash isolation)
};

struct SessionOptions
{
    SessionType type = SessionType::NamedPipe;

    // Transport specifics.
    std::string pipeName;                 // empty = "hapi_c4d_<pid>"
    std::string host = "localhost";
    int port = 9090;
    std::string sharedMemoryName;         // empty = pipeName
    int sharedMemoryBufferMB = 512;

    // Server lifecycle.
    bool autoStartServer = true;          // spawn HARS ourselves
    bool autoCloseServer = true;          // HARS exits when we disconnect
    float serverTimeoutMs = 20000.0f;     // wait for HARS to come up
    std::string serverLogFile;            // empty = no log
    bool scrubEnvironmentForServer = true; // drop LD_LIBRARY_PATH/PYTHONPATH while spawning HARS

    // HAPI_Initialize arguments.
    bool useCookingThread = true;
    int cookingThreadStackSizeMB = -1;    // -1 = Houdini default
    std::string houdiniEnvironmentFiles;  // ';'-separated .env files
    std::string otlSearchPath;
    std::string dsoSearchPath;
    std::string imageDsoSearchPath;
    std::string audioDsoSearchPath;

    // Cook options applied to every cook (see HAPI_CookOptions).
    bool splitGeosByGroup = false;
    std::string splitGroupName;
    bool splitGeosByAttribute = false;
    std::string splitAttributeName;
    int maxVerticesPerPrimitive = -1;     // -1 = no limit (C4D handles n-gons)
    bool refineCurveToLinear = false;
    float curveRefineLOD = 8.0f;
    bool cookTemplatedGeos = false;
    bool splitPointsByVertexAttributes = false;
    HAPI_PackedPrimInstancingMode packedPrimInstancingMode = HAPI_PACKEDPRIM_INSTANCING_MODE_FLAT;
    bool handleBoxPartTypes = false;
    bool handleSpherePartTypes = false;
    bool checkPartChanges = true;
    bool cacheMeshTopology = false;
    bool preferOutputNodes = true;
};

// Result of a cook, including anything Houdini wanted to tell the user.
struct CookResult
{
    bool succeeded = false;      // READY or READY_WITH_COOK_ERRORS
    bool hadCookErrors = false;  // READY_WITH_COOK_ERRORS
    bool aborted = false;        // caller asked us to stop
    std::string message;         // errors/warnings text
};

class Session
{
public:
    static Session& get();

    bool start(const SessionOptions& options, std::string& error);
    void stop();
    bool isValid() const;              // handle valid and HAPI_IsSessionValid
    bool isInitialized() const;

    const HAPI_Session* handle() const { return m_valid ? &m_session : nullptr; }
    const SessionOptions& options() const { return m_options; }
    const HAPI_CookOptions& cookOptions() const { return m_cookOptions; }
    HAPI_ProcessId serverProcessId() const { return m_serverPid; }

    // Every caller must hold this while issuing HAPI calls.
    std::recursive_mutex& mutex() { return m_mutex; }

    // ---- small helpers that every other module uses ----------------------
    std::string getString(HAPI_StringHandle handle) const;
    std::vector<std::string> getStrings(const std::vector<HAPI_StringHandle>& handles) const;

    // Human readable text for the last failed HAPI call / last cook.
    std::string lastCallError() const;
    std::string cookResultText(HAPI_StatusVerbosity verbosity = HAPI_STATUSVERBOSITY_WARNINGS) const;
    std::string nodeCookResultText(HAPI_NodeId node, HAPI_StatusVerbosity verbosity = HAPI_STATUSVERBOSITY_WARNINGS) const;
    std::string connectionError() const;

    // Checks a HAPI_Result; on failure records a message retrievable through
    // lastError() and returns false.
    bool check(HAPI_Result result, const char* what) const;
    const std::string& lastError() const { return m_lastError; }
    void clearLastError() { m_lastError.clear(); }

    // Cook a node and wait. shouldAbort is polled between status checks;
    // progress receives (current, total) from HAPI_GetCookingCurrentCount.
    CookResult cook(HAPI_NodeId node,
                    const std::function<bool()>& shouldAbort = nullptr,
                    const std::function<void(int, int)>& progress = nullptr);

    // Timeline: fps + current time. Returns true if the time changed.
    bool setTime(double seconds);
    bool setTimeline(double fps, double startSeconds, double endSeconds);
    double currentTime() const { return m_time; }

    // Returns true if HAPI_RESULT_* indicates a licensing problem.
    static bool isLicenseError(HAPI_Result r);
    static const char* resultName(HAPI_Result r);

private:
    Session() = default;
    bool createSession(std::string& error);
    bool initialize(std::string& error);
    void buildCookOptions();

    mutable std::recursive_mutex m_mutex;
    HAPI_Session m_session{};
    bool m_valid = false;
    bool m_initialized = false;
    SessionOptions m_options;
    HAPI_CookOptions m_cookOptions{};
    HAPI_ProcessId m_serverPid = 0;
    double m_time = -1.0;
    mutable std::string m_lastError;
};

// RAII wrapper for HAPI_PythonThreadInterpreterLock.
struct PythonLock
{
    explicit PythonLock(const HAPI_Session& s);
    ~PythonLock();
    const HAPI_Session& session;
};

// Scope guard for the session mutex.
struct SessionLock
{
    SessionLock() : lock(Session::get().mutex()) {}
    std::unique_lock<std::recursive_mutex> lock;
};

} // namespace hapi
