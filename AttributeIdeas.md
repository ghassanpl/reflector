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

* [field] **Delegate** - creates Is*FieldName*Set() and Call*FieldName*() script-callable functions that check for std::function assignment and (optionally) call it
* [any] **Document** = true/false - whether or not to generate documentation for the entity
* [record] GenerateResetFunctions - will generate a Reset*FieldName* for each field in the class (see below), unless it has the Reset = false attribute
* [field] Reset/Resettable - will generate a Reset[FieldName] function that sets the field value to its initial value); having any of these in a class will create a Reset() function that resets every field
* [field] Optional - requires an optional type (like std::optional); will create Is#field_name and Reset#field_name in addition to regular accessors
Not sure if this is necessary, optional already has a nice api of its own
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
* [field] Validator=methodname - will generate a ValidateFields(callback) function for the class (customizable)
	`methodname` must be a valid method in the class?
	how to handle this? Should we also generate a Validate[FieldName] function? What's the callback for?
* [any] Plural/Singular - whether to create Is or Are prefixes and such
* [record] PublicFieldAccessors = false - will disable creation of Get/Set functions for public fields (also should be a global option)
* [record] DefaultMethodAttributes = {}
* [record] DefaultFieldAttributes = {}
* [enum] DefaultEnumeratorAttributes = {}

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
