#include <Windows.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>

#include "_func_id.h"
#include "_trace.h"

#define PYTHON_ETW_GUID \
    0x99A10640, 0x320D, 0x4B37, 0x9E, 0x26, 0xC3, 0x11, 0xD8, 0x6D, 0xA7, 0xAB

TRACELOGGING_DEFINE_PROVIDER(PythonProvider, "Python", (PYTHON_ETW_GUID));

enum PythonETWKeywords {
    PYTHON_KEYWORD_THREAD = 0x100,
    PYTHON_KEYWORD_STACK = 0x200,
    PYTHON_KEYWORD_FUNCTION = 0x400,
    PYTHON_KEYWORD_MARK = 0x800,
    PYTHON_KEYWORD_FUNCTION_PUSH = 0x1000,
    PYTHON_KEYWORD_FUNCTION_POP = 0x2000
};


void GetProviderGuid(GUID *provider) {
    GUID g = {PYTHON_ETW_GUID};
    *provider = g;
}


static int register_count = 0;

int Register() {
    if (register_count == 0) {
        TraceLoggingRegister(PythonProvider);
    }
    return ++register_count;
}

int Unregister() {
    if (register_count == 1) {
        TraceLoggingUnregister(PythonProvider);
    }
    return --register_count;
}


void WriteEvalFunctionEvent(void *dll_handle) {
    typedef void* (*PPyInterpreterState_Get)(void);
    typedef void* (*P_PyInterpreterState_GetEvalFrameFunc)(void *interp);
    auto dll = (HMODULE)dll_handle;

    auto PyInterpreterState_Get = (PPyInterpreterState_Get)GetProcAddress(dll, "PyInterpreterState_Get");
    auto _PyInterpreterState_GetEvalFrameFunc =
        (P_PyInterpreterState_GetEvalFrameFunc)GetProcAddress(dll, "_PyInterpreterState_GetEvalFrameFunc");

    void *eval_func = NULL;
    if (PyInterpreterState_Get && _PyInterpreterState_GetEvalFrameFunc) {
        void *interp_state = PyInterpreterState_Get();
        eval_func = _PyInterpreterState_GetEvalFrameFunc(interp_state);
    }
    if (!eval_func) {
        eval_func = GetProcAddress(dll, "_PyEval_EvalFrameDefault");
    }

    TraceLoggingWrite(
        PythonProvider,
        "PythonEvalFunction",
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
        TraceLoggingValue(eval_func, "BeginAddress")
    );
}


void WriteBeginThread(int thread_id) {
    TraceLoggingWrite(
        PythonProvider,
        "PythonThread",
        TraceLoggingLevel(WINEVENT_LEVEL_INFO),
        TraceLoggingKeyword(PYTHON_KEYWORD_THREAD),
        TraceLoggingOpcode(WINEVENT_OPCODE_START),
        TraceLoggingValue(thread_id, "ThreadID")
    );
}

void WriteEndThread(int thread_id) {
    TraceLoggingWrite(
        PythonProvider,
        "PythonThread",
        TraceLoggingLevel(WINEVENT_LEVEL_INFO),
        TraceLoggingKeyword(PYTHON_KEYWORD_THREAD),
        TraceLoggingOpcode(WINEVENT_OPCODE_STOP),
        TraceLoggingValue(thread_id, "ThreadID")
    );
}

void WriteFunctionEvent(
    FUNC_ID func_id,
    void *begin_addr,
    void *end_addr,
    LPCWSTR source_file,
    LPCWSTR name,
    int line_no,
    int is_python_code
) {
    TraceLoggingWrite(
        PythonProvider,
        "PythonFunction",
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
        TraceLoggingKeyword(PYTHON_KEYWORD_FUNCTION),
        TraceLoggingValue(Void_FromFUNC_ID(func_id), "FunctionID"),
        TraceLoggingValue(begin_addr, "BeginAddress"),
        TraceLoggingValue(end_addr, "EndAddress"),
        TraceLoggingValue(line_no, "LineNumber"),
        TraceLoggingValue(source_file, "SourceFile"),
        TraceLoggingValue(name, "Name"),
        TraceLoggingValue(is_python_code, "IsPythonCode")
    );
}


void WriteCustomEvent(const wchar_t *name, int opcode) {
    switch (opcode) {
    case 0:
        TraceLoggingWrite(
            PythonProvider,
            "PythonMark",
            TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
            TraceLoggingKeyword(PYTHON_KEYWORD_MARK),
            TraceLoggingValue(name, "Mark")
        );
        break;
    case 1:
        TraceLoggingWrite(
            PythonProvider,
            "PythonMark",
            TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
            TraceLoggingKeyword(PYTHON_KEYWORD_MARK),
            TraceLoggingOpcode(WINEVENT_OPCODE_START),
            TraceLoggingValue(name, "Mark")
        );
        break;
    case 2:
        TraceLoggingWrite(
            PythonProvider,
            "PythonMark",
            TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
            TraceLoggingKeyword(PYTHON_KEYWORD_MARK),
            TraceLoggingOpcode(WINEVENT_OPCODE_STOP),
            TraceLoggingValue(name, "Mark")
        );
        break;
    case 3:
        TraceLoggingWrite(
            PythonProvider,
            "PythonStackSample",
            TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
            TraceLoggingKeyword(PYTHON_KEYWORD_STACK),
            TraceLoggingValue(name, "Mark")
        );
        break;
    }
}


void WriteFunctionPush(FUNC_ID from_func_id, size_t from_func_line, FUNC_ID func_id) {
    TraceLoggingWrite(
        PythonProvider,
        "PythonFunctionPush",
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
        TraceLoggingKeyword(PYTHON_KEYWORD_FUNCTION_PUSH),
        TraceLoggingValue(Void_FromFUNC_ID(func_id), "FunctionID"),
        TraceLoggingValue(Void_FromFUNC_ID(from_func_id), "Caller"),
        TraceLoggingValue(from_func_line, "CallerLine")
    );
}

void WriteFunctionPop(FUNC_ID func_id) {
    TraceLoggingWrite(
        PythonProvider,
        "PythonFunctionPop",
        TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
        TraceLoggingKeyword(PYTHON_KEYWORD_FUNCTION_POP),
        TraceLoggingValue(Void_FromFUNC_ID(func_id), "FunctionID")
    );
}
