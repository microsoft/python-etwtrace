# cython: language_level=3

from libc.stddef cimport wchar_t
from libc.string cimport memcpy
from libc.stdint cimport uint64_t, uint32_t, uint16_t, uint8_t
from cpython.ref cimport PyObject


cdef extern from "windows.h" nogil:
    ctypedef void *PVOID
    ctypedef uint8_t BYTE
    ctypedef uint8_t UCHAR
    ctypedef uint16_t USHORT
    ctypedef uint32_t DWORD
    ctypedef uint32_t UINT32
    ctypedef uint32_t ULONG
    ctypedef uint64_t ULONG64
    ctypedef uint64_t UINT64
    ctypedef uint64_t ULONGLONG
    ctypedef wchar_t WCHAR
    ctypedef WCHAR *PWSTR
    ctypedef struct GUID:
        pass
    ctypedef struct LARGE_INTEGER:
        uint64_t QuadPart

    cdef DWORD GetLastError()

    int ERROR_INSUFFICIENT_BUFFER
    int ERROR_EVT_INVALID_EVENT_DATA

    ctypedef void *SID
    DWORD IsValidSid(SID *sid)
    DWORD GetLengthSid(SID *sid)

    DWORD LANG_USER_DEFAULT

    cdef DWORD FormatMessageW(
        DWORD   dwFlags,
        const void *lpSource,
        DWORD   dwMessageId,
        DWORD   dwLanguageId,
        PWSTR   lpBuffer,
        DWORD   nSize,
        void    **Arguments
    )

    DWORD FORMAT_MESSAGE_ALLOCATE_BUFFER
    DWORD FORMAT_MESSAGE_FROM_HMODULE
    DWORD FORMAT_MESSAGE_FROM_SYSTEM
    DWORD FORMAT_MESSAGE_IGNORE_INSERTS

    ctypedef void* HMODULE
    cdef HMODULE GetModuleHandleA(const char *moduleName)
    cdef HMODULE GetModuleHandleW(const wchar_t *moduleName)
    cdef void *GetProcAddress(HMODULE hModule, const char *procName)
    cdef void LocalFree(void*)


cdef extern from "string.h" nogil:
    size_t wcsnlen(const wchar_t *s, size_t n)
    size_t strnlen(const char *s, size_t n)
    int memcmp(const void *p1, const void *p2, size_t n)


cdef extern from "evntprov.h" nogil:
    USHORT EVENT_HEADER_FLAG_32_BIT_HEADER
    USHORT EVENT_HEADER_FLAG_64_BIT_HEADER
    USHORT EVENT_HEADER_FLAG_STRING_ONLY
    USHORT EVENT_HEADER_FLAG_CLASSIC_HEADER

    USHORT EVENT_HEADER_EXT_TYPE_STACK_TRACE32
    USHORT EVENT_HEADER_EXT_TYPE_STACK_TRACE64

    UCHAR EVENT_TRACE_TYPE_START
    UCHAR EVENT_TRACE_TYPE_END

    ctypedef struct EVENT_DESCRIPTOR:
        USHORT    Id
        UCHAR     Version
        UCHAR     Channel
        UCHAR     Level
        UCHAR     Opcode
        USHORT    Task
        ULONGLONG Keyword

    ctypedef struct EVENT_HEADER:
        USHORT Size
        USHORT HeaderType
        USHORT Flags
        USHORT EventProperty
        ULONG ThreadId
        ULONG ProcessId
        LARGE_INTEGER TimeStamp
        GUID ProviderId
        EVENT_DESCRIPTOR EventDescriptor
        ULONG KernelTime
        ULONG UserTime
        ULONG64 ProcessorTime
        GUID ActivityId

    ctypedef struct EVENT_HEADER_EXTENDED_DATA_ITEM:
        USHORT ExtType
        USHORT Linkage
        USHORT DataSize
        ULONGLONG DataPtr

    ctypedef struct EVENT_RECORD:
        EVENT_HEADER EventHeader
        #ETW_BUFFER_CONTEXT BufferContext
        USHORT ExtendedDataCount
        USHORT UserDataLength
        EVENT_HEADER_EXTENDED_DATA_ITEM *ExtendedData
        PVOID UserData
        PVOID UserContext

    ctypedef struct EVENT_EXTENDED_ITEM_STACK_TRACE32:
        ULONG64 MatchId
        ULONG *Address

    ctypedef struct EVENT_EXTENDED_ITEM_STACK_TRACE64:
        ULONG64 MatchId
        ULONG64 *Address


cdef extern from "tdh.h" nogil:
    ctypedef uint32_t TDHSTATUS

    USHORT TDH_INTYPE_NULL
    USHORT TDH_INTYPE_UNICODESTRING # length, or nul-terminated
    USHORT TDH_INTYPE_ANSISTRING # length, or nul-terminated
    USHORT TDH_INTYPE_INT8
    USHORT TDH_INTYPE_UINT8
    USHORT TDH_INTYPE_INT16
    USHORT TDH_INTYPE_UINT16
    USHORT TDH_INTYPE_INT32
    USHORT TDH_INTYPE_UINT32
    USHORT TDH_INTYPE_INT64
    USHORT TDH_INTYPE_UINT64
    USHORT TDH_INTYPE_FLOAT
    USHORT TDH_INTYPE_DOUBLE
    USHORT TDH_INTYPE_BOOLEAN # 4 bytes
    USHORT TDH_INTYPE_BINARY
    USHORT TDH_INTYPE_GUID # 16 bytes
    USHORT TDH_INTYPE_POINTER
    USHORT TDH_INTYPE_FILETIME # 8 bytes
    USHORT TDH_INTYPE_SYSTEMTIME # 16 bytes
    USHORT TDH_INTYPE_SID
    USHORT TDH_INTYPE_HEXINT32
    USHORT TDH_INTYPE_HEXINT64
    USHORT TDH_INTYPE_MANIFEST_COUNTEDSTRING
    USHORT TDH_INTYPE_MANIFEST_COUNTEDANSISTRING
    USHORT TDH_INTYPE_MANIFEST_COUNTEDBINARY

    USHORT TDH_INTYPE_COUNTEDSTRING     # 300
    USHORT TDH_INTYPE_COUNTEDANSISTRING # 301
    USHORT TDH_INTYPE_REVERSEDCOUNTEDSTRING # 302
    USHORT TDH_INTYPE_REVERSEDCOUNTEDANSISTRING # 303
    USHORT TDH_INTYPE_NONNULLTERMINATEDSTRING # 304
    USHORT TDH_INTYPE_NONNULLTERMINATEDANSISTRING # 305
    USHORT TDH_INTYPE_UNICODECHAR       # 306
    USHORT TDH_INTYPE_ANSICHAR          # 307
    USHORT TDH_INTYPE_SIZET             # 308
    USHORT TDH_INTYPE_HEXDUMP           # 309
    USHORT TDH_INTYPE_WBEMSID           # 310

    ctypedef enum DECODING_SOURCE:
        DecodingSourceXMLFile
        DecodingSourceWbem
        DecodingSourceWPP
        DecodingSourceTlg
        DecodingSourceMax

    ctypedef enum PROPERTY_FLAGS:
        PropertyStruct #= 0x1,
        PropertyParamLength #= 0x2,
        PropertyParamCount #= 0x4,
        PropertyWBEMXmlFragment #= 0x8,
        PropertyParamFixedLength #= 0x10,
        PropertyParamFixedCount #= 0x20,
        PropertyHasTags #= 0x40,
        PropertyHasCustomSchema #= 0x80

    ctypedef struct _EPI_nonStructType:
        USHORT InType
        USHORT OutType
        ULONG  MapNameOffset

    ctypedef struct _EPI_structType:
        USHORT StructStartIndex
        USHORT NumOfStructMembers
        ULONG  padding

    ctypedef struct _EPI_customSchemaType:
        USHORT InType
        USHORT OutType
        ULONG  CustomSchemaOffset

    ctypedef struct EVENT_PROPERTY_INFO:
        PROPERTY_FLAGS Flags
        ULONG NameOffset
        
        _EPI_nonStructType nonStructType
        _EPI_structType structType
        _EPI_customSchemaType customSchemaType
        USHORT count
        USHORT countPropertyIndex
        USHORT length
        USHORT lengthPropertyIndex
        ULONG Tags

    ctypedef struct TRACE_EVENT_INFO:
        GUID ProviderGuid
        GUID EventGuid
        EVENT_DESCRIPTOR EventDescriptor
        DECODING_SOURCE DecodingSource
        ULONG ProviderNameOffset
        ULONG LevelNameOffset
        ULONG ChannelNameOffset
        ULONG KeywordsNameOffset
        ULONG TaskNameOffset
        ULONG OpcodeNameOffset
        ULONG EventMessageOffset
        ULONG ProviderMessageOffset
        ULONG BinaryXMLOffset
        ULONG BinaryXMLSize
        ULONG EventNameOffset
        ULONG ActivityIDNameOffset
        ULONG EventAttributesOffset
        ULONG RelatedActivityIDNameOffset
        ULONG PropertyCount
        ULONG TopLevelPropertyCount
        ULONG Tags
        EVENT_PROPERTY_INFO *EventPropertyInfoArray

    ctypedef struct EVENT_MAP_INFO:
        pass

    TDHSTATUS TdhGetEventMapInformation(
        EVENT_RECORD    *pEvent,
        PWSTR           pMapName,
        EVENT_MAP_INFO  *pBuffer,
        ULONG           *pBufferSize
    )

    TDHSTATUS TdhFormatProperty(
        TRACE_EVENT_INFO *EventInfo,
        EVENT_MAP_INFO *MapInfo,
        ULONG PointerSize,
        USHORT PropertyInType,
        USHORT PropertyOutType,
        USHORT PropertyLength,
        USHORT UserDataLength,
        BYTE *UserData,
        ULONG *BufferSize,
        WCHAR *Buffer,
        USHORT *UserDataConsumed
    )
