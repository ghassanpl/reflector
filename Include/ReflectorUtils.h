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

	inline Enum const* FindEnumByFullType(std::string_view enum_name)
	{
		for (auto henum = Enums; *henum; ++henum)
		{
			if ((*henum)->FullType == enum_name)
				return *henum;
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

	/// ///////////////////////////////////// ///
	/// Ref
	/// ///////////////////////////////////// ///

	template <typename CLASS_TYPE, typename POINTER_TYPE = CLASS_TYPE*>
	struct PathReference
	{
		using ClassType = CLASS_TYPE;
		using PointerType = POINTER_TYPE;

		PathReference() noexcept = default;
		PathReference(PathReference const&) noexcept = default;
		PathReference(PathReference&&) noexcept = default;
		PathReference& operator=(PathReference const& obj) noexcept = default;
		PathReference& operator=(PathReference&& obj) noexcept = default;

		PathReference(std::string p) : mPath(std::move(p)) {}
		PathReference(PointerType p) : mPointer(std::move(p)) {}

		PointerType operator->() const { return Pointer(); }
		auto& operator*() const { return *Pointer(); }

		auto operator<=>(PathReference const& other) const { return Path() <=> other.Path(); }
		auto operator==(PathReference const& other) const { return Path() == other.Path(); }
		
		auto operator<=>(PointerType const& ptr) const { return Pointer() <=> ptr; }
		auto operator==(PointerType const& ptr) const { return Pointer() == ptr; }
		
		auto operator<=>(std::string_view path) const { return this->Path() <=> path; }
		auto operator==(std::string_view path) const { return this->Path() == path; }
		
		std::string_view Path() const& {
			ResolvePath();
			return mPath;
		}

		std::string Path() && {
			ResolvePath();
			return std::move(mPath);
		}

		PointerType const& Pointer() const {
			ResolvePointer();
			return mPointer;
		}

		PointerType const* TryPointer() const {
			return mPointer ? &mPointer : nullptr;
		}

		auto& operator=(PointerType obj) { mPath = {}; mPointer = std::move(obj); return *this; }
		auto& operator=(std::string path) { mPointer = {}; mPath = std::move(path); return *this; }

		explicit operator bool() const noexcept { return Pointer() != PointerType{}; }

	private:

		void ResolvePointer() const
		{
			if (mPointer || mPath.empty()) return;
			mPointer = ClassType::template ResolveReferenceFromPath<PointerType>(mPath);
		}

		void ResolvePath() const
		{
			if (!mPath.empty() || !mPointer) return;
			mPath = ClassType::ResolvePathFromReference(mPointer);
		}

		mutable std::string mPath{};
		mutable PointerType mPointer{};
	};

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
requires ((SERIALIZABLE::StaticClassFlags() & (1ULL << int(::Reflector::ClassFlags::NotSerializable))) == 0)
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

template <typename SERIALIZABLE>
requires ::Reflector::derives_from_reflectable<SERIALIZABLE> && ((SERIALIZABLE::StaticClassFlags()& (1ULL << int(::Reflector::ClassFlags::NotSerializable))) == 0)
struct adl_serializer<std::unique_ptr<SERIALIZABLE>>
{
	static void to_json(REFLECTOR_JSON_TYPE& j, const std::unique_ptr<SERIALIZABLE>& p)
	{
		if (p)
			adl_serializer<SERIALIZABLE>::to_json(j, *p);
		else
			j = nullptr;
	}

	static void from_json(const REFLECTOR_JSON_TYPE& j, std::unique_ptr<SERIALIZABLE>& p)
	{
		if (j.is_null())
		{
			p.reset();
			return;
		}
		if (!j.is_object())
			throw ::Reflector::DataError{ "JSON source is not an object" };
		if (!j.contains("$type"))
			throw ::Reflector::DataError{ "JSON source does not contain a '$type' field" };
		if (!j["$type"].is_string())
			throw ::Reflector::DataError{ "JSON source '$type' field is not a string" };

		auto type = std::string_view{ j["$type"] };
		if (!p || p->GetRuntimeClass()->FullType != type)
		{
			p.reset();

			auto klass = Reflector::FindClassByFullType(type);
			if (!klass) 
				throw ::Reflector::DataError{ std::format("Unknown reflectable type '{}'", type) };
			/// NOTE: Not using aligned_alloc because MS doesn't handle it properly
			
			p = std::unique_ptr<SERIALIZABLE>{ (SERIALIZABLE*)klass->NewDefault(malloc(klass->Size)) };
		}
		adl_serializer<SERIALIZABLE>::from_json(j, *p);
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
