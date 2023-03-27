# Ideas

```c++
inline const AttributeProperties atEnumeratorSetter{ "Setter" /* "SetterName" ? */, "Only valid on Flag enums, will change the setter for this flag (if one is created) to this value", json::value_t::string};
````
```c++
inline const AttributeProperties atFieldTypeList{ "TypeList", "If set to an (reflected) enum name, creates IsX() { this->field == (decltype(this->field))N; } functions for each enumerator in the enum", json::value_t::string };
```
```c++
inline const AttributeProperties atClassCreateIsChilds{ "CreateIsChilds", "Creates functions IsX (and AsX equivalents) for each subclass of this class in the given list, that checks if this object is of subclass X", json::value_t::array };
```
```c++
inline const AttributeProperties atFieldScriptAccess{ "ScriptAccess", "Whether or not to hook up the generated accessors (getter, setter) to the scripting system", json::value_t::boolean, true };
```

* [field] Optional - requires an optional type (like std::optional); will create Is#field_name and Reset#field_name in addition to regular accessors
* [any] Internal - do not document
* [field] ExposeMethods=[names,of,methods] - will create `template <typename... ARGS> auto method_name(ARGS&&... args) { return field_name.method_name(std::forward<ARGS>(args)...); }`
		Or better yet, RField(ExposeMethods={GetSpeed=GetAnimationSpeed, ...}) Animation mAnimation;
* [record/field] OnAfterLoad=funcname, OnBeforeSave=funcname
* [record/field] CustomEditor=funcname
* [field] Min/Max/Step - how to handle?
		ForceLimits - will clamp the input value in the setter
* [field] VectorGetters=true/element_name - creates Get#At(), Get#Count()
* [field] VectorSetters=true/element_name - creates Set#At(), Push#(), Erase#(), Insert#(), Clear#s(), Resize#(), Pop#()
* [field] MapGetters - Find#()
* [field] MapSetters
* [field] Validator=funcname - will generate a ValidateFields(callback) function for the class (customizable)
* [any] Plural/Singular - whether to create Is or Are prefixes and such

* Children/ParentPointer/NameInHierarchy - combine these with a `generic_path` type to create a general hierarchy system (maybe with named hierarchies?)
	Maybe something like a class attribute RClass(Hierarchies=[list, of, hierarchies, ...]) that creates accessors like Get#Parent, Get#Root for each hierarchy
		and if there is only one hierarchy (or Hierarchy=true), the names are simpler

	`RField(Children, ChildPaths=Direct/Named)` - on a child vector; if Direct (default) paths are 'this_object/child_object', if Named, paths are 'this_object/field_name/child_object'

	`RField(Children, Owned)` - on a child vector, has an effect on whether or not the children are fully serialized

	So, for example:

	```c++
	RClass(Hierarchy, Root); /// or Hierarchies=[Game,Location,Object], RootFor=[Game,Location]
	class Game {
		RField(Children, ChildPaths=Named);
		vector<uptr<Location>> Locations;

		RField(Children, ChildPaths=Named);
		vector<uptr<Achievement>> Achievements;
	};

	RClass(Hierarchy); /// or Hierarchies=[Location, Object], RootFor=[Object]
	class Location {
		RField(NameInHierarchy);
		string Name;

		RField(ParentPointer);
		Location* ParentLocation = nullptr;

		RField(Children, ChildPaths=Direct);
		vector<uptr<Object>> Objects;
	};
	```

	or maybe something like RHierarchyRoot(hierarchy_name) + RHierarchyChild(hierarchy_name) + RHierarchyLeaf(hierarchy_name) along with like
	a special templated class that stores hierarchy info?

	Like:

	`RField(HierarchyRoot/Child/Leaf=hierarchy_name);`
	`Reflector::HierarchyRoot<child_vector_type>/HierarchyChild<root_type, sibling_type>/HierarchyLeaf<root_type, sibling_type> hierarchy_name##Data;`
