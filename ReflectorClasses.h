#pragma once

#include <typeindex>
#include <vector>
#include <string_view>

namespace Reflector
{
	struct ClassReflectionData;
	struct FieldReflectionData;
	struct MethodReflectionData;

	struct ClassReflectionData
	{
		std::string_view Name;
		std::string_view ParentClassName;
		std::string_view Properties = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json PropertiesJSON;
#endif
		void* (*Constructor)(const ::Reflector::ClassReflectionData&) = {};

		/// These are vectors and not e.g. initializer_list's because you might want to create your own classes
		std::vector<FieldReflectionData> Fields; 
		std::vector<MethodReflectionData> Methods;

		std::type_index TypeIndex;
	};

	struct FieldReflectionData
	{
		std::string_view Name;
		std::string_view FieldType;
		std::string_view Initializer;
		std::string_view Properties = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json PropertiesJSON;
#endif
		std::type_index FieldTypeIndex;

		ClassReflectionData const* ParentClass = nullptr;
	};

	struct MethodReflectionData
	{
		std::string_view Name;
		std::string_view ReturnType;
		std::string_view Parameters;
		std::string_view Properties = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json PropertiesJSON;
#endif
		std::type_index ReturnTypeIndex;

		ClassReflectionData const* ParentClass = nullptr;
	};

	struct EnumeratorReflectionData
	{
		std::string_view Name;
		int64_t Value;
	};

	struct EnumReflectionData
	{
		std::string_view Name;
		std::string_view Properties = "{}";
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
				.Properties = "",
#ifdef NLOHMANN_JSON_VERSION_MAJOR
				.PropertiesJSON = ::nlohmann::json::object(),
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