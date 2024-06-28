#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void GetProviderGuid(GUID *provider);
int Register();
int Unregister();

void WriteBeginThread(int thread_id);
void WriteEndThread(int thread_id);

void WriteEvalFunctionEvent(void *dll_handle);

void WriteFunctionEvent(
    FUNC_ID func_id,
    void *begin_addr,
    void *end_addr,
    LPCWSTR source_file,
    LPCWSTR name,
    int line_no,
    int is_python_code
);

void WriteFunctionPush(FUNC_ID from_func_id, size_t from_line, FUNC_ID to_func_id);
void WriteFunctionPop(FUNC_ID func_id);
void WriteCustomEvent(LPCWSTR name, int opcode);

#ifdef __cplusplus
}
#endif
