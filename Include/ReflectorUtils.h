#pragma once

#include "ReflectorClasses.h"

namespace Reflector
{
	template <typename T> concept reflected_class = requires { T::StaticClassFlags(); };
	template <typename T> concept reflected_enum = IsReflectedEnum<T>();

	template <typename REFLECTABLE_TYPE>
	auto const& Reflect()
	{
		if constexpr (reflected_class<REFLECTABLE_TYPE>)
		{
			return REFLECTABLE_TYPE::StaticGetReflectionData();
		}
		else if constexpr (reflected_enum<REFLECTABLE_TYPE>)
		{
			return GetEnumReflectionData<REFLECTABLE_TYPE>();
		}
		static_assert(reflected_class<REFLECTABLE_TYPE> || reflected_enum<REFLECTABLE_TYPE>, "Type must be marked as reflectable");
	}

	template <typename T, typename VISITOR>
	void ForEachField(T&& object, VISITOR&& visitor);

	template <typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::string_view function_name, std::type_identity<std::tuple<ARGS...>> argument_types = {});
	template <CompileTimeLiteral NAME, typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::type_identity<std::tuple<ARGS...>> argument_types = {});

	/*
	template <reflected_class T, typename VISITOR>
	void ForEachProperty(T&& object, VISITOR&& visitor) {

	}
	*/

	extern ClassReflectionData const* Classes[];
	extern EnumReflectionData const* Enums[];

	/// Method implementation

	inline auto ClassReflectionData::FindField(std::string_view name) const -> FieldReflectionData const*
	{
		for (auto& field : Fields)
			if (field.Name == name) return &field;
		return nullptr;
	}

	template<typename T>
	auto ClassReflectionData::FindFirstFieldByType() const -> FieldReflectionData const*
	{
		for (auto& field : Fields)
			if (field.FieldTypeIndex == typeid(T)) return &field;
		return nullptr;
	}

	inline auto ClassReflectionData::FindFirstMethod(std::string_view name) const -> MethodReflectionData const*
	{
		for (auto& method : Methods)
			if (method.Name == name) return &method;
		return nullptr;
	}

	inline auto ClassReflectionData::FindAllMethods(std::string_view name) const -> std::vector<MethodReflectionData const*>
	{
		std::vector<MethodReflectionData const*> result;
		for (auto& method : Methods)
			if (method.Name == name) result.push_back(&method);
		return result;
	}

	template <typename FUNC>
	void ClassReflectionData::ForAllMethodsWithName(std::string_view name, FUNC&& func) const
	{
		for (auto& method : Methods)
			if (method.Name == name) func(method);
	}

	template <typename... ARGS>
	auto ClassReflectionData::FindMethod(std::string_view name, std::type_identity<std::tuple<ARGS...>>) const -> MethodReflectionData const*
	{
		static const std::vector<std::type_index> parameter_ids = { std::type_index{typeid(ARGS)}... };
		for (auto& method : Methods)
			if (method.Name == name && method.ParameterTypeIndices == parameter_ids) return &method;
		return nullptr;
	}


	template <typename T, typename VISITOR>
	void ForEachField(T&& object, VISITOR&& visitor)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachField([&](auto properties) {
			if constexpr (decltype(properties)::HasFlag(FieldFlags::Static))
				visitor(*properties.Pointer, properties);
			else
				visitor((object.*(properties.Pointer)), properties);
		});
	}

	template <typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::string_view function_name, std::type_identity<std::tuple<ARGS...>> argument_types)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachMethod([&](auto properties) {
			if constexpr (decltype(properties)::template CanTake<ARGS...>())
				if (function_name == decltype(properties)::Name) func(properties);
		});
	}

	template <CompileTimeLiteral NAME, typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::type_identity<std::tuple<ARGS...>> argument_types)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachMethod([&](auto properties) {
			if constexpr (decltype(properties)::template CanTake<ARGS...>() && decltype(properties)::Name == NAME)
				func(properties);
		});
	}
}
