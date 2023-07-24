#pragma once

#include "ReflectorClasses.h"
#include <set>
#include <map>
#include <ranges>

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
	void ForEachField(T& object, VISITOR&& visitor);

	template <typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::string_view function_name, std::type_identity<std::tuple<ARGS...>> argument_types = {});
	template <CompileTimeLiteral NAME, typename T, typename... ARGS>
	void WithMethodOverloadThatCanTake(auto&& func, std::type_identity<std::tuple<ARGS...>> argument_types = {});

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

#if defined(REFLECTOR_USES_GC) && REFLECTOR_USES_GC
	class GCHeap
	{
	public:

		std::vector<Reflectable*> AllAllocated;
		std::set<Reflectable*> RootSet;
		std::map<ClassReflectionData const*, std::vector<Reflectable*>> FreeLists;

		template <typename T>
		auto AllOfType() const
		{
			return AllAllocated | std::views::filter([](auto* ptr) { return ptr->Is<T>(); }) | std::views::transform([](auto* ptr) { return (T*)ptr; });
		}

		template <typename T, typename... ARGS>
		requires std::derived_from<T, Reflectable>
		T* Alloc(ARGS&&... args)
		{
			Reflectable* result = nullptr;

			auto klass_data = &T::StaticGetReflectionData();
			if (auto listit = FreeLists.find(klass_data); listit != FreeLists.end() && !listit->second.empty())
			{
				result = listit->second.back();
				listit->second.pop_back();
			}
			else
			{
#ifdef _WIN32
				result = reinterpret_cast<T*>(_aligned_malloc(sizeof(T), alignof(T)));
#else
				result = reinterpret_cast<T*>(std::aligned_alloc(alignof(T), sizeof(T)));
#endif
			}

			new (result) T(std::forward<ARGS>(args)...);
			result->mParentHeap = this;
			return (T*)AllAllocated.emplace_back(result);
		}

		template <typename T>
		requires std::derived_from<T, Reflectable>
		void Free(T* ptr)
		{
			auto klass_data = &ptr->GetReflectionData();
			FreeLists[klass_data].push_back(ptr);
			ptr->~T();
		}

		~GCHeap()
		{
			for (auto& obj : AllAllocated)
			{
				obj->~Reflectable();
				std::free(obj);
			}
			for (auto& [klass, vec] : FreeLists)
			{
				for (auto& obj : vec)
					std::free(obj);
			}
		}

		template <typename... ROOTS>
		void Collect(ROOTS&... roots)
		{
			(Reflector::GCMark(this, roots), ...);
			Mark();
			Sweep();
		}

		void GCMark(auto) const noexcept {}

	private:

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)
		friend void from_json(REFLECTOR_JSON_TYPE const& j, GCHeap& heap);
		intptr_t mID = 0;
#endif

		void Mark()
		{
			for (auto& root : RootSet)
			{
				::Reflector::GCMark(this, root);
			}
		}

		void Sweep()
		{
			std::erase_if(AllAllocated, [this](Reflectable* obj) {
				if (obj->mFlags & (1ULL << int(Reflectable::Flags::Marked)))
				{
					obj->mFlags &= ~(1ULL << int(Reflectable::Flags::Marked));
					return false;
				}
				Free(obj);
				return true;
			});
		}

	};

	template <typename T, typename... ARGS>
	T* Reflectable::New(ARGS&&... args)
	{
		if (!mParentHeap)
			throw std::runtime_error("member New() can only be used in objects allocated on a GCHeap");

		return mParentHeap->Alloc<T>(std::forward<ARGS>(args)...);
	}


	template <typename T>
	struct GCRootPointer
	{
	private:
		T* mPointer = nullptr;
	};

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)

	struct GCHeapLoader
	{
		static GCHeapLoader& ResolveHeapLoader(intptr_t heap_id)
		{
			if (heap_id == 0)
				throw std::invalid_argument("heap_id");

			auto& result = mLoadingHeaps[heap_id];
			if (result.mHeapID == 0)
				result.mHeapID = heap_id;
			return result;
		}

		std::map<intptr_t, Reflectable*> Objects;

		GCHeap CreateHeap(REFLECTOR_JSON_TYPE const& heap_data_obj)
		{
			GCHeap result;
			if (heap_data_obj.at("ID") != mHeapID)
				throw std::runtime_error(std::string{ "Heap needs to be loaded from a heap json object with the ID " } + std::to_string(mHeapID));

			std::vector<intptr_t> AllAllocated = heap_data_obj.at("Objects");
			std::vector<intptr_t> RootSet = heap_data_obj.at("RootSet");

			for (auto& obj_id : AllAllocated)
				result.AllAllocated.push_back(Objects.at(obj_id));
			for (auto& obj_id : AllAllocated)
				result.RootSet.insert(Objects.at(obj_id));

			return result;
		}

	private:

		static inline std::map<intptr_t, GCHeapLoader> mLoadingHeaps;
		intptr_t mHeapID = 0;
	};

	inline void to_json(REFLECTOR_JSON_TYPE& j, GCHeap const& heap)
	{
		using json = REFLECTOR_JSON_TYPE;
		j = json::object();
		j["ID"] = (intptr_t)&heap;
		auto& roots = j["RootSet"] = json::array();
		auto& objects= j["Objects"] = json::array();

		std::vector<Reflectable*> AllAllocated;
		std::set<Reflectable*> RootSet;
		for (auto obj : heap.AllAllocated)
		{
			auto& j = objects.emplace_back();
			j["$id"] = (intptr_t)obj;
			j["$type"] = obj->GetReflectionData().FullType;
			obj->JSONSaveFields(j);
		}
		for (auto obj : heap.RootSet)
			roots.push_back((intptr_t)obj);
	}

	inline void from_json(REFLECTOR_JSON_TYPE const& j, GCHeap& heap)
	{
		const intptr_t heap_id = j.at("ID");

		heap.mID = heap_id;
		GCHeapLoader& heap_loader = GCHeapLoader::ResolveHeapLoader(heap_id);
	}

#endif


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

#if defined(REFLECTOR_USES_GC) && REFLECTOR_USES_GC
template <typename SERIALIZABLE>
requires std::is_pointer_v<SERIALIZABLE> && std::derived_from<std::remove_pointer_t<SERIALIZABLE>, ::Reflector::Reflectable>
struct adl_serializer<SERIALIZABLE>
{
	using GCHeapLoader = ::Reflector::GCHeapLoader;
	using GCHeap = ::Reflector::GCHeap;

	static void to_json(REFLECTOR_JSON_TYPE& j, const SERIALIZABLE& p)
	{
		if (p)
			j = { (intptr_t)p->GetParentHeap(), (intptr_t)p, p->GetReflectionData().FullType };
		else
			j = nullptr;
	}

	static void from_json(const REFLECTOR_JSON_TYPE& j, SERIALIZABLE& p)
	{
		if (j.is_null())
			p = nullptr;
		else
		{
			const intptr_t heap_id = j[0];
			const intptr_t obj_id = j[1];
			const std::string_view obj_type = j[2];
			GCHeapLoader& heap_loader = GCHeapLoader::ResolveHeapLoader(heap_id);
			///p = heap.ResolveObject(obj_id);
		}
	}
};
#endif

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
