//===-- GDBRemoteCommunicationServerCommon.cpp ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteCommunicationServerCommon.h"

#include <errno.h>

// C Includes

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

// C++ Includes
#include <chrono>
#include <cstring>

// Other libraries and framework includes
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/File.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Interpreter/Args.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/FileAction.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/JSON.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamGDBRemote.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/Triple.h"

// Project includes
#include "ProcessGDBRemoteLog.h"
#include "Utility/StringExtractorGDBRemote.h"

#ifdef __ANDROID__
#include "lldb/Host/android/HostInfoAndroid.h"
#endif

#include "llvm/ADT/StringSwitch.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

#ifdef __ANDROID__
const static uint32_t g_default_packet_timeout_sec = 20; // seconds
#else
const static uint32_t g_default_packet_timeout_sec = 0; // not specified
#endif

//----------------------------------------------------------------------
// GDBRemoteCommunicationServerCommon constructor
//----------------------------------------------------------------------
GDBRemoteCommunicationServerCommon::GDBRemoteCommunicationServerCommon(
    const char *comm_name, const char *listener_name)
    : GDBRemoteCommunicationServer(comm_name, listener_name),
      m_process_launch_info(), m_process_launch_error(), m_proc_infos(),
      m_proc_infos_index(0), m_thread_suffix_supported(false),
      m_list_threads_in_stop_reply(false) {
  RegisterMemberFunctionHandler(StringExtractorGDBRemote::eServerPacketType_A,
                                &GDBRemoteCommunicationServerCommon::Handle_A);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QEnvironment,
      &GDBRemoteCommunicationServerCommon::Handle_QEnvironment);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QEnvironmentHexEncoded,
      &GDBRemoteCommunicationServerCommon::Handle_QEnvironmentHexEncoded);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qfProcessInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qfProcessInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qGroupName,
      &GDBRemoteCommunicationServerCommon::Handle_qGroupName);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qHostInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qHostInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QLaunchArch,
      &GDBRemoteCommunicationServerCommon::Handle_QLaunchArch);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qLaunchSuccess,
      &GDBRemoteCommunicationServerCommon::Handle_qLaunchSuccess);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QListThreadsInStopReply,
      &GDBRemoteCommunicationServerCommon::Handle_QListThreadsInStopReply);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qEcho,
      &GDBRemoteCommunicationServerCommon::Handle_qEcho);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qModuleInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qModuleInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_jModulesInfo,
      &GDBRemoteCommunicationServerCommon::Handle_jModulesInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qPlatform_chmod,
      &GDBRemoteCommunicationServerCommon::Handle_qPlatform_chmod);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qPlatform_mkdir,
      &GDBRemoteCommunicationServerCommon::Handle_qPlatform_mkdir);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qPlatform_shell,
      &GDBRemoteCommunicationServerCommon::Handle_qPlatform_shell);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qProcessInfoPID,
      &GDBRemoteCommunicationServerCommon::Handle_qProcessInfoPID);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetDetachOnError,
      &GDBRemoteCommunicationServerCommon::Handle_QSetDetachOnError);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetSTDERR,
      &GDBRemoteCommunicationServerCommon::Handle_QSetSTDERR);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetSTDIN,
      &GDBRemoteCommunicationServerCommon::Handle_QSetSTDIN);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QSetSTDOUT,
      &GDBRemoteCommunicationServerCommon::Handle_QSetSTDOUT);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qSpeedTest,
      &GDBRemoteCommunicationServerCommon::Handle_qSpeedTest);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qsProcessInfo,
      &GDBRemoteCommunicationServerCommon::Handle_qsProcessInfo);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QStartNoAckMode,
      &GDBRemoteCommunicationServerCommon::Handle_QStartNoAckMode);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qSupported,
      &GDBRemoteCommunicationServerCommon::Handle_qSupported);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_QThreadSuffixSupported,
      &GDBRemoteCommunicationServerCommon::Handle_QThreadSuffixSupported);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_qUserName,
      &GDBRemoteCommunicationServerCommon::Handle_qUserName);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_close,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Close);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_exists,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Exists);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_md5,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_MD5);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_mode,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Mode);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_open,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Open);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_pread,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_pRead);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_pwrite,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_pWrite);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_size,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Size);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_stat,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_Stat);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_symlink,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_symlink);
  RegisterMemberFunctionHandler(
      StringExtractorGDBRemote::eServerPacketType_vFile_unlink,
      &GDBRemoteCommunicationServerCommon::Handle_vFile_unlink);
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
GDBRemoteCommunicationServerCommon::~GDBRemoteCommunicationServerCommon() {}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qHostInfo(
    StringExtractorGDBRemote &packet) {
  StreamString response;

  // $cputype:16777223;cpusubtype:3;ostype:Darwin;vendor:apple;endian:little;ptrsize:8;#00

  ArchSpec host_arch(HostInfo::GetArchitecture());
  const llvm::Triple &host_triple = host_arch.GetTriple();
  response.PutCString("triple:");
  response.PutCStringAsRawHex8(host_triple.getTriple().c_str());
  response.Printf(";ptrsize:%u;", host_arch.GetAddressByteSize());

  const char *distribution_id = host_arch.GetDistributionId().AsCString();
  if (distribution_id) {
    response.PutCString("distribution_id:");
    response.PutCStringAsRawHex8(distribution_id);
    response.PutCString(";");
  }

#if defined(__APPLE__)
  // For parity with debugserver, we'll include the vendor key.
  response.PutCString("vendor:apple;");

  // Send out MachO info.
  uint32_t cpu = host_arch.GetMachOCPUType();
  uint32_t sub = host_arch.GetMachOCPUSubType();
  if (cpu != LLDB_INVALID_CPUTYPE)
    response.Printf("cputype:%u;", cpu);
  if (sub != LLDB_INVALID_CPUTYPE)
    response.Printf("cpusubtype:%u;", sub);

  if (cpu == ArchSpec::kCore_arm_any) {
// Indicate the OS type.
#if defined(TARGET_OS_TV) && TARGET_OS_TV == 1
    response.PutCString("ostype:tvos;");
#elif defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
    response.PutCString("ostype:watchos;");
#else
    response.PutCString("ostype:ios;");
#endif

    // On arm, we use "synchronous" watchpoints which means the exception is
    // delivered before the instruction executes.
    response.PutCString("watchpoint_exceptions_received:before;");
  } else {
    response.PutCString("ostype:macosx;");
    response.Printf("watchpoint_exceptions_received:after;");
  }

#else
  if (host_arch.GetMachine() == llvm::Triple::aarch64 ||
      host_arch.GetMachine() == llvm::Triple::aarch64_be ||
      host_arch.GetMachine() == llvm::Triple::arm ||
      host_arch.GetMachine() == llvm::Triple::armeb ||
      host_arch.GetMachine() == llvm::Triple::mips64 ||
      host_arch.GetMachine() == llvm::Triple::mips64el ||
      host_arch.GetMachine() == llvm::Triple::mips ||
      host_arch.GetMachine() == llvm::Triple::mipsel)
    response.Printf("watchpoint_exceptions_received:before;");
  else
    response.Printf("watchpoint_exceptions_received:after;");
#endif

  switch (endian::InlHostByteOrder()) {
  case eByteOrderBig:
    response.PutCString("endian:big;");
    break;
  case eByteOrderLittle:
    response.PutCString("endian:little;");
    break;
  case eByteOrderPDP:
    response.PutCString("endian:pdp;");
    break;
  default:
    response.PutCString("endian:unknown;");
    break;
  }

  uint32_t major = UINT32_MAX;
  uint32_t minor = UINT32_MAX;
  uint32_t update = UINT32_MAX;
  if (HostInfo::GetOSVersion(major, minor, update)) {
    if (major != UINT32_MAX) {
      response.Printf("os_version:%u", major);
      if (minor != UINT32_MAX) {
        response.Printf(".%u", minor);
        if (update != UINT32_MAX)
          response.Printf(".%u", update);
      }
      response.PutChar(';');
    }
  }

  std::string s;
  if (HostInfo::GetOSBuildString(s)) {
    response.PutCString("os_build:");
    response.PutCStringAsRawHex8(s.c_str());
    response.PutChar(';');
  }
  if (HostInfo::GetOSKernelDescription(s)) {
    response.PutCString("os_kernel:");
    response.PutCStringAsRawHex8(s.c_str());
    response.PutChar(';');
  }

#if defined(__APPLE__)

#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  // For iOS devices, we are connected through a USB Mux so we never pretend
  // to actually have a hostname as far as the remote lldb that is connecting
  // to this lldb-platform is concerned
  response.PutCString("hostname:");
  response.PutCStringAsRawHex8("127.0.0.1");
  response.PutChar(';');
#else  // #if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  if (HostInfo::GetHostname(s)) {
    response.PutCString("hostname:");
    response.PutCStringAsRawHex8(s.c_str());
    response.PutChar(';');
  }
#endif // #if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)

#else  // #if defined(__APPLE__)
  if (HostInfo::GetHostname(s)) {
    response.PutCString("hostname:");
    response.PutCStringAsRawHex8(s.c_str());
    response.PutChar(';');
  }
#endif // #if defined(__APPLE__)

  if (g_default_packet_timeout_sec > 0)
    response.Printf("default_packet_timeout:%u;", g_default_packet_timeout_sec);

  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qProcessInfoPID(
    StringExtractorGDBRemote &packet) {
  // Packet format: "qProcessInfoPID:%i" where %i is the pid
  packet.SetFilePos(::strlen("qProcessInfoPID:"));
  lldb::pid_t pid = packet.GetU32(LLDB_INVALID_PROCESS_ID);
  if (pid != LLDB_INVALID_PROCESS_ID) {
    ProcessInstanceInfo proc_info;
    if (Host::GetProcessInfo(pid, proc_info)) {
      StreamString response;
      CreateProcessInfoResponse(proc_info, response);
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(1);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qfProcessInfo(
    StringExtractorGDBRemote &packet) {
  m_proc_infos_index = 0;
  m_proc_infos.Clear();

  ProcessInstanceInfoMatch match_info;
  packet.SetFilePos(::strlen("qfProcessInfo"));
  if (packet.GetChar() == ':') {
    llvm::StringRef key;
    llvm::StringRef value;
    while (packet.GetNameColonValue(key, value)) {
      bool success = true;
      if (key.equals("name")) {
        StringExtractor extractor(value);
        std::string file;
        extractor.GetHexByteString(file);
        match_info.GetProcessInfo().GetExecutableFile().SetFile(file, false);
      } else if (key.equals("name_match")) {
        NameMatch name_match = llvm::StringSwitch<NameMatch>(value)
                                   .Case("equals", NameMatch::Equals)
                                   .Case("starts_with", NameMatch::StartsWith)
                                   .Case("ends_with", NameMatch::EndsWith)
                                   .Case("contains", NameMatch::Contains)
                                   .Case("regex", NameMatch::RegularExpression)
                                   .Default(NameMatch::Ignore);
        match_info.SetNameMatchType(name_match);
        if (name_match == NameMatch::Ignore)
          return SendErrorResponse(2);
      } else if (key.equals("pid")) {
        lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
        if (value.getAsInteger(0, pid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetProcessID(pid);
      } else if (key.equals("parent_pid")) {
        lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
        if (value.getAsInteger(0, pid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetParentProcessID(pid);
      } else if (key.equals("uid")) {
        uint32_t uid = UINT32_MAX;
        if (value.getAsInteger(0, uid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetUserID(uid);
      } else if (key.equals("gid")) {
        uint32_t gid = UINT32_MAX;
        if (value.getAsInteger(0, gid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetGroupID(gid);
      } else if (key.equals("euid")) {
        uint32_t uid = UINT32_MAX;
        if (value.getAsInteger(0, uid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetEffectiveUserID(uid);
      } else if (key.equals("egid")) {
        uint32_t gid = UINT32_MAX;
        if (value.getAsInteger(0, gid))
          return SendErrorResponse(2);
        match_info.GetProcessInfo().SetEffectiveGroupID(gid);
      } else if (key.equals("all_users")) {
        match_info.SetMatchAllUsers(
            Args::StringToBoolean(value, false, &success));
      } else if (key.equals("triple")) {
        match_info.GetProcessInfo().GetArchitecture().SetTriple(
            value.str().c_str(), NULL);
      } else {
        success = false;
      }

      if (!success)
        return SendErrorResponse(2);
    }
  }

  if (Host::FindProcesses(match_info, m_proc_infos)) {
    // We found something, return the first item by calling the get
    // subsequent process info packet handler...
    return Handle_qsProcessInfo(packet);
  }
  return SendErrorResponse(3);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qsProcessInfo(
    StringExtractorGDBRemote &packet) {
  if (m_proc_infos_index < m_proc_infos.GetSize()) {
    StreamString response;
    CreateProcessInfoResponse(
        m_proc_infos.GetProcessInfoAtIndex(m_proc_infos_index), response);
    ++m_proc_infos_index;
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(4);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qUserName(
    StringExtractorGDBRemote &packet) {
#if !defined(LLDB_DISABLE_POSIX)
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("GDBRemoteCommunicationServerCommon::%s begin", __FUNCTION__);

  // Packet format: "qUserName:%i" where %i is the uid
  packet.SetFilePos(::strlen("qUserName:"));
  uint32_t uid = packet.GetU32(UINT32_MAX);
  if (uid != UINT32_MAX) {
    std::string name;
    if (HostInfo::LookupUserName(uid, name)) {
      StreamString response;
      response.PutCStringAsRawHex8(name.c_str());
      return SendPacketNoLock(response.GetString());
    }
  }
  if (log)
    log->Printf("GDBRemoteCommunicationServerCommon::%s end", __FUNCTION__);
#endif
  return SendErrorResponse(5);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qGroupName(
    StringExtractorGDBRemote &packet) {
#if !defined(LLDB_DISABLE_POSIX)
  // Packet format: "qGroupName:%i" where %i is the gid
  packet.SetFilePos(::strlen("qGroupName:"));
  uint32_t gid = packet.GetU32(UINT32_MAX);
  if (gid != UINT32_MAX) {
    std::string name;
    if (HostInfo::LookupGroupName(gid, name)) {
      StreamString response;
      response.PutCStringAsRawHex8(name.c_str());
      return SendPacketNoLock(response.GetString());
    }
  }
#endif
  return SendErrorResponse(6);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qSpeedTest(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qSpeedTest:"));

  llvm::StringRef key;
  llvm::StringRef value;
  bool success = packet.GetNameColonValue(key, value);
  if (success && key.equals("response_size")) {
    uint32_t response_size = 0;
    if (!value.getAsInteger(0, response_size)) {
      if (response_size == 0)
        return SendOKResponse();
      StreamString response;
      uint32_t bytes_left = response_size;
      response.PutCString("data:");
      while (bytes_left > 0) {
        if (bytes_left >= 26) {
          response.PutCString("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
          bytes_left -= 26;
        } else {
          response.Printf("%*.*s;", bytes_left, bytes_left,
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
          bytes_left = 0;
        }
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(7);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Open(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:open:"));
  std::string path;
  packet.GetHexByteStringTerminatedBy(path, ',');
  if (!path.empty()) {
    if (packet.GetChar() == ',') {
      uint32_t flags =
          File::ConvertOpenOptionsForPOSIXOpen(packet.GetHexMaxU32(false, 0));
      if (packet.GetChar() == ',') {
        mode_t mode = packet.GetHexMaxU32(false, 0600);
        Error error;
        const FileSpec path_spec{path, true};
        int fd = ::open(path_spec.GetCString(), flags, mode);
        const int save_errno = fd == -1 ? errno : 0;
        StreamString response;
        response.PutChar('F');
        response.Printf("%i", fd);
        if (save_errno)
          response.Printf(",%i", save_errno);
        return SendPacketNoLock(response.GetString());
      }
    }
  }
  return SendErrorResponse(18);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Close(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:close:"));
  int fd = packet.GetS32(-1);
  Error error;
  int err = -1;
  int save_errno = 0;
  if (fd >= 0) {
    err = close(fd);
    save_errno = err == -1 ? errno : 0;
  } else {
    save_errno = EINVAL;
  }
  StreamString response;
  response.PutChar('F');
  response.Printf("%i", err);
  if (save_errno)
    response.Printf(",%i", save_errno);
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_pRead(
    StringExtractorGDBRemote &packet) {
#ifdef _WIN32
  // Not implemented on Windows
  return SendUnimplementedResponse(
      "GDBRemoteCommunicationServerCommon::Handle_vFile_pRead() unimplemented");
#else
  StreamGDBRemote response;
  packet.SetFilePos(::strlen("vFile:pread:"));
  int fd = packet.GetS32(-1);
  if (packet.GetChar() == ',') {
    uint64_t count = packet.GetU64(UINT64_MAX);
    if (packet.GetChar() == ',') {
      uint64_t offset = packet.GetU64(UINT32_MAX);
      if (count == UINT64_MAX) {
        response.Printf("F-1:%i", EINVAL);
        return SendPacketNoLock(response.GetString());
      }

      std::string buffer(count, 0);
      const ssize_t bytes_read = ::pread(fd, &buffer[0], buffer.size(), offset);
      const int save_errno = bytes_read == -1 ? errno : 0;
      response.PutChar('F');
      response.Printf("%zi", bytes_read);
      if (save_errno)
        response.Printf(",%i", save_errno);
      else {
        response.PutChar(';');
        response.PutEscapedBytes(&buffer[0], bytes_read);
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(21);

#endif
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_pWrite(
    StringExtractorGDBRemote &packet) {
#ifdef _WIN32
  return SendUnimplementedResponse("GDBRemoteCommunicationServerCommon::Handle_"
                                   "vFile_pWrite() unimplemented");
#else
  packet.SetFilePos(::strlen("vFile:pwrite:"));

  StreamGDBRemote response;
  response.PutChar('F');

  int fd = packet.GetU32(UINT32_MAX);
  if (packet.GetChar() == ',') {
    off_t offset = packet.GetU64(UINT32_MAX);
    if (packet.GetChar() == ',') {
      std::string buffer;
      if (packet.GetEscapedBinaryData(buffer)) {
        const ssize_t bytes_written =
            ::pwrite(fd, buffer.data(), buffer.size(), offset);
        const int save_errno = bytes_written == -1 ? errno : 0;
        response.Printf("%zi", bytes_written);
        if (save_errno)
          response.Printf(",%i", save_errno);
      } else {
        response.Printf("-1,%i", EINVAL);
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(27);
#endif
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Size(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:size:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    uint64_t Size;
    if (llvm::sys::fs::file_size(path, Size))
      return SendErrorResponse(5);
    StreamString response;
    response.PutChar('F');
    response.PutHex64(Size);
    if (Size == UINT64_MAX) {
      response.PutChar(',');
      response.PutHex64(Size); // TODO: replace with Host::GetSyswideErrorCode()
    }
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(22);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Mode(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:mode:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    Error error;
    const uint32_t mode = File::GetPermissions(FileSpec{path, true}, error);
    StreamString response;
    response.Printf("F%u", mode);
    if (mode == 0 || error.Fail())
      response.Printf(",%i", (int)error.GetError());
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(23);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Exists(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:exists:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    bool retcode = llvm::sys::fs::exists(path);
    StreamString response;
    response.PutChar('F');
    response.PutChar(',');
    if (retcode)
      response.PutChar('1');
    else
      response.PutChar('0');
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(24);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_symlink(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:symlink:"));
  std::string dst, src;
  packet.GetHexByteStringTerminatedBy(dst, ',');
  packet.GetChar(); // Skip ',' char
  packet.GetHexByteString(src);
  Error error = FileSystem::Symlink(FileSpec{src, true}, FileSpec{dst, false});
  StreamString response;
  response.Printf("F%u,%u", error.GetError(), error.GetError());
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_unlink(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:unlink:"));
  std::string path;
  packet.GetHexByteString(path);
  Error error(llvm::sys::fs::remove(path));
  StreamString response;
  response.Printf("F%u,%u", error.GetError(), error.GetError());
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qPlatform_shell(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qPlatform_shell:"));
  std::string path;
  std::string working_dir;
  packet.GetHexByteStringTerminatedBy(path, ',');
  if (!path.empty()) {
    if (packet.GetChar() == ',') {
      // FIXME: add timeout to qPlatform_shell packet
      // uint32_t timeout = packet.GetHexMaxU32(false, 32);
      uint32_t timeout = 10;
      if (packet.GetChar() == ',')
        packet.GetHexByteString(working_dir);
      int status, signo;
      std::string output;
      Error err =
          Host::RunShellCommand(path.c_str(), FileSpec{working_dir, true},
                                &status, &signo, &output, timeout);
      StreamGDBRemote response;
      if (err.Fail()) {
        response.PutCString("F,");
        response.PutHex32(UINT32_MAX);
      } else {
        response.PutCString("F,");
        response.PutHex32(status);
        response.PutChar(',');
        response.PutHex32(signo);
        response.PutChar(',');
        response.PutEscapedBytes(output.c_str(), output.size());
      }
      return SendPacketNoLock(response.GetString());
    }
  }
  return SendErrorResponse(24);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_Stat(
    StringExtractorGDBRemote &packet) {
  return SendUnimplementedResponse(
      "GDBRemoteCommunicationServerCommon::Handle_vFile_Stat() unimplemented");
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_vFile_MD5(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("vFile:MD5:"));
  std::string path;
  packet.GetHexByteString(path);
  if (!path.empty()) {
    StreamGDBRemote response;
    auto Result = llvm::sys::fs::md5_contents(path);
    if (!Result) {
      response.PutCString("F,");
      response.PutCString("x");
    } else {
      response.PutCString("F,");
      response.PutHex64(Result->low());
      response.PutHex64(Result->high());
    }
    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(25);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qPlatform_mkdir(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qPlatform_mkdir:"));
  mode_t mode = packet.GetHexMaxU32(false, UINT32_MAX);
  if (packet.GetChar() == ',') {
    std::string path;
    packet.GetHexByteString(path);
    Error error(llvm::sys::fs::create_directory(path, mode));

    StreamGDBRemote response;
    response.Printf("F%u", error.GetError());

    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(20);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qPlatform_chmod(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qPlatform_chmod:"));

  auto perms =
      static_cast<llvm::sys::fs::perms>(packet.GetHexMaxU32(false, UINT32_MAX));
  if (packet.GetChar() == ',') {
    std::string path;
    packet.GetHexByteString(path);
    Error error(llvm::sys::fs::setPermissions(path, perms));

    StreamGDBRemote response;
    response.Printf("F%u", error.GetError());

    return SendPacketNoLock(response.GetString());
  }
  return SendErrorResponse(19);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qSupported(
    StringExtractorGDBRemote &packet) {
  StreamGDBRemote response;

  // Features common to lldb-platform and llgs.
  uint32_t max_packet_size = 128 * 1024; // 128KBytes is a reasonable max packet
                                         // size--debugger can always use less
  response.Printf("PacketSize=%x", max_packet_size);

  response.PutCString(";QStartNoAckMode+");
  response.PutCString(";QThreadSuffixSupported+");
  response.PutCString(";QListThreadsInStopReply+");
  response.PutCString(";qEcho+");
#if defined(__linux__) || defined(__NetBSD__)
  response.PutCString(";QPassSignals+");
  response.PutCString(";qXfer:auxv:read+");
#endif

  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QThreadSuffixSupported(
    StringExtractorGDBRemote &packet) {
  m_thread_suffix_supported = true;
  return SendOKResponse();
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QListThreadsInStopReply(
    StringExtractorGDBRemote &packet) {
  m_list_threads_in_stop_reply = true;
  return SendOKResponse();
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetDetachOnError(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetDetachOnError:"));
  if (packet.GetU32(0))
    m_process_launch_info.GetFlags().Set(eLaunchFlagDetachOnError);
  else
    m_process_launch_info.GetFlags().Clear(eLaunchFlagDetachOnError);
  return SendOKResponse();
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QStartNoAckMode(
    StringExtractorGDBRemote &packet) {
  // Send response first before changing m_send_acks to we ack this packet
  PacketResult packet_result = SendOKResponse();
  m_send_acks = false;
  return packet_result;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetSTDIN(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetSTDIN:"));
  FileAction file_action;
  std::string path;
  packet.GetHexByteString(path);
  const bool read = true;
  const bool write = false;
  if (file_action.Open(STDIN_FILENO, FileSpec{path, false}, read, write)) {
    m_process_launch_info.AppendFileAction(file_action);
    return SendOKResponse();
  }
  return SendErrorResponse(15);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetSTDOUT(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetSTDOUT:"));
  FileAction file_action;
  std::string path;
  packet.GetHexByteString(path);
  const bool read = false;
  const bool write = true;
  if (file_action.Open(STDOUT_FILENO, FileSpec{path, false}, read, write)) {
    m_process_launch_info.AppendFileAction(file_action);
    return SendOKResponse();
  }
  return SendErrorResponse(16);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QSetSTDERR(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QSetSTDERR:"));
  FileAction file_action;
  std::string path;
  packet.GetHexByteString(path);
  const bool read = false;
  const bool write = true;
  if (file_action.Open(STDERR_FILENO, FileSpec{path, false}, read, write)) {
    m_process_launch_info.AppendFileAction(file_action);
    return SendOKResponse();
  }
  return SendErrorResponse(17);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qLaunchSuccess(
    StringExtractorGDBRemote &packet) {
  if (m_process_launch_error.Success())
    return SendOKResponse();
  StreamString response;
  response.PutChar('E');
  response.PutCString(m_process_launch_error.AsCString("<unknown error>"));
  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QEnvironment(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QEnvironment:"));
  const uint32_t bytes_left = packet.GetBytesLeft();
  if (bytes_left > 0) {
    m_process_launch_info.GetEnvironmentEntries().AppendArgument(
        llvm::StringRef::withNullAsEmpty(packet.Peek()));
    return SendOKResponse();
  }
  return SendErrorResponse(12);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QEnvironmentHexEncoded(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QEnvironmentHexEncoded:"));
  const uint32_t bytes_left = packet.GetBytesLeft();
  if (bytes_left > 0) {
    std::string str;
    packet.GetHexByteString(str);
    m_process_launch_info.GetEnvironmentEntries().AppendArgument(str);
    return SendOKResponse();
  }
  return SendErrorResponse(12);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_QLaunchArch(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("QLaunchArch:"));
  const uint32_t bytes_left = packet.GetBytesLeft();
  if (bytes_left > 0) {
    const char *arch_triple = packet.Peek();
    ArchSpec arch_spec(arch_triple, NULL);
    m_process_launch_info.SetArchitecture(arch_spec);
    return SendOKResponse();
  }
  return SendErrorResponse(13);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_A(StringExtractorGDBRemote &packet) {
  // The 'A' packet is the most over designed packet ever here with
  // redundant argument indexes, redundant argument lengths and needed hex
  // encoded argument string values. Really all that is needed is a comma
  // separated hex encoded argument value list, but we will stay true to the
  // documented version of the 'A' packet here...

  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PROCESS));
  int actual_arg_index = 0;

  packet.SetFilePos(1); // Skip the 'A'
  bool success = true;
  while (success && packet.GetBytesLeft() > 0) {
    // Decode the decimal argument string length. This length is the
    // number of hex nibbles in the argument string value.
    const uint32_t arg_len = packet.GetU32(UINT32_MAX);
    if (arg_len == UINT32_MAX)
      success = false;
    else {
      // Make sure the argument hex string length is followed by a comma
      if (packet.GetChar() != ',')
        success = false;
      else {
        // Decode the argument index. We ignore this really because
        // who would really send down the arguments in a random order???
        const uint32_t arg_idx = packet.GetU32(UINT32_MAX);
        if (arg_idx == UINT32_MAX)
          success = false;
        else {
          // Make sure the argument index is followed by a comma
          if (packet.GetChar() != ',')
            success = false;
          else {
            // Decode the argument string value from hex bytes
            // back into a UTF8 string and make sure the length
            // matches the one supplied in the packet
            std::string arg;
            if (packet.GetHexByteStringFixedLength(arg, arg_len) !=
                (arg_len / 2))
              success = false;
            else {
              // If there are any bytes left
              if (packet.GetBytesLeft()) {
                if (packet.GetChar() != ',')
                  success = false;
              }

              if (success) {
                if (arg_idx == 0)
                  m_process_launch_info.GetExecutableFile().SetFile(arg, false);
                m_process_launch_info.GetArguments().AppendArgument(arg);
                if (log)
                  log->Printf("LLGSPacketHandler::%s added arg %d: \"%s\"",
                              __FUNCTION__, actual_arg_index, arg.c_str());
                ++actual_arg_index;
              }
            }
          }
        }
      }
    }
  }

  if (success) {
    m_process_launch_error = LaunchProcess();
    if (m_process_launch_info.GetProcessID() != LLDB_INVALID_PROCESS_ID) {
      return SendOKResponse();
    } else {
      Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PROCESS));
      if (log)
        log->Printf("LLGSPacketHandler::%s failed to launch exe: %s",
                    __FUNCTION__, m_process_launch_error.AsCString());
    }
  }
  return SendErrorResponse(8);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qEcho(
    StringExtractorGDBRemote &packet) {
  // Just echo back the exact same packet for qEcho...
  return SendPacketNoLock(packet.GetStringRef());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_qModuleInfo(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("qModuleInfo:"));

  std::string module_path;
  packet.GetHexByteStringTerminatedBy(module_path, ';');
  if (module_path.empty())
    return SendErrorResponse(1);

  if (packet.GetChar() != ';')
    return SendErrorResponse(2);

  std::string triple;
  packet.GetHexByteString(triple);

  ModuleSpec matched_module_spec = GetModuleInfo(module_path, triple);
  if (!matched_module_spec.GetFileSpec())
    return SendErrorResponse(3);

  const auto file_offset = matched_module_spec.GetObjectOffset();
  const auto file_size = matched_module_spec.GetObjectSize();
  const auto uuid_str = matched_module_spec.GetUUID().GetAsString("");

  StreamGDBRemote response;

  if (uuid_str.empty()) {
    auto Result = llvm::sys::fs::md5_contents(matched_module_spec.GetFileSpec().GetPath());
    if (!Result)
      return SendErrorResponse(5);
    response.PutCString("md5:");
    response.PutCStringAsRawHex8(Result->digest().c_str());
  } else {
    response.PutCString("uuid:");
    response.PutCStringAsRawHex8(uuid_str.c_str());
  }
  response.PutChar(';');

  const auto &module_arch = matched_module_spec.GetArchitecture();
  response.PutCString("triple:");
  response.PutCStringAsRawHex8(module_arch.GetTriple().getTriple().c_str());
  response.PutChar(';');

  response.PutCString("file_path:");
  response.PutCStringAsRawHex8(matched_module_spec.GetFileSpec().GetCString());
  response.PutChar(';');
  response.PutCString("file_offset:");
  response.PutHex64(file_offset);
  response.PutChar(';');
  response.PutCString("file_size:");
  response.PutHex64(file_size);
  response.PutChar(';');

  return SendPacketNoLock(response.GetString());
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationServerCommon::Handle_jModulesInfo(
    StringExtractorGDBRemote &packet) {
  packet.SetFilePos(::strlen("jModulesInfo:"));

  StructuredData::ObjectSP object_sp = StructuredData::ParseJSON(packet.Peek());
  if (!object_sp)
    return SendErrorResponse(1);

  StructuredData::Array *packet_array = object_sp->GetAsArray();
  if (!packet_array)
    return SendErrorResponse(2);

  JSONArray::SP response_array_sp = std::make_shared<JSONArray>();
  for (size_t i = 0; i < packet_array->GetSize(); ++i) {
    StructuredData::Dictionary *query =
        packet_array->GetItemAtIndex(i)->GetAsDictionary();
    if (!query)
      continue;
    std::string file, triple;
    if (!query->GetValueForKeyAsString("file", file) ||
        !query->GetValueForKeyAsString("triple", triple))
      continue;

    ModuleSpec matched_module_spec = GetModuleInfo(file, triple);
    if (!matched_module_spec.GetFileSpec())
      continue;

    const auto file_offset = matched_module_spec.GetObjectOffset();
    const auto file_size = matched_module_spec.GetObjectSize();
    const auto uuid_str = matched_module_spec.GetUUID().GetAsString("");

    if (uuid_str.empty())
      continue;

    JSONObject::SP response = std::make_shared<JSONObject>();
    response_array_sp->AppendObject(response);
    response->SetObject("uuid", std::make_shared<JSONString>(uuid_str));
    response->SetObject(
        "triple",
        std::make_shared<JSONString>(
            matched_module_spec.GetArchitecture().GetTriple().getTriple()));
    response->SetObject("file_path",
                        std::make_shared<JSONString>(
                            matched_module_spec.GetFileSpec().GetPath()));
    response->SetObject("file_offset",
                        std::make_shared<JSONNumber>(file_offset));
    response->SetObject("file_size", std::make_shared<JSONNumber>(file_size));
  }

  StreamString response;
  response_array_sp->Write(response);
  StreamGDBRemote escaped_response;
  escaped_response.PutEscapedBytes(response.GetString().data(),
                                   response.GetSize());
  return SendPacketNoLock(escaped_response.GetString());
}

void GDBRemoteCommunicationServerCommon::CreateProcessInfoResponse(
    const ProcessInstanceInfo &proc_info, StreamString &response) {
  response.Printf(
      "pid:%" PRIu64 ";ppid:%" PRIu64 ";uid:%i;gid:%i;euid:%i;egid:%i;",
      proc_info.GetProcessID(), proc_info.GetParentProcessID(),
      proc_info.GetUserID(), proc_info.GetGroupID(),
      proc_info.GetEffectiveUserID(), proc_info.GetEffectiveGroupID());
  response.PutCString("name:");
  response.PutCStringAsRawHex8(proc_info.GetExecutableFile().GetCString());
  response.PutChar(';');
  const ArchSpec &proc_arch = proc_info.GetArchitecture();
  if (proc_arch.IsValid()) {
    const llvm::Triple &proc_triple = proc_arch.GetTriple();
    response.PutCString("triple:");
    response.PutCStringAsRawHex8(proc_triple.getTriple().c_str());
    response.PutChar(';');
  }
}

void GDBRemoteCommunicationServerCommon::
    CreateProcessInfoResponse_DebugServerStyle(
        const ProcessInstanceInfo &proc_info, StreamString &response) {
  response.Printf("pid:%" PRIx64 ";parent-pid:%" PRIx64
                  ";real-uid:%x;real-gid:%x;effective-uid:%x;effective-gid:%x;",
                  proc_info.GetProcessID(), proc_info.GetParentProcessID(),
                  proc_info.GetUserID(), proc_info.GetGroupID(),
                  proc_info.GetEffectiveUserID(),
                  proc_info.GetEffectiveGroupID());

  const ArchSpec &proc_arch = proc_info.GetArchitecture();
  if (proc_arch.IsValid()) {
    const llvm::Triple &proc_triple = proc_arch.GetTriple();
#if defined(__APPLE__)
    // We'll send cputype/cpusubtype.
    const uint32_t cpu_type = proc_arch.GetMachOCPUType();
    if (cpu_type != 0)
      response.Printf("cputype:%" PRIx32 ";", cpu_type);

    const uint32_t cpu_subtype = proc_arch.GetMachOCPUSubType();
    if (cpu_subtype != 0)
      response.Printf("cpusubtype:%" PRIx32 ";", cpu_subtype);

    const std::string vendor = proc_triple.getVendorName();
    if (!vendor.empty())
      response.Printf("vendor:%s;", vendor.c_str());
#else
    // We'll send the triple.
    response.PutCString("triple:");
    response.PutCStringAsRawHex8(proc_triple.getTriple().c_str());
    response.PutChar(';');
#endif
    std::string ostype = proc_triple.getOSName();
    // Adjust so ostype reports ios for Apple/ARM and Apple/ARM64.
    if (proc_triple.getVendor() == llvm::Triple::Apple) {
      switch (proc_triple.getArch()) {
      case llvm::Triple::arm:
      case llvm::Triple::thumb:
      case llvm::Triple::aarch64:
        ostype = "ios";
        break;
      default:
        // No change.
        break;
      }
    }
    response.Printf("ostype:%s;", ostype.c_str());

    switch (proc_arch.GetByteOrder()) {
    case lldb::eByteOrderLittle:
      response.PutCString("endian:little;");
      break;
    case lldb::eByteOrderBig:
      response.PutCString("endian:big;");
      break;
    case lldb::eByteOrderPDP:
      response.PutCString("endian:pdp;");
      break;
    default:
      // Nothing.
      break;
    }
    // In case of MIPS64, pointer size is depend on ELF ABI
    // For N32 the pointer size is 4 and for N64 it is 8
    std::string abi = proc_arch.GetTargetABI();
    if (!abi.empty())
      response.Printf("elf_abi:%s;", abi.c_str());
    response.Printf("ptrsize:%d;", proc_arch.GetAddressByteSize());
  }
}

FileSpec GDBRemoteCommunicationServerCommon::FindModuleFile(
    const std::string &module_path, const ArchSpec &arch) {
#ifdef __ANDROID__
  return HostInfoAndroid::ResolveLibraryPath(module_path, arch);
#else
  return FileSpec(module_path, true);
#endif
}

ModuleSpec GDBRemoteCommunicationServerCommon::GetModuleInfo(
    const std::string &module_path, const std::string &triple) {
  ArchSpec arch(triple.c_str());

  const FileSpec req_module_path_spec(module_path, true);
  const FileSpec module_path_spec =
      FindModuleFile(req_module_path_spec.GetPath(), arch);
  const ModuleSpec module_spec(module_path_spec, arch);

  ModuleSpecList module_specs;
  if (!ObjectFile::GetModuleSpecifications(module_path_spec, 0, 0,
                                           module_specs))
    return ModuleSpec();

  ModuleSpec matched_module_spec;
  if (!module_specs.FindMatchingModuleSpec(module_spec, matched_module_spec))
    return ModuleSpec();

  return matched_module_spec;
}
