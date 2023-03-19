#pragma once

#include "Common.h"

enum class AttributeFor
{
	Record,
	Class,
	Struct,
	Enum,
	Field,
};

struct AttributeProperties
{
	std::string Name;
	std::string Description;
	json::value_t ValueType{};
	json DefaultValueIfAny = nullptr;

	AttributeProperties(std::string name, std::string desc, json::value_t type, json default_value = nullptr) noexcept
		: Name(move(name))
		, Description(move(desc))
		, ValueType(type)
		, DefaultValueIfAny(std::move(default_value))
	{

	}
	AttributeProperties(std::string name, AttributeProperties const& copy) noexcept
		: AttributeProperties(copy)
	{
		Name = std::move(name);
	}

	//operator std::string const&() const noexcept { return Name; }
	//auto operator <=>(std::string const& other) const noexcept { return Name <=> other; }

	bool ExistsIn(json const& attrs) const
	{
		return attrs.contains(this->Name);
	}

	template <typename T>
	auto operator()(json const& attrs, T&& default_value) const
	{
		const auto it = attrs.find(this->Name);
		if (it == attrs.end())
			return std::forward<T>(default_value);
		if constexpr (std::is_same_v<T, bool>)
			return it->is_null() ? true : (bool)*it;
		else
			return (T)*it;
	}

	template <typename T>
	bool TryGet(json const& attrs, T& dest) const
	{
		const auto it = attrs.find(this->Name);
		if (it == attrs.end())
			return false;
		if constexpr (std::is_same_v<T, bool>)
			dest = it->is_null() ? true : (bool)*it;
		else
			dest = (T)*it;
		return true;
	}

private:

	AttributeProperties(AttributeProperties const& other) noexcept = default;
};

inline const AttributeProperties atDisplayName{ "DisplayName", "The name that is going to be displayed in editors and such", json::value_t::string };

inline const AttributeProperties atTypeNamespace{ "Namespace", "A helper since we don't parse namespaces; set this to the full namespace of the following type, otherwise you might get errors", json::value_t::string };

/// TODO: Maybe instead of Getter and Setter, just have one attribute - ReadOnly - which also influences whether or not other setters are created (like flag, vector, optional setters)

inline const AttributeProperties atFieldGetter{ "Getter", "Whether or not to create a getter for this field", json::value_t::boolean, true };
inline const AttributeProperties atFieldSetter{ "Setter", "Whether or not to create a setter for this field", json::value_t::boolean, true };
inline const AttributeProperties atFieldEditor{ "Editor", "Whether or not this field should be editable", json::value_t::boolean, true };
inline const AttributeProperties atFieldEdit{ "Edit", atFieldEditor };
inline const AttributeProperties atFieldSave{ "Save", "Whether or not this field should be saveable", json::value_t::boolean, true };
inline const AttributeProperties atFieldLoad{ "Load", "Whether or not this field should be loadable", json::value_t::boolean, true };

inline const AttributeProperties atFieldSerialize{ "Serialize", "False means both 'Save' and 'Load' are false", json::value_t::boolean, true };
inline const AttributeProperties atFieldPrivate{ "Private", "True sets 'Edit', 'Setter', 'Getter' to false", json::value_t::boolean, false };
inline const AttributeProperties atFieldParentPointer{ "ParentPointer", "Whether or not this field is a pointer to parent object, in a tree hierarchy; implies Edit = false, Setter = false", json::value_t::boolean, false };
inline const AttributeProperties atFieldRequired{ "Required", "A helper flag for the serialization system - will set the FieldFlags::Required flag", json::value_t::boolean, false };

inline const AttributeProperties atFieldOnChange{ "OnChange", "Calls the specified method when this field changes", json::value_t::string };

inline const AttributeProperties atFieldFlagGetters{ "FlagGetters", "If set to an (reflected) enum name, creates getter functions (IsFlag) for each Flag in this field; can't be set if 'Flags' is set", json::value_t::string };
inline const AttributeProperties atFieldFlags{ "Flags", "If set to an (reflected) enum name, creates getter and setter functions (IsFlag, SetFlag, UnsetFlag, ToggleFlag) for each Flag in this field; can't be set if 'FlagGetters' is set", json::value_t::string };
inline const AttributeProperties atFieldFlagNots{ "FlagNots", "Requires 'Flags' attribute. If set, creates IsNotFlag functions in addition to regular IsFlag (except for enumerators with Opposite attribute).", json::value_t::boolean, true };

inline const AttributeProperties atMethodUniqueName{ "UniqueName", "A unique (for this type) name of this method; useful for overloaded functions", json::value_t::string };
inline const AttributeProperties atMethodGetterFor{ "GetterFor", "This function is a getter for the named field", json::value_t::string };
inline const AttributeProperties atMethodSetterFor{ "SetterFor", "This function is a setter for the named field", json::value_t::string };

inline const AttributeProperties atRecordAbstract{ "Abstract", "This record is abstract (don't create special constructors)", json::value_t::boolean, false };
inline const AttributeProperties atRecordSingleton{ "Singleton", "This record is a singleton. Adds a static function 'SingletonInstance' that returns the single instance of this record", json::value_t::boolean, false };

inline const AttributeProperties atRecordCreateProxy{ "CreateProxy", "If set to false, proxy classes are not built for classes with virtual methods", json::value_t::boolean, true };

inline const AttributeProperties atEnumList{ "List", "If set to true, generates GetNext() and GetPrev() functions that return the next/prev enumerator in sequence, wrapping around", json::value_t::boolean, false };
inline const AttributeProperties atEnumeratorOpposite{ "Opposite", "Only valid on Flag enums, will create a virtual flag that is the complement of this one, for the purposes of creating getters and setters", json::value_t::string};

/// Ideas
inline const AttributeProperties atEnumeratorSetter{ "Setter" /* "SetterName" ? */, "Only valid on Flag enums, will change the setter for this flag (if one is created) to this value", json::value_t::string};
inline const AttributeProperties atFieldTypeList{ "TypeList", "If set to an (reflected) enum name, creates IsX() { this->field == (decltype(this->field))N; } functions for each enumerator in the enum", json::value_t::string };

inline const AttributeProperties atClassCreateIsChilds{ "CreateIsChilds", "Creates functions IsX (and AsX equivalents) for each subclass of this class in the given list, that checks if this object is of subclass X", json::value_t::array };

inline const AttributeProperties atFieldScriptAccess{ "ScriptAccess", "Whether or not to hook up the generated accessors (getter, setter) to the scripting system", json::value_t::boolean, true };

/// [field] Optional - requires an optional type (like std::optional); will create Is#field_name and Reset#field_name in addition to regular accessors
/// [field] ExposeMethods=[names,of,methods] - will create `template <typename... ARGS> auto method_name(ARGS&&... args) { return field_name.method_name(std::forward<ARGS>(args)...); }`
///		Or better yet, RField(ExposeMethods={GetSpeed=GetAnimationSpeed, ...}) Animation mAnimation;
/// [field] VectorGetters=true/element_name - creates Get#At(), Get#Count()
/// [field] VectorSetters=true/element_name - creates Set#At(), Push#(), Erase#(), Insert#(), Clear#s(), Resize#(), Pop#()
/// [field] MapGetters - Find#()
/// [field] MapSetters
/// [any] Internal - do not document
/// [field] Min/Max/Step - how to handle?
///		ForceLimits - will clamp the input value in the setter
/// [record/field] OnAfterLoad=funcname, OnBeforeSave=funcname
/// [record/field] CustomEditor=funcname
/// [field] Validator=funcname - will generate a ValidateFields(callback) function for the class (customizable)
/// [any] Plural/Singular - whether to create Is or Are prefixes and such

/// Children/ParentPointer/NameInHierarchy - combine these with a `generic_path` type to create a general hierarchy system (maybe with named hierarchies?)
///		Maybe something like a class attribute RClass(Hierarchies=[list, of, hierarchies, ...]) that creates accessors like Get#Parent, Get#Root for each hierarchy
///			and if there is only one hierarchy (or Hierarchy=true), the names are simpler
///		RField(Children, ChildPaths=Direct/Named) - on a child vector; if Direct (default) paths are 'this_object/child_object', if Named, paths are 'this_object/field_name/child_object'
///		RField(Children, Owned) - on a child vector, has an effect on whether or not the children are fully serialized
/// 
/// So, for example:
/// 
/// RClass(Hierarchy, Root); /// or Hierarchies=[Game,Location,Object], RootFor=[Game,Location]
/// class Game {
///		RField(Children, ChildPaths=Named);
///		vector<uptr<Location>> Locations;
/// 
///		RField(Children, ChildPaths=Named);
///		vector<uptr<Achievement>> Achievements;
/// };
/// 
/// RClass(Hierarchy); /// or Hierarchies=[Location, Object], RootFor=[Object]
/// class Location {
///		RField(NameInHierarchy);
///		string Name;
/// 
///		RField(ParentPointer);
///		Location* ParentLocation = nullptr;
/// 
///		RField(Children, ChildPaths=Direct);
///		vector<uptr<Object>> Objects;
/// };
/// 
/// or maybe something like RHierarchyRoot(hierarchy_name) + RHierarchyChild(hierarchy_name) + RHierarchyLeaf(hierarchy_name) along with like
/// a special templated class that stores hierarchy info?
/// Like:
/// RField(HierarchyRoot/Child/Leaf=hierarchy_name);
/// Reflector::HierarchyRoot<child_vector_type>/HierarchyChild<root_type, sibling_type>/HierarchyLeaf<root_type, sibling_type> hierarchy_name##Data;
