#pragma once

#include <typeindex>
#include <vector>

template <typename T> concept is_reflected_class = requires { T::StaticClassFlags(); };

namespace Reflector
{
	struct ClassReflectionData;
	struct FieldReflectionData;
	struct MethodReflectionData;

	struct ClassReflectionData
	{
		const char* Name = "";
		const char* ParentClassName = "";
		const char* Attributes = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json AttributesJSON;
#endif
		uint64_t UID = 0;
		void* (*Constructor)(const ::Reflector::ClassReflectionData&) = {};

		/// These are vectors and not e.g. initializer_list's because you might want to create your own classes
		std::vector<FieldReflectionData> Fields; 
		std::vector<MethodReflectionData> Methods;

		std::type_index TypeIndex;

		uint64_t Flags = 0;
	};

	enum class ClassFlags
	{
		Struct,
		DeclaredStruct,
		NoConstructors,
		HasProxy
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
	};

	enum class MethodFlags
	{
		Inline,
		Virtual,
		Abstract,
		Static,
		Const,
		Noexcept,
		Final,
		Explicit,
		Artificial,
		HasBody,
		NoCallable
	};

	enum class EnumFlags
	{
	};

	enum class EnumeratorFlags
	{
	};

	template<size_t N>
	struct CompileTimeLiteral
	{
		constexpr CompileTimeLiteral(char const (&s)[N]) { std::copy_n(s, N, this->value); }
		constexpr std::strong_ordering operator<=>(CompileTimeLiteral const&) const = default;
		char value[N];
	};
	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL>
	struct CompileTimePropertyData
	{
		using type = std::remove_cvref_t<FIELD_TYPE>;
		using parent_type = PARENT_TYPE;
		static constexpr uint64_t flags = FLAGS;
		static constexpr const char* name = NAME_CTL.value;

		template <typename FLAG_TYPE>
		static constexpr bool HasFlag(FLAG_TYPE flag_value) noexcept { return (flags & (1ULL << uint64_t(flag_value))) != 0; }
	};
	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, CompileTimeLiteral NAME_CTL, typename PTR_TYPE, PTR_TYPE POINTER>
	struct CompileTimeFieldData : CompileTimePropertyData<FIELD_TYPE, PARENT_TYPE, FLAGS, NAME_CTL>
	{
		static constexpr PTR_TYPE pointer = POINTER;

		static auto Getter(PARENT_TYPE const* obj) -> FIELD_TYPE const& { return (obj->*(pointer)); }
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericGetter(PARENT const* obj) -> FIELD const& { return (obj->*(pointer)); }

		template <typename PARENT = PARENT_TYPE, typename VALUE>
		static auto GenericSetter(PARENT* obj, VALUE&& value) -> void { (obj->*(pointer)) = std::forward<VALUE>(value); };

		static auto CopySetter(PARENT_TYPE* obj, FIELD_TYPE const& value) -> void { (obj->*(pointer)) = value; };
		static auto MoveSetter(PARENT_TYPE* obj, FIELD_TYPE&& value) -> void { (obj->*(pointer)) = std::move(value); };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericCopySetter(PARENT* obj, FIELD const& value) -> void { (obj->*(pointer)) = value; };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericMoveSetter(PARENT* obj, FIELD&& value) -> void { (obj->*(pointer)) = std::move(value); };

		static auto VoidGetter(void const* obj) -> void const* { return &(obj->*(pointer)); }
		static auto VoidSetter(void* obj, void const* value) -> void { (obj->*(pointer)) = reinterpret_cast<FIELD_TYPE const*>(value); };
	};
	template <uint64_t FLAGS, CompileTimeLiteral NAME_CTL>
	struct CompileTimeMethodData
	{
		static constexpr uint64_t flags = FLAGS;
		static constexpr const char* name = NAME_CTL.value;

		static constexpr bool HasFlag(MethodFlags flag_value) noexcept { return (flags & (1ULL << uint64_t(flag_value))) != 0; }
	};

	struct FieldReflectionData
	{
		const char* Name = "";
		const char* FieldType = "";
		const char* Initializer = "";
		const char* Attributes = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json AttributesJSON;
#endif
		std::type_index FieldTypeIndex;
		uint64_t Flags = 0;

		ClassReflectionData const* ParentClass = nullptr;
	};

	struct MethodReflectionData
	{
		const char* Name = "";
		const char* ReturnType = "";
		const char* Parameters = "";
		const char* Attributes = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json AttributesJSON;
#endif
		const char* UniqueName = "";
		const char* Body = "";
		std::type_index ReturnTypeIndex;
		uint64_t Flags = 0;
		uint64_t UID = 0;

		ClassReflectionData const* ParentClass = nullptr;
	};

	struct EnumeratorReflectionData
	{
		const char* Name = "";
		int64_t Value;
		uint64_t Flags = 0;
	};

	struct EnumReflectionData
	{
		const char* Name = "";
		const char* Attributes = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json AttributesJSON;
#endif
		std::vector<EnumeratorReflectionData> Enumerators;
		std::type_index TypeIndex;
		uint64_t Flags = 0;
		uint64_t UID = 0;
	};

	struct Reflectable
	{
		virtual ClassReflectionData const& GetReflectionData() const
		{
			static const ClassReflectionData data = { 
				.Name = "Reflectable",
				.ParentClassName = "",
				.Attributes = "",
#ifdef NLOHMANN_JSON_VERSION_MAJOR
				.AttributesJSON = ::nlohmann::json::object(),
#endif
				.TypeIndex = typeid(Reflectable)
			}; 
			return data;
		}

		Reflectable() noexcept = default;
		Reflectable(::Reflector::ClassReflectionData const& klass) noexcept : mClass(&klass) {}

		virtual ~Reflectable() = default;

	protected:

		ClassReflectionData const* mClass = nullptr;
	};

	template <typename T, typename PROXY_OBJ> 
	struct ProxyFor
	{
		using Type = void;
	};
}