#pragma once

#define REFLECTOR_USES_GC 1

#include "ReflectorUtils.h"
#include <unordered_set>
#include <ranges>

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
		&& !reflectable_class<std::remove_pointer_t<PT>>
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

	struct Heap
	{
		//static std::map<std::string, Reflectable*, std::less<>> Roots;
		static std::map<std::string, Reflectable*, std::less<>> const& Roots();
		static bool SetRoot(std::string_view key, Reflectable* r);
		static bool RemoveRoot(std::string_view key);
		static Reflectable* GetRoot(std::string_view key);
		static void ClearRoots();

		template <reflectable_class T>
		static T* GetRoot(std::string_view key)
		{
			const auto ptr = GetRoot(key);
			return ptr ? ptr->As<T>() : nullptr;
		}

		static std::unordered_set<Reflectable*> const& Objects();

		template <typename T>
		static auto ObjectsOfType()
		{
			static_assert(reflectable_class<T>, "Only reflectable classes can be iterated on");

			return Objects() | std::views::filter([](auto* ptr) { return ptr->Is<T>(); }) | std::views::transform([](auto* ptr) { return (T*)ptr; });
		}

		static Reflectable* New(ClassReflectionData const* type);

		template <typename T>
		static T* New()
		{
			static_assert(reflectable_class<T>, "Only reflectable classes can be New()ed");

			return (T*)Add(new (Alloc<T>()) T());
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
		template <reflectable_class T>
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

		static void ResolveHeapObject(ClassReflectionData const* type, Reflectable const*& p, std::string_view obj_type, intptr_t obj_id);

		static void Clear();

		static Reflectable* Add(Reflectable* obj);
		static void Mark();
		static void Sweep();
		static Reflectable* Alloc(ClassReflectionData const* klass_data);
		template <reflectable_class T, typename... ARGS>
		static T* Alloc()
		{
			return (T*)Alloc(&T::StaticGetReflectionData());
		}
		static void Delete(Reflectable* ptr);

		//static void RootDeref(Reflectable* r);
		//static void RootRef(Reflectable* r);

		template <typename T>
		friend struct GCRootPointer;
	};

	template <typename T>
	T* Reflectable::New()
	{
		return Heap::New<T>();
	}

	/*
	template <typename T>
	struct GCRootPointer
	{
		static_assert(reflectable_class<T>);

		explicit(false) GCRootPointer() noexcept = default;
		explicit(false) GCRootPointer(T* ptr) noexcept
			: mPointer(ptr)
		{
			Heap::RootRef(ptr);
		}
		GCRootPointer(GCRootPointer const& other) noexcept : mPointer(other.mPointer) { Heap::RootRef(mPointer); }
		GCRootPointer(GCRootPointer&& other) noexcept : mPointer(std::exchange(other.mPointer, nullptr)) { }
		GCRootPointer& operator=(GCRootPointer const& other) noexcept
		{
			if (&other != this)
			{
				Heap::RootDeref(mPointer);
				mPointer = other.mPointer;
				Heap::RootRef(mPointer);
			};
			return *this;
		}
		GCRootPointer& operator=(GCRootPointer&& other) noexcept
		{
			if (&other != this)
				mPointer = std::exchange(other.mPointer, nullptr);
			return *this;
		}
		~GCRootPointer() noexcept
		{
			Heap::RootDeref(mPointer);
		}

		explicit operator bool() const noexcept { return !!mPointer; }
		explicit(false) operator T*() const noexcept { return mPointer; }
		[[nodiscard]] T* operator->() const noexcept { return mPointer; }
		[[nodiscard]] T* get() const noexcept { return mPointer; }

		auto operator<=>(GCRootPointer const&) const noexcept = default;
		auto operator<=>(T* ptr) const noexcept { return mPointer <=> ptr; }
		auto operator<=>(std::nullptr_t) const noexcept { return mPointer <=> nullptr; }

	private:

		T* mPointer = nullptr;
	};
	*/

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)

	/*
	template <typename T>
	void to_json(REFLECTOR_JSON_TYPE& j, GCRootPointer<T> const& heap)
	{
		j = heap.get();
	}

	template <typename T>
	void from_json(REFLECTOR_JSON_TYPE const& j, GCRootPointer<T>& heap)
	{
		heap = GCRootPointer<T>{ (T*)j };
	}
	*/

		/*
		static GCHeapLoader& ResolveHeapLoader(intptr_t heap_id)
		{
			if (heap_id == 0)
				throw std::invalid_argument("heap_id");

			auto& result = mLoadingHeaps[heap_id];
			if (result.mHeapID == 0)
				result.mHeapID = heap_id;
			return result;
		}

		std::map<intptr_t, std::pair<void*, std::string_view>> Objects;

		Reflectable* ResolveObject(std::string_view obj_type, intptr_t obj_id)
		{
			if (auto it = Objects.find(obj_id); it != Objects.end())
				return (Reflectable*)it->second.first;

			auto klass_data = FindClassByFullType(obj_type);
			if (!klass_data || !klass_data->HasConstructors())
				throw std::runtime_error("Error resolving GC heap: type '" + std::string{ obj_type } + "' is not a GC-enabled class");

			Reflectable* result = mExistingHeap ? mExistingHeap->Alloc(klass_data) : (Reflectable*)klass_data->Alloc();
			Objects[obj_id] = { result, obj_type };
			return result;
		}

		void ResolveHeap(REFLECTOR_JSON_TYPE const& j, GCHeap& heap)
		{
			if (mExistingHeap)
				throw std::runtime_error(std::string{ "Sanity error: trying to resolve same GC heap twice, id: " } + std::to_string(mHeapID));

			mExistingHeap = &heap;

		}

	private:

		static inline std::map<intptr_t, GCHeapLoader> mLoadingHeaps;
		GCHeap* mExistingHeap = nullptr;
		intptr_t mHeapID = 0;
		*/

	//void to_json(REFLECTOR_JSON_TYPE& j, Heap const& heap);

	//void from_json(REFLECTOR_JSON_TYPE const& j, Heap& heap);

#endif
}

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)
NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename SERIALIZABLE>
requires std::is_pointer_v<SERIALIZABLE> && ::Reflector::reflectable_class<std::remove_pointer_t<SERIALIZABLE>>
struct adl_serializer<SERIALIZABLE>
{
	using Heap = ::Reflector::Heap;

	static void to_json(REFLECTOR_JSON_TYPE& j, const SERIALIZABLE& p)
	{
		if (p)
			j = { (intptr_t)p, p->GetReflectionData().FullType };
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
			//GCHeapLoader& heap_loader = GCHeapLoader::ResolveHeapLoader(heap_id);
			//p = (SERIALIZABLE)heap_loader.ResolveObject(obj_type, obj_id);
		}
	}
};
NLOHMANN_JSON_NAMESPACE_END
#endif