# cython: language_level=3, language=c, binding=True, embedsignature=True, c_string_encoding=ascii

from ._windows cimport *

cdef extern from "src/etwtrace/_tdhreader.h" nogil:
    ctypedef struct TraceHandle:
        pass

    ctypedef struct TraceCallbackInfo:
        int info_bytes
        TRACE_EVENT_INFO *info
        EVENT_RECORD *record

    ctypedef int (__stdcall *TraceCallback)(void *context, TraceCallbackInfo *info) nogil

    int OpenEtlFile(const wchar_t *path, TraceHandle **handle)
    int ReadTraceEvents(TraceHandle *handle, TraceCallback callback, void *context)
    int CancelReadTrace(TraceHandle *handle)
    int CloseTraceHandle(TraceHandle *handle, int *error, const char **error_source)
    int SetTraceProviderFilter(TraceHandle *handle, const GUID *providers, int count)
    int SetTraceProviderNameFilter(TraceHandle *handle, const wchar_t * const *providers, int count)
    int SetTraceEventNameFilter(TraceHandle *handle, const wchar_t * const *providers, int count)
    int SetTraceProcessIdFilter(TraceHandle *handle, const ULONG *process_ids, int count)
    int AddTraceProcessIdFilter(TraceHandle *handle, const ULONG process_id)
    int SetTraceProcessIdChildrenFilter(TraceHandle *handle, int trace_children)
    int SetTraceKeywordFilter(TraceHandle *handle, ULONGLONG allMask, ULONGLONG anyMask)


cdef winerror(int err, const char *dll):
    cdef HMODULE hModule = NULL
    cdef DWORD result
    cdef DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM
    cdef PWSTR message = NULL
    if not err:
        err = GetLastError()
    with nogil:
        if dll:
            hModule = GetModuleHandleA(dll)
            if hModule:
                flags = FORMAT_MESSAGE_FROM_HMODULE
        result = FormatMessageW(
            flags | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
            hModule,
            err,
            LANG_USER_DEFAULT,
            <PWSTR>&message,
            0,
            NULL
        )
    if result and message:
        m = (<char *>message)[:result * 2].decode('utf-16-le').rstrip('\0')
        exc = OSError(None, m, None, err)
        with nogil:
            LocalFree(message)
    else:
        exc = OSError(None, f"Error 0x{err:08X} (failed to retrieve message)", None, err)
    return exc


import os
import uuid

cdef object SYSTRACE_GUID = uuid.UUID('9e814aad-3204-11d2-9a82-006008a86939')
cdef bytes STACKWALK_GUID = uuid.UUID('def2fe46-7bd6-4b80-bd94-f57fe20d0ce3').bytes_le

cdef object PROCESS_EVENT_ID = uuid.UUID('3d6fa8d0-fe05-11d0-9dda-00c04fd7ba7c')
cdef object PERFINFO_EVENT_ID = uuid.UUID('ce1dbfb4-137e-4da6-87b0-3f59aa102cbc')


cdef class EventData:
    cdef readonly object provider
    cdef readonly int id
    cdef readonly int version
    cdef readonly int channel
    cdef readonly int level
    cdef readonly int opcode
    cdef readonly int task
    cdef readonly int keyword

    cdef readonly int thread_id
    cdef readonly int process_id

    cdef readonly str provider_name
    cdef readonly str channel_name
    cdef readonly str level_name
    cdef readonly str opcode_name
    cdef readonly str task_name
    cdef readonly str keyword_names

    cdef readonly str event_name
    cdef readonly object event_uuid
    cdef readonly str event_message
    cdef readonly str provider_message

    cdef readonly object stack

    cdef readonly dict _properties
    cdef int _property_count

    def __init__(self):
        self.provider = None
        self.provider_name = None
        self.channel_name = None
        self.level_name = None
        self.opcode_name = None
        self.task_name = None
        self.keyword_names = None
        self.event_name = None
        self.event_uuid = None
        self.event_message = None
        self.provider_message = None
        self._properties = {}
        self.stack = None

    def __repr__(self):
        ev = self.event_name or self.opcode_name or self.task_name
        msg = f", {self.event_message!r}" if self.event_message else ""
        return f"<EventData({self.provider_name!r}, {self.id}, {ev!r}{msg})>"

    def __getitem__(self, index):
        if isinstance(index, int):
            if index < 0:
                index += self._property_count
            if index >= self._property_count:
                raise IndexError()
        return self._properties[index]

    def __len__(self):
        return self._property_count

    def __iter__(self):
        for i in range(len(self)):
            yield self._properties[i].name

    def items(self):
        for i in range(len(self)):
            p = self._properties[i]
            yield p.name, p.value

    # A few well-known events

    @property
    def is_process_start(self):
        return (self.provider == SYSTRACE_GUID
            and self.event_uuid == PROCESS_EVENT_ID
            and self.opcode == EVENT_TRACE_TYPE_START)

    @property
    def is_process_end(self):
        return (self.provider == SYSTRACE_GUID
            and self.event_uuid == PROCESS_EVENT_ID
            and self.opcode == EVENT_TRACE_TYPE_END)

    @property
    def is_stack_sample(self):
        """Returns True for stack sample events.

        Note that these do not include stacks yet! They are complex to parse"""
        return (self.provider == SYSTRACE_GUID
            and self.event_uuid == PERFINFO_EVENT_ID
            and self.opcode == 46)


cdef class EventPropertyData:
    cdef readonly str name
    cdef readonly int flags
    cdef readonly object value
    cdef object fmt

    def __init__(self):
        self.fmt = lambda v: v

    @property
    def formatted_value(self):
        return self.fmt(self.value)

    def __repr__(self):
        return f"<EventPropertyData({self.name!r}, {self.formatted_value!r})>"

    def __str__(self):
        return f"{self.name}={self.formatted_value}"


cdef class ReadContext:
    cdef list buffer
    cdef int limit
    cdef dict memo
    cdef object exception

    def __init__(self, int limit, dict memo):
        self.limit = limit
        self.buffer = []
        self.memo = memo
        self.exception = None

    cdef str read_str(self, void *base, size_t length, size_t offset):
        if offset >= length:
            raise IndexError()
        if offset == 0:
            return ""
        cdef const wchar_t *p_str = <const wchar_t*>(<unsigned char*>base + offset)
        n = wcsnlen(p_str, (length - offset) // sizeof(wchar_t))
        s = ((<unsigned char*>p_str)[:n * sizeof(wchar_t)]).decode('utf-16-le', 'replace')
        return self.memo.setdefault(s, s)


cdef class UserDataReader:
    cdef BYTE *_base
    cdef USHORT _off
    cdef USHORT _len

    def __cinit__(self):
        self._base = NULL
        self._off = 0
        self._len = 0

    cdef read(self, size_t n):
        n = min(n, self._len - self._off)
        b = bytes((self._base + self._off)[:n])
        self._off += n
        return b

    cdef read_to_double_nul(self):
        n = wcsnlen(
            <const WCHAR*>(self._base + self._off),
            (self._len - self._off) // sizeof(WCHAR)
        ) * sizeof(WCHAR) + sizeof(WCHAR)
        return self.read(n)[:-<int>sizeof(WCHAR)]

    cdef read_to_nul(self):
        n = strnlen(<const char*>self._base + self._off, self._len - self._off) + 1
        return self.read(n)[:-1]

    cdef read_SID(self):
        if not IsValidSid(<SID*>(self._base + self._off)):
            raise TypeError("not a valid SID")
        n = GetLengthSid(<SID*>(self._base + self._off))
        return self.read(n)

    cdef get_ptr(self, BYTE **p, USHORT *length):
        p[0] = self._base + self._off
        length[0] = self._len - self._off


cdef UserDataReader UserDataReader_new(void *base, USHORT length):
    self = UserDataReader()
    self._base = <BYTE *>base
    self._len = length
    return self


cdef object FormatPropertyValue(TraceCallbackInfo *info, EVENT_PROPERTY_INFO *p, UserDataReader userdata, int ptrsize):
    cdef int err

    cdef bytearray _mapbuffer
    cdef EVENT_MAP_INFO *map = NULL
    cdef ULONG map_bytes

    if p.nonStructType.MapNameOffset:
        map_bytes = 1024
        err = ERROR_INSUFFICIENT_BUFFER
        while err == ERROR_INSUFFICIENT_BUFFER:
            _mapbuffer = bytearray(map_bytes)
            map = <EVENT_MAP_INFO *><unsigned char *>_mapbuffer
            with nogil:
                err = TdhGetEventMapInformation(
                    info.record,
                    <PWSTR>(<BYTE *>(info.info) + p.nonStructType.MapNameOffset),
                    map,
                    &map_bytes
                )
        if err:
            raise winerror(err, NULL)

    cdef bytearray _buffer
    cdef WCHAR *buffer = NULL
    cdef ULONG buffer_bytes = 0

    cdef BYTE *ud
    cdef USHORT ud_len
    cdef USHORT ud_read = 0
    userdata.get_ptr(&ud, &ud_len)

    buffer_bytes = 128
    err = ERROR_INSUFFICIENT_BUFFER
    while err == ERROR_INSUFFICIENT_BUFFER:
        _buffer = bytearray(buffer_bytes)
        buffer = <WCHAR*><unsigned char*>_buffer
        with nogil:
            err = TdhFormatProperty(
                info.info, map, ptrsize,
                p.nonStructType.InType, p.nonStructType.OutType, p.length,
                ud_len, ud, &buffer_bytes, buffer, &ud_read
            )
            if map and err == ERROR_EVT_INVALID_EVENT_DATA:
                err = TdhFormatProperty(
                    info.info, NULL, ptrsize,
                    p.nonStructType.InType, p.nonStructType.OutType, p.length,
                    ud_len, ud, &buffer_bytes, buffer, &ud_read
                )
    if err:
        raise winerror(err, NULL)

    userdata.read(ud_read)

    return _buffer.decode('utf-16-le', 'replace').strip('\0\uFEFF')


cdef object ReadPropertyValue(dict properties, EVENT_PROPERTY_INFO *p, UserDataReader userdata, int ptrsize):
    if p.nonStructType.MapNameOffset:
        raise TypeError()

    in_type = p.nonStructType.InType

    if in_type == TDH_INTYPE_NULL:
        return None

    if p.Flags & PropertyParamCount:
        count = properties[p.countPropertyIndex].value
    else:
        count = p.count

    if p.Flags & PropertyParamLength:
        length = properties[p.lengthPropertyIndex].value
    else:
        length = p.length

    r = [_Read1PropertyValue(in_type, length, ptrsize, userdata) for _ in range(count)]
    if count == 1 and not (p.Flags & PropertyParamFixedCount):
        return r[0]
    return r


cdef object _Read1PropertyValue(int in_type, int length, int ptrsize, UserDataReader userdata):
    cdef float float_val
    cdef double double_val

    n = {
        TDH_INTYPE_INT8: 1,
        TDH_INTYPE_INT16: 2,
        TDH_INTYPE_INT32: 4,
        TDH_INTYPE_HEXINT32: 4,
        TDH_INTYPE_INT64: 8,
        TDH_INTYPE_HEXINT64: 8,
    }.get(in_type)
    if n:
        b = userdata.read(n)
        if b and b[-1] & 0x80:
            return -int.from_bytes(((~x & 0xFF) for x in b), 'little') - 1
        return int.from_bytes(b, 'little')

    n = {
        TDH_INTYPE_UINT8: 1,
        TDH_INTYPE_UINT16: 2,
        TDH_INTYPE_UINT32: 4,
        TDH_INTYPE_UINT64: 8,
        TDH_INTYPE_POINTER: ptrsize,
    }.get(in_type)
    if n:
        return int.from_bytes(userdata.read(n), 'little')

    if in_type == TDH_INTYPE_FLOAT:
        b = userdata.read(sizeof(float_val))
        memcpy(&float_val, <unsigned char*>b, sizeof(float_val))
        return float_val
    if in_type == TDH_INTYPE_DOUBLE:
        b = userdata.read(sizeof(double_val))
        memcpy(&double_val, <unsigned char*>b, sizeof(double_val))
        return double_val

    if in_type == TDH_INTYPE_UNICODESTRING:
        if not length:
            return userdata.read_to_double_nul().decode('utf-16-le', 'replace').removeprefix('\ufeff')
        return userdata.read(length).decode('utf-16-le', 'replace').removeprefix('\ufeff')

    if in_type == TDH_INTYPE_UNICODECHAR:
        return userdata.read(sizeof(WCHAR)).decode('utf-16-le', 'replace')

    if in_type == TDH_INTYPE_ANSISTRING:
        if not length:
            return userdata.read_to_nul().decode('utf-8-sig', 'replace')
        return userdata.read(length).decode('utf-8-sig', 'replace')

    if in_type == TDH_INTYPE_ANSICHAR:
        return userdata.read(sizeof(WCHAR)).decode('utf-16-le', 'replace')

    if in_type == TDH_INTYPE_GUID:
        return uuid.UUID(bytes_le=userdata.read(16))

    if in_type == TDH_INTYPE_WBEMSID:
        # WBEM SID is:
        #   TOKEN_USER ( sizeof(TOKEN_USER) == 2 * sizeof(void *) )
        #   SID
        userdata.read(2 * ptrsize)
        return userdata.read_SID()

    raise TypeError(f"Could not decode {in_type}")


cdef object ReadStack32(void *ptr, int cbData):
    n = (cbData - sizeof(ULONG64)) // sizeof(ULONG)
    p = (<EVENT_EXTENDED_ITEM_STACK_TRACE32*>ptr).Address
    r = []
    for i in range(n):
        r.append(p[i])
    return r

cdef object ReadStack64(void *ptr, int cbData):
    n = (cbData - sizeof(ULONG64)) // sizeof(ULONG64)
    p = (<EVENT_EXTENDED_ITEM_STACK_TRACE64*>ptr).Address
    r = []
    for i in range(n):
        r.append(p[i])
    return r


cdef dict _formatters = {
    TDH_INTYPE_HEXINT32: lambda v: f'0x{v:08X}',
    TDH_INTYPE_HEXINT64: lambda v: f'0x{v>>32:08X}_{v&0xFFFFFFFF:08X}',
    TDH_INTYPE_GUID: lambda v: f'{v!s}',
}


cdef object ReadEventTraceInfo(ReadContext ctxt, TraceCallbackInfo *info):
    ed = EventData()

    cdef TRACE_EVENT_INFO *evt = info.info
    cdef int evt_bytes = info.info_bytes
    cdef EVENT_RECORD *record = info.record

    ed.provider = uuid.UUID(bytes_le=(<unsigned char*>&evt.ProviderGuid)[:sizeof(GUID)])
    ed.event_uuid = uuid.UUID(bytes_le=(<unsigned char*>&evt.EventGuid)[:sizeof(GUID)])
    ed.id = evt.EventDescriptor.Id
    ed.version = evt.EventDescriptor.Version
    ed.channel = evt.EventDescriptor.Channel
    ed.level = evt.EventDescriptor.Level
    ed.opcode = evt.EventDescriptor.Opcode
    ed.task = evt.EventDescriptor.Task
    ed.keyword = evt.EventDescriptor.Keyword
    ed.process_id = record.EventHeader.ProcessId
    ed.thread_id = record.EventHeader.ThreadId

    ed.provider_name = ctxt.read_str(evt, evt_bytes, evt.ProviderNameOffset)
    ed.channel_name = ctxt.read_str(evt, evt_bytes, evt.ChannelNameOffset)
    ed.level_name = ctxt.read_str(evt, evt_bytes, evt.LevelNameOffset)
    ed.opcode_name = ctxt.read_str(evt, evt_bytes, evt.OpcodeNameOffset)
    ed.task_name = ctxt.read_str(evt, evt_bytes, evt.TaskNameOffset)
    ed.keyword_names = ctxt.read_str(evt, evt_bytes, evt.KeywordsNameOffset)
    ed.event_name = ctxt.read_str(evt, evt_bytes, evt.EventNameOffset)
    ed.event_message = ctxt.read_str(evt, evt_bytes, evt.EventMessageOffset)
    ed.provider_message = ctxt.read_str(evt, evt_bytes, evt.ProviderMessageOffset)

    cdef ULONG ptrsize = sizeof(void *)
    if info.record.EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER:
        ptrsize = 4
    elif info.record.EventHeader.Flags & EVENT_HEADER_FLAG_64_BIT_HEADER:
        ptrsize = 8

    userdata = UserDataReader_new(info.record.UserData, info.record.UserDataLength)

    ed._property_count = evt.TopLevelPropertyCount

    for i in range(evt.TopLevelPropertyCount):
        p = &evt.EventPropertyInfoArray[i]
        ep = EventPropertyData()
        ep.flags = p.Flags
        ep.name = ctxt.read_str(evt, evt_bytes, p.NameOffset)
        ed._properties[ep.name] = ed._properties[i] = ep

        if p.Flags & PropertyStruct:
            continue

        inType = p.nonStructType.InType
        if inType == TDH_INTYPE_POINTER:
            inType = TDH_INTYPE_HEXINT32 if ptrsize == 4 else TDH_INTYPE_HEXINT64
        try:
            ep.fmt = _formatters[inType]
        except LookupError:
            pass

        ep.value = evt.DecodingSource
        if evt.DecodingSource in (DecodingSourceXMLFile, DecodingSourceTlg, DecodingSourceWbem):
            try:
                ep.value = ReadPropertyValue(ed._properties, p, userdata, ptrsize)
                continue
            except TypeError:
                if evt.DecodingSource == DecodingSourceWbem:
                    raise
            ep.value = FormatPropertyValue(info, p, userdata, ptrsize)

    # Special-case for stack traces
    if ed.provider == SYSTRACE_GUID and not memcmp(<char *>STACKWALK_GUID, &evt.EventGuid, sizeof(GUID)):
        ed.stack = []
        for i in range(3, ed._property_count):
            ed.stack.append(ed._properties[i].value)

        return ed

    for i in range(record.ExtendedDataCount):
        d = &record.ExtendedData[i]
        if d.ExtType == EVENT_HEADER_EXT_TYPE_STACK_TRACE32:
            ed.stack = ReadStack32(<void *>d.DataPtr, d.DataSize)
        elif d.ExtType == EVENT_HEADER_EXT_TYPE_STACK_TRACE64:
            ed.stack = ReadStack64(<void *>d.DataPtr, d.DataSize)

    return ed


cdef object ReadEventRecord(ReadContext ctxt, EVENT_RECORD *record):
    ed = EventData()

    cdef EVENT_HEADER *evt = &record.EventHeader
    cdef size_t n

    ed.provider = uuid.UUID(bytes_le=(<unsigned char*>&evt.ProviderId)[:sizeof(GUID)])
    ed.id = evt.EventDescriptor.Id
    ed.version = evt.EventDescriptor.Version
    ed.channel = evt.EventDescriptor.Channel
    ed.level = evt.EventDescriptor.Level
    ed.opcode = evt.EventDescriptor.Opcode
    ed.task = evt.EventDescriptor.Task
    ed.keyword = evt.EventDescriptor.Keyword
    ed.thread_id = evt.ThreadId
    ed.process_id = evt.ProcessId

    ed.task = evt.EventProperty

    if evt.Flags & EVENT_HEADER_FLAG_STRING_ONLY:
        n = evt.Size - sizeof(EVENT_RECORD) + sizeof(void *) + sizeof (void *)
        n = wcsnlen(<WCHAR *>record.UserData, n // sizeof(WCHAR)) * sizeof(WCHAR)
        ed.event_message = (<BYTE *>record.UserData)[:n].decode('utf-16-le', 'replace')

    for i in range(record.ExtendedDataCount):
        d = &record.ExtendedData[i]
        if d.ExtType == EVENT_HEADER_EXT_TYPE_STACK_TRACE32:
            ed.stack = ReadStack32(<void *>d.DataPtr, d.DataSize)
        elif d.ExtType == EVENT_HEADER_EXT_TYPE_STACK_TRACE64:
            ed.stack = ReadStack64(<void *>d.DataPtr, d.DataSize)

    return ed


cdef int _EtlReader_Event(void *context, TraceCallbackInfo *info) noexcept:
    ctxt = <ReadContext><PyObject *>context
    try:
        if info.info:
            ctxt.buffer.append(ReadEventTraceInfo(ctxt, info))
        else:
            ctxt.buffer.append(ReadEventRecord(ctxt, info.record))
        return 1 if len(ctxt.buffer) < ctxt.limit else 0
    except BaseException as ex:
        ctxt.exception = ex
        return -1


cdef int __stdcall _EtlReader_Event_nogil(void *context, TraceCallbackInfo *info) noexcept nogil:
    with gil:
        return _EtlReader_Event(context, info)


cdef class EtlReader:
    cdef TraceHandle *handle
    cdef dict _memo

    def __cinit__(self):
        self.handle = NULL

    def __init__(
        self,
        path,
        *,
        providers=[],
        provider_names=[],
        event_names=[],
        process_ids=[],
        include_child_process_ids=True,
        keyword_mask_all=0,
        keyword_mask_any=0,
    ):
        path = os.fsdecode(path).encode('utf-16-le') + b'\0\0'
        cdef const wchar_t * path_ = <const wchar_t *><unsigned char*>path
        self._memo = {}
        cdef int err
        with nogil:
            err = OpenEtlFile(path_, &self.handle)
        if err:
            raise winerror(err, NULL)

        cdef GUID *pointers_1
        if providers:
            byte_objects = [p.bytes_le for p in providers]
            pointers = bytearray(sizeof(GUID) * len(byte_objects))
            pointers_1 = <GUID*><unsigned char*>pointers
            for i, p in enumerate(byte_objects):
                pointers_1[i] = (<GUID *><unsigned char *>p)[0]
            err = SetTraceProviderFilter(self.handle, pointers_1, len(byte_objects))
            del byte_objects
            if err:
                raise winerror(err, NULL)

        cdef const wchar_t **pointers_2
        if provider_names:
            byte_objects = [p.encode('utf-16-le') + b'\0\0' for p in provider_names]
            pointers = bytearray(sizeof(void *) * len(byte_objects))
            pointers_2 = <const wchar_t **><unsigned char*>pointers
            for i, p in enumerate(byte_objects):
                pointers_2[i] = <const wchar_t *><unsigned char *>p
            err = SetTraceProviderNameFilter(self.handle, pointers_2, len(byte_objects))
            del byte_objects
            if err:
                raise winerror(err, NULL)
        if event_names:
            byte_objects = [p.encode('utf-16-le') + b'\0\0' for p in event_names]
            pointers = bytearray(sizeof(void *) * len(byte_objects))
            pointers_2 = <const wchar_t **><unsigned char*>pointers
            for i, p in enumerate(byte_objects):
                pointers_2[i] = <const wchar_t *><unsigned char *>p
            err = SetTraceEventNameFilter(self.handle, pointers_2, len(byte_objects))
            del byte_objects
            if err:
                raise winerror(err, NULL)

        cdef const ULONG *pointers_3
        if process_ids:
            byte_objects = [int(p).to_bytes(sizeof(ULONG), 'little') for p in process_ids]
            pointers = b''.join(byte_objects)
            pointers_3 = <const ULONG *><unsigned char*>pointers
            err = SetTraceProcessIdFilter(self.handle, pointers_3, len(byte_objects))
            del byte_objects
            if err:
                raise winerror(err, NULL)
            err = SetTraceProcessIdChildrenFilter(self.handle, 1 if include_child_process_ids else 0)
            if err:
                raise winerror(err, NULL)

        if keyword_mask_all or keyword_mask_any:
            keyword_mask_all = int(keyword_mask_all)
            keyword_mask_any = int(keyword_mask_any)
            err = SetTraceKeywordFilter(self.handle, <ULONGLONG>keyword_mask_all, <ULONGLONG>keyword_mask_any)
            if err:
                raise winerror(err, NULL)

    def __dealloc__(self):
        if self.handle:
            with nogil:
                CloseTraceHandle(self.handle, NULL, NULL)
            self.handle = NULL

    def _get_memo_size(self):
        import sys
        cb = 0
        q = [self._memo]
        while q:
            m = q.pop()
            cb += sys.getsizeof(m)
            for k, v in m.items():
                if k is None:
                    cb += sys.getsizeof(v)
                elif isinstance(k, str):
                    cb += sys.getsizeof(k)
                    cb += sys.getsizeof(v)
                else:
                    cb += sys.getsizeof(k)
                    q.append(v)
        return cb

    def close(self):
        cdef int err = 0
        cdef const char *err_source = NULL
        if self.handle:
            with nogil:
                CloseTraceHandle(self.handle, &err, &err_source)
            self.handle = NULL
            if err:
                exc = winerror(err, NULL)
                try:
                    exc.add_note(f"Cause: {err_source}")
                except AttributeError:
                    pass
                raise exc

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, exc_tb):
        self.close()

    cdef list _get_buffer(self, int n):
        if not self.handle:
            raise ValueError("no ETL trace open")
        cdef int err = 0
        ctxt = ReadContext(n, self._memo)
        with nogil:
            err = ReadTraceEvents(self.handle, &_EtlReader_Event_nogil, <PyObject*>ctxt)
        if err < 0:
            self.close()
        if ctxt.exception:
            raise ctxt.exception
        return ctxt.buffer

    def __iter__(self):
        while True:
            buffer = self._get_buffer(1024)
            i = 0
            if not buffer:
                break
            while i < len(buffer):
                v = buffer[i]
                i += 1
                if isinstance(v, BaseException):
                    raise v
                yield v

    def include_process_id(self, ULONG pid):
        cdef int err = 0
        with nogil:
            err = AddTraceProcessIdFilter(self.handle, pid)
        if err < 0:
            raise winerror(err, NULL)


def open(path, **filters):
    return EtlReader(path, **filters)
