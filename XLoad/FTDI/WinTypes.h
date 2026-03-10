#ifndef _WINTYPES_H_
#define _WINTYPES_H_

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char UCHAR;
typedef unsigned short USHORT;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef void *PVOID;
typedef void *LPVOID;
typedef void *HANDLE;
typedef uint32_t DWORD;
typedef DWORD *LPDWORD;
typedef ULONG *PULONG;
typedef unsigned short WORD;
typedef WORD *LPWORD;
typedef unsigned char BYTE;
typedef uint8_t *PUCHAR;
typedef char *PCHAR;
typedef char *LPCTSTR;
typedef int *LPLONG;
typedef unsigned long ULONG_PTR;
typedef int BOOL;

#define WINAPI
#define WINBASEAPI

// Additional overrides for ftd2xx.h structures on non-windows
typedef struct _SECURITY_ATTRIBUTES {
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    bool   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        } DUMMYSTRUCTNAME;
        PVOID Pointer;
    } DUMMYUNIONNAME;
    HANDLE  hEvent;
} OVERLAPPED, *LPOVERLAPPED;

#ifdef __cplusplus
}
#endif

#endif // _WINTYPES_H_
