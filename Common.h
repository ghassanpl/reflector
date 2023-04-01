/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include <iostream>
#include <filesystem>
#include <vector>
#if __has_include(<expected>)
#include <expected>
using std::expected;
using std::unexpected;
#else
#include <tl/expected.hpp>
using tl::expected;
using tl::unexpected;
#endif
#include <nlohmann/json.hpp>
#include <ghassanpl/enum_flags.h>
#include <ghassanpl/string_ops.h>
#include <magic_enum_format.hpp>
#include <format>
using std::string_view;
#include "Include/ReflectorClasses.h"
using Reflector::ClassFlags;
using Reflector::EnumeratorFlags;
using Reflector::EnumFlags;
using Reflector::FieldFlags;
using Reflector::MethodFlags;
using std::filesystem::path;

using nlohmann::json;

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
void ReportError(path path, size_t line_num, std::string_view fmt, ARGS&& ... args)
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

static constexpr const char* AMStrings[] = { "Unspecified", "Public", "Private", "Protected" };

uint64_t GenerateUID(std::filesystem::path const& file_path, size_t declaration_line);

struct FileMirror;
struct Class;
struct Method;
struct Enum;

enum class DeclarationType
{
	Field,
	Method,
	Property,
	Class,
	Enum,
	Enumerator,
};

struct Declaration
{
	json Attributes = json::object();
	std::string Name;
	size_t DeclarationLine = 0;
	AccessMode Access = AccessMode::Unspecified;
	std::vector<std::string> Comments;

	/// TODO: Fill this in for every declaration!
	uint64_t UID = 0;

	bool Document = true;

	/// TODO: Should we turn this back into a vector since the docs are no longer looking for them?
	std::map<std::string, Method const*, std::less<>> AssociatedArtificialMethods;
	Method const* GetAssociatedMethod(std::string_view function_type) const;
	
	/// TODO: We should probably list the possible doc notes somewhere so the IgnoreDocNotes option is easier to set
	std::vector<std::pair<std::string, std::string>> DocNotes;
	template <typename... ARGS>
	void AddDocNote(std::string header, std::string_view str, ARGS&& ... args)
	{
		DocNotes.emplace_back(move(header), std::vformat(str, std::make_format_args(std::forward<ARGS>(args)...)));
	}

	std::string GeneratedUniqueName() const { return std::format("{}_{:016x}", Name, UID); }

	virtual std::string FullName(std::string_view sep = "_") const
	{
		return Name;
	}

	virtual ~Declaration() noexcept = default;

	virtual DeclarationType DeclarationType() const = 0;

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
};

template <typename PARENT_TYPE>
struct MemberDeclaration : public Declaration
{
	PARENT_TYPE* ParentType{};

	MemberDeclaration(PARENT_TYPE* parent) : ParentType(parent) {}

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

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Field; }
};

using MethodParameter = Reflector::MethodReflectionData::Parameter;

inline json ToJSON(MethodParameter const& param)
{
	return { {"Name", param.Name}, {"Type", param.Type} };
}

struct Method : public MemberDeclaration<Class>
{
	using MemberDeclaration<Class>::MemberDeclaration;

	std::string ReturnType;
	ghassanpl::enum_flags<Reflector::MethodFlags> Flags;

	void SetParameters(std::string params);
	auto const& GetParameters() const { return mParameters; }

	std::vector<MethodParameter> ParametersSplit;
	std::string ParametersNamesOnly;
	std::string ParametersTypesOnly;
	std::string ArtificialBody;
	Declaration const* SourceDeclaration{};
	std::string UniqueName;

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
};

struct Class : public TypeDeclaration
{
	using TypeDeclaration::TypeDeclaration;

	std::string BaseClass;

	std::vector<std::unique_ptr<Field>> Fields;
	std::vector<std::unique_ptr<Method>> Methods;
	std::map<std::string, Property, std::less<>> Properties; /// TODO: Document these! These are generated via 'GetterFor'/'SetterFor' methods
	std::vector<std::string> AdditionalBodyLines;

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
		return nullptr;
	}

	std::vector<Class const*> GetInheritanceList() const
	{
		std::vector<Class const*> result;
		const auto current = this;
		while (auto parent = FindClassByPossiblyQualifiedName(current->BaseClass, current))
		{
			result.push_back(parent);
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
};

struct Enum : public TypeDeclaration
{
	using TypeDeclaration::TypeDeclaration;

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
		return Enumerators.size() > 0 && Enumerators[0]->Value == 0 && IsConsecutive();
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

	bool IsEmpty() const { return !(Classes.size() > 0 || Enums.size() > 0); }

	json ToJSON() const;

	void CreateArtificialMethodsAndDocument(Options const& options);
	
	FileMirror();
	FileMirror(FileMirror const&) = delete;
	FileMirror(FileMirror&&) noexcept = default;
	FileMirror& operator=(FileMirror const&) = delete;
	FileMirror& operator=(FileMirror&&) noexcept = default;
};

inline auto FormatPreFlags(enum_flags<Reflector::FieldFlags> flags) {
	std::vector<std::string_view> prefixes;
	if (flags.is_set(FieldFlags::Mutable)) prefixes.push_back("mutable ");
	if (flags.is_set(FieldFlags::Static)) prefixes.push_back("static ");
	return join(prefixes, "");
}

inline auto FormatPreFlags(enum_flags<Reflector::MethodFlags> flags) {
	std::vector<std::string_view> prefixes;
	if (flags.is_set(MethodFlags::Inline)) prefixes.push_back("inline ");
	if (flags.is_set(MethodFlags::Static)) prefixes.push_back("static ");
	if (flags.is_set(MethodFlags::Virtual)) prefixes.push_back("virtual ");
	if (flags.is_set(MethodFlags::Explicit)) prefixes.push_back("explicit ");
	return join(prefixes, "");
}

inline auto FormatPostFlags(enum_flags<Reflector::MethodFlags> flags) {
	std::vector<std::string_view> suffixes;
	if (flags.is_set(MethodFlags::Const)) suffixes.push_back(" const");
	if (flags.is_set(MethodFlags::Final)) suffixes.push_back(" final");
	if (flags.is_set(MethodFlags::Noexcept)) suffixes.push_back(" noexcept");
	return join(suffixes, "");
}

extern uint64_t ChangeTime;
std::vector<FileMirror const*> GetMirrors();
FileMirror* AddMirror();
void RemoveEmptyMirrors();
void CreateArtificialMethodsAndDocument(Options const& options);

inline std::string OnlyType(std::string str)
{
	if (const auto last = str.find_last_of(':'); last != std::string::npos)
		str = str.substr(last + 1);
	return str;
}