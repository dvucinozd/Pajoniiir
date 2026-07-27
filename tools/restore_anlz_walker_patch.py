#!/usr/bin/env python3
from pathlib import Path
import subprocess

base = "267953d52b8035e78f818ae196cd99ee4a7e3534"
repo_path = "firmware/main-deck-p4/components/library/rekordbox_anlz.c"
path = Path(repo_path)
original = subprocess.check_output(["git", "show", f"{base}:{repo_path}"], text=True)

old = """    long fsz = ftell(fp);
    if (fsz < 12) return TAG_WALK_MALFORMED;

    uint32_t file_len = (uint32_t)fsz;
"""
new = """    long fsz = ftell(fp);
    if (fsz < 12 || (unsigned long)fsz > UINT32_MAX) {
        return TAG_WALK_MALFORMED;
    }

    uint32_t file_len = (uint32_t)fsz;
"""
if original.count(old) != 1:
    raise RuntimeError("file-size walker block mismatch")
text = original.replace(old, new, 1)

old = """        if (tag == target) {
            if (fseek(fp, (long)(pos + 4u), SEEK_SET) != 0) return TAG_WALK_MALFORMED;
            return TAG_WALK_FOUND;
        }

        /* PMAI's segment_size covers the entire file; its sections start
         * right after the PMAI header. Every other section advances by its
         * own total segment size. */
        uint32_t advance = (tag == ANLZ_TAG_PMAI) ? header_size : segment_size;
        if (advance < 12u || advance > file_len - pos) {
            return TAG_WALK_MALFORMED;
        }
        pos += advance;
"""
new = """        /* Validate the section envelope even when this is the requested tag.
         * A target tag with an oversized segment must not bypass the structural
         * walk and become parser-specific partial data. */
        uint32_t advance = (tag == ANLZ_TAG_PMAI) ? header_size : segment_size;
        if (header_size < 12u || segment_size < header_size ||
            advance < 12u || advance > file_len - pos) {
            return TAG_WALK_MALFORMED;
        }

        if (tag == target) {
            if (fseek(fp, (long)(pos + 4u), SEEK_SET) != 0) return TAG_WALK_MALFORMED;
            return TAG_WALK_FOUND;
        }

        /* PMAI's segment_size covers the entire file; its sections start
         * right after the PMAI header. Every other section advances by its
         * own total segment size. */
        pos += advance;
"""
if text.count(old) != 1:
    raise RuntimeError("section-envelope walker block mismatch")
text = text.replace(old, new, 1)

old = """    return TAG_WALK_ABSENT;
}
"""
new = """    /* A clean section chain ends exactly at EOF. One to eleven trailing
     * bytes are a partial section header, not an absent optional tag. */
    return pos == file_len ? TAG_WALK_ABSENT : TAG_WALK_MALFORMED;
}
"""
if text.count(old) < 1:
    raise RuntimeError("walker return block mismatch")
text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8")
print("restored full ANLZ parser and applied bounded walker patch")
