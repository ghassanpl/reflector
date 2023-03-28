#pragma once

#if (__cplusplus < 202002L) && (defined(_MSVC_LANG) && _MSVC_LANG < 202002L)
#error "Reflector requires C++20"
#endif

#include <typeindex>
#include <vector>
#include <compare>
#include <iosfwd>
#include <string_view>
#if REFLECTOR_USES_JSON
#include REFLECTOR_JSON_HEADER
#endif

namespace Reflector
{
	enum class MethodFlags;
	struct FieldReflectionData;
	struct MethodReflectionData;

	template<size_t N>
	struct CompileTimeLiteral
	{
		constexpr CompileTimeLiteral(char const (&s)[N]) { std::copy_n(s, N, this->Value); }
		constexpr std::strong_ordering operator<=>(CompileTimeLiteral const&) const = default;
		constexpr bool operator==(CompileTimeLiteral const&) const = default;
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

		/// TODO: Test these (esp. for static fields)
		static auto Getter(PARENT_TYPE const* obj) -> FIELD_TYPE const& { return (obj->*(Pointer)); }
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericGetter(PARENT const* obj) -> FIELD const& { return (obj->*(Pointer)); }

		template <typename PARENT = PARENT_TYPE, typename VALUE>
		static auto GenericSetter(PARENT* obj, VALUE&& value) -> void { (obj->*(Pointer)) = std::forward<VALUE>(value); };

		static auto CopySetter(PARENT_TYPE* obj, FIELD_TYPE const& value) -> void { (obj->*(Pointer)) = value; };
		static auto MoveSetter(PARENT_TYPE* obj, FIELD_TYPE&& value) -> void { (obj->*(Pointer)) = std::move(value); };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericCopySetter(PARENT* obj, FIELD const& value) -> void { (obj->*(Pointer)) = value; };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericMoveSetter(PARENT* obj, FIELD&& value) -> void { (obj->*(Pointer)) = std::move(value); };

		static auto VoidGetter(void const* obj) -> void const* { return &(obj->*(Pointer)); }
		static auto VoidSetter(void* obj, void const* value) -> void { (obj->*(Pointer)) = reinterpret_cast<FIELD_TYPE const*>(value); };
	};

	template <typename RETURN_TYPE, typename PARAMETER_TUPLE_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL, typename PTR_TYPE, PTR_TYPE POINTER>
	struct CompileTimeMethodData
	{
		static inline constexpr uint64_t Flags = FLAGS;
		static inline constexpr std::string_view Name = NAME_CTL.Value;
		using ReturnType = std::remove_cvref_t<RETURN_TYPE>;
		using ParentType = PARENT_TYPE;
		using ParameterTupleType = PARAMETER_TUPLE_TYPE;
		static inline constexpr auto ParameterCount = std::tuple_size_v<PARAMETER_TUPLE_TYPE>;
		using ParameterIndexSequence = std::make_index_sequence<ParameterCount>;
		template <size_t INDEX>
		using ParameterType = std::tuple_element_t<INDEX, PARAMETER_TUPLE_TYPE>;
		using PointerType = PTR_TYPE;
		static inline constexpr PointerType Pointer = POINTER;

		template <typename... GIVEN_ARG_TYPES>
		static constexpr bool CanTake()
		{
			/// TODO: This function could technically check for the number of optional arguments in the method
			if constexpr (ParameterCount == sizeof...(GIVEN_ARG_TYPES))
			{
				using given_tuple_type = std::tuple<GIVEN_ARG_TYPES...>;
				return []<size_t... INDICES>(std::index_sequence<INDICES...>) {
					return (std::convertible_to<std::tuple_element_t<INDICES, given_tuple_type>, ParameterType<INDICES>> && ...);
				}(std::make_index_sequence<sizeof...(GIVEN_ARG_TYPES)>{});
			}
			else
				return false;
		}

		template <typename FUNC>
		static constexpr void CallForEachParameter(FUNC&& func)
		{
			[func]<size_t... INDICES>(std::index_sequence<INDICES...>) {
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
		Struct,
		DeclaredStruct,
		NoConstructors,
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

		/// These are vectors and not e.g. initializer_list's because you might want to create your own classes
		std::vector<FieldReflectionData> Fields;
		std::vector<MethodReflectionData> Methods;

		auto FindField(std::string_view name) const -> FieldReflectionData const*;
		template <typename T>
		auto FindFirstFieldByType() const -> FieldReflectionData const*;
		auto FindFirstMethod(std::string_view name) const -> MethodReflectionData const*;
		auto FindAllMethods(std::string_view name) const -> std::vector<MethodReflectionData const*>;
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
	};

	enum class EnumeratorFlags
	{
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
		FieldVisitorData(FieldReflectionData const* data) : Data(data) {}

		FieldReflectionData const* Data{};
		CTRD::PointerType Pointer = CTRD::Pointer;
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
		Proxy
	};

	struct MethodReflectionData
	{
		struct Parameter
		{
			std::string Name;
			std::string Type;
		};

		std::string_view Name = "";
		std::string_view ReturnType = "";
		std::string_view Parameters = "";
		std::vector<Parameter> ParametersSplit {}; /// TODO: Could be given at compile-time as well
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
		MethodVisitorData(MethodReflectionData const* data) : Data(data) {}

		MethodReflectionData const* Data{};
		CTRD::PointerType Pointer = CTRD::Pointer;
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
		Reflectable(::Reflector::ClassReflectionData const& klass) noexcept : mClass(&klass) {}

		template <typename T> bool Is() const { return dynamic_cast<T const*>(this) != nullptr; }
		template <typename T> T const* As() const { return dynamic_cast<T const*>(this); }
		template <typename T> T* As() { return dynamic_cast<T*>(this); }

		virtual ~Reflectable() = default;

	protected:

		ClassReflectionData const* mClass = nullptr;
	};

	template <typename T, typename PROXY_OBJ>
	struct ProxyFor
	{
		using Type = void;
	};

	template <typename ENUM>
	inline constexpr bool IsReflectedEnum()
	{
		return false;
	}
	template <typename ENUM>
	inline ::Reflector::EnumReflectionData const& GetEnumReflectionData();

}