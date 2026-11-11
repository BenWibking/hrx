// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// loom-cxx: single-source C++ offload compiler driver.
//
// Compiles one hosted C++ translation unit to one host object that also
// carries the unit's device code. The source is preprocessed and type-checked
// once, by cxx with the hosted profile for the host triple. Both back ends
// consume that one translation unit:
//
//   1. Host: cxx's MLIR/LLVM pipeline lowers the unit to optimized LLVM IR.
//      This runs first, so translation (which may instantiate templates and
//      rewrite device bodies in the unit) cannot change host code.
//   2. Device: if host code references loom::kernel::kernel_ref descriptors,
//      Loom's importer translates the launched kernels of the same unit, and
//      the Loom compiler compiles them for --loom-target. Kernels the importer
//      or the device compiler rejects are dropped when the drop policy allows,
//      and the unit is translated again without them (it is not reparsed).
//   3. Output: the device code object is embedded in the host module, with a
//      static constructor passing it to --loom-register-function, and the
//      importer's kernel descriptors (weak COMDAT definitions of the
//      kernel_ref symbols) become module assembly. Descriptors the host
//      references but the device pass did not define are emitted without
//      their available flag, so launching them fails at run time instead of
//      at link time. The module is emitted as one object file.

#include <cxx/memory_layout.h>
#include <cxx/mlir/llvm_codegen.h>
#include <cxx/preprocessor.h>
#include <cxx/toolchain.h>
#include <cxx/translation_unit.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "iree/base/api.h"
#include "loom/error/diagnostic.h"
#include "loom/import/cxx/unit.h"
#include "loom/tooling/cli/loomc_result.h"
#include "loomc/artifact.h"
#include "loomc/compile.h"
#include "loomc/config.h"
#include "loomc/context.h"
#include "loomc/emit.h"
#include "loomc/import/cxx.h"
#include "loomc/iree.h"
#include "loomc/module.h"
#include "loomc/pass.h"
#include "loomc/result.h"
#include "loomc/source.h"
#include "loomc/target.h"
#include "loomc/target/configured.h"
#include "loomc/target/configured_ir.h"
#include "loomc/workspace.h"

namespace {

constexpr std::string_view kTool = "loom-cxx";
// Itanium mangling prefix of loom::kernel::kernel_ref<...> variables.
constexpr std::string_view kKernelRefPrefix = "_ZN4loom6kernel10kernel_ref";
// Rounds of compile-rejection exclusion, as in the previous two-pass driver.
constexpr int kMaxDeviceRounds = 5;

enum class DeviceFailurePolicy {
  // A failed device pass fails the compile.
  kError,
  // A failed device pass keeps the host object with unavailable descriptors.
  kHostOnly,
};

struct Options {
  std::string source;
  std::string output;
  bool compile = false;
  std::string triple = "x86_64-linux-gnu";
  std::string standard;
  int optimization_level = 0;
  std::vector<std::string> include_paths;
  std::vector<std::string> system_include_paths;
  std::vector<std::string> defines;
  // Dependency file options (-MD/-MMD/-MP/-MF/-MT/-MQ).
  bool dependency_file = false;
  bool dependency_skip_system = false;
  bool dependency_phony = false;
  std::string dependency_path;
  std::string dependency_target;
  // Device options.
  std::string loom_target;
  std::string loom_format;
  std::string register_function;
  bool drop_failed_kernels = false;
  DeviceFailurePolicy device_failure = DeviceFailurePolicy::kError;
  bool stats = false;
  bool verbose = false;
  std::string keep_directory;
  std::vector<std::string> ignored;
};

[[noreturn]] void usage_error(const std::string& message) {
  std::fprintf(stderr, "%s: error: %s\n", kTool.data(), message.c_str());
  std::fprintf(stderr, "run '%s --help' for usage\n", kTool.data());
  std::exit(1);
}

void print_help() {
  std::printf(
      "loom-cxx: single-source C++ offload compiler driver.\n"
      "\n"
      "Usage: loom-cxx [options] -c source.cpp -o object.o\n"
      "\n"
      "Parses the hosted C++ source once, generates host code from it with\n"
      "cxx's MLIR/LLVM pipeline, imports its launched kernels with Loom's\n"
      "C++ importer, compiles them for the device target and emits one host\n"
      "object that embeds the device code and defines the kernel descriptors.\n"
      "\n"
      "Compiler options:\n"
      "  -c                    compile to an object (required)\n"
      "  -o FILE               output object\n"
      "  -I DIR, -isystem DIR  user and system include directories; none are\n"
      "                        discovered implicitly\n"
      "  -D NAME[=VALUE]       macro definition\n"
      "  -std=STD              c++17, c++20, ... (gnu++NN reads as c++NN)\n"
      "  -O0 -O1 -O2 -O3 -Os   host optimization level\n"
      "  -MD -MMD -MP          dependency file, with -MF FILE, -MT/-MQ TARGET\n"
      "  --target=TRIPLE       host triple (default x86_64-linux-gnu)\n"
      "  -W* -f* -g* -m* -w -pthread -pipe -Xclang ARG are ignored.\n"
      "\n"
      "Loom options:\n"
      "  --loom-target=FAMILY:SELECTOR\n"
      "      device target, such as amdgpu:gfx90a\n"
      "  --loom-format=FORMAT\n"
      "      device artifact format (default amdgpu-hsaco for amdgpu)\n"
      "  --loom-register-function=NAME\n"
      "      extern \"C\" void NAME(const void*, size_t) receiving the\n"
      "      embedded code object from a static constructor\n"
      "  --loom-drop-failed-kernels\n"
      "      drop launched kernels that the importer or the device compiler\n"
      "      rejects and keep the rest; their descriptors stay unavailable\n"
      "  --loom-on-device-failure=error|host-only\n"
      "      a failed device pass fails the compile (default) or keeps a\n"
      "      host-only object\n"
      "  --loom-stats\n"
      "      report parse counts and phase times\n"
      "  --loom-keep=DIR\n"
      "      keep the device intermediates in DIR\n"
      "  --loom-verbose\n"
      "      print every device-pass diagnostic\n");
}

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.substr(0, prefix.size()) == prefix;
}

int parse_optimization_level(std::string_view flag) {
  auto level = flag.substr(2);
  if (level.empty()) {
    return 1;
  }
  if (level == "s" || level == "z" || level == "g") {
    return 2;
  }
  if (level == "fast") {
    return 3;
  }
  if (level.size() == 1 && level[0] >= '0' && level[0] <= '9') {
    return std::min(level[0] - '0', 3);
  }
  usage_error(std::format("unknown optimization level '{}'", flag));
}

Options parse_options(int argc, char** argv) {
  Options options;
  std::vector<std::string_view> args(argv + 1, argv + argc);
  auto value = [&](size_t& i, std::string_view flag) -> std::string {
    if (i + 1 >= args.size()) {
      usage_error(std::format("{} needs a value", flag));
    }
    return std::string(args[++i]);
  };
  // Joined (-Ifoo) or separate (-I foo) argument of a short option.
  auto joined = [&](size_t& i, std::string_view flag) -> std::string {
    if (args[i].size() > flag.size()) {
      return std::string(args[i].substr(flag.size()));
    }
    return value(i, flag);
  };
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      print_help();
      std::exit(0);
    } else if (arg == "-c") {
      options.compile = true;
    } else if (starts_with(arg, "-o")) {
      options.output = joined(i, "-o");
    } else if (starts_with(arg, "-isystem")) {
      options.system_include_paths.push_back(joined(i, "-isystem"));
    } else if (starts_with(arg, "-I")) {
      options.include_paths.push_back(joined(i, "-I"));
    } else if (starts_with(arg, "-D")) {
      options.defines.push_back(joined(i, "-D"));
    } else if (starts_with(arg, "-std=")) {
      auto standard = std::string(arg.substr(5));
      if (starts_with(standard, "gnu++")) {
        standard = "c++" + standard.substr(5);
      }
      options.standard = standard;
    } else if (starts_with(arg, "-O")) {
      options.optimization_level = parse_optimization_level(arg);
    } else if (arg == "-MD" || arg == "-MMD") {
      options.dependency_file = true;
      options.dependency_skip_system = arg == "-MMD";
    } else if (arg == "-MP") {
      options.dependency_phony = true;
    } else if (arg == "-MF") {
      options.dependency_path = value(i, arg);
    } else if (arg == "-MT" || arg == "-MQ") {
      options.dependency_target = value(i, arg);
    } else if (starts_with(arg, "--target=")) {
      options.triple = std::string(arg.substr(9));
    } else if (arg == "-target") {
      options.triple = value(i, arg);
    } else if (arg == "-x") {
      if (value(i, arg) != "c++") {
        usage_error("only C++ sources are supported");
      }
    } else if (starts_with(arg, "--loom-target=")) {
      options.loom_target = std::string(arg.substr(14));
    } else if (starts_with(arg, "--loom-format=")) {
      options.loom_format = std::string(arg.substr(14));
    } else if (starts_with(arg, "--loom-register-function=")) {
      options.register_function = std::string(arg.substr(25));
    } else if (arg == "--loom-drop-failed-kernels") {
      options.drop_failed_kernels = true;
    } else if (starts_with(arg, "--loom-on-device-failure=")) {
      auto policy = arg.substr(25);
      if (policy == "error") {
        options.device_failure = DeviceFailurePolicy::kError;
      } else if (policy == "host-only") {
        options.device_failure = DeviceFailurePolicy::kHostOnly;
      } else {
        usage_error(std::format("unknown device failure policy '{}'", policy));
      }
    } else if (arg == "--loom-stats") {
      options.stats = true;
    } else if (arg == "--loom-verbose") {
      options.verbose = true;
    } else if (starts_with(arg, "--loom-keep=")) {
      options.keep_directory = std::string(arg.substr(12));
    } else if (arg == "-Xclang") {
      options.ignored.push_back(std::string(arg) + " " + value(i, arg));
    } else if (starts_with(arg, "-W") || starts_with(arg, "-f") ||
               starts_with(arg, "-g") || starts_with(arg, "-m") ||
               arg == "-w" || arg == "-pthread" || arg == "-pipe" ||
               arg == "-pedantic") {
      // Diagnostics, code-model and debug flags of the surrounding build.
      // The hosted profile fixes exceptions off and non-PIC host code.
      options.ignored.push_back(std::string(arg));
    } else if (starts_with(arg, "-")) {
      usage_error(std::format("unsupported option '{}'", arg));
    } else {
      if (!options.source.empty()) {
        usage_error("expected exactly one source file");
      }
      options.source = std::string(arg);
    }
  }
  if (!options.compile) {
    usage_error("only compilation (-c) is supported");
  }
  if (options.source.empty()) {
    usage_error("no source file");
  }
  if (options.output.empty()) {
    options.output = std::filesystem::path(options.source)
                         .filename()
                         .replace_extension(".o")
                         .string();
  }
  if (!options.loom_target.empty() && options.loom_format.empty()) {
    if (starts_with(options.loom_target, "amdgpu:")) {
      options.loom_format = "amdgpu-hsaco";
    } else {
      usage_error("--loom-format is required for this --loom-target");
    }
  }
  return options;
}

// ---------------------------------------------------------------------------
// Small RAII and status helpers
// ---------------------------------------------------------------------------

template <typename T, void (*Release)(T*)>
struct Releaser {
  void operator()(T* value) const { Release(value); }
};
template <typename T, void (*Release)(T*)>
using Handle = std::unique_ptr<T, Releaser<T, Release>>;

using TargetEnvironment =
    Handle<loomc_target_environment_t, loomc_target_environment_release>;
using Context = Handle<loomc_context_t, loomc_context_release>;
using Workspace = Handle<loomc_workspace_t, loomc_workspace_release>;
using Source = Handle<loomc_source_t, loomc_source_release>;
using Module = Handle<loomc_module_t, loomc_module_release>;
using Result = Handle<loomc_result_t, loomc_result_release>;
using Unit = Handle<loomc_cxx_unit_t, loomc_cxx_unit_release>;
using Compiler = Handle<loomc_compiler_t, loomc_compiler_release>;
using PassProgram = Handle<loomc_pass_program_t, loomc_pass_program_release>;
using TargetProfile =
    Handle<loomc_target_profile_t, loomc_target_profile_release>;

// Thrown for infrastructure failures after the message has been printed.
struct Failure {};

void check(loomc_status_t status, std::string_view what) {
  if (loomc_status_is_ok(status)) {
    return;
  }
  iree_status_t iree_status = iree_status_from_loomc(status);
  std::fprintf(stderr, "%s: error: %.*s: ", kTool.data(), (int)what.size(),
               what.data());
  iree_status_fprint(stderr, iree_status);
  iree_status_free(iree_status);
  throw Failure{};
}

void check_iree(iree_status_t status, std::string_view what) {
  if (iree_status_is_ok(status)) {
    return;
  }
  std::fprintf(stderr, "%s: error: %.*s: ", kTool.data(), (int)what.size(),
               what.data());
  iree_status_fprint(stderr, status);
  iree_status_free(status);
  throw Failure{};
}

// Prints a result's diagnostics to a string; returns whether it succeeded.
bool print_result(const loomc_result_t* result, std::string& text) {
  char* buffer = nullptr;
  size_t size = 0;
  FILE* stream = open_memstream(&buffer, &size);
  if (!stream) {
    std::fprintf(stderr, "%s: error: cannot capture diagnostics\n",
                 kTool.data());
    throw Failure{};
  }
  bool succeeded = false;
  iree_status_t status =
      loom_tooling_cli_print_loomc_result(stream, result, &succeeded);
  std::fclose(stream);
  text.append(buffer, size);
  std::free(buffer);
  check_iree(status, "printing diagnostics");
  return succeeded;
}

std::vector<std::string_view> lines(std::string_view text) {
  std::vector<std::string_view> result;
  while (!text.empty()) {
    auto end = text.find('\n');
    result.push_back(text.substr(0, end));
    if (end == std::string_view::npos) {
      break;
    }
    text.remove_prefix(end + 1);
  }
  return result;
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  std::stringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

using Clock = std::chrono::steady_clock;
double seconds_since(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

class Driver {
 public:
  explicit Driver(Options options) : options_(std::move(options)) {}

  int run();

 private:
  void make_context(bool ir_only, TargetEnvironment& environment,
                    Context& context, Workspace& workspace);
  loomc_cxx_import_options_t import_options() const;
  bool parse();
  void write_dependency_file();
  bool generate_host_code();
  std::vector<std::string> referenced_kernel_refs() const;
  // Imports and compiles the device code, applying the drop policy. Returns
  // false when the device pass failed.
  bool device_pass();
  bool import_device_module(std::vector<std::string>& excluded, int round,
                            std::string& bytecode_out, bool& has_kernels);
  bool compile_device_module(const std::string& bytecode,
                             std::vector<std::string>& rejected);
  void device_failure(std::string_view stage, std::string_view log);
  void embed_device_code();
  void emit_object();
  std::string temp_path(std::string_view name) const;

  Options options_;
  std::string source_text_;
  // Import side: IR-only target environment, as loom-import-cxx uses.
  TargetEnvironment import_environment_;
  Context import_context_;
  Workspace import_workspace_;
  Unit unit_;
  // Host side.
  std::unique_ptr<llvm::TargetMachine> target_machine_;
  std::unique_ptr<llvm::LLVMContext> llvm_context_;
  std::unique_ptr<llvm::Module> host_module_;
  std::vector<std::string> kernel_refs_;
  // Device side.
  std::string work_directory_;
  bool keep_work_directory_ = false;
  std::string descriptors_;
  std::string code_object_;
  int import_count_ = 0;
  int device_compile_count_ = 0;
  // Phase times in seconds, for --loom-stats.
  double parse_seconds_ = 0, host_seconds_ = 0, import_seconds_ = 0,
         device_compile_seconds_ = 0, emit_seconds_ = 0;
};

std::string Driver::temp_path(std::string_view name) const {
  return (std::filesystem::path(work_directory_) / name).string();
}

void Driver::make_context(bool ir_only, TargetEnvironment& environment,
                          Context& context, Workspace& workspace) {
  const loomc_allocator_t allocator = loomc_allocator_system();
  loomc_target_environment_t* raw_environment = nullptr;
  check(ir_only ? loomc_target_environment_create_configured_ir(
                      allocator, &raw_environment)
                : loomc_target_environment_create_configured(allocator,
                                                             &raw_environment),
        "creating the target environment");
  environment.reset(raw_environment);
  loomc_context_target_options_t target_options = {};
  target_options.type = LOOMC_STRUCTURE_TYPE_CONTEXT_TARGET_OPTIONS;
  target_options.structure_size = sizeof(target_options);
  target_options.target_environment = environment.get();
  loomc_context_options_t context_options = {};
  context_options.type = LOOMC_STRUCTURE_TYPE_CONTEXT_OPTIONS;
  context_options.structure_size = sizeof(context_options);
  context_options.next = &target_options;
  loomc_context_t* raw_context = nullptr;
  check(loomc_context_create(&context_options, allocator, &raw_context),
        "creating the compiler context");
  context.reset(raw_context);
  loomc_workspace_options_t workspace_options = {};
  workspace_options.type = LOOMC_STRUCTURE_TYPE_WORKSPACE_OPTIONS;
  workspace_options.structure_size = sizeof(workspace_options);
  // As loom-import-cxx: 64 KiB blocks avoid per-block mappings on allocators
  // with a 128 KiB mapping threshold. The compiler uses default blocks.
  workspace_options.block_size = ir_only ? 64 * 1024 : 0;
  loomc_workspace_t* raw_workspace = nullptr;
  check(loomc_workspace_create(ir_only ? &workspace_options : nullptr,
                               allocator, &raw_workspace),
        "creating the compiler workspace");
  workspace.reset(raw_workspace);
}

// Source configuration of the one parse, also passed to every import.
loomc_cxx_import_options_t Driver::import_options() const {
  loomc_cxx_import_options_t options = {};
  options.type = LOOMC_STRUCTURE_TYPE_CXX_IMPORT_OPTIONS;
  options.structure_size = sizeof(options);
  options.standard = loomc_make_cstring_view(options_.standard.c_str());
  options.triple = loomc_make_cstring_view(options_.triple.c_str());
  options.data_model = LOOMC_CXX_DATA_MODEL_LP64;
  options.fp_model = LOOMC_CXX_FP_MODEL_PRECISE;
  // A host compile: the hosted profile for the triple and exactly the
  // include directories on the command line.
  options.flags =
      LOOMC_CXX_IMPORT_FLAG_HOSTED | LOOMC_CXX_IMPORT_FLAG_NO_BUILTIN_INCLUDES;
  return options;
}

bool Driver::parse() {
  auto start = Clock::now();
  auto text = read_file(options_.source);
  if (!text) {
    std::fprintf(stderr, "%s: error: cannot read '%s'\n", kTool.data(),
                 options_.source.c_str());
    return false;
  }
  source_text_ = std::move(*text);
  make_context(/*ir_only=*/true, import_environment_, import_context_,
               import_workspace_);

  std::vector<loomc_string_view_t> includes, system_includes;
  for (const auto& path : options_.include_paths) {
    includes.push_back(loomc_make_cstring_view(path.c_str()));
  }
  for (const auto& path : options_.system_include_paths) {
    system_includes.push_back(loomc_make_cstring_view(path.c_str()));
  }
  std::vector<std::string> define_names, define_values;
  for (const auto& define : options_.defines) {
    auto equal = define.find('=');
    define_names.push_back(define.substr(0, equal));
    define_values.push_back(equal == std::string::npos
                                ? std::string("1")
                                : define.substr(equal + 1));
  }
  std::vector<loomc_cxx_define_t> defines(options_.defines.size());
  for (size_t i = 0; i < defines.size(); ++i) {
    defines[i].name = loomc_make_cstring_view(define_names[i].c_str());
    defines[i].value = loomc_make_cstring_view(define_values[i].c_str());
  }
  loomc_cxx_import_options_t options = import_options();
  options.include_paths = includes.data();
  options.include_path_count = includes.size();
  options.system_include_paths = system_includes.data();
  options.system_include_path_count = system_includes.size();
  options.defines = defines.data();
  options.define_count = defines.size();

  loomc_source_options_t source_options = {};
  source_options.identifier = loomc_make_cstring_view(options_.source.c_str());
  source_options.contents =
      loomc_make_byte_span(source_text_.data(), source_text_.size());
  source_options.storage = LOOMC_SOURCE_STORAGE_BORROWED;
  loomc_source_t* raw_source = nullptr;
  check(loomc_source_create(&source_options, loomc_allocator_system(),
                            &raw_source),
        "creating the source");
  Source source(raw_source);

  loomc_cxx_unit_t* raw_unit = nullptr;
  loomc_result_t* raw_result = nullptr;
  check(loomc_cxx_unit_parse(import_context_.get(), source.get(), &options,
                             loomc_allocator_system(), &raw_unit, &raw_result),
        "parsing");
  unit_.reset(raw_unit);
  Result result(raw_result);
  std::string text_out;
  const bool succeeded = print_result(result.get(), text_out);
  std::fputs(text_out.c_str(), stderr);
  parse_seconds_ = seconds_since(start);
  return succeeded && unit_;
}

void Driver::write_dependency_file() {
  if (!options_.dependency_file) {
    return;
  }
  auto& unit = loom::cxx_import::unit_translation_unit(
      reinterpret_cast<loom_cxx_unit_t*>(unit_.get()));
  std::vector<std::string> dependencies = {options_.source};
  std::unordered_set<std::string> seen(dependencies.begin(),
                                       dependencies.end());
  for (const auto& [file, system] : unit.preprocessor()->includedFiles()) {
    if (options_.dependency_skip_system && system) {
      continue;
    }
    if (seen.insert(file).second) {
      dependencies.push_back(file);
    }
  }
  std::string path = options_.dependency_path;
  if (path.empty()) {
    path =
        std::filesystem::path(options_.output).replace_extension(".d").string();
  }
  std::string target = options_.dependency_target.empty()
                           ? options_.output
                           : options_.dependency_target;
  std::ofstream out(path);
  out << target << ':';
  for (const auto& dependency : dependencies) {
    out << " \\\n  " << dependency;
  }
  out << '\n';
  if (options_.dependency_phony) {
    for (size_t i = 1; i < dependencies.size(); ++i) {
      out << '\n' << dependencies[i] << ":\n";
    }
  }
  if (!out) {
    std::fprintf(stderr, "%s: error: cannot write '%s'\n", kTool.data(),
                 path.c_str());
    throw Failure{};
  }
}

bool Driver::generate_host_code() {
  auto start = Clock::now();
  auto* native_unit = reinterpret_cast<loom_cxx_unit_t*>(unit_.get());
  // Host code generation reports through the unit's frontend diagnostics.
  loom_cxx_unit_set_diagnostic_sink(
      native_unit,
      loom_diagnostic_sink_t{loom_diagnostic_stderr_sink, nullptr});
  auto& unit = loom::cxx_import::unit_translation_unit(native_unit);
  const auto& toolchain = loom::cxx_import::unit_toolchain(native_unit);
  toolchain.applyEntryPointAbi(&unit);

  std::string error;
  target_machine_ = cxx::createTargetMachine(
      toolchain.memoryLayout()->triple(), options_.optimization_level, error);
  if (!target_machine_) {
    std::fprintf(stderr, "%s: error: %s\n", kTool.data(), error.c_str());
    return false;
  }
  llvm_context_ = std::make_unique<llvm::LLVMContext>();
  cxx::LLVMCodegenOptions codegen_options;
  codegen_options.optimizationLevel = options_.optimization_level;
  host_module_ = cxx::generateLLVMModule(
      &unit, *llvm_context_, target_machine_.get(), codegen_options, error);
  loom_cxx_unit_set_diagnostic_sink(native_unit, loom_diagnostic_sink_t{});
  if (!host_module_ || loom::cxx_import::unit_has_error(native_unit)) {
    if (!error.empty()) {
      std::fprintf(stderr, "%s: error: %s\n", kTool.data(), error.c_str());
    }
    return false;
  }
  kernel_refs_ = referenced_kernel_refs();
  host_seconds_ = seconds_since(start);
  return true;
}

// Descriptors the host code references, as an object file's undefined
// kernel_ref symbols would list them.
std::vector<std::string> Driver::referenced_kernel_refs() const {
  std::set<std::string> names;
  for (const auto& global : host_module_->globals()) {
    if (global.isDeclaration() &&
        starts_with(global.getName().str(), kKernelRefPrefix)) {
      names.insert(global.getName().str());
    }
  }
  return {names.begin(), names.end()};
}

void Driver::device_failure(std::string_view stage, std::string_view log) {
  std::fprintf(stderr, "%s: %.*s failed for %s\n", kTool.data(),
               (int)stage.size(), stage.data(), options_.source.c_str());
  int printed = 0;
  auto log_lines = lines(log);
  for (size_t i = 0; i < log_lines.size() && printed < 40; ++i) {
    if (log_lines[i].find("error [") == std::string_view::npos) {
      continue;
    }
    for (size_t j = i; j < std::min(i + 4, log_lines.size()); ++j, ++printed) {
      std::fprintf(stderr, "  %.*s\n", (int)log_lines[j].size(),
                   log_lines[j].data());
    }
  }
  if (options_.device_failure == DeviceFailurePolicy::kError) {
    throw Failure{};
  }
  std::fprintf(stderr, "%s: warning: continuing without device code for %s\n",
               kTool.data(), options_.source.c_str());
  descriptors_.clear();
  code_object_.clear();
}

bool Driver::import_device_module(std::vector<std::string>& excluded, int round,
                                  std::string& bytecode, bool& has_kernels) {
  auto start = Clock::now();
  const std::string descriptors_path = temp_path("descriptors.s");
  std::filesystem::remove(descriptors_path);
  const loomc_string_view_t roots[] = {loomc_make_cstring_view("@launched")};
  std::vector<loomc_string_view_t> excluded_views;
  for (const auto& name : excluded) {
    excluded_views.push_back(loomc_make_cstring_view(name.c_str()));
  }
  loomc_cxx_import_options_t options = import_options();
  if (options_.drop_failed_kernels) {
    options.flags |= LOOMC_CXX_IMPORT_FLAG_DROP_FAILED_KERNELS;
  }
  options.roots = roots;
  options.root_count = 1;
  options.excluded_kernels = excluded_views.data();
  options.excluded_kernel_count = excluded_views.size();
  options.kernel_descriptors_path =
      loomc_make_cstring_view(descriptors_path.c_str());

  auto* native_unit = reinterpret_cast<loom_cxx_unit_t*>(unit_.get());
  loomc_module_t* raw_module = nullptr;
  loomc_result_t* raw_result = nullptr;
  check(loomc_module_import_cxx_unit(
            import_context_.get(), import_workspace_.get(), unit_.get(),
            &options, loomc_allocator_system(), &raw_module, &raw_result),
        "importing device code");
  ++import_count_;
  Module module(raw_module);
  Result result(raw_result);
  std::string log;
  bool succeeded = print_result(result.get(), log);
  if (options_.verbose) {
    std::fputs(log.c_str(), stderr);
  }

  const auto statistics = loom::cxx_import::unit_statistics(native_unit);
  if (succeeded && statistics.dropped_kernel_count > 0 && round == 1) {
    std::fprintf(stderr, "%s: dropped %llu launched kernel(s) in %s\n",
                 kTool.data(),
                 (unsigned long long)statistics.dropped_kernel_count,
                 options_.source.c_str());
    int printed = 0;
    for (auto line : lines(log)) {
      if (line.find("warning [LOWERING") == std::string_view::npos) {
        continue;
      }
      if (++printed > 40) {
        break;
      }
      std::fprintf(stderr, "  dropped: %.*s\n", (int)line.size(), line.data());
    }
  }
  if (!succeeded || !module) {
    import_seconds_ += seconds_since(start);
    device_failure("device import", log);
    return false;
  }
  descriptors_ = read_file(descriptors_path).value_or(std::string());
  has_kernels = descriptors_.find("\n  .weak") != std::string::npos;
  if (!has_kernels) {
    import_seconds_ += seconds_since(start);
    return true;
  }

  // Cleanup and verification, as loom-import-cxx runs before writing.
  loomc_pass_program_t* raw_program = nullptr;
  raw_result = nullptr;
  check(loomc_pass_program_create_from_pipeline_text(
            import_context_.get(),
            loomc_make_cstring_view("combine,cse,dce,location-dce"), nullptr,
            loomc_allocator_system(), &raw_program, &raw_result),
        "creating the import cleanup pipeline");
  PassProgram program(raw_program);
  result.reset(raw_result);
  if (result && !print_result(result.get(), log)) {
    device_failure("device import cleanup", log);
    return false;
  }
  raw_result = nullptr;
  check(loomc_pass_program_run(program.get(), import_workspace_.get(),
                               module.get(), nullptr, loomc_allocator_system(),
                               &raw_result),
        "running the import cleanup pipeline");
  result.reset(raw_result);
  if (!print_result(result.get(), log)) {
    device_failure("device import cleanup", log);
    return false;
  }
  raw_result = nullptr;
  check(loomc_module_verify(module.get(), nullptr, loomc_allocator_system(),
                            &raw_result),
        "verifying the device module");
  result.reset(raw_result);
  if (!print_result(result.get(), log)) {
    device_failure("device import verification", log);
    return false;
  }
  loomc_module_serialize_options_t serialize_options = {};
  serialize_options.type = LOOMC_STRUCTURE_TYPE_MODULE_SERIALIZE_OPTIONS;
  serialize_options.structure_size = sizeof(serialize_options);
  serialize_options.format = LOOMC_SOURCE_FORMAT_BYTECODE;
  loomc_source_t* raw_serialized = nullptr;
  check(loomc_module_serialize_to_source(module.get(), &serialize_options,
                                         loomc_allocator_system(),
                                         &raw_serialized),
        "serializing the device module");
  Source serialized(raw_serialized);
  const auto contents = loomc_source_contents(serialized.get());
  bytecode.assign(reinterpret_cast<const char*>(contents.data),
                  contents.data_length);
  if (keep_work_directory_) {
    std::ofstream(temp_path("device.loombc"), std::ios::binary) << bytecode;
  }
  import_seconds_ += seconds_since(start);
  return true;
}

bool Driver::compile_device_module(const std::string& bytecode,
                                   std::vector<std::string>& rejected) {
  auto start = Clock::now();
  // The device compiler gets the serialized module, exactly as a separate
  // loom-compile process would read it.
  TargetEnvironment environment;
  Context context;
  Workspace workspace;
  make_context(/*ir_only=*/false, environment, context, workspace);
  loomc_compiler_t* raw_compiler = nullptr;
  check(loomc_compiler_create(context.get(), nullptr, loomc_allocator_system(),
                              &raw_compiler),
        "creating the device compiler");
  Compiler compiler(raw_compiler);
  const std::string identifier = temp_path("device.loombc");
  loomc_source_options_t source_options = {};
  source_options.identifier = loomc_make_cstring_view(identifier.c_str());
  source_options.contents =
      loomc_make_byte_span(bytecode.data(), bytecode.size());
  source_options.storage = LOOMC_SOURCE_STORAGE_BORROWED;
  source_options.format = LOOMC_SOURCE_FORMAT_BYTECODE;
  loomc_source_t* raw_source = nullptr;
  check(loomc_source_create(&source_options, loomc_allocator_system(),
                            &raw_source),
        "creating the device source");
  Source source(raw_source);
  loomc_module_t* raw_module = nullptr;
  loomc_result_t* raw_result = nullptr;
  check(loomc_module_deserialize_from_source(
            context.get(), workspace.get(), source.get(), nullptr,
            loomc_allocator_system(), &raw_module, &raw_result),
        "reading the device module");
  Module module(raw_module);
  Result result(raw_result);
  std::string log;
  if (!print_result(result.get(), log) || !module) {
    device_failure("device compile", log);
    return false;
  }
  loomc_target_profile_t* raw_profile = nullptr;
  check(loomc_target_profile_select(
            environment.get(),
            loomc_make_cstring_view(options_.loom_target.c_str()),
            loomc_allocator_system(), &raw_profile),
        "selecting the device target");
  TargetProfile profile(raw_profile);

  loomc_config_options_t config = {};
  config.flags = LOOMC_CONFIG_POLICY_FLAG_REQUIRE_RESOLVED;
  loomc_emit_options_t emit_options = {};
  emit_options.type = LOOMC_STRUCTURE_TYPE_EMIT_OPTIONS;
  emit_options.structure_size = sizeof(emit_options);
  emit_options.artifact_format =
      loomc_make_cstring_view(options_.loom_format.c_str());
  emit_options.artifact_flags = LOOMC_EMIT_ARTIFACT_FLAG_PRIMARY;
  loomc_compile_artifact_options_t compile_options = {};
  compile_options.type = LOOMC_STRUCTURE_TYPE_COMPILE_ARTIFACT_OPTIONS;
  compile_options.structure_size = sizeof(compile_options);
  compile_options.target_profile = profile.get();
  compile_options.config = &config;
  compile_options.emit_options = &emit_options;
  raw_result = nullptr;
  check(loomc_compile_artifact(compiler.get(), workspace.get(),
                               /*pass_program=*/nullptr, module.get(),
                               &compile_options, loomc_allocator_system(),
                               &raw_result),
        "compiling device code");
  ++device_compile_count_;
  result.reset(raw_result);
  const bool succeeded = print_result(result.get(), log);
  if (options_.verbose) {
    std::fputs(log.c_str(), stderr);
  }
  device_compile_seconds_ += seconds_since(start);
  if (!succeeded) {
    // Kernels the device compiler rejected, by export name.
    std::set<std::string> names;
    for (loomc_host_size_t i = 0;
         i < loomc_result_diagnostic_count(result.get()); ++i) {
      const auto* diagnostic = loomc_result_diagnostic_at(result.get(), i);
      if (diagnostic->severity != LOOMC_DIAGNOSTIC_SEVERITY_ERROR) {
        continue;
      }
      std::string name(
          diagnostic->export_name.data ? diagnostic->export_name.data : "",
          diagnostic->export_name.size);
      for (loomc_host_size_t j = 0;
           name.empty() && j < diagnostic->parameter_count; ++j) {
        const auto& parameter = diagnostic->parameters[j];
        if (std::string_view(parameter.name.data, parameter.name.size) ==
            "export_name") {
          name.assign(parameter.value.data, parameter.value.size);
        }
      }
      if (!name.empty()) {
        names.insert(name);
      }
    }
    rejected.assign(names.begin(), names.end());
    if (rejected.empty() || !options_.drop_failed_kernels) {
      rejected.clear();
      device_failure("device compile", log);
      return false;
    }
    std::set<std::string> reasons;
    for (auto line : lines(log)) {
      if (line.find("error [") == std::string_view::npos) {
        continue;
      }
      std::string reason(line);
      constexpr std::string_view kContext =
          "config '__loom_target_context_0_0' ";
      if (auto context = reason.find(kContext); context != std::string::npos) {
        reason = reason.substr(context + kContext.size());
      }
      reasons.insert(reason.substr(0, 200));
    }
    std::fprintf(stderr,
                 "%s: compile rejected %zu kernel(s) in %s; excluding and "
                 "retrying\n",
                 kTool.data(), rejected.size(), options_.source.c_str());
    int printed = 0;
    for (const auto& reason : reasons) {
      if (++printed > 10) {
        break;
      }
      std::fprintf(stderr, "  compile-dropped: %s\n", reason.c_str());
    }
    return false;
  }
  for (loomc_host_size_t i = 0; i < loomc_result_artifact_count(result.get());
       ++i) {
    const auto* artifact = loomc_result_artifact_at(result.get(), i);
    if (artifact->kind != LOOMC_ARTIFACT_KIND_EXECUTABLE) {
      continue;
    }
    loomc_source_t* raw_artifact = nullptr;
    check(loomc_artifact_create_source(artifact, LOOMC_SOURCE_FORMAT_UNKNOWN,
                                       loomc_allocator_system(), &raw_artifact),
          "reading the device code object");
    Source artifact_source(raw_artifact);
    const auto contents = loomc_source_contents(artifact_source.get());
    code_object_.assign(reinterpret_cast<const char*>(contents.data),
                        contents.data_length);
  }
  if (code_object_.empty()) {
    device_failure("device compile", "error [TOOL]: no code object\n");
    return false;
  }
  if (keep_work_directory_) {
    std::ofstream(temp_path("device.code"), std::ios::binary) << code_object_;
  }
  return true;
}

bool Driver::device_pass() {
  if (options_.loom_target.empty()) {
    std::fprintf(stderr,
                 "%s: error: %s launches kernels; --loom-target is required\n",
                 kTool.data(), options_.source.c_str());
    throw Failure{};
  }
  if (options_.register_function.empty()) {
    std::fprintf(stderr,
                 "%s: error: %s launches kernels; --loom-register-function is "
                 "required\n",
                 kTool.data(), options_.source.c_str());
    throw Failure{};
  }
  std::vector<std::string> excluded;
  for (int round = 1; round <= kMaxDeviceRounds; ++round) {
    std::string bytecode;
    bool has_kernels = false;
    if (!import_device_module(excluded, round, bytecode, has_kernels)) {
      return false;
    }
    if (!has_kernels) {
      return true;
    }
    std::vector<std::string> rejected;
    if (compile_device_module(bytecode, rejected)) {
      return true;
    }
    if (rejected.empty()) {
      return false;  // already reported
    }
    if (round == kMaxDeviceRounds) {
      device_failure("device compile", "error [TOOL]: retry limit reached\n");
      return false;
    }
    excluded.insert(excluded.end(), rejected.begin(), rejected.end());
  }
  return false;
}

std::string assembly_quote(std::string_view text) {
  std::string quoted = "\"";
  for (char c : text) {
    if (c == '"' || c == '\\') {
      quoted += '\\';
    }
    quoted += c;
  }
  return quoted + "\"";
}

void Driver::embed_device_code() {
  std::string assembly = descriptors_;
  // Unavailable descriptors for references the device pass did not define, so
  // the object always links; launching one fails at run time.
  int missing = 0;
  for (const auto& symbol : kernel_refs_) {
    if (descriptors_.find(".weak " + assembly_quote(symbol) + "\n") !=
        std::string::npos) {
      continue;
    }
    const auto quoted = assembly_quote(symbol);
    const auto label = std::format(".Lloom_cxx_missing_{}", missing++);
    assembly += std::format(
        "  .section {3},\"aG\",%progbits,{0},comdat\n"
        "  .weak {0}\n  .type {0},%object\n  .size {0},48\n  .p2align 3\n"
        "{0}:\n  .long 1, 0\n  .quad {1}\n"
        "  .long 0, 0, 0, 0, 0, 0\n  .quad 0\n{1}:\n  .asciz {2}\n",
        quoted, label, quoted, assembly_quote(".rodata." + symbol));
  }
  if (!assembly.empty()) {
    host_module_->appendModuleInlineAsm(assembly);
  }
  if (code_object_.empty()) {
    return;
  }

  // The code object, and a static constructor registering it.
  auto& context = host_module_->getContext();
  auto* data = llvm::ConstantDataArray::get(
      context, llvm::ArrayRef<uint8_t>(
                   reinterpret_cast<const uint8_t*>(code_object_.data()),
                   code_object_.size()));
  auto* blob = new llvm::GlobalVariable(
      *host_module_, data->getType(), /*isConstant=*/true,
      llvm::GlobalValue::PrivateLinkage, data, "__loom_cxx_code_object");
  blob->setAlignment(llvm::Align(16));
  auto* void_type = llvm::Type::getVoidTy(context);
  auto* pointer_type = llvm::PointerType::getUnqual(context);
  auto* size_type = host_module_->getDataLayout().getIntPtrType(context);
  auto callee = host_module_->getOrInsertFunction(
      options_.register_function,
      llvm::FunctionType::get(
          void_type, {static_cast<llvm::Type*>(pointer_type), size_type},
          false));
  auto* constructor =
      llvm::Function::Create(llvm::FunctionType::get(void_type, false),
                             llvm::GlobalValue::InternalLinkage,
                             "__loom_cxx_register_code_object", *host_module_);
  llvm::IRBuilder<> builder(
      llvm::BasicBlock::Create(context, "entry", constructor));
  llvm::Value* arguments[] = {
      blob, llvm::ConstantInt::get(size_type, code_object_.size())};
  builder.CreateCall(callee, arguments);
  builder.CreateRetVoid();
  llvm::appendToGlobalCtors(*host_module_, constructor, 65535);
}

void Driver::emit_object() {
  auto start = Clock::now();
  // Written next to the output and renamed, so a failed compile leaves no
  // partial object behind.
  const std::string partial =
      std::format("{}.loom-cxx-{}.tmp", options_.output, (long)getpid());
  {
    std::error_code error_code;
    llvm::raw_fd_ostream out(partial, error_code);
    if (error_code) {
      std::fprintf(stderr, "%s: error: cannot open '%s': %s\n", kTool.data(),
                   partial.c_str(), error_code.message().c_str());
      throw Failure{};
    }
    std::string error;
    if (!cxx::emitMachineCode(*host_module_, target_machine_.get(), out,
                              /*assembly=*/false, error)) {
      std::fprintf(stderr, "%s: error: %s\n", kTool.data(), error.c_str());
      std::filesystem::remove(partial);
      throw Failure{};
    }
  }
  std::error_code error_code;
  std::filesystem::rename(partial, options_.output, error_code);
  if (error_code) {
    std::fprintf(stderr, "%s: error: cannot write '%s': %s\n", kTool.data(),
                 options_.output.c_str(), error_code.message().c_str());
    std::filesystem::remove(partial);
    throw Failure{};
  }
  emit_seconds_ = seconds_since(start);
}

int Driver::run() {
  const auto before = loom::cxx_import::frontend_statistics();
  if (!parse()) {
    return 1;
  }
  write_dependency_file();
  if (!generate_host_code()) {
    return 1;
  }

  if (!kernel_refs_.empty()) {
    if (!options_.keep_directory.empty()) {
      work_directory_ = options_.keep_directory;
      keep_work_directory_ = true;
      std::filesystem::create_directories(work_directory_);
    } else {
      auto base = std::filesystem::temp_directory_path() /
                  std::format("loom-cxx-{}-XXXXXX", (long)getpid());
      std::string pattern = base.string();
      if (!mkdtemp(pattern.data())) {
        std::fprintf(stderr, "%s: error: cannot create a temporary directory\n",
                     kTool.data());
        return 1;
      }
      work_directory_ = pattern;
    }
    bool ok = false;
    try {
      ok = device_pass();
    } catch (...) {
      if (!keep_work_directory_) {
        std::filesystem::remove_all(work_directory_);
      }
      throw;
    }
    if (!keep_work_directory_) {
      std::filesystem::remove_all(work_directory_);
    }
    if (!ok && options_.device_failure == DeviceFailurePolicy::kError) {
      return 1;
    }
  }
  embed_device_code();
  emit_object();

  if (options_.stats) {
    const auto after = loom::cxx_import::frontend_statistics();
    std::fprintf(
        stderr,
        "%s: stats: %s: preprocess=%llu parse=%llu translation_unit=%p "
        "host_codegen=1 imports=%d device_compiles=%d kernel_refs=%zu "
        "code_object_bytes=%zu times_s: parse=%.3f host=%.3f import=%.3f "
        "device_compile=%.3f emit=%.3f\n",
        kTool.data(), options_.source.c_str(),
        (unsigned long long)(after.preprocess_count - before.preprocess_count),
        (unsigned long long)(after.parse_count - before.parse_count),
        (void*)&loom::cxx_import::unit_translation_unit(
            reinterpret_cast<loom_cxx_unit_t*>(unit_.get())),
        import_count_, device_compile_count_, kernel_refs_.size(),
        code_object_.size(), parse_seconds_, host_seconds_, import_seconds_,
        device_compile_seconds_, emit_seconds_);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options = parse_options(argc, argv);
  if (options.stats || getenv("LOOM_CXX_STATS")) {
    options.stats = true;
  }
  try {
    Driver driver(std::move(options));
    return driver.run();
  } catch (const Failure&) {
    return 1;
  }
}
