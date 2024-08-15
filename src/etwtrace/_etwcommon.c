#include <Windows.h>
#include "_etwcommon.h"
#include "_trace.h"


static FUNC_ID should_ignore(struct ETWCOMMON_STATE *state, PyObject *code, PyObject *filename, int is_filename)
{
    switch (PySet_Contains(state->ignored_files, filename)) {
    case 0:
        break;
    case 1:
        return FUNC_ID_IGNORED;
    default:
        return FUNC_ID_ERROR;
    }

    return FUNC_ID_NOT_FOUND;
}


static FUNC_ID should_include(struct ETWCOMMON_STATE *state, PyObject *code, PyObject *filename, int is_filename)
{
    if (is_filename && PyObject_IsTrue(state->include_prefix) && PyList_Check(state->include_prefix)) {
        PyObject *t = PyList_AsTuple(state->include_prefix);
        if (!t) {
            return FUNC_ID_ERROR;
        }
        PyObject *r = PyObject_CallMethod(filename, "startswith", "O", t);
        Py_DECREF(t);
        if (!r) {
            return FUNC_ID_ERROR;
        }
        int found = PyObject_IsTrue(r);
        Py_DECREF(r);
        if (!found) {
            return FUNC_ID_IGNORED;
        }
    }

    return FUNC_ID_NOT_FOUND;
}


static FUNC_ID register_callable(struct ETWCOMMON_STATE *state, PyObject *code, PyObject *key)
{
    FUNC_ID func_id = FUNC_ID_ERROR;
    PyObject *o_qualname = NULL;
    PyObject *o_module = NULL;
    PyObject *u16qualname = NULL;
    PyObject *u16module = NULL;
    PyObject *nulls = NULL;
    const wchar_t *module;
    const wchar_t *qualname;

    o_module = PyObject_GetAttrString(code, "__module__");
    if (!o_module) {
        return 0;
    }

    switch (should_ignore(state, code, o_module, 0)) {
    case FUNC_ID_ERROR:
        func_id = FUNC_ID_ERROR;
        goto error;
    case FUNC_ID_IGNORED:
        func_id = FUNC_ID_IGNORED;
        goto error;
    }

    o_qualname = PyObject_GetAttrString(code, "__qualname__");
    if (!o_qualname) goto error;
    if (PyObject_IsTrue(o_module)) {
        u16module = PyUnicode_AsUTF16String(o_module);
        if (!u16module) goto error;
    } else {
        u16module = PyBytes_FromStringAndSize(NULL, 0);
    }
    if (PyObject_IsTrue(o_qualname)) {
        u16qualname = PyUnicode_AsUTF16String(o_qualname);
        if (!u16qualname) goto error;
    } else {
        u16qualname = PyBytes_FromStringAndSize(NULL, 0);
    }
    nulls = PyBytes_FromStringAndSize("\0\0", 2);
    if (!nulls) goto error;
    PyBytes_Concat(&u16module, nulls);
    if (!u16module) goto error;
    PyBytes_Concat(&u16qualname, nulls);
    if (!u16qualname) goto error;

    module = (const wchar_t *)PyBytes_AsString(u16module);
    if (module) {
        if (module[0] == 0xFEFF) ++module;
        qualname = (const wchar_t *)PyBytes_AsString(u16qualname);
        if (qualname) {
            if (qualname[0] == 0xFEFF) ++qualname;
            func_id = (*state->get_new_func_id)(state, key, module, NULL, qualname, 0, 0);
        }
    }

error:
    Py_XDECREF(u16module);
    Py_XDECREF(u16qualname);
    Py_XDECREF(o_module);
    Py_XDECREF(o_qualname);
    Py_XDECREF(nulls);
    return func_id;
}

FUNC_ID register_code_object(struct ETWCOMMON_STATE *state, PyObject *code, PyObject *key)
{
    FUNC_ID func_id = FUNC_ID_ERROR;
    PyObject *co_filename = NULL;
    PyObject *co_qualname = NULL;
    PyObject *co_firstlineno = NULL;
    PyObject *u16filename = NULL;
    PyObject *u16qualname = NULL;
    PyObject *nulls = NULL;
    const wchar_t *filename;
    const wchar_t *qualname;
    size_t lineno;

    co_filename = PyObject_GetAttrString(code, "co_filename");
    if (!co_filename) {
        return FUNC_ID_ERROR;
    }

    switch (should_ignore(state, code, co_filename, 1)) {
    case FUNC_ID_ERROR:
        func_id = FUNC_ID_ERROR;
        goto error;
    case FUNC_ID_IGNORED:
        func_id = FUNC_ID_IGNORED;
        goto error;
    }

    co_qualname = PyObject_GetAttrString(code, CO_QUALNAME);
    if (!co_qualname) goto error;
    co_firstlineno = PyObject_GetAttrString(code, "co_firstlineno");
    if (!co_firstlineno) goto error;
    u16filename = PyUnicode_AsUTF16String(co_filename);
    if (!u16filename) goto error;
    u16qualname = PyUnicode_AsUTF16String(co_qualname);
    if (!u16qualname) goto error;
    nulls = PyBytes_FromStringAndSize("\0\0", 2);
    if (!nulls) goto error;
    PyBytes_Concat(&u16filename, nulls);
    if (!u16filename) goto error;
    PyBytes_Concat(&u16qualname, nulls);
    if (!u16qualname) goto error;
    lineno = PyLong_AsSize_t(co_firstlineno);
    if (lineno == ~(SIZE_T)0 && PyErr_Occurred()) goto error;

    filename = (const wchar_t *)PyBytes_AsString(u16filename);
    if (filename) {
        if (filename[0] == 0xFEFF) ++filename;
        qualname = (const wchar_t *)PyBytes_AsString(u16qualname);
        if (qualname) {
            if (qualname[0] == 0xFEFF) ++qualname;
            func_id = (*state->get_new_func_id)(state, key, NULL, filename, qualname, lineno, 1);
        }
    }
error:
    Py_XDECREF(u16filename);
    Py_XDECREF(u16qualname);
    Py_XDECREF(nulls);
    Py_XDECREF(co_filename);
    Py_XDECREF(co_qualname);
    Py_XDECREF(co_firstlineno);
    return func_id;
}


static FUNC_ID find_func(struct ETWCOMMON_STATE *state, PyObject *code, PyObject **key_out, int is_co)
{
    FUNC_ID func_id = FUNC_ID_ERROR;
    PyObject *key = NULL;
    PyObject *co_filename = NULL;
    PyObject *o_qualname = NULL;

    co_filename = PyObject_GetAttrString(code, is_co ? "co_filename" : "__module__");
    if (!co_filename) goto error;

    switch (should_ignore(state, code, co_filename, is_co)) {
    case FUNC_ID_ERROR:
        func_id = FUNC_ID_ERROR;
        goto error;
    case FUNC_ID_IGNORED:
        func_id = FUNC_ID_IGNORED;
        goto error;
    }

    o_qualname = PyObject_GetAttrString(code, is_co ? CO_QUALNAME : "__qualname__");
    if (!o_qualname) goto error;
    key = PyTuple_Pack(2, co_filename, o_qualname);
    if (!key) goto error;
    co_filename = o_qualname = NULL;

    PyObject *o_id = PyObject_GetItem(state->func_table, key);
    if (o_id) {
        func_id = PyLong_AsFUNC_ID(o_id);
        Py_DECREF(o_id);
    } else {
        func_id = FUNC_ID_NOT_FOUND;
        PyErr_Clear();
        if (key_out) {
            *key_out = key;
            Py_INCREF(key);
        }
    }
error:
    Py_XDECREF(key);
    Py_XDECREF(o_qualname);
    Py_XDECREF(co_filename);
    return func_id;
}


FUNC_ID ETWCOMMON_find_or_register_code_object(struct ETWCOMMON_STATE *state, PyObject *code)
{
    PyObject *key = NULL;
    void *v_func_id;
    FUNC_ID func_id = 0;

    if (state->co_extra_index >= 0) {
        if (PyUnstable_Code_GetExtra(code, state->co_extra_index, &v_func_id) == 0 && v_func_id) {
            func_id = Void_AsFUNC_ID(v_func_id);
            return func_id;
        } else {
            PyErr_Clear();
        }
    }

    if (!func_id) {
        func_id = find_func(state, code, &key, 1);
    }
    if (!func_id) {
        func_id = register_code_object(state, code, key);
        if (state->co_extra_index >= 0) {
            if (PyUnstable_Code_SetExtra(code, state->co_extra_index, Void_FromFUNC_ID(func_id)) < 0) {
                PyErr_Clear();
            }
        }
    }
    return func_id;
}

FUNC_ID ETWCOMMON_find_or_register_callable(struct ETWCOMMON_STATE *state, PyObject *code)
{
    PyObject *key = NULL;
    FUNC_ID func_id = find_func(state, code, &key, 0);
    if (!func_id) {
        if (!PyErr_Occurred()) {
            func_id = register_callable(state, code, key);
        }
        Py_XDECREF(key);
    }
    return func_id;
}


static FUNC_ID default_new_func_id(
    struct ETWCOMMON_STATE *state,
    PyObject *key,
    const wchar_t *module,
    const wchar_t *sourcePath,
    const wchar_t *name,
    size_t lineno,
    int is_python_code
) {
    FUNC_ID func_id = ETWCOMMON_ClaimFuncId(state, key, 0);
#ifdef WITH_TRACELOGGING
    if (FUNC_ID_IS_VALID(func_id)) {
        int iLineno = (int)lineno;
        if (iLineno != lineno) {
            iLineno = -1;
        }
        WriteFunctionEvent(func_id, NULL, NULL, sourcePath ? sourcePath : module, name, iLineno, is_python_code);
    }
#endif
    return func_id;
}


int ETWCOMMON_Init(struct ETWCOMMON_STATE *state, void *owner)
{
    if (!state) {
        return 1;
    }
    state->owner = owner;
    if (!state->get_new_func_id) {
        state->get_new_func_id = &default_new_func_id;
    }
    if (!state->func_table) {
        state->func_table = PyDict_New();
        if (!state->func_table) {
            return 0;
        }
    }
    if (!state->include_prefix) {
        state->include_prefix = PyList_New(0);
        if (!state->include_prefix) {
            Py_XDECREF(state->func_table);
            state->func_table = NULL;
            return 0;
        }
    }
    if (!state->ignored_files) {
        state->ignored_files = PySet_New(NULL);
        if (!state->ignored_files) {
            Py_XDECREF(state->include_prefix);
            state->include_prefix = NULL;
            Py_XDECREF(state->func_table);
            state->func_table = NULL;
            return 0;
        }
    }
    state->next_func_id = FUNC_ID_FIRST;
    state->co_extra_index = PyUnstable_Eval_RequestCodeExtraIndex(NULL);
    return 1;
}


int ETWCOMMON_Clear(struct ETWCOMMON_STATE *state)
{
    if (state && state->func_table) {
        Py_CLEAR(state->func_table);
    }
    if (state && state->include_prefix) {
        Py_CLEAR(state->include_prefix);
    }
    if (state && state->ignored_files) {
        Py_CLEAR(state->ignored_files);
    }
    state->next_func_id = FUNC_ID_FIRST;
    state->co_extra_index = -1;
    return 1;
}

int ETWCOMMON_Visit(struct ETWCOMMON_STATE *state, visitproc visit, void *arg)
{
    Py_VISIT(state->ignored_files);
    Py_VISIT(state->include_prefix);
    Py_VISIT(state->func_table);
    return 0;
}


FUNC_ID ETWCOMMON_ClaimFuncId(struct ETWCOMMON_STATE *state, PyObject *key, FUNC_ID func_id)
{
    FUNC_ID next_func_id = state->next_func_id;
    if (!func_id) {
        func_id = next_func_id;
        next_func_id = next_func_id + 1;
    }
    PyObject *o_id = PyLong_FromFUNC_ID(func_id);
    if (!o_id) {
        return FUNC_ID_ERROR;
    }
    if (PyObject_SetItem(state->func_table, key, o_id) < 0) {
        func_id = FUNC_ID_ERROR;
    } else {
        state->next_func_id = next_func_id;
    }
    // Unclear why, but Python 3.10 doesn't like it when we decref this object
#if PY_VERSION_HEX < 0x030A0000 || PY_VERSION_HEX >= 0x030B0000
    Py_DECREF(o_id);
#endif
    return func_id;
}

#ifdef WITH_TRACELOGGING
PyObject *ETWCOMMON_write_mark(PyObject *module, PyObject *args)
{
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
    WriteCustomEvent(event_name, opcode);

    Py_RETURN_NONE;
}
#endif
