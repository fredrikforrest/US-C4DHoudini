#include "HoudiniAssetObject.h"

#include "HoudiniGeoConvert.h"
#include "HoudiniSessionManager.h"
#include "c4d_symbols.h"
#include "ohoudiniasset.h"
#include "lib_description.h"
#include "customgui_description.h"

#include <cmath>
#include <functional>

namespace c4dhoudini {

using namespace hapi;

namespace {
// TODO: replace with a unique id from https://developers.maxon.net before shipping.
const Int32 ID_HOUDINI_ASSET_OBJECT = 1064320;

maxon::String S(const std::string& s) { return maxon::String(s.c_str()); }

std::string componentKey(const std::string& name, Int32 component)
{
    return component ? name + "#" + std::to_string(component) : name;
}
} // namespace

Int32 HoudiniAssetObject::s_sessionGeneration = 1;
std::set<HoudiniAssetObject*> HoudiniAssetObject::s_instances;

// ---------------------------------------------------------------------------
// NodeData lifecycle
// ---------------------------------------------------------------------------

Bool HoudiniAssetObject::Init(GeListNode* node, Bool isCloneInit)
{
    BaseObject* op = static_cast<BaseObject*>(node);
    BaseContainer* bc = op->GetDataInstance();
    if (!bc) return false;
    if (!isCloneInit)
    {
        bc->SetBool(HDA_AUTO_COOK, true);
        bc->SetBool(HDA_COOK_ON_FRAME, true);
        bc->SetFloat(HDA_SCALE, 100.0);
        bc->SetBool(HDA_FLIP_Z, true);
        bc->SetBool(HDA_REVERSE_WINDING, false);
        bc->SetBool(HDA_IMPORT_NORMALS, true);
        bc->SetBool(HDA_IMPORT_UVS, true);
        bc->SetBool(HDA_IMPORT_COLORS, true);
        bc->SetBool(HDA_IMPORT_GROUPS, true);
        bc->SetBool(HDA_IMPORT_MATERIALS, true);
        bc->SetInt32(HDA_INSTANCE_MODE, HDA_INSTANCE_MODE_MULTI);
        bc->SetFloat(HDA_PHONG_ANGLE, DegToRad(40.0));
        bc->SetInt32(HDA_PARM_NEXT_ID, HDA_DYNAMIC_PARM_BASE);
        bc->SetContainer(HDA_PARM_NAME_MAP, BaseContainer());
    }
    m_sessionGeneration = s_sessionGeneration;
    s_instances.insert(this);
    m_status = "Idle"_s;
    return true;
}

void HoudiniAssetObject::Free(GeListNode* node)
{
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    releaseInputs();
    releaseAsset();
    s_instances.erase(this);
}

Bool HoudiniAssetObject::Read(GeListNode* node, HyperFile* hf, Int32 level)
{
    Int32 version = 0;
    if (!hf->ReadInt32(&version)) return false;
    void* data = nullptr;
    Int size = 0;
    if (!hf->ReadMemory(&data, &size)) return false;
    m_preset.clear();
    if (data && size > 0)
    {
        m_preset.assign(static_cast<char*>(data), static_cast<char*>(data) + size);
    }
    if (data) DeleteMem(data);
    return true;
}

Bool HoudiniAssetObject::Write(const GeListNode* node, HyperFile* hf) const
{
    if (!hf->WriteInt32(1)) return false;
    // Houdini's binary preset holds every parm value (including multiparm
    // instances and ramps the description cannot show yet), so a scene
    // reopened on a machine with Houdini restores the exact state.
    static const char empty = 0;
    return hf->WriteMemory(m_preset.empty() ? &empty : m_preset.data(), Int(m_preset.size()));
}

Bool HoudiniAssetObject::CopyTo(NodeData* dest, const GeListNode* snode, GeListNode* dnode, COPYFLAGS flags, AliasTrans* trn) const
{
    HoudiniAssetObject* d = static_cast<HoudiniAssetObject*>(dest);
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    d->m_preset = m_preset;
    if (d->m_preset.empty() && m_node >= 0) getPreset(m_node, d->m_preset);
    // The copy must instantiate its own asset node; never share HAPI nodes.
    d->m_node = -1;
    d->m_loadedFile = Filename();
    d->m_loadedAssetIndex = -1;
    d->m_parms = ParmSet();
    d->m_idCache.clear();
    d->m_needsCook = true;
    return SUPER::CopyTo(dest, snode, dnode, flags, trn);
}

void HoudiniAssetObject::InvalidateAllAssets()
{
    ++s_sessionGeneration;
    for (HoudiniAssetObject* o : s_instances)
    {
        o->m_node = -1;
        o->m_inputNodes.clear();
        o->m_inputMerges.clear();
        o->m_loadedFile = Filename();
        o->m_needsCook = true;
    }
}

// ---------------------------------------------------------------------------
// Asset lifecycle
// ---------------------------------------------------------------------------

void HoudiniAssetObject::setStatus(const maxon::String& s) { m_status = s; }

void HoudiniAssetObject::releaseAsset()
{
    if (m_node >= 0 && m_sessionGeneration == s_sessionGeneration) deleteNode(m_node);
    m_node = -1;
    m_parms = ParmSet();
}

void HoudiniAssetObject::releaseInputs()
{
    if (m_sessionGeneration == s_sessionGeneration)
    {
        for (HAPI_NodeId n : m_inputMerges) deleteNode(n);
        for (HAPI_NodeId n : m_inputNodes) deleteNode(n);
    }
    m_inputMerges.clear();
    m_inputNodes.clear();
}

Bool HoudiniAssetObject::refreshParms(BaseObject* op)
{
    if (m_node < 0) return false;
    return fetchParms(m_node, m_parms);
}

Bool HoudiniAssetObject::ensureAsset(BaseObject* op, Bool forceReload)
{
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    BaseContainer* bc = op->GetDataInstance();
    const Filename file = bc->GetFilename(HDA_FILE);
    Int32 assetIndex = bc->GetInt32(HDA_ASSET_INDEX);

    if (m_sessionGeneration != s_sessionGeneration)
    {
        m_node = -1;
        m_inputNodes.clear();
        m_inputMerges.clear();
        m_sessionGeneration = s_sessionGeneration;
    }
    if (!file.IsPopulated())
    {
        releaseAsset();
        m_loadedFile = Filename();
        setStatus("No HDA file selected"_s);
        return false;
    }
    const Bool sameFile = (file == m_loadedFile) && (assetIndex == m_loadedAssetIndex);
    if (m_node >= 0 && sameFile && !forceReload) return true;

    maxon::String err;
    if (!SessionManager::get().ensure(&err))
    {
        setStatus(err);
        return false;
    }

    if (!loadAssetLibrary(ToStd(file.GetString()), m_library, true))
    {
        setStatus(ToC4D(Session::get().lastError()));
        return false;
    }
    if (m_library.assetNames.empty())
    {
        setStatus("The library contains no assets"_s);
        return false;
    }
    if (assetIndex < 0 || assetIndex >= Int32(m_library.assetNames.size())) assetIndex = 0;

    // Keep the current values across a reload of the same file.
    if (m_node >= 0 && sameFile) getPreset(m_node, m_preset);
    if (!sameFile && file != m_loadedFile) m_preset.clear();
    releaseInputs();
    releaseAsset();

    if (!instantiateAsset(m_library.assetNames[size_t(assetIndex)], ToStd(op->GetName()), m_node))
    {
        setStatus(ToC4D(Session::get().lastError()));
        return false;
    }
    getAssetInfo(m_node, m_assetInfo);
    m_loadedFile = file;
    m_loadedAssetIndex = assetIndex;

    if (!m_preset.empty())
    {
        setPreset(m_node, m_preset);
        refreshParms(op);
        // Preset wins; mirror it into the container so the UI agrees.
        seedContainerFromParms(op);
    }
    else
    {
        refreshParms(op);
        // Values the user already set (scene reopened without a preset) go
        // to Houdini; everything else is seeded from the HDA defaults.
        pushParameterChanges(op);
        seedContainerFromParms(op);
    }
    m_needsCook = true;
    setStatus("Loaded "_s + S(m_assetInfo.label) + (m_assetInfo.version.empty() ? ""_s : " v"_s + S(m_assetInfo.version)));
    return true;
}

// Write every parm value into the container (overwriting), so the Attribute
// Manager shows Houdini's state. Called after instantiate/preset restore.
void HoudiniAssetObject::seedContainerFromParms(BaseObject* op)
{
    BaseContainer* bc = op->GetDataInstance();
    for (const Parm& p : m_parms.parms)
    {
        if (!p.holdsValue() || p.invisible) continue;
        if (p.isToggle())
            bc->SetBool(idForParm(bc, p.name), !p.intValues.empty() && p.intValues[0] != 0);
        else if (p.isInt())
        {
            if (p.size == 1 || p.hasMenu()) bc->SetInt32(idForParm(bc, p.name), p.intValues.empty() ? 0 : p.intValues[0]);
            else for (int k = 0; k < p.size && k < int(p.intValues.size()); ++k) bc->SetInt32(idForParm(bc, p.name, k), p.intValues[size_t(k)]);
        }
        else if (p.isColor() && p.size >= 3 && p.floatValues.size() >= 3)
        {
            bc->SetVector(idForParm(bc, p.name), Vector(p.floatValues[0], p.floatValues[1], p.floatValues[2]));
            if (p.size >= 4 && p.floatValues.size() >= 4) bc->SetFloat(idForParm(bc, p.name, 3), p.floatValues[3]);
        }
        else if (p.isFloat())
        {
            if (p.size == 1 && !p.floatValues.empty()) bc->SetFloat(idForParm(bc, p.name), p.floatValues[0]);
            else if (p.size == 3 && p.floatValues.size() >= 3) bc->SetVector(idForParm(bc, p.name), Vector(p.floatValues[0], p.floatValues[1], p.floatValues[2]));
            else for (int k = 0; k < p.size && k < int(p.floatValues.size()); ++k) bc->SetFloat(idForParm(bc, p.name, k), p.floatValues[size_t(k)]);
        }
        else if (p.isString())
        {
            if (p.isNode()) continue;   // links are owned by the user, not Houdini
            if (p.hasMenu())
            {
                Int32 idx = 0;
                const std::string cur = p.stringValues.empty() ? std::string() : p.stringValues[0];
                for (size_t i = 0; i < p.choices.size(); ++i) if (p.choices[i].value == cur) idx = Int32(i);
                bc->SetInt32(idForParm(bc, p.name), idx);
            }
            else if (p.isPath())
                bc->SetFilename(idForParm(bc, p.name), Filename(S(p.stringValues.empty() ? std::string() : p.stringValues[0])));
            else
                for (int k = 0; k < p.size && k < int(p.stringValues.size()); ++k)
                    bc->SetString(idForParm(bc, p.name, k), S(p.stringValues[size_t(k)]));
        }
    }
}

// ---------------------------------------------------------------------------
// Description id bookkeeping: parm names are stable across sessions and
// Houdini versions, description ids must be too, so the mapping lives in the
// object's container (HDA_PARM_NAME_MAP: id -> "name" or "name#component").
// ---------------------------------------------------------------------------

Int32 HoudiniAssetObject::idForParm(BaseContainer* bc, const std::string& name, Int32 component)
{
    const std::string key = componentKey(name, component);
    auto it = m_idCache.find(key);
    if (it != m_idCache.end()) return it->second;

    BaseContainer* map = bc->GetContainerInstance(HDA_PARM_NAME_MAP);
    if (!map)
    {
        bc->SetContainer(HDA_PARM_NAME_MAP, BaseContainer());
        map = bc->GetContainerInstance(HDA_PARM_NAME_MAP);
    }
    BrowseContainer browse(map);
    Int32 id = 0;
    GeData* data = nullptr;
    while (browse.GetNext(&id, &data))
    {
        if (data && ToStd(data->GetString()) == key)
        {
            m_idCache[key] = id;
            return id;
        }
    }
    Int32 next = bc->GetInt32(HDA_PARM_NEXT_ID, HDA_DYNAMIC_PARM_BASE);
    if (next < HDA_DYNAMIC_PARM_BASE) next = HDA_DYNAMIC_PARM_BASE;
    map->SetString(next, S(key));
    bc->SetInt32(HDA_PARM_NEXT_ID, next + 1);
    m_idCache[key] = next;
    return next;
}

std::string HoudiniAssetObject::parmForId(const BaseContainer* bc, Int32 id, Int32* component) const
{
    if (component) *component = 0;
    const BaseContainer* map = bc->GetContainerInstance(HDA_PARM_NAME_MAP);
    if (!map) return std::string();
    std::string key = ToStd(map->GetString(id));
    size_t hash = key.find('#');
    if (hash != std::string::npos)
    {
        if (component) *component = std::atoi(key.c_str() + hash + 1);
        key = key.substr(0, hash);
    }
    return key;
}

// ---------------------------------------------------------------------------
// Dynamic description
// ---------------------------------------------------------------------------

namespace {

struct DescBuilder
{
    Description* description;
    const DescID* singleid;
    HoudiniAssetObject* self;
    BaseContainer* bc;
    std::function<Int32(const std::string&, Int32)> idFor;

    void add(const DescID& cid, BaseContainer& item, const DescID& parent)
    {
        if (!singleid || cid.IsPartOf(*singleid, nullptr)) description->SetParameter(cid, item, parent);
    }

    static maxon::String labelOf(const Parm& p, const char* suffix = nullptr)
    {
        maxon::String l = S(p.label.empty() ? p.name : p.label);
        if (suffix) l += " "_s + maxon::String(suffix);
        return l;
    }

    BaseContainer base(Int32 dtype, const Parm& p, const char* suffix = nullptr)
    {
        BaseContainer item = GetCustomDataTypeDefault(dtype);
        item.SetString(DESC_NAME, labelOf(p, suffix));
        item.SetString(DESC_SHORT_NAME, labelOf(p, suffix));
        item.SetBool(DESC_ANIMATE, DESC_ANIMATE_ON);
        return item;
    }

    void numericRange(BaseContainer& item, const Parm& p, Bool isInt)
    {
        if (isInt)
        {
            if (p.hasMin) item.SetInt32(DESC_MIN, Int32(p.min));
            if (p.hasMax) item.SetInt32(DESC_MAX, Int32(p.max));
            if (p.hasUIMin) item.SetInt32(DESC_MINSLIDER, Int32(p.uiMin));
            if (p.hasUIMax) item.SetInt32(DESC_MAXSLIDER, Int32(p.uiMax));
            if (p.hasUIMin && p.hasUIMax && p.size == 1) item.SetInt32(DESC_CUSTOMGUI, CUSTOMGUI_LONGSLIDER);
        }
        else
        {
            if (p.hasMin) item.SetFloat(DESC_MIN, p.min);
            if (p.hasMax) item.SetFloat(DESC_MAX, p.max);
            if (p.hasUIMin) item.SetFloat(DESC_MINSLIDER, p.uiMin);
            if (p.hasUIMax) item.SetFloat(DESC_MAXSLIDER, p.uiMax);
            item.SetFloat(DESC_STEP, 0.01);
            item.SetInt32(DESC_UNIT, DESC_UNIT_FLOAT);
            if (p.hasUIMin && p.hasUIMax && p.size == 1) item.SetInt32(DESC_CUSTOMGUI, CUSTOMGUI_REALSLIDER);
        }
    }

    void addParm(const ParmSet& ps, int idx, const DescID& parent)
    {
        const Parm& p = ps.parms[size_t(idx)];
        if (p.invisible) return;

        if (p.isContainer())
        {
            // Folder list: each child folder becomes a group of its own.
            for (int c : p.children) addParm(ps, c, parent);
            return;
        }
        if (p.isFolder())
        {
            const DescID gid = DescLevel(idFor(p.name, 0), DTYPE_GROUP, 0);
            BaseContainer g = GetCustomDataTypeDefault(DTYPE_GROUP);
            g.SetString(DESC_NAME, labelOf(p));
            g.SetInt32(DESC_COLUMNS, 1);
            g.SetBool(DESC_DEFAULT, true);
            add(gid, g, parent);
            for (int c : p.children) addParm(ps, c, gid);
            return;
        }
        if (p.isMultiparm())
        {
            if (p.isRamp())
            {
                BaseContainer t = base(DTYPE_STATICTEXT, p);
                t.SetString(DESC_NAME, labelOf(p) + " (ramp: edit in Houdini, preserved via preset)"_s);
                add(DescLevel(idFor(p.name, 0), DTYPE_STATICTEXT, 0), t, parent);
                return;
            }
            BaseContainer count = base(DTYPE_LONG, p, "(count)");
            count.SetInt32(DESC_MIN, 0);
            add(DescLevel(idFor(p.name, 0), DTYPE_LONG, 0), count, parent);
            for (int c : p.children) addParm(ps, c, parent);
            return;
        }
        if (p.isLabel())
        {
            BaseContainer t = base(DTYPE_STATICTEXT, p);
            add(DescLevel(idFor(p.name, 0), DTYPE_STATICTEXT, 0), t, parent);
            return;
        }
        if (p.isSeparator())
        {
            BaseContainer sep = GetCustomDataTypeDefault(DTYPE_SEPARATOR);
            sep.SetBool(DESC_SEPARATORLINE, true);
            add(DescLevel(idFor(p.name, 0), DTYPE_SEPARATOR, 0), sep, parent);
            return;
        }
        if (p.isButton())
        {
            BaseContainer b = base(DTYPE_BUTTON, p);
            b.SetInt32(DESC_CUSTOMGUI, CUSTOMGUI_BUTTON);
            add(DescLevel(idFor(p.name, 0), DTYPE_BUTTON, 0), b, parent);
            return;
        }
        if (p.isToggle())
        {
            BaseContainer b = base(DTYPE_BOOL, p);
            add(DescLevel(idFor(p.name, 0), DTYPE_BOOL, 0), b, parent);
            return;
        }
        if (p.hasMenu() && (p.isInt() || p.isString()))
        {
            BaseContainer item = base(DTYPE_LONG, p);
            BaseContainer cycle;
            for (size_t i = 0; i < p.choices.size(); ++i) cycle.SetString(Int32(i), S(p.choices[i].label));
            item.SetContainer(DESC_CYCLE, cycle);
            item.SetInt32(DESC_CUSTOMGUI, CUSTOMGUI_CYCLE);
            add(DescLevel(idFor(p.name, 0), DTYPE_LONG, 0), item, parent);
            return;
        }
        if (p.isInt())
        {
            if (p.size == 1)
            {
                BaseContainer item = base(DTYPE_LONG, p);
                numericRange(item, p, true);
                add(DescLevel(idFor(p.name, 0), DTYPE_LONG, 0), item, parent);
            }
            else
            {
                static const char* sfx[] = {".X", ".Y", ".Z", ".W"};
                for (int k = 0; k < p.size; ++k)
                {
                    BaseContainer item = base(DTYPE_LONG, p, k < 4 ? sfx[k] : "");
                    numericRange(item, p, true);
                    add(DescLevel(idFor(p.name, k), DTYPE_LONG, 0), item, parent);
                }
            }
            return;
        }
        if (p.isColor() && p.size >= 3)
        {
            BaseContainer item = base(DTYPE_COLOR, p);
            add(DescLevel(idFor(p.name, 0), DTYPE_COLOR, 0), item, parent);
            if (p.size >= 4)
            {
                BaseContainer a = base(DTYPE_REAL, p, "Alpha");
                a.SetFloat(DESC_MIN, 0); a.SetFloat(DESC_MAX, 1); a.SetFloat(DESC_STEP, 0.01);
                a.SetInt32(DESC_CUSTOMGUI, CUSTOMGUI_REALSLIDER);
                add(DescLevel(idFor(p.name, 3), DTYPE_REAL, 0), a, parent);
            }
            return;
        }
        if (p.isFloat())
        {
            if (p.size == 1)
            {
                BaseContainer item = base(DTYPE_REAL, p);
                numericRange(item, p, false);
                add(DescLevel(idFor(p.name, 0), DTYPE_REAL, 0), item, parent);
            }
            else if (p.size == 3)
            {
                BaseContainer item = base(DTYPE_VECTOR, p);
                item.SetInt32(DESC_UNIT, DESC_UNIT_FLOAT);
                item.SetFloat(DESC_STEP, 0.01);
                add(DescLevel(idFor(p.name, 0), DTYPE_VECTOR, 0), item, parent);
            }
            else
            {
                static const char* sfx[] = {".X", ".Y", ".Z", ".W"};
                for (int k = 0; k < p.size; ++k)
                {
                    BaseContainer item = base(DTYPE_REAL, p, k < 4 ? sfx[k] : "");
                    numericRange(item, p, false);
                    add(DescLevel(idFor(p.name, k), DTYPE_REAL, 0), item, parent);
                }
            }
            return;
        }
        if (p.isNode())
        {
            // Operator path parm: link any object; it is marshalled as geometry input.
            BaseContainer item = base(DTYPE_BASELISTLINK, p);
            BaseContainer accept;
            accept.SetInt32(Obase, 1);
            item.SetContainer(DESC_ACCEPT, accept);
            add(DescLevel(idFor(p.name, 0), DTYPE_BASELISTLINK, 0), item, parent);
            return;
        }
        if (p.isPath())
        {
            BaseContainer item = base(DTYPE_FILENAME, p);
            if (p.type == HAPI_PARMTYPE_PATH_FILE_DIR) item.SetBool(DESC_FILENAME_DIRECTORY, true);
            add(DescLevel(idFor(p.name, 0), DTYPE_FILENAME, 0), item, parent);
            return;
        }
        if (p.isString())
        {
            for (int k = 0; k < p.size; ++k)
            {
                BaseContainer item = base(DTYPE_STRING, p);
                add(DescLevel(idFor(p.name, k), DTYPE_STRING, 0), item, parent);
            }
            return;
        }
    }
};

} // namespace

Bool HoudiniAssetObject::GetDDescription(const GeListNode* node, Description* description, DESCFLAGS_DESC& flags)
{
    if (!description->LoadDescription(node->GetType())) return false;
    // The description is built from object state; loading the asset lazily
    // here means a reopened scene shows its parameters as soon as the object
    // is selected. The const_cast is the usual price of dynamic descriptions.
    BaseObject* op = const_cast<BaseObject*>(static_cast<const BaseObject*>(node));
    BaseContainer* bc = op->GetDataInstance();

    std::lock_guard<std::recursive_mutex> g(m_mutex);
    ensureAsset(op);

    // Asset chooser cycle.
    if (BaseContainer* item = description->GetParameterI(DescLevel(HDA_ASSET_INDEX), nullptr))
    {
        BaseContainer cycle;
        if (m_library.assetNames.empty()) cycle.SetString(0, "(load an HDA)"_s);
        for (size_t i = 0; i < m_library.assetNames.size(); ++i) cycle.SetString(Int32(i), S(m_library.assetNames[i]));
        item->SetContainer(DESC_CYCLE, cycle);
    }

    if (m_node >= 0)
    {
        DescBuilder b;
        b.description = description;
        b.singleid = description->GetSingleDescID();
        b.self = this;
        b.bc = bc;
        b.idFor = [this, bc](const std::string& n, Int32 c) { return idForParm(bc, n, c); };
        const DescID parent = DescLevel(HDA_GROUP_PARMS);
        for (int r : m_parms.roots) b.addParm(m_parms, r, parent);
    }

    flags |= DESCFLAGS_DESC::LOADED;
    return SUPER::GetDDescription(node, description, flags);
}

Bool HoudiniAssetObject::GetDEnabling(const GeListNode* node, const DescID& id, const GeData& t_data, DESCFLAGS_ENABLE flags, const BaseContainer* itemdesc) const
{
    const Int32 i = id[0].id;
    if (i == HDA_COOK || i == HDA_RELOAD) return m_node >= 0;
    if (i >= HDA_DYNAMIC_PARM_BASE)
    {
        const BaseObject* op = static_cast<const BaseObject*>(node);
        std::string name = parmForId(op->GetDataInstance(), i);
        const Parm* p = m_parms.find(name);
        if (p) return !p->disabled;
    }
    return SUPER::GetDEnabling(node, id, t_data, flags, itemdesc);
}

Bool HoudiniAssetObject::GetDParameter(const GeListNode* node, const DescID& id, GeData& t_data, DESCFLAGS_GET& flags) const
{
    switch (id[0].id)
    {
        case HDA_STATUS:
            t_data = GeData(m_status);
            flags |= DESCFLAGS_GET::PARAM_GET;
            return true;
        case HDA_COOK_LOG:
            t_data = GeData(m_cookLog);
            flags |= DESCFLAGS_GET::PARAM_GET;
            return true;
        case HDA_ASSET_LABEL:
        {
            maxon::String s;
            if (m_node >= 0)
            {
                s = S(m_assetInfo.fullOpName);
                s += m_assetInfo.nodeType == HAPI_NODETYPE_SOP ? " (SOP)"_s : " (OBJ)"_s;
                if (m_assetInfo.isTimeDependent) s += ", time dependent"_s;
            }
            else
                s = "-"_s;
            t_data = GeData(s);
            flags |= DESCFLAGS_GET::PARAM_GET;
            return true;
        }
        case HDA_INPUT_INFO:
        {
            maxon::String s;
            if (m_assetInfo.inputs.empty()) s = "This asset has no geometry inputs"_s;
            for (const AssetInput& in : m_assetInfo.inputs)
            {
                if (!s.IsEmpty()) s += ", "_s;
                s += maxon::String::IntToString(in.index + 1) + ": "_s + S(in.name);
            }
            t_data = GeData(s);
            flags |= DESCFLAGS_GET::PARAM_GET;
            return true;
        }
        default:
            break;
    }
    return SUPER::GetDParameter(node, id, t_data, flags);
}

// ---------------------------------------------------------------------------
// Messages: buttons and parameter edits (main thread)
// ---------------------------------------------------------------------------

Bool HoudiniAssetObject::Message(GeListNode* node, Int32 type, void* data)
{
    BaseObject* op = static_cast<BaseObject*>(node);
    switch (type)
    {
        case MSG_DESCRIPTION_COMMAND:
        {
            DescriptionCommand* dc = static_cast<DescriptionCommand*>(data);
            if (!dc) break;
            const Int32 id = dc->_descId[0].id;
            std::lock_guard<std::recursive_mutex> g(m_mutex);
            if (id == HDA_RELOAD)
            {
                ensureAsset(op, true);
                op->SetDirty(DIRTYFLAGS::DESCRIPTION | DIRTYFLAGS::DATA);
                EventAdd();
                return true;
            }
            if (id == HDA_COOK)
            {
                m_needsCook = true;
                op->SetDirty(DIRTYFLAGS::DATA);
                EventAdd();
                return true;
            }
            if (id == HDA_SHOW_LOG)
            {
                ApplicationOutput("[Houdini Engine] @: @\n@", op->GetName(), m_status, m_cookLog);
                return true;
            }
            if (id >= HDA_DYNAMIC_PARM_BASE && m_node >= 0)
            {
                const Parm* p = m_parms.find(parmForId(op->GetDataInstance(), id));
                if (p && p->isButton())
                {
                    pressButton(m_node, *p);
                    // Button callbacks often change other parms or menus.
                    refreshParms(op);
                    seedContainerFromParms(op);
                    m_needsCook = true;
                    op->SetDirty(DIRTYFLAGS::DESCRIPTION | DIRTYFLAGS::DATA);
                    EventAdd();
                    return true;
                }
            }
            break;
        }
        case MSG_DESCRIPTION_POSTSETPARAMETER:
        {
            DescriptionPostSetValue* psv = static_cast<DescriptionPostSetValue*>(data);
            if (!psv || !psv->descid) break;
            const Int32 id = (*psv->descid)[0].id;
            std::lock_guard<std::recursive_mutex> g(m_mutex);
            if (id == HDA_FILE || id == HDA_ASSET_INDEX)
            {
                ensureAsset(op, true);
                op->SetDirty(DIRTYFLAGS::DESCRIPTION | DIRTYFLAGS::DATA);
                EventAdd();
                return true;
            }
            if (id >= HDA_DYNAMIC_PARM_BASE && m_node >= 0)
            {
                // Push right away so visibility/disable conditions and menu
                // scripts re-evaluate, then refresh the description if the
                // interface changed. Cooking waits for GetVirtualObjects.
                std::vector<char> before;
                for (const Parm& p : m_parms.parms) { before.push_back(p.invisible); before.push_back(p.disabled); before.push_back(char(p.choices.size() & 0x7f)); }
                pushParameterChanges(op);
                refreshParms(op);
                std::vector<char> after;
                for (const Parm& p : m_parms.parms) { after.push_back(p.invisible); after.push_back(p.disabled); after.push_back(char(p.choices.size() & 0x7f)); }
                if (before != after) op->SetDirty(DIRTYFLAGS::DESCRIPTION);
                m_needsCook = true;
            }
            break;
        }
        default:
            break;
    }
    return SUPER::Message(node, type, data);
}

// ---------------------------------------------------------------------------
// Pushing values and inputs into Houdini
// ---------------------------------------------------------------------------

Bool HoudiniAssetObject::pushParameterChanges(BaseObject* op)
{
    if (m_node < 0) return false;
    BaseContainer* bc = op->GetDataInstance();
    Bool changed = false;

    for (Parm& p : m_parms.parms)
    {
        if (!p.holdsValue() || p.invisible) continue;
        if (p.isToggle())
        {
            const Int32 id = idForParm(bc, p.name);
            if (bc->GetType(id) == DA_NIL) continue;
            int v = bc->GetBool(id) ? 1 : 0;
            if (p.intValues.empty() || p.intValues[0] != v) { if (setIntValues(m_node, p, {v})) { p.intValues = {v}; changed = true; } }
        }
        else if (p.isInt() || (p.isString() && p.hasMenu()))
        {
            if (p.hasMenu() || p.size == 1)
            {
                const Int32 id = idForParm(bc, p.name);
                if (bc->GetType(id) == DA_NIL) continue;
                int v = bc->GetInt32(id);
                if (p.isString())
                {
                    if (v < 0 || v >= int(p.choices.size())) continue;
                    const std::string& token = p.choices[size_t(v)].value;
                    if (p.stringValues.empty() || p.stringValues[0] != token)
                        if (setStringValue(m_node, p, 0, token)) { p.stringValues = {token}; changed = true; }
                }
                else if (p.intValues.empty() || p.intValues[0] != v)
                {
                    if (setIntValues(m_node, p, {v})) { p.intValues = {v}; changed = true; }
                }
            }
            else
            {
                std::vector<int> v(size_t(p.size));
                Bool any = false;
                for (int k = 0; k < p.size; ++k)
                {
                    const Int32 id = idForParm(bc, p.name, k);
                    if (bc->GetType(id) == DA_NIL) { v[size_t(k)] = k < int(p.intValues.size()) ? p.intValues[size_t(k)] : 0; continue; }
                    v[size_t(k)] = bc->GetInt32(id);
                    any = true;
                }
                if (any && v != p.intValues) if (setIntValues(m_node, p, v)) { p.intValues = v; changed = true; }
            }
        }
        else if (p.isFloat())
        {
            std::vector<float> v(size_t(p.size));
            Bool have = false;
            if ((p.isColor() && p.size >= 3) || p.size == 3)
            {
                const Int32 id = idForParm(bc, p.name);
                if (bc->GetType(id) != DA_NIL)
                {
                    Vector c = bc->GetVector(id);
                    v[0] = float(c.x); v[1] = float(c.y); v[2] = float(c.z);
                    have = true;
                }
                if (p.size >= 4)
                {
                    const Int32 aid = idForParm(bc, p.name, 3);
                    v[3] = bc->GetType(aid) != DA_NIL ? float(bc->GetFloat(aid)) : (p.floatValues.size() > 3 ? p.floatValues[3] : 1.0f);
                }
            }
            else if (p.size == 1)
            {
                const Int32 id = idForParm(bc, p.name);
                if (bc->GetType(id) != DA_NIL) { v[0] = float(bc->GetFloat(id)); have = true; }
            }
            else
            {
                for (int k = 0; k < p.size; ++k)
                {
                    const Int32 id = idForParm(bc, p.name, k);
                    if (bc->GetType(id) == DA_NIL) { v[size_t(k)] = k < int(p.floatValues.size()) ? p.floatValues[size_t(k)] : 0.0f; continue; }
                    v[size_t(k)] = float(bc->GetFloat(id));
                    have = true;
                }
            }
            if (have && v != p.floatValues) if (setFloatValues(m_node, p, v)) { p.floatValues = v; changed = true; }
        }
        else if (p.isString())
        {
            if (p.isNode()) continue;   // handled by pushInputs
            if (p.isPath())
            {
                const Int32 id = idForParm(bc, p.name);
                if (bc->GetType(id) == DA_NIL) continue;
                std::string v = ToStd(bc->GetFilename(id).GetString());
                if (p.stringValues.empty() || p.stringValues[0] != v)
                    if (setStringValue(m_node, p, 0, v)) { p.stringValues = {v}; changed = true; }
            }
            else
            {
                for (int k = 0; k < p.size; ++k)
                {
                    const Int32 id = idForParm(bc, p.name, k);
                    if (bc->GetType(id) == DA_NIL) continue;
                    std::string v = ToStd(bc->GetString(id));
                    if (k >= int(p.stringValues.size())) p.stringValues.resize(size_t(k) + 1);
                    if (p.stringValues[size_t(k)] != v)
                        if (setStringValue(m_node, p, k, v)) { p.stringValues[size_t(k)] = v; changed = true; }
                }
            }
        }
    }
    return changed;
}

Bool HoudiniAssetObject::pushInputs(BaseObject* op, const HierarchyHelp* hh, Bool& dirty)
{
    if (m_node < 0) return false;
    BaseContainer* bc = op->GetDataInstance();
    BaseDocument* doc = hh->GetDocument();
    const Int32 slotCount = Int32(m_assetInfo.inputs.size());

    struct Source { BaseObject* clone = nullptr; Bool fromLink = false; };
    std::vector<Source> sources(size_t(slotCount));

    // Operator-path parms (DTYPE_BASELISTLINK) also count as inputs.
    struct NodeParmSource { std::string parmName; BaseObject* clone = nullptr; };
    std::vector<NodeParmSource> nodeParms;

    op->NewDependenceList();

    // Children in order fill the slots first (OBJECT_INPUT semantics).
    Int32 slot = 0;
    for (BaseObject* child = op->GetDown(); child && slot < slotCount; child = child->GetNext(), ++slot)
    {
        Bool childDirty = false;
        BaseObject* clone = op->GetAndCheckHierarchyClone(hh, child, HIERARCHYCLONEFLAGS::ASPOLY, &childDirty, nullptr, false);
        if (childDirty) dirty = true;
        sources[size_t(slot)].clone = clone;
    }
    // Link fields override.
    const Int32 linkIds[] = {HDA_INPUT_0, HDA_INPUT_1, HDA_INPUT_2, HDA_INPUT_3};
    for (Int32 i = 0; i < slotCount && i < 4; ++i)
    {
        BaseObject* linked = static_cast<BaseObject*>(bc->GetLink(linkIds[i], doc, Obase));
        if (!linked) continue;
        op->AddDependence(hh, linked);
        Bool linkDirty = false;
        BaseObject* clone = op->GetHierarchyClone(hh, linked, HIERARCHYCLONEFLAGS::ASPOLY, &linkDirty, nullptr);
        if (linkDirty) dirty = true;
        if (sources[size_t(i)].clone) BaseObject::Free(sources[size_t(i)].clone);
        sources[size_t(i)].clone = clone;
        sources[size_t(i)].fromLink = true;
    }
    for (const Parm& p : m_parms.parms)
    {
        if (!p.isNode() || p.invisible) continue;
        BaseObject* linked = static_cast<BaseObject*>(bc->GetLink(idForParm(bc, p.name), doc, Obase));
        if (!linked) continue;
        op->AddDependence(hh, linked);
        Bool linkDirty = false;
        BaseObject* clone = op->GetHierarchyClone(hh, linked, HIERARCHYCLONEFLAGS::ASPOLY, &linkDirty, nullptr);
        if (linkDirty) dirty = true;
        nodeParms.push_back({p.name, clone});
    }
    if (!op->CompareDependenceList()) dirty = true;
    op->TouchDependenceList();

    Bool anySource = !nodeParms.empty();
    for (const Source& s : sources) if (s.clone) anySource = true;
    const Bool rebuild = dirty || (anySource && m_inputNodes.empty()) || (!anySource && !m_inputNodes.empty());

    if (rebuild)
    {
        releaseInputs();
        geo::ConvertOptions conv;
        conv.scale = bc->GetFloat(HDA_SCALE, 100.0);
        conv.flipZ = bc->GetBool(HDA_FLIP_Z, true);
        conv.reverseWinding = bc->GetBool(HDA_REVERSE_WINDING, false);
        const Matrix rel = ~op->GetMg();

        auto upload = [&](BaseObject* clone, HAPI_NodeId& outNode) -> Bool {
            outNode = -1;
            if (!clone) return false;
            maxon::BaseArray<InputMesh> meshes;
            maxon::BaseArray<InputCurve> curves;
            CollectInputGeometry(clone, rel, conv, meshes, curves);
            std::vector<HAPI_NodeId> nodes;
            for (const InputMesh& m : meshes)
            {
                HAPI_NodeId n = -1;
                if (createInputMesh(m, n)) { nodes.push_back(n); m_inputNodes.push_back(n); }
            }
            for (const InputCurve& c : curves)
            {
                HAPI_NodeId n = -1;
                if (createInputCurve(c, n)) { nodes.push_back(n); m_inputNodes.push_back(n); }
            }
            if (nodes.empty()) return false;
            if (nodes.size() == 1) { outNode = nodes[0]; return true; }
            if (createMergeNode(nodes, outNode)) { m_inputMerges.push_back(outNode); return true; }
            outNode = nodes[0];
            return true;
        };

        for (Int32 i = 0; i < slotCount; ++i)
        {
            HAPI_NodeId n = -1;
            if (upload(sources[size_t(i)].clone, n)) connectInput(m_node, i, n);
            else disconnectInput(m_node, i);
        }
        for (const NodeParmSource& np : nodeParms)
        {
            HAPI_NodeId n = -1;
            if (upload(np.clone, n)) setNodeParm(m_node, np.parmName, n);
        }
        m_needsCook = true;
    }

    for (Source& s : sources) if (s.clone) BaseObject::Free(s.clone);
    for (NodeParmSource& np : nodeParms) if (np.clone) BaseObject::Free(np.clone);
    return true;
}

// ---------------------------------------------------------------------------
// Cook + convert
// ---------------------------------------------------------------------------

BaseObject* HoudiniAssetObject::GetVirtualObjects(BaseObject* op, const HierarchyHelp* hh)
{
    std::lock_guard<std::recursive_mutex> g(m_mutex);
    BaseDocument* doc = hh->GetDocument();
    BaseContainer* bc = op->GetDataInstance();

    Bool dirty = op->CheckCache(hh) || op->IsDirty(DIRTYFLAGS::DATA);

    if (!ensureAsset(op))
    {
        // Keep children touched so they do not render on their own.
        for (BaseObject* child = op->GetDown(); child; child = child->GetNext()) child->Touch();
        BaseObject* n = BaseObject::Alloc(Onull);
        if (n) n->SetName(op->GetName() + " ("_s + m_status + ")"_s);
        return n;
    }

    Bool inputsDirty = false;
    pushInputs(op, hh, inputsDirty);

    const Float t = doc->GetTime().Get();
    const Bool timeDirty = bc->GetBool(HDA_COOK_ON_FRAME, true) && m_assetInfo.isTimeDependent && t != m_cookedTime;
    const Bool parmsChanged = pushParameterChanges(op);

    if (!dirty && !inputsDirty && !timeDirty && !parmsChanged && !m_needsCook)
    {
        BaseObject* cache = op->GetCache();
        if (cache) return cache;
    }
    if (!bc->GetBool(HDA_AUTO_COOK, true) && !m_needsCook && !timeDirty)
    {
        BaseObject* cache = op->GetCache();
        if (cache) return cache;
    }

    Session& s = Session::get();
    s.setTimeline(Float64(doc->GetFps()), doc->GetMinTime().Get(), doc->GetMaxTime().Get());
    s.setTime(t);

    BaseThread* bt = hh->GetThread();
    CookResult cr = s.cook(m_node, [bt]() { return bt && bt->TestBreak(); });
    m_cookedTime = t;
    m_needsCook = false;
    if (cr.aborted) return nullptr;   // C4D will ask again

    m_cookLog = ToC4D(cr.message);
    if (!cr.succeeded)
    {
        setStatus("Cook failed"_s);
        ApplicationOutput("[Houdini Engine] @ cook failed:\n@", op->GetName(), m_cookLog);
        BaseObject* n = BaseObject::Alloc(Onull);
        if (n) n->SetName(op->GetName() + " (cook failed)"_s);
        return n;
    }

    // Menus/visibility may have changed; keep the container as the source
    // of truth for values so we do not ping-pong with HDA callbacks.
    {
        ParmSet fresh;
        if (fetchParms(m_node, fresh))
        {
            for (Parm& p : fresh.parms)
            {
                const Parm* old = m_parms.find(p.name);
                if (old && old->holdsValue()) { p.intValues = old->intValues; p.floatValues = old->floatValues; p.stringValues = old->stringValues; }
            }
            m_parms = std::move(fresh);
        }
    }

    OutputOptions oo;
    oo.readNormals = bc->GetBool(HDA_IMPORT_NORMALS, true);
    oo.readUVs = bc->GetBool(HDA_IMPORT_UVS, true);
    oo.readColors = bc->GetBool(HDA_IMPORT_COLORS, true);
    oo.readGroups = bc->GetBool(HDA_IMPORT_GROUPS, true);
    oo.readMaterials = bc->GetBool(HDA_IMPORT_MATERIALS, true);
    AssetOutput out;
    if (!fetchAssetOutput(m_node, oo, out))
    {
        setStatus("Output fetch failed"_s);
        m_cookLog = ToC4D(s.lastError());
        return BaseObject::Alloc(Onull);
    }

    BuildOptions bo;
    bo.conv.scale = bc->GetFloat(HDA_SCALE, 100.0);
    bo.conv.flipZ = bc->GetBool(HDA_FLIP_Z, true);
    bo.conv.reverseWinding = bc->GetBool(HDA_REVERSE_WINDING, false);
    bo.normals = oo.readNormals;
    bo.uvs = oo.readUVs;
    bo.colors = oo.readColors;
    bo.groups = oo.readGroups;
    bo.materials = oo.readMaterials;
    bo.instanceMode = bc->GetInt32(HDA_INSTANCE_MODE, HDA_INSTANCE_MODE_MULTI);
    bo.phongAngle = RadToDeg(bc->GetFloat(HDA_PHONG_ANGLE, DegToRad(40.0)));
    bo.doc = doc;

    BaseObject* root = BuildOutputHierarchy(out, bo, op->GetName());

    maxon::String st = "Cooked: "_s + maxon::String::IntToString(Int32(out.totalMeshes())) + " mesh(es)"_s;
    if (out.totalInstancers()) st += ", "_s + maxon::String::IntToString(Int32(out.totalInstancers())) + " instancer(s)"_s;
    if (out.totalCurves()) st += ", "_s + maxon::String::IntToString(Int32(out.totalCurves())) + " curve(s)"_s;
    if (cr.hadCookErrors) st += " (with warnings, see log)"_s;
    setStatus(st);

    // Snapshot for Write(): the full parm state, including ramps/multiparms.
    getPreset(m_node, m_preset);
    return root;
}

Bool RegisterHoudiniAssetObject()
{
    return RegisterObjectPlugin(ID_HOUDINI_ASSET_OBJECT, GeLoadString(IDS_HOUDINI_ASSET),
                                OBJECT_GENERATOR | OBJECT_INPUT, HoudiniAssetObject::Alloc,
                                "Ohoudiniasset"_s, nullptr, 0);
}

} // namespace c4dhoudini
