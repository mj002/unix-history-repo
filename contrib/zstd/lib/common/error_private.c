/**
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

/* The purpose of this file is to have a single list of error strings embedded in binary */

#include "error_private.h"

const char* ERR_getErrorString(ERR_enum code)
{
    static const char* const notErrorCode = "Unspecified error code";
    switch( code )
    {
    case PREFIX(no_error): return "No error detected";
    case PREFIX(GENERIC):  return "Error (generic)";
    case PREFIX(prefix_unknown): return "Unknown frame descriptor";
    case PREFIX(version_unsupported): return "Version not supported";
    case PREFIX(parameter_unknown): return "Unknown parameter type";
    case PREFIX(frameParameter_unsupported): return "Unsupported frame parameter";
    case PREFIX(frameParameter_unsupportedBy32bits): return "Frame parameter unsupported in 32-bits mode";
    case PREFIX(frameParameter_windowTooLarge): return "Frame requires too much memory for decoding";
    case PREFIX(compressionParameter_unsupported): return "Compression parameter is not supported";
    case PREFIX(compressionParameter_outOfBound): return "Compression parameter is out of bound";
    case PREFIX(init_missing): return "Context should be init first";
    case PREFIX(memory_allocation): return "Allocation error : not enough memory";
    case PREFIX(stage_wrong): return "Operation not authorized at current processing stage";
    case PREFIX(dstSize_tooSmall): return "Destination buffer is too small";
    case PREFIX(srcSize_wrong): return "Src size is incorrect";
    case PREFIX(corruption_detected): return "Corrupted block detected";
    case PREFIX(checksum_wrong): return "Restored data doesn't match checksum";
    case PREFIX(tableLog_tooLarge): return "tableLog requires too much memory : unsupported";
    case PREFIX(maxSymbolValue_tooLarge): return "Unsupported max Symbol Value : too large";
    case PREFIX(maxSymbolValue_tooSmall): return "Specified maxSymbolValue is too small";
    case PREFIX(dictionary_corrupted): return "Dictionary is corrupted";
    case PREFIX(dictionary_wrong): return "Dictionary mismatch";
    case PREFIX(dictionaryCreation_failed): return "Cannot create Dictionary from provided samples";
    case PREFIX(frameIndex_tooLarge): return "Frame index is too large";
    case PREFIX(seekableIO): return "An I/O error occurred when reading/seeking";
    case PREFIX(maxCode):
    default: return notErrorCode;
    }
}
