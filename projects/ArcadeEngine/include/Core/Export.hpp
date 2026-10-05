#pragma once

#if defined(_WIN32)
#if defined(ARCADE_ENGINE_BUILD)
#define ARCADE_ENGINE_API __declspec(dllexport)
#else
#define ARCADE_ENGINE_API __declspec(dllimport)
#endif
#else
#define ARCADE_ENGINE_API __attribute__((visibility("default")))
#endif
