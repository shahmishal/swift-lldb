//===-- SwiftLanguageRuntime.h ----------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SwiftLanguageRuntime_h_
#define liblldb_SwiftLanguageRuntime_h_

// C Includes
// C++ Includes
#include <mutex>
#include <tuple>
#include <vector>
// Other libraries and framework includes
// Project includes
#include "Plugins/LanguageRuntime/ObjC/AppleObjCRuntime/AppleObjCRuntimeV2.h"
#include "lldb/Breakpoint/BreakpointPrecondition.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Casting.h"

namespace swift {
namespace remote {
class MemoryReader;
class RemoteAddress;
}

template <typename T> struct External;
template <unsigned PointerSize> struct RuntimeTarget;

namespace reflection {
template <typename T> class ReflectionContext;
class TypeInfo;
}

namespace remoteAST {
class RemoteASTContext;
}
enum class MetadataKind : uint32_t;
class TypeBase;
}

namespace lldb_private {

/// Statically cast an opaque type to a Swift type.
swift::Type GetSwiftType(void *opaque_ptr);
/// Statically cast an opaque type to a Swift type and get its canonical form.
swift::CanType GetCanonicalSwiftType(void *opaque_ptr);
/// Statically cast a CompilerType to a Swift type.
swift::Type GetSwiftType(const CompilerType &type);
/// Statically cast a CompilerType to a Swift type and get its canonical form.
swift::CanType GetCanonicalSwiftType(const CompilerType &type);

class SwiftLanguageRuntime : public LanguageRuntime {
public:
  class MetadataPromise;
  typedef std::shared_ptr<MetadataPromise> MetadataPromiseSP;

  static char ID;

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || LanguageRuntime::isA(ClassID);
  }

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::LanguageRuntime *
  CreateInstance(Process *process, lldb::LanguageType language);

  static lldb_private::ConstString GetPluginNameStatic();

  static bool classof(const LanguageRuntime *runtime) {
    return runtime->isA(&ID);
  }

  static SwiftLanguageRuntime *Get(Process &process) {
    return llvm::cast_or_null<SwiftLanguageRuntime>(
        process.GetLanguageRuntime(lldb::eLanguageTypeSwift));
  }

  static lldb::BreakpointPreconditionSP
  GetBreakpointExceptionPrecondition(lldb::LanguageType language, bool throw_bp);

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  class MethodName {
  public:
    enum Type {
      eTypeInvalid,
      eTypeUnknownMethod,
      eTypeClassMethod,
      eTypeInstanceMethod,
      eTypeOperator,
      eTypeConstructor,
      eTypeDestructor,
      eTypeAllocator,
      eTypeDeallocator
    };

    MethodName()
        : m_full(), m_basename(), m_context(), m_arguments(), m_qualifiers(),
          m_type(eTypeInvalid), m_parsed(false), m_parse_error(false) {}

    MethodName(ConstString s, bool do_parse = false)
        : m_full(s), m_basename(), m_context(), m_arguments(), m_qualifiers(),
          m_type(eTypeInvalid), m_parsed(false), m_parse_error(false) {
      if (do_parse)
        Parse();
    }

    void Clear();

    bool IsValid() const {
      if (m_parse_error)
        return false;
      if (m_type == eTypeInvalid)
        return false;
      return (bool)m_full;
    }

    Type GetType() const { return m_type; }

    ConstString GetFullName() const { return m_full; }

    llvm::StringRef GetBasename();

    static bool ExtractFunctionBasenameFromMangled(ConstString mangled,
                                                   ConstString &basename,
                                                   bool &is_method);

  protected:
    void Parse();

    ConstString m_full;         // Full name:    "foo.bar.baz : <A : AProtocol>
                                // (foo.bar.metatype)(x : Swift.Int64) -> A"
    llvm::StringRef m_basename; // Basename:     "baz"
    llvm::StringRef m_context;  // Decl context: "foo.bar"
    llvm::StringRef m_metatype_ref;  // Meta type:    "(foo.bar.metatype)"
    llvm::StringRef m_template_args; // Generic args: "<A: AProtocol>
    llvm::StringRef m_arguments;     // Arguments:    "(x : Swift.Int64)"
    llvm::StringRef m_qualifiers;    // Qualifiers:   "const"
    llvm::StringRef m_return_type;   // Return type:  "A"
    Type m_type;
    bool m_parsed;
    bool m_parse_error;
  };

  /// A proxy object to support lazy binding of Archetypes.
  class MetadataPromise {
    friend class SwiftLanguageRuntime;

    MetadataPromise(ValueObject &, SwiftLanguageRuntime &, lldb::addr_t);

    lldb::ValueObjectSP m_for_object_sp;
    SwiftLanguageRuntime &m_swift_runtime;
    lldb::addr_t m_metadata_location;
    llvm::Optional<swift::MetadataKind> m_metadata_kind;
    llvm::Optional<CompilerType> m_compiler_type;

  public:
    CompilerType FulfillTypePromise(Status *error = nullptr);
  };

  class SwiftExceptionPrecondition : public BreakpointPrecondition {
  public:
    SwiftExceptionPrecondition();

    virtual ~SwiftExceptionPrecondition() {}

    bool EvaluatePrecondition(StoppointCallbackContext &context) override;
    void GetDescription(Stream &stream, lldb::DescriptionLevel level) override;
    Status ConfigurePrecondition(Args &args) override;

  protected:
    void AddTypeName(const char *type_name);
    void AddEnumSpec(const char *enum_name, const char *element_name);

  private:
    std::unordered_set<std::string> m_type_names;
    std::unordered_map<std::string, std::vector<std::string>> m_enum_spec;
  };

  virtual ~SwiftLanguageRuntime();

  virtual lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeSwift;
  }

  void ModulesDidLoad(const ModuleList &module_list) override;

  virtual bool GetObjectDescription(Stream &str, ValueObject &object) override;

  virtual bool GetObjectDescription(Stream &str, Value &value,
                                    ExecutionContextScope *exe_scope) override;

  /// A pair of depth and index.
  using ArchetypePath = std::pair<uint64_t, uint64_t>;
  /// Populate a map with the names of all archetypes in a function's generic
  /// context.
  static void GetGenericParameterNamesForFunction(
      const SymbolContext &sc,
      llvm::DenseMap<ArchetypePath, llvm::StringRef> &dict);

  static std::string
  DemangleSymbolAsString(llvm::StringRef symbol, bool simplified = false,
                         const SymbolContext *sc = nullptr);

  // Use these passthrough functions rather than calling into Swift directly,
  // since some day we may want to support more than one swift variant.
  static bool IsSwiftMangledName(const char *name);
  
  static bool IsSwiftClassName(const char *name);

  bool IsSymbolARuntimeThunk(const Symbol &symbol) override;

  static const std::string GetCurrentMangledName(const char *mangled_name);

  // provide a quick and yet somewhat reasonable guess as to whether
  // this ValueObject represents something that validly conforms
  // to the magic ErrorType protocol
  virtual bool IsValidErrorValue(ValueObject &in_value);

  virtual lldb::BreakpointResolverSP
  CreateExceptionResolver(Breakpoint *bkpt, bool catch_bp,
                          bool throw_bp) override;

  SwiftExceptionPrecondition *GetExceptionPrecondition();

  static lldb::ValueObjectSP CalculateErrorValue(lldb::StackFrameSP frame_sp,
                                                 ConstString name);

  lldb::ValueObjectSP CalculateErrorValueObjectFromValue(Value &value,
                                                         ConstString name,
                                                         bool persistent);

  llvm::Optional<Value> GetErrorReturnLocationAfterReturn(lldb::StackFrameSP frame_sp);
  
  llvm::Optional<Value> GetErrorReturnLocationBeforeReturn(lldb::StackFrameSP frame_sp,
                                              bool &need_to_check_after_return);

  static void RegisterGlobalError(Target &target, ConstString name,
                                  lldb::addr_t addr);

  // If you are at the initial instruction of the frame passed in, then this
  // will examine the call
  // arguments, and if any of them is a function pointer, this will push the
  // address of the function
  // into addresses.  If debug_only is true, then it will only push function
  // pointers that are in user
  // code.

  void FindFunctionPointersInCall(StackFrame &frame,
                                  std::vector<Address> &addresses,
                                  bool debug_only = true,
                                  bool resolve_thunks = true) override;

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                  bool stop_others) override;

  // this call should return true if it could set the name and/or the type
  virtual bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                        lldb::DynamicValueType use_dynamic,
                                        TypeAndOrName &class_type_or_name,
                                        Address &address,
                                        Value::ValueType &value_type) override;

  virtual TypeAndOrName FixUpDynamicType(const TypeAndOrName &type_and_or_name,
                                         ValueObject &static_value) override;

  /// \return true if this is a Swift tagged pointer (as opposed to an
  /// Objective-C tagged pointer).
  bool IsTaggedPointer(lldb::addr_t addr, CompilerType type);
  virtual std::pair<lldb::addr_t, bool>
  FixupPointerValue(lldb::addr_t addr, CompilerType type) override;
  virtual lldb::addr_t FixupAddress(lldb::addr_t addr, CompilerType type,
                                    Status &error) override;

  /// Ask Remote Mirrors for the type info about a Swift type.
  const swift::reflection::TypeInfo *GetTypeInfo(CompilerType type);
  virtual bool IsStoredInlineInBuffer(CompilerType type) override;

  /// Ask Remote Mirrors for the size of a Swift type.
  llvm::Optional<uint64_t> GetBitSize(CompilerType type);

  /// Ask Remote mirrors for the stride of a Swift type.
  llvm::Optional<uint64_t> GetByteStride(CompilerType type);

  /// Ask Remote mirrors for the alignment of a Swift type.
  llvm::Optional<size_t> GetBitAlignment(CompilerType type);

  bool IsWhitelistedRuntimeValue(ConstString name) override;

  virtual CompilerType DoArchetypeBindingForType(StackFrame &stack_frame,
                                                 CompilerType base_type);
  
  virtual CompilerType GetConcreteType(ExecutionContextScope *exe_scope,
                                       ConstString abstract_type_name) override;

  virtual bool CouldHaveDynamicValue(ValueObject &in_value) override;

  virtual MetadataPromiseSP GetMetadataPromise(lldb::addr_t addr,
                                               ValueObject &for_object);

  /// Build the artificial type metadata variable name for \p swift_type.
  static bool GetAbstractTypeName(StreamString &name, swift::Type swift_type);
  
  /// Retrieve the remote AST context for the given Swift AST context.
  swift::remoteAST::RemoteASTContext &
  GetRemoteASTContext(SwiftASTContext &swift_ast_ctx);

  /// Release the RemoteASTContext associated with the given swift::ASTContext.
  /// Note that a RemoteASTContext must be destroyed before its associated
  /// swift::ASTContext is destroyed.
  void ReleaseAssociatedRemoteASTContext(swift::ASTContext *ctx);

  /// Retrieve the offset of the named member variable within an instance
  /// of the given type.
  ///
  /// \param instance_type
  llvm::Optional<uint64_t>
  GetMemberVariableOffset(CompilerType instance_type,
                          ValueObject *instance,
                          ConstString member_name,
                          Status *error = nullptr);

  /// Determines wether \c variable is the "self" object.
  static bool IsSelf(Variable &variable);

  void AddToLibraryNegativeCache(llvm::StringRef library_name);

  bool IsInLibraryNegativeCache(llvm::StringRef library_name);

  // Swift uses a few known-unused bits in ObjC pointers
  // to record useful-for-bridging information
  // This API's task is to return such pointer+info aggregates
  // back to a pure pointer
  lldb::addr_t MaskMaybeBridgedPointer(lldb::addr_t, lldb::addr_t * = nullptr);

  // Swift uses a few known-unused bits in weak,unowned,unmanaged references
  // to record useful runtime information
  // This API's task is to strip those bits if necessary and return
  // a pure pointer (or a tagged pointer)
  lldb::addr_t MaybeMaskNonTrivialReferencePointer(
      lldb::addr_t, 
      SwiftASTContext::NonTriviallyManagedReferenceStrategy strategy);

  ConstString GetErrorBackstopName();

  ConstString GetStandardLibraryName();

  ConstString GetStandardLibraryBaseName();

  lldb::SyntheticChildrenSP
  GetBridgedSyntheticChildProvider(ValueObject &valobj);

  void WillStartExecutingUserExpression(bool);
  void DidFinishExecutingUserExpression(bool);

  bool IsABIStable();

protected:
  //------------------------------------------------------------------
  // Classes that inherit from SwiftLanguageRuntime can see and modify these
  //------------------------------------------------------------------
  SwiftLanguageRuntime(Process *process);

  Value::ValueType GetValueType(Value::ValueType static_value_type,
                                const CompilerType &static_type,
                                const CompilerType &dynamic_type,
                                bool is_indirect_enum_case);

  bool GetDynamicTypeAndAddress_Class(ValueObject &in_value,
                                      SwiftASTContext &scratch_ctx,
                                      lldb::DynamicValueType use_dynamic,
                                      TypeAndOrName &class_type_or_name,
                                      Address &address);

  bool GetDynamicTypeAndAddress_Protocol(ValueObject &in_value,
                                         CompilerType protocol_type,
                                         SwiftASTContext &scratch_ctx,
                                         lldb::DynamicValueType use_dynamic,
                                         TypeAndOrName &class_type_or_name,
                                         Address &address);

  bool GetDynamicTypeAndAddress_Value(ValueObject &in_value,
                                       CompilerType &bound_type,
                                       lldb::DynamicValueType use_dynamic,
                                       TypeAndOrName &class_type_or_name,
                                       Address &address);

  bool GetDynamicTypeAndAddress_IndirectEnumCase(
      ValueObject &in_value, lldb::DynamicValueType use_dynamic,
      TypeAndOrName &class_type_or_name, Address &address);

  bool GetDynamicTypeAndAddress_ClangType(ValueObject &in_value,
                                          lldb::DynamicValueType use_dynamic,
                                          TypeAndOrName &class_type_or_name,
                                          Address &address,
                                          Value::ValueType &value_type);

  MetadataPromiseSP GetPromiseForTypeNameAndFrame(const char *type_name,
                                                  StackFrame *frame);

  bool GetTargetOfPartialApply(SymbolContext &curr_sc, ConstString &apply_name,
                               SymbolContext &sc);

  AppleObjCRuntimeV2 *GetObjCRuntime();

  void SetupSwiftError();
  void SetupExclusivity();
  void SetupReflection();
  void SetupABIBit();

  const CompilerType &GetBoxMetadataType();

  std::shared_ptr<swift::remote::MemoryReader> GetMemoryReader();

  void PushLocalBuffer(uint64_t local_buffer, uint64_t local_buffer_size);

  void PopLocalBuffer();

  /// We have to load swift dependent libraries by hand, but if they
  /// are missing, we shouldn't keep trying.
  llvm::StringSet<> m_library_negative_cache;
  std::mutex m_negative_cache_mutex;

  llvm::Optional<lldb::addr_t> m_SwiftNativeNSErrorISA;

  std::shared_ptr<swift::remote::MemoryReader> m_memory_reader_sp;

  llvm::DenseMap<std::pair<swift::ASTContext *, lldb::addr_t>,
                 MetadataPromiseSP>
      m_promises_map;

  llvm::DenseMap<swift::ASTContext *,
                 std::unique_ptr<swift::remoteAST::RemoteASTContext>>
      m_remote_ast_contexts;

  /// Uses ConstStrings as keys to avoid storing the strings twice.
  llvm::DenseMap<const char *, lldb::SyntheticChildrenSP> m_bridged_synthetics_map;

  /// Cached member variable offsets.
  using MemberID = std::pair<const swift::TypeBase *, const char *>;
  llvm::DenseMap<MemberID, uint64_t> m_member_offsets;

  CompilerType m_box_metadata_type;

  /// These members are used to track and toggle the state of the "dynamic
  /// exclusivity enforcement flag" in the swift runtime. This flag is set to
  /// true when an LLDB expression starts running, and reset to its original
  /// state after that expression (and any other concurrently running
  /// expressions) terminates.
  /// \{
  std::mutex m_active_user_expr_mutex;
  uint32_t m_active_user_expr_count = 0;
  llvm::Optional<lldb::addr_t> m_dynamic_exclusivity_flag_addr =
    llvm::Optional<lldb::addr_t>();
  bool m_original_dynamic_exclusivity_flag_state = false;
  /// \}

private:
  using NativeReflectionContext = swift::reflection::ReflectionContext<
      swift::External<swift::RuntimeTarget<sizeof(uintptr_t)>>>;
  std::unique_ptr<NativeReflectionContext> reflection_ctx;

  DISALLOW_COPY_AND_ASSIGN(SwiftLanguageRuntime);
};

} // namespace lldb_private

#endif // liblldb_SwiftLanguageRuntime_h_
