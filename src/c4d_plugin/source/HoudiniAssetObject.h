// HoudiniAssetObject: the "Houdini Asset" generator object.
//
// One instance owns one asset node inside the shared Houdini Engine session.
// Its Attribute Manager interface is the HDA's parameter interface, built on
// the fly from hapi::ParmSet in GetDDescription. Children and the Input link
// fields are marshalled into Houdini as input geometry. GetVirtualObjects
// pushes changed parameters, cooks, and converts the result.
//
// Written against the Cinema 4D 2024/2025 C++ SDK. Older SDKs differ in a few
// virtual signatures (Init, GetDEnabling const-ness); see README.
#pragma once

#include "c4d.h"
#include "HapiAsset.h"
#include "HapiGeometry.h"
#include "HapiParms.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace c4dhoudini {

class HoudiniAssetObject : public ObjectData
{
    INSTANCEOF(HoudiniAssetObject, ObjectData)

public:
    static NodeData* Alloc() { return NewObjClear(HoudiniAssetObject); }

    virtual Bool Init(GeListNode* node, Bool isCloneInit);
    virtual void Free(GeListNode* node);
    virtual Bool Read(GeListNode* node, HyperFile* hf, Int32 level);
    virtual Bool Write(const GeListNode* node, HyperFile* hf) const;
    virtual Bool CopyTo(NodeData* dest, const GeListNode* snode, GeListNode* dnode, COPYFLAGS flags, AliasTrans* trn) const;

    virtual BaseObject* GetVirtualObjects(BaseObject* op, const HierarchyHelp* hh);
    virtual Bool GetDDescription(const GeListNode* node, Description* description, DESCFLAGS_DESC& flags);
    virtual Bool GetDEnabling(const GeListNode* node, const DescID& id, const GeData& t_data, DESCFLAGS_ENABLE flags, const BaseContainer* itemdesc) const;
    virtual Bool GetDParameter(const GeListNode* node, const DescID& id, GeData& t_data, DESCFLAGS_GET& flags) const;
    virtual Bool Message(GeListNode* node, Int32 type, void* data);

    // Called after the session was restarted: every node id is stale.
    static void InvalidateAllAssets();

private:
    // ---- asset lifecycle ----
    Bool ensureAsset(BaseObject* op, Bool forceReload = false);
    void releaseAsset();
    Bool refreshParms(BaseObject* op);
    void seedContainerFromParms(BaseObject* op);

    // ---- description ids ----
    Int32 idForParm(BaseContainer* bc, const std::string& name, Int32 component = 0);
    std::string parmForId(const BaseContainer* bc, Int32 id, Int32* component = nullptr) const;

    // ---- cooking ----
    Bool pushParameterChanges(BaseObject* op);
    Bool pushInputs(BaseObject* op, const HierarchyHelp* hh, Bool& dirty);
    void releaseInputs();
    void setStatus(const maxon::String& s);

    // Per-instance state (not saved; rebuilt from the HDA file + container).
    HAPI_NodeId m_node = -1;
    hapi::AssetInfo m_assetInfo;
    hapi::AssetLibrary m_library;
    hapi::ParmSet m_parms;
    Filename m_loadedFile;
    Int32 m_loadedAssetIndex = -1;
    Bool m_needsCook = true;
    Float m_cookedTime = -1.0;
    std::vector<HAPI_NodeId> m_inputNodes;      // input SOPs we created
    std::vector<HAPI_NodeId> m_inputMerges;
    std::vector<char> m_preset;                 // saved parm state (binary preset)
    maxon::String m_status;
    maxon::String m_cookLog;
    Int32 m_sessionGeneration = 0;
    mutable std::map<std::string, Int32> m_idCache;   // parm key -> description id
    mutable std::recursive_mutex m_mutex;             // GVO thread vs. UI thread

    static Int32 s_sessionGeneration;
    static std::set<HoudiniAssetObject*> s_instances;
};

Bool RegisterHoudiniAssetObject();

} // namespace c4dhoudini
