//===-- SBTarget.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTarget.h"
#include "SBReproducerPrivate.h"

#include "lldb/lldb-public.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEnvironment.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBModuleSpec.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBSourceManager.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBSymbolContextList.h"
#include "lldb/API/SBTrace.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/AddressResolver.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/DeclVendor.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/RegularExpression.h"

#include "Commands/CommandObjectBreakpoint.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Regex.h"

// BEGIN SWIFT
#include "lldb/Breakpoint/BreakpointPrecondition.h"
// END SWIFT

using namespace lldb;
using namespace lldb_private;

#define DEFAULT_DISASM_BYTE_SIZE 32

static Status AttachToProcess(ProcessAttachInfo &attach_info, Target &target) {
  std::lock_guard<std::recursive_mutex> guard(target.GetAPIMutex());

  auto process_sp = target.GetProcessSP();
  if (process_sp) {
    const auto state = process_sp->GetState();
    if (process_sp->IsAlive() && state == eStateConnected) {
      // If we are already connected, then we have already specified the
      // listener, so if a valid listener is supplied, we need to error out to
      // let the client know.
      if (attach_info.GetListener())
        return Status("process is connected and already has a listener, pass "
                      "empty listener");
    }
  }

  return target.Attach(attach_info, nullptr);
}

// SBTarget constructor
SBTarget::SBTarget() : m_opaque_sp() {
  LLDB_RECORD_CONSTRUCTOR_NO_ARGS(SBTarget);
}

SBTarget::SBTarget(const SBTarget &rhs) : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_RECORD_CONSTRUCTOR(SBTarget, (const lldb::SBTarget &), rhs);
}

SBTarget::SBTarget(const TargetSP &target_sp) : m_opaque_sp(target_sp) {
  LLDB_RECORD_CONSTRUCTOR(SBTarget, (const lldb::TargetSP &), target_sp);
}

const SBTarget &SBTarget::operator=(const SBTarget &rhs) {
  LLDB_RECORD_METHOD(const lldb::SBTarget &,
                     SBTarget, operator=,(const lldb::SBTarget &), rhs);

  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return LLDB_RECORD_RESULT(*this);
}

// Destructor
SBTarget::~SBTarget() = default;

bool SBTarget::EventIsTargetEvent(const SBEvent &event) {
  LLDB_RECORD_STATIC_METHOD(bool, SBTarget, EventIsTargetEvent,
                            (const lldb::SBEvent &), event);

  return Target::TargetEventData::GetEventDataFromEvent(event.get()) != nullptr;
}

SBTarget SBTarget::GetTargetFromEvent(const SBEvent &event) {
  LLDB_RECORD_STATIC_METHOD(lldb::SBTarget, SBTarget, GetTargetFromEvent,
                            (const lldb::SBEvent &), event);

  return LLDB_RECORD_RESULT(
      Target::TargetEventData::GetTargetFromEvent(event.get()));
}

uint32_t SBTarget::GetNumModulesFromEvent(const SBEvent &event) {
  LLDB_RECORD_STATIC_METHOD(uint32_t, SBTarget, GetNumModulesFromEvent,
                            (const lldb::SBEvent &), event);

  const ModuleList module_list =
      Target::TargetEventData::GetModuleListFromEvent(event.get());
  return module_list.GetSize();
}

SBModule SBTarget::GetModuleAtIndexFromEvent(const uint32_t idx,
                                             const SBEvent &event) {
  LLDB_RECORD_STATIC_METHOD(lldb::SBModule, SBTarget, GetModuleAtIndexFromEvent,
                            (const uint32_t, const lldb::SBEvent &), idx,
                            event);

  const ModuleList module_list =
      Target::TargetEventData::GetModuleListFromEvent(event.get());
  return LLDB_RECORD_RESULT(SBModule(module_list.GetModuleAtIndex(idx)));
}

const char *SBTarget::GetBroadcasterClassName() {
  LLDB_RECORD_STATIC_METHOD_NO_ARGS(const char *, SBTarget,
                                    GetBroadcasterClassName);

  return Target::GetStaticBroadcasterClass().AsCString();
}

bool SBTarget::IsValid() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(bool, SBTarget, IsValid);
  return this->operator bool();
}
SBTarget::operator bool() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(bool, SBTarget, operator bool);

  return m_opaque_sp.get() != nullptr && m_opaque_sp->IsValid();
}

SBProcess SBTarget::GetProcess() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBProcess, SBTarget, GetProcess);

  SBProcess sb_process;
  ProcessSP process_sp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    process_sp = target_sp->GetProcessSP();
    sb_process.SetSP(process_sp);
  }

  return LLDB_RECORD_RESULT(sb_process);
}

SBPlatform SBTarget::GetPlatform() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBPlatform, SBTarget, GetPlatform);

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return LLDB_RECORD_RESULT(SBPlatform());

  SBPlatform platform;
  platform.m_opaque_sp = target_sp->GetPlatform();

  return LLDB_RECORD_RESULT(platform);
}

SBDebugger SBTarget::GetDebugger() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(lldb::SBDebugger, SBTarget, GetDebugger);

  SBDebugger debugger;
  TargetSP target_sp(GetSP());
  if (target_sp)
    debugger.reset(target_sp->GetDebugger().shared_from_this());
  return LLDB_RECORD_RESULT(debugger);
}

SBStructuredData SBTarget::GetStatistics() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBStructuredData, SBTarget, GetStatistics);

  SBStructuredData data;
  TargetSP target_sp(GetSP());
  if (!target_sp)
    return LLDB_RECORD_RESULT(data);
  std::string json_str =
      llvm::formatv("{0:2}",
          DebuggerStats::ReportStatistics(target_sp->GetDebugger(),
                                          target_sp.get())).str();
  data.m_impl_up->SetObjectSP(StructuredData::ParseJSON(json_str));
  return LLDB_RECORD_RESULT(data);
}

void SBTarget::SetCollectingStats(bool v) {
  LLDB_RECORD_METHOD(void, SBTarget, SetCollectingStats, (bool), v);

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return;
  return DebuggerStats::SetCollectingStats(v);
}

bool SBTarget::GetCollectingStats() {
  LLDB_RECORD_METHOD_NO_ARGS(bool, SBTarget, GetCollectingStats);

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return false;
  return DebuggerStats::GetCollectingStats();
}

SBProcess SBTarget::LoadCore(const char *core_file) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, LoadCore, (const char *),
                     core_file);

  lldb::SBError error; // Ignored
  return LLDB_RECORD_RESULT(LoadCore(core_file, error));
}

SBProcess SBTarget::LoadCore(const char *core_file, lldb::SBError &error) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, LoadCore,
                     (const char *, lldb::SBError &), core_file, error);

  SBProcess sb_process;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    FileSpec filespec(core_file);
    FileSystem::Instance().Resolve(filespec);
    ProcessSP process_sp(target_sp->CreateProcess(
        target_sp->GetDebugger().GetListener(), "", &filespec, false));
    if (process_sp) {
      error.SetError(process_sp->LoadCore());
      if (error.Success())
        sb_process.SetSP(process_sp);
    } else {
      error.SetErrorString("Failed to create the process");
    }
  } else {
    error.SetErrorString("SBTarget is invalid");
  }
  return LLDB_RECORD_RESULT(sb_process);
}

SBProcess SBTarget::LaunchSimple(char const **argv, char const **envp,
                                 const char *working_directory) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, LaunchSimple,
                     (const char **, const char **, const char *), argv, envp,
                     working_directory);

  TargetSP target_sp = GetSP();
  if (!target_sp)
    return LLDB_RECORD_RESULT(SBProcess());

  SBLaunchInfo launch_info = GetLaunchInfo();

  if (Module *exe_module = target_sp->GetExecutableModulePointer())
    launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(),
                                  /*add_as_first_arg*/ true);
  if (argv)
    launch_info.SetArguments(argv, /*append*/ true);
  if (envp)
    launch_info.SetEnvironmentEntries(envp, /*append*/ false);
  if (working_directory)
    launch_info.SetWorkingDirectory(working_directory);

  SBError error;
  return LLDB_RECORD_RESULT(Launch(launch_info, error));
}

SBError SBTarget::Install() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBError, SBTarget, Install);

  SBError sb_error;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    sb_error.ref() = target_sp->Install(nullptr);
  }
  return LLDB_RECORD_RESULT(sb_error);
}

SBProcess SBTarget::Launch(SBListener &listener, char const **argv,
                           char const **envp, const char *stdin_path,
                           const char *stdout_path, const char *stderr_path,
                           const char *working_directory,
                           uint32_t launch_flags, // See LaunchFlags
                           bool stop_at_entry, lldb::SBError &error) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, Launch,
                     (lldb::SBListener &, const char **, const char **,
                      const char *, const char *, const char *, const char *,
                      uint32_t, bool, lldb::SBError &),
                     listener, argv, envp, stdin_path, stdout_path, stderr_path,
                     working_directory, launch_flags, stop_at_entry, error);

  SBProcess sb_process;
  ProcessSP process_sp;
  TargetSP target_sp(GetSP());

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    if (stop_at_entry)
      launch_flags |= eLaunchFlagStopAtEntry;

    if (getenv("LLDB_LAUNCH_FLAG_DISABLE_ASLR"))
      launch_flags |= eLaunchFlagDisableASLR;

    StateType state = eStateInvalid;
    process_sp = target_sp->GetProcessSP();
    if (process_sp) {
      state = process_sp->GetState();

      if (process_sp->IsAlive() && state != eStateConnected) {
        if (state == eStateAttaching)
          error.SetErrorString("process attach is in progress");
        else
          error.SetErrorString("a process is already being debugged");
        return LLDB_RECORD_RESULT(sb_process);
      }
    }

    if (state == eStateConnected) {
      // If we are already connected, then we have already specified the
      // listener, so if a valid listener is supplied, we need to error out to
      // let the client know.
      if (listener.IsValid()) {
        error.SetErrorString("process is connected and already has a listener, "
                             "pass empty listener");
        return LLDB_RECORD_RESULT(sb_process);
      }
    }

    if (getenv("LLDB_LAUNCH_FLAG_DISABLE_STDIO"))
      launch_flags |= eLaunchFlagDisableSTDIO;

    ProcessLaunchInfo launch_info(FileSpec(stdin_path), FileSpec(stdout_path),
                                  FileSpec(stderr_path),
                                  FileSpec(working_directory), launch_flags);

    Module *exe_module = target_sp->GetExecutableModulePointer();
    if (exe_module)
      launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(), true);
    if (argv) {
      launch_info.GetArguments().AppendArguments(argv);
    } else {
      auto default_launch_info = target_sp->GetProcessLaunchInfo();
      launch_info.GetArguments().AppendArguments(
          default_launch_info.GetArguments());
    }
    if (envp) {
      launch_info.GetEnvironment() = Environment(envp);
    } else {
      auto default_launch_info = target_sp->GetProcessLaunchInfo();
      launch_info.GetEnvironment() = default_launch_info.GetEnvironment();
    }

    if (listener.IsValid())
      launch_info.SetListener(listener.GetSP());

    error.SetError(target_sp->Launch(launch_info, nullptr));

    sb_process.SetSP(target_sp->GetProcessSP());
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  return LLDB_RECORD_RESULT(sb_process);
}

SBProcess SBTarget::Launch(SBLaunchInfo &sb_launch_info, SBError &error) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, Launch,
                     (lldb::SBLaunchInfo &, lldb::SBError &), sb_launch_info,
                     error);


  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    StateType state = eStateInvalid;
    {
      ProcessSP process_sp = target_sp->GetProcessSP();
      if (process_sp) {
        state = process_sp->GetState();

        if (process_sp->IsAlive() && state != eStateConnected) {
          if (state == eStateAttaching)
            error.SetErrorString("process attach is in progress");
          else
            error.SetErrorString("a process is already being debugged");
          return LLDB_RECORD_RESULT(sb_process);
        }
      }
    }

    lldb_private::ProcessLaunchInfo launch_info = sb_launch_info.ref();

    if (!launch_info.GetExecutableFile()) {
      Module *exe_module = target_sp->GetExecutableModulePointer();
      if (exe_module)
        launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(), true);
    }

    const ArchSpec &arch_spec = target_sp->GetArchitecture();
    if (arch_spec.IsValid())
      launch_info.GetArchitecture() = arch_spec;

    error.SetError(target_sp->Launch(launch_info, nullptr));
    sb_launch_info.set_ref(launch_info);
    sb_process.SetSP(target_sp->GetProcessSP());
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  return LLDB_RECORD_RESULT(sb_process);
}

lldb::SBProcess SBTarget::Attach(SBAttachInfo &sb_attach_info, SBError &error) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, Attach,
                     (lldb::SBAttachInfo &, lldb::SBError &), sb_attach_info,
                     error);

  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (target_sp) {
    ProcessAttachInfo &attach_info = sb_attach_info.ref();
    if (attach_info.ProcessIDIsValid() && !attach_info.UserIDIsValid()) {
      PlatformSP platform_sp = target_sp->GetPlatform();
      // See if we can pre-verify if a process exists or not
      if (platform_sp && platform_sp->IsConnected()) {
        lldb::pid_t attach_pid = attach_info.GetProcessID();
        ProcessInstanceInfo instance_info;
        if (platform_sp->GetProcessInfo(attach_pid, instance_info)) {
          attach_info.SetUserID(instance_info.GetEffectiveUserID());
        } else {
          error.ref().SetErrorStringWithFormat(
              "no process found with process ID %" PRIu64, attach_pid);
          return LLDB_RECORD_RESULT(sb_process);
        }
      }
    }
    error.SetError(AttachToProcess(attach_info, *target_sp));
    if (error.Success())
      sb_process.SetSP(target_sp->GetProcessSP());
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  return LLDB_RECORD_RESULT(sb_process);
}

lldb::SBProcess SBTarget::AttachToProcessWithID(
    SBListener &listener,
    lldb::pid_t pid, // The process ID to attach to
    SBError &error   // An error explaining what went wrong if attach fails
) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, AttachToProcessWithID,
                     (lldb::SBListener &, lldb::pid_t, lldb::SBError &),
                     listener, pid, error);

  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (target_sp) {
    ProcessAttachInfo attach_info;
    attach_info.SetProcessID(pid);
    if (listener.IsValid())
      attach_info.SetListener(listener.GetSP());

    ProcessInstanceInfo instance_info;
    if (target_sp->GetPlatform()->GetProcessInfo(pid, instance_info))
      attach_info.SetUserID(instance_info.GetEffectiveUserID());

    error.SetError(AttachToProcess(attach_info, *target_sp));
    if (error.Success())
      sb_process.SetSP(target_sp->GetProcessSP());
  } else
    error.SetErrorString("SBTarget is invalid");

  return LLDB_RECORD_RESULT(sb_process);
}

lldb::SBProcess SBTarget::AttachToProcessWithName(
    SBListener &listener,
    const char *name, // basename of process to attach to
    bool wait_for, // if true wait for a new instance of "name" to be launched
    SBError &error // An error explaining what went wrong if attach fails
) {
  LLDB_RECORD_METHOD(lldb::SBProcess, SBTarget, AttachToProcessWithName,
                     (lldb::SBListener &, const char *, bool, lldb::SBError &),
                     listener, name, wait_for, error);

  SBProcess sb_process;
  TargetSP target_sp(GetSP());

  if (name && target_sp) {
    ProcessAttachInfo attach_info;
    attach_info.GetExecutableFile().SetFile(name, FileSpec::Style::native);
    attach_info.SetWaitForLaunch(wait_for);
    if (listener.IsValid())
      attach_info.SetListener(listener.GetSP());

    error.SetError(AttachToProcess(attach_info, *target_sp));
    if (error.Success())
      sb_process.SetSP(target_sp->GetProcessSP());
  } else
    error.SetErrorString("SBTarget is invalid");

  return LLDB_RECORD_RESULT(sb_process);
}

lldb::SBProcess SBTarget::ConnectRemote(SBListener &listener, const char *url,
                                        const char *plugin_name,
                                        SBError &error) {
  LLDB_RECORD_METHOD(
      lldb::SBProcess, SBTarget, ConnectRemote,
      (lldb::SBListener &, const char *, const char *, lldb::SBError &),
      listener, url, plugin_name, error);

  SBProcess sb_process;
  ProcessSP process_sp;
  TargetSP target_sp(GetSP());

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (listener.IsValid())
      process_sp =
          target_sp->CreateProcess(listener.m_opaque_sp, plugin_name, nullptr,
                                   true);
    else
      process_sp = target_sp->CreateProcess(
          target_sp->GetDebugger().GetListener(), plugin_name, nullptr, true);

    if (process_sp) {
      sb_process.SetSP(process_sp);
      error.SetError(process_sp->ConnectRemote(url));
    } else {
      error.SetErrorString("unable to create lldb_private::Process");
    }
  } else {
    error.SetErrorString("SBTarget is invalid");
  }

  return LLDB_RECORD_RESULT(sb_process);
}

SBFileSpec SBTarget::GetExecutable() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBFileSpec, SBTarget, GetExecutable);

  SBFileSpec exe_file_spec;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    Module *exe_module = target_sp->GetExecutableModulePointer();
    if (exe_module)
      exe_file_spec.SetFileSpec(exe_module->GetFileSpec());
  }

  return LLDB_RECORD_RESULT(exe_file_spec);
}

bool SBTarget::operator==(const SBTarget &rhs) const {
  LLDB_RECORD_METHOD_CONST(bool, SBTarget, operator==,(const lldb::SBTarget &),
                           rhs);

  return m_opaque_sp.get() == rhs.m_opaque_sp.get();
}

bool SBTarget::operator!=(const SBTarget &rhs) const {
  LLDB_RECORD_METHOD_CONST(bool, SBTarget, operator!=,(const lldb::SBTarget &),
                           rhs);

  return m_opaque_sp.get() != rhs.m_opaque_sp.get();
}

lldb::TargetSP SBTarget::GetSP() const { return m_opaque_sp; }

void SBTarget::SetSP(const lldb::TargetSP &target_sp) {
  m_opaque_sp = target_sp;
}

lldb::SBAddress SBTarget::ResolveLoadAddress(lldb::addr_t vm_addr) {
  LLDB_RECORD_METHOD(lldb::SBAddress, SBTarget, ResolveLoadAddress,
                     (lldb::addr_t), vm_addr);

  lldb::SBAddress sb_addr;
  Address &addr = sb_addr.ref();
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (target_sp->ResolveLoadAddress(vm_addr, addr))
      return LLDB_RECORD_RESULT(sb_addr);
  }

  // We have a load address that isn't in a section, just return an address
  // with the offset filled in (the address) and the section set to NULL
  addr.SetRawAddress(vm_addr);
  return LLDB_RECORD_RESULT(sb_addr);
}

lldb::SBAddress SBTarget::ResolveFileAddress(lldb::addr_t file_addr) {
  LLDB_RECORD_METHOD(lldb::SBAddress, SBTarget, ResolveFileAddress,
                     (lldb::addr_t), file_addr);

  lldb::SBAddress sb_addr;
  Address &addr = sb_addr.ref();
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (target_sp->ResolveFileAddress(file_addr, addr))
      return LLDB_RECORD_RESULT(sb_addr);
  }

  addr.SetRawAddress(file_addr);
  return LLDB_RECORD_RESULT(sb_addr);
}

lldb::SBAddress SBTarget::ResolvePastLoadAddress(uint32_t stop_id,
                                                 lldb::addr_t vm_addr) {
  LLDB_RECORD_METHOD(lldb::SBAddress, SBTarget, ResolvePastLoadAddress,
                     (uint32_t, lldb::addr_t), stop_id, vm_addr);

  lldb::SBAddress sb_addr;
  Address &addr = sb_addr.ref();
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    if (target_sp->ResolveLoadAddress(vm_addr, addr))
      return LLDB_RECORD_RESULT(sb_addr);
  }

  // We have a load address that isn't in a section, just return an address
  // with the offset filled in (the address) and the section set to NULL
  addr.SetRawAddress(vm_addr);
  return LLDB_RECORD_RESULT(sb_addr);
}

SBSymbolContext
SBTarget::ResolveSymbolContextForAddress(const SBAddress &addr,
                                         uint32_t resolve_scope) {
  LLDB_RECORD_METHOD(lldb::SBSymbolContext, SBTarget,
                     ResolveSymbolContextForAddress,
                     (const lldb::SBAddress &, uint32_t), addr, resolve_scope);

  SBSymbolContext sc;
  SymbolContextItem scope = static_cast<SymbolContextItem>(resolve_scope);
  if (addr.IsValid()) {
    TargetSP target_sp(GetSP());
    if (target_sp)
      target_sp->GetImages().ResolveSymbolContextForAddress(addr.ref(), scope,
                                                            sc.ref());
  }
  return LLDB_RECORD_RESULT(sc);
}

size_t SBTarget::ReadMemory(const SBAddress addr, void *buf, size_t size,
                            lldb::SBError &error) {
  LLDB_RECORD_METHOD(size_t, SBTarget, ReadMemory,
                     (const lldb::SBAddress, void *, size_t, lldb::SBError &),
                     addr, buf, size, error);

  SBError sb_error;
  size_t bytes_read = 0;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    bytes_read =
        target_sp->ReadMemory(addr.ref(), buf, size, sb_error.ref(), true);
  } else {
    sb_error.SetErrorString("invalid target");
  }

  return bytes_read;
}

SBBreakpoint SBTarget::BreakpointCreateByLocation(const char *file,
                                                  uint32_t line) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByLocation,
                     (const char *, uint32_t), file, line);

  return LLDB_RECORD_RESULT(
      SBBreakpoint(BreakpointCreateByLocation(SBFileSpec(file, false), line)));
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation(const SBFileSpec &sb_file_spec,
                                     uint32_t line) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByLocation,
                     (const lldb::SBFileSpec &, uint32_t), sb_file_spec, line);

  return LLDB_RECORD_RESULT(BreakpointCreateByLocation(sb_file_spec, line, 0));
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation(const SBFileSpec &sb_file_spec,
                                     uint32_t line, lldb::addr_t offset) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByLocation,
                     (const lldb::SBFileSpec &, uint32_t, lldb::addr_t),
                     sb_file_spec, line, offset);

  SBFileSpecList empty_list;
  return LLDB_RECORD_RESULT(
      BreakpointCreateByLocation(sb_file_spec, line, offset, empty_list));
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation(const SBFileSpec &sb_file_spec,
                                     uint32_t line, lldb::addr_t offset,
                                     SBFileSpecList &sb_module_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByLocation,
                     (const lldb::SBFileSpec &, uint32_t, lldb::addr_t,
                      lldb::SBFileSpecList &),
                     sb_file_spec, line, offset, sb_module_list);

  return LLDB_RECORD_RESULT(BreakpointCreateByLocation(sb_file_spec, line, 0,
                                                       offset, sb_module_list));
}

SBBreakpoint SBTarget::BreakpointCreateByLocation(
    const SBFileSpec &sb_file_spec, uint32_t line, uint32_t column,
    lldb::addr_t offset, SBFileSpecList &sb_module_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByLocation,
                     (const lldb::SBFileSpec &, uint32_t, uint32_t,
                      lldb::addr_t, lldb::SBFileSpecList &),
                     sb_file_spec, line, column, offset, sb_module_list);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && line != 0) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    const LazyBool check_inlines = eLazyBoolCalculate;
    const LazyBool skip_prologue = eLazyBoolCalculate;
    const bool internal = false;
    const bool hardware = false;
    const LazyBool move_to_nearest_code = eLazyBoolCalculate;
    const FileSpecList *module_list = nullptr;
    if (sb_module_list.GetSize() > 0) {
      module_list = sb_module_list.get();
    }
    sb_bp = target_sp->CreateBreakpoint(
        module_list, *sb_file_spec, line, column, offset, check_inlines,
        skip_prologue, internal, hardware, move_to_nearest_code);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

SBBreakpoint SBTarget::BreakpointCreateByLocation(
    const SBFileSpec &sb_file_spec, uint32_t line, uint32_t column,
    lldb::addr_t offset, SBFileSpecList &sb_module_list,
    bool move_to_nearest_code) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByLocation,
                     (const lldb::SBFileSpec &, uint32_t, uint32_t,
                      lldb::addr_t, lldb::SBFileSpecList &, bool),
                     sb_file_spec, line, column, offset, sb_module_list,
                     move_to_nearest_code);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && line != 0) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    const LazyBool check_inlines = eLazyBoolCalculate;
    const LazyBool skip_prologue = eLazyBoolCalculate;
    const bool internal = false;
    const bool hardware = false;
    const FileSpecList *module_list = nullptr;
    if (sb_module_list.GetSize() > 0) {
      module_list = sb_module_list.get();
    }
    sb_bp = target_sp->CreateBreakpoint(
        module_list, *sb_file_spec, line, column, offset, check_inlines,
        skip_prologue, internal, hardware,
        move_to_nearest_code ? eLazyBoolYes : eLazyBoolNo);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

SBBreakpoint SBTarget::BreakpointCreateByName(const char *symbol_name,
                                              const char *module_name) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                     (const char *, const char *), symbol_name, module_name);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp.get()) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    const bool internal = false;
    const bool hardware = false;
    const LazyBool skip_prologue = eLazyBoolCalculate;
    const lldb::addr_t offset = 0;
    if (module_name && module_name[0]) {
      FileSpecList module_spec_list;
      module_spec_list.Append(FileSpec(module_name));
      sb_bp = target_sp->CreateBreakpoint(
          &module_spec_list, nullptr, symbol_name, eFunctionNameTypeAuto,
          eLanguageTypeUnknown, offset, skip_prologue, internal, hardware);
    } else {
      sb_bp = target_sp->CreateBreakpoint(
          nullptr, nullptr, symbol_name, eFunctionNameTypeAuto,
          eLanguageTypeUnknown, offset, skip_prologue, internal, hardware);
    }
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateByName(const char *symbol_name,
                                 const SBFileSpecList &module_list,
                                 const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                     (const char *, const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &),
                     symbol_name, module_list, comp_unit_list);

  lldb::FunctionNameType name_type_mask = eFunctionNameTypeAuto;
  return LLDB_RECORD_RESULT(
      BreakpointCreateByName(symbol_name, name_type_mask, eLanguageTypeUnknown,
                             module_list, comp_unit_list));
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByName(
    const char *symbol_name, uint32_t name_type_mask,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                     (const char *, uint32_t, const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &),
                     symbol_name, name_type_mask, module_list, comp_unit_list);

  return LLDB_RECORD_RESULT(
      BreakpointCreateByName(symbol_name, name_type_mask, eLanguageTypeUnknown,
                             module_list, comp_unit_list));
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByName(
    const char *symbol_name, uint32_t name_type_mask,
    LanguageType symbol_language, const SBFileSpecList &module_list,
    const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                     (const char *, uint32_t, lldb::LanguageType,
                      const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &),
                     symbol_name, name_type_mask, symbol_language, module_list,
                     comp_unit_list);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && symbol_name && symbol_name[0]) {
    const bool internal = false;
    const bool hardware = false;
    const LazyBool skip_prologue = eLazyBoolCalculate;
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    FunctionNameType mask = static_cast<FunctionNameType>(name_type_mask);
    sb_bp = target_sp->CreateBreakpoint(module_list.get(), comp_unit_list.get(),
                                        symbol_name, mask, symbol_language, 0,
                                        skip_prologue, internal, hardware);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByNames(
    const char *symbol_names[], uint32_t num_names, uint32_t name_type_mask,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(
      lldb::SBBreakpoint, SBTarget, BreakpointCreateByNames,
      (const char **, uint32_t, uint32_t, const lldb::SBFileSpecList &,
       const lldb::SBFileSpecList &),
      symbol_names, num_names, name_type_mask, module_list, comp_unit_list);

  return LLDB_RECORD_RESULT(BreakpointCreateByNames(
      symbol_names, num_names, name_type_mask, eLanguageTypeUnknown,
      module_list, comp_unit_list));
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByNames(
    const char *symbol_names[], uint32_t num_names, uint32_t name_type_mask,
    LanguageType symbol_language, const SBFileSpecList &module_list,
    const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByNames,
                     (const char **, uint32_t, uint32_t, lldb::LanguageType,
                      const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &),
                     symbol_names, num_names, name_type_mask, symbol_language,
                     module_list, comp_unit_list);

  return LLDB_RECORD_RESULT(BreakpointCreateByNames(
      symbol_names, num_names, name_type_mask, eLanguageTypeUnknown, 0,
      module_list, comp_unit_list));
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByNames(
    const char *symbol_names[], uint32_t num_names, uint32_t name_type_mask,
    LanguageType symbol_language, lldb::addr_t offset,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByNames,
                     (const char **, uint32_t, uint32_t, lldb::LanguageType,
                      lldb::addr_t, const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &),
                     symbol_names, num_names, name_type_mask, symbol_language,
                     offset, module_list, comp_unit_list);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && num_names > 0) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool internal = false;
    const bool hardware = false;
    FunctionNameType mask = static_cast<FunctionNameType>(name_type_mask);
    const LazyBool skip_prologue = eLazyBoolCalculate;
    sb_bp = target_sp->CreateBreakpoint(
        module_list.get(), comp_unit_list.get(), symbol_names, num_names, mask,
        symbol_language, offset, skip_prologue, internal, hardware);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

SBBreakpoint SBTarget::BreakpointCreateByRegex(const char *symbol_name_regex,
                                               const char *module_name) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByRegex,
                     (const char *, const char *), symbol_name_regex,
                     module_name);

  SBFileSpecList module_spec_list;
  SBFileSpecList comp_unit_list;
  if (module_name && module_name[0]) {
    module_spec_list.Append(FileSpec(module_name));
  }
  return LLDB_RECORD_RESULT(
      BreakpointCreateByRegex(symbol_name_regex, eLanguageTypeUnknown,
                              module_spec_list, comp_unit_list));
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateByRegex(const char *symbol_name_regex,
                                  const SBFileSpecList &module_list,
                                  const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByRegex,
                     (const char *, const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &),
                     symbol_name_regex, module_list, comp_unit_list);

  return LLDB_RECORD_RESULT(BreakpointCreateByRegex(
      symbol_name_regex, eLanguageTypeUnknown, module_list, comp_unit_list));
}

lldb::SBBreakpoint SBTarget::BreakpointCreateByRegex(
    const char *symbol_name_regex, LanguageType symbol_language,
    const SBFileSpecList &module_list, const SBFileSpecList &comp_unit_list) {
  LLDB_RECORD_METHOD(
      lldb::SBBreakpoint, SBTarget, BreakpointCreateByRegex,
      (const char *, lldb::LanguageType, const lldb::SBFileSpecList &,
       const lldb::SBFileSpecList &),
      symbol_name_regex, symbol_language, module_list, comp_unit_list);


  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && symbol_name_regex && symbol_name_regex[0]) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    RegularExpression regexp((llvm::StringRef(symbol_name_regex)));
    const bool internal = false;
    const bool hardware = false;
    const LazyBool skip_prologue = eLazyBoolCalculate;

    sb_bp = target_sp->CreateFuncRegexBreakpoint(
        module_list.get(), comp_unit_list.get(), std::move(regexp),
        symbol_language, skip_prologue, internal, hardware);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

SBBreakpoint SBTarget::BreakpointCreateByAddress(addr_t address) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByAddress,
                     (lldb::addr_t), address);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    sb_bp = target_sp->CreateBreakpoint(address, false, hardware);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

SBBreakpoint SBTarget::BreakpointCreateBySBAddress(SBAddress &sb_address) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateBySBAddress,
                     (lldb::SBAddress &), sb_address);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (!sb_address.IsValid()) {
    return LLDB_RECORD_RESULT(sb_bp);
  }

  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    sb_bp = target_sp->CreateBreakpoint(sb_address.ref(), false, hardware);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateBySourceRegex(const char *source_regex,
                                        const lldb::SBFileSpec &source_file,
                                        const char *module_name) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget,
                     BreakpointCreateBySourceRegex,
                     (const char *, const lldb::SBFileSpec &, const char *),
                     source_regex, source_file, module_name);

  SBFileSpecList module_spec_list;

  if (module_name && module_name[0]) {
    module_spec_list.Append(FileSpec(module_name));
  }

  SBFileSpecList source_file_list;
  if (source_file.IsValid()) {
    source_file_list.Append(source_file);
  }

  return LLDB_RECORD_RESULT(BreakpointCreateBySourceRegex(
      source_regex, module_spec_list, source_file_list));
}

lldb::SBBreakpoint SBTarget::BreakpointCreateBySourceRegex(
    const char *source_regex, const SBFileSpecList &module_list,
    const lldb::SBFileSpecList &source_file_list) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget,
                     BreakpointCreateBySourceRegex,
                     (const char *, const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &),
                     source_regex, module_list, source_file_list);

  return LLDB_RECORD_RESULT(BreakpointCreateBySourceRegex(
      source_regex, module_list, source_file_list, SBStringList()));
}

lldb::SBBreakpoint SBTarget::BreakpointCreateBySourceRegex(
    const char *source_regex, const SBFileSpecList &module_list,
    const lldb::SBFileSpecList &source_file_list,
    const SBStringList &func_names) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget,
                     BreakpointCreateBySourceRegex,
                     (const char *, const lldb::SBFileSpecList &,
                      const lldb::SBFileSpecList &, const lldb::SBStringList &),
                     source_regex, module_list, source_file_list, func_names);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp && source_regex && source_regex[0]) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    const LazyBool move_to_nearest_code = eLazyBoolCalculate;
    RegularExpression regexp((llvm::StringRef(source_regex)));
    std::unordered_set<std::string> func_names_set;
    for (size_t i = 0; i < func_names.GetSize(); i++) {
      func_names_set.insert(func_names.GetStringAtIndex(i));
    }

    sb_bp = target_sp->CreateSourceRegexBreakpoint(
        module_list.get(), source_file_list.get(), func_names_set,
        std::move(regexp), false, hardware, move_to_nearest_code);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateForException(lldb::LanguageType language,
                                       bool catch_bp, bool throw_bp) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateForException,
                     (lldb::LanguageType, bool, bool), language, catch_bp,
                     throw_bp);

  // BEGIN SWIFT
  SBStringList no_extra_args;
  return LLDB_RECORD_RESULT(BreakpointCreateForException(
      language, catch_bp, throw_bp, no_extra_args));
  // END SWIFT
}

// BEGIN SWIFT
lldb::SBBreakpoint
SBTarget::BreakpointCreateForException(lldb::LanguageType language,
                                       bool catch_bp, bool throw_bp,
                                       SBStringList &extra_args) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateForException,
                     (lldb::LanguageType, bool, bool, SBStringList &), language,
                     catch_bp, throw_bp, extra_args);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    const bool hardware = false;
    sb_bp = target_sp->CreateExceptionBreakpoint(language, catch_bp, throw_bp,
                                                  hardware);
    size_t num_extra_args = extra_args.GetSize();
    if (num_extra_args > 0) {
      // Have to convert this to Args, and pass it to the precondition:
      if (num_extra_args % 2 == 0) {
        Args args;
        for (size_t i = 0; i < num_extra_args; i += 2) {
          args.AppendArgument(extra_args.GetStringAtIndex(i));
          args.AppendArgument(extra_args.GetStringAtIndex(i + 1));
        }
        BreakpointSP bkpt = sb_bp.GetSP();
        BreakpointPreconditionSP pre_condition_sp =
            bkpt->GetPrecondition();
        if (pre_condition_sp)
          pre_condition_sp->ConfigurePrecondition(args);
      }
    }
  }

  return LLDB_RECORD_RESULT(sb_bp);
}
// END SWIFT

lldb::SBBreakpoint SBTarget::BreakpointCreateFromScript(
    const char *class_name, SBStructuredData &extra_args,
    const SBFileSpecList &module_list, const SBFileSpecList &file_list,
    bool request_hardware) {
  LLDB_RECORD_METHOD(
      lldb::SBBreakpoint, SBTarget, BreakpointCreateFromScript,
      (const char *, lldb::SBStructuredData &, const lldb::SBFileSpecList &,
       const lldb::SBFileSpecList &, bool),
      class_name, extra_args, module_list, file_list, request_hardware);

  SBBreakpoint sb_bp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    Status error;

    StructuredData::ObjectSP obj_sp = extra_args.m_impl_up->GetObjectSP();
    sb_bp =
        target_sp->CreateScriptedBreakpoint(class_name,
                                            module_list.get(),
                                            file_list.get(),
                                            false, /* internal */
                                            request_hardware,
                                            obj_sp,
                                            &error);
  }

  return LLDB_RECORD_RESULT(sb_bp);
}

uint32_t SBTarget::GetNumBreakpoints() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(uint32_t, SBTarget, GetNumBreakpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The breakpoint list is thread safe, no need to lock
    return target_sp->GetBreakpointList().GetSize();
  }
  return 0;
}

SBBreakpoint SBTarget::GetBreakpointAtIndex(uint32_t idx) const {
  LLDB_RECORD_METHOD_CONST(lldb::SBBreakpoint, SBTarget, GetBreakpointAtIndex,
                           (uint32_t), idx);

  SBBreakpoint sb_breakpoint;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The breakpoint list is thread safe, no need to lock
    sb_breakpoint = target_sp->GetBreakpointList().GetBreakpointAtIndex(idx);
  }
  return LLDB_RECORD_RESULT(sb_breakpoint);
}

bool SBTarget::BreakpointDelete(break_id_t bp_id) {
  LLDB_RECORD_METHOD(bool, SBTarget, BreakpointDelete, (lldb::break_id_t),
                     bp_id);

  bool result = false;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    result = target_sp->RemoveBreakpointByID(bp_id);
  }

  return result;
}

SBBreakpoint SBTarget::FindBreakpointByID(break_id_t bp_id) {
  LLDB_RECORD_METHOD(lldb::SBBreakpoint, SBTarget, FindBreakpointByID,
                     (lldb::break_id_t), bp_id);

  SBBreakpoint sb_breakpoint;
  TargetSP target_sp(GetSP());
  if (target_sp && bp_id != LLDB_INVALID_BREAK_ID) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    sb_breakpoint = target_sp->GetBreakpointByID(bp_id);
  }

  return LLDB_RECORD_RESULT(sb_breakpoint);
}

bool SBTarget::FindBreakpointsByName(const char *name,
                                     SBBreakpointList &bkpts) {
  LLDB_RECORD_METHOD(bool, SBTarget, FindBreakpointsByName,
                     (const char *, lldb::SBBreakpointList &), name, bkpts);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    llvm::Expected<std::vector<BreakpointSP>> expected_vector =
        target_sp->GetBreakpointList().FindBreakpointsByName(name);
    if (!expected_vector) {
      LLDB_LOG(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS),
               "invalid breakpoint name: {}",
               llvm::toString(expected_vector.takeError()));
      return false;
    }
    for (BreakpointSP bkpt_sp : *expected_vector) {
      bkpts.AppendByID(bkpt_sp->GetID());
    }
  }
  return true;
}

void SBTarget::GetBreakpointNames(SBStringList &names) {
  LLDB_RECORD_METHOD(void, SBTarget, GetBreakpointNames, (lldb::SBStringList &),
                     names);

  names.Clear();

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

    std::vector<std::string> name_vec;
    target_sp->GetBreakpointNames(name_vec);
    for (auto name : name_vec)
      names.AppendString(name.c_str());
  }
}

void SBTarget::DeleteBreakpointName(const char *name) {
  LLDB_RECORD_METHOD(void, SBTarget, DeleteBreakpointName, (const char *),
                     name);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->DeleteBreakpointName(ConstString(name));
  }
}

bool SBTarget::EnableAllBreakpoints() {
  LLDB_RECORD_METHOD_NO_ARGS(bool, SBTarget, EnableAllBreakpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->EnableAllowedBreakpoints();
    return true;
  }
  return false;
}

bool SBTarget::DisableAllBreakpoints() {
  LLDB_RECORD_METHOD_NO_ARGS(bool, SBTarget, DisableAllBreakpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->DisableAllowedBreakpoints();
    return true;
  }
  return false;
}

bool SBTarget::DeleteAllBreakpoints() {
  LLDB_RECORD_METHOD_NO_ARGS(bool, SBTarget, DeleteAllBreakpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    target_sp->RemoveAllowedBreakpoints();
    return true;
  }
  return false;
}

lldb::SBError SBTarget::BreakpointsCreateFromFile(SBFileSpec &source_file,
                                                  SBBreakpointList &new_bps) {
  LLDB_RECORD_METHOD(lldb::SBError, SBTarget, BreakpointsCreateFromFile,
                     (lldb::SBFileSpec &, lldb::SBBreakpointList &),
                     source_file, new_bps);

  SBStringList empty_name_list;
  return LLDB_RECORD_RESULT(
      BreakpointsCreateFromFile(source_file, empty_name_list, new_bps));
}

lldb::SBError SBTarget::BreakpointsCreateFromFile(SBFileSpec &source_file,
                                                  SBStringList &matching_names,
                                                  SBBreakpointList &new_bps) {
  LLDB_RECORD_METHOD(
      lldb::SBError, SBTarget, BreakpointsCreateFromFile,
      (lldb::SBFileSpec &, lldb::SBStringList &, lldb::SBBreakpointList &),
      source_file, matching_names, new_bps);

  SBError sberr;
  TargetSP target_sp(GetSP());
  if (!target_sp) {
    sberr.SetErrorString(
        "BreakpointCreateFromFile called with invalid target.");
    return LLDB_RECORD_RESULT(sberr);
  }
  std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());

  BreakpointIDList bp_ids;

  std::vector<std::string> name_vector;
  size_t num_names = matching_names.GetSize();
  for (size_t i = 0; i < num_names; i++)
    name_vector.push_back(matching_names.GetStringAtIndex(i));

  sberr.ref() = target_sp->CreateBreakpointsFromFile(source_file.ref(),
                                                     name_vector, bp_ids);
  if (sberr.Fail())
    return LLDB_RECORD_RESULT(sberr);

  size_t num_bkpts = bp_ids.GetSize();
  for (size_t i = 0; i < num_bkpts; i++) {
    BreakpointID bp_id = bp_ids.GetBreakpointIDAtIndex(i);
    new_bps.AppendByID(bp_id.GetBreakpointID());
  }
  return LLDB_RECORD_RESULT(sberr);
}

lldb::SBError SBTarget::BreakpointsWriteToFile(SBFileSpec &dest_file) {
  LLDB_RECORD_METHOD(lldb::SBError, SBTarget, BreakpointsWriteToFile,
                     (lldb::SBFileSpec &), dest_file);

  SBError sberr;
  TargetSP target_sp(GetSP());
  if (!target_sp) {
    sberr.SetErrorString("BreakpointWriteToFile called with invalid target.");
    return LLDB_RECORD_RESULT(sberr);
  }
  SBBreakpointList bkpt_list(*this);
  return LLDB_RECORD_RESULT(BreakpointsWriteToFile(dest_file, bkpt_list));
}

lldb::SBError SBTarget::BreakpointsWriteToFile(SBFileSpec &dest_file,
                                               SBBreakpointList &bkpt_list,
                                               bool append) {
  LLDB_RECORD_METHOD(lldb::SBError, SBTarget, BreakpointsWriteToFile,
                     (lldb::SBFileSpec &, lldb::SBBreakpointList &, bool),
                     dest_file, bkpt_list, append);

  SBError sberr;
  TargetSP target_sp(GetSP());
  if (!target_sp) {
    sberr.SetErrorString("BreakpointWriteToFile called with invalid target.");
    return LLDB_RECORD_RESULT(sberr);
  }

  std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
  BreakpointIDList bp_id_list;
  bkpt_list.CopyToBreakpointIDList(bp_id_list);
  sberr.ref() = target_sp->SerializeBreakpointsToFile(dest_file.ref(),
                                                      bp_id_list, append);
  return LLDB_RECORD_RESULT(sberr);
}

uint32_t SBTarget::GetNumWatchpoints() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(uint32_t, SBTarget, GetNumWatchpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The watchpoint list is thread safe, no need to lock
    return target_sp->GetWatchpointList().GetSize();
  }
  return 0;
}

SBWatchpoint SBTarget::GetWatchpointAtIndex(uint32_t idx) const {
  LLDB_RECORD_METHOD_CONST(lldb::SBWatchpoint, SBTarget, GetWatchpointAtIndex,
                           (uint32_t), idx);

  SBWatchpoint sb_watchpoint;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The watchpoint list is thread safe, no need to lock
    sb_watchpoint.SetSP(target_sp->GetWatchpointList().GetByIndex(idx));
  }
  return LLDB_RECORD_RESULT(sb_watchpoint);
}

bool SBTarget::DeleteWatchpoint(watch_id_t wp_id) {
  LLDB_RECORD_METHOD(bool, SBTarget, DeleteWatchpoint, (lldb::watch_id_t),
                     wp_id);


  bool result = false;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    result = target_sp->RemoveWatchpointByID(wp_id);
  }

  return result;
}

SBWatchpoint SBTarget::FindWatchpointByID(lldb::watch_id_t wp_id) {
  LLDB_RECORD_METHOD(lldb::SBWatchpoint, SBTarget, FindWatchpointByID,
                     (lldb::watch_id_t), wp_id);


  SBWatchpoint sb_watchpoint;
  lldb::WatchpointSP watchpoint_sp;
  TargetSP target_sp(GetSP());
  if (target_sp && wp_id != LLDB_INVALID_WATCH_ID) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    watchpoint_sp = target_sp->GetWatchpointList().FindByID(wp_id);
    sb_watchpoint.SetSP(watchpoint_sp);
  }

  return LLDB_RECORD_RESULT(sb_watchpoint);
}

lldb::SBWatchpoint SBTarget::WatchAddress(lldb::addr_t addr, size_t size,
                                          bool read, bool write,
                                          SBError &error) {
  LLDB_RECORD_METHOD(lldb::SBWatchpoint, SBTarget, WatchAddress,
                     (lldb::addr_t, size_t, bool, bool, lldb::SBError &), addr,
                     size, read, write, error);

  SBWatchpoint sb_watchpoint;
  lldb::WatchpointSP watchpoint_sp;
  TargetSP target_sp(GetSP());
  if (target_sp && (read || write) && addr != LLDB_INVALID_ADDRESS &&
      size > 0) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    uint32_t watch_type = 0;
    if (read)
      watch_type |= LLDB_WATCH_TYPE_READ;
    if (write)
      watch_type |= LLDB_WATCH_TYPE_WRITE;
    if (watch_type == 0) {
      error.SetErrorString(
          "Can't create a watchpoint that is neither read nor write.");
      return LLDB_RECORD_RESULT(sb_watchpoint);
    }

    // Target::CreateWatchpoint() is thread safe.
    Status cw_error;
    // This API doesn't take in a type, so we can't figure out what it is.
    CompilerType *type = nullptr;
    watchpoint_sp =
        target_sp->CreateWatchpoint(addr, size, type, watch_type, cw_error);
    error.SetError(cw_error);
    sb_watchpoint.SetSP(watchpoint_sp);
  }

  return LLDB_RECORD_RESULT(sb_watchpoint);
}

bool SBTarget::EnableAllWatchpoints() {
  LLDB_RECORD_METHOD_NO_ARGS(bool, SBTarget, EnableAllWatchpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    target_sp->EnableAllWatchpoints();
    return true;
  }
  return false;
}

bool SBTarget::DisableAllWatchpoints() {
  LLDB_RECORD_METHOD_NO_ARGS(bool, SBTarget, DisableAllWatchpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    target_sp->DisableAllWatchpoints();
    return true;
  }
  return false;
}

SBValue SBTarget::CreateValueFromAddress(const char *name, SBAddress addr,
                                         SBType type) {
  LLDB_RECORD_METHOD(lldb::SBValue, SBTarget, CreateValueFromAddress,
                     (const char *, lldb::SBAddress, lldb::SBType), name, addr,
                     type);

  SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  if (IsValid() && name && *name && addr.IsValid() && type.IsValid()) {
    lldb::addr_t load_addr(addr.GetLoadAddress(*this));
    ExecutionContext exe_ctx(
        ExecutionContextRef(ExecutionContext(m_opaque_sp.get(), false)));
    CompilerType ast_type(type.GetSP()->GetCompilerType(true));
    new_value_sp = ValueObject::CreateValueObjectFromAddress(name, load_addr,
                                                             exe_ctx, ast_type);
  }
  sb_value.SetSP(new_value_sp);
  return LLDB_RECORD_RESULT(sb_value);
}

lldb::SBValue SBTarget::CreateValueFromData(const char *name, lldb::SBData data,
                                            lldb::SBType type) {
  LLDB_RECORD_METHOD(lldb::SBValue, SBTarget, CreateValueFromData,
                     (const char *, lldb::SBData, lldb::SBType), name, data,
                     type);

  SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  if (IsValid() && name && *name && data.IsValid() && type.IsValid()) {
    DataExtractorSP extractor(*data);
    ExecutionContext exe_ctx(
        ExecutionContextRef(ExecutionContext(m_opaque_sp.get(), false)));
    CompilerType ast_type(type.GetSP()->GetCompilerType(true));
    new_value_sp = ValueObject::CreateValueObjectFromData(name, *extractor,
                                                          exe_ctx, ast_type);
  }
  sb_value.SetSP(new_value_sp);
  return LLDB_RECORD_RESULT(sb_value);
}

lldb::SBValue SBTarget::CreateValueFromExpression(const char *name,
                                                  const char *expr) {
  LLDB_RECORD_METHOD(lldb::SBValue, SBTarget, CreateValueFromExpression,
                     (const char *, const char *), name, expr);

  SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  if (IsValid() && name && *name && expr && *expr) {
    ExecutionContext exe_ctx(
        ExecutionContextRef(ExecutionContext(m_opaque_sp.get(), false)));
    new_value_sp =
        ValueObject::CreateValueObjectFromExpression(name, expr, exe_ctx);
  }
  sb_value.SetSP(new_value_sp);
  return LLDB_RECORD_RESULT(sb_value);
}

bool SBTarget::DeleteAllWatchpoints() {
  LLDB_RECORD_METHOD_NO_ARGS(bool, SBTarget, DeleteAllWatchpoints);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    std::unique_lock<std::recursive_mutex> lock;
    target_sp->GetWatchpointList().GetListMutex(lock);
    target_sp->RemoveAllWatchpoints();
    return true;
  }
  return false;
}

void SBTarget::AppendImageSearchPath(const char *from, const char *to,
                                     lldb::SBError &error) {
  LLDB_RECORD_METHOD(void, SBTarget, AppendImageSearchPath,
                     (const char *, const char *, lldb::SBError &), from, to,
                     error);

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return error.SetErrorString("invalid target");

  llvm::StringRef srFrom = from, srTo = to;
  if (srFrom.empty())
    return error.SetErrorString("<from> path can't be empty");
  if (srTo.empty())
    return error.SetErrorString("<to> path can't be empty");

  target_sp->GetImageSearchPathList().Append(srFrom, srTo, true);
}

lldb::SBModule SBTarget::AddModule(const char *path, const char *triple,
                                   const char *uuid_cstr) {
  LLDB_RECORD_METHOD(lldb::SBModule, SBTarget, AddModule,
                     (const char *, const char *, const char *), path, triple,
                     uuid_cstr);

  return LLDB_RECORD_RESULT(AddModule(path, triple, uuid_cstr, nullptr));
}

lldb::SBModule SBTarget::AddModule(const char *path, const char *triple,
                                   const char *uuid_cstr, const char *symfile) {
  LLDB_RECORD_METHOD(lldb::SBModule, SBTarget, AddModule,
                     (const char *, const char *, const char *, const char *),
                     path, triple, uuid_cstr, symfile);

  lldb::SBModule sb_module;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    ModuleSpec module_spec;
    if (path)
      module_spec.GetFileSpec().SetFile(path, FileSpec::Style::native);

    if (uuid_cstr)
      module_spec.GetUUID().SetFromStringRef(uuid_cstr);

    if (triple)
      module_spec.GetArchitecture() = Platform::GetAugmentedArchSpec(
          target_sp->GetPlatform().get(), triple);
    else
      module_spec.GetArchitecture() = target_sp->GetArchitecture();

    if (symfile)
      module_spec.GetSymbolFileSpec().SetFile(symfile, FileSpec::Style::native);

    sb_module.SetSP(target_sp->GetOrCreateModule(module_spec, true /* notify */));
  }
  return LLDB_RECORD_RESULT(sb_module);
}

lldb::SBModule SBTarget::AddModule(const SBModuleSpec &module_spec) {
  LLDB_RECORD_METHOD(lldb::SBModule, SBTarget, AddModule,
                     (const lldb::SBModuleSpec &), module_spec);

  lldb::SBModule sb_module;
  TargetSP target_sp(GetSP());
  if (target_sp)
    sb_module.SetSP(target_sp->GetOrCreateModule(*module_spec.m_opaque_up,
                                                 true /* notify */));
  return LLDB_RECORD_RESULT(sb_module);
}

bool SBTarget::AddModule(lldb::SBModule &module) {
  LLDB_RECORD_METHOD(bool, SBTarget, AddModule, (lldb::SBModule &), module);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    target_sp->GetImages().AppendIfNeeded(module.GetSP());
    return true;
  }
  return false;
}

uint32_t SBTarget::GetNumModules() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(uint32_t, SBTarget, GetNumModules);

  uint32_t num = 0;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The module list is thread safe, no need to lock
    num = target_sp->GetImages().GetSize();
  }

  return num;
}

void SBTarget::Clear() {
  LLDB_RECORD_METHOD_NO_ARGS(void, SBTarget, Clear);

  m_opaque_sp.reset();
}

SBModule SBTarget::FindModule(const SBFileSpec &sb_file_spec) {
  LLDB_RECORD_METHOD(lldb::SBModule, SBTarget, FindModule,
                     (const lldb::SBFileSpec &), sb_file_spec);

  SBModule sb_module;
  TargetSP target_sp(GetSP());
  if (target_sp && sb_file_spec.IsValid()) {
    ModuleSpec module_spec(*sb_file_spec);
    // The module list is thread safe, no need to lock
    sb_module.SetSP(target_sp->GetImages().FindFirstModule(module_spec));
  }
  return LLDB_RECORD_RESULT(sb_module);
}

SBSymbolContextList SBTarget::FindCompileUnits(const SBFileSpec &sb_file_spec) {
  LLDB_RECORD_METHOD(lldb::SBSymbolContextList, SBTarget, FindCompileUnits,
                     (const lldb::SBFileSpec &), sb_file_spec);

  SBSymbolContextList sb_sc_list;
  const TargetSP target_sp(GetSP());
  if (target_sp && sb_file_spec.IsValid())
    target_sp->GetImages().FindCompileUnits(*sb_file_spec, *sb_sc_list);
  return LLDB_RECORD_RESULT(sb_sc_list);
}

lldb::ByteOrder SBTarget::GetByteOrder() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::ByteOrder, SBTarget, GetByteOrder);

  TargetSP target_sp(GetSP());
  if (target_sp)
    return target_sp->GetArchitecture().GetByteOrder();
  return eByteOrderInvalid;
}

const char *SBTarget::GetTriple() {
  LLDB_RECORD_METHOD_NO_ARGS(const char *, SBTarget, GetTriple);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    std::string triple(target_sp->GetArchitecture().GetTriple().str());
    // Unique the string so we don't run into ownership issues since the const
    // strings put the string into the string pool once and the strings never
    // comes out
    ConstString const_triple(triple.c_str());
    return const_triple.GetCString();
  }
  return nullptr;
}

uint32_t SBTarget::GetDataByteSize() {
  LLDB_RECORD_METHOD_NO_ARGS(uint32_t, SBTarget, GetDataByteSize);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    return target_sp->GetArchitecture().GetDataByteSize();
  }
  return 0;
}

uint32_t SBTarget::GetCodeByteSize() {
  LLDB_RECORD_METHOD_NO_ARGS(uint32_t, SBTarget, GetCodeByteSize);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    return target_sp->GetArchitecture().GetCodeByteSize();
  }
  return 0;
}

uint32_t SBTarget::GetMaximumNumberOfChildrenToDisplay() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(uint32_t, SBTarget, GetMaximumNumberOfChildrenToDisplay);

  TargetSP target_sp(GetSP());
  if(target_sp){
     return target_sp->GetMaximumNumberOfChildrenToDisplay();
  }
  return 0;
}

uint32_t SBTarget::GetAddressByteSize() {
  LLDB_RECORD_METHOD_NO_ARGS(uint32_t, SBTarget, GetAddressByteSize);

  TargetSP target_sp(GetSP());
  if (target_sp)
    return target_sp->GetArchitecture().GetAddressByteSize();
  return sizeof(void *);
}

SBModule SBTarget::GetModuleAtIndex(uint32_t idx) {
  LLDB_RECORD_METHOD(lldb::SBModule, SBTarget, GetModuleAtIndex, (uint32_t),
                     idx);

  SBModule sb_module;
  ModuleSP module_sp;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    // The module list is thread safe, no need to lock
    module_sp = target_sp->GetImages().GetModuleAtIndex(idx);
    sb_module.SetSP(module_sp);
  }

  return LLDB_RECORD_RESULT(sb_module);
}

bool SBTarget::RemoveModule(lldb::SBModule module) {
  LLDB_RECORD_METHOD(bool, SBTarget, RemoveModule, (lldb::SBModule), module);

  TargetSP target_sp(GetSP());
  if (target_sp)
    return target_sp->GetImages().Remove(module.GetSP());
  return false;
}

SBBroadcaster SBTarget::GetBroadcaster() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(lldb::SBBroadcaster, SBTarget,
                                   GetBroadcaster);


  TargetSP target_sp(GetSP());
  SBBroadcaster broadcaster(target_sp.get(), false);


  return LLDB_RECORD_RESULT(broadcaster);
}

bool SBTarget::GetDescription(SBStream &description,
                              lldb::DescriptionLevel description_level) {
  LLDB_RECORD_METHOD(bool, SBTarget, GetDescription,
                     (lldb::SBStream &, lldb::DescriptionLevel), description,
                     description_level);

  Stream &strm = description.ref();

  TargetSP target_sp(GetSP());
  if (target_sp) {
    target_sp->Dump(&strm, description_level);
  } else
    strm.PutCString("No value");

  return true;
}

lldb::SBSymbolContextList SBTarget::FindFunctions(const char *name,
                                                  uint32_t name_type_mask) {
  LLDB_RECORD_METHOD(lldb::SBSymbolContextList, SBTarget, FindFunctions,
                     (const char *, uint32_t), name, name_type_mask);

  lldb::SBSymbolContextList sb_sc_list;
  if (!name || !name[0])
    return LLDB_RECORD_RESULT(sb_sc_list);

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return LLDB_RECORD_RESULT(sb_sc_list);

  ModuleFunctionSearchOptions function_options;
  function_options.include_symbols = true;
  function_options.include_inlines = true;

  FunctionNameType mask = static_cast<FunctionNameType>(name_type_mask);
  target_sp->GetImages().FindFunctions(ConstString(name), mask,
                                       function_options, *sb_sc_list);
  return LLDB_RECORD_RESULT(sb_sc_list);
}

lldb::SBSymbolContextList SBTarget::FindGlobalFunctions(const char *name,
                                                        uint32_t max_matches,
                                                        MatchType matchtype) {
  LLDB_RECORD_METHOD(lldb::SBSymbolContextList, SBTarget, FindGlobalFunctions,
                     (const char *, uint32_t, lldb::MatchType), name,
                     max_matches, matchtype);

  lldb::SBSymbolContextList sb_sc_list;
  if (name && name[0]) {
    llvm::StringRef name_ref(name);
    TargetSP target_sp(GetSP());
    if (target_sp) {
      ModuleFunctionSearchOptions function_options;
      function_options.include_symbols = true;
      function_options.include_inlines = true;

      std::string regexstr;
      switch (matchtype) {
      case eMatchTypeRegex:
        target_sp->GetImages().FindFunctions(RegularExpression(name_ref),
                                             function_options, *sb_sc_list);
        break;
      case eMatchTypeStartsWith:
        regexstr = llvm::Regex::escape(name) + ".*";
        target_sp->GetImages().FindFunctions(RegularExpression(regexstr),
                                             function_options, *sb_sc_list);
        break;
      default:
        target_sp->GetImages().FindFunctions(ConstString(name),
                                             eFunctionNameTypeAny,
                                             function_options, *sb_sc_list);
        break;
      }
    }
  }
  return LLDB_RECORD_RESULT(sb_sc_list);
}

lldb::SBType SBTarget::FindFirstType(const char *typename_cstr) {
  LLDB_RECORD_METHOD(lldb::SBType, SBTarget, FindFirstType, (const char *),
                     typename_cstr);

  TargetSP target_sp(GetSP());
  if (typename_cstr && typename_cstr[0] && target_sp) {
    ConstString const_typename(typename_cstr);
    SymbolContext sc;
    const bool exact_match = false;

    const ModuleList &module_list = target_sp->GetImages();
    size_t count = module_list.GetSize();
    for (size_t idx = 0; idx < count; idx++) {
      ModuleSP module_sp(module_list.GetModuleAtIndex(idx));
      if (module_sp) {
        TypeSP type_sp(
            module_sp->FindFirstType(sc, const_typename, exact_match));
        if (type_sp)
          return LLDB_RECORD_RESULT(SBType(type_sp));
      }
    }

    // Didn't find the type in the symbols; Try the loaded language runtimes
    // BEGIN SWIFT
    // FIXME: This depends on clang, but should be able to support any
    // TypeSystem/compiler.
    if (auto process_sp = target_sp->GetProcessSP()) {
      for (auto *runtime : process_sp->GetLanguageRuntimes()) {
        if (auto vendor = runtime->GetDeclVendor()) {
          std::vector<CompilerDecl> decls;
          if (vendor->FindDecls(const_typename, /*append*/ true,
                                /*max_matches*/ 1, decls) > 0) {
            auto compiler_decl = decls.front();
            auto *ctx = llvm::dyn_cast<TypeSystemClang>(compiler_decl.GetTypeSystem());
            if (ctx)
              if (CompilerType type =
                      ctx->GetTypeForDecl(compiler_decl.GetOpaqueDecl()))
                return LLDB_RECORD_RESULT(SBType(type));
          }
        }
      }
    }
    // END SWIFT

    // No matches, search for basic typename matches
    for (auto *type_system : target_sp->GetScratchTypeSystems())
      if (auto type = type_system->GetBuiltinTypeByName(const_typename))
        return LLDB_RECORD_RESULT(SBType(type));
  }

  return LLDB_RECORD_RESULT(SBType());
}

SBType SBTarget::GetBasicType(lldb::BasicType type) {
  LLDB_RECORD_METHOD(lldb::SBType, SBTarget, GetBasicType, (lldb::BasicType),
                     type);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    for (auto *type_system : target_sp->GetScratchTypeSystems())
      if (auto compiler_type = type_system->GetBasicTypeFromAST(type))
        return LLDB_RECORD_RESULT(SBType(compiler_type));
  }
  return LLDB_RECORD_RESULT(SBType());
}

lldb::SBTypeList SBTarget::FindTypes(const char *typename_cstr) {
  LLDB_RECORD_METHOD(lldb::SBTypeList, SBTarget, FindTypes, (const char *),
                     typename_cstr);

  SBTypeList sb_type_list;
  TargetSP target_sp(GetSP());
  if (typename_cstr && typename_cstr[0] && target_sp) {
    ModuleList &images = target_sp->GetImages();
    ConstString const_typename(typename_cstr);
    bool exact_match = false;
    TypeList type_list;
    llvm::DenseSet<SymbolFile *> searched_symbol_files;
    images.FindTypes(nullptr, const_typename, exact_match, UINT32_MAX,
                     searched_symbol_files, type_list);

    for (size_t idx = 0; idx < type_list.GetSize(); idx++) {
      TypeSP type_sp(type_list.GetTypeAtIndex(idx));
      if (type_sp)
        sb_type_list.Append(SBType(type_sp));
    }

    // Try the loaded language runtimes
    // BEGIN SWIFT
    // FIXME: This depends on clang, but should be able to support any
    // TypeSystem/compiler.
    if (auto process_sp = target_sp->GetProcessSP()) {
      for (auto *runtime : process_sp->GetLanguageRuntimes()) {
        if (auto *vendor = runtime->GetDeclVendor()) {
          std::vector<CompilerDecl> decls;
          if (vendor->FindDecls(const_typename, /*append*/ true,
                                /*max_matches*/ 1, decls) > 0) {
            for (auto decl : decls) {
              auto *ctx = llvm::dyn_cast<TypeSystemClang>(decl.GetTypeSystem());
              if (ctx)
                if (CompilerType type =
                        ctx->GetTypeForDecl(decl.GetOpaqueDecl()))
                  sb_type_list.Append(SBType(type));
            }
          }
        }
      }
    }
    // END SWIFT

    if (sb_type_list.GetSize() == 0) {
      // No matches, search for basic typename matches
      for (auto *type_system : target_sp->GetScratchTypeSystems())
        if (auto compiler_type =
                type_system->GetBuiltinTypeByName(const_typename))
          sb_type_list.Append(SBType(compiler_type));
    }
  }
  return LLDB_RECORD_RESULT(sb_type_list);
}

SBValueList SBTarget::FindGlobalVariables(const char *name,
                                          uint32_t max_matches) {
  LLDB_RECORD_METHOD(lldb::SBValueList, SBTarget, FindGlobalVariables,
                     (const char *, uint32_t), name, max_matches);

  SBValueList sb_value_list;

  TargetSP target_sp(GetSP());
  if (name && target_sp) {
    VariableList variable_list;
    target_sp->GetImages().FindGlobalVariables(ConstString(name), max_matches,
                                               variable_list);
    if (!variable_list.Empty()) {
      ExecutionContextScope *exe_scope = target_sp->GetProcessSP().get();
      if (exe_scope == nullptr)
        exe_scope = target_sp.get();
      for (const VariableSP &var_sp : variable_list) {
        lldb::ValueObjectSP valobj_sp(
            ValueObjectVariable::Create(exe_scope, var_sp));
        if (valobj_sp)
          sb_value_list.Append(SBValue(valobj_sp));
      }
    }
  }

  return LLDB_RECORD_RESULT(sb_value_list);
}

SBValueList SBTarget::FindGlobalVariables(const char *name,
                                          uint32_t max_matches,
                                          MatchType matchtype) {
  LLDB_RECORD_METHOD(lldb::SBValueList, SBTarget, FindGlobalVariables,
                     (const char *, uint32_t, lldb::MatchType), name,
                     max_matches, matchtype);

  SBValueList sb_value_list;

  TargetSP target_sp(GetSP());
  if (name && target_sp) {
    llvm::StringRef name_ref(name);
    VariableList variable_list;

    std::string regexstr;
    switch (matchtype) {
    case eMatchTypeNormal:
      target_sp->GetImages().FindGlobalVariables(ConstString(name), max_matches,
                                                 variable_list);
      break;
    case eMatchTypeRegex:
      target_sp->GetImages().FindGlobalVariables(RegularExpression(name_ref),
                                                 max_matches, variable_list);
      break;
    case eMatchTypeStartsWith:
      regexstr = llvm::Regex::escape(name) + ".*";
      target_sp->GetImages().FindGlobalVariables(RegularExpression(regexstr),
                                                 max_matches, variable_list);
      break;
    }
    if (!variable_list.Empty()) {
      ExecutionContextScope *exe_scope = target_sp->GetProcessSP().get();
      if (exe_scope == nullptr)
        exe_scope = target_sp.get();
      for (const VariableSP &var_sp : variable_list) {
        lldb::ValueObjectSP valobj_sp(
            ValueObjectVariable::Create(exe_scope, var_sp));
        if (valobj_sp)
          sb_value_list.Append(SBValue(valobj_sp));
      }
    }
  }

  return LLDB_RECORD_RESULT(sb_value_list);
}

lldb::SBValue SBTarget::FindFirstGlobalVariable(const char *name) {
  LLDB_RECORD_METHOD(lldb::SBValue, SBTarget, FindFirstGlobalVariable,
                     (const char *), name);

  SBValueList sb_value_list(FindGlobalVariables(name, 1));
  if (sb_value_list.IsValid() && sb_value_list.GetSize() > 0)
    return LLDB_RECORD_RESULT(sb_value_list.GetValueAtIndex(0));
  return LLDB_RECORD_RESULT(SBValue());
}

SBSourceManager SBTarget::GetSourceManager() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBSourceManager, SBTarget, GetSourceManager);

  SBSourceManager source_manager(*this);
  return LLDB_RECORD_RESULT(source_manager);
}

lldb::SBInstructionList SBTarget::ReadInstructions(lldb::SBAddress base_addr,
                                                   uint32_t count) {
  LLDB_RECORD_METHOD(lldb::SBInstructionList, SBTarget, ReadInstructions,
                     (lldb::SBAddress, uint32_t), base_addr, count);

  return LLDB_RECORD_RESULT(ReadInstructions(base_addr, count, nullptr));
}

lldb::SBInstructionList SBTarget::ReadInstructions(lldb::SBAddress base_addr,
                                                   uint32_t count,
                                                   const char *flavor_string) {
  LLDB_RECORD_METHOD(lldb::SBInstructionList, SBTarget, ReadInstructions,
                     (lldb::SBAddress, uint32_t, const char *), base_addr,
                     count, flavor_string);

  SBInstructionList sb_instructions;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    Address *addr_ptr = base_addr.get();

    if (addr_ptr) {
      DataBufferHeap data(
          target_sp->GetArchitecture().GetMaximumOpcodeByteSize() * count, 0);
      bool force_live_memory = true;
      lldb_private::Status error;
      lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;
      const size_t bytes_read =
          target_sp->ReadMemory(*addr_ptr, data.GetBytes(), data.GetByteSize(),
                                error, force_live_memory, &load_addr);
      const bool data_from_file = load_addr == LLDB_INVALID_ADDRESS;
      sb_instructions.SetDisassembler(Disassembler::DisassembleBytes(
          target_sp->GetArchitecture(), nullptr, flavor_string, *addr_ptr,
          data.GetBytes(), bytes_read, count, data_from_file));
    }
  }

  return LLDB_RECORD_RESULT(sb_instructions);
}

lldb::SBInstructionList SBTarget::GetInstructions(lldb::SBAddress base_addr,
                                                  const void *buf,
                                                  size_t size) {
  LLDB_RECORD_METHOD(lldb::SBInstructionList, SBTarget, GetInstructions,
                     (lldb::SBAddress, const void *, size_t), base_addr, buf,
                     size);

  return LLDB_RECORD_RESULT(
      GetInstructionsWithFlavor(base_addr, nullptr, buf, size));
}

lldb::SBInstructionList
SBTarget::GetInstructionsWithFlavor(lldb::SBAddress base_addr,
                                    const char *flavor_string, const void *buf,
                                    size_t size) {
  LLDB_RECORD_METHOD(lldb::SBInstructionList, SBTarget,
                     GetInstructionsWithFlavor,
                     (lldb::SBAddress, const char *, const void *, size_t),
                     base_addr, flavor_string, buf, size);

  SBInstructionList sb_instructions;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    Address addr;

    if (base_addr.get())
      addr = *base_addr.get();

    const bool data_from_file = true;

    sb_instructions.SetDisassembler(Disassembler::DisassembleBytes(
        target_sp->GetArchitecture(), nullptr, flavor_string, addr, buf, size,
        UINT32_MAX, data_from_file));
  }

  return LLDB_RECORD_RESULT(sb_instructions);
}

lldb::SBInstructionList SBTarget::GetInstructions(lldb::addr_t base_addr,
                                                  const void *buf,
                                                  size_t size) {
  LLDB_RECORD_METHOD(lldb::SBInstructionList, SBTarget, GetInstructions,
                     (lldb::addr_t, const void *, size_t), base_addr, buf,
                     size);

  return LLDB_RECORD_RESULT(GetInstructionsWithFlavor(
      ResolveLoadAddress(base_addr), nullptr, buf, size));
}

lldb::SBInstructionList
SBTarget::GetInstructionsWithFlavor(lldb::addr_t base_addr,
                                    const char *flavor_string, const void *buf,
                                    size_t size) {
  LLDB_RECORD_METHOD(lldb::SBInstructionList, SBTarget,
                     GetInstructionsWithFlavor,
                     (lldb::addr_t, const char *, const void *, size_t),
                     base_addr, flavor_string, buf, size);

  return LLDB_RECORD_RESULT(GetInstructionsWithFlavor(
      ResolveLoadAddress(base_addr), flavor_string, buf, size));
}

SBError SBTarget::SetSectionLoadAddress(lldb::SBSection section,
                                        lldb::addr_t section_base_addr) {
  LLDB_RECORD_METHOD(lldb::SBError, SBTarget, SetSectionLoadAddress,
                     (lldb::SBSection, lldb::addr_t), section,
                     section_base_addr);

  SBError sb_error;
  TargetSP target_sp(GetSP());
  if (target_sp) {
    if (!section.IsValid()) {
      sb_error.SetErrorStringWithFormat("invalid section");
    } else {
      SectionSP section_sp(section.GetSP());
      if (section_sp) {
        if (section_sp->IsThreadSpecific()) {
          sb_error.SetErrorString(
              "thread specific sections are not yet supported");
        } else {
          ProcessSP process_sp(target_sp->GetProcessSP());
          if (target_sp->SetSectionLoadAddress(section_sp, section_base_addr)) {
            ModuleSP module_sp(section_sp->GetModule());
            if (module_sp) {
              ModuleList module_list;
              module_list.Append(module_sp);
              target_sp->ModulesDidLoad(module_list);
            }
            // Flush info in the process (stack frames, etc)
            if (process_sp)
              process_sp->Flush();
          }
        }
      }
    }
  } else {
    sb_error.SetErrorString("invalid target");
  }
  return LLDB_RECORD_RESULT(sb_error);
}

SBError SBTarget::ClearSectionLoadAddress(lldb::SBSection section) {
  LLDB_RECORD_METHOD(lldb::SBError, SBTarget, ClearSectionLoadAddress,
                     (lldb::SBSection), section);

  SBError sb_error;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    if (!section.IsValid()) {
      sb_error.SetErrorStringWithFormat("invalid section");
    } else {
      SectionSP section_sp(section.GetSP());
      if (section_sp) {
        ProcessSP process_sp(target_sp->GetProcessSP());
        if (target_sp->SetSectionUnloaded(section_sp)) {
          ModuleSP module_sp(section_sp->GetModule());
          if (module_sp) {
            ModuleList module_list;
            module_list.Append(module_sp);
            target_sp->ModulesDidUnload(module_list, false);
          }
          // Flush info in the process (stack frames, etc)
          if (process_sp)
            process_sp->Flush();
        }
      } else {
        sb_error.SetErrorStringWithFormat("invalid section");
      }
    }
  } else {
    sb_error.SetErrorStringWithFormat("invalid target");
  }
  return LLDB_RECORD_RESULT(sb_error);
}

SBError SBTarget::SetModuleLoadAddress(lldb::SBModule module,
                                       int64_t slide_offset) {
  LLDB_RECORD_METHOD(lldb::SBError, SBTarget, SetModuleLoadAddress,
                     (lldb::SBModule, int64_t), module, slide_offset);

  SBError sb_error;

  TargetSP target_sp(GetSP());
  if (target_sp) {
    ModuleSP module_sp(module.GetSP());
    if (module_sp) {
      bool changed = false;
      if (module_sp->SetLoadAddress(*target_sp, slide_offset, true, changed)) {
        // The load was successful, make sure that at least some sections
        // changed before we notify that our module was loaded.
        if (changed) {
          ModuleList module_list;
          module_list.Append(module_sp);
          target_sp->ModulesDidLoad(module_list);
          // Flush info in the process (stack frames, etc)
          ProcessSP process_sp(target_sp->GetProcessSP());
          if (process_sp)
            process_sp->Flush();
        }
      }
    } else {
      sb_error.SetErrorStringWithFormat("invalid module");
    }

  } else {
    sb_error.SetErrorStringWithFormat("invalid target");
  }
  return LLDB_RECORD_RESULT(sb_error);
}

SBError SBTarget::ClearModuleLoadAddress(lldb::SBModule module) {
  LLDB_RECORD_METHOD(lldb::SBError, SBTarget, ClearModuleLoadAddress,
                     (lldb::SBModule), module);

  SBError sb_error;

  char path[PATH_MAX];
  TargetSP target_sp(GetSP());
  if (target_sp) {
    ModuleSP module_sp(module.GetSP());
    if (module_sp) {
      ObjectFile *objfile = module_sp->GetObjectFile();
      if (objfile) {
        SectionList *section_list = objfile->GetSectionList();
        if (section_list) {
          ProcessSP process_sp(target_sp->GetProcessSP());

          bool changed = false;
          const size_t num_sections = section_list->GetSize();
          for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
            SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));
            if (section_sp)
              changed |= target_sp->SetSectionUnloaded(section_sp);
          }
          if (changed) {
            ModuleList module_list;
            module_list.Append(module_sp);
            target_sp->ModulesDidUnload(module_list, false);
            // Flush info in the process (stack frames, etc)
            ProcessSP process_sp(target_sp->GetProcessSP());
            if (process_sp)
              process_sp->Flush();
          }
        } else {
          module_sp->GetFileSpec().GetPath(path, sizeof(path));
          sb_error.SetErrorStringWithFormat("no sections in object file '%s'",
                                            path);
        }
      } else {
        module_sp->GetFileSpec().GetPath(path, sizeof(path));
        sb_error.SetErrorStringWithFormat("no object file for module '%s'",
                                          path);
      }
    } else {
      sb_error.SetErrorStringWithFormat("invalid module");
    }
  } else {
    sb_error.SetErrorStringWithFormat("invalid target");
  }
  return LLDB_RECORD_RESULT(sb_error);
}

lldb::SBSymbolContextList SBTarget::FindSymbols(const char *name,
                                                lldb::SymbolType symbol_type) {
  LLDB_RECORD_METHOD(lldb::SBSymbolContextList, SBTarget, FindSymbols,
                     (const char *, lldb::SymbolType), name, symbol_type);

  SBSymbolContextList sb_sc_list;
  if (name && name[0]) {
    TargetSP target_sp(GetSP());
    if (target_sp)
      target_sp->GetImages().FindSymbolsWithNameAndType(
          ConstString(name), symbol_type, *sb_sc_list);
  }
  return LLDB_RECORD_RESULT(sb_sc_list);
}

lldb::SBValue SBTarget::EvaluateExpression(const char *expr) {
  LLDB_RECORD_METHOD(lldb::SBValue, SBTarget, EvaluateExpression,
                     (const char *), expr);

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return LLDB_RECORD_RESULT(SBValue());

  SBExpressionOptions options;
  lldb::DynamicValueType fetch_dynamic_value =
      target_sp->GetPreferDynamicValue();
  options.SetFetchDynamicValue(fetch_dynamic_value);
  options.SetUnwindOnError(true);
  return LLDB_RECORD_RESULT(EvaluateExpression(expr, options));
}

lldb::SBValue SBTarget::EvaluateExpression(const char *expr,
                                           const SBExpressionOptions &options) {
  LLDB_RECORD_METHOD(lldb::SBValue, SBTarget, EvaluateExpression,
                     (const char *, const lldb::SBExpressionOptions &), expr,
                     options);

  Log *expr_log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));
  SBValue expr_result;
  ValueObjectSP expr_value_sp;
  TargetSP target_sp(GetSP());
  StackFrame *frame = nullptr;
  if (target_sp) {
    if (expr == nullptr || expr[0] == '\0') {
      return LLDB_RECORD_RESULT(expr_result);
    }

    std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
    ExecutionContext exe_ctx(m_opaque_sp.get());


    frame = exe_ctx.GetFramePtr();
    Target *target = exe_ctx.GetTargetPtr();

    if (target) {
      target->EvaluateExpression(expr, frame, expr_value_sp, options.ref());

      expr_result.SetSP(expr_value_sp, options.GetFetchDynamicValue());
    }
  }
  LLDB_LOGF(expr_log,
            "** [SBTarget::EvaluateExpression] Expression result is "
            "%s, summary %s **",
            expr_result.GetValue(), expr_result.GetSummary());
  return LLDB_RECORD_RESULT(expr_result);
}

lldb::addr_t SBTarget::GetStackRedZoneSize() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::addr_t, SBTarget, GetStackRedZoneSize);

  TargetSP target_sp(GetSP());
  if (target_sp) {
    ABISP abi_sp;
    ProcessSP process_sp(target_sp->GetProcessSP());
    if (process_sp)
      abi_sp = process_sp->GetABI();
    else
      abi_sp = ABI::FindPlugin(ProcessSP(), target_sp->GetArchitecture());
    if (abi_sp)
      return abi_sp->GetRedZoneSize();
  }
  return 0;
}

bool SBTarget::IsLoaded(const SBModule &module) const {
  LLDB_RECORD_METHOD_CONST(bool, SBTarget, IsLoaded, (const lldb::SBModule &),
                           module);

  TargetSP target_sp(GetSP());
  if (!target_sp)
    return false;

  ModuleSP module_sp(module.GetSP());
  if (!module_sp)
    return false;

  return module_sp->IsLoadedInTarget(target_sp.get());
}

lldb::SBLaunchInfo SBTarget::GetLaunchInfo() const {
  LLDB_RECORD_METHOD_CONST_NO_ARGS(lldb::SBLaunchInfo, SBTarget, GetLaunchInfo);

  lldb::SBLaunchInfo launch_info(nullptr);
  TargetSP target_sp(GetSP());
  if (target_sp)
    launch_info.set_ref(m_opaque_sp->GetProcessLaunchInfo());
  return LLDB_RECORD_RESULT(launch_info);
}

void SBTarget::SetLaunchInfo(const lldb::SBLaunchInfo &launch_info) {
  LLDB_RECORD_METHOD(void, SBTarget, SetLaunchInfo,
                     (const lldb::SBLaunchInfo &), launch_info);

  TargetSP target_sp(GetSP());
  if (target_sp)
    m_opaque_sp->SetProcessLaunchInfo(launch_info.ref());
}

SBEnvironment SBTarget::GetEnvironment() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBEnvironment, SBTarget, GetEnvironment);
  TargetSP target_sp(GetSP());

  if (target_sp) {
    return LLDB_RECORD_RESULT(SBEnvironment(target_sp->GetEnvironment()));
  }

  return LLDB_RECORD_RESULT(SBEnvironment());
}

lldb::SBTrace SBTarget::GetTrace() {
  LLDB_RECORD_METHOD_NO_ARGS(lldb::SBTrace, SBTarget, GetTrace);
  TargetSP target_sp(GetSP());

  if (target_sp)
    return LLDB_RECORD_RESULT(SBTrace(target_sp->GetTrace()));

  return LLDB_RECORD_RESULT(SBTrace());
}

lldb::SBTrace SBTarget::CreateTrace(lldb::SBError &error) {
  LLDB_RECORD_METHOD(lldb::SBTrace, SBTarget, CreateTrace, (lldb::SBError &),
                     error);
  TargetSP target_sp(GetSP());
  error.Clear();

  if (target_sp) {
    if (llvm::Expected<lldb::TraceSP> trace_sp = target_sp->CreateTrace()) {
      return LLDB_RECORD_RESULT(SBTrace(*trace_sp));
    } else {
      error.SetErrorString(llvm::toString(trace_sp.takeError()).c_str());
    }
  } else {
    error.SetErrorString("missing target");
  }
  return LLDB_RECORD_RESULT(SBTrace());
}

namespace lldb_private {
namespace repro {

template <>
void RegisterMethods<SBTarget>(Registry &R) {
  LLDB_REGISTER_CONSTRUCTOR(SBTarget, ());
  LLDB_REGISTER_CONSTRUCTOR(SBTarget, (const lldb::SBTarget &));
  LLDB_REGISTER_CONSTRUCTOR(SBTarget, (const lldb::TargetSP &));
  LLDB_REGISTER_METHOD(const lldb::SBTarget &,
                       SBTarget, operator=,(const lldb::SBTarget &));
  LLDB_REGISTER_STATIC_METHOD(bool, SBTarget, EventIsTargetEvent,
                              (const lldb::SBEvent &));
  LLDB_REGISTER_STATIC_METHOD(lldb::SBTarget, SBTarget, GetTargetFromEvent,
                              (const lldb::SBEvent &));
  LLDB_REGISTER_STATIC_METHOD(uint32_t, SBTarget, GetNumModulesFromEvent,
                              (const lldb::SBEvent &));
  LLDB_REGISTER_STATIC_METHOD(lldb::SBModule, SBTarget,
                              GetModuleAtIndexFromEvent,
                              (const uint32_t, const lldb::SBEvent &));
  LLDB_REGISTER_STATIC_METHOD(const char *, SBTarget, GetBroadcasterClassName,
                              ());
  LLDB_REGISTER_METHOD_CONST(bool, SBTarget, IsValid, ());
  LLDB_REGISTER_METHOD_CONST(bool, SBTarget, operator bool, ());
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, GetProcess, ());
  LLDB_REGISTER_METHOD(lldb::SBPlatform, SBTarget, GetPlatform, ());
  LLDB_REGISTER_METHOD_CONST(lldb::SBDebugger, SBTarget, GetDebugger, ());
  LLDB_REGISTER_METHOD(lldb::SBStructuredData, SBTarget, GetStatistics, ());
  LLDB_REGISTER_METHOD(void, SBTarget, SetCollectingStats, (bool));
  LLDB_REGISTER_METHOD(bool, SBTarget, GetCollectingStats, ());
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, LoadCore, (const char *));
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, LoadCore,
                       (const char *, lldb::SBError &));
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, LaunchSimple,
                       (const char **, const char **, const char *));
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, Install, ());
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, Launch,
                       (lldb::SBListener &, const char **, const char **,
                        const char *, const char *, const char *,
                        const char *, uint32_t, bool, lldb::SBError &));
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, Launch,
                       (lldb::SBLaunchInfo &, lldb::SBError &));
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, Attach,
                       (lldb::SBAttachInfo &, lldb::SBError &));
  LLDB_REGISTER_METHOD(lldb::SBProcess, SBTarget, AttachToProcessWithID,
                       (lldb::SBListener &, lldb::pid_t, lldb::SBError &));
  LLDB_REGISTER_METHOD(
      lldb::SBProcess, SBTarget, AttachToProcessWithName,
      (lldb::SBListener &, const char *, bool, lldb::SBError &));
  LLDB_REGISTER_METHOD(
      lldb::SBProcess, SBTarget, ConnectRemote,
      (lldb::SBListener &, const char *, const char *, lldb::SBError &));
  LLDB_REGISTER_METHOD(lldb::SBFileSpec, SBTarget, GetExecutable, ());
  LLDB_REGISTER_METHOD_CONST(bool,
                             SBTarget, operator==,(const lldb::SBTarget &));
  LLDB_REGISTER_METHOD_CONST(bool,
                             SBTarget, operator!=,(const lldb::SBTarget &));
  LLDB_REGISTER_METHOD(lldb::SBAddress, SBTarget, ResolveLoadAddress,
                       (lldb::addr_t));
  LLDB_REGISTER_METHOD(lldb::SBAddress, SBTarget, ResolveFileAddress,
                       (lldb::addr_t));
  LLDB_REGISTER_METHOD(lldb::SBAddress, SBTarget, ResolvePastLoadAddress,
                       (uint32_t, lldb::addr_t));
  LLDB_REGISTER_METHOD(lldb::SBSymbolContext, SBTarget,
                       ResolveSymbolContextForAddress,
                       (const lldb::SBAddress &, uint32_t));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateByLocation, (const char *, uint32_t));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateByLocation,
                       (const lldb::SBFileSpec &, uint32_t));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateByLocation,
                       (const lldb::SBFileSpec &, uint32_t, lldb::addr_t));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateByLocation,
                       (const lldb::SBFileSpec &, uint32_t, lldb::addr_t,
                        lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateByLocation,
                       (const lldb::SBFileSpec &, uint32_t, uint32_t,
                        lldb::addr_t, lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByLocation,
                       (const lldb::SBFileSpec &, uint32_t, uint32_t,
                        lldb::addr_t, lldb::SBFileSpecList &, bool));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                       (const char *, const char *));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                       (const char *, const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                       (const char *, uint32_t, const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByName,
                       (const char *, uint32_t, lldb::LanguageType,
                        const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByNames,
                       (const char **, uint32_t, uint32_t,
                        const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByNames,
                       (const char **, uint32_t, uint32_t, lldb::LanguageType,
                        const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByNames,
                       (const char **, uint32_t, uint32_t, lldb::LanguageType,
                        lldb::addr_t, const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByRegex,
                       (const char *, const char *));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByRegex,
                       (const char *, const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, BreakpointCreateByRegex,
                       (const char *, lldb::LanguageType,
                        const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateByAddress, (lldb::addr_t));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateBySBAddress, (lldb::SBAddress &));
  LLDB_REGISTER_METHOD(
      lldb::SBBreakpoint, SBTarget, BreakpointCreateBySourceRegex,
      (const char *, const lldb::SBFileSpec &, const char *));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateBySourceRegex,
                       (const char *, const lldb::SBFileSpecList &,
                        const lldb::SBFileSpecList &));
  LLDB_REGISTER_METHOD(
      lldb::SBBreakpoint, SBTarget, BreakpointCreateBySourceRegex,
      (const char *, const lldb::SBFileSpecList &,
       const lldb::SBFileSpecList &, const lldb::SBStringList &));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateForException,
                       (lldb::LanguageType, bool, bool));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget,
                       BreakpointCreateForException,
                       (lldb::LanguageType, bool, bool, SBStringList &));
  LLDB_REGISTER_METHOD(
      lldb::SBBreakpoint, SBTarget, BreakpointCreateFromScript,
      (const char *, lldb::SBStructuredData &, const lldb::SBFileSpecList &,
       const lldb::SBFileSpecList &, bool));
  LLDB_REGISTER_METHOD_CONST(uint32_t, SBTarget, GetNumBreakpoints, ());
  LLDB_REGISTER_METHOD_CONST(lldb::SBBreakpoint, SBTarget,
                             GetBreakpointAtIndex, (uint32_t));
  LLDB_REGISTER_METHOD(bool, SBTarget, BreakpointDelete, (lldb::break_id_t));
  LLDB_REGISTER_METHOD(lldb::SBBreakpoint, SBTarget, FindBreakpointByID,
                       (lldb::break_id_t));
  LLDB_REGISTER_METHOD(bool, SBTarget, FindBreakpointsByName,
                       (const char *, lldb::SBBreakpointList &));
  LLDB_REGISTER_METHOD(void, SBTarget, GetBreakpointNames,
                       (lldb::SBStringList &));
  LLDB_REGISTER_METHOD(void, SBTarget, DeleteBreakpointName, (const char *));
  LLDB_REGISTER_METHOD(bool, SBTarget, EnableAllBreakpoints, ());
  LLDB_REGISTER_METHOD(bool, SBTarget, DisableAllBreakpoints, ());
  LLDB_REGISTER_METHOD(bool, SBTarget, DeleteAllBreakpoints, ());
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, BreakpointsCreateFromFile,
                       (lldb::SBFileSpec &, lldb::SBBreakpointList &));
  LLDB_REGISTER_METHOD(
      lldb::SBError, SBTarget, BreakpointsCreateFromFile,
      (lldb::SBFileSpec &, lldb::SBStringList &, lldb::SBBreakpointList &));
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, BreakpointsWriteToFile,
                       (lldb::SBFileSpec &));
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, BreakpointsWriteToFile,
                       (lldb::SBFileSpec &, lldb::SBBreakpointList &, bool));
  LLDB_REGISTER_METHOD_CONST(uint32_t, SBTarget, GetNumWatchpoints, ());
  LLDB_REGISTER_METHOD_CONST(lldb::SBWatchpoint, SBTarget,
                             GetWatchpointAtIndex, (uint32_t));
  LLDB_REGISTER_METHOD(bool, SBTarget, DeleteWatchpoint, (lldb::watch_id_t));
  LLDB_REGISTER_METHOD(lldb::SBWatchpoint, SBTarget, FindWatchpointByID,
                       (lldb::watch_id_t));
  LLDB_REGISTER_METHOD(lldb::SBWatchpoint, SBTarget, WatchAddress,
                       (lldb::addr_t, size_t, bool, bool, lldb::SBError &));
  LLDB_REGISTER_METHOD(bool, SBTarget, EnableAllWatchpoints, ());
  LLDB_REGISTER_METHOD(bool, SBTarget, DisableAllWatchpoints, ());
  LLDB_REGISTER_METHOD(lldb::SBValue, SBTarget, CreateValueFromAddress,
                       (const char *, lldb::SBAddress, lldb::SBType));
  LLDB_REGISTER_METHOD(lldb::SBValue, SBTarget, CreateValueFromData,
                       (const char *, lldb::SBData, lldb::SBType));
  LLDB_REGISTER_METHOD(lldb::SBValue, SBTarget, CreateValueFromExpression,
                       (const char *, const char *));
  LLDB_REGISTER_METHOD(bool, SBTarget, DeleteAllWatchpoints, ());
  LLDB_REGISTER_METHOD(void, SBTarget, AppendImageSearchPath,
                       (const char *, const char *, lldb::SBError &));
  LLDB_REGISTER_METHOD(lldb::SBModule, SBTarget, AddModule,
                       (const char *, const char *, const char *));
  LLDB_REGISTER_METHOD(
      lldb::SBModule, SBTarget, AddModule,
      (const char *, const char *, const char *, const char *));
  LLDB_REGISTER_METHOD(lldb::SBModule, SBTarget, AddModule,
                       (const lldb::SBModuleSpec &));
  LLDB_REGISTER_METHOD(bool, SBTarget, AddModule, (lldb::SBModule &));
  LLDB_REGISTER_METHOD_CONST(uint32_t, SBTarget, GetNumModules, ());
  LLDB_REGISTER_METHOD(void, SBTarget, Clear, ());
  LLDB_REGISTER_METHOD(lldb::SBModule, SBTarget, FindModule,
                       (const lldb::SBFileSpec &));
  LLDB_REGISTER_METHOD(lldb::SBSymbolContextList, SBTarget, FindCompileUnits,
                       (const lldb::SBFileSpec &));
  LLDB_REGISTER_METHOD(lldb::ByteOrder, SBTarget, GetByteOrder, ());
  LLDB_REGISTER_METHOD(const char *, SBTarget, GetTriple, ());
  LLDB_REGISTER_METHOD(uint32_t, SBTarget, GetDataByteSize, ());
  LLDB_REGISTER_METHOD(uint32_t, SBTarget, GetCodeByteSize, ());
  LLDB_REGISTER_METHOD_CONST(uint32_t, SBTarget, GetMaximumNumberOfChildrenToDisplay,());
  LLDB_REGISTER_METHOD(uint32_t, SBTarget, GetAddressByteSize, ());
  LLDB_REGISTER_METHOD(lldb::SBModule, SBTarget, GetModuleAtIndex,
                       (uint32_t));
  LLDB_REGISTER_METHOD(bool, SBTarget, RemoveModule, (lldb::SBModule));
  LLDB_REGISTER_METHOD_CONST(lldb::SBBroadcaster, SBTarget, GetBroadcaster,
                             ());
  LLDB_REGISTER_METHOD(bool, SBTarget, GetDescription,
                       (lldb::SBStream &, lldb::DescriptionLevel));
  LLDB_REGISTER_METHOD(lldb::SBSymbolContextList, SBTarget, FindFunctions,
                       (const char *, uint32_t));
  LLDB_REGISTER_METHOD(lldb::SBSymbolContextList, SBTarget,
                       FindGlobalFunctions,
                       (const char *, uint32_t, lldb::MatchType));
  LLDB_REGISTER_METHOD(lldb::SBType, SBTarget, FindFirstType, (const char *));
  LLDB_REGISTER_METHOD(lldb::SBType, SBTarget, GetBasicType,
                       (lldb::BasicType));
  LLDB_REGISTER_METHOD(lldb::SBTypeList, SBTarget, FindTypes, (const char *));
  LLDB_REGISTER_METHOD(lldb::SBValueList, SBTarget, FindGlobalVariables,
                       (const char *, uint32_t));
  LLDB_REGISTER_METHOD(lldb::SBValueList, SBTarget, FindGlobalVariables,
                       (const char *, uint32_t, lldb::MatchType));
  LLDB_REGISTER_METHOD(lldb::SBValue, SBTarget, FindFirstGlobalVariable,
                       (const char *));
  LLDB_REGISTER_METHOD(lldb::SBSourceManager, SBTarget, GetSourceManager, ());
  LLDB_REGISTER_METHOD(lldb::SBInstructionList, SBTarget, ReadInstructions,
                       (lldb::SBAddress, uint32_t));
  LLDB_REGISTER_METHOD(lldb::SBInstructionList, SBTarget, ReadInstructions,
                       (lldb::SBAddress, uint32_t, const char *));
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, SetSectionLoadAddress,
                       (lldb::SBSection, lldb::addr_t));
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, ClearSectionLoadAddress,
                       (lldb::SBSection));
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, SetModuleLoadAddress,
                       (lldb::SBModule, int64_t));
  LLDB_REGISTER_METHOD(lldb::SBError, SBTarget, ClearModuleLoadAddress,
                       (lldb::SBModule));
  LLDB_REGISTER_METHOD(lldb::SBSymbolContextList, SBTarget, FindSymbols,
                       (const char *, lldb::SymbolType));
  LLDB_REGISTER_METHOD(lldb::SBValue, SBTarget, EvaluateExpression,
                       (const char *));
  LLDB_REGISTER_METHOD(lldb::SBValue, SBTarget, EvaluateExpression,
                       (const char *, const lldb::SBExpressionOptions &));
  LLDB_REGISTER_METHOD(lldb::addr_t, SBTarget, GetStackRedZoneSize, ());
  LLDB_REGISTER_METHOD_CONST(bool, SBTarget, IsLoaded,
                             (const lldb::SBModule &));
  LLDB_REGISTER_METHOD_CONST(lldb::SBLaunchInfo, SBTarget, GetLaunchInfo, ());
  LLDB_REGISTER_METHOD(void, SBTarget, SetLaunchInfo,
                       (const lldb::SBLaunchInfo &));
  LLDB_REGISTER_METHOD(
      size_t, SBTarget, ReadMemory,
      (const lldb::SBAddress, void *, size_t, lldb::SBError &));
  LLDB_REGISTER_METHOD(lldb::SBInstructionList, SBTarget, GetInstructions,
                       (lldb::SBAddress, const void *, size_t));
  LLDB_REGISTER_METHOD(lldb::SBInstructionList, SBTarget,
                       GetInstructionsWithFlavor,
                       (lldb::SBAddress, const char *, const void *, size_t));
  LLDB_REGISTER_METHOD(lldb::SBInstructionList, SBTarget, GetInstructions,
                       (lldb::addr_t, const void *, size_t));
  LLDB_REGISTER_METHOD(lldb::SBInstructionList, SBTarget,
                       GetInstructionsWithFlavor,
                       (lldb::addr_t, const char *, const void *, size_t));
  LLDB_REGISTER_METHOD(lldb::SBEnvironment, SBTarget, GetEnvironment, ());
  LLDB_REGISTER_METHOD(lldb::SBTrace, SBTarget, GetTrace, ());
  LLDB_REGISTER_METHOD(lldb::SBTrace, SBTarget, CreateTrace, (lldb::SBError &));
}

}
}
