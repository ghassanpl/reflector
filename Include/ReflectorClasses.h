#pragma once

#if (__cplusplus < 202002L) && (defined(_MSVC_LANG) && _MSVC_LANG < 202002L)
#error "Reflector requires C++20"
#endif

#include <typeindex>
#include <vector>
#include <compare>
#include <string>
#include <tuple>
#if REFLECTOR_USES_JSON
#include REFLECTOR_JSON_HEADER
#endif
#if REFLECTOR_USES_GC
#include <set>
#endif

namespace Reflector
{
	enum class MethodFlags;
	struct Field;
	struct Method;
	struct Property;
	struct Class;

	template <typename T> concept reflected_class = requires {
		T::StaticClassFlags();
		{ T::StaticGetReflectionData() } -> std::same_as<Class const&>;
		typename T::self_type;
	}&& std::same_as<T, typename T::self_type>;

	enum class AccessMode { Unspecified, Public, Private, Protected };

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
	struct CompileTimeCommonData
	{
		using Type = std::remove_cvref_t<FIELD_TYPE>;
		using ParentType = PARENT_TYPE;
		static constexpr uint64_t Flags = FLAGS;
		static constexpr std::string_view Name = NAME_CTL.Value;

		template <typename FLAG_TYPE>
		static constexpr bool HasFlag(FLAG_TYPE flag_value) noexcept { return (Flags & (1ULL << uint64_t(flag_value))) != 0; }

		template <typename TYPE>
		static constexpr bool IsType() noexcept { return std::is_same_v<TYPE, FIELD_TYPE>; }
	};
	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL, typename PTR_TYPE, PTR_TYPE POINTER>
	struct CompileTimeFieldData : CompileTimeCommonData<FIELD_TYPE, PARENT_TYPE, FLAGS, NAME_CTL>
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

		static auto CopySetter(PARENT_TYPE* obj, FIELD_TYPE const& value) -> void { (obj->*Pointer) = value; }
		static auto MoveSetter(PARENT_TYPE* obj, FIELD_TYPE&& value) -> void { (obj->*Pointer) = std::move(value); }
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericCopySetter(PARENT* obj, FIELD const& value) -> void { (obj->*Pointer) = value; }
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericMoveSetter(PARENT* obj, FIELD& value) -> void { (obj->*Pointer) = std::move(value); }

		static auto VoidGetter(void const* obj) -> void const* { return &(obj->*Pointer); }
		static auto VoidSetter(void* obj, void const* value) -> void { (obj->*Pointer) = static_cast<FIELD_TYPE const*>(value); }
	};

	template <typename RETURN_TYPE, typename PARAMETER_TUPLE_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL, typename PTR_TYPE, PTR_TYPE POINTER, AccessMode ACCESS_MODE>
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
		static constexpr AccessMode Access = ACCESS_MODE;

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
			[func = std::forward<FUNC>(func)] <size_t... INDICES>(std::index_sequence<INDICES...>) {
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
	
	enum class EntityFlags
	{
		Unimplemented,
		Deprecated,
		MaxEntityFlags = 16,
	};
	inline constexpr int MaxEntityFlags = (int)EntityFlags::MaxEntityFlags;

	enum class ClassFlags
	{
		Struct = MaxEntityFlags, /// TODO: Should we docnote this?
		DeclaredStruct,          /// TODO: Should we docnote this?
		NoConstructors,          /// TODO: Should we docnote this?
		HasProxy,
		NotSerializable, /// TODO: Since we have strict attributes, we could generate flags for all of the bool ones, for example as a "AttributeFlags" field
		NotEditable,
		NotScriptable,
	};

	void* AlignedAlloc(size_t alignment, size_t size);
	void AlignedFree(void* obj);

	struct Class
	{
		std::string_view Name = {};
		std::string_view DisplayName = {};
		std::string_view FullType = {};
		std::string_view BaseClassName = {};
		std::string_view Attributes = "{}";
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON = REFLECTOR_JSON_TYPE::object();
#endif
		uint64_t ReflectionUID = 0;
		std::string GUID = {};
		size_t Alignment{};
		size_t Size{};
		void (*DefaultPlacementConstructor)(void*) = {};
		void* (*DefaultConstructor)() = {};
		void (*Destructor)(void*) = {};

		void* Alloc() const;
		void Delete(void* obj) const;
		void* NewDefault(void* at) const;

		template <typename T>
		T* New() const
		{
			if (DefaultConstructor)
				return static_cast<T*>(DefaultConstructor());
			return nullptr;
		}
		void* New() const
		{
			if (DefaultConstructor)
				return DefaultConstructor();
			return nullptr;
		}

		/// These are vectors and not e.g. initializer_list's because you might want to create your own classes
		std::vector<Field> Fields;
		std::vector<Method> Methods;
		std::vector<Property> Properties;

#if REFLECTOR_USES_JSON
		void(*JSONLoadFieldsFunc)(void* dest_object, REFLECTOR_JSON_TYPE const& src_object);
		void(*JSONSaveFieldsFunc)(void const* src_object, REFLECTOR_JSON_TYPE& dest_object);

		template <typename T>
		T AttributeValue(std::string_view attr_name, T&& default_value) const
		{
			if (!AttributesJSON.is_object())
				return std::forward<T>(default_value);
			return AttributesJSON.value(attr_name, std::forward<T>(default_value));
		}
#endif
		auto FindField(std::string_view name) const->Field const*;
		template <typename T>
		auto FindFirstFieldByType() const->Field const*;
		auto FindFirstMethod(std::string_view name) const->Method const*;
		auto FindAllMethods(std::string_view name) const->std::vector<Method const*>;
		template <typename FUNC>
		void ForAllMethodsWithName(std::string_view name, FUNC&& func) const;
		template <typename... ARGS>
		auto FindMethod(std::string_view name, std::type_identity<std::tuple<ARGS...>> = {}) const->Method const*;

		auto FindBaseClass() const -> Class const*;
		auto HasBaseClass(std::string_view base_klass_name) const -> bool;
		auto HasBaseClass(Class const& klass) const -> bool;
		template <typename U>
		requires reflected_class<U>
		auto HasBaseClass() const -> bool;

		std::type_index TypeIndex = typeid(void);

		uint64_t Flags = 0;

		/*
		bool IsDerivedFrom(Class const& klass) const
		{
			if (this->)
		}
		bool IsBaseOf(Class const& klass) const
		{

		}
		*/

		constexpr bool IsStruct() const { return (Flags & (1ULL << uint64_t(ClassFlags::Struct))) != 0; }
		constexpr bool WasDeclaredStruct() const { return (Flags & (1ULL << uint64_t(ClassFlags::DeclaredStruct))) != 0; }
		constexpr bool HasConstructors() const { return (Flags & (1ULL << uint64_t(ClassFlags::NoConstructors))) == 0; }
		constexpr bool HasProxy() const { return (Flags & (1ULL << uint64_t(ClassFlags::HasProxy))) != 0; }
		constexpr bool NotSerializable() const { return (Flags & (1ULL << uint64_t(ClassFlags::NotSerializable))) != 0; }
		constexpr bool IsSerializable() const { return (Flags & (1ULL << uint64_t(ClassFlags::NotSerializable))) == 0; }
		constexpr bool NotEditable() const { return (Flags & (1ULL << uint64_t(ClassFlags::NotEditable))) != 0; }
		constexpr bool IsEditable() const { return (Flags & (1ULL << uint64_t(ClassFlags::NotEditable))) == 0; }
		constexpr bool NotScriptable() const { return (Flags & (1ULL << uint64_t(ClassFlags::NotScriptable))) != 0; }
		constexpr bool IsScriptable() const { return (Flags & (1ULL << uint64_t(ClassFlags::NotScriptable))) == 0; }
	};

	enum class EnumFlags
	{
		Dummy = MaxEntityFlags
	};

	enum class EnumeratorFlags
	{
		Dummy = MaxEntityFlags
	};

	enum class FieldFlags
	{
		NoSetter = MaxEntityFlags,
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

		BraceInitialized,

		/// TODO: ReadOnly
	};

	struct Field
	{
		std::string_view Name;
		std::string_view DisplayName;
		std::string_view FieldType;
		std::string_view Initializer;
		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
		/// TODO: std::string_view ScriptName; /// Set to ScriptName if not empty, else UniqueName if not empty, otherwise Name
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON = REFLECTOR_JSON_TYPE::object();
#endif
		std::type_index FieldTypeIndex = typeid(void);
		uint64_t Flags = 0;

		Class const* ParentClass = nullptr;
	};

	template <typename CTRD>
	struct FieldVisitorData : CTRD
	{
		explicit FieldVisitorData(Field const* data) : Data(data) {}

		Field const* Data{};
	};

	enum class MethodFlags
	{
		Explicit = MaxEntityFlags,
		Inline,
		Virtual,
		Static,

		Const,
		Noexcept,
		Final,

		Abstract,
		Artificial,
		HasBody,
		NoScript,
		Proxy,
		NoReturn,
		NoDiscard,
		ForFlag,
	};

	struct Method
	{
		struct Parameter
		{
			std::string Name;
			std::string Type;
			std::string Initializer;
		};

		std::string_view Name;
		std::string_view DisplayName;
		std::string_view ReturnType = "void";
		std::string_view Parameters;
		std::vector<Parameter> ParametersSplit{}; /// TODO: Could be given at compile-time as well
		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON = REFLECTOR_JSON_TYPE::object();
#endif

		std::string_view UniqueName; /// TODO: Could be given at compile-time as well
		std::string_view ArtificialBody;
		std::type_index ReturnTypeIndex = typeid(void);
		std::vector<std::type_index> ParameterTypeIndices = {};
		uint64_t Flags = 0;
		uint64_t UID = 0;

		/// TODO: std::string_view ScriptName; /// Set to ScriptName if not empty, else UniqueName if not empty, otherwise Name
		std::string_view ScriptName() const { return UniqueName.empty() ? Name : UniqueName; }

		Class const* ParentClass = nullptr;
	};

	template <typename CTRD>
	struct MethodVisitorData : CTRD
	{
		explicit(false) MethodVisitorData(Method const* data) : Data(data) {}

		Method const* Data{};
		typename CTRD::PointerType Pointer = CTRD::Pointer;
	};

	struct Enumerator
	{
		std::string_view Name;
		std::string_view DisplayName;
		int64_t Value;
		uint64_t Flags = 0;

		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON = REFLECTOR_JSON_TYPE::object();
#endif
	};

	struct Enum
	{
		std::string_view Name;
		std::string_view DisplayName;
		std::string_view FullType;
		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON = REFLECTOR_JSON_TYPE::object();
#endif
		std::vector<Enumerator> Enumerators;
		std::type_index TypeIndex = typeid(void);
		uint64_t Flags = 0;
		uint64_t UID = 0;
	};

	enum class PropertyFlags
	{
		NoEdit = MaxEntityFlags,
		NoScript, /// TODO: We should probably move NoScript (and maybe others) to EntityFlags
		NoDebug,

		FromField,
	};

	struct Property
	{
		std::string_view Name;
		std::string_view DisplayName;
		std::string_view Type;
		std::string_view Attributes = "{}"; /// TODO: Could be given at compile-time as well
#if REFLECTOR_USES_JSON
		REFLECTOR_JSON_TYPE AttributesJSON = REFLECTOR_JSON_TYPE::object();
#endif
		void(*Getter)(void const*, void*) = {};
		void(*Setter)(void*, void const*) = {};
		std::type_index PropertyTypeIndex = typeid(void);
		uint64_t Flags = 0;

		Class const* ParentClass = nullptr;
	};

	template <typename CTRD>
	struct PropertyVisitorData : CTRD
	{
		explicit PropertyVisitorData(Property const* data) : Data(data) {}

		Property const* Data{};
	};

	struct Reflectable
	{
		using Class = Class;
		using Method = Field;
		using Field = Field;

		virtual Class const& GetReflectionData() const;
		static Class const& StaticGetReflectionData();
		Class const* GetRuntimeClass() const { return mClass_; }

		template <typename VISITOR> static void ForEachMethod(VISITOR&& visitor) { }
		template <typename VISITOR> static void ForEachField(VISITOR&& visitor) { }
		template <typename VISITOR> static void ForEachProperty(VISITOR&& visitor) { }

		template <typename T> bool Is() const { return dynamic_cast<T const*>(this) != nullptr; }
		template <typename T> T const* As() const { return dynamic_cast<T const*>(this); }
		template <typename T> T* As() { return dynamic_cast<T*>(this); }

		explicit Reflectable() noexcept
		{
		}

		Reflectable(Reflectable const&) noexcept = delete;
		Reflectable& operator=(Reflectable const&) noexcept = delete;

		explicit Reflectable(Class const& klass) noexcept
			: mClass_(&klass)
		{

		}

		virtual ~Reflectable() noexcept = default;

		constexpr auto operator<=>(Reflectable const&) const noexcept = default;

#if REFLECTOR_USES_JSON && defined(NLOHMANN_JSON_NAMESPACE_BEGIN)

		/// TODO: Could we turn these into to_json/from_json?

		virtual void JSONLoadFields(REFLECTOR_JSON_TYPE const& src_object) {}
		virtual void JSONSaveFields(REFLECTOR_JSON_TYPE& src_object) const {}

#endif

	protected:

		Class const* mClass_;

		void SetClass(Class const* klass)
		{
			mClass_ = klass;
		}
		
#if defined(REFLECTOR_USES_GC) && REFLECTOR_USES_GC

		enum class Flags { Dirty, OnHeap, Marked, };

		friend struct Heap;

		mutable uint32_t mFlags;

	private:

		explicit Reflectable(Class const& klass, uint32_t flags) noexcept : mClass_(&klass), mFlags(flags) {}

	public:

		virtual void GCMark() const
		{
			if (GC_IsOnHeap())
				mFlags |= (1ULL << int(Flags::Marked));
		}

		bool GC_IsMarked() const noexcept { return (mFlags & (1ULL << int(Flags::Marked))) != 0; }
		bool GC_IsOnHeap() const noexcept { return (mFlags & (1ULL << int(Flags::OnHeap))) != 0; }

		template <typename T, typename... ARGS>
		T* New(ARGS&&... args);
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
	Enum const& GetEnumReflectionData();

	template <typename ENUM>
	Enumerator const* GetEnumeratorReflectionData(ENUM value)
	{
		auto& henum = GetEnumReflectionData<ENUM>();
		for (auto& enumerator : henum.Enumerators)
			if (enumerator.Value == (int64_t)value)
				return &enumerator;
		return nullptr;
	}

	template <typename T> concept derives_from_reflectable = (::Reflector::reflected_class<std::remove_cvref_t<T>> && std::derived_from<std::remove_cvref_t<T>, Reflectable>) 
		|| std::same_as<std::remove_cvref_t<T>, Reflectable>;

	template <typename T> concept reflected_enum = IsReflectedEnum<T>();

}