#include "HapiParms.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace hapi {

int ParmSet::indexOf(HAPI_ParmId id) const
{
    for (size_t i = 0; i < parms.size(); ++i)
        if (parms[i].id == id) return int(i);
    return -1;
}

const Parm* ParmSet::find(const std::string& name) const
{
    auto it = byName.find(name);
    return it == byName.end() ? nullptr : &parms[size_t(it->second)];
}

Parm* ParmSet::find(const std::string& name)
{
    auto it = byName.find(name);
    return it == byName.end() ? nullptr : &parms[size_t(it->second)];
}

const char* parmTypeName(HAPI_ParmType t)
{
    switch (t)
    {
        case HAPI_PARMTYPE_INT: return "int";
        case HAPI_PARMTYPE_MULTIPARMLIST: return "multiparm";
        case HAPI_PARMTYPE_TOGGLE: return "toggle";
        case HAPI_PARMTYPE_BUTTON: return "button";
        case HAPI_PARMTYPE_FLOAT: return "float";
        case HAPI_PARMTYPE_COLOR: return "color";
        case HAPI_PARMTYPE_STRING: return "string";
        case HAPI_PARMTYPE_PATH_FILE: return "file";
        case HAPI_PARMTYPE_PATH_FILE_GEO: return "file_geo";
        case HAPI_PARMTYPE_PATH_FILE_IMAGE: return "file_image";
        case HAPI_PARMTYPE_PATH_FILE_DIR: return "directory";
        case HAPI_PARMTYPE_NODE: return "node";
        case HAPI_PARMTYPE_FOLDERLIST: return "folderlist";
        case HAPI_PARMTYPE_FOLDERLIST_RADIO: return "folderlist_radio";
        case HAPI_PARMTYPE_FOLDER: return "folder";
        case HAPI_PARMTYPE_LABEL: return "label";
        case HAPI_PARMTYPE_SEPARATOR: return "separator";
        default: return "unknown";
    }
}

bool fetchParms(HAPI_NodeId node, ParmSet& out)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) return false;

    out = ParmSet();
    out.nodeId = node;

    HAPI_NodeInfo ni = Lib::NodeInfo_Create ? Lib::NodeInfo_Create() : HAPI_NodeInfo{};
    if (!s.check(Lib::GetNodeInfo(h, node, &ni), "HAPI_GetNodeInfo")) return false;
    if (ni.parmCount <= 0) return true;

    std::vector<HAPI_ParmInfo> infos(size_t(ni.parmCount));
    if (!s.check(Lib::GetParameters(h, node, infos.data(), 0, ni.parmCount), "HAPI_GetParameters")) return false;

    std::vector<int> ints(size_t(ni.parmIntValueCount));
    std::vector<float> floats(size_t(ni.parmFloatValueCount));
    std::vector<HAPI_StringHandle> strings(size_t(ni.parmStringValueCount));
    std::vector<HAPI_ParmChoiceInfo> choices(size_t(ni.parmChoiceCount));
    if (ni.parmIntValueCount > 0 &&
        !s.check(Lib::GetParmIntValues(h, node, ints.data(), 0, ni.parmIntValueCount), "HAPI_GetParmIntValues")) return false;
    if (ni.parmFloatValueCount > 0 &&
        !s.check(Lib::GetParmFloatValues(h, node, floats.data(), 0, ni.parmFloatValueCount), "HAPI_GetParmFloatValues")) return false;
    if (ni.parmStringValueCount > 0 &&
        !s.check(Lib::GetParmStringValues(h, node, true, strings.data(), 0, ni.parmStringValueCount), "HAPI_GetParmStringValues")) return false;
    if (ni.parmChoiceCount > 0 &&
        !s.check(Lib::GetParmChoiceLists(h, node, choices.data(), 0, ni.parmChoiceCount), "HAPI_GetParmChoiceLists")) return false;

    // Gather every string handle we need and resolve them in one batch.
    std::vector<HAPI_StringHandle> handles;
    handles.reserve(infos.size() * 4 + strings.size() + choices.size() * 2);
    for (const HAPI_ParmInfo& pi : infos)
    {
        handles.push_back(pi.nameSH);
        handles.push_back(pi.labelSH);
        handles.push_back(pi.helpSH);
        handles.push_back(pi.typeInfoSH);
    }
    for (HAPI_StringHandle sh : strings) handles.push_back(sh);
    for (const HAPI_ParmChoiceInfo& c : choices) { handles.push_back(c.labelSH); handles.push_back(c.valueSH); }
    std::vector<std::string> resolved = s.getStrings(handles);

    size_t cursor = 0;
    out.parms.resize(infos.size());
    for (size_t i = 0; i < infos.size(); ++i)
    {
        const HAPI_ParmInfo& pi = infos[i];
        Parm& p = out.parms[i];
        p.id = pi.id;
        p.parentId = pi.parentId;
        p.childIndex = pi.childIndex;
        p.type = pi.type;
        p.scriptType = pi.scriptType;
        p.name = resolved[cursor++];
        p.label = resolved[cursor++];
        p.help = resolved[cursor++];
        p.typeInfo = resolved[cursor++];
        p.size = pi.size;
        p.invisible = pi.invisible;
        p.disabled = pi.disabled;
        p.spare = pi.spare;
        p.joinNext = pi.joinNext;
        p.labelNone = pi.labelNone;
        p.hasMin = pi.hasMin; p.hasMax = pi.hasMax; p.hasUIMin = pi.hasUIMin; p.hasUIMax = pi.hasUIMax;
        p.min = pi.min; p.max = pi.max; p.uiMin = pi.UIMin; p.uiMax = pi.UIMax;
        p.choiceListType = pi.choiceListType;
        p.useMenuItemTokenAsValue = pi.useMenuItemTokenAsValue;
        p.rampType = pi.rampType;
        p.inputNodeType = pi.inputNodeType;
        p.inputNodeFlag = pi.inputNodeFlag;
        p.isChildOfMultiparm = pi.isChildOfMultiParm;
        p.instanceNum = pi.instanceNum;
        p.instanceLength = pi.instanceLength;
        p.instanceCount = pi.instanceCount;
        p.instanceStartOffset = pi.instanceStartOffset;
        p.intValuesIndex = pi.intValuesIndex;
        p.floatValuesIndex = pi.floatValuesIndex;
        p.stringValuesIndex = pi.stringValuesIndex;
        p.choiceIndex = pi.choiceIndex;

        if (p.isInt() && pi.intValuesIndex >= 0)
            for (int k = 0; k < pi.size && size_t(pi.intValuesIndex + k) < ints.size(); ++k)
                p.intValues.push_back(ints[size_t(pi.intValuesIndex + k)]);
        if (p.isFloat() && pi.floatValuesIndex >= 0)
            for (int k = 0; k < pi.size && size_t(pi.floatValuesIndex + k) < floats.size(); ++k)
                p.floatValues.push_back(floats[size_t(pi.floatValuesIndex + k)]);
        // Multiparm lists store their instance count as an int value too.
        if (p.isMultiparm() && pi.intValuesIndex >= 0 && size_t(pi.intValuesIndex) < ints.size())
            p.intValues.assign(1, ints[size_t(pi.intValuesIndex)]);
        if (p.isString() && pi.stringValuesIndex >= 0)
            for (int k = 0; k < pi.size && size_t(pi.stringValuesIndex + k) < strings.size(); ++k)
                p.stringValues.push_back(resolved[infos.size() * 4 + size_t(pi.stringValuesIndex + k)]);

        if (pi.choiceCount > 0 && pi.choiceIndex >= 0)
        {
            size_t base = infos.size() * 4 + strings.size();
            for (int k = 0; k < pi.choiceCount && size_t(pi.choiceIndex + k) < choices.size(); ++k)
            {
                size_t ci = size_t(pi.choiceIndex + k);
                ParmChoice c;
                c.label = resolved[base + ci * 2];
                c.value = resolved[base + ci * 2 + 1];
                p.choices.push_back(c);
            }
        }
        out.byName[p.name] = int(i);
    }

    // Build the tree.
    for (size_t i = 0; i < out.parms.size(); ++i)
    {
        Parm& p = out.parms[i];
        if (p.parentId == HAPI_INVALID_PARM_ID) { out.roots.push_back(int(i)); continue; }
        int pi = out.indexOf(p.parentId);
        if (pi < 0) out.roots.push_back(int(i));
        else out.parms[size_t(pi)].children.push_back(int(i));
    }
    return true;
}

bool setIntValues(HAPI_NodeId node, const Parm& p, const std::vector<int>& values)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle() || p.intValuesIndex < 0 || values.empty()) return false;
    int n = int(values.size()) < p.size ? int(values.size()) : p.size;
    return s.check(Lib::SetParmIntValues(s.handle(), node, values.data(), p.intValuesIndex, n), "HAPI_SetParmIntValues");
}

bool setFloatValues(HAPI_NodeId node, const Parm& p, const std::vector<float>& values)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle() || p.floatValuesIndex < 0 || values.empty()) return false;
    int n = int(values.size()) < p.size ? int(values.size()) : p.size;
    return s.check(Lib::SetParmFloatValues(s.handle(), node, values.data(), p.floatValuesIndex, n), "HAPI_SetParmFloatValues");
}

bool setStringValue(HAPI_NodeId node, const Parm& p, int index, const std::string& value)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle()) return false;
    return s.check(Lib::SetParmStringValue(s.handle(), node, value.c_str(), p.id, index), "HAPI_SetParmStringValue");
}

bool pressButton(HAPI_NodeId node, const Parm& p)
{
    // Buttons are ints; writing 1 fires the callback (Unreal/Unity do this).
    return setIntValues(node, p, std::vector<int>{1});
}

bool setNodeParm(HAPI_NodeId node, const std::string& parmName, HAPI_NodeId valueNode)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle()) return false;
    return s.check(Lib::SetParmNodeValue(s.handle(), node, parmName.c_str(), valueNode), "HAPI_SetParmNodeValue");
}

bool insertMultiparmInstance(HAPI_NodeId node, const Parm& list, int position)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle()) return false;
    return s.check(Lib::InsertMultiparmInstance(s.handle(), node, list.id, position), "HAPI_InsertMultiparmInstance");
}

bool removeMultiparmInstance(HAPI_NodeId node, const Parm& list, int position)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle()) return false;
    return s.check(Lib::RemoveMultiparmInstance(s.handle(), node, list.id, position), "HAPI_RemoveMultiparmInstance");
}

bool setParmFromString(HAPI_NodeId node, ParmSet& parms, const std::string& name, const std::string& text)
{
    Parm* p = parms.find(name);
    if (!p)
    {
        Session::get().check(HAPI_RESULT_INVALID_ARGUMENT, ("parm '" + name + "' not found").c_str());
        return false;
    }
    std::vector<std::string> parts;
    {
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, ',')) parts.push_back(item);
        if (parts.empty()) parts.push_back(text);
    }
    if (p->isButton()) return pressButton(node, *p);
    if (p->isInt() || p->isMultiparm())
    {
        std::vector<int> v;
        for (const std::string& t : parts) v.push_back(std::atoi(t.c_str()));
        if (p->isToggle() && (text == "true" || text == "on")) v = {1};
        if (p->isToggle() && (text == "false" || text == "off")) v = {0};
        // Menus accept the token text too.
        if (p->hasMenu() && v.size() == 1 && !text.empty() && !std::isdigit((unsigned char)text[0]))
            for (size_t i = 0; i < p->choices.size(); ++i)
                if (p->choices[i].value == text || p->choices[i].label == text) v = {int(i)};
        return setIntValues(node, *p, v);
    }
    if (p->isFloat())
    {
        std::vector<float> v;
        for (const std::string& t : parts) v.push_back(float(std::atof(t.c_str())));
        return setFloatValues(node, *p, v);
    }
    if (p->isString())
    {
        for (size_t i = 0; i < parts.size() && int(i) < p->size; ++i)
            if (!setStringValue(node, *p, int(i), parts[i])) return false;
        return true;
    }
    return false;
}

bool getPreset(HAPI_NodeId node, std::vector<char>& out)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle() || !Lib::GetPresetBufLength) return false;
    int len = 0;
    if (!s.check(Lib::GetPresetBufLength(s.handle(), node, HAPI_PRESETTYPE_BINARY, nullptr, &len), "HAPI_GetPresetBufLength")) return false;
    out.resize(size_t(len));
    if (len == 0) return true;
    return s.check(Lib::GetPreset(s.handle(), node, out.data(), len), "HAPI_GetPreset");
}

bool setPreset(HAPI_NodeId node, const std::vector<char>& data)
{
    Session& s = Session::get();
    SessionLock lock;
    if (!s.handle() || !Lib::SetPreset || data.empty()) return false;
    return s.check(Lib::SetPreset(s.handle(), node, HAPI_PRESETTYPE_BINARY, nullptr, data.data(), int(data.size())), "HAPI_SetPreset");
}

} // namespace hapi
