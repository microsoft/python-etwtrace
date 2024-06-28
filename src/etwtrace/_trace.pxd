cdef extern from "Python.h":
    int PyTrace_CALL
    int PyTrace_RETURN
    int PyTrace_C_CALL
    int PyTrace_C_RETURN
    int PyTrace_C_EXCEPTION

    ctypedef struct PyInterpreterState:
        pass

    PyInterpreterState* PyInterpreterState_Get()


ctypedef int (__cdecl *Py_tracefunc)(object obj, object frame, int what, object arg) except -1
ctypedef void (*PPyEval_SetProfile)(Py_tracefunc func, object obj)


cdef extern from "Windows.h" nogil:
    ctypedef struct GUID:
        pass
    ctypedef void* HMODULE
    ctypedef unsigned short WCHAR
    ctypedef const WCHAR* LPCWSTR

    HMODULE LoadLibraryW(LPCWSTR module)
    void FreeLibrary(HMODULE module)
    void *GetProcAddress(HMODULE module, const char *name)
    int GetLastError()


cdef inline void PyEval_SetProfile(Py_tracefunc func, obj):
    import sys
    cdef HMODULE h_python_dll = <HMODULE><size_t>sys.dllhandle
    cdef PPyEval_SetProfile fn

    with nogil:
        fn = <PPyEval_SetProfile>GetProcAddress(h_python_dll, "PyEval_SetProfile")
    if not fn:
        raise OSError(None, None, None, GetLastError())

    fn(func, obj)


cdef extern from "src/etwtrace/_trace.h" nogil:
    int Register()
    int Unregister()
    void WriteBeginThread(int thread_id)
    void WriteEndThread(int thread_id)
    void WriteEvalFunctionEvent(void *dll_handle)
    void WriteFunctionEvent(
        void *func_id,
        void *begin_addr,
        void *end_addr,
        const char *source_file,
        const char *name,
        int line_no,
        bint is_python_code,
    )
    void WriteCallEvent(void *func_id)
    void WriteReturnEvent(void *func_id)
    void WriteCustomStartEvent(const char *name)
    void WriteCustomStopEvent(const char *name)
    void GetProviderGuid(GUID *provider)
