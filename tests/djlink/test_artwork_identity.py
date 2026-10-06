"""Run production cache ownership/lookup with host allocation stubs, no LVGL/RTOS."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
component = root / "firmware/p4-core/components"
source = (component / "ui/ui_artwork.c").read_text(encoding="utf-8")
out = root / ".cache/artwork-identity"
out.mkdir(parents=True, exist_ok=True)

def block(marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

slot_start = source.index("typedef struct {\n    uint32_t key, gen, stamp;")
slot_end = source.index("} art_slot_t;", slot_start) + len("} art_slot_t;")
code = r'''
#include "ui_artwork.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef enum { SLOT_EMPTY, SLOT_PENDING, SLOT_READY, SLOT_NONE } slot_state_t;
''' + source[slot_start:slot_end] + r'''
static art_slot_t s_slot[UI_ARTWORK_SLOTS];
static ui_artwork_thumb_t storage[UI_ARTWORK_SLOTS], *s_thumbs=storage;
static uint32_t s_clock,s_gen=1;
static bool s_init_done=true,s_init_failed;
static bool art_init(void) {return !s_init_failed;}
'''
for marker in ("static const uint16_t *slot_pixels(", "static int slot_victim(",
               "const uint16_t *ui_artwork_get_identity(", "void ui_artwork_publish_identity("):
    code += block(marker) + "\n"
code += r'''
int main(void) {
    media_persistent_id_t a={.valid=true},b={.valid=true},absent={.valid=true};
    a.bytes[31]=1;b.bytes[31]=2;absent.bytes[31]=3;
    ui_artwork_thumb_t thumb={0};thumb.deck[0]=123;thumb.row[0]=124;
    ui_artwork_publish_identity(7,&a,&thumb);
    thumb.deck[0]=999; /* mutate/reuse the worker's buffer after publication */
    assert(ui_artwork_get_identity(7,&a,UI_ARTWORK_DECK)[0]==123);
    assert(!ui_artwork_get_identity(7,&b,UI_ARTWORK_DECK));
    ui_artwork_publish_identity(7,&b,&thumb); /* identical 32-bit lookup key */
    assert(ui_artwork_get_identity(7,&a,UI_ARTWORK_DECK)[0]==123);
    assert(ui_artwork_get_identity(7,&b,UI_ARTWORK_DECK)[0]==999);
    s_gen++; /* a local USB catalog refresh must not expire remote ownership */
    assert(ui_artwork_get_identity(7,&a,UI_ARTWORK_ROW)[0]==124);
    assert(!ui_artwork_get_identity(7,&absent,UI_ARTWORK_DECK));
    s_init_failed=true;assert(!ui_artwork_get_identity(7,&a,UI_ARTWORK_DECK));
    puts("PASS production artwork deep-copy, full-ID collision isolation and local epoch independence");
}
'''
c_file = out / "test.c"
c_file.write_text(code, encoding="utf-8")
target = out / ("test.exe" if os.name == "nt" else "test")
gcc = shutil.which("gcc")
if not gcc:
    raise RuntimeError("gcc is required on PATH")
subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I" + str(component / "ui/include"),
                "-I" + str(component / "media_identity/include"), str(c_file),
                str(component / "media_identity/media_identity.c"),
                "-o", str(target)], check=True)
subprocess.run([str(target)], check=True, timeout=30)
