#pragma once

extern "C" {

typedef void (__stdcall *PEnterFunction)(void *pVirtualFunctionId);
typedef void (__stdcall *PExitFunction)(void *pVirtualFunctionId);

#define EnterFunctionName "_CAP_Enter_Virtual_Function"
#define ExitFunctionName "_CAP_Exit_Virtual_Function"

}
