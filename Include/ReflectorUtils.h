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

	template <typename FUNC, typename BASE>
	void ForEachClass(FUNC&& func, BASE&& base_class)
	{
		for (auto klass = Classes; *klass; ++klass)
		{
			if ((*klass)->HasBaseClass(base_class))
				func(*klass);
		}
	}
	template <typename BASE, typename FUNC>
	void ForEachClass(FUNC&& func)
	{
		for (auto klass = Classes; *klass; ++klass)
		{
			if ((*klass)->HasBaseClass<BASE>())
				func(*klass);
		}
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

	inline Class const* FindClassByGUID(std::string_view class_guid)
	{
		for (auto klass = Classes; *klass; ++klass)
		{
			if ((*klass)->GUID == class_guid)
				return *klass;
		}
		return nullptr;
	}

	inline auto Class::FindBaseClass() const -> Class const*
	{
		return FindClassByFullType(BaseClassName);
	}
	
	inline auto Class::HasBaseClass(std::string_view base_klass_name) const -> bool
	{
		if (BaseClassName == base_klass_name)
			return true;
		if (const auto base_class = FindBaseClass())
			return base_class->HasBaseClass(base_klass_name);
		return false;
	}

	inline auto Class::HasBaseClass(Class const& klass) const -> bool
	{
		if (const auto base_class = FindBaseClass())
		{
			if (base_class == &klass)
				return true;
			return base_class->HasBaseClass(klass);
		}
		return false;
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

	template<typename U>
	requires reflected_class<U>
	auto Class::HasBaseClass() const -> bool
	{
		return this->HasBaseClass(::Reflector::Reflect<U>());
	}


	template <typename T, typename VISITOR>
	void ForEachField(T& object, VISITOR&& visitor)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachField([&]<typename T0>(T0 properties) {
			if constexpr (T0::HasFlag(FieldFlags::Static))
				visitor(*properties.Pointer, properties);
			else
				visitor((object.*(properties.Pointer)), properties);
		});
	}

	template <typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::string_view function_name, std::type_identity<std::tuple<ARGS...>>)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachMethod([&]<typename T0>(T0 properties) {
			if constexpr (T0::template CanTake<ARGS...>())
				if (function_name == T0::Name) func(properties);
		});
	}

	template <CompileTimeLiteral NAME, typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::type_identity<std::tuple<ARGS...>>)
	{
		static_assert(reflected_class<std::remove_cvref_t<T>>, "Type must be marked as reflectable");
		std::remove_cvref_t<T>::ForEachMethod([&]<typename T0>(T0 properties) {
			if constexpr (T0::template CanTake<ARGS...>() && T0::Name == NAME)
				func(properties);
		});
	}

	/// ///////////////////////////////////// ///
	/// Ref
	/// ///////////////////////////////////// ///

	template <typename CLASS_TYPE, typename POINTER_TYPE = CLASS_TYPE*, typename... TAGS>
	struct PathReference
	{
		using ClassType = CLASS_TYPE;
		using PointerType = POINTER_TYPE;

		PathReference() noexcept = default;
		PathReference(PathReference const&) noexcept = default;
		PathReference(PathReference&&) noexcept = default;
		PathReference& operator=(PathReference const& obj) noexcept = default;
		PathReference& operator=(PathReference&& obj) noexcept = default;
		
		template <typename T>
		requires std::constructible_from<std::string, T>
		PathReference(T&& p) : mPath(std::forward<T>(p)) { ValidatePath(); }

		template <typename T>
		requires std::constructible_from<PointerType, T>
		PathReference(T&& p) : mPointer(std::forward<T>(p)) { }

		template <typename T>
		requires std::constructible_from<std::string, T>
		auto& operator=(T&& p) { 
			mPointer = {}; 
			mPath = std::forward<T>(p);
			ValidatePath();
			return *this;
		}

		template <typename T>
		requires std::constructible_from<PointerType, T>
		auto& operator=(T&& p) {
			mPath = {};
			mPointer = std::forward<T>(p);
			return *this;
		}

		PointerType operator->() const { return Pointer(); }
		auto& operator*() const { return *Pointer(); }

		auto operator<=>(PathReference const& other) const { return Path() <=> other.Path(); }
		bool operator==(PathReference const& other) const { return Path() == other.Path(); }
		
		auto operator<=>(PointerType const& ptr) const { return Pointer() <=> ptr; }
		bool operator==(PointerType const& ptr) const { return Pointer() == ptr; }
		
		auto operator<=>(std::string_view path) const { return this->Path() <=> path; }
		bool operator==(std::string_view path) const { return this->Path() == path; }

		void Reset() { mPath = {}; mPointer = {}; }
		
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

		explicit operator bool() const noexcept { return Pointer() != PointerType{}; }

	private:

		void ValidatePath()
		{
			ClassType::template ValidatePath<PointerType, TAGS...>(mPath, mPointer);
		}

		void ResolvePointer() const
		{
			if (mPointer || mPath.empty()) return;
			ClassType::template ResolveReferenceFromPath<PointerType, TAGS...>(mPath, mPointer);
		}

		void ResolvePath() const
		{
			if (!mPath.empty() || !mPointer) return;
			ClassType::template ResolvePathFromReference<PointerType, TAGS...>(mPointer, mPath);
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
	struct UserError
	{
		std::string Message;
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

	/// TODO: $type here should be dependent on the Options.JSON.ObjectTypeFieldName

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
			if (!klass && j.contains("$guid") && j.at("$guid").is_string())
				klass = Reflector::FindClassByGUID(j.at("$guid"));
			if (!klass)
				throw ::Reflector::DataError{ std::format("Unknown reflectable type '{}'", type) };
			/// NOTE: Not using aligned_alloc because MS doesn't handle it properly
			
			const auto ptr = static_cast<Reflector::Reflectable*>(klass->NewDefault(malloc(klass->Size)));
			if (!ptr)
				throw ::Reflector::UserError{ std::format("Could not construct object of type '{}' - type has no default constructor", type) };
			p = std::unique_ptr<SERIALIZABLE>{ dynamic_cast<SERIALIZABLE*>(ptr) };
			if (!p)
				throw ::Reflector::UserError{ std::format("Could not pass constructed object of type '{}' to pointer of type '{}'", type, SERIALIZABLE::StaticGetReflectionData().Name) };
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
