#pragma once

#include "Common.h"
#include "ReflectorUtils.h"
#include "ReflectorGC.h"

#if __has_include("Options.h.mirror")
inline constexpr bool BootstrapBuild = false;
#include "Options.h.mirror"
#else
inline constexpr bool BootstrapBuild = true;
#include "DummyReflector.h"
#endif

/// NOTE: Changing this file requires a bootstrap rebuild!

/// TODO: We probably should drop support for anything except nlohmann, or create our own interface to handle json values
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

	/// When converting object fields to JSON fields, the serializer will check if the field is
	/// equal to its initializer, skipping serializing that field if it is equal.
	/// Set AlwaysSaveAllFields to true to always save all object fields to JSON.
	RField();
	bool AlwaysSaveAllFields = false;

	/// Toggles generation of JSON serialization methods for reflected classes
	RField();
	bool GenerateSerializationMethods = true;
};

RClass(DefaultFieldAttributes = { Setter = false, Getter = false });
struct DocumentationOptions
{
	RBody();

	/// Whether or not to generate documentation
	RField();
	bool Generate = true;

	/// TODO: A structure that somehow turns filenames into github (or other) links

	/// Will be added to the end of page titles
	RField();
	std::string PageTitleSuffix = " - Documentation";

	/// Will be used in addition to and instead of the default styles
	RField();
	json AdditionalStyles = json::object();
	
	/// Will be added directly to the <head> tag of each page
	RField();
	std::string AdditionalHeadTags {};

	/// language tag (e.g. "en") to add to the <html> tag to define the language of the page
	RField();
	std::string Language = "en";

	/// The directory to put documentation into, (relative to options.json file)?
	RField();
	path TargetDirectory = "Documentation";

	/// Whether to show the initial (construct-time) values of class fields in the class page
	RField();
	bool ShowFieldInitialValues = true;

	/// TODO: Show headers of tables listing members
	RField();
	bool TableHeaders = false;

	/// Remove "std::" prefix from types
	RField();
	bool RemoveStdNamespace = true;

	/// TODO: Special formatting for certain types, e.g "string" -> "&lt;b>string&lt;/b>"
	RField();
	std::map<std::string, std::string> AdditionalTypeFormatting{};

	/// TODO: Types in signatures to replace, e.g "ImageResolvable" -> "Resolvable&lt;Image>", or smth
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
struct ArtifactOptions
{
	RBody();
};

RClass(DefaultFieldAttributes = { Setter = false, Getter = false });
struct NameOptions
{
	RBody();
};

RClass(DefaultFieldAttributes = { Setter = false, Getter = false });
struct ScriptBindingOptions
{
	RBody();
};

RClass(DefaultFieldAttributes = { Setter = false, Getter = false });
struct Options
{
	RBody();

	Options() = default;
	Options(path exe_path, path const& options_file_path);

	/// **Required** A filename or list of filenames (or directories) to scan for reflectable types
	RField(Required);
	json Files;

	/// List of extensions for files to scan for reflectable types
	RField();
	std::vector<std::string> ExtensionsToScan = { ".h", ".hpp" };

	/// TODO: File/dir exclusions

	/// Path to the directory where the general artifact files will be created (relative to the directory with options file)
	RField();
	path ArtifactPath = "Reflection/";

	/// The extension for generated mirror files (`bla.h` => `bla.h.mirror`)
	RField();
	std::string MirrorExtension = ".mirror";

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

	/// TODO: Whether to warn when a reflected attribute is not recognized by the program.
	/// This will have to wait until we have a full reflected list of attributes.
	RField();
	bool WarnOnUnknownAttributes = false;

	/// TODO: Will add `Opposite` attributes to every flag enumerator with the given values (e.g. if you set this to {'Alive': 'Dead'}, any flag enumerator called Alive will get an opposite called 'Dead')
	RField();
	std::map<std::string, std::string> DefaultFlagOpposites;

	/// For later
	std::map<std::string, json> DefaultClassAttributes;
	std::map<std::string, json> DefaultFieldAttributes;
	std::map<std::string, json> DefaultMethodAttributes;

	/// Whether or not to create the `ReflectDatabase.json` database file with reflection data.
	RField();
	bool CreateDatabase = false;

	/// Whether to create Set* and Get* methods for public fields.
	RField();
	bool GenerateAccessorsForPublicFields = true;

	//bool GenerateLuaFunctionBindings = false; /// Maybe instead of Lua function bindings, since this options is loaded from a JSON, we can have a nice JSON structure that defines how to create scripting bindings?

	/// Whether to add support for garbage-collected heaps for reflected classes.
	RField();
	bool AddGCFunctionality = true;

	/// Whether to output forward declarations of reflected classes
	RField();
	bool ForwardDeclare = true;

	/// Whether to create additional comments in the generated files (files that are not meant to be viewed), for ease of debugging
	RField();
	bool DebuggingComments = true;

	/// The default namespace for all reflected types.
	/// TODO: Can be modified by the file-level `RNamespace(...);` annotation, or by the entity-level `Namespace` attribute
	RField();
	std::string DefaultNamespace = {};

	/// TODO: If true, will keep the history of all reflected entities (timestamps of when they were added/changed/removed),
	/// in an artifact file. If this is true, the generated documentation will provide the information as well.
	RField();
	bool KeepEntityHistory = false;

	/// Documentation options
	RField();
	DocumentationOptions Documentation = {};

	/// JSON options
	RField();
	JSONOptions JSON = {};

	/// TODO: Move affixes and macro names to a different struct

	/// Prefix for all the annotation macros, e.g. X => XClass, XBody, XField, XMethod, etc
	RField();
	std::string AnnotationPrefix = "R";

	/// Prefix for all the generated special macros, like *REFLECT*\_VISIT\_Class\_METHODS
	RField();
	std::string MacroPrefix = "REFLECT";

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

	/// TODO: Name of namespace annotation macro. If not set, will be `AnnotationPrefix + "Namespace"`.
	RField();
	std::string NamespaceAnnotationName;

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
