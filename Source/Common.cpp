/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Attributes.h"
#include "Options.h"
#include "Documentation.h"
#include <ghassanpl/string_ops.h>
#include <ghassanpl/hashes.h>
#include <ghassanpl/ranges.h>
#include <mutex>
#include <future>
#include <regex>
using namespace std::string_literals;

std::vector<std::unique_ptr<FileMirror>> Mirrors;

extern Options const* global_options;

std::string HighlightTypes(std::string_view type, TypeDeclaration const* search_context);

void Declaration::CreateArtificialMethodsAndDocument(Options const& options)
{
	if (auto& attr = Attribute::Deprecated.TryGet(*this); !attr.is_null())
	{
		if (attr.is_boolean() && attr == false)
			return;
		
		Deprecation = attr.is_string() ? std::string{attr} : std::string{};
		EntityFlags.set(Reflector::EntityFlags::Deprecated);
	}

	EntityFlags.set_to(Attribute::Unimplemented.GetOr(*this, false), Reflector::EntityFlags::Unimplemented);
	Attribute::DocumentMembers.TryGet(*this, DocumentMembers);
	if (bool document_set = false; Attribute::Document.TryGet(*this, document_set))
		ForceDocument = document_set;

	if (auto& attr = Attribute::NoDiscard.TryGet(*this); !attr.is_null())
	{
		if (attr.is_boolean() && attr == false)
			return;

		AddNoDiscard(attr.is_string() ? std::string{attr} : std::string{});
	}

	if (EntityFlags.is_set(Reflector::EntityFlags::Deprecated))
	{
		if (Deprecation)
			AddWarningDocNote("Deprecated", Deprecation.value());
		else
			AddWarningDocNote("Deprecated", "This {} is deprecated; no reason was given", ascii::tolower(magic_enum::enum_name(this->DeclarationType())));
	}

	if (EntityFlags.is_set(Reflector::EntityFlags::Unimplemented))
	{
		AddWarningDocNote("Unimplemented", "This {}'s functionality is unimplemented", ascii::tolower(magic_enum::enum_name(this->DeclarationType()))).Icon = "circle-slash";
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
	if (DisplayName != Name) result["DisplayName"] = DisplayName;
	if (!DocumentMembers) result["DocumentMembers"] = DocumentMembers;
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

std::vector<Class const*> FindClasses(string_view name)
{
	std::vector<Class const*> result;
	for (auto const& mirror : Mirrors)
		for (auto const& klass : mirror->Classes)
			if (klass->Name == name || klass->FullType() == name)
				result.push_back(klass.get());
	return result;
}

std::vector<TypeDeclaration const*> FindTypes(string_view name)
{
	std::vector<TypeDeclaration const*> result;
	for (auto const& mirror : Mirrors)
	{
		for (auto const& klass : mirror->Classes)
			if (klass->Name == name || klass->FullType() == name)
				result.push_back(klass.get());

		for (auto const& henum : mirror->Enums)
			if (henum->Name == name || henum->FullType() == name)
				result.push_back(henum.get());
	}
	return result;
}

void to_json(json& j, DocNote const& p)
{
	j = {
		{ "Header", p.Header },
		{ "Contents", p.Contents }
	};
	if (p.ShowInMemberList) j["ShowInMemberList"] = p.ShowInMemberList;
	if (!p.Icon.empty()) j["Icon"] = p.Icon;
}

Method* Field::AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags, ghassanpl::enum_flags<Reflector::EntityFlags> entity_flags)
{
	return ParentType->AddArtificialMethod(*this, std::move(function_type), std::move(results), std::move(name), std::move(parameters), std::move(body), std::move(comments), additional_flags, entity_flags);
}

void Field::CreateArtificialMethodsAndDocument(Options const& options)
{
	MemberDeclaration::CreateArtificialMethodsAndDocument(options);

	/// Create comments string
	/// TODO: Improve this
	auto field_comments = Access != AccessMode::Public ?
		std::format("the value of the `{}` private field of this object", DisplayName) :
		std::format("the value of the {} field of this object", MakeLink());

	Property* property_for_field = nullptr;
	if (
		(Flags.is_set(Reflector::FieldFlags::DeclaredPrivate) || options.GeneratePropertiesForPublicFields)
		&& !Flags.contains_all_of(FieldFlags::NoGetter, FieldFlags::NoSetter)
		&& Attribute::Property.GetOr(*this, true))
	{
		property_for_field = &ParentType->EnsureProperty(CleanName);
		property_for_field->SourceField = this;
		if (property_for_field->Type.empty()) property_for_field->Type = Type;
	}

	/// Getters and Setters
	if (!Flags.is_set(Reflector::FieldFlags::NoGetter))
	{
		using enum Reflector::MethodFlags;
		auto getter = AddArtificialMethod("Getter", Type + " const&", options.Names.GetterPrefix + CleanName, "", "return this->" + Name + ";", {"Gets " + field_comments},
			{Const, Noexcept, NoDiscard});
		if (Flags.is_set(Reflector::FieldFlags::NoScript)) 
			getter->Flags += Reflector::MethodFlags::NoScript;
		AddDocNote("Getter", "The value of this field is retrieved by the {} method.", getter->MakeLink());

		if (property_for_field)
			property_for_field->Getter = getter;
	}

	if (!Flags.is_set(Reflector::FieldFlags::NoSetter))
	{
		auto on_change = Attribute::OnChange(*this);
		auto setter = AddArtificialMethod("Setter", "void", options.Names.SetterPrefix + CleanName, Type + " const& value", 
			"static_assert(std::is_copy_assignable_v<decltype(this->"+Name+")>, \"err\"); this->" + Name + " = value; " + on_change + ";", 
			{"Sets " + field_comments}, {});
		if (Flags.is_set(Reflector::FieldFlags::NoScript))
			setter->Flags += Reflector::MethodFlags::NoScript;
		AddDocNote("Setter", "The value of this field is set by the {} method.", setter->MakeLink());
		if (!on_change.empty())
			AddDocNote("On Change", "When this field is changed (via its setter and other such functions), the following code will be executed: `{}`", Escaped(on_change));

		if (property_for_field)
			property_for_field->Setter = setter;
	}

	if (Flags.is_set(Reflector::FieldFlags::NoEdit)) AddDocNote("Not Editable", "This field is not be editable in the editor.");
	if (Flags.is_set(Reflector::FieldFlags::NoScript)) AddDocNote("Not Scriptable", "This field is not accessible via script.");
	if (Flags.is_set(Reflector::FieldFlags::NoSave)) AddDocNote("Not Saved", "This field will not be serialized when saving {}.", ParentType->MakeLink());
	if (Flags.is_set(Reflector::FieldFlags::NoLoad)) AddDocNote("Not Loaded", "This field will not be deserialized when loading {}.", ParentType->MakeLink());
	if (Flags.is_set(Reflector::FieldFlags::NoDebug)) AddDocNote("Not Debuggable", "This field will not be debuggable when debugging {}.", ParentType->MakeLink());

	if (Flags.is_set(Reflector::FieldFlags::NoUniqueAddress)) AddDocNote("No Unique Address", "This field has the [\\[\\[no_unique_address\\]\\]](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address) attribute applied to it..", ParentType->MakeLink());

	/// This doesn't work because the ID needs to be serialized to work, which means we can only do it per-class
	/*
	if (Attribute::UniqueID_Fields.SafeGet(*this))
	{
		/// TODO: Add option for the generator function prefix
		auto method = AddArtificialMethod(
			format("NewUniqueIDFor_{}", Name), 
			Type, 
			options.Names.NewUniqueIDFor + Name, 
			"", /// Parameters
			std::format("static {} UniqueID__ = {{}}; return ++UniqueID__;", Type),
			{}, 
			MethodFlags::Static
		);
		method->ForceDocument = false;
	}
	*/

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
		constexpr auto enum_getter_flags = enum_flags{ MethodFlags::Const, MethodFlags::Inline, MethodFlags::Noexcept, MethodFlags::NoDiscard, MethodFlags::ForFlag };
		constexpr auto enum_setter_flags = enum_flags{ MethodFlags::Inline, MethodFlags::ForFlag };
		auto henum = FindEnum(*enum_name);
		if (!henum)
		{
			ReportError(*this, "Enum `{}' not reflected", *enum_name);
			return;
		}

		AddDocNote("Flags", "This is a bitflag field, with bits representing flags in the {} enum; accessor functions were generated in {} for each flag.", henum->MakeLink(), ParentType->MakeLink());

		ParentType->AdditionalBodyLines.push_back(std::format("static_assert(::std::is_integral_v<{0}>, \"Type '{0}' for field '{2}' with attribute 'Flags={2}' must be integral\");", this->Type, henum->FullType(), this->Name));
		if (!henum->IsConsecutive())
		{
			AddWarningDocNote("Non-Consecutive Flags", "The enumerators in the {} enum are not consecutive, which may cause issues with the generated flag methods.", henum->MakeLink());
			ReportWarning(*this, "The enumerators in the '{}' enum are not consecutive, which may cause issues with the generated flag methods.", henum->FullType());
		}
		enum_flags<int64_t> set_bits;
		for (auto& enumerator : henum->Enumerators)
			set_bits += enumerator->Value;
		//if (set_bits.last_set() >= ReportWarning(*this, 
		ParentType->AdditionalBodyLines.push_back(std::format("static_assert(sizeof({0})*CHAR_BIT >= {3}, \"Type '{0}' for field '{2}' with Flags attribute is too small to hold all values of its flag type {1}\");", this->Type, henum->FullType(), this->Name, set_bits.last_set() + 1));

		const auto flag_nots = Attribute::FlagNots(*this);

		/// TODO: Make sure we're generating the proper accesses for flag methods generated from private fields!

		for (auto& enumerator : henum->Enumerators)
		{
			/// TODO: If enumerator name starts with 'Not' (notprefix?), invert the logic of the getters and setters; control this using an option
			
			this->Parent()->ClassDeclaredFlags.emplace_back(enumerator->Name, this, enumerator.get());

			AddArtificialMethod(format("FlagGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.Names.IsPrefix + enumerator->Name, "", 
				std::format("return (this->{} & {}{{{}}}) != 0;", Name, Type, 1ULL << enumerator->Value),
				{ "Checks whether the " + enumerator->MakeLink() + " flag is set in " + MakeLink()}, enum_getter_flags);

			if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
			{
				AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.Names.IsPrefix + *opposite, "", 
					std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the " + enumerator->MakeLink()  + " flag is NOT set in " + MakeLink()}, enum_getter_flags);
			}
			else if (flag_nots)
			{
				AddArtificialMethod(format("FlagOppositeGetter.{}.{}", henum->FullName(), enumerator->Name), "bool", options.Names.IsNotPrefix + enumerator->Name, "", 
					std::format("return (this->{} & {}{{{}}}) == 0;", Name, Type, 1ULL << enumerator->Value),
					{ "Checks whether the " + enumerator->MakeLink() + " flag is set in " + MakeLink()}, enum_getter_flags);
			}
		}

		/// Setters
		{
			auto on_change = Attribute::OnChange(*this);

			auto setter_access = do_setters ? AccessMode::Public : AccessMode::Protected;
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagSetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.SetterPrefix + enumerator->Name, "", 
					std::format("this->{} |= {}{{{}}}; {};", Name, Type, 1ULL << enumerator->Value, on_change),
					{ "Sets the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags)->Access = setter_access;
				AddArtificialMethod(format("FlagSetterTo.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.SetterPrefix + enumerator->Name, "bool val",
					std::format("val ? (this->{0} |= {1}{{{2}}}) : (this->{0} &= ~{1}{{{2}}}); {3};", Name, Type, 1ULL << enumerator->Value, on_change),
					{ "Sets or unsets the " + enumerator->MakeLink() + " flag in " + MakeLink() + " depending on the given value" }, enum_setter_flags)->Access = setter_access;

				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeSetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.SetterPrefix + *opposite, "", 
						std::format("this->{} &= ~{}{{{}}}; {};", Name, Type, 1ULL << enumerator->Value, on_change),
						{ "Clears the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags)->Access = setter_access;

				}
				else if (flag_nots)
				{
					AddArtificialMethod(format("FlagOppositeSetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.SetNotPrefix + enumerator->Name, "", 
						std::format("this->{} &= ~{}{{{}}}; {};", Name, Type, 1ULL << enumerator->Value, on_change),
						{ "Clears the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags)->Access = setter_access;
				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagUnsetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.UnsetPrefix + enumerator->Name, "", 
					std::format("this->{} &= ~{}{{{}}}; {};", Name, Type, 1ULL << enumerator->Value, on_change),
					{ "Clears the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags)->Access = setter_access;


				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeUnsetter.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.UnsetPrefix + *opposite, "", 
						std::format("this->{} |= {}{{{}}}; {};", Name, Type, 1ULL << enumerator->Value, on_change),
						{ "Sets the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags)->Access = setter_access;

				}
			}
			for (auto& enumerator : henum->Enumerators)
			{
				AddArtificialMethod(format("FlagToggler.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.TogglePrefix + enumerator->Name, "", 
					std::format("this->{} ^= {}{{{}}}; {};", Name, Type, 1ULL << enumerator->Value, on_change),
					{ "Toggles the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags)->Access = setter_access;

				if (auto opposite = Attribute::Opposite.SafeGet(*enumerator))
				{
					AddArtificialMethod(format("FlagOppositeToggler.{}.{}", henum->FullName(), enumerator->Name), "void", options.Names.TogglePrefix + *opposite, "", 
						std::format("this->{} ^= {}{{{}}}; {};", Name, Type, 1ULL << enumerator->Value, on_change),
						{ "Toggles the " + enumerator->MakeLink() + " flag in " + MakeLink() }, enum_setter_flags)->Access = setter_access;
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
void SetFlags(enum_flags<T> Flags, json& result, std::string_view field_name = "Flags")
{
	if (!Flags.empty())
	{
		auto& flag_array = result[field_name] = json::array();
		Flags.for_each([&](auto v) {
			flag_array.push_back(std::format("{}", magic_enum::enum_name(v)));
		});
	}
}

json Field::ToJSON() const
{
	json result = MemberDeclaration<Class>::ToJSON();
	result["Type"] = Type;
	if (!InitializingExpression.empty()) result["InitializingExpression"] = InitializingExpression;
	if (CleanName != Name) result["CleanName"] = CleanName;
	SetFlags(Flags, result);
	return result;
}

void Method::Split()
{
	std::vector<MethodParameter> args;
	int brackets = 0;
	int tris = 0;
	int parens = 0;
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
	using enum Reflector::MethodFlags;

	auto base = Flags.is_set(Static) ? 
		std::format("{} (*)({})", Return.Name, ParametersTypesOnly)
	:
		std::format("{} ({}::*)({})", Return.Name, parent_class.FullType(), ParametersTypesOnly);
	if (Flags.is_set(Const))
		base += " const";
	if (Flags.is_set(Noexcept))
		base += " noexcept";
	return base;
}

Method* Method::AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags, ghassanpl::enum_flags<Reflector::EntityFlags> entity_flags)
{
	return ParentType->AddArtificialMethod(*this, std::move(function_type), std::move(results), std::move(name), std::move(parameters), std::move(body), std::move(comments), additional_flags, entity_flags);
}

void Method::CreateArtificialMethodsAndDocument(Options const& options)
{
	using enum Reflector::MethodFlags;

	MemberDeclaration::CreateArtificialMethodsAndDocument(options);

	if (ParentType->Flags.is_set(ClassFlags::HasProxy) && Flags.is_set(Virtual))
	{
		Method* proxy_method = nullptr;
		const auto flags = (Flags - Virtual) + Proxy;
		if (Flags.is_set(Abstract))
			proxy_method = AddArtificialMethod("Proxy", Return.Name, options.Names.ProxyMethodPrefix + Name, GetParameters(), std::format("throw std::runtime_error{{\"invalid abstract call to function {}::{}\"}};", ParentType->FullType(), Name), {"Proxy function for " + MakeLink() }, flags);
		else
			proxy_method = AddArtificialMethod("Proxy", Return.Name, options.Names.ProxyMethodPrefix + Name, GetParameters(), "return self_type::" + Name + "(" + ParametersNamesOnly + ");", { "Proxy function for " + MakeLink() }, flags);
		proxy_method->ForceDocument = false;
	}

	if (Flags.is_set(NoReturn)) AddDocNote("Does Not Return", "This function does not return.");
	if (Flags.is_set(NoScript)) AddDocNote("Not Scriptable", "This method is not accessible via script.");
}

std::string Method::FullName(std::string_view sep) const
{
	return std::format("{}{}{}", ParentType->FullName(sep), sep, GeneratedUniqueName());
}

json Method::ToJSON() const
{
	json result = MemberDeclaration<Class>::ToJSON();
	result["Return.Name"] = Return.Name;
	if (!ParametersSplit.empty()) std::ranges::transform(ParametersSplit, std::back_inserter(result["Parameters"]), [](auto& param) { return param.ToJSON(); });
	if (!ArtificialBody.empty()) result["ArtificialBody"] = ArtificialBody;
	if (SourceDeclaration) result["SourceDeclaration"] = SourceDeclaration->FullName(".");
	if (!UniqueName.empty()) result["UniqueName"] = UniqueName;
	SetFlags(Flags, result);
	return result;
}

void Property::CreateArtificialMethodsAndDocument(Options const& options)
{
	if (SourceField)
	{
		Flags += Reflector::PropertyFlags::FromField;
		if (SourceField->Flags.is_set(Reflector::FieldFlags::NoEdit))
			Flags += Reflector::PropertyFlags::NoEdit;
		if (SourceField->Flags.is_set(Reflector::FieldFlags::NoScript))
			Flags += Reflector::PropertyFlags::NoScript;
		if (SourceField->Flags.is_set(Reflector::FieldFlags::NoDebug))
			Flags += Reflector::PropertyFlags::NoDebug;
	}
	/// TODO: Static properties?

	MemberDeclaration::CreateArtificialMethodsAndDocument(options);
}

json Property::ToJSON() const
{
	auto result = MemberDeclaration<Class>::ToJSON();
	result.update(json::object({
		{ "Type", Type },
		{ "Getter", Getter ? json{Getter->FullName(".")} : json{} },
		{ "Setter", Setter ? json{Setter->FullName(".")} : json{} },
	}));
	SetFlags(Flags, result);
	return result;
}

Method* Class::AddArtificialMethod(Declaration& for_decl, std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, ghassanpl::enum_flags<Reflector::MethodFlags> additional_flags, ghassanpl::enum_flags<Reflector::EntityFlags> entity_flags)
{
	Method& method = *mArtificialMethods.emplace_back(std::make_unique<Method>(this));
	method.SourceDeclaration = this; /// The class is the default source of the artificial method
	method.Flags += Reflector::MethodFlags::Artificial;
	method.Flags += additional_flags;
	method.EntityFlags += entity_flags;
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
	method.ReflectionUID = ghassanpl::hash64(ParentMirror->SourceFilePath.string(), method.ActualDeclarationLine(), method.GetParameters());
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
		using enum Reflector::MethodFlags;
		const auto getter = AddArtificialMethod(*this, "SingletonGetter", format("{}&", this->FullType()), options.Names.SingletonInstanceGetterName, "",
			"static_assert(!::Reflector::derives_from_reflectable<self_type>, \"Reflectable classes cannot be singletons currently\"); static self_type instance; return instance;",
			{ "Returns the single instance of this class" }, { Noexcept, Static, NoDiscard });
		AddDocNote("Singleton", "This class is a singleton. Call {} to get the instance.", getter->MakeLink());
	}

	if (Attribute::Abstract(*this))
	{
		AddDocNote("Abstract", "This class is not constructible via the reflection system.");
	}

	/// TODO: Find duplicates and other issues like:
	/// - field with the same name as a type (will cause issues in DB)
	/// Also find a good/better place to put this duplicate checker

	/// Create methods for fields and methods

	for (const auto& method : Methods)
		method->CreateArtificialMethodsAndDocument(options);
	for (const auto& field : Fields)
		field->CreateArtificialMethodsAndDocument(options);
	for (auto& [name, property] : Properties)
		property.CreateArtificialMethodsAndDocument(options);

	for (auto& am : mArtificialMethods)
	{
		Methods.push_back(std::exchange(am, {}));
	}
	mArtificialMethods.clear();


	/// First check unique method names
	MethodsByName.clear();

	for (auto const& method : Methods)
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

	/// Check if non-abstract-marked class has abstract methods
	
	if (!Attribute::Abstract(*this))
	{
		for (auto const& method : Methods)
		{
			if (method->Flags.is_set(Reflector::MethodFlags::Abstract))
			{
				ReportError(*method, "Abstract method '{}' in non-abstract class '{}'", FullType());
				return;
			}
		}
	}
}

Property& Class::EnsureProperty(std::string name)
{
	auto [it, added] = Properties.try_emplace(name, this);
	if (added)
	{
		it->second.Name = name;
		it->second.DisplayName = name;
	}
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
	MemberDeclaration::CreateArtificialMethodsAndDocument(options);

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

string_view TrimWhitespaceAndComments(std::string_view str)
{
	while (true)
	{
		trim_whitespace_left(str);
		if (str.starts_with("//"))
			return { str.end(), str.end() };
		if (consume(str, "/*"))
		{
			std::ignore = consume_until(str, "*/");
			std::ignore = consume(str, "*/");
		}
		else
			break;
	}
	return str;
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

std::string FormatAccess(AccessMode mode)
{
	switch (mode)
	{
	case AccessMode::Public: return "public: ";
	case AccessMode::Protected: return "protected: ";
	case AccessMode::Private: return "private: ";
	default:
		break;
	}
	return {};
}

std::string FormatPreFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except)
{
	using enum Reflector::MethodFlags;

	std::vector<std::string_view> prefixes;
	flags = flags - except;
	if (flags.is_set(NoDiscard)) prefixes.push_back("[[nodiscard]] ");
	if (flags.is_set(Inline)) prefixes.push_back("inline ");
	if (flags.is_set(Static)) prefixes.push_back("static ");
	if (flags.is_set(Virtual)) prefixes.push_back("virtual ");
	if (flags.is_set(Explicit)) prefixes.push_back("explicit ");
	return join(prefixes, "");
}

std::string FormatPostFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except)
{
	using enum Reflector::MethodFlags;

	std::vector<std::string_view> suffixes;
	flags = flags - except;
	if (flags.is_set(Const)) suffixes.push_back(" const");
	if (flags.is_set(Final)) suffixes.push_back(" final");
	if (flags.is_set(Noexcept)) suffixes.push_back(" noexcept");
	return join(suffixes, "");
}

std::vector<FileMirror const*> GetMirrors()
{
	std::unique_lock lock{ mirror_mutex };
	return ghassanpl::to<std::vector<FileMirror const*>>(Mirrors | std::views::transform([](auto& mir) { return (FileMirror const*)mir.get(); }));
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

void SortMirrors()
{
	std::unique_lock lock{ mirror_mutex };
	std::ranges::sort(Mirrors, [](auto& mirror_a, auto& mirror_b) { return mirror_a->SourceFilePath < mirror_b->SourceFilePath; });
}

void CreateArtificialMethodsAndDocument(Options const& options)
{
	std::unique_lock lock{ mirror_mutex };

	/// Creating artificial methods should be plenty fast, we don't need to do it multithreaded, and it's safer that way
	for (auto const& mirror : Mirrors)
		mirror->CreateArtificialMethodsAndDocument(options);
}

void SimpleDeclaration::ForEachCommentDirective(std::string_view directive_name, std::function<void(std::span<const std::string>)> callback) const
{
	const auto directive = std::format("@{}", directive_name);
	auto start_find = Comments.begin();
	const auto end_find = Comments.end();
	while (start_find != end_find)
	{
		auto directive_start = std::find_if(start_find, end_find, [&](std::string_view line) { return line.starts_with(directive); });
		if (directive_start == end_find) return;

		auto directive_end = std::find_if(std::next(directive_start), end_find, [&](std::string_view line) { return line.empty() || line.starts_with('@'); });

		callback({ directive_start, directive_end });

		start_find = directive_end;
	}
}

bool SimpleDeclaration::Document() const
{
	bool default_document = true;
	if (global_options->Documentation.HideUnimplemented && Unimplemented())
		default_document = false;
	return ForceDocument.value_or(default_document);
}

json SimpleDeclaration::ToJSON() const
{
	json result = json::object();
	result["Name"] = Name;
	if (!Comments.empty()) result["Comments"] = Comments;
	if (Document() != true) result["Document"] = false;
	if (Deprecation) result["Deprecation"] = Deprecation->empty() ? json{true} : json{ *Deprecation };
	if (!DocNotes.empty()) result["DocNotes"] = DocNotes;
	SetFlags(EntityFlags, result, "EntityFlags");
	return result;
}

json MethodParameter::ToJSON() const
{
	auto result = SimpleDeclaration::ToJSON();
	result["Type"] = Type;
	if (!Initializer.empty()) result["Initializer"] = Initializer;
	return result;
}

std::string Icon(std::string_view icon)
{
	if (icon.empty())
		return {};
	return format(R"(<i class="codicon codicon-{}"></i>)", icon);
}

std::string IconFor(DeclarationType type)
{
	static constexpr std::array<std::string_view, magic_enum::enum_count<DeclarationType>()> icons = {
		"symbol-field",
		"symbol-method",
		"symbol-property",
		"symbol-class",
		"symbol-enum",
		"symbol-enum-member",
		"symbol-namespace",
		"symbol-parameter",
		"symbol-parameter",
	};
	return Icon(icons[(int)type]);
}

struct LinkParts
{
	LinkParts(Declaration const& decl, LinkFlags flags)
		: Name(decl.Name)
		, Href(decl.FullName("."))
		, SourceDeclaration(decl)
	{
		if (flags.contain(LinkFlag::DeclarationType))
			this->DeclarationType = IconFor(decl.DeclarationType());
		if (decl.Deprecation) 
			LinkClasses.emplace_back("deprecated");
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
	std::string DeclarationType;
	Declaration const& SourceDeclaration;
};

std::string ConstructLink(LinkParts const& parts)
{
	if (parts.SourceDeclaration.Document())
	{
		return std::format("{9}<small class='specifiers'>{0}</small><a href='{1}.html' class='entitylink {2}'><small class='namespace'>{8}</small><small class='parent'>{3}</small>{4}{5}<small class='specifiers'>{6}</small></a><small class='membertype'>{7}</small>",
			parts.PreSpecifiers, /// 0
			parts.Href, /// 1
			join(parts.LinkClasses, " "), /// 2
			Escaped(parts.Parent), /// 3
			parts.Name, /// 4
			Escaped(parts.Parameters), /// 5
			parts.PostSpecifiers, /// 6
			parts.ReturnType, /// 7
			parts.Namespace, /// 8
			parts.DeclarationType
		);
	}
	else
	{
		return std::format("{9}<small class='specifiers'>{0}</small><span class='entitylink {2}'><small class='namespace'>{8}</small><small class='parent'>{3}</small>{4}{5}<small class='specifiers'>{6}</small></span><small class='membertype'>{7}</small>",
			parts.PreSpecifiers, /// 0
			parts.Href, /// 1
			join(parts.LinkClasses, " "), /// 2
			Escaped(parts.Parent), /// 3
			parts.Name, /// 4
			Escaped(parts.Parameters), /// 5
			parts.PostSpecifiers, /// 6
			parts.ReturnType, /// 7
			parts.Namespace, /// 8
			parts.DeclarationType
		);
	}
}

std::string Enumerator::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this, flags };
	if (flags.contain(LinkFlag::Parent)) parts.Parent = ParentType->Name + ".";
	return ConstructLink(parts);
}

std::string TypeDeclaration::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this, flags };
	//if (flags.contain(LinkFlag::SignatureSpecifiers)) {}
	//if (flags.contain(LinkFlag::Specifiers)) {}
	if (flags.contain(LinkFlag::Namespace) && !parts.Namespace.empty()) parts.Namespace = Namespace + "::";
	return ConstructLink(parts);
}

std::string HighlightTypes(std::string_view type_, TypeDeclaration const* search_context)
{	
	static const std::regex matcher{ R"(\w+)" };
	static const std::regex std_remover{ R"(\bstd::)" };
	static const std::set<std::string, std::less<>> keywords_to_highlight{ "void", "bool", "char", "wchar_t", "char32_t", "char8_t",
		"char16_t", "unsigned", "signed", "long", "short", "int", "float", "double", "auto",
		"const",  };
	static const std::set<std::string, std::less<>> types_to_highlight{ "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
		"intptr_t", "uintptr_t", "pair", "tuple", "optional", "variant", "map", "vector", "set", "string", "json", "path", "Reflectable"};

	/// TODO: Add the unqualified name of the json type given in options
	
	std::string result;
	
	std::string clean_type = global_options->Documentation.RemoveStdNamespace
		? std::regex_replace(std::string{ type_ }, std_remover, "")
		: std::string{ type_ };

	auto words_begin = std::sregex_iterator(clean_type.begin(), clean_type.end(), matcher);
	auto words_end = std::sregex_iterator{};
	size_t end_of_last = 0;

	for (auto&& i = words_begin; i != words_end; ++i)
	{
		result += Escaped(i->prefix().str());

		auto potential_type = i->str();

		if (auto word_type = TypeDeclaration::FindTypeByPossiblyQualifiedName(potential_type, search_context))
			potential_type = word_type->MakeLink();
		else if (keywords_to_highlight.contains(potential_type))
			potential_type = std::format(R"(<span class="hljs-keyword">{}</span>)", potential_type);
		else if (types_to_highlight.contains(potential_type))
			potential_type = std::format(R"(<span class="hljs-type">{}</span>)", potential_type);
		
		result += potential_type;

		end_of_last = i->position() + i->length();
	}

	result += clean_type.substr(end_of_last);

	return result;
}

std::string Field::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this, flags };
	if (flags.contain(LinkFlag::Parent)) parts.Parent = ParentType->Name + ".";
	if (flags.contain(LinkFlag::ReturnType)) parts.ReturnType = HighlightTypes(Type, ParentType);
	return ConstructLink(parts);
}

std::string Method::MakeLink(LinkFlags flags) const
{
	using enum Reflector::MethodFlags;
	LinkParts parts{ *this, flags };
	if (flags.contain(LinkFlag::Parent)) parts.Parent = ParentType->Name + "::";
	if (flags.contain(LinkFlag::SignatureSpecifiers)) parts.PostSpecifiers = FormatPostFlags(Flags, { Final, Noexcept });
	if (flags.contain(LinkFlag::Specifiers)) parts.PreSpecifiers = FormatPreFlags(Flags, { Inline, NoDiscard });
	if (flags.contain(LinkFlag::ReturnType) && Return.Name != "void") parts.ReturnType = " -> " + HighlightTypes(Return.Name, ParentType);

	std::vector<std::string> par_types;
	for (auto& param : ParametersSplit)
		par_types.push_back(HighlightTypes(param.Type, ParentType));

	if (flags.contain(LinkFlag::Parameters)) parts.Parameters = std::format("({})", join(par_types));
	return ConstructLink(parts);
}

std::string Property::MakeLink(LinkFlags flags) const
{
	LinkParts parts{ *this, flags };
	if (flags.contain(LinkFlag::Parent)) parts.Parent = ParentType->Name + ".";
	if (flags.contain(LinkFlag::ReturnType)) parts.ReturnType = HighlightTypes(Type, ParentType);
	return ConstructLink(parts);
}

void BaseMemberDeclaration::CreateArtificialMethodsAndDocument(Options const& options)
{
	Declaration::CreateArtificialMethodsAndDocument(options);
	//Document = Attribute::Document.GetOr(*this, ParentDecl()->DocumentMembers);
}

bool BaseMemberDeclaration::Document() const
{
	bool default_document = ParentDecl()->DocumentMembers;
	if (global_options->Documentation.HideUnimplemented && Unimplemented())
		default_document = false;
	return ForceDocument.value_or(default_document);
}
