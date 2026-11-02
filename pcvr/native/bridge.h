#pragma once
#include <stddef.h>
#ifdef _WIN32
#define RT_EXPORT __declspec(dllexport)
#else
#define RT_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
RT_EXPORT int rt_abi_version(void);
// Inputs are length-delimited UTF-8. Outputs belong to this DLL; release each
// non-null output exactly once with rt_free, including error responses.
RT_EXPORT int rt_call(const char *request, size_t request_size, char **response, size_t *response_size);
// RGBA bytes, bottom row first, matching Unity Texture2D.LoadRawTextureData.
RT_EXPORT int rt_chart(int service, void **pixels, size_t *size, int *width, int *height);
RT_EXPORT void rt_free(void *buffer);
#ifdef __cplusplus
}
#endif
