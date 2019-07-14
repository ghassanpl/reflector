#include "Component.h"
#include <fmt/format.h>

struct FieldVisitor
{
	template <typename T>
	void operator()(const Reflector::FieldReflectionData* data, T&& pointer, std::true_type can_edit)
	{
		fmt::print("Field: {}\n", data->Name);
	}

	template <typename T>
	void operator()(const Reflector::FieldReflectionData* data, T&& pointer, std::false_type cant_edit)
	{
		fmt::print("Field: {}\n", data->Name);
	}
}; 

struct MethodVisitor
{
	template <typename T, typename U>
	void operator()(const Reflector::MethodReflectionData* data, T&& pointer, U&& ptr)
	{
		fmt::print("Method: {} ({})\n", data->Name, typeid(U).name());
	}
};

int main()
{
	Ass::Component component;

	Ass::Component::StaticVisitFields(FieldVisitor{});
	Ass::Component::StaticVisitMethods(MethodVisitor{});
	return 0;
}
