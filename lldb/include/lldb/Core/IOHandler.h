//===-- IOHandler.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_IOHANDLER_H
#define LLDB_CORE_IOHANDLER_H

#include "lldb/Core/ValueObjectList.h"
#include "lldb/Host/Config.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cstdint>
#include <cstdio>

namespace lldb_private {
class Debugger;
namespace repro {
class DataRecorder;
}
}

namespace curses {
class Application;
typedef std::unique_ptr<Application> ApplicationAP;
} // namespace curses

namespace lldb_private {

class IOHandler {
public:
  enum class Type {
    CommandInterpreter,
    CommandList,
    Confirm,
    Curses,
    Expression,
    REPL,
    ProcessIO,
    PythonInterpreter,
    LuaInterpreter,
    PythonCode,
    Other
  };

  IOHandler(Debugger &debugger, IOHandler::Type type);

  IOHandler(Debugger &debugger, IOHandler::Type type,
            const lldb::FileSP &input_sp, const lldb::StreamFileSP &output_sp,
            const lldb::StreamFileSP &error_sp, uint32_t flags,
            repro::DataRecorder *data_recorder);

  virtual ~IOHandler();

  // Each IOHandler gets to run until it is done. It should read data from the
  // "in" and place output into "out" and "err and return when done.
  virtual void Run() = 0;

  // Called when an input reader should relinquish its control so another can
  // be pushed onto the IO handler stack, or so the current IO handler can pop
  // itself off the stack

  virtual void Cancel() = 0;

  // Called when CTRL+C is pressed which usually causes
  // Debugger::DispatchInputInterrupt to be called.

  virtual bool Interrupt() = 0;

  virtual void GotEOF() = 0;

  virtual bool IsActive() { return m_active && !m_done; }

  virtual void SetIsDone(bool b) { m_done = b; }

  virtual bool GetIsDone() { return m_done; }

  Type GetType() const { return m_type; }

  virtual void Activate() { m_active = true; }

  virtual void Deactivate() { m_active = false; }

  virtual void TerminalSizeChanged() {}

  virtual const char *GetPrompt() {
    // Prompt support isn't mandatory
    return nullptr;
  }

  virtual bool SetPrompt(llvm::StringRef prompt) {
    // Prompt support isn't mandatory
    return false;
  }
  bool SetPrompt(const char *) = delete;

  virtual ConstString GetControlSequence(char ch) { return ConstString(); }

  virtual const char *GetCommandPrefix() { return nullptr; }

  virtual const char *GetHelpPrologue() { return nullptr; }

  int GetInputFD();

  int GetOutputFD();

  int GetErrorFD();

  FILE *GetInputFILE();

  FILE *GetOutputFILE();

  FILE *GetErrorFILE();

  lldb::FileSP GetInputFileSP();

  lldb::StreamFileSP GetOutputStreamFileSP();

  lldb::StreamFileSP GetErrorStreamFileSP();

  Debugger &GetDebugger() { return m_debugger; }

  void *GetUserData() { return m_user_data; }

  void SetUserData(void *user_data) { m_user_data = user_data; }

  Flags &GetFlags() { return m_flags; }

  const Flags &GetFlags() const { return m_flags; }

  /// Check if the input is being supplied interactively by a user
  ///
  /// This will return true if the input stream is a terminal (tty or
  /// pty) and can cause IO handlers to do different things (like
  /// for a confirmation when deleting all breakpoints).
  bool GetIsInteractive();

  /// Check if the input is coming from a real terminal.
  ///
  /// A real terminal has a valid size with a certain number of rows
  /// and columns. If this function returns true, then terminal escape
  /// sequences are expected to work (cursor movement escape sequences,
  /// clearing lines, etc).
  bool GetIsRealTerminal();

  void SetPopped(bool b);

  void WaitForPop();

  virtual void PrintAsync(Stream *stream, const char *s, size_t len) {
    stream->Write(s, len);
    stream->Flush();
  }

protected:
  Debugger &m_debugger;
  lldb::FileSP m_input_sp;
  lldb::StreamFileSP m_output_sp;
  lldb::StreamFileSP m_error_sp;
  repro::DataRecorder *m_data_recorder;
  Predicate<bool> m_popped;
  Flags m_flags;
  Type m_type;
  void *m_user_data;
  bool m_done;
  bool m_active;

private:
  IOHandler(const IOHandler &) = delete;
  const IOHandler &operator=(const IOHandler &) = delete;
};

/// A delegate class for use with IOHandler subclasses.
///
/// The IOHandler delegate is designed to be mixed into classes so
/// they can use an IOHandler subclass to fetch input and notify the
/// object that inherits from this delegate class when a token is
/// received.
class IOHandlerDelegate {
public:
  enum class Completion { None, LLDBCommand, Expression };

  IOHandlerDelegate(Completion completion = Completion::None)
      : m_completion(completion) {}

  virtual ~IOHandlerDelegate() = default;

  virtual void IOHandlerActivated(IOHandler &io_handler, bool interactive) {}

  virtual void IOHandlerDeactivated(IOHandler &io_handler) {}

  virtual llvm::Optional<std::string> IOHandlerSuggestion(IOHandler &io_handler,
                                                          llvm::StringRef line);

  virtual void IOHandlerComplete(IOHandler &io_handler,
                                 CompletionRequest &request);

  virtual const char *IOHandlerGetFixIndentationCharacters() { return nullptr; }

  /// Called when a new line is created or one of an identified set of
  /// indentation characters is typed.
  ///
  /// This function determines how much indentation should be added
  /// or removed to match the recommended amount for the final line.
  ///
  /// \param[in] io_handler
  ///     The IOHandler that responsible for input.
  ///
  /// \param[in] lines
  ///     The current input up to the line to be corrected.  Lines
  ///     following the line containing the cursor are not included.
  ///
  /// \param[in] cursor_position
  ///     The number of characters preceding the cursor on the final
  ///     line at the time.
  ///
  /// \return
  ///     Returns an integer describing the number of spaces needed
  ///     to correct the indentation level.  Positive values indicate
  ///     that spaces should be added, while negative values represent
  ///     spaces that should be removed.
  virtual int IOHandlerFixIndentation(IOHandler &io_handler,
                                      const StringList &lines,
                                      int cursor_position) {
    return 0;
  }

  /// Called when a line or lines have been retrieved.
  ///
  /// This function can handle the current line and possibly call
  /// IOHandler::SetIsDone(true) when the IO handler is done like when
  /// "quit" is entered as a command, of when an empty line is
  /// received. It is up to the delegate to determine when a line
  /// should cause a IOHandler to exit.
  virtual void IOHandlerInputComplete(IOHandler &io_handler,
                                      std::string &data) = 0;

  virtual void IOHandlerInputInterrupted(IOHandler &io_handler,
                                         std::string &data) {}

  /// Called to determine whether typing enter after the last line in
  /// \a lines should end input.  This function will not be called on
  /// IOHandler objects that are getting single lines.
  /// \param[in] io_handler
  ///     The IOHandler that responsible for updating the lines.
  ///
  /// \param[in] lines
  ///     The current multi-line content.  May be altered to provide
  ///     alternative input when complete.
  ///
  /// \return
  ///     Return an boolean to indicate whether input is complete,
  ///     true indicates that no additional input is necessary, while
  ///     false indicates that more input is required.
  virtual bool IOHandlerIsInputComplete(IOHandler &io_handler,
                                        StringList &lines) {
    // Impose no requirements for input to be considered complete.  subclasses
    // should do something more intelligent.
    return true;
  }

  virtual ConstString IOHandlerGetControlSequence(char ch) {
    return ConstString();
  }

  virtual const char *IOHandlerGetCommandPrefix() { return nullptr; }

  virtual const char *IOHandlerGetHelpPrologue() { return nullptr; }

  // Intercept the IOHandler::Interrupt() calls and do something.
  //
  // Return true if the interrupt was handled, false if the IOHandler should
  // continue to try handle the interrupt itself.
  virtual bool IOHandlerInterrupt(IOHandler &io_handler) { return false; }

protected:
  Completion m_completion; // Support for common builtin completions
};

// IOHandlerDelegateMultiline
//
// A IOHandlerDelegate that handles terminating multi-line input when
// the last line is equal to "end_line" which is specified in the constructor.
class IOHandlerDelegateMultiline : public IOHandlerDelegate {
public:
  IOHandlerDelegateMultiline(const char *end_line,
                             Completion completion = Completion::None)
      : IOHandlerDelegate(completion),
        m_end_line((end_line && end_line[0]) ? end_line : "") {}

  ~IOHandlerDelegateMultiline() override = default;

  ConstString IOHandlerGetControlSequence(char ch) override {
    if (ch == 'd')
      return ConstString(m_end_line + "\n");
    return ConstString();
  }

  bool IOHandlerIsInputComplete(IOHandler &io_handler,
                                StringList &lines) override {
    // Determine whether the end of input signal has been entered
    const size_t num_lines = lines.GetSize();
    if (num_lines > 0 && lines[num_lines - 1] == m_end_line) {
      // Remove the terminal line from "lines" so it doesn't appear in the
      // resulting input and return true to indicate we are done getting lines
      lines.PopBack();
      return true;
    }
    return false;
  }

protected:
  const std::string m_end_line;
};

class IOHandlerEditline : public IOHandler {
public:
  IOHandlerEditline(Debugger &debugger, IOHandler::Type type,
                    const char *editline_name, // Used for saving history files
                    llvm::StringRef prompt, llvm::StringRef continuation_prompt,
                    bool multi_line, bool color_prompts,
                    uint32_t line_number_start, // If non-zero show line numbers
                                                // starting at
                                                // 'line_number_start'
                    IOHandlerDelegate &delegate,
                    repro::DataRecorder *data_recorder);

  IOHandlerEditline(Debugger &debugger, IOHandler::Type type,
                    const lldb::FileSP &input_sp,
                    const lldb::StreamFileSP &output_sp,
                    const lldb::StreamFileSP &error_sp, uint32_t flags,
                    const char *editline_name, // Used for saving history files
                    llvm::StringRef prompt, llvm::StringRef continuation_prompt,
                    bool multi_line, bool color_prompts,
                    uint32_t line_number_start, // If non-zero show line numbers
                                                // starting at
                                                // 'line_number_start'
                    IOHandlerDelegate &delegate,
                    repro::DataRecorder *data_recorder);

  IOHandlerEditline(Debugger &, IOHandler::Type, const char *, const char *,
                    const char *, bool, bool, uint32_t,
                    IOHandlerDelegate &) = delete;

  IOHandlerEditline(Debugger &, IOHandler::Type, const lldb::FileSP &,
                    const lldb::StreamFileSP &, const lldb::StreamFileSP &,
                    uint32_t, const char *, const char *, const char *, bool,
                    bool, uint32_t, IOHandlerDelegate &) = delete;

  ~IOHandlerEditline() override;

  void Run() override;

  void Cancel() override;

  bool Interrupt() override;

  void GotEOF() override;

  void Activate() override;

  void Deactivate() override;

  void TerminalSizeChanged() override;

  ConstString GetControlSequence(char ch) override {
    return m_delegate.IOHandlerGetControlSequence(ch);
  }

  const char *GetCommandPrefix() override {
    return m_delegate.IOHandlerGetCommandPrefix();
  }

  const char *GetHelpPrologue() override {
    return m_delegate.IOHandlerGetHelpPrologue();
  }

  const char *GetPrompt() override;

  bool SetPrompt(llvm::StringRef prompt) override;
  bool SetPrompt(const char *prompt) = delete;

  const char *GetContinuationPrompt();

  void SetContinuationPrompt(llvm::StringRef prompt);
  void SetContinuationPrompt(const char *) = delete;

  bool GetLine(std::string &line, bool &interrupted);

  bool GetLines(StringList &lines, bool &interrupted);

  void SetBaseLineNumber(uint32_t line);

  bool GetInterruptExits() { return m_interrupt_exits; }

  void SetInterruptExits(bool b) { m_interrupt_exits = b; }

  const StringList *GetCurrentLines() const { return m_current_lines_ptr; }

  uint32_t GetCurrentLineIndex() const;

  void PrintAsync(Stream *stream, const char *s, size_t len) override;

private:
#if LLDB_ENABLE_LIBEDIT
  bool IsInputCompleteCallback(Editline *editline, StringList &lines);

  int FixIndentationCallback(Editline *editline, const StringList &lines,
                             int cursor_position);

  llvm::Optional<std::string> SuggestionCallback(llvm::StringRef line);

  void AutoCompleteCallback(CompletionRequest &request);
#endif

protected:
#if LLDB_ENABLE_LIBEDIT
  std::unique_ptr<Editline> m_editline_up;
#endif
  IOHandlerDelegate &m_delegate;
  std::string m_prompt;
  std::string m_continuation_prompt;
  StringList *m_current_lines_ptr;
  uint32_t m_base_line_number; // If non-zero, then show line numbers in prompt
  uint32_t m_curr_line_idx;
  bool m_multi_line;
  bool m_color_prompts;
  bool m_interrupt_exits;
  std::string m_line_buffer;
};

// The order of base classes is important. Look at the constructor of
// IOHandlerConfirm to see how.
class IOHandlerConfirm : public IOHandlerDelegate, public IOHandlerEditline {
public:
  IOHandlerConfirm(Debugger &debugger, llvm::StringRef prompt,
                   bool default_response);

  ~IOHandlerConfirm() override;

  bool GetResponse() const { return m_user_response; }

  void IOHandlerComplete(IOHandler &io_handler,
                         CompletionRequest &request) override;

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override;

protected:
  const bool m_default_response;
  bool m_user_response;
};

class IOHandlerStack {
public:
  IOHandlerStack() = default;

  size_t GetSize() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_stack.size();
  }

  void Push(const lldb::IOHandlerSP &sp) {
    if (sp) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      sp->SetPopped(false);
      m_stack.push_back(sp);
      // Set m_top the non-locking IsTop() call
      m_top = sp.get();

      UpdateREPLIsActive();
    }
  }

  bool IsEmpty() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_stack.empty();
  }

  lldb::IOHandlerSP Top() {
    lldb::IOHandlerSP sp;
    {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      if (!m_stack.empty())
        sp = m_stack.back();
    }
    return sp;
  }

  void Pop() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (!m_stack.empty()) {
      lldb::IOHandlerSP sp(m_stack.back());
      m_stack.pop_back();
      sp->SetPopped(true);
    }
    // Set m_top the non-locking IsTop() call
    m_top = (m_stack.empty() ? nullptr : m_stack.back().get());

    UpdateREPLIsActive();
  }

  std::recursive_mutex &GetMutex() { return m_mutex; }

  bool IsTop(const lldb::IOHandlerSP &io_handler_sp) const {
    return m_top == io_handler_sp.get();
  }

  bool CheckTopIOHandlerTypes(IOHandler::Type top_type,
                              IOHandler::Type second_top_type) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    const size_t num_io_handlers = m_stack.size();
    return (num_io_handlers >= 2 &&
            m_stack[num_io_handlers - 1]->GetType() == top_type &&
            m_stack[num_io_handlers - 2]->GetType() == second_top_type);
  }

  ConstString GetTopIOHandlerControlSequence(char ch) {
    return ((m_top != nullptr) ? m_top->GetControlSequence(ch) : ConstString());
  }

  const char *GetTopIOHandlerCommandPrefix() {
    return ((m_top != nullptr) ? m_top->GetCommandPrefix() : nullptr);
  }

  const char *GetTopIOHandlerHelpPrologue() {
    return ((m_top != nullptr) ? m_top->GetHelpPrologue() : nullptr);
  }

  // Returns true if the REPL is the active IOHandler or if it is just
  // below the Process IOHandler.
  bool REPLIsActive() {
    // This is calculated and cached by UpdateREPLIsActive() as IOHandlers
    // are pushed and popped since it gets called for all process events.
    return m_repl_active;
  }

  // Returns true if any REPL IOHandlers are anywhere on the stack
  bool REPLIsEnabled() {
    // This is calculated and cached by UpdateREPLIsActive() as IOHandlers
    // are pushed and popped since it gets called for all process events.
    return m_repl_enabled;
  }

  void PrintAsync(Stream *stream, const char *s, size_t len);

protected:
  void UpdateREPLIsActive() {
    m_repl_active = false;
    m_repl_enabled = false;
    // This function should only be called when the mutex is locked...
    if (m_top) {
      switch (m_top->GetType()) {
      case IOHandler::Type::ProcessIO:
        // Check the REPL is underneath the process IO handler...
        if (m_stack.size() > 1) {
          if (m_stack[m_stack.size() - 2]->GetType() == IOHandler::Type::REPL) {
            m_repl_active = true;
            m_repl_enabled = true;
          }
        }
        break;

      case IOHandler::Type::REPL:
        m_repl_active = true;
        m_repl_enabled = true;
        break;

      default:
        break;
      }
    }

    if (!m_repl_enabled) {
      for (const auto &io_handler_sp : m_stack) {
        if (io_handler_sp->GetType() == IOHandler::Type::REPL) {
          m_repl_enabled = true;
          break;
        }
      }
    }
  }

  typedef std::vector<lldb::IOHandlerSP> collection;
  collection m_stack;
  mutable std::recursive_mutex m_mutex;
  IOHandler *m_top = nullptr;
  /// REPL is the active IOHandler or right underneath the process IO handler.
  bool m_repl_active = false;
  // REPL is on IOHandler stack somewhere
  bool m_repl_enabled = false;

private:
  IOHandlerStack(const IOHandlerStack &) = delete;
  const IOHandlerStack &operator=(const IOHandlerStack &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_IOHANDLER_H
