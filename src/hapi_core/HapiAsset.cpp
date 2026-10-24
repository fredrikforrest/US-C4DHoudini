#include "HapiAsset.h"

namespace hapi {

bool loadAssetLibrary(const std::string& hdaPath, AssetLibrary& out, bool allowOverwrite)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) { s.check(HAPI_RESULT_INVALID_SESSION, "loadAssetLibrary"); return false; }

    out = AssetLibrary();
    out.path = hdaPath;
    HAPI_Result r = Lib::LoadAssetLibraryFromFile(h, hdaPath.c_str(), allowOverwrite, &out.id);
    // When overwrite is disallowed and the same file is already loaded HAPI
    // reports ASSET_DEF_ALREADY_LOADED and still returns the id; accept it.
    if (r != HAPI_RESULT_SUCCESS && r != HAPI_RESULT_ASSET_DEF_ALREADY_LOADED)
        return s.check(r, "HAPI_LoadAssetLibraryFromFile");

    int count = 0;
    if (!s.check(Lib::GetAvailableAssetCount(h, out.id, &count), "HAPI_GetAvailableAssetCount")) return false;
    if (count > 0)
    {
        std::vector<HAPI_StringHandle> names(static_cast<size_t>(count));
        if (!s.check(Lib::GetAvailableAssets(h, out.id, names.data(), count), "HAPI_GetAvailableAssets")) return false;
        out.assetNames = s.getStrings(names);
    }
    return true;
}

bool instantiateAsset(const std::string& operatorName, const std::string& label, HAPI_NodeId& outNode)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) { s.check(HAPI_RESULT_INVALID_SESSION, "instantiateAsset"); return false; }
    outNode = -1;
    // parent -1: HAPI creates an OBJ container for SOP assets automatically.
    return s.check(Lib::CreateNode(h, -1, operatorName.c_str(), label.empty() ? nullptr : label.c_str(), false, &outNode),
                   "HAPI_CreateNode");
}

bool deleteNode(HAPI_NodeId node)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h || node < 0) return false;
    return s.check(Lib::DeleteNode(h, node), "HAPI_DeleteNode");
}

bool nodeIsValid(HAPI_NodeId node, int uniqueNodeId)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h || node < 0 || !Lib::IsNodeValid) return false;
    HAPI_Bool ok = false;
    if (Lib::IsNodeValid(h, node, uniqueNodeId, &ok) != HAPI_RESULT_SUCCESS) return false;
    return ok;
}

bool getAssetInfo(HAPI_NodeId node, AssetInfo& out)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) return false;

    out = AssetInfo();
    HAPI_AssetInfo ai = Lib::AssetInfo_Create ? Lib::AssetInfo_Create() : HAPI_AssetInfo{};
    if (!s.check(Lib::GetAssetInfo(h, node, &ai), "HAPI_GetAssetInfo")) return false;
    HAPI_NodeInfo ni = Lib::NodeInfo_Create ? Lib::NodeInfo_Create() : HAPI_NodeInfo{};
    if (!s.check(Lib::GetNodeInfo(h, node, &ni), "HAPI_GetNodeInfo")) return false;

    out.nodeId = ai.nodeId;
    out.objectNodeId = ai.objectNodeId;
    out.nodeType = ni.type;
    out.isTimeDependent = ni.isTimeDependent;
    out.geoInputCount = ai.geoInputCount;
    out.transformInputCount = ai.transformInputCount;
    out.objectCount = ai.objectCount;

    std::vector<HAPI_StringHandle> hs = {ai.nameSH, ai.labelSH, ai.fullOpNameSH, ai.versionSH, ai.helpTextSH, ai.helpURLSH, ai.filePathSH};
    std::vector<std::string> str = s.getStrings(hs);
    out.name = str[0]; out.label = str[1]; out.fullOpName = str[2]; out.version = str[3];
    out.helpText = str[4]; out.helpUrl = str[5]; out.filePath = str[6];

    // Input labels (e.g. "Input Geometry", "Collision Mesh").
    for (int i = 0; i < ni.inputCount; ++i)
    {
        AssetInput in;
        in.index = i;
        HAPI_StringHandle sh = 0;
        if (Lib::GetNodeInputName && Lib::GetNodeInputName(h, node, i, &sh) == HAPI_RESULT_SUCCESS)
            in.name = s.getString(sh);
        if (in.name.empty()) in.name = "Input " + std::to_string(i + 1);
        out.inputs.push_back(in);
    }
    return true;
}

} // namespace hapi
