// Plugin entry points. Registers the Houdini Asset generator object and two
// commands (session info / restart).
#include "c4d.h"
#include "c4d_symbols.h"
#include "HoudiniAssetObject.h"
#include "HoudiniSessionManager.h"

namespace {

// TODO: obtain unique ids from https://developers.maxon.net (Plugin Cafe)
// before shipping. These are placeholders in the public range.
const Int32 ID_CMD_SESSION_INFO = 1064321;
const Int32 ID_CMD_SESSION_RESTART = 1064322;

class SessionInfoCommand : public CommandData
{
public:
    virtual Bool Execute(BaseDocument* doc, GeDialog* parentManager)
    {
        c4dhoudini::SessionManager& sm = c4dhoudini::SessionManager::get();
        maxon::String err;
        sm.ensure(&err);
        maxon::String msg = sm.statusLine();
        if (!err.IsEmpty()) msg += "\n\n"_s + err;
        if (hapi::Lib::isLoaded())
        {
            msg += "\n\nlibHAPIL: "_s + c4dhoudini::ToC4D(hapi::Lib::info().libraryPath);
            msg += "\nHARS: "_s + c4dhoudini::ToC4D(hapi::Lib::info().harsPath);
        }
        msg += "\n\nOverride with env HAPI_LIBRARY_PATH or HFS, or the plugin preferences container."_s;
        MessageDialog(msg);
        return true;
    }
};

class SessionRestartCommand : public CommandData
{
public:
    virtual Bool Execute(BaseDocument* doc, GeDialog* parentManager)
    {
        c4dhoudini::SessionManager::get().shutdown();
        maxon::String err;
        if (!c4dhoudini::SessionManager::get().ensure(&err))
            MessageDialog("Houdini Engine session could not be started:\n\n"_s + err);
        else
            MessageDialog(c4dhoudini::SessionManager::get().statusLine());
        // Every asset node is gone with the old session; force re-instantiation.
        c4dhoudini::HoudiniAssetObject::InvalidateAllAssets();
        if (doc) EventAdd();
        return true;
    }
};

} // namespace

Bool PluginStart()
{
    if (!c4dhoudini::RegisterHoudiniAssetObject()) return false;
    RegisterCommandPlugin(ID_CMD_SESSION_INFO, GeLoadString(IDS_HOUDINI_SESSION_INFO), 0, nullptr,
                          "Show Houdini Engine session state"_s, NewObjClear(SessionInfoCommand));
    RegisterCommandPlugin(ID_CMD_SESSION_RESTART, GeLoadString(IDS_HOUDINI_SESSION_RESTART), 0, nullptr,
                          "Stop and restart the Houdini Engine session"_s, NewObjClear(SessionRestartCommand));
    return true;
}

void PluginEnd()
{
    c4dhoudini::SessionManager::get().shutdown();
}

Bool PluginMessage(Int32 id, void* data)
{
    switch (id)
    {
        case C4DPL_INIT_SYS:
            if (!g_resource.Init()) return false;   // load res/ files
            return true;
        case C4DMSG_PRIORITY:
            return true;
        case C4DPL_ENDACTIVITY:
            // Shut HARS down before Cinema unloads libraries.
            c4dhoudini::SessionManager::get().shutdown();
            return true;
        default:
            break;
    }
    return false;
}
