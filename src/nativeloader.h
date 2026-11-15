#pragma once
#include <windows.h>
#include <wchar.h>

typedef void (__cdecl *NativeLoaderLog_t)(const char *mod, const char *message);

typedef struct NativeLoaderCallbacks {
  void (__cdecl *OnGameStart)(void);
  void (__cdecl *OnTypesRegistering)(void);
  void (__cdecl *OnTypesRegistered)(void);
  void (__cdecl *OnTypeRegistering)(void *type);
  void (__cdecl *OnTypeRegistered)(void *type);
} NativeLoaderCallbacks;

typedef void (__cdecl *NativeLoaderInit_t)(NativeLoaderCallbacks *callbacks);

static __inline HMODULE NativeLoaderModule(void) {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return NULL;
  wchar_t *slash = wcsrchr(path, L'\\');
  if (!slash) return NULL;
  if (wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"winmm.dll") != 0) return NULL;
  return GetModuleHandleW(path);
}

static __inline NativeLoaderLog_t NativeLoaderGetLog(void) {
  HMODULE h = NativeLoaderModule();
  return h ? (NativeLoaderLog_t)GetProcAddress(h, "NativeLoaderLog") : NULL;
}
