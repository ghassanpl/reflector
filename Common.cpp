/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Parse.h"
#include "Attributes.h"
#include "Options.h"
#include "Documentation.h"
#include <ghassanpl/string_ops.h>
#include <mutex>
#include <future>
#include <fstream>
using namespace std::string_literals;

uint64_t ChangeTime = 0;
std::vector<std::unique_ptr<FileMirror>> Mirrors;

Method const* Declaration::GetAssociatedMethod(std::string_view function_type) const
{
	auto it = AssociatedArtificialMethods.find(function_type);
	if (it == AssociatedArtificialMethods.end())
		return {};
	return it->second;
}

json Declaration::ToJSON() const
{
	/// TODO: Update these functions, they don't reflect the latest fields in the declaration classes

	json result = json::object();
	if (!Attributes.empty())
		result["Attributes"] = Attributes;
	result["Name"] = Name;
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
		for (auto& henum : mirror->Enums)
			if (henum->Name == name || henum->FullType() == name)
				return henum.get();
	return nullptr;
}

Method* Field::AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	return ParentType->AddArtificialMethod(*this, move(function_type), move(results), move(name), move(parameters), move(body), move(comments), additional_flags);
}

void Field::CreateArtificialMethodsAndDocument(Options const& options)
{
	/// Create comments string
	/// TODO: We used to create the comments for accessors from the comments of the field.
	/// Now we don't but private fields don't show up in documentation, so giving the name of the field that isn't available is confusing...
	auto field_comments = std::format("the **{}** field of this object", DisplayName);

	/// Getters and Setters
	if (!Flags.is_set(Reflector::FieldFlags::NoGetter))
	{
		auto getter = AddArtificialMethod("Getter", Type + " const &", options.GetterPrefix + CleanName, "", "return this->" + Name + ";", {"Gets " + field_comments}, {Reflector::MethodFlags::Const});
		AddDocNote("Getter", "The value of this field is retrieved by the {} method.", MakeLink(getter));
	}

	if (!Flags.is_set(Reflector::FieldFlags::NoSetter))
	{
		auto on_change = Attribute::OnChange(*this);
		auto setter = AddArtificialMethod("Setter", "void", options.SetterPrefix + CleanName, Type + " const & value", "this->" + Name + " = value; " + on_change + ";", {"Sets " + field_comments}, {});
		AddDocNote("Setter", "The value of this field is set by the {} method.", MakeLink(setter));
		if (!on_change.empty())
			AddDocNote("On Change", "When this field is changed (via its setter and other such functions), the following code will be executed: `{}`", Escaped(on_change));
	}

	if (Flags.is_set(Reflector::FieldFlags::NoEdit)) AddDocNote("Not Editable", "This field is not be editable in the editor.");
	if (Flags.is_set(Reflector::FieldFlags::NoScript)) AddDocNote("Not Scriptable", "This field is not accessible via script.");
	if (Flags.is_set(Reflector::FieldFlags::NoSave)) AddDocNote("Not Saved", "This field will not be serialized when saving {}.", MakeLink(ParentType));
	if (Flags.is_set(Reflector::FieldFlags::NoLoad)) AddDocNote("Not Editable", "This field will not be deserialized when loading {}.", MakeLink(ParentType));
	if (Flags.is_set(Reflector::FieldFlags::NoDebug)) AddDocNote("Not Editable", "This field will not be debuggable when debugging {}.", MakeLink(ParentType));

	auto flag_getters = Attribute::FlagGetters.SafeGet(*this);
	auto flag_setters = Attribute::Flags.SafeGet(*this);
	if (flag_getters && flag_setters)
	{
		ReportError(*this, "Only one of `FlagGetters' and `Flags' can be declared");
		return;
	}
	const bool do_flags = flag_getters || flag_setters;
	const bool do_setters = !!flag_setters;
	auto& enum_name = do_setters ? flag_setters : flag_getters;

	if (do_flags)
	{
		constexpr auto enum_getter_flags = enum_flags{ Reflector::MethodFlags::Const, Reflector::MethodFlags::Inline, Reflector::MethodFlags::Noexcept };
		constexpr auto enum_setter_flags = enum_getter_flags - Reflector::MethodFlags::Const;
		auto henum = FindEnum(*enum_name);
		if (!henum)
		{
			ReportError(*this, "Enum `{}' not reflected", *enum_name);
			return;
		}

		AddDocNote("Flags", "This is a bitflag field, with bits representing flags in the {} enum; accessor functions were generated in {} for each flag.", MakeLink(henum), MakeLink(ParentType));

		ParentType->AdditionalBodyLines.push_back(std::format("static_assert(::std::is_integral_v<{}>, \"Type '{}' for field '{}' with Flags attribute must be integral\");", this->Type, henum->FullType(), this->Name));

		const auto flag_nots = Attribute::FlagNots(*this);

		/// TODO: use MakeLink instead of naked DisplayName in the comments
		for (auto& enumerator : henum->Enumerators)
		{
			AddArtificialMethod(format("FlagGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.IsPrefix + enumerator->Name, "", std::format("return (this->{} & {}{{{}}}) != 0;", Name, Type, 1ULL << enumerator->Value),
				{ "Checks whether the `" + enumerator->Name + "` flag is set in " + DisplayName }, enum_getter_flags);

			if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
			{
				AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.IsPrefix + *opposite, "", std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the `" + enumerator->Name + "` flag is NOT set in " + DisplayName }, enum_getter_flags);
			}
			else if (flag_nots)
			{
				AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.IsNotPrefix + enumerator->Name, "", std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the `" + enumerator->Name + "` flag is set in " + DisplayName }, enum_getter_flags);
			}
		}

		if (do_setters)
		{
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagSetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.SetterPrefix + enumerator->Name, "", std::format("this->{} |= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Sets the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.SetterPrefix + *opposite, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Clears the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				}
				else if (flag_nots)
				{
					AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.SetNotPrefix + enumerator->Name, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Clears the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);
				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagUnsetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.UnsetPrefix + enumerator->Name, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Clears the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);


				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeUnsetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.UnsetPrefix + *opposite, "", std::format("this->{} |= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Sets the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagToggler.{}.{}", henum->FullName(), enumerator->Name), "void", options.TogglePrefix + enumerator->Name, "", std::format("this->{} ^= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Toggles the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);

				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeToggler.{}.{}", henum->FullName(), enumerator->Name), "void", options.TogglePrefix + *opposite, "", std::format("this->{} ^= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Toggles the `" + enumerator->Name + "` flag in " + DisplayName }, enum_setter_flags);
				}
			}
		}
	}

	if (Attribute::Required(*this))
	{
		AddDocNote("Required", "This field is required to be present when deserializing class {}.", MakeLink(ParentType));
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

Method* Method::AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	return ParentType->AddArtificialMethod(*this, move(function_type), move(results), move(name), move(parameters), move(body), move(comments), additional_flags);
}

void Method::CreateArtificialMethodsAndDocument(Options const& options)
{
	if (ParentType->Flags.is_set(ClassFlags::HasProxy) && Flags.is_set(MethodFlags::Virtual))
	{
		Method* proxy_method = nullptr;
		auto flags = (Flags - MethodFlags::Virtual) + MethodFlags::Proxy;
		if (Flags.is_set(MethodFlags::Abstract))
			proxy_method = AddArtificialMethod("Proxy", ReturnType, options.ProxyMethodPrefix + Name, GetParameters(), std::format("throw std::runtime_error{{\"invalid abstract call to function {}::{}\"}};", ParentType->FullType(), Name), {"Proxy function for " + Name}, flags);
		else
			proxy_method = AddArtificialMethod("Proxy", ReturnType, options.ProxyMethodPrefix + Name, GetParameters(), "return self_type::" + Name + "(" + ParametersNamesOnly + ");", { "Proxy function for " + Name }, flags);
		proxy_method->Document = false;
	}
}

std::string Method::FullName(std::string_view sep) const
{
	return std::format("{}{}{}", ParentType->FullName(sep), sep, GeneratedUniqueName());
}

json Method::ToJSON() const
{
	json result = Declaration::ToJSON();
	result["ReturnType"] = ReturnType;
	if (ParametersSplit.size() > 0)
		std::transform(ParametersSplit.begin(), ParametersSplit.end(), std::back_inserter(result["Parameters"]), &::ToJSON);
	if (!ArtificialBody.empty())
		result["ArtificialBody"] = ArtificialBody;
	if (SourceDeclaration && SourceDeclaration->DeclarationLine != 0)
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

void Property::CreateArtificialMethodsAndDocument(Class& klass, Options const& options)
{
}

Method* Class::AddArtificialMethod(Declaration& for_decl, std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
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
	method.SourceDeclaration = &for_decl;
	for_decl.AssociatedArtificialMethods[move(function_type)] = &method; /// TODO: insert() and check for existing
	return &method;
}

void Class::CreateArtificialMethodsAndDocument(Options const& options)
{
	/// Check if we should build proxy
	bool should_build_proxy = false;

	for (auto& method : Methods)
	{
		if (method->Flags.is_set(Reflector::MethodFlags::Virtual) && !method->Flags.is_set(Reflector::MethodFlags::Final))
			should_build_proxy = true;
	}

	if (should_build_proxy && !Attribute::CreateProxy(*this))
	{
		AddDocNote("No Proxy", "Even though this class has virtual methods, no proxy class will be created for it, which means creating runtime subclasses for it will be limited or impossible.");
	}

	should_build_proxy = should_build_proxy && Attribute::CreateProxy(*this);

	Flags.set_to(should_build_proxy, ClassFlags::HasProxy);

	/// Create singleton method if singleton
	if (Attribute::Singleton(*this))
	{
		auto getter = AddArtificialMethod(*this, "SingletonGetter", format("{}&", this->FullType()), options.SingletonInstanceGetterName, "", "static self_type instance; return instance;", { "Returns the single instance of this class" }, Reflector::MethodFlags::Static);
		AddDocNote("Singleton", "This class is a singleton. Call {} to get the instance.", MakeLink(getter));
	}

	if (Attribute::Abstract(*this))
	{
		AddDocNote("Abstract", "This class is not constructible via the reflection system.");
	}

	/// Create methods for fields and methods

	for (auto& field : Fields)
		field->CreateArtificialMethodsAndDocument(options);
	for (auto& method : Methods)
		method->CreateArtificialMethodsAndDocument(options);
	for (auto& property : Properties)
		property.second.CreateArtificialMethodsAndDocument(*this, options);

	for (auto& am : mArtificialMethods)
	{
		am->UID = GenerateUID(ParentMirror->SourceFilePath, am->ActualDeclarationLine());
		Methods.push_back(std::exchange(am, {}));
	}
	mArtificialMethods.clear();

	/// First check unique method names
	MethodsByName.clear();

	for (auto& method : Methods)
	{
		MethodsByName[method->Name].push_back(method.get());
		if (!method->UniqueName.empty() && method->Name != method->UniqueName)
		{
			MethodsByName[method->UniqueName].push_back(method.get());
			method->AddDocNote("Unique Name", "This method's unique name will be `{}` in scripts.", method->UniqueName);
		}
	}
	
	for (auto& method : Methods)
	{
		if (method->UniqueName.empty())
			continue;
		if (MethodsByName[method->UniqueName].size() > 1)
		{
			std::string message;
			message += std::format("{}({},0): method with unique name not unique", ParentMirror->SourceFilePath.string(), method->DeclarationLine + 1);
			for (auto& conflicting_method : MethodsByName[method->UniqueName])
			{
				if (conflicting_method != method.get())
					message += std::format("\n{}({},0):   conflicts with this declaration", ParentMirror->SourceFilePath.string(), conflicting_method->DeclarationLine + 1);
			}
			throw std::exception{ message.c_str() };
		}
	}
}

json Class::ToJSON() const
{
	auto result = TypeDeclaration::ToJSON();
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

void Enumerator::CreateArtificialMethodsAndDocument(Options const& options)
{
	if (auto opposite = Attribute::Opposite(*this); !opposite.empty())
	{
		AddDocNote("Opposite", "The complement of this flag value is named `{}`.", opposite);
	}
}

json Enum::ToJSON() const
{
	json result = TypeDeclaration::ToJSON();
	auto& enumerators = result["Enumerators"] = json::object();
	for (auto& enumerator : Enumerators)
		enumerators[enumerator->Name] = enumerator->ToJSON();
	return result;
}

void Enum::CreateArtificialMethodsAndDocument(Options const& options)
{
	if (Attribute::List(*this))
	{
		AddDocNote("List Enum", "This enum represents a list of some sort, and its values will therefore be incrementable/decrementable (with wraparound behavior).");
	}

	for (auto& enumerator : Enumerators)
		enumerator->CreateArtificialMethodsAndDocument(options);
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

void FileMirror::CreateArtificialMethodsAndDocument(Options const& options)
{
	for (auto& klass : Classes)
		klass->CreateArtificialMethodsAndDocument(options);
	for (auto& henum : Enums)
		henum->CreateArtificialMethodsAndDocument(options);
}

void PrintSafe(std::ostream& strm, std::string val)
{
	static std::mutex print_mutex;
	std::unique_lock locl{ print_mutex };
	strm << val;
}

bool ParsingMirrors = false;
static std::mutex mirror_mutex;

std::vector<FileMirror const*> GetMirrors()
{
	std::unique_lock lock{ mirror_mutex };
	return Mirrors | std::views::transform([](auto& mir) { return (FileMirror const*)mir.get(); }) | std::ranges::to<std::vector>();
}


FileMirror* AddMirror()
{
	std::unique_lock lock{ mirror_mutex };
	return Mirrors.emplace_back(std::make_unique<FileMirror>()).get();
}

void RemoveEmptyMirrors()
{
	std::unique_lock lock{ mirror_mutex };
	std::erase_if(Mirrors, [](auto& mirror) { return mirror->IsEmpty(); });
}

void CreateArtificialMethodsAndDocument(Options const& options)
{
#if 0
	/// TODO: Not sure if these are safe to be multithreaded, we ARE adding new methods to the mirrors after all...
	/// This could be an issue if we're changing a different mirror...
	std::vector<std::future<void>> futures;
	for (auto& mirror : Mirrors)
		futures.push_back(std::async([&]() { mirror->CreateArtificialMethodsAndDocument(options); }));
	
	for (auto& future : futures)
		future.get(); /// to propagate exceptions
#else
	std::unique_lock lock{ mirror_mutex };

	/// Creating artificial methods should be plenty fast, we don't need to do it multithreaded, and it's safer that way
	for (auto& mirror : Mirrors)
		mirror->CreateArtificialMethodsAndDocument(options);
#endif
}

FileMirror::FileMirror()
{
}
