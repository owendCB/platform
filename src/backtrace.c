/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "config.h"

#include <platform/backtrace.h>
#include <strings.h>

#if defined(WIN32)
#  define HAVE_BACKTRACE_SUPPORT 1
#  include <Dbghelp.h>
#endif

#if defined(HAVE_BACKTRACE) && defined(HAVE_DLADDR)
#  define HAVE_BACKTRACE_SUPPORT 1
#  include <execinfo.h> // for backtrace()
#  include <dlfcn.h> // for dladdr()
#  include <stddef.h> // for ptrdiff_t
#endif

// Maximum number of frames that will be printed.
#define MAX_FRAMES 50

#if defined(HAVE_BACKTRACE_SUPPORT)
/**
 * Populates buf with a description of the given address in the program.
 **/
static void describe_address(char* msg, size_t len, void* addr) {
#if defined(WIN32)

    // Get module information
    IMAGEHLP_MODULE64 module_info;
    module_info.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
    SymGetModuleInfo64(GetCurrentProcess(), (DWORD64)addr, &module_info);

    // Get symbol information.
    DWORD64 displacement = 0;
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO sym_info = (PSYMBOL_INFO)buffer;
    sym_info->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym_info->MaxNameLen = MAX_SYM_NAME;

    if (SymFromAddr(GetCurrentProcess(), (DWORD64)addr, &displacement,
                    sym_info)) {
        snprintf(msg, len, "%s(%s+%lld) [0x%p]",
                 module_info.ImageName ? module_info.ImageName : "",
                 sym_info->Name, displacement, addr);
    } else {
        // No symbol found.
        snprintf(msg, len, "[0x%p]", addr);
    }
#else // !WIN32
    Dl_info info;
    int status = dladdr(addr, &info);

    if (status != 0 &&
        info.dli_fname != NULL &&
        info.dli_fname[0] != '\0') {

        if (info.dli_saddr == 0) {
            // No offset calculation possible.
            snprintf(msg, len, "%s(%s) [%p]",
                    info.dli_fname,
                    info.dli_sname ? info.dli_sname : "",
                    addr);
        } else {
            char sign;
            ptrdiff_t offset;
            if (addr >= info.dli_saddr) {
                sign = '+';
                offset = (char*)addr - (char*)info.dli_saddr;
            } else {
                sign = '-';
                offset = (char*)info.dli_saddr - (char*)addr;
            }
            snprintf(msg, len, "%s(%s%c%#tx) [%p]",
                    info.dli_fname,
                    info.dli_sname ? info.dli_sname : "",
                    sign, offset, addr);
        }
    } else {
        // No symbol found.
        snprintf(msg, len, "[%p]", addr);
    }
#endif // WIN32
}

PLATFORM_PUBLIC_API
void print_backtrace(write_cb_t write_cb, void* context) {
    void* frames[MAX_FRAMES];
#if defined(WIN32)
    int active_frames = CaptureStackBackTrace(0, MAX_FRAMES, frames, NULL);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#else
    int active_frames = backtrace(frames, MAX_FRAMES);
#endif

    // Note we start from 1 to skip our own frame.
    for (int ii = 1; ii < active_frames; ii++) {
        // Fixed-sized buffer; possible that description will be cropped.
        char msg[200];
        describe_address(msg, sizeof(msg), frames[ii]);
        write_cb(context, msg);
    }
    if (active_frames == MAX_FRAMES) {
        write_cb(context, "<frame limit reached, possible truncation>");
    }
}

#else // if defined(HAVE_BACKTRACE_SUPPORT)

PLATFORM_PUBLIC_API
void print_backtrace(write_cb_t write_cb, void* context) {
    write_cb(context, "<backtrace not supported on this platform>");
}

#endif // defined(HAVE_BACKTRACE_SUPPORT)

static void print_to_file_cb(void* ctx, const char* frame) {
    fprintf(ctx, "\t%s\n", frame);
}

PLATFORM_PUBLIC_API
void print_backtrace_to_file(FILE* stream) {
    print_backtrace(print_to_file_cb, stream);
}

struct context {
    const char *indent;
    char *buffer;
    size_t size;
    size_t offset;
    bool error;
};

static void memory_cb(void* ctx, const char* frame) {
    struct context *c = ctx;


    if (!c->error) {
        int length = snprintf(c->buffer + c->offset, c->size - c->offset - 1,
                              "%s%s\n", c->indent, frame);

        if (length < 0) {
            c->error = true;
        } else {
            c->offset += length;
        }
    }
}

PLATFORM_PUBLIC_API
bool print_backtrace_to_buffer(const char *indent, char *buffer, size_t size) {
    struct context c = {
        .indent = indent,
        .buffer = buffer,
        .size = size,
        .offset = 0,
        .error = false};
    print_backtrace(memory_cb, &c);
    return !c.error;
}
