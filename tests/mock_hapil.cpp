// A fake libHAPIL with just enough exports to exercise hapi::Lib::load().
#include <HAPI/HAPI.h>
#include <cstring>

HAPI_DECL HAPI_GetEnvInt(HAPI_EnvIntType t, int* v)
{
    switch (t)
    {
        case HAPI_ENVINT_VERSION_HOUDINI_MAJOR: *v = 22; break;
        case HAPI_ENVINT_VERSION_HOUDINI_MINOR: *v = 0; break;
        case HAPI_ENVINT_VERSION_HOUDINI_BUILD: *v = 475; break;
        case HAPI_ENVINT_VERSION_HOUDINI_PATCH: *v = 0; break;
        case HAPI_ENVINT_VERSION_HOUDINI_ENGINE_MAJOR: *v = 9; break;
        case HAPI_ENVINT_VERSION_HOUDINI_ENGINE_MINOR: *v = 0; break;
        case HAPI_ENVINT_VERSION_HOUDINI_ENGINE_API: *v = 0; break;
        default: *v = -1; return HAPI_RESULT_FAILURE;
    }
    return HAPI_RESULT_SUCCESS;
}
HAPI_DECL HAPI_CreateThriftNamedPipeSession(HAPI_Session* s, const char*, const HAPI_SessionInfo*)
{
    s->type = HAPI_SESSION_THRIFT; s->id = 42; return HAPI_RESULT_SUCCESS;
}
HAPI_DECL HAPI_Initialize(const HAPI_Session*, const HAPI_CookOptions*, HAPI_Bool, int, const char*, const char*, const char*, const char*, const char*)
{
    return HAPI_RESULT_SUCCESS;
}
HAPI_DECL HAPI_CookNode(const HAPI_Session*, HAPI_NodeId, const HAPI_CookOptions*) { return HAPI_RESULT_SUCCESS; }
HAPI_DECL HAPI_GetString(const HAPI_Session*, HAPI_StringHandle, char* out, int len)
{
    std::strncpy(out, "mock", size_t(len)); return HAPI_RESULT_SUCCESS;
}
HAPI_DECL HAPI_GetStringBufLength(const HAPI_Session*, HAPI_StringHandle, int* len) { *len = 5; return HAPI_RESULT_SUCCESS; }
HAPI_DECL HAPI_IsSessionValid(const HAPI_Session*) { return HAPI_RESULT_SUCCESS; }
HAPI_DECL HAPI_CloseSession(const HAPI_Session*) { return HAPI_RESULT_SUCCESS; }
HAPI_DECL HAPI_Cleanup(const HAPI_Session*) { return HAPI_RESULT_SUCCESS; }
HAPI_DECL HAPI_GetStatus(const HAPI_Session*, HAPI_StatusType, int* status) { *status = HAPI_STATE_READY; return HAPI_RESULT_SUCCESS; }
