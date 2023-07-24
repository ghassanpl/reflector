#pragma once

#if (__cplusplus < 202002L) && (defined(_MSVC_LANG) && _MSVC_LANG < 202002L)
#error "Reflector requires C++20"
#endif

#include <typeindex>
#include <vector>
#include <compare>
#include <string_view>
#if REFLECTOR_USES_JSON
#include REFLECTOR_JSON_HEADER
#endif
#if REFLECTOR_USES_GC
#include <set>
#include <map>
#endif

namespace Reflector
{
	enum class MethodFlags;
	struct FieldReflectionData;
	struct MethodReflectionData;

	template<size_t N>
	struct CompileTimeLiteral
	{
		explicit(false) constexpr CompileTimeLiteral(char const (&s)[N]) { std::copy_n(s, N, this->Value); }
		constexpr std::strong_ordering operator<=>(CompileTimeLiteral const&) const = default;
		constexpr bool operator==(std::string_view str) const { return str == Value; }
		char Value[N];
	};

	/// TODO: We could technically put attributes in here as well (at least top-level bool ones, as flags or something)

	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL>
	struct CompileTimePropertyData
	{
		using Type = std::remove_cvref_t<FIELD_TYPE>;
		using ParentType = PARENT_TYPE;
		static constexpr uint64_t Flags = FLAGS;
		static constexpr std::string_view Name = NAME_CTL.Value;

		template <typename FLAG_TYPE>
		static constexpr bool HasFlag(FLAG_TYPE flag_value) noexcept { return (Flags & (1ULL << uint64_t(flag_value))) != 0; }
	};
	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL, typename PTR_TYPE, PTR_TYPE POINTER>
	struct CompileTimeFieldData : CompileTimePropertyData<FIELD_TYPE, PARENT_TYPE, FLAGS, NAME_CTL>
	{
		using PointerType = PTR_TYPE;
		static constexpr PTR_TYPE Pointer = POINTER;
		static constexpr bool IsStatic = !std::is_member_object_pointer_v<PTR_TYPE>;

		static auto Getter(PARENT_TYPE const* obj) -> FIELD_TYPE const&
		{
			if constexpr (IsStatic)
				return *Pointer;
			else
				return (obj->*Pointer);
		}
		static auto Getter(PARENT_TYPE* obj) -> FIELD_TYPE&
		{
			if constexpr (IsStatic)
				return *Pointer;
			else
				return (obj->*Pointer);
		}
		static auto Getter() -> FIELD_TYPE& requires IsStatic
		{
			return *Pointer;
		}
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericGetter(PARENT const* obj) -> FIELD const& { return (obj->*Pointer); }

		template <typename PARENT = PARENT_TYPE, typename VALUE>
		static auto GenericSetter(PARENT* obj, VALUE&& value) -> void { (obj->*Pointer) = std::forward<VALUE>(value); };

		static auto CopySetter(PARENT_TYPE* obj, FIELD_TYPE const& value) -> void { (obj->*Pointer) = value; };
		static auto MoveSetter(PARENT_TYPE* obj, FIELD_TYPE&& value) -> void { (obj->*Pointer) = std::move(value); };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericCopySetter(PARENT* obj, FIELD const& value) -> void { (obj->*Pointer) = value; };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericMoveSetter(PARENT* obj, FIELD& value) -> void { (obj->*Pointer) = std::move(value); };

		static auto VoidGetter(void const* obj) -> void const* { return &(obj->*Pointer); }
		static auto VoidSetter(void* obj, void const* value) -> void { (obj->*Pointer) = reinterpret_cast<FIELD_TYPE const*>(value); };
	};

	template <typename RETURN_TYPE, typename PARAMETER_TUPLE_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL, typename PTR_TYPE, PTR_TYPE POINTER>
	struct CompileTimeMethodData
	{
		static constexpr uint64_t Flags = FLAGS;
		static constexpr std::string_view Name = NAME_CTL.Value;
		using ReturnType = std::remove_cvref_t<RETURN_TYPE>;
		using ParentType = PARENT_TYPE;
		using ParameterTupleType = PARAMETER_TUPLE_TYPE;
		static constexpr auto ParameterCount = std::tuple_size_v<PARAMETER_TUPLE_TYPE>;
		/// TODO: static inline constexpr auto OptionalParameterCount = OPTIONAL_PARAM_COUNT;
		using ParameterIndexSequence = std::make_index_sequence<ParameterCount>;
		template <size_t INDEX>
		using ParameterType = std::tuple_element_t<INDEX, PARAMETER_TUPLE_TYPE>;
		using PointerType = PTR_TYPE;
		static constexpr PointerType Pointer = POINTER;

		template <typename... GIVEN_ARG_TYPES>
		static constexpr bool CanTake()
		{
			/// TODO: This function could technically check for the number of optional arguments in the method
			if constexpr (ParameterCount == sizeof...(GIVEN_ARG_TYPES))
			{
				using given_tuple_type = std::tuple<GIVEN_ARG_TYPES...>;
				return[]<size_t... INDICES>(std::index_sequence<INDICES...>) {
					return (std::convertible_to<std::tuple_element_t<INDICES, given_tuple_type>, ParameterType<INDICES>> && ...);
				}(std::make_index_sequence<sizeof...(GIVEN_ARG_TYPES)>{});
			}
			else
				return false;
		}

		template <typename FUNC>
		static constexpr void CallForEachParameter(FUNC&& func)
		{
			[func] <size_t... INDICES>(std::index_sequence<INDICES...>) {
				(func(ParameterInfo<INDICES, ParameterType<INDICES>>{}), ...);
			}(ParameterIndexSequence{});
		}

		static constexpr bool HasFlag(MethodFlags flag_value) noexcept { return (Flags & (1ULL << uint64_t(flag_value))) != 0; }

	private:

		template <size_t INDEX, typename TYPE/*, CompileTimeLiteral NAME*/> struct ParameterInfo
		{
			static constexpr size_t Index = INDEX;
			using Type = TYPE;
			//static constexpr std::string_view name = NAME_CTL.Value;
		};

	};

	/// TODO: We definitely should have CompileTimeClassData and a ForEachClass() function (same for enums)

	enum class ClassFlags
	{
		Struct,         /// TODO: Should we docnote this?
		DeclaredStruct, /// TODO: Should we docnote this?
		NoConstructors, /// TODO: Should we docnote this?
		HasProxy
	};

	struct ClassReflectionData
	{
		std::string_view Name = "";
		std::string_view FullType = "";
		std::string_view BaseClassName = "";
		std::string_view Attributes = "{}";
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON;
#endif
		uint64_t UID = 0;
		void* (*Constructor)(const ClassReflectionData&) = {};
		void (*Destructor)(void*) = {};

		/// These are vectors and not e.g. initializer_list's because you might want to create your own classes
		std::vector<FieldReflectionData> Fields;
		std::vector<MethodReflectionData> Methods;

#if REFLECTOR_USES_JSON
		void(*JSONLoadFieldsFunc)(void* dest_object, REFLECTOR_JSON_TYPE const& src_object);
		void(*JSONSaveFieldsFunc)(void const* src_object, REFLECTOR_JSON_TYPE& dest_object);
#endif

		auto FindField(std::string_view name) const->FieldReflectionData const*;
		template <typename T>
		auto FindFirstFieldByType() const->FieldReflectionData const*;
		auto FindFirstMethod(std::string_view name) const->MethodReflectionData const*;
		auto FindAllMethods(std::string_view name) const->std::vector<MethodReflectionData const*>;
		template <typename FUNC>
		void ForAllMethodsWithName(std::string_view name, FUNC&& func) const;
		template <typename... ARGS>
		auto FindMethod(std::string_view name, std::type_identity<std::tuple<ARGS...>> = {}) const->MethodReflectionData const*;

		std::type_index TypeIndex = typeid(void);

		uint64_t Flags = 0;

		bool IsStruct() const { return (Flags & (1ULL << uint64_t(ClassFlags::Struct))) != 0; }
		bool WasDeclaredStruct() const { return (Flags & (1ULL << uint64_t(ClassFlags::DeclaredStruct))) != 0; }
		bool HasConstructors() const { return (Flags & (1ULL << uint64_t(ClassFlags::NoConstructors))) == 0; }
		bool HasProxy() const { return (Flags & (1ULL << uint64_t(ClassFlags::HasProxy))) != 0; }
	};

	enum class EnumFlags
	{
		Dummy
	};

	enum class EnumeratorFlags
	{
		Dummy
	};

	enum class FieldFlags
	{
		NoSetter,
		NoGetter,
		NoEdit,
		NoScript,
		NoSave,
		NoLoad,
		NoDebug,

		NoUniqueAddress,

		Required,
		Artificial,
		Static,
		Mutable,
		DeclaredPrivate,
	};

	struct FieldReflectionData
	{
		std::string_view Name = "";
		std::string_view FieldType = "";
		std::string_view Initializer = "";
		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON;
#endif
		std::type_index FieldTypeIndex = typeid(void);
		uint64_t Flags = 0;

		ClassReflectionData const* ParentClass = nullptr;
	};

	template <typename CTRD>
	struct FieldVisitorData : public CTRD
	{
		explicit FieldVisitorData(FieldReflectionData const* data) : Data(data) {}

		FieldReflectionData const* Data{};
	};

	enum class MethodFlags
	{
		Explicit,
		Inline,
		Virtual,
		Static,

		Const,
		Noexcept,
		Final,

		Abstract,
		Artificial,
		HasBody,
		NoCallable,
		Proxy,
		NoReturn,
		NoDiscard,
		ForFlag,
	};

	struct MethodReflectionData
	{
		struct Parameter
		{
			std::string Name;
			std::string Type;
			std::string Initializer;
		};

		std::string_view Name = "";
		std::string_view ReturnType = "";
		std::string_view Parameters = "";
		std::vector<Parameter> ParametersSplit{}; /// TODO: Could be given at compile-time as well
		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON;
#endif

		std::string_view UniqueName = ""; /// TODO: Could be given at compile-time as well
		std::string_view ArtificialBody = "";
		std::type_index ReturnTypeIndex = typeid(void);
		std::vector<std::type_index> ParameterTypeIndices = {};
		uint64_t Flags = 0;
		uint64_t UID = 0;

		ClassReflectionData const* ParentClass = nullptr;
	};

	template <typename CTRD>
	struct MethodVisitorData : public CTRD
	{
		explicit(false) MethodVisitorData(MethodReflectionData const* data) : Data(data) {}

		MethodReflectionData const* Data{};
		typename CTRD::PointerType Pointer = CTRD::Pointer;
	};

	struct EnumeratorReflectionData
	{
		std::string_view Name = "";
		int64_t Value;
		uint64_t Flags = 0;
	};

	struct EnumReflectionData
	{
		std::string_view Name = "";
		std::string_view FullType = "";
		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON;
#endif
		std::vector<EnumeratorReflectionData> Enumerators;
		std::type_index TypeIndex = typeid(void);
		uint64_t Flags = 0;
		uint64_t UID = 0;
	};

	struct Reflectable
	{
		virtual ClassReflectionData const& GetReflectionData() const
		{
			static const ClassReflectionData data = {
				.Name = "Reflectable",
				.FullType = "Reflector::Reflectable",
				.BaseClassName = "",
				.Attributes = "",
#if REFLECTOR_USES_JSON
				.AttributesJSON = {},
#endif
				.TypeIndex = typeid(Reflectable)
			};
			return data;
		}

		Reflectable() noexcept = default;
		explicit Reflectable(::Reflector::ClassReflectionData const& klass) noexcept : mClass(&klass) {}

		template <typename T> bool Is() const { return dynamic_cast<T const*>(this) != nullptr; }
		template <typename T> T const* As() const { return dynamic_cast<T const*>(this); }
		template <typename T> T* As() { return dynamic_cast<T*>(this); }

		virtual ~Reflectable() = default;

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)

		/// TODO: Could we turn these into to_json/from_json?

		virtual void JSONLoadFields(REFLECTOR_JSON_TYPE const& src_object) {}
		virtual void JSONSaveFields(REFLECTOR_JSON_TYPE& src_object) const {}

#endif

	protected:

		ClassReflectionData const* mClass = nullptr;

#if defined(REFLECTOR_USES_GC) && REFLECTOR_USES_GC

	public:

		virtual void GCMark(class GCHeap* heap) const
		{
			mFlags |= (1ULL << int(Flags::Marked));
		}

		bool IsMarked() const noexcept { return (mFlags & (1ULL << int(Flags::Marked))) != 0; }

		class GCHeap* GetParentHeap() const { return mParentHeap; }

		template <typename T, typename... ARGS>
		T* New(ARGS&&... args);

	protected:

		enum class Flags { Marked, };

		friend class GCHeap;

		mutable uintptr_t mFlags = 0;
		class GCHeap* mParentHeap = nullptr;
#endif
	};

	template <typename T, typename PROXY_OBJ>
	struct ProxyFor
	{
		using Type = void;
	};

	template <typename ENUM>
	constexpr bool IsReflectedEnum()
	{
		return false;
	}
	template <typename ENUM>
	::Reflector::EnumReflectionData const& GetEnumReflectionData();


#if defined(REFLECTOR_USES_GC) && REFLECTOR_USES_GC

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
	concept has_mark_func = requires (T const& val, class GCHeap* heap) { val.GCMark(heap); };
	
	template <typename T, typename PT = std::remove_cvref_t<T>>
	requires (!has_mark_func<PT> 
		&& !std::derived_from<std::remove_pointer_t<PT>, Reflectable>
		&& !std::is_pointer_v<PT>
	)
	void GCMark(class GCHeap const*, T&&)
	{
		/// Don't mark stuff that isn't an aggregate
		static_assert(!could_be_marked<T>::value, "Type is an aggregate but cannot be marked. Make sure you overload the Reflector::GCMark(GCHeap*, T&) function.");
	}

	template <typename T> void GCMark(class GCHeap* heap, std::basic_string<T> const& val) {}
	template <typename T> void GCMark(class GCHeap* heap, std::basic_string_view<T> const& val) {}
	template <typename... ELS>
	void GCMark(class GCHeap* heap, std::tuple<ELS...> const& val) { std::apply([heap](auto const& ...x) { (GCMark(heap, x), ...); }, val); }
	template <typename F, typename S>
	void GCMark(class GCHeap* heap, std::pair<F, S> const& val) { GCMark(heap, val.first); GCMark(heap, val.second); }

	template <typename T>
	requires (has_mark_func<std::remove_cvref_t<T>>)
	void GCMark(class GCHeap* heap, T& val)
	{
		val.GCMark(heap);
	}

	inline void GCMark(class GCHeap* heap, Reflectable const* r)
	{
		if (r && heap == r->GetParentHeap() && !r->IsMarked())
			r->GCMark(heap);
	}

	template <typename T>
	void GCMark(class GCHeap* heap, T const* r);

	template <std::ranges::range RANGE>
	void GCMark(class GCHeap* heap, RANGE&& range)
	{
		if constexpr (could_be_marked<std::ranges::range_value_t<RANGE>>::value)
		{
			for (auto&& val : range)
				GCMark(heap, val);
		}
	}

#endif
}