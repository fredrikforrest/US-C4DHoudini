// HapiParms: a host-neutral snapshot of an HDA's parameter interface.
//
// Cinema 4D builds its Attribute Manager UI from this (GetDDescription), and
// writes values back through setInt/setFloat/setString. Nothing here knows
// about Cinema 4D, so the same model drives the CLI probe and unit tests.
#pragma once

#include "HapiSession.h"

#include <map>
#include <string>
#include <vector>

namespace hapi {

struct ParmChoice
{
    std::string label;
    std::string value;   // token; what SetParmStringValue expects for string menus
};

struct Parm
{
    HAPI_ParmId id = HAPI_INVALID_PARM_ID;
    HAPI_ParmId parentId = HAPI_INVALID_PARM_ID;
    int childIndex = 0;
    HAPI_ParmType type = HAPI_PARMTYPE_MAX;
    HAPI_PrmScriptType scriptType = HAPI_PRM_SCRIPT_TYPE_INT;
    std::string name;        // e.g. "scale" or "folder0_1" or "pt2x" for multiparm children
    std::string label;
    std::string help;
    std::string typeInfo;    // parm "type info" tag (e.g. "unreal_ref")
    int size = 1;            // tuple size (3 for vectors, 4 for RGBA)
    bool invisible = false;
    bool disabled = false;
    bool spare = false;
    bool joinNext = false;
    bool labelNone = false;
    bool hasMin = false, hasMax = false, hasUIMin = false, hasUIMax = false;
    float min = 0, max = 10, uiMin = 0, uiMax = 10;
    HAPI_ChoiceListType choiceListType = HAPI_CHOICELISTTYPE_NONE;
    std::vector<ParmChoice> choices;
    bool useMenuItemTokenAsValue = false;
    HAPI_RampType rampType = HAPI_RAMPTYPE_INVALID;
    HAPI_NodeType inputNodeType = HAPI_NODETYPE_NONE;
    HAPI_NodeFlags inputNodeFlag = HAPI_NODEFLAGS_NONE;

    // Multiparm bookkeeping.
    bool isChildOfMultiparm = false;
    int instanceNum = -1;
    int instanceLength = 0;
    int instanceCount = 0;
    int instanceStartOffset = 0;

    // Indices into the node's flat value arrays.
    int intValuesIndex = -1, floatValuesIndex = -1, stringValuesIndex = -1, choiceIndex = -1;

    // Current values (size entries; strings are evaluated).
    std::vector<int> intValues;
    std::vector<float> floatValues;
    std::vector<std::string> stringValues;

    // Tree: indices into ParmSet::parms.
    std::vector<int> children;

    bool isInt() const      { return type >= HAPI_PARMTYPE_INT_START && type <= HAPI_PARMTYPE_INT_END; }
    bool isFloat() const    { return type >= HAPI_PARMTYPE_FLOAT_START && type <= HAPI_PARMTYPE_FLOAT_END; }
    bool isString() const   { return type >= HAPI_PARMTYPE_STRING_START && type <= HAPI_PARMTYPE_STRING_END; }
    bool isPath() const     { return type >= HAPI_PARMTYPE_PATH_START && type <= HAPI_PARMTYPE_PATH_END; }
    bool isNode() const     { return type == HAPI_PARMTYPE_NODE; }
    bool isContainer() const{ return type >= HAPI_PARMTYPE_CONTAINER_START && type <= HAPI_PARMTYPE_CONTAINER_END; }
    bool isFolder() const   { return type == HAPI_PARMTYPE_FOLDER; }
    bool isMultiparm() const{ return type == HAPI_PARMTYPE_MULTIPARMLIST; }
    bool isToggle() const   { return type == HAPI_PARMTYPE_TOGGLE; }
    bool isButton() const   { return type == HAPI_PARMTYPE_BUTTON; }
    bool isColor() const    { return type == HAPI_PARMTYPE_COLOR; }
    bool isLabel() const    { return type == HAPI_PARMTYPE_LABEL; }
    bool isSeparator() const{ return type == HAPI_PARMTYPE_SEPARATOR; }
    bool isRamp() const     { return isMultiparm() && rampType != HAPI_RAMPTYPE_INVALID; }
    bool hasMenu() const    { return choiceListType != HAPI_CHOICELISTTYPE_NONE && !choices.empty(); }
    bool holdsValue() const { return (isInt() || isFloat() || isString()) && !isButton(); }
};

struct ParmSet
{
    HAPI_NodeId nodeId = -1;
    std::vector<Parm> parms;     // in HAPI order (depth-first as Houdini lays them out)
    std::vector<int> roots;      // top-level parm indices
    std::map<std::string, int> byName;

    int indexOf(HAPI_ParmId id) const;
    const Parm* find(const std::string& name) const;
    Parm* find(const std::string& name);
};

// Reads the whole interface (infos, values, choices, strings) in a handful
// of round trips.
bool fetchParms(HAPI_NodeId node, ParmSet& out);

// Writers. They push straight to Houdini; the node is dirty afterwards and
// needs a cook. Multi-value parms (vectors/colors) pass all components.
bool setIntValues(HAPI_NodeId node, const Parm& p, const std::vector<int>& values);
bool setFloatValues(HAPI_NodeId node, const Parm& p, const std::vector<float>& values);
bool setStringValue(HAPI_NodeId node, const Parm& p, int index, const std::string& value);
bool pressButton(HAPI_NodeId node, const Parm& p);
bool setNodeParm(HAPI_NodeId node, const std::string& parmName, HAPI_NodeId valueNode);
bool insertMultiparmInstance(HAPI_NodeId node, const Parm& list, int position);
bool removeMultiparmInstance(HAPI_NodeId node, const Parm& list, int position);

// Convenience: set by name from a string ("1", "0.5", "1,2,3", "foo").
bool setParmFromString(HAPI_NodeId node, ParmSet& parms, const std::string& name, const std::string& text);

// Presets: an opaque blob holding every parm value, ideal for storing the
// HDA state inside the Cinema 4D document and restoring it on load.
bool getPreset(HAPI_NodeId node, std::vector<char>& out);
bool setPreset(HAPI_NodeId node, const std::vector<char>& data);

const char* parmTypeName(HAPI_ParmType t);

} // namespace hapi
