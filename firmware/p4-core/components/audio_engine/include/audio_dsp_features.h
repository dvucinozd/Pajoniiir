#pragma once
/* Allocate persistent FIR state only when the product enables antialiasing.
 * Host DSP qualification includes both policies unless explicitly overridden. */
#ifndef AUDIO_ANTIALIAS_CACHE
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#ifdef CONFIG_AUDIO_ANTIALIAS_CACHE
#define AUDIO_ANTIALIAS_CACHE 1
#else
#define AUDIO_ANTIALIAS_CACHE 0
#endif
#else
#define AUDIO_ANTIALIAS_CACHE 1
#endif
#endif
