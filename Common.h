/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include <iostream>
#include <filesystem>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include <ghassanpl/enum_flags.h>
#include <ghassanpl/string_ops.h>
//#define FMT_HEADER_ONLY 1
//#include <fmt/format.h>
//#include <fmt/ostream.h>
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

struct Declaration
{
	json Attributes = json::object();
	std::string Name;
	size_t DeclarationLine = 0;
	AccessMode Access = AccessMode::Unspecified;
	std::vector<std::string> Comments;

	/// TODO: Fill this in for every declaration!
	uint64_t UID = 0;

	std::string Namespace;

	std::vector<Method const*> AssociatedArtificialMethods;

	std::string FullType() const
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}::{}", Namespace, Name);
	}

	std::string GeneratedUniqueName() const { return std::format("{}_{:016x}", Name, UID); }

	virtual std::string FullName(std::string_view sep = "_") const
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}{}{}", ghassanpl::string_ops::replaced(Namespace, "::", sep), sep, Name);
	}

	virtual ~Declaration() noexcept = default;

	virtual std::string_view DeclarationType() const = 0;

	json ToJSON() const;
};


struct Field : public Declaration
{
	Class* ParentClass{};

	Field(Class* parent) : ParentClass(parent) {}

	ghassanpl::enum_flags<Reflector::FieldFlags> Flags;
	std::string Type;
	std::string InitializingExpression;

	/// A name to be displayed in editors and such
	std::string DisplayName;

	/// A identifier name without any member prefixes
	std::string CleanName;

	void AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags = {});
	void CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options);

	virtual std::string FullName(std::string_view sep = "_") const override;

	json ToJSON() const;

	virtual std::string_view DeclarationType() const { return "Field"; }
};

using MethodParameter = Reflector::MethodReflectionData::Parameter;

inline json ToJSON(MethodParameter const& param)
{
	return { {"Name", param.Name}, {"Type", param.Type} };
}

struct Method : public Declaration
{
	Class* ParentClass{};

	Method(Class* parent);

	std::string ReturnType;
	ghassanpl::enum_flags<Reflector::MethodFlags> Flags;

	void SetParameters(std::string params);
	auto const& GetParameters() const { return mParameters; }

	std::vector<MethodParameter> ParametersSplit;
	std::string ParametersNamesOnly;
	std::string ParametersTypesOnly;
	std::string ArtificialBody;
	//size_t SourceFieldDeclarationLine = 0;
	Declaration const* SourceDeclaration{}; /// TODO: This
	std::string UniqueName;

	size_t ActualDeclarationLine() const
	{
		return DeclarationLine ? DeclarationLine : SourceDeclaration->DeclarationLine;
	}

	std::string GetSignature(Class const& parent_class) const;

	void AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags = {});
	void CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options);

	virtual std::string FullName(std::string_view sep = "_") const override;

	json ToJSON() const;

	virtual std::string_view DeclarationType() const { return "Method"; }

private:
	std::string mParameters;
	void Split();
};

struct Property
{
	std::string Name;
	std::string SetterName;
	size_t SetterLine = 0;
	std::string GetterName;
	size_t GetterLine = 0;
	std::string Type;

	void CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options);
};

struct Class : public Declaration
{
	std::string BaseClass;

	std::vector<std::unique_ptr<Field>> Fields;
	std::vector<std::unique_ptr<Method>> Methods;
	std::map<std::string, Property, std::less<>> Properties;
	std::vector<std::string> AdditionalBodyLines;

	json DefaultFieldAttributes = json::object();
	json DefaultMethodAttributes = json::object();

	ghassanpl::enum_flags<ClassFlags> Flags;

	size_t BodyLine = 0;

	std::map<std::string, std::vector<Method const*>> MethodsByName;

	Method* AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags = {});
	void CreateArtificialMethods(FileMirror& mirror, Options const& options);

	json ToJSON() const;

	static Class const* FindClassByPossiblyQualifiedName(std::string_view class_name, Class const* search_context)
	{
		return nullptr;
	}

	std::vector<Class const*> GetInheritanceList() const
	{
		std::vector<Class const*> result;
		auto current = this;
		while (auto parent = FindClassByPossiblyQualifiedName(current->BaseClass, current))
		{
			result.push_back(parent);
		}
		return result;
	}

	virtual std::string_view DeclarationType() const { return "Class"; }

private:

	std::vector<std::unique_ptr<Method>> mArtificialMethods;
};

struct Enumerator : Declaration
{
	Enum* ParentEnum{};

	Enumerator(Enum* parent) : ParentEnum(parent) {}

	int64_t Value = 0;

	ghassanpl::enum_flags<EnumeratorFlags> Flags;

	json ToJSON() const;

	virtual std::string_view DeclarationType() const { return "Enumerator"; }
};

struct Enum : public Declaration
{
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

	json ToJSON() const;

	virtual std::string_view DeclarationType() const { return "Enum"; }
};

struct FileMirror
{
	path SourceFilePath;
	std::vector<std::unique_ptr<Class>> Classes;
	std::vector<std::unique_ptr<Enum>> Enums;

	json ToJSON() const;

	void CreateArtificialMethods(Options const& options);
	
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
std::vector<FileMirror> const& GetMirrors();
void AddMirror(FileMirror mirror);
void CreateArtificialMethods(Options const& options);

inline std::string OnlyType(std::string str)
{
	auto last = str.find_last_of(':');
	if (last != std::string::npos)
		str = str.substr(last + 1);
	return str;
}