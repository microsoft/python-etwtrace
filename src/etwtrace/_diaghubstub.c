#include <Windows.h>
#define PY_SSIZE_T_CLEAN 1
#include <Python.h>

#define EXPORT __declspec(dllexport)
#define PROBE_IMPL __stdcall

static PyObject *_on_event = NULL;

EXPORT void PROBE_IMPL Cap_Enter_Function_Script(_In_ void* pFunction)
{
    if (_on_event) {
        PyObject *r = PyObject_CallFunction(_on_event, "sn", "Cap_Enter_Function_Script", (Py_ssize_t)pFunction);
        if (!r) {
            PyErr_WriteUnraisable(NULL);
        } else {
            Py_DECREF(r);
        }
    }
}

EXPORT void PROBE_IMPL Cap_Pop_Function_Script()
{
    if (_on_event) {
        PyObject *r = PyObject_CallFunction(_on_event, "s", "Cap_Pop_Function_Script");
        if (!r) {
            PyErr_WriteUnraisable(NULL);
        } else {
            Py_DECREF(r);
        }
    }
}

EXPORT void PROBE_IMPL Cap_Define_Script_Module(_In_ void* pModule, _In_z_ LPCWSTR szModuleName, _In_z_ LPCWSTR szFilePath)
{
    if (_on_event) {
        PyObject *r = PyObject_CallFunction(_on_event, "snuu", "Cap_Define_Script_Module", (Py_ssize_t)pModule, szModuleName, szFilePath);
        if (!r) {
            PyErr_WriteUnraisable(NULL);
        } else {
            Py_DECREF(r);
        }
    }
}

EXPORT void PROBE_IMPL Cap_Define_Script_Function(_In_ void* pFunction, _In_ void* pModule, _In_ int lineNum, _In_z_ LPCWSTR szName)
{
    if (_on_event) {
        PyObject *r = PyObject_CallFunction(_on_event, "snnnu", "Cap_Define_Script_Function", (Py_ssize_t)pFunction, (Py_ssize_t)pModule, (Py_ssize_t)lineNum, szName);
        if (!r) {
            PyErr_WriteUnraisable(NULL);
        } else {
            Py_DECREF(r);
        }
    }
}

EXPORT void PROBE_IMPL Stub_Write_Mark(_In_ int opcode, _In_z_ LPCWSTR szMark)
{
    if (_on_event) {
        PyObject *r = PyObject_CallFunction(_on_event, "snu", "Stub_Write_Mark", (Py_ssize_t)opcode, szMark);
        if (!r) {
            PyErr_WriteUnraisable(NULL);
        } else {
            Py_DECREF(r);
        }
    }
}

EXPORT void __stdcall OnEvent(PyObject *callable)
{
    Py_XDECREF(_on_event);
    _on_event = callable;
    Py_XINCREF(_on_event);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch(fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
