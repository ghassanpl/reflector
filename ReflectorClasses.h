#pragma once

#include <typeindex>

namespace Reflector
{
	struct Class
	{

	};

	struct ClassReflectionData
	{
		const char* ClassName;
		const char* ParentClassName;
		const char* Parameters;
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json ParametersJSON;
#endif
		std::type_index TypeIndex;
	};

	struct Reflectable
	{
		virtual ClassReflectionData const& GetReflectionData() const
		{
			static const ClassReflectionData data = { 
				.ClassName = "Reflectable",
				.ParentClassName = "",
				.Parameters = "",
#ifdef NLOHMANN_JSON_VERSION_MAJOR
				.ParametersJSON = ::nlohmann::json::object(),
#endif
				.TypeIndex = typeid(Reflectable)
			}; 
			return data;
		}

		virtual ~Reflectable() = default;
	};
}