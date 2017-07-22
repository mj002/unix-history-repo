//===-- OptionGroupFormat.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupFormat.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/ArchSpec.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupFormat::OptionGroupFormat(lldb::Format default_format,
                                     uint64_t default_byte_size,
                                     uint64_t default_count)
    : m_format(default_format, default_format),
      m_byte_size(default_byte_size, default_byte_size),
      m_count(default_count, default_count), m_prev_gdb_format('x'),
      m_prev_gdb_size('w') {}

OptionGroupFormat::~OptionGroupFormat() {}

static OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "format", 'f', OptionParser::eRequiredArgument,
     nullptr, nullptr, 0, eArgTypeFormat,
     "Specify a format to be used for display."},
    {LLDB_OPT_SET_2, false, "gdb-format", 'G', OptionParser::eRequiredArgument,
     nullptr, nullptr, 0, eArgTypeGDBFormat,
     "Specify a format using a GDB format specifier string."},
    {LLDB_OPT_SET_3, false, "size", 's', OptionParser::eRequiredArgument,
     nullptr, nullptr, 0, eArgTypeByteSize,
     "The size in bytes to use when displaying with the selected format."},
    {LLDB_OPT_SET_4, false, "count", 'c', OptionParser::eRequiredArgument,
     nullptr, nullptr, 0, eArgTypeCount,
     "The number of total items to display."},
};

llvm::ArrayRef<OptionDefinition> OptionGroupFormat::GetDefinitions() {
  auto result = llvm::makeArrayRef(g_option_table);
  if (m_byte_size.GetDefaultValue() < UINT64_MAX) {
    if (m_count.GetDefaultValue() < UINT64_MAX)
      return result;
    else
      return result.take_front(3);
  }
  return result.take_front(2);
}

Status OptionGroupFormat::SetOptionValue(uint32_t option_idx,
                                         llvm::StringRef option_arg,
                                         ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;

  switch (short_option) {
  case 'f':
    error = m_format.SetValueFromString(option_arg);
    break;

  case 'c':
    if (m_count.GetDefaultValue() == 0) {
      error.SetErrorString("--count option is disabled");
    } else {
      error = m_count.SetValueFromString(option_arg);
      if (m_count.GetCurrentValue() == 0)
        error.SetErrorStringWithFormat("invalid --count option value '%s'",
                                       option_arg.str().c_str());
    }
    break;

  case 's':
    if (m_byte_size.GetDefaultValue() == 0) {
      error.SetErrorString("--size option is disabled");
    } else {
      error = m_byte_size.SetValueFromString(option_arg);
      if (m_byte_size.GetCurrentValue() == 0)
        error.SetErrorStringWithFormat("invalid --size option value '%s'",
                                       option_arg.str().c_str());
    }
    break;

  case 'G': {
    uint64_t count = 0;
    llvm::StringRef gdb_format_str = option_arg;
    gdb_format_str.consumeInteger(0, count);

    Format format = eFormatDefault;
    uint32_t byte_size = 0;

    while (!gdb_format_str.empty() &&
           ParserGDBFormatLetter(execution_context, gdb_format_str[0], format,
                                 byte_size)) {
      gdb_format_str = gdb_format_str.drop_front();
    }

    // We the first character of the "gdb_format_str" is not the
    // NULL terminator, we didn't consume the entire string and
    // something is wrong. Also, if none of the format, size or count
    // was specified correctly, then abort.
    if (!gdb_format_str.empty() ||
        (format == eFormatInvalid && byte_size == 0 && count == 0)) {
      // Nothing got set correctly
      error.SetErrorStringWithFormat("invalid gdb format string '%s'",
                                     option_arg.str().c_str());
      return error;
    }

    // At least one of the format, size or count was set correctly.
    // Anything that wasn't set correctly should be set to the
    // previous default
    if (format == eFormatInvalid)
      ParserGDBFormatLetter(execution_context, m_prev_gdb_format, format,
                            byte_size);

    const bool byte_size_enabled = m_byte_size.GetDefaultValue() < UINT64_MAX;
    const bool count_enabled = m_count.GetDefaultValue() < UINT64_MAX;
    if (byte_size_enabled) {
      // Byte size is enabled
      if (byte_size == 0)
        ParserGDBFormatLetter(execution_context, m_prev_gdb_size, format,
                              byte_size);
    } else {
      // Byte size is disabled, make sure it wasn't specified
      // but if this is an address, it's actually necessary to
      // specify one so don't error out
      if (byte_size > 0 && format != lldb::eFormatAddressInfo) {
        error.SetErrorString(
            "this command doesn't support specifying a byte size");
        return error;
      }
    }

    if (count_enabled) {
      // Count is enabled and was not set, set it to the default for gdb format
      // statements (which is 1).
      if (count == 0)
        count = 1;
    } else {
      // Count is disabled, make sure it wasn't specified
      if (count > 0) {
        error.SetErrorString("this command doesn't support specifying a count");
        return error;
      }
    }

    m_format.SetCurrentValue(format);
    m_format.SetOptionWasSet();
    if (byte_size_enabled) {
      m_byte_size.SetCurrentValue(byte_size);
      m_byte_size.SetOptionWasSet();
    }
    if (count_enabled) {
      m_count.SetCurrentValue(count);
      m_count.SetOptionWasSet();
    }
  } break;

  default:
    error.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
    break;
  }

  return error;
}

bool OptionGroupFormat::ParserGDBFormatLetter(
    ExecutionContext *execution_context, char format_letter, Format &format,
    uint32_t &byte_size) {
  m_has_gdb_format = true;
  switch (format_letter) {
  case 'o':
    format = eFormatOctal;
    m_prev_gdb_format = format_letter;
    return true;
  case 'x':
    format = eFormatHex;
    m_prev_gdb_format = format_letter;
    return true;
  case 'd':
    format = eFormatDecimal;
    m_prev_gdb_format = format_letter;
    return true;
  case 'u':
    format = eFormatUnsigned;
    m_prev_gdb_format = format_letter;
    return true;
  case 't':
    format = eFormatBinary;
    m_prev_gdb_format = format_letter;
    return true;
  case 'f':
    format = eFormatFloat;
    m_prev_gdb_format = format_letter;
    return true;
  case 'a':
    format = eFormatAddressInfo;
    {
      TargetSP target_sp =
          execution_context ? execution_context->GetTargetSP() : TargetSP();
      if (target_sp)
        byte_size = target_sp->GetArchitecture().GetAddressByteSize();
      m_prev_gdb_format = format_letter;
      return true;
    }
  case 'i':
    format = eFormatInstruction;
    m_prev_gdb_format = format_letter;
    return true;
  case 'c':
    format = eFormatChar;
    m_prev_gdb_format = format_letter;
    return true;
  case 's':
    format = eFormatCString;
    m_prev_gdb_format = format_letter;
    return true;
  case 'T':
    format = eFormatOSType;
    m_prev_gdb_format = format_letter;
    return true;
  case 'A':
    format = eFormatHexFloat;
    m_prev_gdb_format = format_letter;
    return true;

  case 'b':
  case 'h':
  case 'w':
  case 'g':
    {
      // Size isn't used for printing instructions, so if a size is specified, and
      // the previous format was
      // 'i', then we should reset it to the default ('x').  Otherwise we'll
      // continue to print as instructions,
      // which isn't expected.
      if (format_letter == 'b')
          byte_size = 1;
      else if (format_letter == 'h')
          byte_size = 2;
      else if (format_letter == 'w')
          byte_size = 4;
      else if (format_letter == 'g')
          byte_size = 8;

        m_prev_gdb_size = format_letter;
        if (m_prev_gdb_format == 'i')
          m_prev_gdb_format = 'x';
        return true;
    }
    break;
  default:
    break;
  }


  return false;
}

void OptionGroupFormat::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_format.Clear();
  m_byte_size.Clear();
  m_count.Clear();
  m_has_gdb_format = false;
}
