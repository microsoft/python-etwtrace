#include <Windows.h>
#include <assert.h>

#include "_etwcommon.h"
#include "_trace.h"

const SIZE_T DEFAULT_TABLE_SIZE = 4 * 1024 * 1024;
const int THUNK_ALIGNMENT = 16;


typedef PyObject *(*PThunk)(PyThreadState *tstate, FRAME_OBJECT *frame, int o, _PyFrameEvalFunction eval);

#pragma optimize("", off)
#pragma optimize("s", on)
static PyObject *_thunk(PyThreadState *tstate, FRAME_OBJECT *frame, int o, _PyFrameEvalFunction eval)
{
    return eval(tstate, frame, o);
}
#pragma optimize("", on)


struct THUNK {
    PThunk thunk;
    FUNC_ID func_id;
};


#ifdef _ARM64_
struct UNWIND_INFO {
    unsigned char Version : 3;
    unsigned char Flags : 5;
    unsigned char SizeOfProlog;
    unsigned char CountOfCodes;
    unsigned char FrameRegister : 4;
    unsigned char FrameOffset : 4;
    // Just to make sure we copy enough
    unsigned short UnwindCode[8];
};
#else
struct UNWIND_INFO {
    UCHAR Version : 3;
    UCHAR Flags : 5;
    UCHAR SizeOfProlog;
    UCHAR CountOfCodes;
    UCHAR FrameRegister : 4;
    UCHAR FrameOffset : 4;
    // Just to make sure we copy enough
    DWORD ExtraData[4];
};
#endif


struct THUNK_TABLE {
    struct THUNK_TABLE *next;
    void *func_table;
    DWORD64 base;
    SIZE_T size;
    int allocated;
    struct THUNK thunk[1];
};


struct ETWTRACE_STATE {
    struct ETWCOMMON_STATE common;
    _PyFrameEvalFunction default_eval;
    struct THUNK_TABLE *table;
    SIZE_T table_size;
    int thunk_count;
    USHORT thunk_size;
    USHORT thunk_stride;
    DWORD unwind_offset;
    struct UNWIND_INFO unwind_info;
    RUNTIME_FUNCTION *functions;
};


static struct THUNK_TABLE *AllocThunkTable(struct ETWTRACE_STATE *state)
{
    int err = 0;
    struct THUNK_TABLE *table = NULL;
    void *new_table = NULL;

    table = (struct THUNK_TABLE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(struct THUNK_TABLE) + sizeof(struct THUNK) * (state->thunk_count - 1));
    if (!table) goto error;

    new_table = VirtualAlloc(NULL, state->table_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!new_table) goto error;

    table->base = (DWORD64)new_table;
    table->size = state->table_size;

    for (int i = 0; i < state->thunk_count; ++i) {
        UINT8* p = (UINT8*)new_table + (i * state->thunk_stride);
        memcpy(p, _thunk, state->thunk_size);
        table->thunk[i].thunk = (PThunk)p;
    }
    memcpy((UINT8*)new_table + state->unwind_offset, &state->unwind_info, sizeof(struct UNWIND_INFO));

    if (!VirtualAlloc(new_table, state->table_size, MEM_COMMIT, PAGE_EXECUTE_READ)) {
        goto error;
    }

    err = RtlAddGrowableFunctionTable(
        &table->func_table, state->functions,
        state->thunk_count, state->thunk_count,
        table->base, table->base + state->table_size
    );
    if (err) goto nt_error;

    return table;

error:
    err = GetLastError();
nt_error:
    if (new_table)
        VirtualFree(new_table, state->table_size, MEM_RELEASE);
    if (table)
        HeapFree(GetProcessHeap(), 0, table);
    SetLastError(err);
    return NULL;
}


static void FreeThunkTable(struct THUNK_TABLE *table)
{
    struct THUNK_TABLE *tt = table;
    while (tt) {
        struct THUNK_TABLE *next = tt->next;
        RtlDeleteGrowableFunctionTable(tt->func_table);
        VirtualFree((void *)tt->base, tt->size, MEM_RELEASE);
        HeapFree(GetProcessHeap(), 0, tt);
        tt = next;
    }
}


struct THUNK IGNORED_THUNK = { 0 };


static struct THUNK *find_thunk(struct ETWTRACE_STATE *state, FUNC_ID func_id)
{
    struct THUNK_TABLE *tt = state->table;

    while (tt && FUNC_ID_IS_VALID(func_id)) {
        if (func_id <= tt->allocated) {
            struct THUNK *t = &tt->thunk[func_id - 1];
            return t;
        }

        func_id -= tt->allocated;
        tt = tt->next;
    }

    return &IGNORED_THUNK;
}


static FUNC_ID alloc_new_thunk(
    struct ETWCOMMON_STATE *common,
    PyObject *key,
    const wchar_t *module,
    const wchar_t *sourcePath,
    const wchar_t *name,
    size_t lineno,
    int is_python_code
) {
    struct ETWTRACE_STATE *state = (struct ETWTRACE_STATE *)common->owner;

    struct THUNK_TABLE *tt = state->table;
    FUNC_ID func_id = FUNC_ID_FIRST;

    while (tt) {
        if (tt->allocated < state->thunk_count) {
            struct THUNK *t = &tt->thunk[tt->allocated];
            func_id += tt->allocated;
            t->func_id = func_id;
            tt->allocated += 1;

            void *pBegin = (void *)t->thunk;
            void *pEnd = (void *)((UINT8*)pBegin + state->thunk_size);
            WriteFunctionEvent(func_id, pBegin, pEnd, sourcePath ? sourcePath : module, name, lineno, is_python_code);

            return func_id;
        }

        if (!tt->next) {
            tt->next = AllocThunkTable(state);
            if (!tt->next) {
                PyErr_SetFromWindowsErr(0);
                return FUNC_ID_ERROR;
            }
        }
        tt = tt->next;
        func_id += state->thunk_count;
    }

    if (!PyErr_Occurred()) {
        PyErr_SetString(PyExc_SystemError, "alloc_new_thunk: failed without exception");
    }
    return FUNC_ID_ERROR;
}


static __declspec(noinline)
struct THUNK *GetThunkForPythonFrame(PyThreadState *tstate, FRAME_OBJECT *frame, int o, _PyFrameEvalFunction *default_eval)
{
    PyInterpreterState *interp = PyThreadState_GetInterpreter(tstate);
    PyObject *interp_dict = interp ? PyInterpreterState_GetDict(interp) : NULL;
    if (!interp_dict) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict required for tracing");
        return NULL;
    }
    PyObject *module = PyDict_GetItemString(interp_dict, "etwtrace._etwtrace");
    if (!module) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict missing etwtrace module");
        return NULL;
    }
    PyObject *code = PyUnstable_InterpreterFrame_GetCode(frame);
    struct ETWTRACE_STATE *state = PyModule_GetState(module);
    *default_eval = state->default_eval;
    FUNC_ID func_id = ETWCOMMON_find_or_register_code_object(&state->common, code);
    Py_DECREF(code);
    switch (func_id) {
    case FUNC_ID_IGNORED:
        return &IGNORED_THUNK;
    case FUNC_ID_NOT_FOUND:
        PyErr_SetString(PyExc_SystemError, "did not find or register thunk");
        return NULL;
    case FUNC_ID_ERROR:
        return NULL;
    default:
        break;
    }
    return find_thunk(state, func_id);
}


#pragma optimize("", off)
#pragma optimize("t", on)
static PyObject *PythonFrame(PyThreadState *tstate, FRAME_OBJECT *frame, int o)
{
    _PyFrameEvalFunction default_eval;
    struct THUNK *thunk = GetThunkForPythonFrame(tstate, frame, o, &default_eval);
    if (!thunk)
        return NULL;
    if (!thunk->thunk) {
        if (!default_eval) {
            PyErr_SetString(PyExc_SystemError, "called thunk without stack sampling enabled");
            return NULL;
        }
        return default_eval(tstate, frame, o);
    }
    return thunk->thunk(tstate, frame, o, default_eval);
}
#pragma optimize("", on)


static PyObject *etwtrace_enable(PyObject *module, PyObject *args)
{
    int and_threads = 1;
    if (!PyArg_ParseTuple(args, "|p:enable", &and_threads)) {
        return NULL;
    }

    struct ETWTRACE_STATE *state = PyModule_GetState(module);
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *interp_dict = PyInterpreterState_GetDict(interp);
    if (!interp_dict) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict required for tracing");
        return NULL;
    }

    if (PyDict_SetItemString(interp_dict, "etwtrace._etwtrace", module) < 0) {
        return NULL;
    }

    if (!ETWCOMMON_Init(&state->common, state)) {
        return NULL;
    }
    state->common.get_new_func_id = alloc_new_thunk;

    if (!state->table) {
        state->table = AllocThunkTable(state);
        if (!state->table) {
            PyErr_SetFromWindowsErr(0);
            return NULL;
        }
    }
    if (!state->default_eval) {
        state->default_eval = _PyInterpreterState_GetEvalFrameFunc(interp);
    }

    Register();
    WriteBeginThread(GetCurrentThreadId());

    _PyInterpreterState_SetEvalFrameFunc(interp, PythonFrame);

    Py_RETURN_NONE;
}


static PyObject *etwtrace_disable(PyObject *module, PyObject *args)
{
    struct ETWTRACE_STATE *state = PyModule_GetState(module);
    PyInterpreterState *interp = PyInterpreterState_Get();

    if (state->default_eval) {
        _PyInterpreterState_SetEvalFrameFunc(interp, state->default_eval);
        state->default_eval = NULL;
    }

    WriteEndThread(GetCurrentThreadId());
    Unregister();

    PyObject *interp_dict = PyInterpreterState_GetDict(interp);
    if (!interp_dict) {
        PyErr_SetString(PyExc_SystemError, "interpreter dict required for tracing");
        return NULL;
    }
    if (PyDict_DelItemString(interp_dict, "etwtrace._etwtrace") < 0) {
        PyErr_Clear();
        PyErr_SetString(PyExc_RuntimeError, "tracing was not enabled");
        return NULL;
    }
    if (state->table) {
        FreeThunkTable(state->table);
        state->table = NULL;
    }

    Py_RETURN_NONE;
}


static PyObject *etwtrace_get_ignored_files(PyObject *module, PyObject *args)
{
    struct ETWTRACE_STATE *state = PyModule_GetState(module);
    Py_INCREF(state->common.ignored_files);
    return state->common.ignored_files;
}


static PyObject *etwtrace_get_include_prefix(PyObject *module, PyObject *args)
{
    struct ETWTRACE_STATE *state = PyModule_GetState(module);
    Py_INCREF(state->common.include_prefix);
    return state->common.include_prefix;
}


static PyObject *etwtrace_get_info(PyObject *module, PyObject *args)
{
    struct ETWTRACE_STATE *state = PyModule_GetState(module);
    // Schema history:
    // __name__ 1 arch table_size thunk_count thunk_size thunk_stride unwind_offset
    return Py_BuildValue("sisnnnnn",
        "_etwtrace",
        1, // version number
#ifdef _ARM64_
        "ARM64",
#else
        "AMD64",
#endif
        (Py_ssize_t)state->table_size,
        (Py_ssize_t)state->thunk_count,
        (Py_ssize_t)state->thunk_size,
        (Py_ssize_t)state->thunk_stride,
        (Py_ssize_t)state->unwind_offset
    );
}


static int etwtrace_exec(PyObject *m)
{
    struct ETWTRACE_STATE *state = PyModule_GetState(m);

    DWORD64 imagebase;
    UNWIND_HISTORY_TABLE history;
    PRUNTIME_FUNCTION orig = RtlLookupFunctionEntry((DWORD64)(void *)_thunk, &imagebase, &history);
#ifdef _ARM64_
    if ((orig->UnwindData & 3) == 3) {
        if ((orig->UnwindData & 4) == 0) {
            // Redirects to a different entry
            orig = (PRUNTIME_FUNCTION)(imagebase + orig->UnwindData - 3);
        } else {
            // Invalid for (UnwindData & 7) == 3
            orig = NULL;
        }
    }
#endif
    if (!orig) {
        PyErr_SetString(PyExc_SystemError, "Unable to initialize thunks");
        return -1;
    }

    state->table_size = DEFAULT_TABLE_SIZE;
#ifdef _ARM64_
    int cb = (int)(orig->FunctionLength) * 4;
#else
    int cb = (int)(orig->EndAddress - orig->BeginAddress);
#endif
    state->thunk_size = cb;
    cb = (((cb - 1) / THUNK_ALIGNMENT) + 1) * THUNK_ALIGNMENT;
    state->thunk_stride = cb;
    assert(state->thunk_stride >= state->thunk_size);
    assert((state->thunk_stride % THUNK_ALIGNMENT) == 0);
    state->thunk_count = (int)((state->table_size - sizeof(struct UNWIND_INFO)) / state->thunk_stride);
    state->unwind_offset = state->thunk_stride * state->thunk_count;
    assert((state->unwind_offset % sizeof(void *)) == 0);
    assert(state->thunk_stride * state->thunk_count <= state->unwind_offset);
    assert(state->unwind_offset + sizeof(struct UNWIND_INFO) <= state->table_size);
#ifdef _ARM64_
    // Non-zero Flag indicates we don't have an address
    if (!orig->Flag)
#endif
    {
        memcpy(&state->unwind_info, (void *)(imagebase + orig->UnwindData), sizeof(struct UNWIND_INFO));
    }

    state->functions = (RUNTIME_FUNCTION *)HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY, sizeof(RUNTIME_FUNCTION) * state->thunk_count);
    if (!state->functions) {
        PyErr_NoMemory();
        return -1;
    }

    for (int i = 0; i < state->thunk_count; ++i) {
        state->functions[i].BeginAddress = i * state->thunk_stride;
#ifndef _ARM64_
        state->functions[i].EndAddress = i * state->thunk_stride + state->thunk_size;
#endif
        state->functions[i].UnwindData = state->unwind_offset;
    }

    state->table = NULL;

    if (!ETWCOMMON_Init(&state->common, state)) {
        HeapFree(GetProcessHeap(), 0, state->functions);
        state->functions = NULL;
        return -1;
    }

    return 0;
}


static int etwtrace_traverse(PyObject *m, visitproc visit, void *arg)
{
    struct ETWTRACE_STATE *state = PyModule_GetState(m);
    ETWCOMMON_VISIT(&state->common);
    return 0;
}


static int etwtrace_clear(PyObject *m)
{
    struct ETWTRACE_STATE *state = PyModule_GetState(m);
    ETWCOMMON_Clear(&state->common);
    return 0;
}


static void etwtrace_free(void *m)
{
    struct ETWTRACE_STATE *state = PyModule_GetState((PyObject *)m);

    HeapFree(GetProcessHeap(), 0, state->functions);
    state->functions = NULL;
}


static struct PyMethodDef etwtrace_methods[] = {
    { "enable", etwtrace_enable, METH_VARARGS,
      "Enables tracing, optionally for all created threads and interpreters." },
    { "disable", etwtrace_disable, METH_VARARGS,
      "Enables tracing, optionally for all created threads and interpreters." },
    { "write_mark", ETWCOMMON_write_mark, METH_VARARGS,
      "Write a custom mark into the trace." },
    { "get_ignored_files", etwtrace_get_ignored_files, METH_NOARGS,
      "Returns a reference to the set containing filenames to ignore" },
    { "get_include_prefixes", etwtrace_get_include_prefix, METH_NOARGS,
      "Returns a reference to the list containing path prefixes to include" },
    { "_get_technical_info", etwtrace_get_info, METH_NOARGS,
      "Returns technical information about the build" },
    { NULL },
};


static struct PyModuleDef_Slot etwtrace_slots[] = {
    { Py_mod_exec, etwtrace_exec },
    { 0, NULL }
};


static struct PyModuleDef _etwtracemodule = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "_etwtrace",
    .m_doc = "Implementation for ETW tracing",
    .m_size = sizeof(struct ETWTRACE_STATE),
    .m_methods = etwtrace_methods,
    .m_slots = etwtrace_slots,
    .m_traverse = etwtrace_traverse,
    .m_clear = etwtrace_clear,
    .m_free = etwtrace_free
};

PyMODINIT_FUNC PyInit__etwtrace(void)
{
    return PyModuleDef_Init(&_etwtracemodule);
}
