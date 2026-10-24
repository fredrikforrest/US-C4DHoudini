// HoudiniSessionManager: plugin-wide access to the one Houdini Engine
// session, plus the preferences that decide where Houdini lives.
#pragma once

#include "c4d.h"
#include "HapiSession.h"

#include <string>

namespace c4dhoudini {

struct Prefs
{
    maxon::String libraryPath;     // explicit libHAPIL; empty = auto detect
    Int32 sessionType = 0;         // 0 pipe, 1 socket, 2 shared memory, 3 in-process
    Bool connectToExisting = true; // try an already running HARS / Houdini (-hess) first
    Int32 port = 9090;
    Bool scrubEnvironment = true;
    maxon::String otlSearchPath;
    maxon::String logFile;
};

class SessionManager
{
public:
    static SessionManager& get();

    // Loads libHAPIL and starts the session on first use. Cheap when already up.
    Bool ensure(maxon::String* errorOut = nullptr);
    void shutdown();
    Bool isUp() const;

    maxon::String statusLine() const;    // "Houdini 22.0.475 / Engine 9.0, pipe hapi_c4d_1234"
    const maxon::String& lastError() const { return m_lastError; }

    Prefs prefs() const;
    void setPrefs(const Prefs& p);

private:
    SessionManager() = default;
    Bool m_triedOnce = false;
    maxon::String m_lastError;
};

// Console + status helpers.
inline maxon::String ToC4D(const std::string& s) { return maxon::String(s.c_str()); }
std::string ToStd(const maxon::String& s);

} // namespace c4dhoudini
