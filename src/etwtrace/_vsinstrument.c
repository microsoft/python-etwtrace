#include <Windows.h>
#include <assert.h>

#include "_etwcommon.h"

const SIZE_T CHUNK_LEN = 1020;

typedef void (*Cap_Enter_Function_Script)(_In_ void* pFunction);
typedef void (*Cap_Pop_Function_Script)();
typedef void (*Cap_Define_Script_Module)(_In_ void* pModule, _In_z_ LPCWSTR szModuleName, _In_z_ LPCWSTR szFilePath);
typedef void (*Cap_Define_Script_Function)(_In_ void* pFunction, _In_ void* pModule, _In_ int lineNum, _In_z_ LPCWSTR szName);
typedef void (*Stub_Write_Mark)(_In_ int opcode, _In_z_ LPCWSTR szMark);


struct VSINSTRUMENT_STATE {
    struct ETWCOMMON_STATE common;
    PyObject *modules;
    ptrdiff_t nextModuleId;
    HMODULE hModule;
    Cap_Enter_Function_Script EnterFunction;
    Cap_Pop_Function_Script PopFunction;
    Cap_Define_Script_Module DefineModule;
    Cap_Define_Script_Function DefineFunction;
    Stub_Write_Mark WriteMark;
};


static FUNC_ID alloc_new_thunk(
    struct ETWCOMMON_STATE *common,
    PyObject *key,
    const wchar_t *module,
    const wchar_t *sourcePath,
    const wchar_t *name,
    size_t lineno,
    int is_python_code
) {
    struct VSINSTRUMENT_STATE *state = (struct VSINSTRUMENT_STATE *)common->owner;
    if (!sourcePath && !module) {
        return FUNC_ID_IGNORED;
    }
    PyObject *module_key = PyUnicode_FromWideChar(sourcePath ? sourcePath : module, -1);
    if (!module_key) {
        return FUNC_ID_ERROR;
    }
    PyObject *module_id = PyDict_GetItem(state->modules, module_key);
    void *module_id_value;
    if (module_id) {
        Py_DECREF(module_key);
        module_id_value = PyLong_AsVoidPtr(module_id);
        Py_DECREF(module_id);
        if (PyErr_Occurred()) {
            return FUNC_ID_ERROR;
        }
    } else {
        module_id_value = (void *)state->nextModuleId;
        module_id = PyLong_FromVoidPtr(module_id_value);
        if (!module_id) {
            Py_DECREF(module_key);
            return FUNC_ID_ERROR;
        }
        PyDict_SetItem(state->modules, module_key, module_id);
        Py_DECREF(module_id);
        Py_DECREF(module_key);
        if (PyErr_Occurred()) {
            return FUNC_ID_ERROR;
        }
        state->nextModuleId++;
        (*state->DefineModule)(module_id_value, module, sourcePath);
    }
    
    FUNC_ID func_id = ETWCOMMON_ClaimFuncId(common, key, 0);
    if (FUNC_ID_IS_VALID(func_id)) {
        if ((int)lineno != lineno) {
            lineno = (int)-1;
        }
        (*state->DefineFunction)((void *)(ptrdiff_t)func_id, module_id_value, (int)lineno, name);
    }
    return func_id;
}


static int tracefunc(PyObject *module, PyFrameObject *frame, int what, PyObject *arg)
{
    struct VSINSTRUMENT_STATE *state;
    if (what == PyTrace_CALL || what == PyTrace_C_CALL) {
        FUNC_ID to_thunk;
        state = PyModule_GetState(module);
        if (what == PyTrace_CALL) {
            PyObject *code_obj = (PyObject *)PyFrame_GetCode(frame);
            to_thunk = ETWCOMMON_find_or_register_code_object(&state->common, code_obj);
            Py_DECREF(code_obj);
        } else {
            to_thunk = ETWCOMMON_find_or_register_callable(&state->common, arg);
        }
        if (to_thunk == FUNC_ID_ERROR) {
            return -1;
        }
        if (FUNC_ID_IS_VALID(to_thunk)) {
            state->EnterFunction((void *)(ptrdiff_t)to_thunk);
        }
    }

    if (what == PyTrace_RETURN || what == PyTrace_C_RETURN || what == PyTrace_C_EXCEPTION) {
        FUNC_ID from_thunk;
        state = PyModule_GetState(module);
        if (what == PyTrace_RETURN) {
            PyObject *code_obj = (PyObject *)PyFrame_GetCode(frame);
            from_thunk = ETWCOMMON_find_or_register_code_object(&state->common, code_obj);
            Py_DECREF(code_obj);
        } else {
            from_thunk = ETWCOMMON_find_or_register_callable(&state->common, arg);
        }
        if (from_thunk == FUNC_ID_ERROR) {
            return -1;
        }
        if (FUNC_ID_IS_VALID(from_thunk)) {
            state->PopFunction(from_thunk);
        }
    }

    return 0;
}


static PyObject *vsinstrument_enable(PyObject *module, PyObject *args)
{
    int and_threads = 1;
    if (!PyArg_ParseTuple(args, "|p:enable", &and_threads)) {
        return NULL;
    }

    struct VSINSTRUMENT_STATE *state = PyModule_GetState(module);
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *interp_dict = PyInterpreterState_GetDict(interp);
    if (!interp_dict) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict required for tracing");
        return NULL;
    }
    if (PyDict_SetItemString(interp_dict, "etwtrace._vsinstrument", module) < 0) {
        return NULL;
    }
    state->modules = PyDict_New();
    if (!state->modules) {
        return NULL;
    }
    if (!ETWCOMMON_Init(&state->common, state)) {
        return NULL;
    }

    state->common.get_new_func_id = alloc_new_thunk;

    state->nextModuleId = 1;

    if (and_threads) {
        PyObject *threading = PyImport_ImportModule("threading");
        if (!threading) {
            return NULL;
        }
        PyObject *func = PyObject_GetAttrString(module, "_enable_thread");
        if (!func) {
            Py_DECREF(threading);
            return NULL;
        }
        PyObject *r = PyObject_CallMethod(threading, "setprofile", "O", func);
        Py_DECREF(threading);
        Py_DECREF(func);
        if (!r) {
            return NULL;
        }
        Py_DECREF(r);
    }

    PyEval_SetProfile(tracefunc, module);

    Py_RETURN_NONE;
}

static PyObject *vsinstrument_enable_thread(PyObject *module, PyObject *args)
{
    PyEval_SetProfile(tracefunc, module);
    Py_RETURN_NONE;
}


static PyObject *vsinstrument_disable(PyObject *module, PyObject *args)
{
    PyEval_SetProfile(NULL, NULL);

    struct VSINSTRUMENT_STATE *state = PyModule_GetState(module);
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *interp_dict = PyInterpreterState_GetDict(interp);
    if (!interp_dict) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict required for tracing");
        return NULL;
    }
    if (PyDict_DelItemString(interp_dict, "etwtrace._vsinstrument") < 0) {
        PyErr_Clear();
        PyErr_SetString(PyExc_RuntimeError, "tracing was not enabled");
        return NULL;
    }
    if (state->modules) {
        Py_CLEAR(state->modules);
    }
    if (!ETWCOMMON_Clear(&state->common)) {
        return NULL;
    }

    // No actions needed for VS interface

    Py_RETURN_NONE;
}


static PyObject *vsinstrument_write_mark(PyObject *module, PyObject *args)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState(module);
    if (!state->WriteMark) {
        PyErr_SetString(PyExc_RuntimeError, "unable to write marks");
        return NULL;
    }
    wchar_t _event_name[256];
    wchar_t *event_name = _event_name;
    Py_ssize_t cb_event_name = sizeof(_event_name);
    int opcode = 0;
    if (!PyArg_ParseTuple(args,
        "es#|i:write_mark",
        "utf-16-le", (char**)&event_name, &cb_event_name,
        &opcode
    )) {
        return NULL;
    }

    event_name[cb_event_name / sizeof(wchar_t)] = L'\0';
    (*state->WriteMark)(opcode, event_name);
    Py_RETURN_NONE;
}


static PyObject *vsinstrument_get_ignored_files(PyObject *module, PyObject *args)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState(module);
    Py_INCREF(state->common.ignored_files);
    return state->common.ignored_files;
}


static PyObject *vsinstrument_get_include_prefix(PyObject *module, PyObject *args)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState(module);
    Py_INCREF(state->common.include_prefix);
    return state->common.include_prefix;
}


static PyObject *vsinstrument_get_info(PyObject *module, PyObject *args)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState(module);
    // Schema history:
    // __name__ 1 arch reserved
    return Py_BuildValue("sisn",
        "_vsinstrument",
        1, // version number
#ifdef _ARM64_
        "ARM64",
#else
        "AMD64",
#endif
        (Py_ssize_t)0
    );
}


static int vsinstrument_exec(PyObject *m)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState(m);

    if (!ETWCOMMON_Init(&state->common, state)) {
        return -1;
    }

    // We ensure the DLL is loaded already before loading it again, but
    // use LoadLibraryW to increment the reference count to ensure it does not
    // get freed on us.
    if (!GetModuleHandleW(L"DiagnosticsHub.InstrumentationCollector.dll")) {
        PyErr_SetString(PyExc_RuntimeError, "VS tracing must be launched from Diagnostics Hub");
        return -1;
    }
    state->hModule = LoadLibraryW(L"DiagnosticsHub.InstrumentationCollector.dll");
    if (!state->hModule) {
        PyErr_SetFromWindowsErr(0);
        return -1;
    }

    state->EnterFunction = (Cap_Enter_Function_Script)GetProcAddress(state->hModule, "Cap_Enter_Function_Script");
    if (!state->EnterFunction) {
        PyErr_SetFromWindowsErr(0);
        return -1;
    }
    state->PopFunction = (Cap_Pop_Function_Script)GetProcAddress(state->hModule, "Cap_Pop_Function_Script");
    if (!state->PopFunction) {
        PyErr_SetFromWindowsErr(0);
        return -1;
    }
    state->DefineModule = (Cap_Define_Script_Module)GetProcAddress(state->hModule, "Cap_Define_Script_Module");
    if (!state->DefineModule) {
        PyErr_SetFromWindowsErr(0);
        return -1;
    }
    state->DefineFunction = (Cap_Define_Script_Function)GetProcAddress(state->hModule, "Cap_Define_Script_Function");
    if (!state->DefineFunction) {
        PyErr_SetFromWindowsErr(0);
        return -1;
    }
    // Allowed to be absent
    state->WriteMark = (Stub_Write_Mark)GetProcAddress(state->hModule, "Stub_Write_Mark");

    return 0;
}


static int vsinstrument_traverse(PyObject *m, visitproc visit, void *arg)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState(m);
    ETWCOMMON_VISIT(&state->common);
    Py_VISIT(state->modules);
    return 0;
}


static int vsinstrument_clear(PyObject *m)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState(m);
    ETWCOMMON_Clear(&state->common);
    Py_CLEAR(state->modules);
    return 0;
}


static void vsinstrument_free(void *m)
{
    struct VSINSTRUMENT_STATE *state = PyModule_GetState((PyObject *)m);
    if (state->hModule) {
        FreeLibrary(state->hModule);
        state->hModule = NULL;
    }
}


static struct PyMethodDef vsinstrument_methods[] = {
    { "enable", vsinstrument_enable, METH_VARARGS,
      "Enables tracing, optionally for all created threads and interpreters." },
    { "_enable_thread", vsinstrument_enable_thread, METH_VARARGS,
      "Enables tracing on a new thread." },
    { "disable", vsinstrument_disable, METH_VARARGS,
      "Enables tracing, optionally for all created threads and interpreters." },
    { "write_mark", vsinstrument_write_mark, METH_VARARGS,
      "Write a custom mark into the trace." },
    { "get_ignored_files", vsinstrument_get_ignored_files, METH_NOARGS,
      "Returns a reference to the set containing filenames to ignore" },
    { "get_include_prefixes", vsinstrument_get_include_prefix, METH_NOARGS,
      "Returns a reference to the list containing path prefixes to include" },
    { "_get_technical_info", vsinstrument_get_info, METH_NOARGS,
      "Returns technical information about the build" },
    { NULL },
};


static struct PyModuleDef_Slot vsinstrument_slots[] = {
    { Py_mod_exec, vsinstrument_exec },
    { 0, NULL }
};


static struct PyModuleDef _vsinstrumentmodule = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "_vsinstrument",
    .m_doc = "Implementation for ETW instrumentation",
    .m_size = sizeof(struct VSINSTRUMENT_STATE),
    .m_methods = vsinstrument_methods,
    .m_slots = vsinstrument_slots,
    .m_traverse = vsinstrument_traverse,
    .m_clear = vsinstrument_clear,
    .m_free = vsinstrument_free
};

PyMODINIT_FUNC PyInit__vsinstrument(void)
{
    return PyModuleDef_Init(&_vsinstrumentmodule);
}
