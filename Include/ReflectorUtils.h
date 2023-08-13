#pragma once

#include "ReflectorClasses.h"

namespace Reflector
{

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

	extern Class const* Classes[];
	extern Enum const* Enums[];

	template <typename FUNC>
	void ForEachClass(FUNC&& func)
	{
		for (auto klass = Classes; *klass; ++klass)
			func(*klass);
	}

	inline Class const* FindClassByFullType(std::string_view class_name)
	{
		for (auto klass = Classes; *klass; ++klass)
		{
			if ((*klass)->FullType == class_name)
				return *klass;
		}
		return nullptr;
	}

	template <typename T, typename VISITOR>
	void ForEachField(T& object, VISITOR&& visitor);

	template <typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::string_view function_name, std::type_identity<std::tuple<ARGS...>> argument_types = {});
	template <CompileTimeLiteral NAME, typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::type_identity<std::tuple<ARGS...>> argument_types = {});

	/// Method implementation

	
	template<typename T>
	auto Class::FindFirstFieldByType() const -> Field const*
	{
		for (auto& field : Fields)
			if (field.FieldTypeIndex == typeid(T)) return &field;
		return nullptr;
	}

	template <typename FUNC>
	void Class::ForAllMethodsWithName(std::string_view name, FUNC&& func) const
	{
		for (auto& method : Methods)
			if (method.Name == name) func(method);
	}

	template <typename... ARGS>
	auto Class::FindMethod(std::string_view name, std::type_identity<std::tuple<ARGS...>>) const -> Method const*
	{
		static const std::vector<std::type_index> parameter_ids = { std::type_index{typeid(ARGS)}... };
		for (auto& method : Methods)
			if (method.Name == name && method.ParameterTypeIndices == parameter_ids) return &method;
		return nullptr;
	}


	template <typename T, typename VISITOR>
	void ForEachField(T& object, VISITOR&& visitor)
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
	void WithMethodOverloadThatCanTake(auto&& func, std::string_view function_name, std::type_identity<std::tuple<ARGS...>>)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachMethod([&](auto properties) {
			if constexpr (decltype(properties)::template CanTake<ARGS...>())
				if (function_name == decltype(properties)::Name) func(properties);
		});
	}

	template <CompileTimeLiteral NAME, typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::type_identity<std::tuple<ARGS...>>)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachMethod([&](auto properties) {
			if constexpr (decltype(properties)::template CanTake<ARGS...>() && decltype(properties)::Name == NAME)
				func(properties);
		});
	}

	template <typename POINTER_TYPE, typename PATH_TYPE, typename RESOLVING_CLASS>
	struct BaseResolvable
	{
		using pointer_type = POINTER_TYPE;
		using path_type = PATH_TYPE;
		using resolving_class = RESOLVING_CLASS;

		BaseResolvable() noexcept(std::is_nothrow_constructible_v<path_type> && std::is_nothrow_constructible_v<pointer_type>) = default;
		BaseResolvable(BaseResolvable const&) noexcept(std::is_nothrow_copy_constructible_v<path_type>&& std::is_nothrow_copy_constructible_v<pointer_type>) = default;
		BaseResolvable(BaseResolvable&&) noexcept(std::is_nothrow_move_constructible_v<path_type>&& std::is_nothrow_move_constructible_v<pointer_type>) = default;
		BaseResolvable& operator=(BaseResolvable const& obj) noexcept(std::is_nothrow_copy_assignable_v<path_type>&& std::is_nothrow_copy_assignable_v<pointer_type>) = default;
		BaseResolvable& operator=(BaseResolvable&& obj) noexcept(std::is_nothrow_move_assignable_v<path_type>&& std::is_nothrow_move_assignable_v<pointer_type>) = default;

		pointer_type operator->() const requires std::is_pointer_v<pointer_type> { return mPointer; }
		pointer_type const& operator->() const requires (!std::is_pointer_v<pointer_type>) { return mPointer; }
		auto&& operator*() const { return *mPointer; }

		auto operator<=>(pointer_type const& ptr) const { return mPointer <=> ptr; }
		auto operator==(pointer_type const& ptr) const { return mPointer == ptr; }
		auto operator<=>(path_type const& path) const { return mPath <=> path; }
		auto operator==(path_type const& path) const { return mPath == path; }

		path_type const& path() const { return mPath; }
		pointer_type const& pointer() const& { return mPointer; }
		pointer_type pointer()&& { return std::move(mPointer); }

		void set_pointer(pointer_type const& obj)
		{
			auto path = mPath;
			if (RESOLVING_CLASS::unresolve(path, obj))
			{
				mPath = std::move(path);
				mPointer = obj;
			}
		}

		auto& operator=(pointer_type const& obj) { set_pointer(obj); return *this; }

		void set_path(path_type const& path)
		{
			if (RESOLVING_CLASS::resolve(path, mPointer))
				mPath = path;
		}

		auto& operator=(path_type const& path) { set_path(path); return *this; }

		explicit operator bool() const noexcept { return (bool)mPointer; }

	private:

		path_type mPath{};
		pointer_type mPointer{};
	};


#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)

	template <typename OBJECT_TYPE, typename PATH_TYPE, typename RESOLVING_CLASS>
	inline void to_json(REFLECTOR_JSON_TYPE& j, BaseResolvable<OBJECT_TYPE, PATH_TYPE, RESOLVING_CLASS> const& p)
	{
		j = p.path();
	}

	template <typename OBJECT_TYPE, typename PATH_TYPE, typename RESOLVING_CLASS>
	inline void from_json(REFLECTOR_JSON_TYPE const& j, BaseResolvable<OBJECT_TYPE, PATH_TYPE, RESOLVING_CLASS>& p)
	{
		p.set_path((PATH_TYPE)j);
	}

#endif

}

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)

namespace Reflector
{
	struct DataError
	{
		std::string Message;
		std::string File;
		int Line;
	};
}

NLOHMANN_JSON_NAMESPACE_BEGIN
template <::Reflector::reflected_class SERIALIZABLE>
struct adl_serializer<SERIALIZABLE>
{
	static void to_json(REFLECTOR_JSON_TYPE& j, const SERIALIZABLE& p)
	{
		p.JSONSaveFields(j);
	}

	static void from_json(const REFLECTOR_JSON_TYPE& j, SERIALIZABLE& p)
	{
		p.JSONLoadFields(j);
	}
};

template <::Reflector::reflected_enum SERIALIZABLE>
struct adl_serializer<SERIALIZABLE>
{
	static void to_json(REFLECTOR_JSON_TYPE& j, const SERIALIZABLE& p)
	{
		j = std::underlying_type_t<SERIALIZABLE>(p);
	}

	static void from_json(const REFLECTOR_JSON_TYPE& j, SERIALIZABLE& p)
	{
		if (j.is_string()) p = GetEnumeratorFromName(p, j); else p = (SERIALIZABLE)(std::underlying_type_t<SERIALIZABLE>)j;
	}
};
NLOHMANN_JSON_NAMESPACE_END

#endif
