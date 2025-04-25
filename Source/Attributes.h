#pragma once

#include "Common.h"
#include <ghassanpl/named.h>

struct AttributeProperties;

using AttributeValidatorFunc = std::function<tl::expected<void, std::string>(json const&, Declaration const&)>;
extern AttributeValidatorFunc IsString;
extern AttributeValidatorFunc NotEmptyString;

enum class AttributePropertyFlags
{
	/// If this is set, this attribute cannot be directly set by the user
	NotUserSettable,
};

using AttributeCategory = ghassanpl::named<std::string_view, "AttributeCategory">;
ghassanpl_named_string_literal_ce(AttributeCategory, _ac);

struct AttributeProperties
{
	std::vector<std::string> ValidNames;
	std::string_view Name() const { return ValidNames[0]; }
	std::string Description;
	std::string Category = "Miscellaneous";
	enum_flags<DeclarationType> ValidTargets{};

	bool AppliesTo(Declaration const& decl) const { return ValidTargets.contain(decl.DeclarationType()); }


	json DefaultValueIfAny = nullptr;

	AttributeValidatorFunc Validator;

	enum_flags<AttributePropertyFlags> Flags{};

	static std::vector<std::string_view> FindUnsettable(json const& attrs);
	static std::vector<AttributeProperties const*> AllAttributes;

	template <typename... ARGS>
	AttributeProperties(std::string name, std::string desc, enum_flags<DeclarationType> targets, json default_value = nullptr, ARGS&&... args) noexcept
		: ValidNames(split<std::string>(name, ";"))
		, Description(std::move(desc))
		, ValidTargets(targets)
		, DefaultValueIfAny(std::move(default_value))
	{
		AllAttributes.push_back(this);

		(this->Set(std::forward<ARGS>(args)), ...);
	}

	tl::expected<void, std::string> Validate(json const& attr_value, Declaration const& decl) const;

	void Set(AttributeValidatorFunc validator) { this->Validator = std::move(validator); }
	void Set(enum_flags<AttributePropertyFlags> flags) { this->Flags += flags; }
	void Set(AttributePropertyFlags flag) { this->Flags += flag; }
	void Set(AttributeCategory ac) { this->Category.assign(ac.value); }

	std::optional<std::string> ExistsIn(Declaration const& decl) const;
	std::optional<std::string> ExistsIn(json const& attrs) const;

	template <typename T>
	auto operator()(Declaration const& decl, T&& default_value) const
	{
		const auto it = Find(decl);
		if (!it || it->is_null())
			return std::forward<T>(default_value);
		return (T)*it;
	}

	json const* operator()(Declaration const& decl) const
	{
		return Find(decl);
	}

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

	json const& TryGet(Declaration const& decl) const
	{
		static const json empty_json;
		const auto it = Find(decl);
		if (!it)
			return empty_json;
		return *it;
	}

protected:

	void ValidateThrowing(json const& attr_value, Declaration const& decl) const
	{
		if (auto exp = Validate(attr_value, decl); !exp)
			ReportError(decl, "Invalid attribute '{}': {}", Name(), std::move(exp).error());
	}

	json const* Find(Declaration const& decl, bool validate = true) const
	{
		for (auto& name : ValidNames)
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

	AttributeProperties(AttributeProperties const& other) noexcept = default;
};

template <typename T>
struct TypedAttributeProperties : public AttributeProperties
{
	using AttributeProperties::AttributeProperties;
	using AttributeProperties::Set;
	using AttributeType = T;
	T TypedDefaultValue{};

	template <typename... ARGS>
	TypedAttributeProperties(std::string name, std::string desc, enum_flags<DeclarationType> targets, ARGS&&... args) noexcept
		: AttributeProperties(std::move(name), std::move(desc), targets, {})
	{
		if constexpr (std::is_same_v<std::string, T>) this->Set(NotEmptyString); /// NOTE: String attributes cannot be empty by default!

		(this->Set(std::forward<ARGS>(args)), ...);
	}

	template <typename U>
	void Set(U default_value) requires std::same_as<std::remove_cvref_t<U>, T> { this->DefaultValueIfAny = default_value; this->TypedDefaultValue = std::move(default_value); }

	T operator()(Declaration const& decl) const
	{
		if (const auto found = Find(decl))
			return (T)*found;
		return TypedDefaultValue;
	}
	
	template <typename U = T>
	std::optional<U> SafeGet(Declaration const& decl) const
	{
		if (const auto found = Find(decl))
			return (U)*found;
		return std::nullopt;
	}

	template <typename U = T, typename D>
	U GetOr(Declaration const& decl, D&& default_value) const
	{
		if (const auto found = Find(decl))
			return (U)*found;
		return std::forward<D>(default_value);
	}
};

using StringAttributeProperties = TypedAttributeProperties<std::string>;
using BoolAttributeProperties = TypedAttributeProperties<bool>;
struct Attribute
{
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

	static const BoolAttributeProperties GenerateJSONSchema;

	/// These mirror C++ attributes but should not be set by the user directly, most of the time
	static const BoolAttributeProperties NoReturn;
	static const BoolAttributeProperties Deprecated;
	static const BoolAttributeProperties NoDiscard; /// TODO: We should have two versions of this: "NoDiscardUser" and "NoDiscardFromCppAttr" with different targets
	static const BoolAttributeProperties NoUniqueAddress;
};