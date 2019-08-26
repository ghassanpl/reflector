/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include <baselib/ASCII.h>
#include <mutex>
#include <future>
#include <thread>

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
	auto field_comments = baselib::Join(Comments, " ");
	if (field_comments.size())
		field_comments[0] = (char)baselib::tolower(field_comments[0]);
	else
		field_comments = baselib::Stringify("the `", DisplayName, "` field of this object");

	/// Getters and Setters
	if (!Flags.IsSet(Reflector::FieldFlags::NoGetter))
	{
		klass.AddArtificialMethod(Type + " const &", "Get" + DisplayName, "", "return " + Name + ";", { "Gets " + field_comments }, { MethodFlags::Const }, DeclarationLine);
	}

	if (!Flags.IsSet(Reflector::FieldFlags::NoSetter))
	{
		auto on_change = Attributes.value("OnChange", "");
		if (!on_change.empty())
			on_change = on_change + "(); ";
		klass.AddArtificialMethod("void", "Set" + DisplayName, Type + " const & value", Name + " = value; " + on_change, { "Sets " + field_comments }, {}, DeclarationLine);
	}

	auto flag_getters = Attributes.value("FlagGetters", "");
	auto flag_setters = Attributes.value("Flags", "");
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
			ReportError(mirror.SourceFilePath, DeclarationLine, "Enum `", enum_name, "' not reflected");
			return;
		}

		for (auto& enumerator : henum->Enumerators)
		{
			klass.AddArtificialMethod("bool", "Is" + enumerator.Name, "", baselib::Stringify("return (", Name, " & ", Type, "{", 1ULL << enumerator.Value, "}) != 0;"),
				{ "Checks whether the `" + enumerator.Name + "` flag is set in " + field_comments }, MethodFlags::Const, DeclarationLine);
		}

		if (do_setters)
		{
			for (auto& enumerator : henum->Enumerators)
			{
				klass.AddArtificialMethod("void", "Set" + enumerator.Name, "", baselib::Stringify(Name, " |= ", Type, "{", 1ULL << enumerator.Value, "};"),
					{ "Sets the `" + enumerator.Name + "` flag in " + field_comments }, {}, DeclarationLine);
			}
			for (auto& enumerator : henum->Enumerators)
			{
				klass.AddArtificialMethod("void", "Unset" + enumerator.Name, "", baselib::Stringify(Name, " &= ~", Type, "{", 1ULL << enumerator.Value, "};"),
					{ "Clears the `" + enumerator.Name + "` flag in " + field_comments }, {}, DeclarationLine);
			}
			for (auto& enumerator : henum->Enumerators)
			{
				klass.AddArtificialMethod("void", "Toggle" + enumerator.Name, "", baselib::Stringify(Name, " ^= ", Type, "{", 1ULL << enumerator.Value, "};"),
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
#define ADDFLAG(n) if (Flags.IsSet(Reflector::FieldFlags::n)) result[#n] = true
	ADDFLAG(NoGetter);
	ADDFLAG(NoSetter);
	ADDFLAG(NoEdit);
#undef ADDFLAG
	return result;
}

std::string Method::GetSignature(Class const& parent_class) const
{
	return baselib::Stringify(Type, " (", parent_class.Name, "::*)(", Parameters, ")");
}

void Method::CreateArtificialMethods(FileMirror& mirror, Class& klass)
{
}

json Method::ToJSON() const
{
	json result = Declaration::ToJSON();
	result["Type"] = Type;
	result["Parameters"] = Parameters;
	if (!Body.empty())
		result["Body"] = Body;
	if (SourceFieldDeclarationLine != 0)
		result["SourceFieldDeclarationLine"] = SourceFieldDeclarationLine;
#define ADDFLAG(n) if (Flags.IsSet(MethodFlags::n)) result[#n] = true
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

void Class::AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, baselib::EnumFlags<MethodFlags> additional_flags, size_t source_field_declaration_line)
{
	Method method;
	method.Flags += MethodFlags::Artificial;
	method.Flags += additional_flags;
	method.Type = std::move(results);
	method.Name = std::move(name);
	method.Parameters = std::move(parameters);
	method.Body = std::move(body);
	if (!method.Body.empty())
		method.Flags += MethodFlags::HasBody;
	method.DeclarationLine = 0;
	method.Access = AccessMode::Public;
	method.Comments = std::move(comments);
	method.SourceFieldDeclarationLine = source_field_declaration_line;
	Methods.push_back(std::move(method));
}

void Class::CreateArtificialMethods(FileMirror& mirror)
{
	for (auto& field : Fields)
		field.CreateArtificialMethods(mirror, *this);
	for (auto& method : Methods)
		method.CreateArtificialMethods(mirror, *this);
	for (auto& property : Properties)
		property.second.CreateArtificialMethods(mirror, *this);

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
			message += baselib::Stringify(mirror.SourceFilePath.string(), "(", method.DeclarationLine + 1, ",0): method with unique name not unique");
			for (auto& conflicting_method : MethodsByName[method.UniqueName])
			{
				if (conflicting_method != &method)
					message += baselib::Stringify("\n", mirror.SourceFilePath.string(), "(", conflicting_method->DeclarationLine + 1, ",0):   conflicts with this declaration");
			}
			throw std::exception{ message.c_str() };
		}
	}

	/// Check for same-name methods
	for (auto& method_names : MethodsByName)
	{
		if (method_names.second.size() > 1)
		{
			/// We make sure that no methods with this name have default arguments,
			/// as we cannot differentiate between them in the visitors, as we need to cast them to their appropriate types, and
			/// we cannot create a valid signature for a function with default arguments (stupid undecidable C++ syntax)
			for (auto& other_method : method_names.second)
			{
				if (other_method->Parameters.find('=') != std::string::npos)
					throw std::exception{ baselib::Stringify(mirror.SourceFilePath.string(), "(", other_method->DeclarationLine + 1, ",0): limitation: methods that have overloads cannot have default arguments").c_str() };
			}
		}
	}
}

json Class::ToJSON() const
{
	auto result = Declaration::ToJSON();
	if (!ParentClass.empty())
		result["ParentClass"] = ParentClass;

	if (Flags.IsSet(ClassFlags::Struct))
		result["Struct"] = true;
	if (Flags.IsSet(ClassFlags::NoConstructors))
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
		auto& methods = result["Methods"] = json::object();
		for (auto& method : Methods)
		{
			methods[method.Name] = method.ToJSON();
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

Options::Options(bool recursive, bool quiet, bool force, bool verbose, bool use_json, std::string_view annotation_prefix, std::string_view macro_prefix)
	: Recursive(recursive), Quiet(quiet), Force(force), Verbose(verbose), UseJSON(use_json), AnnotationPrefix(annotation_prefix), MacroPrefix(macro_prefix)
{
	EnumPrefix = AnnotationPrefix + "Enum";
	EnumeratorPrefix = AnnotationPrefix + "Enumerator";
	ClassPrefix = AnnotationPrefix + "Class";
	FieldPrefix = AnnotationPrefix + "Field";
	MethodPrefix = AnnotationPrefix + "Method";
	BodyPrefix = AnnotationPrefix + "Body";
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
