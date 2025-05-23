/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Attributes.h"
#include "Documentation.h"
#include "Declarations.h"

bool AttributeProperties::AppliesTo(Declaration const& decl) const { return mValidTargets.contain(decl.DeclarationType()); }

std::vector<std::string_view> AttributeProperties::FindUnsettable(json const& attrs)
{
	std::vector<std::string_view> result;
	for (auto const& attr : mAllAttributes)
	{
		if (attr->mFlags.contain(AttributePropertyFlags::NotUserSettable) && attr->ExistsIn(attrs))
			result.push_back(attr->Name());
	}
	return result;
}

expected<void, std::string> AttributeProperties::Validate(json const& attr_value, Declaration const& decl) const
{
	if (!AppliesTo(decl))
		return tl::unexpected(std::format("`{}` attribute only applies on the following entities: {}", Name(), join(mValidTargets, ", ", [](auto e) { return magic_enum::enum_name(e); })));
	if (mValidator)
		return mValidator(attr_value, decl);
	return {};
}

std::optional<std::string> AttributeProperties::ExistsIn(Declaration const& decl) const
{
	return ExistsIn(decl.Attributes);
}

std::optional<std::string> AttributeProperties::ExistsIn(json const& attrs) const
{
	if (const auto it = std::ranges::find_if(mValidNames, [&](std::string const& name) { return attrs.contains(name); }); it != mValidNames.end())
		return *it;
	return std::nullopt;
}

void AttributeProperties::ValidateThrowing(json const& attr_value, Declaration const& decl) const
{
	if (auto exp = Validate(attr_value, decl); !exp)
		ReportError(decl, "Invalid attribute '{}': {}", Name(), std::move(exp).error());
}

json const* AttributeProperties::Find(Declaration const& decl, bool validate) const
{
	for (auto& name : mValidNames)
	{
		if (auto it = decl.Attributes.find(name); it != decl.Attributes.end() && !it->is_null())
		{
			if (validate)
				ValidateThrowing(*it, decl);
			return std::to_address(it);
		}
	}
	return nullptr;
}

/// Simple aliases for common sets of attribute targets
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

AttributeValidatorFunc IsString = [](json const& attr_value, Declaration const&) -> expected<void, std::string> {
	if (!attr_value.is_string())
		return tl::unexpected("must be a string"s);
	return {};
};

AttributeValidatorFunc IsBoolOrString = [](json const& attr_value, Declaration const&) -> expected<void, std::string> {
	if (!attr_value.is_string() && !attr_value.is_boolean())
		return tl::unexpected("must be a string or boolean"s);
	return {};
};

AttributeValidatorFunc NotEmptyString = [](json const& attr_value, Declaration const& on_decl) -> expected<void, std::string> {
	if (auto err = IsString(attr_value, on_decl); !err) return err;
	if (attr_value.get_ref<std::string const&>().empty())
		return tl::unexpected(", if set, cannot be empty"s);
	return {};
};

AttributeValidatorFunc IsReflectedEnum = [](json const& attr_value, Declaration const& on_decl) -> expected<void, std::string> {
	if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
	if (FindEnum(attr_value) != nullptr)
		return {};
	return tl::unexpected(format("must name a reflected enum; '{}' is not a reflected enum.", attr_value.get_ref<std::string const&>()));
};

AttributeValidatorFunc IsIdentifier = [](json const& attr_value, Declaration const& on_decl)->expected<void, std::string> {
	if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
	if (std::string_view identifier{attr_value}; !ascii::is_identifier(identifier))
		return tl::unexpected(format("must be a valid C++ identifier", identifier));
	return {};
};

AttributeValidatorFunc NoValidator{};

const StringAttributeProperties Attribute::DisplayName {
	"DisplayName",
	"The name that is going to be displayed in editors and such",
	Targets::Any
};

const StringAttributeProperties Attribute::SaveName {
	"SaveName",
	"The name that this field will be saved with; can be used to rename fields without losing alraedy serialized data",
	Targets::Fields
};

/// TODO: This should become LoadName with "LoadNames;LoadName", and be a ';'-separated list of names to try to load from
const StringAttributeProperties Attribute::LoadName {
	"LoadName",
	"The name that this field will be loaded from; can be used to rename fields without losing alraedy serialized data",
	Targets::Fields
};

const StringAttributeProperties Attribute::Namespace {
	"Namespace",
	"A helper since we don't parse namespaces; set this to the full namespace of the following type, otherwise you might get errors",
	Targets::Types,
	[](json const& attr_value, Declaration const& on_decl) -> expected<void, std::string> {
		if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
		auto namespaces = split(attr_value.get_ref<std::string const&>(), "::");
		for (auto& ns : namespaces)
		{
			if (!ascii::is_identifier(ns))
				return tl::unexpected(format("must be a valid namespace name ('{}' unexpected)", ns));
		}
		return {};
	}
};

const StringAttributeProperties Attribute::GUID {
	"GUID",
	"A globaly-unique ID for this type. Can aid with renaming.",
	Targets::Types,
	NotEmptyString
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
	"Whether or not this entity should be editable",
	Targets::Fields + Targets::Classes,
	true
};
const BoolAttributeProperties Attribute::Script {
	"Script;Scriptable",
	"Whether or not this field should be accessible via script",
	Targets::Members,
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
	"Document;Doc",
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

const BoolAttributeProperties Attribute::Serialize{ "Serialize", "False means both 'Save' and 'Load' are false", {DeclarationType::Field, DeclarationType::Class}, true };
const BoolAttributeProperties Attribute::Private{ "Private", "True sets 'Edit', 'Setter', 'Getter' to false", Targets::Fields, false };
const BoolAttributeProperties Attribute::Transient{ "Transient", "True sets 'Setter' and 'Serialize' to false", Targets::Fields, false };
const BoolAttributeProperties Attribute::ScriptPrivate{ "ScriptPrivate", "True sets 'Setter', 'Getter' to false", Targets::Fields, false };

/// TODO: Add AMs and docnotes for this
//const BoolAttributeProperties Attribute::ParentPointer{ "ParentPointer", "Whether or not this field is a pointer to parent object, in a tree hierarchy; implies Edit = false, Setter = false", Targets::Fields, false };

const BoolAttributeProperties Attribute::Required {
	"Required",
	"The marked field is required to be present when deserializing class",
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
	"Executes the given code when this field changes via setter functions",
	Targets::Fields,
	IsString,
};

constexpr auto FlagsCat = "Flags"_ac;

/// TODO: We could make `Requires(attr)` and `Excludes(attr)` validators, and a function
/// that concats validators; this way we can enforce FlagGetters/FlagNots/etc. early

const StringAttributeProperties Attribute::FlagGetters {
	"FlagGetters",
	"If set to an (reflected) enum name, creates public getter functions (IsFlag) for each Flag in the enum, and private setters; can't be set if the 'Flags' attribute is set",
	Targets::Fields,
	IsReflectedEnum,
	FlagsCat
};
const StringAttributeProperties Attribute::Flags {
	"Flags",
	"If set to an (reflected) enum name, creates public getter and setter functions (IsFlag, SetFlag, UnsetFlag, ToggleFlag) for each Flag in the enum; can't be set if the 'FlagGetters' attribute is set",
	Targets::Fields,
	IsReflectedEnum,
	FlagsCat
};
const BoolAttributeProperties Attribute::FlagNots {
	"FlagNots",
	"Requires 'Flags' attribute. If set, creates IsNotFlag functions in addition to regular IsFlag (except for enumerators with Opposite attribute).",
	Targets::Fields,
	true,
	FlagsCat
};
/// TODO: SkipFlags=[Black, Blue] for Flags fields

const StringAttributeProperties Attribute::UniqueName {
	"UniqueName",
	"A unique (within this class) name of this method; useful when script-binding overloaded functions to languages without overloading",
	Targets::Methods,
	IsIdentifier
};
const StringAttributeProperties Attribute::ScriptName {
	"ScriptName",
	"The name of this class member that will be used in scripts",
	Targets::Members,
	IsIdentifier
};

const StringAttributeProperties Attribute::GetterFor {
	"GetterFor",
	"This function is a getter for the named field; useful when you are binding Property accessors to scripts",
	Targets::Methods,
};
const StringAttributeProperties Attribute::SetterFor {
	"SetterFor",
	"This function is a setter for the named field; useful when you are binding Property accessors to scripts",
	Targets::Methods,
};
const BoolAttributeProperties Attribute::Property{
	"Property",
	"For methods, if true, is equivalent to `GetterFor = <methodname>`. For fields, if false, will not create a property for this field if it would otherwise have been.",
	Targets::Members,
};

/// TODO: Should this also be a flag?
const BoolAttributeProperties Attribute::Abstract {
	"Abstract;Interface",
	"This class is abstract (don't create special constructors)",
	Targets::Classes,
	false
};

const BoolAttributeProperties Attribute::Singleton {
	"Singleton",
	"This class is a singleton. Adds a static function (default name 'SingletonInstance') that returns the single instance of this record. Note that for now, GC-enabled classes cannot be singletons.",
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

const BoolAttributeProperties Attribute::Unimplemented {
	"Unimplemented",
	"The functionality this entity represents is not implemented; mostly useful for documentation, but can generate some warnings",
	Targets::Any,
	false
};

/// TODO: This will also generate a enum class type that represents the unique IDs (`ClassNameID` or controlled by options: `{}_id`)
const StringAttributeProperties Attribute::UniqueID {
	"UniqueID",
	"If set, will create a unique ID field with the given name, and a generator function, for this class",
	Targets::Classes,
	IsIdentifier
};

constexpr auto EnumerationsCat = "Enumerations"_ac;

const BoolAttributeProperties Attribute::List {
	"List;Sequence",
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
const BoolAttributeProperties Attribute::AliasEnum{
	"AliasEnum",
	"The marked enum is not meant as a container for enumerators, but as an strong type alias for another integral type",
	Targets::Enums,
	false,
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

/// TODO: Should deprecation imply Save=false ?
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