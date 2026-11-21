#pragma once
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#if CONFIG_AUDIO_KEYLOCK_WSOLA2
#include "audio_keylock_wsola2.h"
typedef audio_keylock2_t audio_keylock_selected_t;
#define audio_keylock_selected_reset audio_keylock2_reset
#define audio_keylock_selected_configure audio_keylock2_configure
#define audio_keylock_selected_next audio_keylock2_next
#else
#include "audio_keylock.h"
typedef audio_keylock_t audio_keylock_selected_t;
#define audio_keylock_selected_reset audio_keylock_reset
#define audio_keylock_selected_configure audio_keylock_configure
#define audio_keylock_selected_next audio_keylock_next
#endif
