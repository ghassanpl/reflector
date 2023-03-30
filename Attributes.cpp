#include "Attributes.h"

Enum const* FindEnum(string_view name);

namespace Targets
{
	enum_flags<AttributeTarget> Enums = AttributeTarget::Enum;
	enum_flags<AttributeTarget> Fields = AttributeTarget::Field;
	enum_flags<AttributeTarget> Methods = AttributeTarget::Method;
	enum_flags<AttributeTarget> Classes = AttributeTarget::Class;
	enum_flags<AttributeTarget> Members { AttributeTarget::Method, AttributeTarget::Field };
	enum_flags<AttributeTarget> Enumerators { AttributeTarget::Enumerator };
	enum_flags<AttributeTarget> Any = enum_flags<AttributeTarget>::all();
	enum_flags<AttributeTarget> Types { AttributeTarget::Class, AttributeTarget::Enum };
}

AttributeValidatorFunc IsString = [](json const& attr_value, Declaration const& on_decl) -> expected<void, std::string> {
	if (!attr_value.is_string())
		return unexpected("must be a string"s);
	return {};
};

AttributeValidatorFunc NotEmptyString = [](json const& attr_value, Declaration const& on_decl) -> expected<void, std::string> {
	if (auto err = IsString(attr_value, on_decl); !err) return err;
	if (attr_value.get_ref<std::string const&>().empty())
		return unexpected(", if set, cannot be empty"s);
	return {};
};

AttributeValidatorFunc IsReflectedEnum = [](json const& attr_value, Declaration const& on_decl) -> expected<void, std::string> {
	if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
	if (auto henum = FindEnum(attr_value))
		return {};
	return unexpected(format("must name a reflected enum; '{}' is not a reflected enum.", attr_value.get_ref<std::string const&>()));
};

AttributeValidatorFunc NoValidator{};

const StringAttributeProperties Attribute::DisplayName {
	"DisplayName",
	"The name that is going to be displayed in editors and such",
	Targets::Any
};

const StringAttributeProperties Attribute::Namespace{
	"Namespace",
	"A helper since we don't parse namespaces; set this to the full namespace of the following type, otherwise you might get errors",
	Targets::Types,
	[](json const& attr_value, Declaration const& on_decl) -> expected<void, std::string> {
		if (auto err = NotEmptyString(attr_value, on_decl); !err) return err;
		auto namespaces = string_ops::split(attr_value.get_ref<std::string const&>(), "::");
		for (auto& ns : namespaces)
		{
			if (!string_ops::ascii::is_identifier(ns))
				return unexpected(format("must be a valid namespace ('{}' unexpected)", ns));
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

const BoolAttributeProperties Attribute::Serialize{ "Serialize", "False means both 'Save' and 'Load' are false", Targets::Fields, true };
const BoolAttributeProperties Attribute::Private{ "Private", "True sets 'Edit', 'Setter', 'Getter' to false", Targets::Fields, false };
const BoolAttributeProperties Attribute::ParentPointer{ "ParentPointer", "Whether or not this field is a pointer to parent object, in a tree hierarchy; implies Edit = false, Setter = false", Targets::Fields, false };

const BoolAttributeProperties Attribute::Required {
	"Required",
	"A helper flag for the serialization system - will set the FieldFlags::Required flag",
	Targets::Fields,
	false,
	[](json const& value, Declaration const& decl) {
		if (!value) return std::string{};
		return std::format("This field is required to be present when deserializing class {}.", static_cast<Field const&>(decl).ParentType->Name);
	}
};

const StringAttributeProperties Attribute::OnChange { 
	"OnChange",
	"Executes the given code when this field changes", 
	Targets::Fields,
	[](json const& value, Declaration const& decl) { 
		return std::format("When this field is changed (via its setter and other such functions), the following code will be executed: `{}`", value.get<std::string>()); 
	},
	NoValidator /// It can be empty, because it is code
};

const StringAttributeProperties Attribute::FlagGetters {
	"FlagGetters",
	"If set to an (reflected) enum name, creates getter functions (IsFlag) for each Flag in this field; can't be set if 'Flags' is set",
	Targets::Fields,
	IsReflectedEnum
};
const StringAttributeProperties Attribute::Flags {
	"Flags",
	"If set to an (reflected) enum name, creates getter and setter functions (IsFlag, SetFlag, UnsetFlag, ToggleFlag) for each Flag in this field; can't be set if 'FlagGetters' is set",
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
	[](json const& value, Declaration const& decl) { 
		return std::format("This method's unique name will be `{}` in scripts.", value.get<std::string>()); 
	}
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
	false,
	[](json const& value, Declaration const& decl) {
		if (!value) return ""s; 
		return "This class is a singleton. Call @see SingletonInstance to get the instance."s; /// TODO: Instead of hardcoding the SingletonInstance name, get it from options
	}
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
	"If set to false, a proxy classes will not be built for this class, even if it has virtual methods",
	Targets::Classes,
	true
};

const BoolAttributeProperties Attribute::List {
	"List",
	"If set to true, generates GetNext() and GetPrev() functions that return the next/prev enumerator in sequence, wrapping around",
	Targets::Enums,
	false
};
const StringAttributeProperties Attribute::Opposite {
	"Opposite",
	"When used in a Flag enum, will create a virtual flag with the given name that is the complement of this one, for the purposes of creating getters and setters",
	Targets::Enumerators,
};