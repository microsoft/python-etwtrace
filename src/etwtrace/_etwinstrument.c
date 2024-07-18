#include <Windows.h>
#include <assert.h>

#include "_etwcommon.h"
#include "_trace.h"

const SIZE_T CHUNK_LEN = 1020;

struct ETWINSTRUMENT_STATE {
    struct ETWCOMMON_STATE common;
};


static int tracefunc(PyObject *module, PyFrameObject *frame, int what, PyObject *arg)
{
    struct ETWINSTRUMENT_STATE *state;
    FUNC_ID from_thunk;
    size_t from_line;
    FUNC_ID to_thunk;
    PyObject *code_obj;

    if (what == PyTrace_CALL || what == PyTrace_C_CALL) {
        state = PyModule_GetState(module);
        PyFrameObject *back = (what == PyTrace_CALL) ? PyFrame_GetBack(frame) : frame;
        if (back) {
            code_obj = (PyObject *)PyFrame_GetCode(back);
            from_thunk = ETWCOMMON_find_or_register_code_object(&state->common, code_obj);
            from_line = PyFrame_GetLineNumber(back);
            Py_DECREF(code_obj);
            if (back != frame) {
                Py_DECREF(back);
            }
            if (from_thunk == FUNC_ID_ERROR) {
                return -1;
            }
        } else {
            from_thunk = FUNC_ID_NOT_FOUND;
        }
        if (what == PyTrace_CALL) {
            code_obj = (PyObject *)PyFrame_GetCode(frame);
            to_thunk = ETWCOMMON_find_or_register_code_object(&state->common, code_obj);
            Py_DECREF(code_obj);
        } else {
            to_thunk = ETWCOMMON_find_or_register_callable(&state->common, arg);
        }
        if (to_thunk == FUNC_ID_ERROR) {
            return -1;
        }
        // Don't recheck from_thunk - it's either valid or empty at this point
        if (FUNC_ID_IS_VALID(to_thunk)) {
            WriteFunctionPush(from_thunk, from_line, to_thunk);
        }
    }

    if (what == PyTrace_RETURN || what == PyTrace_C_RETURN || what == PyTrace_C_EXCEPTION) {
        state = PyModule_GetState(module);
        if (what == PyTrace_RETURN) {
            code_obj = (PyObject *)PyFrame_GetCode(frame);
            from_thunk = ETWCOMMON_find_or_register_code_object(&state->common, code_obj);
            Py_DECREF(code_obj);
        } else {
            from_thunk = ETWCOMMON_find_or_register_callable(&state->common, arg);
        }
        if (from_thunk == FUNC_ID_ERROR) {
            return -1;
        }
        if (FUNC_ID_IS_VALID(from_thunk)) {
            WriteFunctionPop(from_thunk);
        }
    }

    return 0;
}


static PyObject *etwinstrument_enable(PyObject *module, PyObject *args)
{
    int and_threads = 1;
    if (!PyArg_ParseTuple(args, "|p:enable", &and_threads)) {
        return NULL;
    }

    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(module);
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *interp_dict = PyInterpreterState_GetDict(interp);
    if (!interp_dict) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict required for tracing");
        return NULL;
    }
    if (PyDict_SetItemString(interp_dict, "etwtrace._etwinstrument", module) < 0) {
        return NULL;
    }
    if (!ETWCOMMON_Init(&state->common, state)) {
        return NULL;
    }
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

    Register();
    WriteBeginThread(GetCurrentThreadId());
    PyEval_SetProfile(tracefunc, module);


    Py_RETURN_NONE;
}


static int _trace_back(PyObject *module, PyFrameObject *frame)
{
    PyFrameObject *back = PyFrame_GetBack(frame);
    if (back) {
        if (_trace_back(module, back) < 0) {
            return -1;
        }
    }

    return tracefunc(module, frame, PyTrace_CALL, NULL);
}


static PyObject *etwinstrument_enable_thread(PyObject *module, PyObject *args)
{
    Register();
    WriteBeginThread(GetCurrentThreadId());
    PyEval_SetProfile(tracefunc, module);

    Py_RETURN_NONE;
}


static PyObject *etwinstrument_disable(PyObject *module, PyObject *args)
{
    PyEval_SetProfile(NULL, NULL);

    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(module);
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *interp_dict = PyInterpreterState_GetDict(interp);
    if (!interp_dict) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict required for tracing");
        return NULL;
    }
    if (PyDict_DelItemString(interp_dict, "etwtrace._etwinstrument") < 0) {
        PyErr_Clear();
        PyErr_SetString(PyExc_RuntimeError, "tracing was not enabled");
        return NULL;
    }
    if (!ETWCOMMON_Clear(&state->common)) {
        return NULL;
    }

    WriteEndThread(GetCurrentThreadId());
    Unregister();

    Py_RETURN_NONE;
}


static PyObject *etwinstrument_get_ignored_files(PyObject *module, PyObject *args)
{
    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(module);
    Py_INCREF(state->common.ignored_files);
    return state->common.ignored_files;
}


static PyObject *etwinstrument_get_include_prefix(PyObject *module, PyObject *args)
{
    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(module);
    Py_INCREF(state->common.include_prefix);
    return state->common.include_prefix;
}


static PyObject *etwinstrument_get_info(PyObject *module, PyObject *args)
{
    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(module);
    // Schema history:
    // __name__ 1 arch reserved
    return Py_BuildValue("sisn",
        "_etwinstrument",
        1, // version number
#ifdef _ARM64_
        "ARM64",
#else
        "AMD64",
#endif
        (Py_ssize_t)0
    );
}


static int etwinstrument_exec(PyObject *m)
{
    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(m);

    if (!ETWCOMMON_Init(&state->common, state)) {
        return -1;
    }

    return 0;
}


static int etwinstrument_traverse(PyObject *m, visitproc visit, void *arg)
{
    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(m);
    ETWCOMMON_VISIT(&state->common);
    return 0;
}


static int etwinstrument_clear(PyObject *m)
{
    struct ETWINSTRUMENT_STATE *state = PyModule_GetState(m);
    ETWCOMMON_Clear(&state->common);
    return 0;
}


static void etwinstrument_free(void *m)
{
    //struct ETWINSTRUMENT_STATE *state = PyModule_GetState((PyObject *)m);
}


static struct PyMethodDef etwinstrument_methods[] = {
    { "enable", etwinstrument_enable, METH_VARARGS,
      "Enables tracing, optionally for all created threads and interpreters." },
    { "_enable_thread", etwinstrument_enable_thread, METH_VARARGS,
      "Enables tracing on a new thread." },
    { "disable", etwinstrument_disable, METH_VARARGS,
      "Enables tracing, optionally for all created threads and interpreters." },
    { "write_mark", ETWCOMMON_write_mark, METH_VARARGS,
      "Write a custom mark into the trace." },
    { "get_ignored_files", etwinstrument_get_ignored_files, METH_NOARGS,
      "Returns a reference to the set containing filenames to ignore" },
    { "get_include_prefixes", etwinstrument_get_include_prefix, METH_NOARGS,
      "Returns a reference to the list containing path prefixes to include" },
    { "_get_technical_info", etwinstrument_get_info, METH_NOARGS,
      "Returns technical information about the build" },
    { NULL },
};


static struct PyModuleDef_Slot etwinstrument_slots[] = {
    { Py_mod_exec, etwinstrument_exec },
    { 0, NULL }
};


static struct PyModuleDef _etwinstrumentmodule = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "_etwinstrument",
    .m_doc = "Implementation for ETW instrumentation",
    .m_size = sizeof(struct ETWINSTRUMENT_STATE),
    .m_methods = etwinstrument_methods,
    .m_slots = etwinstrument_slots,
    .m_traverse = etwinstrument_traverse,
    .m_clear = etwinstrument_clear,
    .m_free = etwinstrument_free
};

PyMODINIT_FUNC PyInit__etwinstrument(void)
{
    return PyModuleDef_Init(&_etwinstrumentmodule);
}
