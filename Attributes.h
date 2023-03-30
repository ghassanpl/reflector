#pragma once

#include "Common.h"

struct AttributeProperties;

//using AttributeDocFunc = std::function<std::string(json const&, Declaration const&)>;
using AttributeValidatorFunc = std::function<expected<void, std::string>(json const&, Declaration const&)>;
extern AttributeValidatorFunc IsString;
extern AttributeValidatorFunc NotEmptyString;

struct AttributeProperties
{
	std::vector<std::string> ValidNames;
	std::string_view Name() const { return ValidNames[0]; }
	std::string Description;
	enum_flags<DeclarationType> ValidTargets{};

	bool AppliesTo(Declaration const& decl) const { return ValidTargets.contain(decl.DeclarationType()); }

	json DefaultValueIfAny = nullptr;

	AttributeValidatorFunc Validator;

	static inline std::vector<AttributeProperties const*> AllAttributes;

	template <typename... ARGS>
	AttributeProperties(std::string name, std::string desc, enum_flags<DeclarationType> targets, json default_value = nullptr, ARGS&&... args) noexcept
		: ValidNames(string_ops::split<std::string>(name, ";"))
		, Description(move(desc))
		, ValidTargets(targets)
		, DefaultValueIfAny(std::move(default_value))
	{
		AllAttributes.push_back(this);

		(this->Set(std::forward<ARGS>(args)), ...);
	}

	expected<void, std::string> Validate(json const& attr_value, Declaration const& decl) const;

	void Set(AttributeValidatorFunc validator) { this->Validator = move(validator); }

	std::optional<std::string> ExistsIn(Declaration const& decl) const
	{
		auto it = std::ranges::find_if(ValidNames, [&](std::string const& name) { return decl.Attributes.contains(name); });
		if (it != ValidNames.end())
			return *it;
		return std::nullopt;
	}

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
		: AttributeProperties(move(name), move(desc), targets, {})
	{
		if constexpr (std::is_same_v<std::string, T>) this->Set(NotEmptyString); /// NOTE: String attributes cannot be empty by default!

		(this->Set(std::forward<ARGS>(args)), ...);
	}

	template <typename U>
	void Set(U default_value) requires std::same_as<std::remove_cvref_t<U>, T> { this->DefaultValueIfAny = default_value; this->TypedDefaultValue = std::move(default_value); }

	/*
	template <typename U = T>
	auto operator()(json const& attrs, U&& default_value) const
	{
		const auto it = attrs.find(this->Name);
		if (it == attrs.end())
			return std::forward<T>(default_value);
		if constexpr (std::is_same_v<T, bool>)
			return it->is_null() ? true : (bool)*it;
		else
			return (T)*it;
	}
	*/

	T operator()(Declaration const& decl) const
	{
		if (auto found = Find(decl))
			return (T)*found;
		return TypedDefaultValue;
	}
	std::optional<T> SafeGet(Declaration const& decl) const
	{
		if (auto found = Find(decl))
			return (T)*found;
		return std::nullopt;
	}
	T GetOr(Declaration const& decl, T default_value) const
	{
		if (auto found = Find(decl))
			return (T)*found;
		return default_value;
	}
};

using StringAttributeProperties = TypedAttributeProperties<std::string>;
using BoolAttributeProperties = TypedAttributeProperties<bool>;
struct Attribute
{
	static const StringAttributeProperties DisplayName;

	static const StringAttributeProperties Namespace;

	/// TODO: Maybe instead of Getter and Setter, just have one attribute - ReadOnly - which also influences whether or not other setters are created (like flag, vector, optional setters)

	static const BoolAttributeProperties Getter;
	static const BoolAttributeProperties Setter;
	static const BoolAttributeProperties Editor;
	static const BoolAttributeProperties Save;
	static const BoolAttributeProperties Load;

	static const BoolAttributeProperties Document; /// TODO: This

	static const BoolAttributeProperties Serialize;
	static const BoolAttributeProperties Private;
	static const BoolAttributeProperties ParentPointer;
	static const BoolAttributeProperties Required;

	static const StringAttributeProperties OnChange;

	static const StringAttributeProperties FlagGetters;
	static const StringAttributeProperties Flags;
	static const BoolAttributeProperties FlagNots;

	static const StringAttributeProperties UniqueName;

	static const StringAttributeProperties GetterFor;
	static const StringAttributeProperties SetterFor;

	static const BoolAttributeProperties Abstract;
	static const BoolAttributeProperties Singleton;

	static const AttributeProperties DefaultFieldAttributes;
	static const AttributeProperties DefaultMethodAttributes;
	static const AttributeProperties DefaultEnumeratorAttributes;

	static const BoolAttributeProperties CreateProxy;

	static const BoolAttributeProperties List;
	static const StringAttributeProperties Opposite;
};