// HapiAsset: loading .hda libraries and instantiating/deleting asset nodes.
#pragma once

#include "HapiSession.h"

#include <string>
#include <vector>

namespace hapi {

struct AssetLibrary
{
    HAPI_AssetLibraryId id = -1;
    std::string path;
    std::vector<std::string> assetNames;   // full operator names, e.g. "Sop/us::rock_generator::1.0"
};

struct AssetInput
{
    int index = 0;
    std::string name;   // label from HAPI_GetNodeInputName
};

struct AssetInfo
{
    HAPI_NodeId nodeId = -1;
    HAPI_NodeId objectNodeId = -1;
    HAPI_NodeType nodeType = HAPI_NODETYPE_NONE;
    std::string name, label, fullOpName, version, helpText, helpUrl, filePath;
    int geoInputCount = 0;
    int transformInputCount = 0;
    int objectCount = 0;
    std::vector<AssetInput> inputs;
    bool isTimeDependent = false;
};

// Load (or reload) a .hda. allowOverwrite=true lets the user re-save the
// asset in Houdini and have the next cook pick it up.
bool loadAssetLibrary(const std::string& hdaPath, AssetLibrary& out, bool allowOverwrite = true);

// Instantiate an asset by operator name. Returns the asset node id. The
// node is created without cooking so parameters can be set first.
bool instantiateAsset(const std::string& operatorName, const std::string& label, HAPI_NodeId& outNode);

bool deleteNode(HAPI_NodeId node);
bool getAssetInfo(HAPI_NodeId node, AssetInfo& out);
bool nodeIsValid(HAPI_NodeId node, int uniqueNodeId);

} // namespace hapi
