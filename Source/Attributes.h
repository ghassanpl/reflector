/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"
#include <ghassanpl/named.h>

/// Attributes are per-entity properties that can be set manually by the user in the parameter brackets (`()`) of the
/// reflection macros (e.g. RField(Deprecated=true)), using a simplified JSON syntax (see `Wilson` in the wiki).
/// They are different from flags, in that flags are mostly set based on the code itself, rather than the reflection markup.
///
/// Attributes change how and what reflection data and code is generated. For now, there is no automatic documentation generated
/// for what attributes are available and what they do. See `Attributes.cpp` for a list and description of currently supported
/// attributes.
///
/// You can query the attributes (as a JSON string/value) at runtime, using the `Attributes` and `AttributesJSON` fields of the
/// reflection structs (not that the parsed JSON value is only available if the JSON option is enabled).
/// Access to the attributes at compile time is currently not supported, but could be added if requested.
///
/// Some attributes will be set implicitly based on the code itself, for ease of parsing. In general, you
/// should never use these attributes manually, and the tool will warn you of that.


/// Attributes can have validation functions for parse-time validation of attribute values. These can be used to provide
/// useful information to the user on why the value they gave was of incorrect type or value.
using AttributeValidatorFunc = std::function<expected<void, std::string>(json const&, Declaration const&)>;

/// There are two basic validators, with more defined in Attributes.cpp
extern AttributeValidatorFunc IsString;
extern AttributeValidatorFunc NotEmptyString;

/// Attributes can have flags that also specify their behavior.
enum class AttributePropertyFlags
{
	/// If this is set, this attribute cannot be directly set by the user
	NotUserSettable,
};

/// A helper type for attribute constructors to easily set the attribute category using the setter pattern
using AttributeCategory = named<std::string_view, "AttributeCategory">;
ghassanpl_named_string_literal_ce(AttributeCategory, _ac);

/// The base class for all types of attribute properties. Stores almost everything about the attribute needed
/// to parse it and make it usable for, and discoverable by, the end-user.
/// Uses the setter pattern to set most attribute properties. See Attributes.cpp for examples of usage of this, and
/// child classes.
/// 
struct AttributeProperties
{
	/// \param name Attributes can have multiple names, separated by semicolons. This is used to allow for aliases
	///		for attributes. For example, giving "Editor;Edit" will make both `Editor=false` and `Edit=false` valid.
	///		The first name is the primary name and will be treated as canonical, especially for searches,
	///		documentation and error reporting.
	/// \param desc The description of the attribute. This is used for documentation and error reporting.
	/// \param targets The types of declarations this attribute can be applied to.
	/// \param default_value The default value of the attribute.
	/// \param args Additional properties of this attribute. See the `Set` methods for more information.
	template <typename... ARGS>
	AttributeProperties(std::string name, std::string desc, enum_flags<DeclarationType> targets, json default_value = nullptr, ARGS&&... args) noexcept
		: mValidNames(split<std::string>(name, ";"))
		, mDescription(std::move(desc))
		, mValidTargets(targets)
		, mDefaultValueIfAny(std::move(default_value))
	{
		mAllAttributes.push_back(this);

		(this->Set(std::forward<ARGS>(args)), ...);
	}

	void Set(AttributeValidatorFunc validator) { this->mValidator = std::move(validator); }
	void Set(enum_flags<AttributePropertyFlags> flags) { this->mFlags += flags; }
	void Set(AttributePropertyFlags flag) { this->mFlags += flag; }
	void Set(AttributeCategory ac) { this->mCategory.assign(ac.value); }

	std::string_view Name() const { return mValidNames.at(0); }

	/// Returns whether this attribute is applicable to the given declaration.
	bool AppliesTo(Declaration const& decl) const;

	/// Returns the names of any unsettable attributes in the given attributes JSON object.
	static std::vector<std::string_view> FindUnsettable(json const& attrs);

	/// Validates that the value (given in `attr_value`) of this attribute is valid for the given declaration.
	/// If it's not, the unexpected will contain an error message.
	expected<void, std::string> Validate(json const& attr_value, Declaration const& decl) const;

	/// Returns whether this attribute is set in the given declaration. If it is, returns the value.
	std::optional<std::string> ExistsIn(Declaration const& decl) const;
	/// Returns whether this attribute is set in the given attributes object. If it is, returns the value.
	std::optional<std::string> ExistsIn(json const& attrs) const;

	/// Returns the value of this attribute in the given declaration (as type T). If it is not set, returns 
	/// `default_value`.
	template <typename T>
	auto operator()(Declaration const& decl, T&& default_value) const
	{
		const auto it = Find(decl);
		if (!it || it->is_null())
			return std::forward<T>(default_value);
		return (T)*it;
	}

	/// Returns the pointer to the value of this attribute in the given declaration. If it is not set, returns nullptr.
	json const* operator()(Declaration const& decl) const
	{
		return Find(decl);
	}

	/// Puts the value of this attribute in the given declaration (as type T) into `dest`, if it is set.
	/// Returns true if the value was set, false otherwise.
	template <typename T>
	bool TryGet(Declaration const& decl, T& dest) const
	{
		const auto it = Find(decl);
		if (!it)
			return false;
		if (it->is_null())
			return false;
		dest = (T)*it;
		return true;
	}

	/// Returns a reference to the value of this attribute in the given declaration. If it is not set, returns reference 
	/// to empty JSON object.
	json const& TryGet(Declaration const& decl) const
	{
		static const json empty_json;
		const auto it = Find(decl);
		if (!it)
			return empty_json;
		return *it;
	}

protected:

	std::vector<std::string> mValidNames;
	std::string mDescription;
	std::string mCategory = "Miscellaneous";
	enum_flags<DeclarationType> mValidTargets{};
	json mDefaultValueIfAny = nullptr;
	AttributeValidatorFunc mValidator;
	enum_flags<AttributePropertyFlags> mFlags{};

	static inline std::vector<AttributeProperties const*> mAllAttributes;

	/// Validates the attribute value using the validator function, throwing an exception if it fails.
	void ValidateThrowing(json const& attr_value, Declaration const& decl) const;

	/// Finds the attribute value in the given declaration. If it is not set, returns nullptr.
	/// If `validate` is true, it will validate the attribute value using the validator function,
	/// throwing an exception if it fails.
	json const* Find(Declaration const& decl, bool validate = true) const;

	AttributeProperties(AttributeProperties const& other) noexcept = default;
};

/// Since most attributes are simple types, like booleans, strings or numbers, this class is
/// used to simplify the creation, validation and retrieval of attributes with a specific type.
template <typename T>
struct TypedAttributeProperties : AttributeProperties
{
	/// Same as `AttributeProperties`, except that the default value is not set.
	/// NOTE: If the type is `std::string`, by default the NonEmptyString validator is used.
	template <typename... ARGS>
	TypedAttributeProperties(std::string name, std::string desc, enum_flags<DeclarationType> targets, ARGS&&... args) noexcept
		: AttributeProperties(std::move(name), std::move(desc), targets, {})
	{
		if constexpr (std::is_same_v<std::string, T>) this->Set(NotEmptyString); /// NOTE: String attributes cannot be empty by default!

		(this->Set(std::forward<ARGS>(args)), ...);
	}

	using AttributeProperties::AttributeProperties;
	using AttributeProperties::Set;
	using AttributeType = T;

	/// Sets the default value of this attribute.
	template <typename U>
	void Set(U&& default_value) 
	requires std::same_as<std::remove_cvref_t<U>, T>
	{
		this->mDefaultValueIfAny = default_value;
		this->mTypedDefaultValue = std::move(default_value);
	}

	/// Same as `AttributeProperties::operator()`, but returns the value as type T,
	/// and uses the default value set in the constructor or using the Set method.
	T operator()(Declaration const& decl) const
	{
		if (const auto found = Find(decl))
			return (T)*found;
		return mTypedDefaultValue;
	}
	
	/// Same as `operator()`, but returns an optional of type T instead of using the default value (which might not be set).
	template <typename U = T>
	std::optional<U> SafeGet(Declaration const& decl) const
	{
		if (const auto found = Find(decl))
			return (U)*found;
		return std::nullopt;
	}

	/// Returns the value of this attribute (as either type T or a specified type) in declaration `decl`,
	/// or `default_value` if not set.
	template <typename U = T, typename D>
	U GetOr(Declaration const& decl, D&& default_value) const
	{
		if (const auto found = Find(decl))
			return (U)*found;
		return std::forward<D>(default_value);
	}

protected:

	T mTypedDefaultValue{};
};

using StringAttributeProperties = TypedAttributeProperties<std::string>;
using BoolAttributeProperties = TypedAttributeProperties<bool>;
struct Attribute
{
	/// These are here for public access, but they are documented mostly in the Attributes.cpp file.
	
	static const StringAttributeProperties DisplayName;
	static const StringAttributeProperties SaveName;
	static const StringAttributeProperties LoadName;

	static const StringAttributeProperties Namespace;
	static const StringAttributeProperties GUID;

	static const BoolAttributeProperties Getter;
	static const BoolAttributeProperties Setter;
	static const BoolAttributeProperties Editor;
	static const BoolAttributeProperties Script;
	static const BoolAttributeProperties Save;
	static const BoolAttributeProperties Load;

	static const BoolAttributeProperties Document;
	static const BoolAttributeProperties DocumentMembers;

	static const StringAttributeProperties UniqueID; /// TODO: This

	static const BoolAttributeProperties Serialize;
	static const BoolAttributeProperties Private;
	static const BoolAttributeProperties Transient;
	static const BoolAttributeProperties ScriptPrivate;
	static const BoolAttributeProperties Required;

	static const BoolAttributeProperties PrivateGetters;
	static const BoolAttributeProperties PrivateSetters;

	static const StringAttributeProperties OnChange;

	static const StringAttributeProperties FlagGetters;
	static const StringAttributeProperties Flags;
	static const BoolAttributeProperties FlagNots;

	static const StringAttributeProperties UniqueName;
	static const StringAttributeProperties ScriptName;

	static const StringAttributeProperties GetterFor;
	static const StringAttributeProperties SetterFor;
	static const BoolAttributeProperties Property;

	static const BoolAttributeProperties Resettable; /// TODO: Can reset entire object to default values (basically default value of Reset for all fields)
	static const BoolAttributeProperties Reset; /// TODO: Can reset field to default value

	static const BoolAttributeProperties Abstract;
	static const BoolAttributeProperties Singleton;

	static const AttributeProperties DefaultFieldAttributes;
	static const AttributeProperties DefaultMethodAttributes;
	static const AttributeProperties DefaultEnumeratorAttributes;

	static const BoolAttributeProperties CreateProxy;
	
	static const BoolAttributeProperties Unimplemented;

	static const BoolAttributeProperties List;
	static const StringAttributeProperties Opposite;
	/// TODO: If this is set, it will create a `constexpr inline <EnumName> Invalid<EnumName> = <ThisValue>`,
	///		and will return it in functions that can return an invalid value (e.g. `GetEnumeratorValue`, etc.)
	static const TypedAttributeProperties<int64_t> InvalidValue;
	static const StringAttributeProperties InvalidValueName;
	static const BoolAttributeProperties AliasEnum; 

	static const BoolAttributeProperties GenerateJSONSchema;

	/// These mirror C++ attributes but should not be set by the user directly, most of the time
	static const BoolAttributeProperties NoReturn;
	static const BoolAttributeProperties Deprecated;
	static const BoolAttributeProperties NoDiscard; /// TODO: We should have two versions of this: "NoDiscardUser" and "NoDiscardFromCppAttr" with different targets
	static const BoolAttributeProperties NoUniqueAddress;
};