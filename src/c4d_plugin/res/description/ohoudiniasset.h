#ifndef OHOUDINIASSET_H__
#define OHOUDINIASSET_H__

enum
{
    HDA_GROUP_ASSET = 1000,
    HDA_FILE = 1001,              // FILENAME: path to the .hda
    HDA_ASSET_INDEX = 1002,       // LONG cycle: which operator inside the library
    HDA_RELOAD = 1003,            // BUTTON: reload library from disk (allow_overwrite)
    HDA_COOK = 1004,              // BUTTON: force cook
    HDA_AUTO_COOK = 1005,         // BOOL: cook when parameters/inputs change
    HDA_COOK_ON_FRAME = 1006,     // BOOL: push time and recook time-dependent assets
    HDA_STATUS = 1007,            // STATICTEXT: last status line (read only)
    HDA_ASSET_LABEL = 1008,       // STATICTEXT: asset label + version

    HDA_GROUP_CONVERT = 1100,
    HDA_SCALE = 1101,             // REAL: Houdini units -> C4D units (100 = metres to cm)
    HDA_FLIP_Z = 1102,            // BOOL: negate Z (right- to left-handed)
    HDA_REVERSE_WINDING = 1103,   // BOOL: reverse polygon vertex order
    HDA_IMPORT_NORMALS = 1104,
    HDA_IMPORT_UVS = 1105,
    HDA_IMPORT_COLORS = 1106,
    HDA_IMPORT_GROUPS = 1107,
    HDA_IMPORT_MATERIALS = 1108,
    HDA_INSTANCE_MODE = 1109,     // LONG cycle: how instancer parts are realised
    HDA_INSTANCE_MODE_MULTI = 0,
    HDA_INSTANCE_MODE_RENDER = 1,
    HDA_INSTANCE_MODE_COPIES = 2,
    HDA_PHONG_ANGLE = 1110,       // REAL (degrees)

    HDA_GROUP_INPUTS = 1200,
    HDA_INPUT_0 = 1201,           // LINK fields; children of the object are used first
    HDA_INPUT_1 = 1202,
    HDA_INPUT_2 = 1203,
    HDA_INPUT_3 = 1204,
    HDA_INPUT_INFO = 1205,        // STATICTEXT: input labels from the HDA

    HDA_GROUP_PARMS = 1300,       // dynamic HDA parameters are inserted under this group

    HDA_GROUP_LOG = 1400,
    HDA_COOK_LOG = 1401,          // STRING (read only): last cook messages
    HDA_SHOW_LOG = 1402,          // BUTTON: print full log to console

    // Internal storage (not shown in UI)
    HDA_PARM_ID_MAP = 1900,       // sub-container: parm name -> description id
    HDA_PARM_NEXT_ID = 1901,      // next free dynamic id
    HDA_PARM_NAME_MAP = 1902,     // sub-container: description id -> parm name

    HDA_DYNAMIC_PARM_BASE = 100000 // first dynamic description id
};

#endif // OHOUDINIASSET_H__
