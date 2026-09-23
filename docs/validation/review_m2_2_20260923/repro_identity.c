#define main existing_library_test_main
#include "../../../tests/library_anlz/test_library_anlz.c"
#undef main

int main(void)
{
    library_track_t a = {0}, b = {0};
    a.track_id = b.track_id = 42;
    strcpy(a.path, "/Contents/CollectionA/TrackA.mp3");
    strcpy(b.path, "/Contents/CollectionB/TrackB.mp3");
    unsigned ka = library_track_key(&a), kb = library_track_key(&b);
    printf("INDEPENDENT_TRACKS keyA=%u keyB=%u equal=%d (expected distinct persistent identities)\n", ka, kb, ka == kb);
    return 0;
}
