#define INITGUID
#include <Windows.h>
#include <assert.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "_tdhreader.h"

struct TraceHandle {
    EVENT_TRACE_LOGFILEW logfile;
    TRACEHANDLE handle;
    HANDLE thread;
    HANDLE ready_event;
    HANDLE done_event;
    LPWSTR path;
    TraceCallback callback;
    void *context;
    volatile bool cancelled;
    int error;
    const char *error_source;

    EVENT_RECORD *record;
    size_t info_bytes;
    TRACE_EVENT_INFO *info;

    const GUID *include_provider;
    const wchar_t * const *include_provider_name;
    const wchar_t * const *include_event_name;
    const ULONG *include_process_id;
    ULONGLONG include_all_keyword_mask;
    ULONGLONG include_any_keyword_mask;
    int include_provider_count;
    int include_provider_name_count;
    int include_event_name_count;
    int include_process_id_count;
    bool include_child_processes;
};


DEFINE_GUID( /* 3d6fa8d0-fe05-11d0-9dda-00c04fd7ba7c */
    System_ProcessEvent,
    0x3d6fa8d0, 0xfe05, 0x11d0, 0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c
);

int AddTraceProcessIdFilter(TraceHandle *handle, ULONG process_id);


static void RecordCallback(PEVENT_RECORD evt)
{
    auto ph = GetProcessHeap();
    auto t = (TraceHandle *)evt->UserContext;
    if (t->cancelled)
        return;

    int err;

    if (t->include_provider && t->include_provider_count > 0) {
        err = ERROR_NOT_FOUND;
        for (int i = 0; i < t->include_provider_count; ++i) {
            if (evt->EventHeader.ProviderId == t->include_provider[i]) {
                err = 0;
                break;
            }
        }
        if (err) {
            return;
        }
    }
    if (t->include_process_id && t->include_process_id_count > 0) {
        err = ERROR_NOT_FOUND;
        for (int i = 0; i < t->include_process_id_count; ++i) {
            if (evt->EventHeader.ProcessId == t->include_process_id[i]) {
                err = 0;
                break;
            }
        }
        if (err) {
            return;
        }
    }

    ULONG ptrsize = sizeof(void *);
    if (evt->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) {
        ptrsize = 4;
    } else if (evt->EventHeader.Flags & EVENT_HEADER_FLAG_64_BIT_HEADER) {
        ptrsize = 8;
    }

    ULONGLONG kw = evt->EventHeader.EventDescriptor.Keyword;
    ULONGLONG mask = t->include_all_keyword_mask;
    if (mask && (kw & mask) != mask) {
        return;
    }
    mask = t->include_any_keyword_mask;
    if (mask && (kw & mask) == 0) {
        return;
    }

    ULONG infoSize = 1024;
    do {
        if (t->info) {
            HeapFree(ph, 0, t->info);
        }
        t->info = (TRACE_EVENT_INFO *)HeapAlloc(ph, 0, infoSize);
        if (!t->info) {
            t->cancelled = true;
            t->error = err;
            t->error_source = "RecordCallback.HeapAlloc`1";
            return;
        }
        err = TdhGetEventInformation(evt, 0, NULL, t->info, &infoSize);
    } while (err == ERROR_INSUFFICIENT_BUFFER && infoSize);
    
    switch (err) {
    case ERROR_NOT_FOUND:
        // Schema not found, so ignore the info and just provide raw event
        infoSize = 0;
        if (t->info) {
            HeapFree(ph, 0, t->info);
            t->info = NULL;
        }
        break;
    case 0:
        break;
    default:
        t->cancelled = true;
        t->error = err;
        t->error_source = "RecordCallback.TdhGetEventInformation";
        goto error;
    }

    if (t->info && t->info->ProviderGuid == SystemTraceControlGuid) {
        if (t->info->EventGuid == System_ProcessEvent &&
            t->include_process_id &&
            t->include_child_processes &&
            (t->info->EventDescriptor.Opcode == EVENT_TRACE_TYPE_START ||
             t->info->EventDescriptor.Opcode == EVENT_TRACE_TYPE_END) &&
            evt->UserDataLength >= ptrsize + sizeof(ULONG) * 2
        ) {
            ULONG pid, ppid, add_pid = 0;
            ULONG *ud = (ULONG *)((BYTE*)evt->UserData + ptrsize);
            memcpy(&pid, &ud[0], sizeof(ULONG));
            memcpy(&ppid, &ud[1], sizeof(ULONG));
            if (t->info->EventDescriptor.Opcode == EVENT_TRACE_TYPE_START) {
                /* Start - we want events from children started by tracked processes */
                for (int i = 0; i < t->include_process_id_count; ++i) {
                    if (t->include_process_id[i] == ppid) {
                        add_pid = pid;
                        break;
                    }
                }
                if (add_pid) {
                    err = AddTraceProcessIdFilter(t, add_pid);
                    if (err) {
                        t->cancelled = true;
                        t->error = err;
                        t->error_source = "RecordCallback.AddTraceProcessIdFilter";
                        goto error;
                    }
                }
            } else  if (t->info->EventDescriptor.Opcode == EVENT_TRACE_TYPE_END) {
                /* End - stop getting events from a tracked process when it ends */
                for (int i = 0; i < t->include_process_id_count; ++i) {
                    if (t->include_process_id[i] == pid) {
                        ((ULONG *)t->include_process_id)[i] = 0;
                        break;
                    }
                }
            }
        }
    }

    if (t->include_provider_name && t->include_provider_name_count > 0) {
        err = ERROR_NOT_FOUND;
        if (t->info) {
            for (int i = 0; i < t->include_provider_name_count; ++i) {
                const wchar_t *name = (const wchar_t *)((const char *)t->info + t->info->ProviderNameOffset);
                if (t->info->ProviderNameOffset && !_wcsicmp(name, t->include_provider_name[i])) {
                    err = 0;
                    break;
                }
            }
        }
        if (err) {
            err = 0;
            goto error;
        }
    }
    if (t->include_event_name && t->include_event_name_count > 0) {
        err = ERROR_NOT_FOUND;
        if (t->info) {
            for (int i = 0; i < t->include_event_name_count; ++i) {
                const wchar_t *name = (const wchar_t *)((const char *)t->info + t->info->EventNameOffset);
                if (t->info->EventNameOffset && !_wcsicmp(name, t->include_event_name[i])) {
                    err = 0;
                    break;
                }
            }
        }
        if (err) {
            err = 0;
            goto error;
        }
    }

    t->info_bytes = infoSize;
    t->record = evt;
    SetEvent(t->ready_event);
    err = WaitForSingleObject(t->done_event, INFINITE);
    if (err) {
        t->cancelled = true;
    }
    
error:
    if (t->info) {
        HeapFree(ph, 0, t->info);
        t->info = NULL;
    }
    t->record = NULL;
}


int ReadTraceEvents(TraceHandle *handle, TraceCallback callback, void *context)
{
    int r = 1;
    int err;
    while (r > 0 && !handle->cancelled) {
        err = WaitForSingleObject(handle->ready_event, INFINITE);
        if (err) {
            if (!handle->cancelled) {
                handle->cancelled = true;
                handle->error = GetLastError();
                handle->error_source = "ReadTraceEvents.WaitForSingleObject";
            }
            r = -1;
        } else if (!handle->record) {
            r = 0;
        } else {
            TraceCallbackInfo tci;
            tci.info_bytes = (int)handle->info_bytes;
            tci.info = handle->info;
            tci.record = handle->record;
            r = callback(context, &tci);
            HeapFree(GetProcessHeap(), 0, handle->info);
            handle->info = NULL;
            SetEvent(handle->done_event);
        }
    }
    return r;
}


static ULONG BufferCallback(EVENT_TRACE_LOGFILEW *logfile)
{
    auto t = (TraceHandle *)logfile->Context;
    return !t->cancelled;
}


DWORD WINAPI EtlProcessor(LPVOID lpParam)
{
    auto handle = (TraceHandle *)lpParam;
    handle->cancelled = false;
    handle->error = 0;
    handle->error_source = NULL;
    int err = ProcessTrace(&handle->handle, 1, NULL, NULL);
    if (!err) {
        SetEvent(handle->ready_event);
    }
    CloseHandle(handle->ready_event);
    handle->ready_event = NULL;
    if (handle->cancelled) {
        err = 0;
    } else {
        handle->cancelled = true;
        handle->error = err;
        handle->error_source = "EtlProcessor.ProcessTrace";
    }
    return err;
}


int OpenEtlFile(const wchar_t *path, TraceHandle **handle)
{
    auto ph = GetProcessHeap();
    auto t = (TraceHandle *)HeapAlloc(ph, HEAP_ZERO_MEMORY, sizeof(TraceHandle));
    if (!t)
        return GetLastError();
    size_t pathlen = wcslen(path) + 1;
    t->path = (LPWSTR)HeapAlloc(ph, 0, sizeof(wchar_t) * pathlen);
    if (!t->path)
        goto error;
    wcscpy_s(t->path, pathlen, path);
    t->logfile.LogFileName = t->path;
    t->logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    t->logfile.EventRecordCallback = RecordCallback;
    t->logfile.BufferCallback = BufferCallback;
    t->logfile.Context = t;
    t->handle = OpenTraceW(&t->logfile);
    if (t->handle == INVALID_PROCESSTRACE_HANDLE)
        goto error;
    t->ready_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!t->ready_event)
        goto error;
    t->done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!t->done_event)
        goto error;
    t->thread = CreateThread(NULL, 0, EtlProcessor, t, 0, NULL);
    if (!t->thread)
        goto error;
    *handle = t;
    return 0;
error:
    int err = GetLastError();
    if (t->handle && t->handle != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(t->handle);
        t->handle = NULL;
    }
    if (t->ready_event) {
        CloseHandle(t->ready_event);
        t->ready_event = NULL;
    }
    if (t->done_event) {
        CloseHandle(t->done_event);
        t->done_event = NULL;
    }
    if (t->thread) {
        if (WaitForSingleObject(t->thread, 1000) == WAIT_TIMEOUT) {
            TerminateThread(t->thread, WAIT_TIMEOUT);
        }
        CloseHandle(t->thread);
        t->thread = NULL;
    }
    if (t->path) {
        HeapFree(ph, 0, t->path);
        t->path = NULL;
    }
    HeapFree(ph, 0, t);
    return err;
}

static int CopyValueArray(const void **dest, int *destCount, const void *src, int count, int stride)
{
    auto ph = GetProcessHeap();
    if (*dest) {
        HeapFree(ph, 0, (LPVOID)*dest);
        *dest = NULL;
        *destCount = 0;
    }
    if (src && count > 0) {
        void *p = HeapAlloc(ph, 0, stride * count);
        if (!p)
            return GetLastError();
        memcpy(p, src, stride * count);
        *dest = p;
        *destCount = count;
    }
    return 0;
}

int SetTraceProviderFilter(TraceHandle *handle, const GUID *providers, int count)
{
    return CopyValueArray(
        (const void**)&handle->include_provider,
        &handle->include_provider_count,
        providers,
        count,
        sizeof(GUID)
    );
}


int SetTraceProcessIdFilter(TraceHandle *handle, const ULONG *process_ids, int count)
{
    return CopyValueArray(
        (const void**)&handle->include_process_id,
        &handle->include_process_id_count,
        process_ids,
        count,
        sizeof(GUID)
    );
}


int AddTraceProcessIdFilter(TraceHandle *handle, ULONG process_id)
{
    int free = -1;
    for (int i = 0; i < handle->include_process_id_count; ++i) {
        if (handle->include_process_id[i] == process_id) {
            return 0;
        }
        if (!handle->include_process_id[i] && free < 0) {
            free = i;
        }
    }
    if (free < 0) {
        auto ph = GetProcessHeap();
        free = handle->include_process_id_count;
        handle->include_process_id_count = free + 1;
        handle->include_process_id = (const ULONG *)HeapReAlloc(ph, 0,
            (LPVOID)handle->include_process_id,
            sizeof(ULONG) * handle->include_process_id_count
        );
        if (!handle->include_process_id) {
            handle->include_process_id_count = 0;
            return GetLastError();
        }
    }
    ((ULONG *)handle->include_process_id)[free] = process_id;
    return 0;
}


int SetTraceProcessIdChildrenFilter(TraceHandle *handle, int trace_children)
{
    handle->include_child_processes = trace_children != 0;
    return 0;
}


static int CopyWcharArray(const wchar_t * const **dest, int *destCount, const wchar_t * const *src, int count)
{
    auto ph = GetProcessHeap();
    if (*dest) {
        for (int i = 0; i < *destCount; ++i) {
            HeapFree(ph, 0, (LPVOID)(*dest[i]));
        }
        HeapFree(ph, 0, (LPVOID)*dest);
        *dest = NULL;
        *destCount = 0;
    }
    if (src && count > 0) {
        const wchar_t **p_array = (const wchar_t **)HeapAlloc(ph, 0, sizeof(const wchar_t *) * count);
        if (!p_array)
            return GetLastError();
        for (int i = 0; i < count; ++i) {
            size_t len = wcslen(src[i]) + 1;
            wchar_t *p = (wchar_t *)HeapAlloc(ph, 0, sizeof(wchar_t) * len);
            if (!p) {
                int err = GetLastError();
                while (--i >= 0) {
                    HeapFree(ph, 0, (LPVOID)p_array[i]);
                }
                HeapFree(ph, 0, (LPVOID)p_array);
                return err;
            }
            wcscpy_s(p, len, src[i]);
            p_array[i] = p;
        }
        *dest = p_array;
        *destCount = count;
    }
    return 0;
}


int SetTraceProviderNameFilter(TraceHandle *handle, const wchar_t * const *providers, int count)
{
    return CopyWcharArray(
        &handle->include_provider_name,
        &handle->include_provider_name_count,
        providers,
        count
    );
}


int SetTraceEventNameFilter(TraceHandle *handle, const wchar_t * const *events, int count)
{
    return CopyWcharArray(
        &handle->include_event_name,
        &handle->include_event_name_count,
        events,
        count
    );
}


int SetTraceKeywordFilter(TraceHandle *handle, ULONGLONG allMask, ULONGLONG anyMask)
{
    handle->include_all_keyword_mask = allMask;
    handle->include_any_keyword_mask = anyMask;
    return 0;
}


int CancelReadTrace(TraceHandle *handle)
{
    handle->cancelled = true;
    CloseHandle(handle->ready_event);
    CloseHandle(handle->done_event);
    handle->ready_event = NULL;
    handle->done_event = NULL;
    return 0;
}


int CloseTraceHandle(TraceHandle *handle, int *error, const char **error_source)
{
    auto ph = GetProcessHeap();
    if (!handle->cancelled) {
        CancelReadTrace(handle);
    }
    if (handle->ready_event) {
        CloseHandle(handle->ready_event);
        handle->ready_event = NULL;
    }
    if (handle->done_event) {
        CloseHandle(handle->done_event);
        handle->done_event = NULL;
    }
    if (handle->thread) {
        if (WaitForSingleObject(handle->thread, 1000) == WAIT_TIMEOUT) {
            TerminateThread(handle->thread, WAIT_TIMEOUT);
        }
        CloseHandle(handle->thread);
        handle->thread = NULL;
    }
    if (handle->handle) {
        CloseTrace(handle->handle);
        handle->handle = NULL;
    }
    if (handle->path) {
        HeapFree(ph, 0, handle->path);
        handle->path = NULL;
    }
    SetTraceProviderFilter(handle, NULL, 0);
    SetTraceProviderNameFilter(handle, NULL, 0);
    SetTraceEventNameFilter(handle, NULL, 0);
    SetTraceProcessIdFilter(handle, NULL, 0);

    if (error) {
        *error = handle->error;
        if (error_source) {
            *error_source = handle->error_source;
        }
    }

    HeapFree(ph, 0, handle);
    return 0;
}
