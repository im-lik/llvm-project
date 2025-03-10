//===-- SymbolFile.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/SymbolFile.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-private.h"

#include <future>

using namespace lldb_private;
using namespace lldb;

char SymbolFile::ID;

void SymbolFile::PreloadSymbols() {
  // No-op for most implementations.
}

std::recursive_mutex &SymbolFile::GetModuleMutex() const {
  return GetObjectFile()->GetModule()->GetMutex();
}
ObjectFile *SymbolFile::GetMainObjectFile() {
  return m_objfile_sp->GetModule()->GetObjectFile();
}

SymbolFile *SymbolFile::FindPlugin(ObjectFileSP objfile_sp) {
  std::unique_ptr<SymbolFile> best_symfile_up;
  if (objfile_sp != nullptr) {

    // We need to test the abilities of this section list. So create what it
    // would be with this new objfile_sp.
    lldb::ModuleSP module_sp(objfile_sp->GetModule());
    if (module_sp) {
      // Default to the main module section list.
      ObjectFile *module_obj_file = module_sp->GetObjectFile();
      if (module_obj_file != objfile_sp.get()) {
        // Make sure the main object file's sections are created
        module_obj_file->GetSectionList();
        objfile_sp->CreateSections(*module_sp->GetUnifiedSectionList());
      }
    }

    // TODO: Load any plug-ins in the appropriate plug-in search paths and
    // iterate over all of them to find the best one for the job.

    uint32_t best_symfile_abilities = 0;

    SymbolFileCreateInstance create_callback;
    for (uint32_t idx = 0;
         (create_callback = PluginManager::GetSymbolFileCreateCallbackAtIndex(
              idx)) != nullptr;
         ++idx) {
      std::unique_ptr<SymbolFile> curr_symfile_up(create_callback(objfile_sp));

      if (curr_symfile_up) {
        const uint32_t sym_file_abilities = curr_symfile_up->GetAbilities();
        if (sym_file_abilities > best_symfile_abilities) {
          best_symfile_abilities = sym_file_abilities;
          best_symfile_up.reset(curr_symfile_up.release());
          // If any symbol file parser has all of the abilities, then we should
          // just stop looking.
          if ((kAllAbilities & sym_file_abilities) == kAllAbilities)
            break;
        }
      }
    }
    if (best_symfile_up) {
      // Let the winning symbol file parser initialize itself more completely
      // now that it has been chosen
      best_symfile_up->InitializeObject();
    }
  }
  return best_symfile_up.release();
}

llvm::Expected<TypeSystem &>
SymbolFile::GetTypeSystemForLanguage(lldb::LanguageType language) {
  auto type_system_or_err =
      m_objfile_sp->GetModule()->GetTypeSystemForLanguage(language);
  if (type_system_or_err) {
    type_system_or_err->SetSymbolFile(this);
  }
  return type_system_or_err;
}

bool SymbolFile::ForceInlineSourceFileCheck() {
  // Force checking for inline breakpoint locations for any JIT object files.
  // If we have a symbol file for something that has been JIT'ed, chances
  // are we used "#line" directives to point to the expression code and this
  // means we will have DWARF line tables that have source implementation
  // entries that do not match the compile unit source (usually a memory buffer)
  // file. Returning true for JIT files means all breakpoints set by file and
  // line
  // will be found correctly.
  return m_objfile_sp->GetType() == ObjectFile::eTypeJIT;
}

std::vector<lldb::DataBufferSP>
SymbolFile::GetASTData(lldb::LanguageType language) {
  // SymbolFile subclasses must add this functionality
  return std::vector<lldb::DataBufferSP>();
}

uint32_t
SymbolFile::ResolveSymbolContext(const SourceLocationSpec &src_location_spec,
                                 lldb::SymbolContextItem resolve_scope,
                                 SymbolContextList &sc_list) {
  return 0;
}

void SymbolFile::FindGlobalVariables(ConstString name,
                                     const CompilerDeclContext &parent_decl_ctx,
                                     uint32_t max_matches,
                                     VariableList &variables) {}

void SymbolFile::FindGlobalVariables(const RegularExpression &regex,
                                     uint32_t max_matches,
                                     VariableList &variables) {}

void SymbolFile::FindFunctions(ConstString name,
                               const CompilerDeclContext &parent_decl_ctx,
                               lldb::FunctionNameType name_type_mask,
                               bool include_inlines,
                               SymbolContextList &sc_list) {}

void SymbolFile::FindFunctions(const RegularExpression &regex,
                               bool include_inlines,
                               SymbolContextList &sc_list) {}

void SymbolFile::GetMangledNamesForFunction(
    const std::string &scope_qualified_name,
    std::vector<ConstString> &mangled_names) {
  return;
}

void SymbolFile::FindTypes(
    ConstString name, const CompilerDeclContext &parent_decl_ctx,
    uint32_t max_matches,
    llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
    TypeMap &types) {}

void SymbolFile::FindTypes(llvm::ArrayRef<CompilerContext> pattern,
                           LanguageSet languages,
                           llvm::DenseSet<SymbolFile *> &searched_symbol_files,
                           TypeMap &types) {}

void SymbolFile::AssertModuleLock() {
  // The code below is too expensive to leave enabled in release builds. It's
  // enabled in debug builds or when the correct macro is set.
#if defined(LLDB_CONFIGURATION_DEBUG)
  // We assert that we have to module lock by trying to acquire the lock from a
  // different thread. Note that we must abort if the result is true to
  // guarantee correctness.
  assert(std::async(
             std::launch::async,
             [this] {
               return this->GetModuleMutex().try_lock();
             }).get() == false &&
         "Module is not locked");
#endif
}

uint32_t SymbolFile::GetNumCompileUnits() {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (!m_compile_units) {
    // Create an array of compile unit shared pointers -- which will each
    // remain NULL until someone asks for the actual compile unit information.
    m_compile_units.emplace(CalculateNumCompileUnits());
  }
  return m_compile_units->size();
}

CompUnitSP SymbolFile::GetCompileUnitAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  uint32_t num = GetNumCompileUnits();
  if (idx >= num)
    return nullptr;
  lldb::CompUnitSP &cu_sp = (*m_compile_units)[idx];
  if (!cu_sp)
    cu_sp = ParseCompileUnitAtIndex(idx);
  return cu_sp;
}

void SymbolFile::SetCompileUnitAtIndex(uint32_t idx, const CompUnitSP &cu_sp) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  const size_t num_compile_units = GetNumCompileUnits();
  assert(idx < num_compile_units);
  (void)num_compile_units;

  // Fire off an assertion if this compile unit already exists for now. The
  // partial parsing should take care of only setting the compile unit
  // once, so if this assertion fails, we need to make sure that we don't
  // have a race condition, or have a second parse of the same compile
  // unit.
  assert((*m_compile_units)[idx] == nullptr);
  (*m_compile_units)[idx] = cu_sp;
}

Symtab *SymbolFile::GetSymtab() {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  if (m_symtab)
    return m_symtab;

  // Fetch the symtab from the main object file.
  m_symtab = GetMainObjectFile()->GetSymtab();

  // Then add our symbols to it.
  if (m_symtab)
    AddSymbols(*m_symtab);

  return m_symtab;
}

void SymbolFile::SectionFileAddressesChanged() {
  ObjectFile *module_objfile = GetMainObjectFile();
  ObjectFile *symfile_objfile = GetObjectFile();
  if (symfile_objfile != module_objfile)
    symfile_objfile->SectionFileAddressesChanged();
  if (m_symtab)
    m_symtab->SectionFileAddressesChanged();
}

void SymbolFile::Dump(Stream &s) {
  s.Format("SymbolFile {0} ({1})\n", GetPluginName(),
           GetMainObjectFile()->GetFileSpec());
  s.PutCString("Types:\n");
  m_type_list.Dump(&s, /*show_context*/ false);
  s.PutChar('\n');

  s.PutCString("Compile units:\n");
  if (m_compile_units) {
    for (const CompUnitSP &cu_sp : *m_compile_units) {
      // We currently only dump the compile units that have been parsed
      if (cu_sp)
        cu_sp->Dump(&s, /*show_context*/ false);
    }
  }
  s.PutChar('\n');

  if (Symtab *symtab = GetSymtab())
    symtab->Dump(&s, nullptr, eSortOrderNone);
}

SymbolFile::RegisterInfoResolver::~RegisterInfoResolver() = default;

uint64_t SymbolFile::GetDebugInfoSize() {
  if (!m_objfile_sp)
    return 0;
  ModuleSP module_sp(m_objfile_sp->GetModule());
  if (!module_sp)
    return 0;
  const SectionList *section_list = module_sp->GetSectionList();
  if (section_list)
    return section_list->GetDebugInfoSize();
  return 0;
}
