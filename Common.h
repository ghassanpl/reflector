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

	std::string FullName() const
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}_{}", ghassanpl::string_ops::replaced(Namespace, "::", "_"), Name);
	}

	std::string FullType() const
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}::{}", Namespace, Name);
	}

	std::string GeneratedUniqueName() const { return std::format("{}_{:016x}", Name, UID); }

	json ToJSON() const;
};


struct Field : public Declaration
{
	ghassanpl::enum_flags<Reflector::FieldFlags> Flags;
	std::string Type;
	std::string InitializingExpression;
	std::string DisplayName;
	std::string CleanName;

	void CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options);

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
	ghassanpl::enum_flags<Reflector::MethodFlags> Flags;
private:
	std::string mParameters;
	void Split();
public:
	void SetParameters(std::string params);
	auto const& GetParameters() const { return mParameters; }
	std::vector<MethodParameter> ParametersSplit;
	std::string ParametersNamesOnly;
	std::string ParametersTypesOnly;
	std::string ArtificialBody;
	size_t SourceFieldDeclarationLine = 0;
	std::string UniqueName;

	size_t ActualDeclarationLine() const
	{
		return DeclarationLine ? DeclarationLine : SourceFieldDeclarationLine;
	}

	std::string GetSignature(Class const& parent_class) const;

	void CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options);

	json ToJSON() const;
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
	std::string ParentClass;

	std::vector<Field> Fields;
	std::vector<Method> Methods;
	std::map<std::string, Property, std::less<>> Properties;
	std::vector<std::string> AdditionalBodyLines;

	ghassanpl::enum_flags<ClassFlags> Flags;

	size_t BodyLine = 0;

	void AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags = {}, size_t source_field_declaration_line = 0);
	void CreateArtificialMethods(FileMirror& mirror, Options const& options);

	std::map<std::string, std::vector<Method const*>> MethodsByName;

	json ToJSON() const;

private:

	std::vector<Method> mArtificialMethods;
};

struct Enumerator : Declaration
{
	int64_t Value = 0;

	ghassanpl::enum_flags<EnumeratorFlags> Flags;

	json ToJSON() const;
};

struct Enum : public Declaration
{
	std::vector<Enumerator> Enumerators;

	ghassanpl::enum_flags<EnumFlags> Flags;

	bool IsConsecutive() const
	{
		for (size_t i = 1; i < Enumerators.size(); ++i)
			if (Enumerators[i].Value != Enumerators[i - 1].Value + 1)
				return false;
		return true;
	}

	json ToJSON() const;
};

struct FileMirror
{
	path SourceFilePath;
	std::vector<Class> Classes;
	std::vector<Enum> Enums;

	json ToJSON() const;

	void CreateArtificialMethods(Options const& options);
};

extern uint64_t ChangeTime;
std::vector<FileMirror> const& GetMirrors();
void AddMirror(FileMirror mirror);
void CreateArtificialMethods(Options const& options);

struct Options
{
	/// TODO: We should document all of this

	Options(path exe_path, path const& options_file_path);

	bool Recursive = false;
	bool Quiet = false;
	bool Force = false;
	bool Verbose = false;
	bool UseJSON = true;
	/// TODO: Check if these work in combinations
	bool CreateArtifacts = true;
	bool CreateDatabase = true;
	bool GenerateLuaFunctionBindings = false; /// Maybe instead of Lua function bindings, since this options is loaded from a JSON, we can have a nice JSON structure that defines how to create scripting bindings?
	bool GenerateTypeIndices = false;
	bool ForwardDeclare = true;

	/// TODO: We should also load all these

	std::string JSONHeaderPath = "<nlohmann/json.hpp>";
	std::string JSONParseFunction = "::nlohmann::json::parse";
	std::string JSONType = "::nlohmann::json";

	/// TODO: Will create std::maps and such for easy access to database information
	bool GenerateEasyDatabaseAccess = false;


	path ExePath;

	path ArtifactPath;

	std::vector<path> PathsToScan;

	std::string AnnotationPrefix = "R";
	std::string MacroPrefix = "REFLECT";

	std::string MirrorExtension = ".mirror";
	std::vector<std::string> ExtensionsToScan = { ".h", ".hpp" };

	std::string EnumPrefix;
	std::string EnumeratorPrefix;
	std::string ClassPrefix;
	std::string FieldPrefix;
	std::string MethodPrefix;
	std::string BodyPrefix;

	path OptionsFilePath;
	json OptionsFile;

	std::string GetterPrefix = "Get";
	std::string SetterPrefix = "Set"; /// both for regular setters as well as flag setters
	std::string IsPrefix = "Is";
	std::string IsNotPrefix = "IsNot";
	std::string SetNotPrefix = "SetNot";
	std::string UnsetPrefix = "Unset";
	std::string TogglePrefix = "Toggle";
	std::string ProxyMethodPrefix = "_proxy_";
	std::string SingletonInstanceGetterName = "SingletonInstance";
	std::string ProxyClassSuffix = "_Proxy";
};

inline std::string OnlyType(std::string str)
{
	auto last = str.find_last_of(':');
	if (last != std::string::npos)
		str = str.substr(last + 1);
	return str;
}