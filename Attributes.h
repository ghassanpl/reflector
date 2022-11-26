#pragma once

#include "Common.h"

enum class AttributeFor
{
	Record,
	Class,
	Struct,
	Enum,
	Field,
};

struct AttributeProperties
{
	std::string Name;
	std::string Description;
	json::value_t ValueType{};
	json DefaultValueIfAny = nullptr;

	AttributeProperties(std::string name, std::string desc, json::value_t type, json default_value = nullptr) noexcept
		: Name(move(name))
		, Description(move(desc))
		, ValueType(type)
		, DefaultValueIfAny(std::move(default_value))
	{

	}
	AttributeProperties(std::string name, AttributeProperties const& copy) noexcept
		: AttributeProperties(copy)
	{
		Name = std::move(name);
	}

	operator std::string const&() const noexcept { return Name; }
	auto operator <=>(std::string const& other) const noexcept { return Name <=> other; }

	template <typename T>
	auto operator()(json const& attrs, T&& default_value)
	{
		return attrs.value(this->Name, std::forward<T>(default_value));
	}

private:

	AttributeProperties(AttributeProperties const& other) noexcept = default;
};

inline const AttributeProperties atFieldGetter{ "Getter", "Whether or not to create a getter for this field", json::value_t::boolean, true };
inline const AttributeProperties atFieldSetter{ "Setter", "Whether or not to create a setter for this field", json::value_t::boolean, true };
inline const AttributeProperties atFieldEditor{ "Editor", "Whether or not this field should be editable", json::value_t::boolean, true };
inline const AttributeProperties atFieldEdit{ "Edit", atFieldEditor };
inline const AttributeProperties atFieldSave{ "Save", "Whether or not this field should be saveable", json::value_t::boolean, true };
inline const AttributeProperties atFieldLoad{ "Load", "Whether or not this field should be loadable", json::value_t::boolean, true };

inline const AttributeProperties atFieldSerialize{ "Serialize", "False means both 'Save' and 'Load' are false", json::value_t::boolean, true };
inline const AttributeProperties atFieldPrivate{ "Private", "True sets 'Edit', 'Setter', 'Getter' to false", json::value_t::boolean, false };
inline const AttributeProperties atFieldParentPointer{ "ParentPointer", "Whether or not this field is a pointer to parent object, in a tree hierarchy; implies Edit = false, Setter = false", json::value_t::boolean, false };

inline const AttributeProperties atFieldOnChange{ "OnChange", "Calls the specified method when this field changes", json::value_t::string };

inline const AttributeProperties atFieldFlagGetters{ "FlagGetters", "If set to an (reflected) enum name, creates getter functions (IsFlag) for each Flag in this field; can't be set if 'Flags' is set", json::value_t::string };
inline const AttributeProperties atFieldFlags{ "Flags", "If set to an (reflected) enum name, creates getter and setter functions (SetFlag, UnsetFlag, ToggleFlag) for each Flag in this field; can't be set if 'FlagGetters' is set", json::value_t::string };

inline const AttributeProperties atMethodUniqueName{ "UniqueName", "A unique (for this type) name of this method; useful for overloaded functions", json::value_t::string };
inline const AttributeProperties atMethodGetterFor{ "GetterFor", "This function is a getter for the named field", json::value_t::string };
inline const AttributeProperties atMethodSetterFor{ "SetterFor", "This function is a setter for the named field", json::value_t::string };

inline const AttributeProperties atRecordAbstract{ "Abstract", "This record is abstract (don't create special constructors)", json::value_t::boolean, false };
inline const AttributeProperties atRecordSingleton{ "Singleton", "This record is a singleton ", json::value_t::boolean, false };

inline const AttributeProperties atRecordCreateProxy{ "CreateProxy", "If set to false, proxy classes are not built for classes with virtual methods", json::value_t::boolean, true };