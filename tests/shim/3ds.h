/* HOST SHIM of the libctru APT hook declarations used by src/apt_lifecycle.c. Signatures are
 * copied from libctru 2.x <3ds/services/apt.h>; the ARM build compiles the same source against
 * the REAL header, so any drift fails `make 3ds`. Not an SDK implementation. */
#ifndef SHIM_3DS_H
#define SHIM_3DS_H
typedef enum { APTHOOK_ONSUSPEND = 0, APTHOOK_ONRESTORE, APTHOOK_ONSLEEP, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT, APTHOOK_COUNT } APT_HookType;
typedef void (*aptHookFn)(APT_HookType hook, void* param);
typedef struct tag_aptHookCookie { struct tag_aptHookCookie* next; aptHookFn callback; void* param; } aptHookCookie;
void aptHook(aptHookCookie* cookie, aptHookFn callback, void* param);
void aptUnhook(aptHookCookie* cookie);
#endif
