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
#define FMT_HEADER_ONLY 1
#include <fmt/format.h>
#include <fmt/ostream.h>
using baselib::string_view;
#include "Include/ReflectorClasses.h"
using std::filesystem::path;

using nlohmann::json;

void PrintSafe(std::ostream& strm, std::string val);

template <typename... ARGS>
void ReportError(path path, size_t line_num, ARGS&& ... args)
{
	PrintSafe(std::cerr, fmt::format("{}({},1): error: {}\n", path.string(), line_num, fmt::format(std::forward<ARGS>(args)...)));
}

template <typename... ARGS>
void PrintLine(ARGS&&... args)
{
	PrintSafe(std::cout, fmt::format(std::forward<ARGS>(args)...) + "\n");
}

std::string EscapeJSON(json const& json);

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
	enum_flags::enum_flags<Reflector::FieldFlags> Flags;
	std::string Type;
	std::string InitializingExpression;
	std::string DisplayName;

	void CreateArtificialMethods(FileMirror& mirror, Class& klass);

	json ToJSON() const;
};

/*
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
};*/

struct Method : public Declaration
{
	struct MethodParameter
	{
		std::string Name;
		std::string Type;
		//std::string DefaultValue;
		json ToJSON() const
		{
			//return { {"Name", Name}, {"Type", Type}, {"DefaultValue", DefaultValue} };
			return { {"Name", Name}, {"Type", Type} };
		}
	};

	std::string Type;
	enum_flags::enum_flags<Reflector::MethodFlags> Flags;
private:
	std::string mParameters;
	void Split();
public:
	void SetParameters(std::string params);
	auto const& GetParameters() const { return mParameters; }
	std::vector<MethodParameter> ParametersSplit;
	std::string ParametersTypesOnly;
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
	DeclaredStruct,
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

	enum_flags::enum_flags<ClassFlags> Flags;

	size_t BodyLine = 0;

	void AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, enum_flags::enum_flags<Reflector::MethodFlags> additional_flags = {}, size_t source_field_declaration_line = 0);
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
	path SourceFilePath;
	std::vector<Class> Classes;
	std::vector<Enum> Enums;

	json ToJSON() const;

	void CreateArtificialMethods();
};

extern uint64_t ChangeTime;
std::vector<FileMirror*> const& GetMirrors();
void AddMirror(FileMirror* mirror);
void CreateArtificialMethods();

struct Options
{
	//Options(json&& options_file);
	Options(path const& options_file_path);

	bool Recursive = false;
	bool Quiet = false;
	bool Force = false;
	bool Verbose = false;
	bool UseJSON = true;
	bool CreateArtifacts = true;
	bool CreateDatabase = true;

	/// TODO: Read this from cmdline
	bool ForwardDeclare = true;

	path ArtifactPath;

	std::vector<path> PathsToScan;

	std::string AnnotationPrefix = "R";
	std::string MacroPrefix = "REFLECT";

	std::string MirrorExtension = ".mirror";
	std::vector<std::string> ExtensionsToScan = { ".h", ".hpp", ".cpp" };

	std::string EnumPrefix;
	std::string EnumeratorPrefix;
	std::string ClassPrefix;
	std::string FieldPrefix;
	std::string MethodPrefix;
	std::string BodyPrefix;

	path OptionsFilePath;
	json OptionsFile;
};

inline std::string OnlyType(std::string str)
{
	auto last = str.find_last_of(':');
	if (last != std::string::npos)
		str = str.substr(last + 1);
	return str;
}