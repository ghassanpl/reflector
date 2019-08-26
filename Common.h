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
#include <baselib/EnumFlags.h>
#include <baselib/Strings.h>
using baselib::string_view;
#include "Include/ReflectorClasses.h"

using nlohmann::json;

void PrintSafe(std::ostream& strm, std::string val);

template <typename... ARGS>
void ReportError(std::filesystem::path path, size_t line_num, ARGS&& ... args)
{
	PrintSafe(std::cerr, baselib::Stringify(path.string(), "(", line_num, ",1): error: ", std::forward<ARGS>(args)..., "\n"));
}

template <typename... ARGS>
void PrintLine(ARGS&&... args)
{
	PrintSafe(std::cout, baselib::Stringify(std::forward<ARGS>(args)..., "\n"));
}

enum class AccessMode { Unspecified, Public, Private, Protected };

static constexpr const char* AMStrings[] = { "Unspecified", "Public", "Private", "Protected" };

struct FileMirror;
struct Class;

struct Declaration
{
	json Attributes = json::object();
	std::string Name;
	size_t DeclarationLine = 0;
	AccessMode Access = AccessMode::Unspecified;
	std::vector<std::string> Comments;

	json ToJSON() const;
};

struct Field : public Declaration
{
	baselib::EnumFlags<Reflector::FieldFlags> Flags;
	std::string Type;
	std::string InitializingExpression;
	std::string DisplayName;

	void CreateArtificialMethods(FileMirror& mirror, Class& klass);

	json ToJSON() const;
};

enum class MethodFlags
{
	Inline,
	Virtual,
	Static,
	Const,
	Noexcept,
	Final,
	Explicit,
	Artificial,
	HasBody,
	NoCallable
};

struct Method : public Declaration
{
	std::string Type;
	baselib::EnumFlags<MethodFlags> Flags;
	std::string Parameters;
	std::string Body;
	size_t SourceFieldDeclarationLine = 0;
	std::string UniqueName;

	size_t ActualDeclarationLine() const
	{
		return DeclarationLine ? DeclarationLine : SourceFieldDeclarationLine;
	}

	std::string GetSignature(Class const& parent_class) const;

	void CreateArtificialMethods(FileMirror& mirror, Class& klass);

	json ToJSON() const;
};

enum class ClassFlags
{
	Struct,
	NoConstructors
};

struct Property
{
	std::string Name;
	std::string SetterName;
	size_t SetterLine = 0;
	std::string GetterName;
	size_t GetterLine = 0;
	std::string Type;

	void CreateArtificialMethods(FileMirror& mirror, Class& klass);
};

struct Class : public Declaration
{
	std::string ParentClass;

	std::vector<Field> Fields;
	std::vector<Method> Methods;
	std::map<std::string, Property, std::less<>> Properties;

	baselib::EnumFlags<ClassFlags> Flags;

	size_t BodyLine = 0;

	void AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, baselib::EnumFlags<MethodFlags> additional_flags = {}, size_t source_field_declaration_line = 0);
	void CreateArtificialMethods(FileMirror& mirror);

	std::map<std::string, std::vector<Method const*>> MethodsByName;

	json ToJSON() const;
};

struct Enumerator : Declaration
{
	int64_t Value = 0;

	json ToJSON() const;
};

struct Enum : public Declaration
{
	std::vector<Enumerator> Enumerators;

	json ToJSON() const;
};

struct FileMirror
{
	std::filesystem::path SourceFilePath;
	std::vector<Class> Classes;
	std::vector<Enum> Enums;

	json ToJSON() const;

	void CreateArtificialMethods();
};

extern uint64_t ChangeTime;
std::vector<FileMirror> const& GetMirrors();
void AddMirror(FileMirror mirror);
void CreateArtificialMethods();

struct Options
{
	Options(bool recursive, bool quiet, bool force, bool verbose, bool use_json, std::string_view annotation_prefix, std::string_view macro_prefix);

	bool Recursive = false;
	bool Quiet = false;
	bool Force = false;
	bool Verbose = false;
	bool UseJSON = true;

	std::string AnnotationPrefix = "R";
	std::string MacroPrefix = "REFLECT";

	std::string EnumPrefix;
	std::string EnumeratorPrefix;
	std::string ClassPrefix;
	std::string FieldPrefix;
	std::string MethodPrefix;
	std::string BodyPrefix;
};

inline std::string OnlyType(std::string str)
{
	auto last = str.find_last_of(':');
	if (last != std::string::npos)
		str = str.substr(last + 1);
	return str;
}