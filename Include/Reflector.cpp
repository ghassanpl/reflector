#if __has_include("Reflector.h")
#include "Reflector.h"
#endif
#include "ReflectorUtils.h"
#if defined(REFLECTOR_USES_GC) && REFLECTOR_USES_GC
#include "ReflectorGC.h"
#endif

#include <unordered_map>
#include <atomic>

namespace Reflector
{

	void* AlignedAlloc(size_t alignment, size_t size)
	{
#ifdef _WIN32
		return _aligned_malloc(size, alignment);
#else
		return std::aligned_alloc(alignment, size);
#endif
	}

	void AlignedFree(void* obj)
	{
#ifdef _WIN32
		_aligned_free(obj);
#else
		std::free(obj);
#endif
	}

	void* Class::Alloc() const
	{
		return AlignedAlloc(Alignment, Size);
	}

	void Class::Delete(void* obj) const
	{
		Destructor(obj);
		AlignedFree(obj);
	}

	void* Class::NewDefault(void* at) const
	{
		if (at && DefaultConstructor)
		{
			DefaultConstructor(at);
			return at;
		}
		return nullptr;
	}

	auto Class::FindField(std::string_view name) const -> Field const*
	{
		for (auto& field : Fields)
			if (field.Name == name) return &field;
		return nullptr;
	}

	auto Class::FindFirstMethod(std::string_view name) const -> Method const*
	{
		for (auto& method : Methods)
			if (method.Name == name) return &method;
		return nullptr;
	}

	auto Class::FindAllMethods(std::string_view name) const -> std::vector<Method const*>
	{
		std::vector<Method const*> result;
		for (auto& method : Methods)
			if (method.Name == name) result.push_back(&method);
		return result;
	}

	Class const& Reflectable::StaticGetReflectionData()
	{
		static const Class data = {
			.Name = "Reflectable",
			.FullType = "Reflector::Reflectable",
			.BaseClassName = "",
			.Attributes = "",
	#if REFLECTOR_USES_JSON
			.AttributesJSON = {},
	#endif
			.Alignment = alignof(Reflectable),
			.Size = sizeof(Reflectable),
			.Destructor = [](void* obj) { auto ptr = (Reflectable*)obj; ptr->~Reflectable(); },
			.TypeIndex = typeid(Reflectable)
		};
		return data;
	}

	Class const& Reflectable::GetReflectionData() const
	{
		return StaticGetReflectionData();
	}

#if defined(REFLECTOR_USES_GC) && REFLECTOR_USES_GC

	std::unordered_set<Reflectable*> mExtantObjects;
	/// TODO: Force RootSet items to always be in Objects, as serializing depends on it
	std::unordered_map<Class const*, std::vector<Reflectable*>> mFreeLists;
	size_t mAllocatedBytes = 0;
	size_t mFreeListBytes = 0;
	std::unordered_map<intptr_t, std::pair<Reflectable*, Class const*>> mLoadingMapping;
	bool mLoadingHeap = false;

	std::map<std::string, Reflectable*, std::less<>> mRoots;

	std::map<std::string, Reflectable*, std::less<>> const& Heap::Roots()
	{
		return mRoots;
	}
	
	bool Heap::SetRoot(std::string_view key, Reflectable* r)
	{
		if (r == nullptr)
			return false;
		mRoots[std::string{ key }] = r;
		return true;
	}
	
	bool Heap::RemoveRoot(std::string_view key)
	{
		if (auto it = mRoots.find(key); it != mRoots.end())
		{
			mRoots.erase(it);
			return true;
		}
		return false;
	}

	Reflectable* Heap::GetRoot(std::string_view key)
	{
		if (auto it = mRoots.find(key); it != mRoots.end())
			return it->second;
		return nullptr;
	}
	
	void Heap::ClearRoots()
	{
		mRoots.clear();
	}


	std::unordered_set<Reflectable*> const& Heap::Objects()
	{
		return mExtantObjects;
	}

	/*
	Reflectable* Heap::New(Class const* type)
	{
		Reflectable* result = Alloc(type);
		type->DefaultConstructor(result);
		return Add(result);
	}
	*/

	void Heap::MinimizeMemory()
	{
		for (auto const& [klass, vec] : mFreeLists)
		{
			for (auto& obj : vec)
				AlignedFree(obj);
		}
		mFreeLists.clear();
		mFreeListBytes = 0;
	}

	void Heap::Clear()
	{
		for (auto& obj : mExtantObjects)
		{
			obj->~Reflectable();
			AlignedFree(obj);
		}
		MinimizeMemory();

		mExtantObjects.clear();
		mRoots.clear();
		
		mAllocatedBytes = 0;
	}

	Reflectable* Heap::Alloc(Class const* klass_data)
	{
		void* result_alloc = nullptr;

		if (auto listit = mFreeLists.find(klass_data); listit != mFreeLists.end() && !listit->second.empty())
		{
			result_alloc = listit->second.back();
			listit->second.pop_back();
		}
		else
		{
			result_alloc = klass_data->Alloc();
		}

		::memset(result_alloc, 0, klass_data->Size);
		Reflectable* result = new (result_alloc) Reflectable(*klass_data, (1ULL << int(Reflectable::Flags::OnHeap)));
		return result;
	}

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)

	REFLECTOR_JSON_TYPE Heap::ToJson()
	{
		using json = REFLECTOR_JSON_TYPE;
		json j = json::object();
		auto& roots = j["Roots"] = json::object();
		auto& objects = j["Objects"] = json::array();

		for (auto obj : mExtantObjects)
		{
			auto& j = objects.emplace_back();
			j["$id"] = (intptr_t)obj;
			j["$type"] = obj->GetReflectionData().FullType;
			obj->JSONSaveFields(j);
		}

		for (auto const& [name, obj] : mRoots)
			roots[name] = (intptr_t)obj;

		return j;
	}

	void Heap::FromJson(REFLECTOR_JSON_TYPE const& j)
	{
		mLoadingHeap = true;

		Heap::Clear();

		mLoadingMapping.clear();

		/// Go through all serialized objects, alloc/construct them if necessary, and load their data.
		for (auto& obj_data : j.at("Objects"))
		{
			const intptr_t id = obj_data.at("$id");
			std::string_view type = obj_data.at("$type");
			auto klass = FindClassByFullType(type);
			assert(klass);

			assert(!mLoadingMapping.contains(id));

			Reflectable* obj = nullptr;
			obj = Alloc(klass);
			mLoadingMapping[id] = { obj, klass };
		}
		
		for (auto& obj_data : j.at("Objects"))
		{
			const intptr_t id = obj_data.at("$id");

			auto [obj, klass] = mLoadingMapping[id];

			klass->DefaultConstructor(obj);
			Add(obj);
			obj->JSONLoadFields(obj_data);
		}

		/// At this point, if we have any objects that were referenced in the loader, but have not been found
		/// in the Heap objects, this indicates an error. However, instead of throwing, we default-construct 
		/// these objects, just in case, and let the GC and the user sort'em out.
		for (auto& [id, mapping] : mLoadingMapping)
		{
			if (mExtantObjects.contains(mapping.first))
				continue;

			auto klass = mapping.second;
			assert(klass);

			klass->DefaultConstructor(mapping.first);
			Add(mapping.first);
		}

		/// Load rootset
		for (auto& [name, id] : j.at("Roots").items())
			mRoots[name] = mLoadingMapping.at((intptr_t)id).first;

		mLoadingHeap = false;
	}


	void Heap::ResolveHeapObject(Class const* base_type, Reflectable const*& p, std::string_view obj_type, intptr_t obj_id)
	{
		const auto actual_type = FindClassByFullType(obj_type);
		assert(actual_type);
		assert(base_type);
		/// TODO: assert(base_type->IsDerivedFrom(actual_type));

		Reflectable* obj = nullptr;
		if (mLoadingHeap)
		{
			if (auto it = mLoadingMapping.find(obj_id); it != mLoadingMapping.end())
			{
				obj = it->second.first;
			}
			else
			{
				obj = Alloc(actual_type);
				mLoadingMapping[obj_id] = { obj, actual_type };
			}
		}
		else
		{
			if (auto it = mExtantObjects.find(reinterpret_cast<Reflectable*>(obj_id)); it != mExtantObjects.end())
			{
				obj = reinterpret_cast<Reflectable*>(obj_id);
				assert(obj->GetReflectionData().FullType == obj_type);
			}
			else
			{
				/// TODO: We are trying to deserialize an object reference that has not been created... what now?
				/// Maybe log something?
				obj = nullptr;
			}
		}
		p = obj;
	}

#endif

	Reflectable* Heap::Add(Reflectable* obj)
	{
		obj->mFlags |= 1ULL<<int(Reflectable::Flags::OnHeap);
		mExtantObjects.insert(obj);
		return obj;
	}

	void Heap::Mark()
	{
		for (auto const& [name, root] : mRoots)
		{
			::Reflector::GCMark(root);
		}
	}

	void Heap::Delete(Reflectable* ptr)
	{
		auto klass_data = &ptr->GetReflectionData();
		mFreeLists[klass_data].push_back(ptr);
		ptr->~Reflectable();

#ifndef NDEBUG
		std::memset((void*)ptr, 0xFE, klass_data->Size);
#endif
	}

	void Heap::Sweep()
	{
		std::erase_if(mExtantObjects, [](Reflectable* obj) {
			if (obj->mFlags & (1ULL << int(Reflectable::Flags::Marked)))
			{
				obj->mFlags &= ~(1ULL << int(Reflectable::Flags::Marked));
				return false;
			}
			Delete(obj);
			return true;
		});
	}

#endif

}
