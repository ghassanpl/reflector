#pragma once

#define REFLECTOR_USES_GC 1

#include "ReflectorUtils.h"
#include <unordered_set>
#include <ranges>
#include <variant>

namespace Reflector
{
	template <typename T>
	struct could_be_marked
	{
		static constexpr bool value = std::is_class_v<std::remove_cvref_t<T>> || std::is_array_v<std::remove_cvref_t<T>>;
	};

	template <typename T>
	requires std::is_pointer_v<std::remove_cvref_t<T>>
	struct could_be_marked<T> : could_be_marked<std::remove_pointer_t<std::remove_cvref_t<T>>>
	{
	};

	template <typename T>
	concept has_mark_func = requires (T const& val) { val.GCMark(); };

	template <typename T, typename PT = std::remove_cvref_t<T>>
	requires (!has_mark_func<PT>
		&& !derives_from_reflectable<std::remove_pointer_t<PT>>
		&& !std::is_pointer_v<PT>
		&& !std::ranges::range<T>
	)
	void GCMark(T&&)
	{
		/// Don't mark stuff that isn't an aggregate
		static_assert(!could_be_marked<T>::value, "Type is an aggregate but cannot be marked. Make sure you overload the Reflector::GCMark(T&) function.");
	}

	template <typename T> void GCMark(std::basic_string<T> const&) {}
	template <typename T> void GCMark(std::basic_string_view<T> const&) {}
	template <typename... ELS>
	void GCMark(std::tuple<ELS...> const& val) { std::apply([](auto const& ...x) { (GCMark(x), ...); }, val); }
	template <typename F, typename S>
	void GCMark(std::pair<F, S> const& val) { GCMark(val.first); GCMark(val.second); }
	template <typename... TYPES>
	void GCMark(std::variant<TYPES...> const& val) { std::visit([](auto const& v) { GCMark(v); }, val); }

	template <typename T>
	requires (has_mark_func<std::remove_cvref_t<T>>)
	void GCMark(T& val)
	{
		val.GCMark();
	}

	inline void GCMark(Reflectable const* r)
	{
		if (r && r->GC_IsOnHeap() && !r->GC_IsMarked())
			r->GCMark();
	}

	template <typename T>
	void GCMark(T const* r);

	template <std::ranges::range RANGE>
	void GCMark(RANGE&& range)
	{
		if constexpr (could_be_marked<std::ranges::range_value_t<RANGE>>::value)
		{
			for (auto&& val : range)
				GCMark(val);
		}
	}

	/// This class is NOT thread-safe.
	struct Heap
	{
		static std::map<std::string, Reflectable*, std::less<>> const& Roots();
		static bool SetRoot(std::string_view key, Reflectable* r);
		static bool RemoveRoot(std::string_view key);
		static Reflectable* GetRoot(std::string_view key);
		static void ClearRoots();

		template <derives_from_reflectable T>
		static T* GetRoot(std::string_view key)
		{
			const auto ptr = GetRoot(key);
			return ptr ? ptr->As<T>() : nullptr;
		}

		static std::unordered_set<Reflectable*> const& Objects();

		template <typename T>
		static auto ObjectsOfType()
		{
			static_assert(derives_from_reflectable<T>, "Only reflectable classes can be iterated on");

			return Objects() | std::views::filter([](auto* ptr) { return ptr->template Is<T>(); }) | std::views::transform([](auto* ptr) { return (T*)ptr; });
		}

		//static Reflectable* New(Class const* type);

		template <typename T, typename... ARGS>
		static T* New(ARGS&&... args)
		{
			static_assert(derives_from_reflectable<T>, "Only reflectable classes can be New()ed");

			if constexpr (sizeof...(ARGS) > 0)
				return (T*)Add(new (Alloc<T>()) T(std::forward<ARGS>(args)...));
			else
				return (T*)Add(new (Alloc<T>()) T);
		}

		template <typename T>
		static T* NewRoot(std::string_view name)
		{
			auto ptr = New<T>();
			SetRoot(name, ptr);
			return ptr;
		}

		static void MinimizeMemory();

		template <typename... ROOTS>
		static void Collect(ROOTS&... roots)
		{
			(Reflector::GCMark(roots), ...);
			Mark();
			Sweep();
		}

		static void GCMark(auto) noexcept {}

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)
		template <derives_from_reflectable T>
		static void ResolveHeapObject(T*& p, std::string_view obj_type, intptr_t obj_id)
		{
			Reflectable const* r = p;
			ResolveHeapObject(&T::StaticGetReflectionData(), r, obj_type, obj_id);
			p = (T*)r;
		}

		static REFLECTOR_JSON_TYPE ToJson();
		static void FromJson(REFLECTOR_JSON_TYPE const&);
#endif

	private:

		static void ResolveHeapObject(Class const* type, Reflectable const*& p, std::string_view obj_type, intptr_t obj_id);

		static void Clear();

		static Reflectable* Add(Reflectable* obj);
		static void Mark();
		static void Sweep();
		static Reflectable* Alloc(Class const* klass_data);
		template <derives_from_reflectable T, typename... ARGS>
		static T* Alloc()
		{
			return (T*)Alloc(&T::StaticGetReflectionData());
		}
		static void Delete(Reflectable* ptr);

		template <typename T>
		friend struct GCRootPointer;
	};

	template <reflected_class T, typename... ARGS>
	T* New(ARGS&&... args)
	{
		if constexpr (derives_from_reflectable<T>)
			return ::Reflector::Heap::New<T>(std::forward<ARGS>(args)...);
		else
			return nullptr;
	}

	template <typename T, typename... ARGS>
	T* Reflectable::New(ARGS&&... args)
	{
		static_assert(reflected_class<T>, "Only reflected classes can be created using New<T>()");
		return ::Reflector::New<T>(std::forward<ARGS>(args)...);
	}
}

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)
NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename SERIALIZABLE>
requires std::is_pointer_v<SERIALIZABLE> && ::Reflector::derives_from_reflectable<std::remove_pointer_t<SERIALIZABLE>>
struct adl_serializer<SERIALIZABLE>
{
	using Heap = ::Reflector::Heap;

	static void to_json(REFLECTOR_JSON_TYPE& j, const SERIALIZABLE& p)
	{
		if (p)
		{
			if (!p->GC_IsOnHeap())
				throw std::runtime_error("Pointer to a GC-enabled object points to an object not on the GC heap, cannot serialize");

			j = { (intptr_t)p, p->GetReflectionData().FullType };
		}
		else
			j = nullptr;
	}

	static void from_json(const REFLECTOR_JSON_TYPE& j, SERIALIZABLE& p)
	{
		if (j.is_null())
			p = nullptr;
		else
		{
			const intptr_t obj_id = j[0];
			const std::string_view obj_type = j[1];
			Heap::ResolveHeapObject(p, obj_type, obj_id);
		}
	}
};
NLOHMANN_JSON_NAMESPACE_END
#endif