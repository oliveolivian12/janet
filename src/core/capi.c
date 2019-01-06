/*
* Copyright (c) 2019 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include <janet/janet.h>
#include "state.h"
#include "fiber.h"

void janet_panicv(Janet message) {
    if (janet_vm_fiber != NULL) {
        janet_fiber_push(janet_vm_fiber, message);
        longjmp(janet_vm_fiber->buf, 1);
    } else {
        fputs((const char *)janet_formatc("janet top level panic - %v\n", message), stdout);
        exit(1);
    }
}

void janet_panic(const char *message) {
    janet_panicv(janet_cstringv(message));
}

void janet_panics(const uint8_t *message) {
    janet_panicv(janet_wrap_string(message));
}

void janet_panic_type(Janet x, int32_t n, int expected) {
    janet_panicf("bad slot #%d, expected %T, got %v", n, expected, x);
}

void janet_panic_abstract(Janet x, int32_t n, const JanetAbstractType *at) {
    janet_panicf("bad slot #%d, expected %s, got %v", n, at->name, x);
}

void janet_fixarity(int32_t arity, int32_t fix) {
    if (arity != fix)
        janet_panicf("arity mismatch, expected %d, got %d", fix, arity);
}

void janet_arity(int32_t arity, int32_t min, int32_t max) {
    if (min >= 0 && arity < min)
        janet_panicf("arity mismatch, expected at least %d, got %d", min, arity);
    if (max >= 0 && arity > max)
        janet_panicf("arity mismatch, expected at most %d, got %d", max, arity);
}

#define DEFINE_GETTER(name, NAME, type) \
type janet_get##name(const Janet *argv, int32_t n) { \
    Janet x = argv[n]; \
    if (!janet_checktype(x, JANET_##NAME)) { \
        janet_panic_type(x, n, JANET_TFLAG_##NAME); \
    } \
    return janet_unwrap_##name(x); \
}

DEFINE_GETTER(number, NUMBER, double)
DEFINE_GETTER(array, ARRAY, JanetArray *)
DEFINE_GETTER(tuple, TUPLE, const Janet *)
DEFINE_GETTER(table, TABLE, JanetTable *)
DEFINE_GETTER(struct, STRUCT, const JanetKV *)
DEFINE_GETTER(string, STRING, const uint8_t *)
DEFINE_GETTER(keyword, KEYWORD, const uint8_t *)
DEFINE_GETTER(symbol, SYMBOL, const uint8_t *)
DEFINE_GETTER(buffer, BUFFER, JanetBuffer *) 
DEFINE_GETTER(fiber, FIBER, JanetFiber *) 
DEFINE_GETTER(function, FUNCTION, JanetFunction *) 
DEFINE_GETTER(cfunction, CFUNCTION, JanetCFunction) 

int janet_getboolean(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (janet_checktype(x, JANET_TRUE)) {
        return 1;
    } else if (!janet_checktype(x, JANET_FALSE)) {
        janet_panicf("bad slot #%d, expected boolean, got %v", n, x);
    }
    return 0;
}

int32_t janet_getinteger(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checkint(x)) {
        janet_panicf("bad slot #%d, expected integer, got %v", n, x);
    }
    return janet_unwrap_integer(x);
}

int64_t janet_getinteger64(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checkint64(x)) {
        janet_panicf("bad slot #%d, expected 64 bit integer, got %v", n, x);
    }
    return (int64_t) janet_unwrap_number(x);
}

JanetView janet_getindexed(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    JanetView view;
    if (!janet_indexed_view(x, &view.items, &view.len)) {
        janet_panic_type(x, n, JANET_TFLAG_INDEXED);
    }
    return view;
}

JanetByteView janet_getbytes(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    JanetByteView view;
    if (!janet_bytes_view(x, &view.bytes, &view.len)) {
        janet_panic_type(x, n, JANET_TFLAG_BYTES);
    }
    return view;
}

JanetDictView janet_getdictionary(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    JanetDictView view;
    if (!janet_dictionary_view(x, &view.kvs, &view.len, &view.cap)) {
        janet_panic_type(x, n, JANET_TFLAG_DICTIONARY);
    }
    return view;
}

void *janet_getabstract(const Janet *argv, int32_t n, const JanetAbstractType *at) {
    Janet x = argv[n];
    if (!janet_checktype(x, JANET_ABSTRACT)) {
        janet_panic_abstract(x, n, at);
    }
    void *abstractx = janet_unwrap_abstract(x);
    if (janet_abstract_type(abstractx) != at) {
        janet_panic_abstract(x, n, at);
    }
    return abstractx;
}

JanetRange janet_getslice(int32_t argc, const Janet *argv) {
    janet_arity(argc, 1, 3);
    JanetRange range;
    int32_t length = janet_length(argv[0]);
    if (argc == 1) {
        range.start = 0;
        range.end = length;
    } else if (argc == 2) {
        range.start = janet_getinteger(argv, 1);
        range.end = length;
        if (range.start < 0) {
            range.start += length + 1;
        }
        if (range.start < 0 || range.start > length) {
            janet_panicf("slice start: index %d out of range [0,%d]", range.start, length);
        }
    } else if (argc == 3) {
        range.start = janet_getinteger(argv, 1);
        range.end = janet_getinteger(argv, 2);
        if (range.start < 0) {
            range.start += length + 1;
        }
        if (range.end < 0) {
            range.end += length + 1;
        }
        if (range.start < 0 || range.start > length) {
            janet_panicf("slice start: index %d out of range [0,%d]", range.start, length);
        }
        if (range.end < 0 || range.end > length) {
            janet_panicf("slice end: index %d out of range [0,%d]", range.end, length);
        }
        if (range.end < range.start) {
            range.end = range.start;
        }
    }
    return range;
}