#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct TraceHandle;
struct TraceCallbackInfo {
    int info_bytes;
    TRACE_EVENT_INFO *info;
    EVENT_RECORD *record;
};

#ifndef __cplusplus
typedef struct TraceHandle TraceHandle;
typedef struct TraceCallbackInfo TraceCallbackInfo;
#endif


typedef int (__stdcall *TraceCallback)(void *context, TraceCallbackInfo *info);

int OpenEtlFile(const wchar_t *path, TraceHandle **handle);
int ReadTraceEvents(TraceHandle *handle, TraceCallback callback, void *context);
int CancelReadTrace(TraceHandle *handle);
int CloseTraceHandle(TraceHandle *handle, int *error, const char **error_source);
int SetTraceProviderFilter(TraceHandle *handle, const GUID *providers, int count);
int SetTraceProviderNameFilter(TraceHandle *handle, const wchar_t * const *providers, int count);
int SetTraceEventNameFilter(TraceHandle *handle, const wchar_t * const *providers, int count);
int SetTraceProcessIdFilter(TraceHandle *handle, const ULONG *process_ids, int count);
int AddTraceProcessIdFilter(TraceHandle *handle, ULONG process_id);
int SetTraceProcessIdChildrenFilter(TraceHandle *handle, int trace_children);
int SetTraceKeywordFilter(TraceHandle *handle, ULONGLONG allMask, ULONGLONG anyMask);

#ifdef __cplusplus
}
#endif
