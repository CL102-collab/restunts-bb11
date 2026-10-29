#ifndef RESTUNTS_TEST_EMSCRIPTEN_H
#define RESTUNTS_TEST_EMSCRIPTEN_H

/* Native filesystem tests supply deterministic persistence hooks instead of
 * executing JavaScript. Only this fixture's include path selects this header. */
#define EM_JS(result, name, arguments, ...) result name arguments
#define EM_ASYNC_JS(result, name, arguments, ...) result name arguments

#endif
