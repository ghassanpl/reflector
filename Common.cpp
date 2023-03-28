/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Parse.h"
#include "Attributes.h"
#include "Options.h"
#include <ghassanpl/string_ops.h>
#include <mutex>
#include <future>
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
	if (!Namespace.empty())
	result["Namespace"] = Namespace;
	if (DeclarationLine != 0)
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
			if (henum->Name == name || henum->FullType() == name)
				return henum.get();
	return nullptr;
}

void Field::AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	auto method = ParentClass->AddArtificialMethod(move(results), move(name), move(parameters), move(body), move(comments), additional_flags);
	this->AssociatedArtificialMethods.push_back(method);
	method->SourceDeclaration = this;
}

void Field::CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options)
{
	/// Create comments string
	/// TODO: We used to create the comments for accessors from the comments of the field.
	/// Now we don't but private fields don't show up in documentation, so giving the name of the field that isn't available is confusing...
	auto field_comments = std::format("the **{}** field of this object", DisplayName);

	/// Getters and Setters
	if (!Flags.is_set(Reflector::FieldFlags::NoGetter))
	{
		AddArtificialMethod(Type + " const &", options.GetterPrefix + CleanName, "", "return this->" + Name + ";", { "Gets " + field_comments }, { Reflector::MethodFlags::Const });
	}

	if (!Flags.is_set(Reflector::FieldFlags::NoSetter))
	{
		auto on_change = atFieldOnChange(Attributes, ""s);
		if (!on_change.empty())
			on_change = on_change + "(); ";
		AddArtificialMethod("void", options.SetterPrefix + CleanName, Type + " const & value", "this->" + Name + " = value; " + on_change, {"Sets " + field_comments}, {});
	}

	auto flag_getters = atFieldFlagGetters(Attributes, ""s);
	auto flag_setters = atFieldFlags(Attributes, ""s);
	if (!flag_getters.empty() && !flag_setters.empty())
	{
		ReportError(mirror.SourceFilePath, DeclarationLine, "Only one of `FlagGetters' and `Flags' can be declared");
		return;
	}
	const bool do_flags = !flag_getters.empty() || !flag_setters.empty();
	const bool do_setters = !flag_setters.empty();
	auto& enum_name = do_setters ? flag_setters : flag_getters;

	if (do_flags)
	{
		constexpr auto enum_getter_flags = enum_flags{ Reflector::MethodFlags::Const, Reflector::MethodFlags::Inline, Reflector::MethodFlags::Noexcept };
		constexpr auto enum_setter_flags = enum_getter_flags - Reflector::MethodFlags::Const;
		auto henum = FindEnum(string_view{ enum_name });
		if (!henum)
		{
			ReportError(mirror.SourceFilePath, DeclarationLine, "Enum `{}' not reflected", enum_name);
			return;
		}

		klass.AdditionalBodyLines.push_back(std::format("static_assert(::std::is_integral_v<{}>, \"Type '{}' for field '{}' with Flags attribute must be integral\");", this->Type, henum->FullType(), this->Name));

		const auto flag_nots = atFieldFlagNots(Attributes, true);

		for (auto& enumerator : henum->Enumerators)
		{
			AddArtificialMethod("bool", options.IsPrefix + enumerator->Name, "", std::format("return (this->{} & {}{{{}}}) != 0;", Name, Type, 1ULL << enumerator->Value),
				{ "Checks whether the `" + enumerator->Name + "` flag is set in " + DisplayName }, enum_getter_flags);

			if (auto opposite = atEnumeratorOpposite(enumerator->Attributes, ""s); !opposite.empty())
			{
				AddArtificialMethod("bool", options.IsPrefix + opposite, "", std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the `" + enumerator->Name + "` flag is NOT set in " + DisplayName }, enum_getter_flags);
			}
			else if (flag_nots)
			{
				AddArtificialMethod("bool", options.IsNotPrefix + enumerator->Name, "", std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the `" + enumerator->Name + "` flag is set in " + DisplayName }, enum_getter_flags);
			}
		}

		if (do_setters)
		{
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod("void", options.SetterPrefix + enumerator->Name, "", std::format("this->{} |= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Sets the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				if (auto opposite = atEnumeratorOpposite(enumerator->Attributes, ""s); !opposite.empty())
				{
					AddArtificialMethod("void", options.SetterPrefix + opposite, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Clears the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				}
				else if (flag_nots)
				{
					AddArtificialMethod("void", options.SetNotPrefix + enumerator->Name, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Clears the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);
				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod("void", options.UnsetPrefix + enumerator->Name, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Clears the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);


				if (auto opposite = atEnumeratorOpposite(enumerator->Attributes, ""s); !opposite.empty())
				{
					AddArtificialMethod("void", options.UnsetPrefix + opposite, "", std::format("this->{} |= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Sets the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod("void", options.TogglePrefix + enumerator->Name, "", std::format("this->{} ^= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Toggles the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				if (auto opposite = atEnumeratorOpposite(enumerator->Attributes, ""s); !opposite.empty())
				{
					AddArtificialMethod("void", options.TogglePrefix + opposite, "", std::format("this->{} ^= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Toggles the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);
				}
			}
		}
	}
}

std::string Field::FullName(std::string_view sep) const
{
	return std::format("{}{}{}", ParentClass->FullName(sep), sep, Declaration::FullName(sep));
}

json Field::ToJSON() const
{
	json result = Declaration::ToJSON();
	result["Type"] = Type;
	if (!InitializingExpression.empty())
		result["InitializingExpression"] = InitializingExpression;
	if (DisplayName != Name)
		result["DisplayName"] = DisplayName;
	if (CleanName != Name)
		result["CleanName"] = CleanName;
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

Method::Method(Class* parent) : ParentClass(parent) { SourceDeclaration = parent; }

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
		std::format("{} (*)({})", ReturnType, ParametersTypesOnly)
	:
		std::format("{} ({}::*)({})", ReturnType, parent_class.FullType(), ParametersTypesOnly);
	if (Flags.is_set(Reflector::MethodFlags::Const))
		base += " const";
	if (Flags.is_set(Reflector::MethodFlags::Noexcept))
		base += " noexcept";
	return base;
}

void Method::AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	auto method = ParentClass->AddArtificialMethod(move(results), move(name), move(parameters), move(body), move(comments), additional_flags);
	this->AssociatedArtificialMethods.push_back(method);
	method->SourceDeclaration = this;
}

void Method::CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options)
{
	if (klass.Flags.is_set(ClassFlags::HasProxy) && Flags.is_set(MethodFlags::Virtual))
	{
		auto flags = (Flags - MethodFlags::Virtual) + MethodFlags::Proxy;
		if (Flags.is_set(MethodFlags::Abstract))
			AddArtificialMethod(ReturnType, options.ProxyMethodPrefix + Name, GetParameters(), std::format("throw std::runtime_error{{\"invalid abstract call to function {}::{}\"}};", klass.FullType(), Name), {"Proxy function for " + Name}, flags);
		else
			AddArtificialMethod(ReturnType, options.ProxyMethodPrefix + Name, GetParameters(), "return self_type::" + Name + "(" + ParametersNamesOnly + ");", { "Proxy function for " + Name }, flags);
	}
}

std::string Method::FullName(std::string_view sep) const
{
	return std::format("{}{}{}", ParentClass->FullName(sep), sep, GeneratedUniqueName());
}

json Method::ToJSON() const
{
	json result = Declaration::ToJSON();
	result["ReturnType"] = ReturnType;
	if (ParametersSplit.size() > 0)
		std::transform(ParametersSplit.begin(), ParametersSplit.end(), std::back_inserter(result["Parameters"]), &::ToJSON);
	if (!ArtificialBody.empty())
		result["ArtificialBody"] = ArtificialBody;
	if (SourceDeclaration->DeclarationLine != 0)
		result["SourceFieldDeclarationLine"] = SourceDeclaration->DeclarationLine;
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

void Property::CreateArtificialMethods(FileMirror& mirror, Class& klass, Options const& options)
{
}

Method* Class::AddArtificialMethod(std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	Method& method = *mArtificialMethods.emplace_back(std::make_unique<Method>(this));
	method.SourceDeclaration = this; /// The class is the default source of the artificial method
	method.Flags += Reflector::MethodFlags::Artificial;
	method.Flags += additional_flags;
	method.ReturnType = std::move(results);
	method.Name = std::move(name);
	method.SetParameters(std::move(parameters));
	method.ArtificialBody = std::move(body);
	if (!method.ArtificialBody.empty())
		method.Flags += Reflector::MethodFlags::HasBody;
	method.DeclarationLine = 0;
	method.Access = AccessMode::Public;
	method.Comments = std::move(comments);
	//method.SourceFieldDeclarationLine = source_field_declaration_line;
	return &method;
}

void Class::CreateArtificialMethods(FileMirror& mirror, Options const& options)
{
	/// Check if we should build proxy
	bool should_build_proxy = false;

	for (auto& method : Methods)
	{
		if (method->Flags.is_set(Reflector::MethodFlags::Virtual) && !method->Flags.is_set(Reflector::MethodFlags::Final))
			should_build_proxy = true;
	}

	should_build_proxy = should_build_proxy && atRecordCreateProxy(Attributes, true);

	Flags.set_to(should_build_proxy, ClassFlags::HasProxy);

	/// Create singleton method if singleton
	if (atRecordSingleton(Attributes, false))
		AddArtificialMethod("self_type&", options.SingletonInstanceGetterName, "", "static self_type instance; return instance;", { "Returns the single instance of this class" }, Reflector::MethodFlags::Static);

	/// Create methods for fields and methods

	for (auto& field : Fields)
		field->CreateArtificialMethods(mirror, *this, options);
	for (auto& method : Methods)
		method->CreateArtificialMethods(mirror, *this, options);
	for (auto& property : Properties)
		property.second.CreateArtificialMethods(mirror, *this, options);

	for (auto& am : mArtificialMethods)
	{
		am->UID = GenerateUID(mirror.SourceFilePath, am->ActualDeclarationLine());
		Methods.push_back(std::exchange(am, {}));
	}
	mArtificialMethods.clear();

	/// First check unique method names
	MethodsByName.clear();

	for (auto& method : Methods)
	{
		MethodsByName[method->Name].push_back(method.get());
		if (!method->UniqueName.empty() && method->Name != method->UniqueName)
			MethodsByName[method->UniqueName].push_back(method.get());
	}
	
	for (auto& method : Methods)
	{
		if (method->UniqueName.empty())
			continue;
		if (MethodsByName[method->UniqueName].size() > 1)
		{
			std::string message;
			message += std::format("{}({},0): method with unique name not unique", mirror.SourceFilePath.string(), method->DeclarationLine + 1);
			for (auto& conflicting_method : MethodsByName[method->UniqueName])
			{
				if (conflicting_method != method.get())
					message += std::format("\n{}({},0):   conflicts with this declaration", mirror.SourceFilePath.string(), conflicting_method->DeclarationLine + 1);
			}
			throw std::exception{ message.c_str() };
		}
	}
}

json Class::ToJSON() const
{
	auto result = Declaration::ToJSON();
	if (!BaseClass.empty())
		result["BaseClass"] = BaseClass;

	if (Flags.is_set(ClassFlags::Struct))
		result["Struct"] = true;
	if (Flags.is_set(ClassFlags::NoConstructors))
		result["NoConstructors"] = true;

	if (!Fields.empty())
	{
		auto& fields = result["Fields"] = json::object();
		for (auto& field : Fields)
		{
			fields[field->Name] = field->ToJSON();
		}
	}
	if (!Methods.empty())
	{
		auto& methods = result["Methods"] = json::array();
		for (auto& method : Methods)
		{
			methods.push_back(method->ToJSON());
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
		enumerators[enumerator->Name] = enumerator->ToJSON();
	return result;
}

json FileMirror::ToJSON() const
{
	json result = json::object();
	result["SourceFilePath"] = SourceFilePath.string();
	auto& classes = result["Classes"] = json::object();
	for (auto& klass : Classes)
		classes[klass->FullName()] = klass->ToJSON();
	auto& enums = result["Enums"] = json::object();
	for (auto& enum_ : Enums)
		enums[enum_->FullName()] = enum_->ToJSON();
	return result;
}

void FileMirror::CreateArtificialMethods(Options const& options)
{
	for (auto& klass : Classes)
	{
		klass->CreateArtificialMethods(*this, options);
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

void CreateArtificialMethods(Options const& options)
{
	/// TODO: Not sure if these are safe to be multithreaded, we ARE adding new methods to the mirrors after all...
	std::vector<std::future<void>> futures;
	for (auto& mirror : Mirrors)
		futures.push_back(std::async([&]() { mirror.CreateArtificialMethods(options); }));
	
	for (auto& future : futures)
		future.get(); /// to propagate exceptions
}


FileMirror::FileMirror()
{
}
