#include "web_firmware_json.h"
#include "web_api_helpers.h"

#include <string.h>

void web_firmware_json_escape_in_place(char *value, size_t value_size)
{
    if (!value || value_size == 0u) {
        return;
    }

    /* The firmware status fields are small fixed arrays. Copy first so the JSON
     * escaper may safely write back into the original storage without source/
     * destination overlap. GCC/ESP-IDF and the host harness both support C99
     * variable-length arrays. */
    char source[value_size];
    memcpy(source, value, value_size);
    source[value_size - 1u] = '\0';
    (void)web_api_json_escape(source, value, value_size);
}
