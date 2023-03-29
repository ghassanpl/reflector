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

using AttributeDocFunc = std::function<std::string(json const&, Declaration const&)>;

struct AttributeProperties
{
	std::string Name;
	std::string Description;
	json::value_t ValueType{};
	json DefaultValueIfAny = nullptr;

	AttributeDocFunc DocumentationDescriptionGenerator;

	static inline std::vector<AttributeProperties const*> AllAttributes;

	AttributeProperties(std::string name, std::string desc, json::value_t type, json default_value = nullptr, AttributeDocFunc docfunc = {}) noexcept
		: Name(move(name))
		, Description(move(desc))
		, ValueType(type)
		, DefaultValueIfAny(std::move(default_value))
		, DocumentationDescriptionGenerator(std::move(docfunc))
	{
		AllAttributes.push_back(this);
	}

	AttributeProperties(std::string name, AttributeProperties const& copy) noexcept
		: AttributeProperties(copy)
	{
		Name = std::move(name);
	}

	//operator std::string const&() const noexcept { return Name; }
	//auto operator <=>(std::string const& other) const noexcept { return Name <=> other; }

	bool ExistsIn(json const& attrs) const
	{
		return attrs.contains(this->Name);
	}

	template <typename T>
	auto operator()(json const& attrs, T&& default_value) const
	{
		const auto it = attrs.find(this->Name);
		if (it == attrs.end())
			return std::forward<T>(default_value);
		if constexpr (std::is_same_v<T, bool>)
			return it->is_null() ? true : (bool)*it;
		else
			return (T)*it;
	}

	template <typename T>
	bool TryGet(json const& attrs, T& dest) const
	{
		const auto it = attrs.find(this->Name);
		if (it == attrs.end())
			return false;
		if constexpr (std::is_same_v<T, bool>)
			dest = it->is_null() ? true : (bool)*it;
		else
			dest = (T)*it;
		return true;
	}

	json const& TryGet(json const& attrs) const
	{
		static const json empty_json;
		const auto it = attrs.find(this->Name);
		if (it == attrs.end())
			return empty_json;
		return *it;
	}

private:

	AttributeProperties(AttributeProperties const& other) noexcept = default;
};

/// TODO: Move these to a .cpp file. Since we're autoregistering them anyway, might as well output the documentation for these at bootstrap.

/// TODO: Change these to static fields of a reflected class (or classes), so that documentation is generated

inline const AttributeProperties atDisplayName{ "DisplayName", "The name that is going to be displayed in editors and such", json::value_t::string };

inline const AttributeProperties atTypeNamespace{ "Namespace", "A helper since we don't parse namespaces; set this to the full namespace of the following type, otherwise you might get errors", json::value_t::string };

/// TODO: Maybe instead of Getter and Setter, just have one attribute - ReadOnly - which also influences whether or not other setters are created (like flag, vector, optional setters)

inline const AttributeProperties atFieldGetter{ "Getter", "Whether or not to create a getter for this field", json::value_t::boolean, true };
inline const AttributeProperties atFieldSetter{ "Setter", "Whether or not to create a setter for this field", json::value_t::boolean, true };
inline const AttributeProperties atFieldEditor{ "Editor", "Whether or not this field should be editable", json::value_t::boolean, true };
inline const AttributeProperties atFieldEdit{ "Edit", atFieldEditor };
inline const AttributeProperties atFieldSave{ "Save", "Whether or not this field should be saveable", json::value_t::boolean, true };
inline const AttributeProperties atFieldLoad{ "Load", "Whether or not this field should be loadable", json::value_t::boolean, true };

inline const AttributeProperties atFieldSerialize{ "Serialize", "False means both 'Save' and 'Load' are false", json::value_t::boolean, true };
inline const AttributeProperties atFieldPrivate{ "Private", "True sets 'Edit', 'Setter', 'Getter' to false", json::value_t::boolean, false };
inline const AttributeProperties atFieldParentPointer{ "ParentPointer", "Whether or not this field is a pointer to parent object, in a tree hierarchy; implies Edit = false, Setter = false", json::value_t::boolean, false };
inline const AttributeProperties atFieldRequired{ "Required", "A helper flag for the serialization system - will set the FieldFlags::Required flag", json::value_t::boolean, false,
	[](json const& value, Declaration const& decl) { if (value) { return std::format("This field is required to be present when deserializing class {}.", static_cast<Field const&>(decl).ParentClass->Name); } return std::string{}; } 
};

inline const AttributeProperties atFieldOnChange{ "OnChange", "Calls the specified method when this field changes", json::value_t::string, {},
	[](json const& value, Declaration const& decl) { return std::format("When this field is changed (via its setter and other such functions) it will call the `{}` method.", value.get<std::string>()); }
};

inline const AttributeProperties atFieldFlagGetters{ "FlagGetters", "If set to an (reflected) enum name, creates getter functions (IsFlag) for each Flag in this field; can't be set if 'Flags' is set", json::value_t::string };
inline const AttributeProperties atFieldFlags{ "Flags", "If set to an (reflected) enum name, creates getter and setter functions (IsFlag, SetFlag, UnsetFlag, ToggleFlag) for each Flag in this field; can't be set if 'FlagGetters' is set", json::value_t::string };
inline const AttributeProperties atFieldFlagNots{ "FlagNots", "Requires 'Flags' attribute. If set, creates IsNotFlag functions in addition to regular IsFlag (except for enumerators with Opposite attribute).", json::value_t::boolean, true };

inline const AttributeProperties atMethodUniqueName{ "UniqueName", "A unique (for this type) name of this method; useful for overloaded functions", json::value_t::string, {},
	[](json const& value, Declaration const& decl) { return std::format("This method's unique name will be `{}` in scripts.", value.get<std::string>()); }
};
inline const AttributeProperties atMethodGetterFor{ "GetterFor", "This function is a getter for the named field", json::value_t::string };
inline const AttributeProperties atMethodSetterFor{ "SetterFor", "This function is a setter for the named field", json::value_t::string };

inline const AttributeProperties atRecordAbstract{ "Abstract", "This record is abstract (don't create special constructors)", json::value_t::boolean, false };
inline const AttributeProperties atRecordSingleton{ "Singleton", "This record is a singleton. Adds a static function (default name 'SingletonInstance') that returns the single instance of this record", json::value_t::boolean, false,
	[](json const& value, Declaration const& decl) { if (!value) return ""s; return "This class is a singleton. Call @see SingletonInstance to get the instance."s; } /// TODO: Instead of hardcoding the SingletonInstance name, get it from options
};

inline const AttributeProperties atRecordDefaultFieldAttributes{ "DefaultFieldAttributes", "These attributes will be added as default to every reflected field of this class", json::value_t::discarded, json::object() };
inline const AttributeProperties atRecordDefaultMethodAttributes{ "DefaultMethodAttributes", "These attributes will be added as default to every reflected method of this class", json::value_t::discarded, json::object() };
inline const AttributeProperties atEnumDefaultEnumeratorAttributes{ "DefaultEnumeratorAttributes", "These attributes will be added as default to every enumerator of this enum", json::value_t::discarded, json::object() };

inline const AttributeProperties atRecordCreateProxy{ "CreateProxy", "If set to false, proxy classes are not built for classes with virtual methods", json::value_t::boolean, true };

inline const AttributeProperties atEnumList{ "List", "If set to true, generates GetNext() and GetPrev() functions that return the next/prev enumerator in sequence, wrapping around", json::value_t::boolean, false };
inline const AttributeProperties atEnumeratorOpposite{ "Opposite", "Only valid on Flag enums, will create a virtual flag that is the complement of this one, for the purposes of creating getters and setters", json::value_t::string};
