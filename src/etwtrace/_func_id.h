#pragma once

// Must be no larger than sizeof(void *), as it is reported as pointer sized
// in ETW events.
typedef int FUNC_ID;

#define PyLong_AsFUNC_ID PyLong_AsLong
#define PyLong_FromFUNC_ID PyLong_FromLong

#define FUNC_ID_IS_VALID(f) ((f) > 0)

static inline FUNC_ID Void_AsFUNC_ID(void *v) {
#pragma warning(push)
#pragma warning(disable: 4302 4311)
    return (FUNC_ID)v;
#pragma warning(pop)
}

static inline void *Void_FromFUNC_ID(FUNC_ID v) {
#pragma warning(push)
#pragma warning(disable: 4312)
    return (void *)v;
#pragma warning(pop)
}

#define FUNC_ID_FIRST (FUNC_ID)1
#define FUNC_ID_NOT_FOUND (FUNC_ID)0
#define FUNC_ID_IGNORED (FUNC_ID)-2
#define FUNC_ID_ERROR (FUNC_ID)-1
