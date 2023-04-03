/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Attributes.h"
#include "Options.h"
#include "Documentation.h"
#include <ghassanpl/string_ops.h>
#include <mutex>
#include <future>
using namespace std::string_literals;

uint64_t ChangeTime = 0;
std::vector<std::unique_ptr<FileMirror>> Mirrors;

void Declaration::CreateArtificialMethodsAndDocument(Options const& options)
{
	if (auto& attr = Attribute::Deprecated.TryGet(*this); !attr.is_null())
	{
		if (attr.is_boolean() && attr == false)
			return;
		this->Deprecation = attr.is_string() ? std::string{attr} : std::string{};
	}

	Attribute::Document.TryGet(*this, Document);

	if (auto& attr = Attribute::NoDiscard.TryGet(*this); !attr.is_null())
	{
		if (attr.is_boolean() && attr == false)
			return;

		AddNoDiscard(attr.is_string() ? std::string{attr} : std::string{});
	}

	/// TODO: We could do a preliminary pass on Comments here and create a vector<string, vector<string>> that holds lines for each section.
	/// Then the class-specific implementations of this function will move them to their appropriate sub-declarations (like params or returns or w/e)
}

json Declaration::ToJSON() const
{
	json result = SimpleDeclaration::ToJSON();
	if (!Attributes.empty())
		result["Attributes"] = Attributes;
	result["FullName"] = FullName(".");
	if (DeclarationLine != 0) result["DeclarationLine"] = DeclarationLine;
	if (Access != AccessMode::Unspecified) result["Access"] = AMStrings[(int)Access];
	if (!AssociatedArtificialMethods.empty())
	{
		auto& target_ams = result["AssociatedArtificialMethods"] = json::object(); 
		for (auto& [name, am] : AssociatedArtificialMethods)
		{
			target_ams = am->FullName(".");
		}
	}
	return result;
}

Enum const* FindEnum(string_view name)
{
	for (auto const& mirror : Mirrors)
		for (auto const& henum : mirror->Enums)
			if (henum->Name == name || henum->FullType() == name)
				return henum.get();
	return nullptr;
}

Method* Field::AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	return ParentType->AddArtificialMethod(*this, std::move(function_type), std::move(results), std::move(name), std::move(parameters), std::move(body), std::move(comments), additional_flags);
}

void Field::CreateArtificialMethodsAndDocument(Options const& options)
{
	Declaration::CreateArtificialMethodsAndDocument(options);

	/// Create comments string
	/// TODO: Improve this
	auto field_comments = Access != AccessMode::Public ?
		std::format("the value of the `{}` private field of this object", DisplayName) :
		std::format("the value of the {} field of this object", MakeLink());

	/// Getters and Setters
	if (!Flags.is_set(Reflector::FieldFlags::NoGetter))
	{
		auto getter = AddArtificialMethod("Getter", Type + " const &", options.GetterPrefix + CleanName, "", "return this->" + Name + ";", {"Gets " + field_comments},
			{MethodFlags::Const, MethodFlags::Noexcept, MethodFlags::NoDiscard});
		AddDocNote("Getter", "The value of this field is retrieved by the {} method.", getter->MakeLink());
	}

	if (!Flags.is_set(Reflector::FieldFlags::NoSetter))
	{
		auto on_change = Attribute::OnChange(*this);
		auto setter = AddArtificialMethod("Setter", "void", options.SetterPrefix + CleanName, Type + " const & value", "this->" + Name + " = value; " + on_change + ";", {"Sets " + field_comments}, {});
		AddDocNote("Setter", "The value of this field is set by the {} method.", setter->MakeLink());
		if (!on_change.empty())
			AddDocNote("On Change", "When this field is changed (via its setter and other such functions), the following code will be executed: `{}`", Escaped(on_change));
	}

	if (Flags.is_set(Reflector::FieldFlags::NoEdit)) AddDocNote("Not Editable", "This field is not be editable in the editor.");
	if (Flags.is_set(Reflector::FieldFlags::NoScript)) AddDocNote("Not Scriptable", "This field is not accessible via script.");
	if (Flags.is_set(Reflector::FieldFlags::NoSave)) AddDocNote("Not Saved", "This field will not be serialized when saving {}.", ParentType->MakeLink());
	if (Flags.is_set(Reflector::FieldFlags::NoLoad)) AddDocNote("Not Editable", "This field will not be deserialized when loading {}.", ParentType->MakeLink());
	if (Flags.is_set(Reflector::FieldFlags::NoDebug)) AddDocNote("Not Editable", "This field will not be debuggable when debugging {}.", ParentType->MakeLink());

	if (Flags.is_set(Reflector::FieldFlags::NoUniqueAddress)) AddDocNote("No Unique Address", "This field has the [\\[\\[no_unique_address\\]\\]](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address) attribute applied to it..", ParentType->MakeLink());

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
		constexpr auto enum_getter_flags = enum_flags{ MethodFlags::Const, MethodFlags::Inline, MethodFlags::Noexcept, MethodFlags::NoDiscard };
		constexpr auto enum_setter_flags = enum_flags{ MethodFlags::Inline };
		auto henum = FindEnum(*enum_name);
		if (!henum)
		{
			ReportError(*this, "Enum `{}' not reflected", *enum_name);
			return;
		}

		AddDocNote("Flags", "This is a bitflag field, with bits representing flags in the {} enum; accessor functions were generated in {} for each flag.", henum->MakeLink(), ParentType->MakeLink());

		ParentType->AdditionalBodyLines.push_back(std::format("static_assert(::std::is_integral_v<{}>, \"Type '{}' for field '{}' with Flags attribute must be integral\");", this->Type, henum->FullType(), this->Name));

		const auto flag_nots = Attribute::FlagNots(*this);

		for (auto& enumerator : henum->Enumerators)
		{
			AddArtificialMethod(format("FlagGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.IsPrefix + enumerator->Name, "", std::format("return (this->{} & {}{{{}}}) != 0;", Name, Type, 1ULL << enumerator->Value),
				{ "Checks whether the " + enumerator->MakeLink() + " flag is set in " + MakeLink()}, enum_getter_flags);

			if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
			{
				AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.IsPrefix + *opposite, "", std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the " + enumerator->MakeLink()  + " flag is NOT set in " + MakeLink()}, enum_getter_flags);
			}
			else if (flag_nots)
			{
				AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.IsNotPrefix + enumerator->Name, "", std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the " + enumerator->MakeLink() + " flag is set in " + MakeLink()}, enum_getter_flags);
			}
		}

		if (do_setters)
		{
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagSetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.SetterPrefix + enumerator->Name, "", std::format("this->{} |= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Sets the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags);

				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeSetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.SetterPrefix + *opposite, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Clears the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags);

				}
				else if (flag_nots)
				{
					AddArtificialMethod(format("FlagOppositeSetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.SetNotPrefix + enumerator->Name, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Clears the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags);
				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagUnsetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.UnsetPrefix + enumerator->Name, "", std::format("this->{} &= ~{}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Clears the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags);


				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeUnsetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.UnsetPrefix + *opposite, "", std::format("this->{} |= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Sets the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags);

				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagToggler.{}.{}", henum->FullName(), enumerator->Name), "void", options.TogglePrefix + enumerator->Name, "", std::format("this->{} ^= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
					{ "Toggles the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags);

				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeToggler.{}.{}", henum->FullName(), enumerator->Name), "void", options.TogglePrefix + *opposite, "", std::format("this->{} ^= {}{{{}}};", Name, Type, 1ULL << enumerator->Value),
						{ "Toggles the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags);
				}
			}
		}
	}

	if (Attribute::Required(*this))
	{
		AddDocNote("Required", "This field is required to be present when deserializing class {}.", ParentType->MakeLink());
	}
}

template <typename T>
void SetFlags(enum_flags<T> Flags, json& result)
{
	if (!Flags.empty())
	{
		auto& flag_array = result["Flags"] = json::array();
		Flags.for_each([&](auto v) {
			flag_array.push_back(std::format("{}", v));
		});
	}
}

json Field::ToJSON() const
{
	json result = MemberDeclaration<Class>::ToJSON();
	result["Type"] = Type;
	if (!InitializingExpression.empty()) result["InitializingExpression"] = InitializingExpression;
	if (DisplayName != Name) result["DisplayName"] = DisplayName;
	if (CleanName != Name) result["CleanName"] = CleanName;
	SetFlags(Flags, result);
	return result;
}

void Method::Split()
{
	std::vector<MethodParameter> args;
	int brackets = 0, tris = 0, parens = 0;
	auto argstring = std::string_view{ mParameters };
	auto begin = argstring.begin();
	MethodParameter current{};
	const char* eq_ptr = nullptr;
	auto add = [&] {
		if (eq_ptr)
		{
			const auto pre_eq = trimmed_whitespace(make_sv(begin, eq_ptr));
			const auto start_of_id = std::find_if(pre_eq.rbegin(), pre_eq.rend(), std::not_fn(ascii::isident)).base();
			current.Type = trimmed_whitespace(make_string(begin, start_of_id));
			current.Name = trimmed_whitespace(make_string(start_of_id, eq_ptr));
			if (current.Type.empty())
				current.Type = std::exchange(current.Name, {});
			current.Initializer = trimmed_whitespace(make_string(eq_ptr, argstring.begin()));
			eq_ptr = nullptr;
		}
		else
		{
			const auto full = trimmed_whitespace(make_sv(begin, argstring.begin()));
			const auto start_of_id = std::find_if(full.rbegin(), full.rend(), std::not_fn(ascii::isident)).base();
			current.Type = trimmed_whitespace(make_string(begin, start_of_id));
			current.Name = trimmed_whitespace(make_string(start_of_id, argstring.begin()));
			if (current.Type.empty())
				current.Type = std::exchange(current.Name, {});
		}
		args.push_back(std::exchange(current, {}));
	};
	while (!argstring.empty())
	{
		switch (argstring[0])
		{
		case '(': parens++; break;
		case ')': parens--; break;
		case '<': tris++; break;
		case '>': tris--; break;
		case '[': brackets++; break;
		case ']': brackets--; break;
		case ',':
			if (parens == 0 && tris == 0 && brackets == 0)
			{
				add();
				begin = argstring.begin() + 1;
			}
			break;
		case '=':
			if (parens == 0 && tris == 0 && brackets == 0)
			{
				eq_ptr = &argstring[0];
			}
			break;
		}

		argstring.remove_prefix(1);

		if (argstring.empty())
		{
			add();
			break;
		}
	}

	ParametersSplit = std::move(args);
	ParametersTypesOnly = join(ParametersSplit, ",", [](MethodParameter const& param) { return param.Type; });
	ParametersNamesOnly = join(ParametersSplit, ",", [](MethodParameter const& param) { return param.Name; });
}

void Method::SetParameters(std::string params)
{
	mParameters = std::move(params);
	Split();
}

std::string Method::GetSignature(Class const& parent_class) const
{
	auto base = Flags.is_set(MethodFlags::Static) ? 
		std::format("{} (*)({})", Return.Name, ParametersTypesOnly)
	:
		std::format("{} ({}::*)({})", Return.Name, parent_class.FullType(), ParametersTypesOnly);
	if (Flags.is_set(Reflector::MethodFlags::Const))
		base += " const";
	if (Flags.is_set(Reflector::MethodFlags::Noexcept))
		base += " noexcept";
	return base;
}

Method* Method::AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	return ParentType->AddArtificialMethod(*this, std::move(function_type), std::move(results), std::move(name), std::move(parameters), std::move(body), std::move(comments), additional_flags);
}

void Method::CreateArtificialMethodsAndDocument(Options const& options)
{
	Declaration::CreateArtificialMethodsAndDocument(options);

	if (ParentType->Flags.is_set(ClassFlags::HasProxy) && Flags.is_set(MethodFlags::Virtual))
	{
		Method* proxy_method = nullptr;
		const auto flags = (Flags - MethodFlags::Virtual) + MethodFlags::Proxy;
		if (Flags.is_set(MethodFlags::Abstract))
			proxy_method = AddArtificialMethod("Proxy", Return.Name, options.ProxyMethodPrefix + Name, GetParameters(), std::format("throw std::runtime_error{{\"invalid abstract call to function {}::{}\"}};", ParentType->FullType(), Name), {"Proxy function for " + MakeLink() }, flags);
		else
			proxy_method = AddArtificialMethod("Proxy", Return.Name, options.ProxyMethodPrefix + Name, GetParameters(), "return self_type::" + Name + "(" + ParametersNamesOnly + ");", { "Proxy function for " + MakeLink() }, flags);
		proxy_method->Document = false;
	}

	if (Flags.is_set(MethodFlags::NoReturn)) AddDocNote("Does Not Return", "This function does not return.");
}

std::string Method::FullName(std::string_view sep) const
{
	return std::format("{}{}{}", ParentType->FullName(sep), sep, GeneratedUniqueName());
}

json Method::ToJSON() const
{
	json result = MemberDeclaration<Class>::ToJSON();
	result["Return.Name"] = Return.Name;
	if (ParametersSplit.size() > 0) std::ranges::transform(ParametersSplit, std::back_inserter(result["Parameters"]), [](auto& param) { return param.ToJSON(); });
	if (!ArtificialBody.empty()) result["ArtificialBody"] = ArtificialBody;
	if (SourceDeclaration) result["SourceDeclaration"] = SourceDeclaration->FullName(".");
	if (!UniqueName.empty()) result["UniqueName"] = UniqueName;
	SetFlags(Flags, result);
	return result;
}

void Property::CreateArtificialMethodsAndDocument(Options const& options)
{
	Declaration::CreateArtificialMethodsAndDocument(options);
}

json Property::ToJSON() const
{
	auto result = Declaration::ToJSON();
	result.update(json::object({
		{ "Type", Type },
		{ "Getter", Getter ? json{Getter->FullName(".")} : json{} },
		{ "Setter", Setter ? json{Setter->FullName(".")} : json{} },
	}));
	return result;
}

Method* Class::AddArtificialMethod(Declaration& for_decl, std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags)
{
	Method& method = *mArtificialMethods.emplace_back(std::make_unique<Method>(this));
	method.SourceDeclaration = this; /// The class is the default source of the artificial method
	method.Flags += Reflector::MethodFlags::Artificial;
	method.Flags += additional_flags;
	method.Return.Name = std::move(results);
	method.Name = std::move(name);
	method.SetParameters(std::move(parameters));
	method.ArtificialBody = std::move(body);
	if (!method.ArtificialBody.empty())
		method.Flags += Reflector::MethodFlags::HasBody;
	method.DeclarationLine = 0;
	method.Access = AccessMode::Public;
	method.Comments = std::move(comments);
	method.SourceDeclaration = &for_decl;
	auto [it, inserted] = for_decl.AssociatedArtificialMethods.try_emplace(function_type, &method);
	if (!inserted)
	{
		ReportError(for_decl, "Artificial method '{}' already exists in class {}: {}", function_type, MakeLink(), it->second->MakeLink());
		throw std::runtime_error("Internal error");
	}
	method.UID = GenerateUID(ParentMirror->SourceFilePath, method.ActualDeclarationLine());
	return &method;
}

void Class::CreateArtificialMethodsAndDocument(Options const& options)
{
	Declaration::CreateArtificialMethodsAndDocument(options);

	/// Check if we should build proxy
	bool should_build_proxy = false;

	for (const auto& method : Methods)
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
		const auto getter = AddArtificialMethod(*this, "SingletonGetter", format("{}&", this->FullType()), options.SingletonInstanceGetterName, "", "static self_type instance; return instance;",
			{ "Returns the single instance of this class" }, { MethodFlags::Noexcept, MethodFlags::Static, MethodFlags::NoDiscard });
		AddDocNote("Singleton", "This class is a singleton. Call {} to get the instance.", getter->MakeLink());
	}

	if (Attribute::Abstract(*this))
	{
		AddDocNote("Abstract", "This class is not constructible via the reflection system.");
	}

	/// Create methods for fields and methods

	for (const auto& method : Methods)
		method->CreateArtificialMethodsAndDocument(options);
	for (const auto& field : Fields)
		field->CreateArtificialMethodsAndDocument(options);
	for (auto& property : Properties)
		property.second.CreateArtificialMethodsAndDocument(options);

	for (auto& am : mArtificialMethods)
	{
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
	
	for (auto const& method : Methods)
	{
		if (method->UniqueName.empty())
			continue;
		if (MethodsByName[method->UniqueName].size() > 1)
		{
			std::string message;
			message += std::format("{}({},0): method with unique name not unique", ParentMirror->SourceFilePath.string(), method->DeclarationLine + 1);
			for (auto const& conflicting_method : MethodsByName[method->UniqueName])
			{
				if (conflicting_method != method.get())
					message += std::format("\n{}({},0):   conflicts with this declaration", ParentMirror->SourceFilePath.string(), conflicting_method->DeclarationLine + 1);
			}
			throw std::exception{ message.c_str() };
		}
	}
}

Property& Class::EnsureProperty(std::string name)
{
	auto [it, added] = Properties.try_emplace(name, Property{ this });
	if (added)
		it->second.Name = name;
	else
		assert(it->second.Name == name);
	return it->second;
}

json Class::ToJSON() const
{
	auto result = TypeDeclaration::ToJSON();
	if (!BaseClass.empty()) result["BaseClass"] = BaseClass;
	
	SetFlags(Flags, result);
	
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
	if (!Properties.empty())
	{
		auto& properties = result["Properties"] = json::array();
		for (auto& [name, property] : Properties)
		{
			properties.push_back(property.ToJSON());
		}
	}

	result["BodyLine"] = BodyLine;
	if (!AdditionalBodyLines.empty()) result["AdditionalBodyLines"] = AdditionalBodyLines;
	if (!DefaultFieldAttributes.empty()) result["DefaultFieldAttributes"] = DefaultFieldAttributes;
	if (!DefaultMethodAttributes.empty()) result["DefaultMethodAttributes"] = DefaultMethodAttributes;

	SetFlags(Flags, result);

	return result;
}

json Enumerator::ToJSON() const
{
	json result = MemberDeclaration<Enum>::ToJSON();
	result["Value"] = Value;
	if (!Opposite.empty()) result["Opposite"] = Opposite;
	SetFlags(Flags, result);
	return result;
}

void Enumerator::CreateArtificialMethodsAndDocument(Options const& options)
{
	Declaration::CreateArtificialMethodsAndDocument(options);

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
	if (!DefaultEnumeratorAttributes.empty()) result["DefaultEnumeratorAttributes"] = DefaultEnumeratorAttributes;
	SetFlags(Flags, result);
	return result;
}

void Enum::CreateArtificialMethodsAndDocument(Options const& options)
{
	Declaration::CreateArtificialMethodsAndDocument(options);

	if (Attribute::List(*this))
	{
		AddDocNote("List Enum", "This enum represents a list of some sort, and its values will therefore be incrementable/decrementable (with wraparound behavior).");
	}

	for (auto const& enumerator : Enumerators)
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
	for (auto const& klass : Classes)
		klass->CreateArtificialMethodsAndDocument(options);
	for (auto const& henum : Enums)
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

std::string FormatPreFlags(enum_flags<Reflector::FieldFlags> flags, enum_flags<Reflector::FieldFlags> except)
{
	std::vector<std::string_view> prefixes;
	flags = flags - except;
	if (flags.is_set(FieldFlags::Mutable)) prefixes.push_back("mutable ");
	if (flags.is_set(FieldFlags::Static)) prefixes.push_back("static ");
	return join(prefixes, "");
}

std::string FormatPreFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except)
{
	std::vector<std::string_view> prefixes;
	flags = flags - except;
	if (flags.is_set(MethodFlags::NoDiscard)) prefixes.push_back("[[nodiscard]] ");
	if (flags.is_set(MethodFlags::Inline)) prefixes.push_back("inline ");
	if (flags.is_set(MethodFlags::Static)) prefixes.push_back("static ");
	if (flags.is_set(MethodFlags::Virtual)) prefixes.push_back("virtual ");
	if (flags.is_set(MethodFlags::Explicit)) prefixes.push_back("explicit ");
	return join(prefixes, "");
}

std::string FormatPostFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except)
{
	std::vector<std::string_view> suffixes;
	flags = flags - except;
	if (flags.is_set(MethodFlags::Const)) suffixes.push_back(" const");
	if (flags.is_set(MethodFlags::Final)) suffixes.push_back(" final");
	if (flags.is_set(MethodFlags::Noexcept)) suffixes.push_back(" noexcept");
	return join(suffixes, "");
}

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
	std::unique_lock lock{ mirror_mutex };

	/// Creating artificial methods should be plenty fast, we don't need to do it multithreaded, and it's safer that way
	for (auto const& mirror : Mirrors)
		mirror->CreateArtificialMethodsAndDocument(options);
}

FileMirror::FileMirror()
{
}

json SimpleDeclaration::ToJSON() const
{
	json result = json::object();
	result["Name"] = Name;
	if (!Comments.empty()) result["Comments"] = Comments;
	if (Document != true) result["Document"] = Document;
	if (Deprecation) result["Deprecation"] = Deprecation->empty() ? json{true} : json{ *Deprecation };
	if (!DocNotes.empty()) result["DocNotes"] = DocNotes;
	return result;
}

json MethodParameter::ToJSON() const
{
	auto result = SimpleDeclaration::ToJSON();
	result["Type"] = Type;
	if (!Initializer.empty()) result["Initializer"] = Initializer;
	return result;
}

struct LinkParts
{
	LinkParts(Declaration const& decl)
		: Name(decl.Name)
		, Href(decl.FullName("."))
	{
		if (decl.Deprecation) LinkClasses.push_back("deprecated");
	}

	std::string Name;
	std::string Href;
	std::string PreSpecifiers;
	std::string Parent;
	std::string PostSpecifiers;
	std::string ReturnType;
	std::string Parameters;
	std::string Namespace;
	std::vector<std::string> LinkClasses;
};

std::string ConstructLink(LinkParts const& parts)
{
	return std::format("<small class='specifiers'>{0}</small><a href='{1}.html' class='entitylink {2}'><small class='namespace'>{8}</small><small class='parent'>{3}</small>{4}{5}<small class='specifiers'>{6}</small></a> <small class='membertype'>{7}</small>",
		parts.PreSpecifiers, /// 0
		parts.Href, /// 1
		join(parts.LinkClasses, " "), /// 2
		Escaped(parts.Parent), /// 3
		parts.Name, /// 4
		Escaped(parts.Parameters), /// 5
		parts.PostSpecifiers, /// 6
		Escaped(parts.ReturnType), /// 7
		parts.Namespace
	);
}

std::string Enumerator::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this };
	return ConstructLink(std::move(parts));
}

std::string TypeDeclaration::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this };
	if (flags.contain(LinkFlag::SignatureSpecifiers)) {}
	if (flags.contain(LinkFlag::Specifiers)) {}
	if (flags.contain(LinkFlag::Namespace) && !parts.Namespace.empty()) parts.Namespace = Namespace + "::";
	return ConstructLink(std::move(parts));
}

std::string Field::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this };
	if (flags.contain(LinkFlag::Parent)) parts.Parent = ParentType->Name + ".";
	if (flags.contain(LinkFlag::ReturnType)) parts.ReturnType = Type;
	return ConstructLink(std::move(parts));
}

std::string Method::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this };
	if (flags.contain(LinkFlag::Parent)) parts.Parent = ParentType->Name + "::";
	if (flags.contain(LinkFlag::SignatureSpecifiers)) parts.PostSpecifiers = FormatPostFlags(Flags, { MethodFlags::Final, MethodFlags::Noexcept });
	if (flags.contain(LinkFlag::Specifiers)) parts.PreSpecifiers = FormatPreFlags(Flags, { MethodFlags::Inline, MethodFlags::NoDiscard });
	if (flags.contain(LinkFlag::ReturnType) && Return.Name != "void") parts.ReturnType = "-> " + Return.Name;
	if (flags.contain(LinkFlag::Parameters)) parts.Parameters = std::format("({})", join(ParametersTypesOnly));
	return ConstructLink(std::move(parts));
}

std::string Property::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this };
	if (flags.contain(LinkFlag::Parent)) parts.Parent = ParentType->Name + ".";
	if (flags.contain(LinkFlag::ReturnType)) parts.ReturnType = Type;
	return ConstructLink(std::move(parts));
}