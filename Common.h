/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include <iostream>
#include <filesystem>
#include <vector>
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L && __has_include(<expected>)
#include <expected>
using std::expected;
using std::unexpected;
#else
#include <tl/expected.hpp>
namespace std { /// NOTE: Undefined behavior, technically
	using tl::expected;
	using tl::unexpected;
}
#endif
#include <nlohmann/json.hpp>
#include <ghassanpl/enum_flags.h>
#include <ghassanpl/string_ops.h>
#include <magic_enum_format.hpp>
#include <format>
using std::string_view;
using nlohmann::json;

#include "Include/ReflectorClasses.h"
using Reflector::ClassFlags;
using Reflector::EnumeratorFlags;
using Reflector::EnumFlags;
using Reflector::FieldFlags;
using Reflector::MethodFlags;
using std::filesystem::path;

using namespace ghassanpl;
using namespace ghassanpl::string_ops;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct Options;
struct Artifactory;

inline string_view TrimWhitespace(std::string_view str)
{
	return ghassanpl::string_ops::trimmed_whitespace(str);
}

void PrintSafe(std::ostream& strm, std::string val);

template <typename... ARGS>
void ReportError(path const& path, size_t line_num, std::string_view fmt, ARGS&& ... args)
{
	PrintSafe(std::cerr, std::format("{}({},1): error: {}\n", path.string(), line_num, std::vformat(fmt, std::make_format_args(std::forward<ARGS>(args)...))));
}

template <typename... ARGS>
void PrintLine(std::string_view fmt, ARGS&&... args)
{
	PrintSafe(std::cout, std::vformat(fmt, std::make_format_args(std::forward<ARGS>(args)...)) + "\n");
}

template <typename... ARGS>
void Print(std::string_view fmt, ARGS&&... args)
{
	PrintSafe(std::cout, std::vformat(fmt, std::make_format_args(std::forward<ARGS>(args)...)));
}

std::string EscapeJSON(json const& json);
std::string EscapeString(std::string_view str);

enum class AccessMode { Unspecified, Public, Private, Protected };
std::string FormatAccess(AccessMode mode);

static constexpr const char* AMStrings[] = { "Unspecified", "Public", "Private", "Protected" };

uint64_t GenerateUID(std::filesystem::path const& file_path, size_t declaration_line);

struct FileMirror;
struct TypeDeclaration;
struct Class;
struct Method;
struct Enum;
struct Enumerator;

Enum const* FindEnum(string_view name);
std::vector<Class const*> FindClasses(string_view name);
std::vector<TypeDeclaration const*> FindTypes(string_view name);

enum class DeclarationType
{
	Field,
	Method,
	Property,
	Class,
	Enum,
	Enumerator,
	Namespace,
	Parameter,
	ReturnType,

	/*
	* Constant,
	* Event,
	* Interface,
	* Operator,
	*/
};

/// TODO: How much of the data in these classes should we output into the reflection system (*ReflectionData classes, etc)?
/// In particular: Should we output doc note and comments?

struct DocNote
{
	std::string Header;
	std::string Contents;
	bool ShowInMemberList = false;
	std::string Icon = "";
};

void to_json(json& j, DocNote const& p);

struct SimpleDeclaration
{
	std::string Name;
	std::vector<std::string> Comments;

	void ForEachCommentDirective(std::string_view directive_name, std::function<void(std::span<const std::string>)> callback) const;

	auto NonDirectiveCommentLines() const
	{
		return Comments | std::ranges::views::filter([](std::string_view s) { return !trimmed_whitespace_left(s).starts_with('@'); });
	}

	std::optional<bool> ForceDocument;
	std::optional<std::string> Deprecation;

	virtual bool Document() const;
		
	std::vector<DocNote> DocNotes;

	enum_flags<Reflector::EntityFlags> EntityFlags;

	bool Unimplemented() const { return EntityFlags.is_set(Reflector::EntityFlags::Unimplemented); }

	template <typename... ARGS>
	DocNote& AddDocNote(std::string header, std::string_view str, ARGS&& ... args)
	{
		return DocNotes.emplace_back(std::move(header), std::vformat(str, std::make_format_args(std::forward<ARGS>(args)...)));
	}

	template <typename... ARGS>
	DocNote& AddWarningDocNote(std::string header, std::string_view str, ARGS&& ... args)
	{
		auto& note = DocNotes.emplace_back(std::move(header), std::vformat(str, std::make_format_args(std::forward<ARGS>(args)...)));
		note.ShowInMemberList = true;
		note.Icon = "warning";
		return note;
	}

	json ToJSON() const;

	virtual ~SimpleDeclaration() noexcept = default;
};

enum class LinkFlag
{
	Parent,
	SignatureSpecifiers, /// const, noexcept
	Specifiers, /// rest
	ReturnType,
	Parameters,
	Namespace,
	DeclarationType,
};
using LinkFlags = enum_flags<LinkFlag>;

struct Declaration : public SimpleDeclaration
{
	size_t DeclarationLine = 0;
	json Attributes = json::object();
	AccessMode Access = AccessMode::Unspecified;

	/// TODO: Fill this in for every declaration!
	uint64_t UID = 0;

	/// These are in a map for historical reasons mostly, but it's easier to find bugs this way
	std::map<std::string, Method const*, std::less<>> AssociatedArtificialMethods;

	bool DocumentMembers = true;
	
	std::string GeneratedUniqueName() const { return std::format("{}_{:016x}", Name, UID); }

	virtual std::string FullName(std::string_view sep = "_") const
	{
		return Name;
	}

	virtual std::string MakeLink(LinkFlags flags = {}) const = 0;

	virtual DeclarationType DeclarationType() const = 0;

	void CreateArtificialMethodsAndDocument(Options const& options);

	virtual void AddNoDiscard(std::optional<std::string> reason) {}

	virtual json ToJSON() const;
};

template <typename... ARGS>
void ReportError(Declaration const& decl, std::string_view fmt, ARGS&& ... args)
{
	ReportError("TODO declaration file", decl.DeclarationLine, fmt, std::forward<ARGS>(args)...);
}

struct TypeDeclaration : public Declaration
{
	FileMirror* ParentMirror = nullptr;
	std::string Namespace;

	TypeDeclaration(FileMirror* parent) : ParentMirror(parent) {}

	std::string FullType() const
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}::{}", Namespace, Name);
	}

	virtual std::string FullName(std::string_view sep = "_") const override
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}{}{}", ghassanpl::string_ops::replaced(Namespace, "::", sep), sep, Name);
	}

	virtual json ToJSON() const override
	{
		auto result = Declaration::ToJSON();
		if (!Namespace.empty())
			result["Namespace"] = Namespace;
		return result;
	}
	
	virtual void AddNoDiscard(std::optional<std::string> reason) override
	{
		AddDocNote("No Discard", "The compiler will warn you if you discard a function return value of this type.");
	}

	static TypeDeclaration const* FindTypeByPossiblyQualifiedName(std::string_view type_name, TypeDeclaration const* search_context)
	{
		if (auto types = FindTypes(type_name); !types.empty())
		{
			if (types.size() == 1)
				return types[0];

			/// TODO: Use search_context to locate single candidate
			/// ALgo:
			///		auto path = split(search_context->Namespace, "::");
			///		while (!path.empty()) {
			///			if (auto klass = FindClassByFullName(join(path, "::") + "::" + class_name)) return klass;
			///			path = path.parent_path();
			///		}
		}
		return nullptr;
	}

	virtual std::string MakeLink(LinkFlags flags = {}) const override;
};

struct BaseMemberDeclaration : Declaration
{
	virtual Declaration* ParentDecl() const = 0;

	virtual bool Document() const override;

	void CreateArtificialMethodsAndDocument(Options const& options);
};

template <typename PARENT_TYPE>
struct MemberDeclaration : public BaseMemberDeclaration
{
	PARENT_TYPE* ParentType{};

	MemberDeclaration(PARENT_TYPE* parent) : ParentType(parent) {}

	virtual Declaration* ParentDecl() const override { return ParentType; }

	PARENT_TYPE* Parent() const { return ParentType; }

	virtual std::string FullName(std::string_view sep = "_") const override
	{
		return std::format("{}{}{}", ParentType->FullName(sep), sep, Declaration::FullName(sep));
	}
};

struct Field : public MemberDeclaration<Class>
{
	using MemberDeclaration<Class>::MemberDeclaration;

	ghassanpl::enum_flags<Reflector::FieldFlags> Flags;
	std::string Type;
	std::string InitializingExpression;

	/// A name to be displayed in editors and such
	std::string DisplayName;

	/// A identifier name without any member prefixes
	std::string CleanName;

	Method* AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags = {});
	void CreateArtificialMethodsAndDocument(Options const& options);

	virtual json ToJSON() const override;

	virtual std::string MakeLink(LinkFlags flags = {}) const override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Field; }
};

struct MethodParameter : SimpleDeclaration
{
	std::string Type;
	std::string Initializer;

	json ToJSON() const;
};

struct Method : public MemberDeclaration<Class>
{
	using MemberDeclaration<Class>::MemberDeclaration;

	ghassanpl::enum_flags<Reflector::MethodFlags> Flags;

	void SetParameters(std::string params);
	auto const& GetParameters() const { return mParameters; }

	std::vector<MethodParameter> ParametersSplit;
	std::string ParametersNamesOnly;
	std::string ParametersTypesOnly;
	std::string ArtificialBody;
	Declaration const* SourceDeclaration{};
	std::string UniqueName;
	SimpleDeclaration Return;

	virtual std::string MakeLink(LinkFlags flags = {}) const override;

	size_t ActualDeclarationLine() const
	{
		return DeclarationLine ? DeclarationLine : SourceDeclaration->DeclarationLine;
	}

	std::string GetSignature(Class const& parent_class) const;

	Method* AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags = {});
	void CreateArtificialMethodsAndDocument(Options const& options);

	virtual std::string FullName(std::string_view sep = "_") const override;

	virtual json ToJSON() const override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Method; }

	virtual void AddNoDiscard(std::optional<std::string> reason) override
	{
		Flags += MethodFlags::NoDiscard;
		AddDocNote("No Discard", "The compiler will warn you if you discard this function's return value.");
	}

private:
	std::string mParameters;
	void Split();
};

struct Property : MemberDeclaration<Class>
{
	using MemberDeclaration<Class>::MemberDeclaration;

	Method const* Setter = nullptr;
	Method const* Getter = nullptr;
	std::string Type;

	void CreateArtificialMethodsAndDocument(Options const& options);

	virtual json ToJSON() const override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Property; }

	virtual std::string MakeLink(LinkFlags flags = {}) const;
};

struct Class : public TypeDeclaration
{
	using TypeDeclaration::TypeDeclaration;

	std::string BaseClass;

	std::vector<std::unique_ptr<Field>> Fields;
	std::vector<std::unique_ptr<Method>> Methods;
	std::map<std::string, Property, std::less<>> Properties; /// TODO: Document these! These are generated via 'GetterFor'/'SetterFor' methods and via private fields with getters/setters
	std::vector<std::string> AdditionalBodyLines;

	struct ClassDeclaredFlag
	{
		std::string Name;
		Field* SourceField = nullptr;
		Enumerator* Represents = nullptr;
		std::vector<Method*> GeneratedArtificialMethods;
	};

	std::vector<ClassDeclaredFlag> ClassDeclaredFlags; /// TODO: Generated from Flags=... fields. Should create a Fields section in the class documentation if not empty

	json DefaultFieldAttributes = json::object();
	json DefaultMethodAttributes = json::object();

	ghassanpl::enum_flags<ClassFlags> Flags;

	size_t BodyLine = 0;

	std::map<std::string, std::vector<Method const*>> MethodsByName;

	Method* AddArtificialMethod(
		Declaration& for_decl, 
		std::string function_type, 
		std::string results, 
		std::string name, 
		std::string parameters, 
		std::string body, 
		std::vector<std::string> comments, 
		ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags = {}
	);
	void CreateArtificialMethodsAndDocument(Options const& options);

	Property& EnsureProperty(std::string name);

	virtual json ToJSON() const override;

	static Class const* FindClassByPossiblyQualifiedName(std::string_view class_name, Class const* search_context)
	{
		/// TODO: This needs to be better, as it won't find a semi-qualified class, e.g. `B::C` won't find class '::A::B::C`
		if (auto klasses = FindClasses(class_name); !klasses.empty())
		{
			if (klasses.size() == 1)
				return klasses[0];
			
			/// TODO: Use search_context to locate single candidate
			/// ALgo:
			///		auto path = split(search_context->Namespace, "::");
			///		while (!path.empty()) {
			///			if (auto klass = FindClassByFullName(join(path, "::") + "::" + class_name)) return klass;
			///			path = path.parent_path();
			///		}
		}
		return nullptr;
	}

	std::vector<Class const*> GetInheritanceList() const
	{
		std::vector<Class const*> result;
		auto current = this;
		while (current = FindClassByPossiblyQualifiedName(current->BaseClass, current))
		{
			result.push_back(current);
		}
		return result;
	}

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Class; }

private:

	std::vector<std::unique_ptr<Method>> mArtificialMethods;
};

struct Enumerator : public MemberDeclaration<Enum>
{
	using MemberDeclaration<Enum>::MemberDeclaration;

	int64_t Value = 0;

	std::string Opposite; /// TODO: Initialize this from options & attribute

	ghassanpl::enum_flags<EnumeratorFlags> Flags;

	virtual json ToJSON() const override;

	/// TODO: We should also use the artificial method subsystem for enumerators, as it means they will get docnotes
	void CreateArtificialMethodsAndDocument(Options const& options);

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Enumerator; }

	virtual std::string MakeLink(LinkFlags flags = {}) const override;
};

struct Enum : public TypeDeclaration
{
	using TypeDeclaration::TypeDeclaration;
	
	std::string BaseType;

	std::vector<std::unique_ptr<Enumerator>> Enumerators;

	json DefaultEnumeratorAttributes = json::object();

	ghassanpl::enum_flags<EnumFlags> Flags;

	bool IsConsecutive() const
	{
		for (size_t i = 1; i < Enumerators.size(); ++i)
			if (Enumerators[i]->Value != Enumerators[i - 1]->Value + 1)
				return false;
		return true;
	}

	bool IsTrivial() const
	{
		return !Enumerators.empty() && Enumerators[0]->Value == 0 && IsConsecutive();
	}

	virtual json ToJSON() const override;

	/// TODO: We should also use the artificial method subsystem to create functions like GetNext, etc.
	/// as they will be documented/referenced that way!
	void CreateArtificialMethodsAndDocument(Options const& options);

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Enum; }
};

struct FileMirror
{
	path SourceFilePath;
	std::vector<std::unique_ptr<Class>> Classes;
	std::vector<std::unique_ptr<Enum>> Enums;

	bool IsEmpty() const { return Classes.empty() && Enums.empty(); }

	json ToJSON() const;

	void CreateArtificialMethodsAndDocument(Options const& options);

	void Sort();
	
	FileMirror() = default;
	FileMirror(FileMirror const&) = delete;
	FileMirror(FileMirror&&) noexcept = default;
	FileMirror& operator=(FileMirror const&) = delete;
	FileMirror& operator=(FileMirror&&) noexcept = default;
};

std::string FormatPreFlags(enum_flags<Reflector::FieldFlags> flags, enum_flags<Reflector::FieldFlags> except = {});
std::string FormatPreFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except = {});
std::string FormatPostFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except = {});
std::string Icon(std::string_view icon);
std::string IconFor(DeclarationType type);

extern uint64_t ExecutableChangeTime;
extern uint64_t InvocationTime;
std::vector<FileMirror const*> GetMirrors();
FileMirror* AddMirror();
void RemoveEmptyMirrors();
void SortMirrors();
void CreateArtificialMethodsAndDocument(Options const& options);

inline std::string OnlyType(std::string str)
{
	if (const auto last = str.find_last_of(':'); last != std::string::npos)
		str = str.substr(last + 1);
	return str;
}
