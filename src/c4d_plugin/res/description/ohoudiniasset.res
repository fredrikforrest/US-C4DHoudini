CONTAINER Ohoudiniasset
{
    NAME Ohoudiniasset;
    INCLUDE Obase;

    GROUP ID_OBJECTPROPERTIES
    {
        GROUP HDA_GROUP_ASSET
        {
            DEFAULT 1;
            FILENAME HDA_FILE { }
            LONG HDA_ASSET_INDEX { CYCLE { } }
            STATICTEXT HDA_ASSET_LABEL { }
            BOOL HDA_AUTO_COOK { }
            BOOL HDA_COOK_ON_FRAME { }
            BUTTON HDA_COOK { }
            BUTTON HDA_RELOAD { }
            STATICTEXT HDA_STATUS { }
        }
        GROUP HDA_GROUP_PARMS
        {
            DEFAULT 1;
        }
        GROUP HDA_GROUP_INPUTS
        {
            STATICTEXT HDA_INPUT_INFO { }
            LINK HDA_INPUT_0 { ACCEPT { Obase; } }
            LINK HDA_INPUT_1 { ACCEPT { Obase; } }
            LINK HDA_INPUT_2 { ACCEPT { Obase; } }
            LINK HDA_INPUT_3 { ACCEPT { Obase; } }
        }
        GROUP HDA_GROUP_CONVERT
        {
            REAL HDA_SCALE { MIN 0.0001; STEP 1.0; }
            BOOL HDA_FLIP_Z { }
            BOOL HDA_REVERSE_WINDING { }
            BOOL HDA_IMPORT_NORMALS { }
            BOOL HDA_IMPORT_UVS { }
            BOOL HDA_IMPORT_COLORS { }
            BOOL HDA_IMPORT_GROUPS { }
            BOOL HDA_IMPORT_MATERIALS { }
            LONG HDA_INSTANCE_MODE
            {
                CYCLE
                {
                    HDA_INSTANCE_MODE_MULTI;
                    HDA_INSTANCE_MODE_RENDER;
                    HDA_INSTANCE_MODE_COPIES;
                }
            }
            REAL HDA_PHONG_ANGLE { UNIT DEGREE; MIN 0; MAX 180; }
        }
        GROUP HDA_GROUP_LOG
        {
            STRING HDA_COOK_LOG { CUSTOMGUI MULTISTRING; SCALE_V; }
            BUTTON HDA_SHOW_LOG { }
        }
    }
}
