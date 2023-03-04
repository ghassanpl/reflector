/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Parse.h"
#include "Attributes.h"
#include <ghassanpl/string_ops.h>
#include <mutex>
#include <future>
#include <thread>
#include <fstream>
using namespace std::string_literals;

uint64_t ChangeTime = 0;
std::vector<FileMirror> Mirrors;

json Declaration::ToJSON() const
{
	json result = json::object();
	if (!Attributes.empty())
		result["Attributes"] = Attributes;
	result["Name"] = Name;
	result["DeclarationLine"] = DeclarationLine;
	if (Access != AccessMode::Unspecified)
		result["Access"] = AMStrings[(int)Access];
	if (!Comments.empty())
		result["Comments"] = Comments;
	return result;
}

Enum const* FindEnum(string_view name)
{
	for (auto& mirror : Mirrors)
		for (auto& henum : mirror.Enums)
			if (henum.Name == name)
				return &henum;
	return nullptr;
}

void Field::CreateArtificialMethods(FileMirror& mirror, Class& klass)
{
	/// Create comments string
	auto field_comments = join(Comments, " ");
	if (field_comments.size())
		field_comments[0] = (char)ascii::tolower(field_comments[0]);
	else
		field_comments = std::format("the `{}` field of this object", DisplayName);

	/// Getters and Setters
	if (!Flags.is_set(Reflector::FieldFlags::NoGetter))
	{
		klass.AddArtificialMethod(Type + " const &", "Get" + DisplayName, "", "return " + Name + ";", { "Gets " + field_comments }, { Reflector::MethodFlags::Const }, DeclarationLine);
	}

	if (!Flags.is_set(Reflector::FieldFlags::NoSetter))
	{
		auto on_change = atFieldOnChange(Attributes, ""s);
		if (!on_change.empty())
			on_change = on_change + "(); ";
		klass.AddArtificialMethod("void", "Set" + DisplayName, Type + " const & value", Name + " = value; " + on_change, { "Sets " + field_comments }, {}, DeclarationLine);
	}

	auto flag_getters = atFieldFlagGetters(Attributes, ""s);
	auto flag_setters = atFieldFlags(Attributes, ""s);
	if (!flag_getters.empty() && !flag_setters.empty())
	{
		ReportError(mirror.SourceFilePath, DeclarationLine, "Only one of `FlagGetters' and `Flags' can be declared");
		return;
	}
	bool do_flags = !flag_getters.empty() || !flag_setters.empty();
	bool do_setters = !flag_setters.empty();
	auto& enum_name = do_setters ? flag_setters : flag_getters;

	if (do_flags)
	{
		auto henum = FindEnum(string_view{ enum_name });
		if (!henum)
		{
			ReportError(mirror.SourceFilePath, DeclarationLine, "Enum `{}' not reflected", enum_name);
			return;
		}

		klass.AdditionalBodyLines.push_back(std::format("static_assert(::std::is_integral_v<{}>, \"Type '{}' for field '{}' with Flags attribute must be integral\");", this->Type, henum->Name, this->Name));

		for (auto& enumerator : henum->Enumerators)
		{
			klass.AddArtificialMethod("bool", "Is" + enumerator.Name, "", std::format("return ({} & {}{{{}}}) != 0;", Name, Type, 1ULL << enumerator.Value),
				{ "Checks whether the `" + enumerator.Name + "` flag is set in " + field_comments }, Reflector::MethodFlags::Const, DeclarationLine);
		}

		if (do_setters)
		{
			for (auto& enumerator : henum->Enumerators)
			{
				klass.AddArtificialMethod("void", "Set" + enumerator.Name, "", std::format("{} |= {}{{{}}};", Name, Type, 1ULL << enumerator.Value),
					{ "Sets the `" + enumerator.Name + "` flag in " + field_comments }, {}, DeclarationLine);
			}
			for (auto& enumerator : henum->Enumerators)
			{
				klass.AddArtificialMethod("void", "Unset" + enumerator.Name, "", std::format("{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator.Value),
					{ "Clears the `" + enumerator.Name + "` flag in " + field_comments }, {}, DeclarationLine);
			}
			for (auto& enumerator : henum->Enumerators)
			{
				klass.AddArtificialMethod("void", "Toggle" + enumerator.Name, "", std::format("{} ^= {}{{{}}};", Name, Type, 1ULL << enumerator.Value),
					{ "Toggles the `" + enumerator.Name + "` flag in " + field_comments }, {}, DeclarationLine);
			}
		}
	}
}

json Field::ToJSON() const
{
	json result = Declaration::ToJSON();
	result["Type"] = Type;
	if (!InitializingExpression.empty())
		result["InitializingExpression"] = InitializingExpression;
	if (DisplayName != Name)
		result["DisplayName"] = DisplayName;
#define ADDFLAG(n) if (Flags.is_set(Reflector::FieldFlags::n)) result[#n] = true
	ADDFLAG(NoGetter);
	ADDFLAG(NoSetter);
	ADDFLAG(NoEdit);
#undef ADDFLAG
	return result;
}

void Method::Split()
{
	auto args = SplitArgs(string_view{ mParameters });
	std::vector<std::string> parameters_split;
	std::transform(args.begin(), args.end(), std::back_inserter(parameters_split), [](string_view param) { return std::string{ param }; });

	ParametersSplit.clear();
	for (auto& full_param : parameters_split)
	{
		auto& param = ParametersSplit.emplace_back();
		auto start_of_id = std::find_if(full_param.rbegin(), full_param.rend(), std::not_fn(ascii::isident)).base();
		param.Type = TrimWhitespace({std::to_address(full_param.begin()), std::to_address(start_of_id) });
		param.Name = TrimWhitespace({ std::to_address(start_of_id), std::to_address(full_param.end()) });

		if (param.Type.empty()) /// If we didn't specify a name, type was at the end, not name, so fix that
			param.Type = param.Type + ' ' + param.Name;
	}

	ParametersTypesOnly = join(ParametersSplit, ",", [](MethodParameter const& param) { return param.Type; });
	ParametersNamesOnly = join(ParametersSplit, ",", [](MethodParameter const& param) { return param.Name; });
}

void Method::SetParameters(std::string params)
{
	mParameters = std::move(params);
	if (mParameters.find('=') != std::string::npos)
	{
		throw std::exception{ "Default parameters not supported" };
	}
	Split();
}

std::string Method::GetSignature(Class const& parent_class) const
{
	auto base = Flags.is_set(MethodFlags::Static) ? 
		std::format("{} (*)({})", Type, ParametersTypesOnly)
	:
		std::format("{} ({}::*)({})", Type, parent_class.Name, ParametersTypesOnly);
	if (Flags.is_set(Reflector::MethodFlags::Const))
		base += " const";
	if (Flags.is_set(Reflector::MethodFlags::Noexcept))
		base += " noexcept";
	return base;
}

void Method::CreateArtificialMethods(FileMirror& mirror, Class& klass)
{
	if (klass.Flags.is_set(ClassFlags::HasProxy) && Flags.is_set(MethodFlags::Virtual))
	{
		if (Flags.is_set(MethodFlags::Abstract))
			klass.AddArtificialMethod(Type, "_PROXY_"+Name, GetParameters(), std::format("throw std::runtime_error{{\"invalid abstract call to function {}::{}\"}};", klass.Name, Name), { "Proxy function for " + Name }, Flags - MethodFlags::Virtual, DeclarationLine);
		else
			klass.AddArtificialMethod(Type, "_PROXY_" + Name, GetParameters(), "return self_type::" + Name + "(" + ParametersNamesOnly + ");", { "Proxy function for " + Name }, Flags - MethodFlags::Virtual, DeclarationLine);
	}
}

json Method::ToJSON() const
{
	json result = Declaration::ToJSON();
	result["Type"] = Type;
	std::transform(ParametersSplit.begin(), ParametersSplit.end(), std::back_inserter(result["Parameters"]), std::function<json(MethodParameter const&)>{ &MethodParameter::ToJSON });
	if (!Body.empty())
		result["Body"] = Body;
	if (SourceFieldDeclarationLine != 0)
		result["SourceFieldDeclarationLine"] = SourceFieldDeclarationLine;
#define ADDFLAG(n) if (Flags.is_set(Reflector::MethodFlags::n)) result[#n] = true
	ADDFLAG(Inline);
	ADDFLAG(Virtual);
	ADDFLAG(Static);
	ADDFLAG(Const);
	ADDFLAG(Noexcept);
	ADDFLAG(Final);
	ADDFLAG(Explicit);
	ADDFLAG(Artificial);
	ADDFLAG(HasBody);
	ADDFLAG(NoCallable);
#undef ADDFLAG
	return result;
}

void Property::CreateArtificialMethods(FileMirror& mirror, Class& klass)
{
}

void Class::AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags, size_t source_field_declaration_line)
{
	Method method;
	method.Flags += Reflector::MethodFlags::Artificial;
	method.Flags += additional_flags;
	method.Type = std::move(results);
	method.Name = std::move(name);
	method.SetParameters(std::move(parameters));
	method.Body = std::move(body);
	if (!method.Body.empty())
		method.Flags += Reflector::MethodFlags::HasBody;
	method.DeclarationLine = 0;
	method.Access = AccessMode::Public;
	method.Comments = std::move(comments);
	method.SourceFieldDeclarationLine = source_field_declaration_line;
	mArtificialMethods.push_back(std::move(method));
}

void Class::CreateArtificialMethods(FileMirror& mirror)
{
	/// Check if we should build proxy
	bool should_build_proxy = false;

	for (auto& method : Methods)
	{
		if (method.Flags.is_set(Reflector::MethodFlags::Virtual) && !method.Flags.is_set(Reflector::MethodFlags::Final))
			should_build_proxy = true;
	}

	should_build_proxy = should_build_proxy && atRecordCreateProxy(Attributes, true);

	Flags.set_to(should_build_proxy, ClassFlags::HasProxy);

	/// Create singleton method if singleton
	if (atRecordSingleton(Attributes, false))
		AddArtificialMethod("self_type&", "SingletonInstance", "", "static self_type instance; return instance;", { "Returns the single instance of this class" }, Reflector::MethodFlags::Static);

	/// Create methods for fields and methods

	for (auto& field : Fields)
		field.CreateArtificialMethods(mirror, *this);
	for (auto& method : Methods)
		method.CreateArtificialMethods(mirror, *this);
	for (auto& property : Properties)
		property.second.CreateArtificialMethods(mirror, *this);

	for (auto& am : mArtificialMethods)
	{
		am.UID = GenerateUID(mirror.SourceFilePath, am.ActualDeclarationLine());
		Methods.push_back(std::move(am));
	}
	mArtificialMethods.clear();

	/// First check unique method names
	MethodsByName.clear();

	for (auto& method : Methods)
	{
		MethodsByName[method.Name].push_back(&method);
		if (!method.UniqueName.empty() && method.Name != method.UniqueName)
			MethodsByName[method.UniqueName].push_back(&method);
	}
	
	for (auto& method : Methods)
	{
		if (method.UniqueName.empty())
			continue;
		if (MethodsByName[method.UniqueName].size() > 1)
		{
			std::string message;
			message += std::format("{}({},0): method with unique name not unique", mirror.SourceFilePath.string(), method.DeclarationLine + 1);
			for (auto& conflicting_method : MethodsByName[method.UniqueName])
			{
				if (conflicting_method != &method)
					message += std::format("\n{}({},0):   conflicts with this declaration", mirror.SourceFilePath.string(), conflicting_method->DeclarationLine + 1);
			}
			throw std::exception{ message.c_str() };
		}
	}
}

json Class::ToJSON() const
{
	auto result = Declaration::ToJSON();
	if (!ParentClass.empty())
		result["ParentClass"] = ParentClass;

	if (Flags.is_set(ClassFlags::Struct))
		result["Struct"] = true;
	if (Flags.is_set(ClassFlags::NoConstructors))
		result["NoConstructors"] = true;

	if (!Fields.empty())
	{
		auto& fields = result["Fields"] = json::object();
		for (auto& field : Fields)
		{
			fields[field.Name] = field.ToJSON();
		}
	}
	if (!Methods.empty())
	{
		auto& methods = result["Methods"] = json::array();
		for (auto& method : Methods)
		{
			methods.push_back(method.ToJSON());
		}
	}

	result["BodyLine"] = BodyLine;

	return result;
}

json Enumerator::ToJSON() const
{
	json result = Declaration::ToJSON();
	result["Value"] = Value;
	return result;
}

json Enum::ToJSON() const
{
	json result = Declaration::ToJSON();
	auto& enumerators = result["Enumerators"] = json::object();
	for (auto& enumerator : Enumerators)
		enumerators[enumerator.Name] = enumerator.ToJSON();
	return result;
}

json FileMirror::ToJSON() const
{
	json result = json::object();
	result["SourceFilePath"] = SourceFilePath.string();
	auto& classes = result["Classes"] = json::object();
	for (auto& klass : Classes)
		classes[klass.Name] = klass.ToJSON();
	auto& enums = result["Enums"] = json::object();
	for (auto& enum_ : Enums)
		enums[enum_.Name] = enum_.ToJSON();
	return result;
}

void FileMirror::CreateArtificialMethods()
{
	for (auto& klass : Classes)
	{
		klass.CreateArtificialMethods(*this);
	}
}

#define OPTION(name, default_value, description) name = OptionsFile.value(#name, default_value); OptionsFile.erase(#name);

Options::Options(path exe_path, path const& options_file_path)
	: ExePath(std::move(exe_path))
	, OptionsFilePath(std::filesystem::canonical(options_file_path))
	, OptionsFile(json::parse(std::fstream{ options_file_path }))
{
	if (!OptionsFile.is_object())
		throw std::exception{ "Options file must contain a JSON object" };

	OPTION(Recursive, false, "Recursively search the provided directories for files");
	OPTION(Quiet, false, "Don't print out created file names");
	OPTION(Force, false, "Ignore timestamps, regenerate all files");
	OPTION(Verbose, false, "Print additional information");
	OPTION(CreateDatabase, true, "Create a JSON database with reflection data");
	OPTION(UseJSON, true, "Output code that uses json to store class attributes");
	OPTION(ForwardDeclare, true, "Output forward declarations of reflected classes");
	OPTION(CreateArtifacts, true, "Whether to generate artifacts (*.reflect.h files, db, others)");
	OPTION(AnnotationPrefix, "R", "The prefix for all annotation macros");
	OPTION(MacroPrefix, "REFLECT", "The prefix for all autogenerated macros this tool will generate");

	OPTION(ArtifactPath, "", "Path to the directory where the general artifact files will be created");

	if (!OptionsFile.contains("Files"))
		throw std::exception{ "Options file missing `Files' entry" };

	if (OptionsFile["Files"].is_array())
	{
		for (auto& file : OptionsFile["Files"])
		{
			if (!file.is_string())
				throw std::exception{ "`Files' array must contain only strings" };
			PathsToScan.push_back((std::string)file);
		}
	}
	else if (OptionsFile["Files"].is_string())
		PathsToScan.push_back((std::string)OptionsFile["Files"]);
	else
		throw std::exception{ "`Files' entry must be an array of strings or a string" };

	OptionsFile.erase("Files");
	
	/// Hidden options :)
	OPTION(EnumPrefix, AnnotationPrefix + "Enum", "");
	OPTION(EnumeratorPrefix, AnnotationPrefix + "Enumerator", "");
	OPTION(ClassPrefix, AnnotationPrefix + "Class", "");
	OPTION(FieldPrefix, AnnotationPrefix + "Field", "");
	OPTION(MethodPrefix, AnnotationPrefix + "Method", "");
	OPTION(BodyPrefix, AnnotationPrefix + "Body", "");

	if (OptionsFile.size() > 0 && Verbose)
	{
		for (auto& opt : OptionsFile.items())
		{
			PrintLine("Warning: Unrecognized option: {}\n", opt.key());
		}
	}
}

void PrintSafe(std::ostream& strm, std::string val)
{
	static std::mutex print_mutex;
	std::unique_lock locl{ print_mutex };
	strm << val;
}

std::vector<FileMirror> const& GetMirrors()
{
	return Mirrors;
}

void AddMirror(FileMirror mirror)
{
	static std::mutex mirror_mutex;
	std::unique_lock lock{ mirror_mutex };
	Mirrors.push_back(std::move(mirror));
}

void CreateArtificialMethods()
{
	/// TODO: Not sure if these are safe to be multithreaded, we ARE adding new methods to the mirrors after all...
	std::vector<std::future<void>> futures;
	for (auto& mirror : Mirrors)
		futures.push_back(std::async([&]() { mirror.CreateArtificialMethods(); }));
	
	for (auto& future : futures)
		future.get(); /// to propagate exceptions
}
