#pragma once

#include "Common.h"
#include "ReflectorUtils.h"

#include <nlohmann/json.hpp>

#if __has_include("Options.h.mirror")
static inline constexpr bool BootstrapBuild = false;
#include "Options.h.mirror"
#else
static inline constexpr bool BootstrapBuild = true;
#include "DummyReflector.h"
#endif

/// NOTE: Changing this file requires a bootstrap rebuild!

RClass(DefaultFieldAttributes = { Setter = false, Getter = false });
struct JSONOptions
{
	RBody();

	/// Whether or not to use JSON C++ types to represent attribute reflection data (instead of just a stringified version)
	RField();
	bool Use = true;

	/// The header path to include in order for JSON attribute data to work
	RField();
	std::string HeaderPath = "<nlohmann/json.hpp>";

	/// The type that stores the JSON attribute data
	RField();
	std::string Type = "::nlohmann::json";

	/// The function that takes the stringified attribute data and turns into the JSON type
	RField();
	std::string ParseFunction = "::nlohmann::json::parse";
};

RClass(DefaultFieldAttributes = { Setter = false, Getter = false });
struct DocumentationOptions
{
	RBody();

	/// Whether or not to generate documentation
	RField();
	bool Generate = true;

	/// TODO: A structure that somehow turns filenames into github (or other) links
	RField();
	bool FileLinkResolve = false;

	/// Will be added to the end of page titles
	RField();
	std::string PageTitleSuffix = " - Documentation";

	/// TODO: Will be used in addition to and instead of the default styles
	RField();
	json AdditionalStyles = json::object();
	
	/// TODO: Will be added directly to the <head> tag of each page
	RField();
	std::string AdditionalHeadTags {};

	/// TODO: language tag (e.g. "en") to add to the <html> tag to define the language of the page
	RField();
	std::string Language = "en";

	/// TODO: The directory to put documentation into, (relative to artifact directory or options.json file)?
	RField();
	path DocumentationDirectory = "Documentation";

	/// TODO: Types in signatures to replace, e.g "ImageResolvable" -> "Resolvable<Image>", or smth
	RField();
	std::map<std::string, std::string> TypeAliases {};

	/// TODO: Enables syntax highlighting of additional types
	RField();
	std::vector<std::string> AdditionalTypesToHighlight {};

	/// TODO: If true, will generate a .html for each reflected header, with syntax highlighting and line anchors, so we can easily reference them
	RField();
	bool GenerateSyntaxHighlightedSourceFiles = false;

	/// TODO: Will generate a warning on execution for each reflected entity that isn't documented but has `Document` attribute set to true (the default)
	RField();
	bool WarnOnUndocumentedEntites = false;

	/// TODO: If a method has comments (like \param or <param>) that describe its parameters, validates that the names are the same as the actual method parameters
	RField();
	bool ValidateMethodArgumentComments = false;

	/// TODO: List all the doc note headers (like "On Change" or "Not Editable") that should not be output for any entities
	RField();
	std::vector<std::string> IgnoreDocNotes {};
};

RClass(DefaultFieldAttributes = { Setter = false, Getter = false });
struct Options
{
	RBody();

	Options(path exe_path, path const& options_file_path);

	/// A filename or list of filenames (or directories) to scan for reflectable types
	RField(Required);
	json Files{};

	/// Path to the directory where the general artifact files will be created (relative to ???)
	RField();
	path ArtifactPath;

	/// Whether to recursively search the provided directories for reflectable files
	RField();
	bool Recursive = false;

	/// If true, will not print out filenames of created files and artifacts
	RField();
	bool Quiet = false;

	/// Force recreation of all mirror files and artifacts, regardless of their timestamps and content
	RField();
	bool Force = false;

	/// Print out additional information
	RField();
	bool Verbose = false;

	/// JSON options
	RField();
	JSONOptions JSON = {};

	/// TODO: Whether to warn when a reflected attribute is not recognized by the program.
	/// This will have to wait until we have a full reflected list of attributes.
	RField();
	bool WarnOnUnknownAttributes = false;

	/// Whether or not to create optional files in the target directory; in particular: `Includes.reflect.h`, `Classes.reflect.h`, `ReflectorClasses.h` and `ReflectorUtils.h`.
	/// `ReflectorClasses.h` is required for reflection to work, but you can include it from the reflector codebase yourself if you prefer.
	/// See documentation for more information on what is stored in each artifact file.
	RField();
	bool CreateArtifacts = true;

	/// Whether or not to create the `ReflectDatabase.json` database file with reflection data.
	RField();
	bool CreateDatabase = true;

	//bool GenerateLuaFunctionBindings = false; /// Maybe instead of Lua function bindings, since this options is loaded from a JSON, we can have a nice JSON structure that defines how to create scripting bindings?

	/// Whether to create `typeid(...)` expressions in the reflection data. Useful if you have RTTI turned off.
	RField();
	bool GenerateTypeIndices = true;

	/// Whether to output forward declarations of reflected classes
	RField();
	bool ForwardDeclare = true;

	/// Whether to create additional comments in the generated files (files that are not meant to be viewed), for ease of debugging
	RField();
	bool DebuggingComments = true;

	/// Documentation options
	RField();
	DocumentationOptions Documentation = {};

	/// Prefix for all the annotation macros, e.g. X => XClass, XBody, XField, XMethod, etc
	RField();
	std::string AnnotationPrefix = "R";

	/// Prefix for all the generated special macros, like *REFLECT*\_VISIT\_Class\_METHODS
	RField();
	std::string MacroPrefix = "REFLECT";

	/// The extension for generated mirror files (`bla.h` => `bla.h.mirror`)
	RField();
	std::string MirrorExtension = ".mirror";

	/// List of extensions for files to scan for reflectable types
	RField();
	std::vector<std::string> ExtensionsToScan = { ".h", ".hpp" };

	/// If true, will keep the history of all reflected entities (timestamps of when they were added/changed/removed),
	/// in an artifact file. If this is true, the generated documentation will provide the information as well.
	RField();
	bool KeepEntityHistory = false;

	/// Name of enum annotation macro. If not set, will be `AnnotationPrefix + "Enum"`.
	RField();
	std::string EnumAnnotationName;

	/// Name of enumerator annotation macro. If not set, will be `AnnotationPrefix + "Enumerator"`.
	RField();
	std::string EnumeratorAnnotationName;

	/// Name of class annotation macro. If not set, will be `AnnotationPrefix + "Class"`.
	RField();
	std::string ClassAnnotationName;

	/// Name of field annotation macro. If not set, will be `AnnotationPrefix + "Field"`.
	RField();
	std::string FieldAnnotationName;

	/// Name of method annotation macro. If not set, will be `AnnotationPrefix + "Method"`.
	RField();
	std::string MethodAnnotationName;

	/// Name of class body macro. If not set, will be `AnnotationPrefix + "Body"`.
	RField();
	std::string BodyAnnotationName;

	RField();
	std::string GetterPrefix = "Get";

	/// Both for regular setters as well as flag setters
	RField();
	std::string SetterPrefix = "Set";

	RField();
	std::string IsPrefix = "Is";

	RField();
	std::string IsNotPrefix = "IsNot";

	RField();
	std::string SetNotPrefix = "SetNot";

	RField();
	std::string UnsetPrefix = "Unset";

	RField();
	std::string TogglePrefix = "Toggle";

	RField();
	std::string ProxyMethodPrefix = "_proxy_";

	RField();
	std::string SingletonInstanceGetterName = "SingletonInstance";

	RField();
	std::string ProxyClassSuffix = "_Proxy";

	/// TODO: Will add `Opposite` attributes to every flag enumerator with the given values (e.g. if you set this to {'Alive': 'Dead'}, any flag enumerator called Alive will get an opposite called 'Dead')
	RField();
	std::map<std::string, std::string> DefaultFlagOpposites;

	/// For later
	std::map<std::string, json> DefaultClassAttributes;
	std::map<std::string, json> DefaultFieldAttributes;
	std::map<std::string, json> DefaultMethodAttributes;

	auto const& GetExePath() const { return mExePath; }
	auto const& GetOptionsFilePath() const { return mOptionsFilePath; }
	auto const& GetOptionsFile() const { return mOptionsFile; }
	auto const& GetPathsToScan() const { return mPathsToScan; }

private:

	path mExePath;
	path mOptionsFilePath;
	json mOptionsFile;
	std::vector<path> mPathsToScan;
};

NLOHMANN_JSON_NAMESPACE_BEGIN
template <Reflector::reflected_class SERIALIZABLE>
struct adl_serializer<SERIALIZABLE>
{
	static void to_json(json& j, const SERIALIZABLE& p)
	{
		SERIALIZABLE::ForEachField([&]<typename FIELD>(FIELD properties) {
			if constexpr (!FIELD::HasFlag(Reflector::FieldFlags::NoSave))
			{
				j[FIELD::Name] = (p).*(FIELD::Pointer);
			}
		});
	}

	static void from_json(const json& j, SERIALIZABLE& p)
	{
		SERIALIZABLE::ForEachField([&]<typename FIELD>(FIELD properties) {
			if constexpr (!FIELD::HasFlag(Reflector::FieldFlags::NoLoad))
			{
				auto it = j.find(FIELD::Name);
				if (it == j.end())
				{
					if constexpr (FIELD::HasFlag(Reflector::FieldFlags::Required))
					{
						throw std::runtime_error{ std::format("Missing field '{}'", FIELD::Name) };
					}
					else
					{
						; /// ignore
					}
				}
				else
				{
					FIELD::CopySetter(&p, it->get<typename FIELD::Type>());
				}
			}
		});
	}
};
template <Reflector::reflected_enum SERIALIZABLE>
struct adl_serializer<SERIALIZABLE>
{
	static void to_json(json& j, const SERIALIZABLE& p)
	{
		j = std::underlying_type_t<SERIALIZABLE>(p);
	}

	static void from_json(const json& j, SERIALIZABLE& p)
	{
		if (j.is_string()) p = GetEnumeratorFromName(p, j); else p = (SERIALIZABLE)(std::underlying_type_t<SERIALIZABLE>)j;
	}
};
NLOHMANN_JSON_NAMESPACE_END