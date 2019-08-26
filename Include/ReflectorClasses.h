#pragma once

#include <typeindex>
#include <vector>

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
		void* (*Constructor)(const ::Reflector::ClassReflectionData&) = {};

		/// These are vectors and not e.g. initializer_list's because you might want to create your own classes
		std::vector<FieldReflectionData> Fields; 
		std::vector<MethodReflectionData> Methods;

		std::type_index TypeIndex;
	};

	/// TODO: Actually add these
	enum class FieldFlags
	{
		NoSetter,
		NoGetter,
		NoEdit,
		NoScript,
		NoSave,
		NoLoad,
		NoDebug
	};

	enum class MethodFlags
	{
		Inline,
		Virtual,
		Static,
		Const,
		Noexcept,
		Final,
		Explicit,
		Artificial,
		HasBody,
		NoCallable
	};

	template <char... CHARS>
	struct CompileTimeLiteral
	{
		static constexpr const char value[] = { CHARS... };
	};
	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, typename NAME_CTL>
	struct CompileTimeFieldData
	{
		using type = std::remove_cvref_t<FIELD_TYPE>;
		using parent_type = PARENT_TYPE;
		static constexpr uint64_t flags = FLAGS;
		static constexpr const char* name = NAME_CTL::value;
	};
	template <uint64_t FLAGS, typename NAME_CTL>
	struct CompileTimeMethodData
	{
		static constexpr uint64_t flags = FLAGS;
		static constexpr const char* name = NAME_CTL::value;
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

		ClassReflectionData const* ParentClass = nullptr;
	};

	struct EnumeratorReflectionData
	{
		const char* Name = "";
		int64_t Value;
	};

	struct EnumReflectionData
	{
		const char* Name = "";
		const char* Attributes = "{}";
		std::vector<EnumeratorReflectionData> Enumerators;
		std::type_index TypeIndex;
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
}