#include "Attributes.h"
#include "Documentation.h"

Enum const* FindEnum(string_view name);

std::vector<std::string_view> AttributeProperties::FindUnsettable(json const& attrs)
{
	std::vector<std::string_view> result;
	for (auto& attr : AllAttributes)
	{
		if (attr->ExistsIn(attrs) && attr->Flags.contain(AttributePropertyFlags::NotUserSettable))
			result.push_back(attr->Name());
	}
	return result;
}

std::expected<void, std::string> AttributeProperties::Validate(json const& attr_value, Declaration const& decl) const
{
	if (!AppliesTo(decl))
		return std::unexpected(std::format("`{}` attribute only applies on the following entities: {}", Name(), join(ValidTargets, ", ", [](auto e) { return magic_enum::enum_name(e); })));
	if (Validator)
		return Validator(attr_value, decl);
	return {};
}

std::optional<std::string> AttributeProperties::ExistsIn(Declaration const& decl) const
{
	return ExistsIn(decl.Attributes);
}

std::optional<std::string> AttributeProperties::ExistsIn(json const& attrs) const
{
	if (const auto it = std::ranges::find_if(ValidNames, [&](std::string const& name) { return attrs.contains(name); }); it != ValidNames.end())
		return *it;
	return std::nullopt;
}

namespace Targets
{
	enum_flags Enums = DeclarationType::Enum;
	enum_flags Fields = DeclarationType::Field;
	enum_flags Methods = DeclarationType::Method;
	enum_flags Classes = DeclarationType::Class;
	enum_flags Members { DeclarationType::Method, DeclarationType::Field };
	enum_flags Enumerators = DeclarationType::Enumerator;
	enum_flags Any = enum_flags<DeclarationType>::all();
	enum_flags Types { DeclarationType::Class, DeclarationType::Enum };
}

AttributeValidatorFunc IsString = [](json const& attr_value, Declaration const& on_decl) -> std::expected<void, std::string> {
	if (!attr_value.is_string())
		return std::unexpected("must be a string"s);
	return {};
};

AttributeValidatorFunc IsBoolOrString = [](json const& attr_value, Declaration const& on_decl) -> std::expected<void, std::string> {
	if (!attr_value.is_string() && !attr_value.is_boolean())
		return std::unexpected("must be a string or boolean"s);
	return {};
};

AttributeValidatorFunc NotEmptyString = [](json const& attr_value, Declaration const& on_decl) -> std::expected<void, std::string> {
	if (auto err = IsString(attr_value, on_decl); !err) return err;
	if (attr_value.get_ref<std::string const&>().empty())
		return std::unexpected(", if set, cannot be empty"s);
	return {};
};

AttributeValidatorFunc IsReflectedEnum = [](json const& attr_value, Declaration const& on_decl) -> std::expected<void, std::string> {
	if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
	if (FindEnum(attr_value) != nullptr)
		return {};
	return std::unexpected(format("must name a reflected enum; '{}' is not a reflected enum.", attr_value.get_ref<std::string const&>()));
};

AttributeValidatorFunc IsIdentifier = [](json const& attr_value, Declaration const& on_decl)->std::expected<void, std::string> {
	if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
	std::string_view identifier{attr_value};
	if (!string_ops::ascii::is_identifier(identifier))
		return std::unexpected(format("must be a valid C++ identifier", identifier));
	return {};
};

AttributeValidatorFunc NoValidator{};

const StringAttributeProperties Attribute::DisplayName {
	"DisplayName",
	"The name that is going to be displayed in editors and such",
	Targets::Any
};

const StringAttributeProperties Attribute::Namespace {
	"Namespace",
	"A helper since we don't parse namespaces; set this to the full namespace of the following type, otherwise you might get errors",
	Targets::Types,
	[](json const& attr_value, Declaration const& on_decl) -> std::expected<void, std::string> {
		if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
		auto namespaces = string_ops::split(attr_value.get_ref<std::string const&>(), "::");
		for (auto& ns : namespaces)
		{
			if (!string_ops::ascii::is_identifier(ns))
				return std::unexpected(format("must be a valid namespace ('{}' unexpected)", ns));
		}
		return {};
	}
};

/// TODO: Maybe instead of Getter and Setter, just have one attribute - ReadOnly - which also influences whether or not other setters are created (like flag, vector, optional setters)

const BoolAttributeProperties Attribute::Getter {
	"Getter",
	"Whether or not to create a getter for this field",
	Targets::Fields,
	true
};
const BoolAttributeProperties Attribute::Setter {
	"Setter",
	"Whether or not to create a setter for this field",
	Targets::Fields,
	true
};
const BoolAttributeProperties Attribute::Editor {
	"Editor;Edit",
	"Whether or not this field should be editable",
	Targets::Fields,
	true
};
const BoolAttributeProperties Attribute::Save {
	"Save",
	"Whether or not this field should be saveable",
	Targets::Fields,
	true
};
const BoolAttributeProperties Attribute::Load {
	"Load",
	"Whether or not this field should be loadable",
	Targets::Fields,
	true
};
const BoolAttributeProperties Attribute::Document {
	"Document",
	"Whether or not to create a documentation entry for this entity",
	Targets::Any,
	true
};

const BoolAttributeProperties Attribute::DocumentMembers {
	"DocumentMembers",
	"Whether or not to create documentation entries for members of this entity",
	Targets::Types,
	true
};

const BoolAttributeProperties Attribute::Serialize{ "Serialize", "False means both 'Save' and 'Load' are false", Targets::Fields, true };
const BoolAttributeProperties Attribute::Private{ "Private", "True sets 'Edit', 'Setter', 'Getter' to false", Targets::Fields, false };
const BoolAttributeProperties Attribute::Transient{ "Transient", "True sets 'Setter' and 'Serialize' to false", Targets::Fields, false }; /// TODO: This
const BoolAttributeProperties Attribute::ScriptPrivate{ "ScriptPrivate", "True sets 'Setter', 'Getter' to false", Targets::Fields, false };

/// TODO: Add AMs and docnotes for this
//const BoolAttributeProperties Attribute::ParentPointer{ "ParentPointer", "Whether or not this field is a pointer to parent object, in a tree hierarchy; implies Edit = false, Setter = false", Targets::Fields, false };

const BoolAttributeProperties Attribute::Required {
	"Required",
	"A helper flag for the serialization system - will set the FieldFlags::Required flag",
	Targets::Fields,
	false
};

const BoolAttributeProperties Attribute::PrivateGetters {
	"PrivateGetters",
	"Whether to generate getters for private members",
	Targets::Classes,
	true
};

const BoolAttributeProperties Attribute::PrivateSetters {
	"PrivateSetters",
	"Whether to generate setters for private members",
	Targets::Classes,
	false
};

const StringAttributeProperties Attribute::OnChange {
	"OnChange",
	"Executes the given code when this field changes",
	Targets::Fields,
	NoValidator /// It can be empty, because it is code
};

const StringAttributeProperties Attribute::FlagGetters {
	"FlagGetters",
	"If set to an (reflected) enum name, creates public getter functions (IsFlag) for each Flag in this field, and private setters; can't be set if 'Flags' is set",
	Targets::Fields,
	IsReflectedEnum
};
const StringAttributeProperties Attribute::Flags {
	"Flags",
	"If set to an (reflected) enum name, creates public getter and setter functions (IsFlag, SetFlag, UnsetFlag, ToggleFlag) for each Flag in this field; can't be set if 'FlagGetters' is set",
	Targets::Fields,
	IsReflectedEnum
};
const BoolAttributeProperties Attribute::FlagNots {
	"FlagNots",
	"Requires 'Flags' attribute. If set, creates IsNotFlag functions in addition to regular IsFlag (except for enumerators with Opposite attribute).",
	Targets::Fields,
	true
};

const StringAttributeProperties Attribute::UniqueName {
	"UniqueName",
	"A unique (for this type) name of this method; useful for overloaded functions",
	Targets::Methods,
	IsIdentifier
};

const StringAttributeProperties Attribute::GetterFor {
	"GetterFor",
	"This function is a getter for the named field",
	Targets::Methods,
};
const StringAttributeProperties Attribute::SetterFor {
	"SetterFor",
	"This function is a setter for the named field",
	Targets::Methods,
};

/// TODO: Should this also be a flag?
const BoolAttributeProperties Attribute::Abstract {
	"Abstract",
	"This record is abstract (don't create special constructors)",
	Targets::Classes,
	false
};

const BoolAttributeProperties Attribute::Singleton {
	"Singleton",
	"This record is a singleton. Adds a static function (default name 'SingletonInstance') that returns the single instance of this record",
	Targets::Classes,
	false
};

const AttributeProperties Attribute::DefaultFieldAttributes {
	"DefaultFieldAttributes",
	"These attributes will be added as default to every reflected field of this class",
	Targets::Classes,
	json::object(),
};

const AttributeProperties Attribute::DefaultMethodAttributes {
	"DefaultMethodAttributes",
	"These attributes will be added as default to every reflected method of this class",
	Targets::Classes,
	json::object()
};

const AttributeProperties Attribute::DefaultEnumeratorAttributes {
	"DefaultEnumeratorAttributes",
	"These attributes will be added as default to every enumerator of this enum",
	Targets::Classes,
	json::object()
};

const BoolAttributeProperties Attribute::CreateProxy {
	"CreateProxy",
	"Whether or not proxy methods should be built for this class",
	Targets::Classes,
	true
};

constexpr auto EnumerationsCat = "Enumerations"_ac;

const BoolAttributeProperties Attribute::List {
	"List",
	"Whether or not to generate GetNext() and GetPrev() functions that return the next/prev enumerator in sequence, wrapping around",
	Targets::Enums,
	false,
	EnumerationsCat
};
const StringAttributeProperties Attribute::Opposite {
	"Opposite",
	"When used in a Flag enum, will create a virtual flag with the given name that is the complement of this one, for the purposes of creating getters and setters",
	Targets::Enumerators,
	IsIdentifier,
	EnumerationsCat
};

constexpr auto CppAttributesCat = "C++ Attributes"_ac;

/// TODO: Check for these and output flags/docnotes
const BoolAttributeProperties Attribute::NoReturn {
	"NoReturn",
	"Do not set this directly. Use [[noreturn]] instead.",
	Targets::Methods,
	false,
	AttributePropertyFlags::NotUserSettable,
	CppAttributesCat,
};
const BoolAttributeProperties Attribute::Deprecated {
	"Deprecated",
	"Do not set this directly. Use [[deprecated]] instead.",
	Targets::Any,
	false,
	AttributePropertyFlags::NotUserSettable,
	IsBoolOrString,
	CppAttributesCat
};
const BoolAttributeProperties Attribute::NoDiscard {
	"NoDiscard",
	"Do not set this directly. Use [[nodiscard]] instead.",
	{ DeclarationType::Class, DeclarationType::Enum, DeclarationType::Method },
	false,
	AttributePropertyFlags::NotUserSettable,
	IsBoolOrString,
	CppAttributesCat
};
const BoolAttributeProperties Attribute::NoUniqueAddress { "NoUniqueAddress", 
	"Do not set this directly. Use [[no_unique_address]] instead.", 
	Targets::Fields, 
	false, 
	AttributePropertyFlags::NotUserSettable,
	CppAttributesCat
};