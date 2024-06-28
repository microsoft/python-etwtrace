#pragma once

#include "_func_id.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>


#if PY_VERSION_HEX >= 0x030D0000

#define FRAME_OBJECT struct _PyInterpreterFrame
#define CO_QUALNAME "co_qualname"

#elif PY_VERSION_HEX >= 0x030B0000

#include <internal/pycore_frame.h>
#define FRAME_OBJECT struct _PyInterpreterFrame
#define CO_QUALNAME "co_qualname"

#else

#include <frameobject.h>
#define FRAME_OBJECT PyFrameObject
#define CO_QUALNAME "co_name"

#endif


#if PY_VERSION_HEX < 0x030D0000

static inline PyObject *PyUnstable_InterpreterFrame_GetCode(FRAME_OBJECT *frame) {
    PyObject *code = (PyObject *)frame->f_code;
    Py_INCREF(code);
    return code;
}

#endif


#if PY_VERSION_HEX < 0x030C0000

static inline Py_ssize_t PyUnstable_Eval_RequestCodeExtraIndex(freefunc f) {
    return _PyEval_RequestCodeExtraIndex(f);
}

static inline int PyUnstable_Code_GetExtra(PyObject *code, Py_ssize_t index, void **extra) {
    return _PyCode_GetExtra(code, index, extra);
}

static inline int PyUnstable_Code_SetExtra(PyObject *code, Py_ssize_t index, void *extra) {
    return _PyCode_SetExtra(code, index, extra);
}

#else

#endif


struct ETWCOMMON_STATE {
    // Fields for the owner to set/use directly
    FUNC_ID (*get_new_func_id)(
        struct ETWCOMMON_STATE *state,
        PyObject *key,
        const wchar_t *module,
        const wchar_t *sourcePath,
        const wchar_t *name,
        size_t lineno,
        int is_python_code
    );
    void *owner;

    // Our 'private' fields
    PyObject *func_table;
    PyObject *include_prefix;
    PyObject *ignored_files;
    FUNC_ID next_func_id;
    Py_ssize_t co_extra_index;
};

int ETWCOMMON_Init(struct ETWCOMMON_STATE *state, void *owner);
int ETWCOMMON_Clear(struct ETWCOMMON_STATE *state);
#define ETWCOMMON_VISIT(s) do { int i = ETWCOMMON_Visit(s, visit, arg); if (i) return i; } while (0);
int ETWCOMMON_Visit(struct ETWCOMMON_STATE *state, visitproc visit, void *arg);

FUNC_ID ETWCOMMON_ClaimFuncId(struct ETWCOMMON_STATE *state, PyObject *key, FUNC_ID func_id);
FUNC_ID ETWCOMMON_find_or_register_code_object(struct ETWCOMMON_STATE *state, PyObject *code);
FUNC_ID ETWCOMMON_find_or_register_callable(struct ETWCOMMON_STATE *state, PyObject *code);

PyObject *ETWCOMMON_write_mark(PyObject *module, PyObject *args);
