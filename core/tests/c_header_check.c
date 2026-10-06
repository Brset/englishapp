/* Compiled as C to make sure pron_c.h is a valid C header. */
#include "pron/pron_c.h"

#include <string.h>

int pron_c_header_check(void) {
    pron_word w;
    pron_assessor* a = pron_assessor_create();
    char* json;
    int ok;
    w.text = "hello";
    w.start = 0.0;
    w.end = 0.5;
    w.probability = 1.0f;
    if (!a) return 0;
    json = pron_assess(a, "Hello", &w, 1, NULL, 0, 0, 0.02);
    ok = json != NULL && strstr(json, "\"words\"") != NULL && strlen(pron_version()) > 0;
    pron_free_string(json);
    pron_assessor_destroy(a);
    return ok;
}
