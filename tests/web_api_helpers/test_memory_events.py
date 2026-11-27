"""Exercise the production HTTP memory-action mapping at the queue boundary."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
components = root / "firmware/p4-core/components"
source = (components / "web_server/web_server.c").read_text(encoding="utf-8")
start = source.index('if (!strcmp(action, "memory_store")')
end = source.index(' else if (strcmp(action, "pfl")', start)
branch = source[start:end]

code = r'''
#include <assert.h>
#include <string.h>
#include "control_link.h"

static ctrl_event_t queued;
static unsigned submissions;
static esp_err_t deck_core_queue_remote_event(const ctrl_event_t *event)
{
    queued = *event;
    submissions++;
    return ESP_OK;
}

static esp_err_t submit(const char *action, unsigned deck)
{
    esp_err_t queue_rc = ESP_ERR_INVALID_ARG;
''' + branch + r'''
    return queue_rc;
}

int main(void)
{
    const char *actions[] = {"memory_store", "memory_delete", "memory_prev",
                             "memory_next", "memory_restore_source"};
    const unsigned kinds[] = {CTRL_DECK_EXT_ACTION_MEMORY_STORE, CTRL_DECK_EXT_ACTION_MEMORY_DELETE,
                              CTRL_DECK_EXT_ACTION_MEMORY_PREV, CTRL_DECK_EXT_ACTION_MEMORY_NEXT,
                              CTRL_DECK_EXT_ACTION_MEMORY_RESTORE_SOURCE};
    for (unsigned deck = 0; deck < 2; ++deck) {
        for (unsigned index = 0; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
            assert(submit(actions[index], deck) == ESP_OK);
            assert(queued.type == CTRL_EV_BUTTON);
            assert(queued.deck == deck && control_link_id_deck(queued.id) == deck);
            assert(control_link_id_control(queued.id) == CTRL_DECK_CTL_EXT_ACTION);
            assert(queued.value == CTRL_DECK_EXT_VALUE(kinds[index], true));
            assert(queued.expected_track_generation == 0);
        }
    }
    assert(submissions == 10);
    assert(submit("memory_unknown", 0) == ESP_ERR_INVALID_ARG && submissions == 10);
}
'''
with tempfile.TemporaryDirectory(prefix="p4-web-memory-") as temporary:
    folder = Path(temporary)
    test = folder / "test.c"
    executable = folder / "test.exe"
    test.write_text(code, encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "tests/support/stubs"),
                    "-I" + str(components / "control_link/include"),
                    str(test), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS production web memory actions for both decks")
