/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "ReflectionDataBuilding.h"
#include "Attributes.h"
#include <charconv>

bool BuildDatabaseEntriesForMirror(FileWriter& output, const Options& options, FileMirror const& file);

uint64_t FileNeedsUpdating(const path& target_path, const path& source_path, const Options& opts)
{
	auto stat = std::filesystem::status(target_path);
	uint64_t file_change_time = std::max(ChangeTime, (uint64_t)std::filesystem::last_write_time(source_path).time_since_epoch().count());
	if (stat.type() != std::filesystem::file_type::not_found)
	{
		/// Open file and get first line
		std::ifstream f(target_path);
		std::string line;
		std::getline(f, line);
		if (line.empty() || line.size() < sizeof(TIMESTAMP_TEXT))
			return 1; /// corrupted file, regenerate

		uint64_t stored_change_time = 0;
		const auto time = string_view{ string_view{ line }.substr(sizeof(TIMESTAMP_TEXT) - 1) };
		const auto result = std::from_chars(to_address(time.begin()), to_address(time.end()), stored_change_time);
		if (!opts.Force && result.ec == std::errc{} && file_change_time == stored_change_time)
			return 0;
	}

	return file_change_time;
}

void WriteForwardDeclaration(FileWriter& output, const Class& klass, const Options& options)
{
	if (klass.Namespace.empty())
		output.WriteLine("{} {};", (klass.Flags.is_set(ClassFlags::DeclaredStruct) ? "struct" : "class"), klass.FullType());
	else
		output.WriteLine("namespace {} {{ {} {}; }}", klass.Namespace, klass.Flags.is_set(ClassFlags::DeclaredStruct) ? "struct" : "class", klass.Name);
}

void WriteForwardDeclaration(FileWriter& output, const Enum& henum, const Options& options)
{
	output.WriteLine("enum class {};", henum.FullType());
}

bool CreateJSONDBArtifact(path const& path, Options const& options)
{
	json db;

	for (auto& mirror : GetMirrors())
	{
		db[mirror.SourceFilePath.string()] = mirror.ToJSON();
	}

	std::ofstream jsondb{ path, std::ios_base::openmode{ std::ios_base::trunc } };
	jsondb << db.dump(1, '\t');
	jsondb.close();

	return true;
}

bool CreateReflectorClassesHeaderArtifact(path const& path, const Options& options)
{
	std::filesystem::copy_file(options.ExePath.parent_path() / "Include" / "ReflectorClasses.h", path, std::filesystem::copy_options::overwrite_existing);
	return true;
}

bool CreateReflectorDatabaseArtifact(path const& target_path, const Options& opts)
{
	FileWriter database_file{ target_path };
	
	database_file.WriteLine("#include \"Reflector.h\"");
	if (opts.GenerateTypeIndices)
	{
		database_file.WriteLine("#include \"Includes.reflect.h\"");
	}
	
	for (auto& mirror : GetMirrors())
	{
		if (!BuildDatabaseEntriesForMirror(database_file, opts, mirror))
			return false;
	}
	database_file.Close();
	
	return true;
}

bool CreateReflectorHeaderArtifact(path const& target_path, const Options& options, path const& final_path)
{
	auto rel = final_path.parent_path().lexically_relative(options.ArtifactPath);

	FileWriter reflect_file{ target_path };
	reflect_file.WriteLine("#pragma once");
	if (options.UseJSON)
	{
		reflect_file.WriteLine("#include {}", options.JSONHeaderPath);
		reflect_file.WriteLine("#define REFLECTOR_USES_JSON 1", options.MacroPrefix);
		reflect_file.WriteLine("#define REFLECTOR_JSON_TYPE {}", options.JSONType);
		reflect_file.WriteLine("#define REFLECTOR_JSON_PARSE_FUNC {}", options.JSONParseFunction);
	}
	reflect_file.WriteLine("#include \"{}/ReflectorClasses.h\"", rel.string());
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE3_IMPL(x, y, z) x ## y ## z");
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE3(x, y, z) REFLECTOR_TOKENPASTE3_IMPL(x, y, z)");
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE2_IMPL(x, y) x ## y");
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE2(x, y) REFLECTOR_TOKENPASTE2_IMPL(x, y)");
	reflect_file.WriteLine("");
	reflect_file.WriteLine("#define {}(...) REFLECTOR_TOKENPASTE2({}_GENERATED_CLASS_, __LINE__)", options.ClassPrefix, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...)", options.FieldPrefix);
	reflect_file.WriteLine("#define {}(...)", options.MethodPrefix);
	reflect_file.WriteLine("#define {}(...) REFLECTOR_TOKENPASTE2({}_GENERATED_CLASS_BODY_, __LINE__)", options.BodyPrefix, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...) REFLECTOR_TOKENPASTE2({}_ENUM_, __LINE__)", options.EnumPrefix, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...)", options.EnumeratorPrefix);
	reflect_file.WriteLine("");
	if (options.GenerateLuaFunctionBindings)
		reflect_file.WriteLine("#define {}_CALLABLE(ret, name, args) static int ScriptFunction_ ## name(struct lua_State* thread) {{ return 0; }}", options.MacroPrefix);
	reflect_file.Close();
	
	return true;
}

bool CreateIncludeListArtifact(path const& path, Options const& options)
{
	std::ofstream includes_file(path, std::ios_base::openmode{ std::ios_base::trunc });
	for (auto& mirror : GetMirrors())
	{
		includes_file << "#include " << mirror.SourceFilePath << "" << std::endl;
	}
	return true;
}

bool CreateTypeListArtifact(path const& path, Options const& options)
{
	std::ofstream classes_file(path, std::ios_base::openmode{ std::ios_base::trunc });

	for (auto& mirror : GetMirrors())
	{
		for (auto& klass : mirror.Classes)
		{
			//if (!klass.Flags.is_set(ClassFlags::Struct))
			classes_file << "ReflectClass(" << klass.Name << ", " << klass.FullType() << ")" << std::endl;
		}
		for (auto& henum : mirror.Enums)
			classes_file << "ReflectEnum(" << henum.Name << ", " << henum.FullType() << ")" << std::endl;
	}
	return true;
}

std::string BuildCompileTimeLiteral(std::string_view str)
{
	return std::format("\"{}\"",ghassanpl::string_ops::escaped(str));
}

void BuildStaticReflectionData(FileWriter& output, const Enum& henum, const Options& options)
{
	WriteForwardDeclaration(output, henum, options);
	output.WriteLine("::Reflector::EnumReflectionData const& StaticGetReflectionData_For_{}() {{", henum.GeneratedUniqueName());
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::EnumReflectionData _data = {{");
	output.CurrentIndent++;

	output.WriteLine(".Name = \"{}\",", henum.Name);
	output.WriteLine(".FullType = \"{}\",", henum.FullType());
	if (!henum.Attributes.empty())
	{
		output.WriteLine(".Attributes = {},", EscapeJSON(henum.Attributes));
		if (options.UseJSON)
			output.WriteLine(".AttributesJSON = {}({}),", options.JSONParseFunction, EscapeJSON(henum.Attributes));
	}
	output.WriteLine(".Enumerators = {{");
	output.CurrentIndent++;
	for (auto& enumerator : henum.Enumerators)
	{
		output.WriteLine("::Reflector::EnumeratorReflectionData {{");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"{}\",", enumerator.Name);
		output.WriteLine(".Value = {},", enumerator.Value);
		output.WriteLine(".Flags = {},", enumerator.Flags.bits);
		output.CurrentIndent--;
		output.WriteLine("}},");
	}
	output.CurrentIndent--;
	output.WriteLine("}},");
	output.WriteLine(".TypeIndex = typeid({}),", henum.FullType());
	output.WriteLine(".Flags = {},", henum.Flags.bits);
	output.CurrentIndent--;
	output.WriteLine("}}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}}");

	output.WriteLine("template <>");
	output.WriteLine("::Reflector::EnumReflectionData const& Reflector::Reflect<{}>() {{ return StaticGetReflectionData_For_{}(); }}", henum.FullType(), henum.GeneratedUniqueName());
}

void BuildStaticReflectionData(FileWriter& output, const Class& klass, const Options& options)
{
	WriteForwardDeclaration(output, klass, options);
	output.WriteLine("::Reflector::ClassReflectionData const& StaticGetReflectionData_For_{}() {{", klass.GeneratedUniqueName());
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::ClassReflectionData _data = {{");
	output.CurrentIndent++;
	output.WriteLine(".Name = \"{}\",", klass.Name);
	output.WriteLine(".FullType = \"{}\",", klass.FullType());
	output.WriteLine(".ParentClassName = \"{}\",", OnlyType(klass.ParentClass));

	if (!klass.Attributes.empty())
	{
		output.WriteLine(".Attributes = {},", EscapeJSON(klass.Attributes));
		if (options.UseJSON)
			output.WriteLine(".AttributesJSON = {}({}),", options.JSONParseFunction, EscapeJSON(klass.Attributes));
	}
	if (!klass.Flags.is_set(ClassFlags::NoConstructors))
		output.WriteLine(".Constructor = +[](const ::Reflector::ClassReflectionData& klass){{ return (void*)new self_type{{klass}}; }},");

	/// Fields
	output.WriteLine(".Fields = {{");
	output.CurrentIndent++;
	for (auto& field : klass.Fields)
	{
		output.WriteLine("::Reflector::FieldReflectionData {{");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"{}\",", field.Name);
		output.WriteLine(".FieldType = \"{}\",", field.Type);
		if (!field.InitializingExpression.empty())
			output.WriteLine(".Initializer = {},", EscapeJSON(field.InitializingExpression));
		if (!field.Attributes.empty())
		{
			output.WriteLine(".Attributes = {},", EscapeJSON(field.Attributes));
			if (options.UseJSON)
				output.WriteLine(".AttributesJSON = {}({}),", options.JSONParseFunction, EscapeJSON(field.Attributes));
		}
		if (options.GenerateTypeIndices)
			output.WriteLine(".FieldTypeIndex = typeid({}),", field.Type);
		output.WriteLine(".Flags = {},", field.Flags.bits);
		//output.WriteLine(".ParentClass = &_data");
		output.CurrentIndent--;
		output.WriteLine("}},");
	}
	output.CurrentIndent--;
	output.WriteLine("}},");

	/// Methods
	output.WriteLine(".Methods = {{");
	output.CurrentIndent++;
	for (auto& method : klass.Methods)
	{
		output.WriteLine("::Reflector::MethodReflectionData {{");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"{}\",", method.Name);
		output.WriteLine(".ReturnType = \"{}\",", method.Type);
		if (!method.GetParameters().empty())
			output.WriteLine(".Parameters = {},", EscapeJSON(method.GetParameters()));
		if (!method.Attributes.empty())
		{
			output.WriteLine(".Attributes = {},", EscapeJSON(method.Attributes));
			if (options.UseJSON)
				output.WriteLine(".AttributesJSON = {}({}),", options.JSONParseFunction, EscapeJSON(method.Attributes));
		}
		if (!method.UniqueName.empty())
			output.WriteLine(".UniqueName = \"{}\",", method.UniqueName);
		if (!method.Body.empty())
			output.WriteLine(".Body = {},", EscapeJSON(method.Body));
		if (options.GenerateTypeIndices)
			output.WriteLine(".ReturnTypeIndex = typeid({}),", method.Type);
		output.WriteLine(".Flags = {},", method.Flags.bits);
		//output.WriteLine(".ParentClass = &_data");
		output.CurrentIndent--;
		output.WriteLine("}},");
	}
	output.CurrentIndent--;
	output.WriteLine("}},");

	if (options.GenerateTypeIndices)
		output.WriteLine(".TypeIndex = typeid({}),", klass.FullType());
	output.WriteLine(".Flags = {}", klass.Flags.bits);
	output.CurrentIndent--;
	output.WriteLine("}}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}}");

	output.WriteLine("template <>");
	output.WriteLine("::Reflector::ClassReflectionData const& Reflector::Reflect<{}>() {{ return StaticGetReflectionData_For_{}(); }}", klass.FullType(), klass.GeneratedUniqueName());
}

bool BuildClassEntry(FileWriter& output, const FileMirror& mirror, const Class& klass, const Options& options)
{
	output.WriteLine("/// From class: {}", klass. FullType());

	/// ///////////////////////////////////// ///
	/// Forward declare all classes
	/// ///////////////////////////////////// ///

	if (options.ForwardDeclare)
	{
		WriteForwardDeclaration(output, klass, options);
	}
	output.WriteLine("::Reflector::ClassReflectionData const& StaticGetReflectionData_For_{}();", klass.GeneratedUniqueName());

	/// ///////////////////////////////////// ///
	/// Visitor macros
	/// ///////////////////////////////////// ///

	/// Field visitor
	output.StartDefine("#define {}_VISIT_{}_FIELDS({}_VISITOR)", options.MacroPrefix, klass.FullName(), options.MacroPrefix);
	//for (auto& field : klass.Fields)
	for (size_t i = 0; i < klass.Fields.size(); i++)
	{
		const auto& field = klass.Fields[i];
		const auto ptr_str = "&" + klass.FullType() + "::" + field.Name;
		output.WriteLine("{}_VISITOR(&{}::StaticGetReflectionData().Fields[{}], {}, ::Reflector::CompileTimeFieldData<{}, {}, {}, {}, decltype({}), {}>{{}});"
			, options.MacroPrefix, klass.FullType(), i, ptr_str, field.Type, klass.FullType(), field.Flags.bits, BuildCompileTimeLiteral(field.Name), ptr_str, ptr_str);
	}
	output.EndDefine("");

	/// Method visitor
	output.StartDefine("#define {0}_VISIT_{1}_METHODS({0}_VISITOR)", options.MacroPrefix, klass.FullName());
	for (size_t i = 0; i < klass.Methods.size(); i++)
	{
		const auto& method = klass.Methods[i];
		if (options.GenerateLuaFunctionBindings && !method.Flags.is_set(Reflector::MethodFlags::NoCallable)) /// if callable
		{
			output.WriteLine("{0}_VISITOR(&{1}::StaticGetReflectionData().Methods[{2}], ({3})&{1}::{4}, &{1}::ScriptFunction_{5}, ::Reflector::CompileTimeMethodData<{6}, {7}>{{}});",
				options.MacroPrefix, klass.FullType(), i, method.GetSignature(klass), method.Name, method.GeneratedUniqueName(), method.Flags.bits, BuildCompileTimeLiteral(method.Name));
		}
		else
		{
			output.WriteLine("{0}_VISITOR(&{1}::StaticGetReflectionData().Methods[{2}], ({3})&{1}::{4}, ::Reflector::CompileTimeMethodData<{5}, {6}>{{}});",
				options.MacroPrefix, klass.FullType(), i, method.GetSignature(klass), method.Name, method.Flags.bits, BuildCompileTimeLiteral(method.Name));
		}
	}
	output.EndDefine("");

	/// Property visitor
	output.StartDefine("#define {0}_VISIT_{1}_PROPERTIES({0}_VISITOR)", options.MacroPrefix, klass.FullName());
	for (auto& prop : klass.Properties)
	{
		auto& property = prop.second;
		std::string getter_name = "nullptr";
		if (!property.GetterName.empty())
			getter_name = std::format("&{}::{}", klass.FullType(), property.GetterName);
		std::string setter_name = "nullptr";
		if (!property.SetterName.empty())
			setter_name = std::format("&{}::{}", klass.FullType(), property.SetterName);
		output.WriteLine("{0}_VISITOR(&{1}::StaticGetReflectionData(), \"{2}\", {3}, {4}, ::Reflector::CompileTimePropertyData<{5}, {1}, 0ULL, {6}>{{}});",
			options.MacroPrefix, klass.FullType(), property.Name, getter_name, setter_name, property.Type, BuildCompileTimeLiteral(property.Name));
	}
	output.EndDefine("");

	/// ///////////////////////////////////// ///
	/// Class body
	/// ///////////////////////////////////// ///

	output.WriteLine("#undef {}_GENERATED_CLASS_BODY_{}", options.MacroPrefix, klass.BodyLine);
	output.StartDefine("#define {}_GENERATED_CLASS_BODY_{}", options.MacroPrefix, klass.BodyLine);

	/// All these are public
	output.CurrentIndent--;
	output.WriteLine("public:");
	output.CurrentIndent++;

	/// Typedefs
	output.WriteLine("typedef {} self_type;", klass.Name);
	if (!klass.ParentClass.empty())
	{
		output.WriteLine("typedef {} parent_type;", klass.ParentClass);
		output.WriteLine("using parent_type::parent_type;");

		/// Constructors
		if (!klass.Flags.is_set(ClassFlags::NoConstructors))
		{
			output.WriteLine("static self_type* Construct(){{ return new self_type{{StaticGetReflectionData()}}; }}");
			//output.WriteLine("static self_type* Construct(const ::Reflector::ClassReflectionData& klass){ return new self_type{klass}; }");
			output.WriteLine("{}(::Reflector::ClassReflectionData const& klass) : {}(klass) {{}}", klass.Name, klass.ParentClass);
		}
	}
	else
	{
		output.WriteLine("typedef void parent_type;");
	}

	/// Flags
	output.WriteLine("static constexpr int StaticClassFlags() {{ return {}; }}", klass.Flags.bits);

	/// Other body lines
	for (auto& line : klass.AdditionalBodyLines)
		output.WriteLine(line);

	/// ///////////////////////////////////// ///
	/// Reflection Data Method
	/// ///////////////////////////////////// ///

	//output.WriteLine("private: extern ::Reflector::ClassReflectionData const& StaticGetReflectionData_For_{}();", klass.GeneratedUniqueName());
	output.WriteLine("public: static ::Reflector::ClassReflectionData const& StaticGetReflectionData() {{ return StaticGetReflectionData_For_{}(); }}", klass.GeneratedUniqueName());

	if (!klass.ParentClass.empty())
	{
		output.WriteLine("virtual ::Reflector::ClassReflectionData const& GetReflectionData() const {{ return StaticGetReflectionData(); }}");
	}

	/// ///////////////////////////////////// ///
	/// Visitor methods
	/// ///////////////////////////////////// ///

	/// - StaticVisitMethods
	output.WriteLine("template <typename VISITOR> static void StaticVisitMethods(VISITOR&& visitor) {{");
	output.WriteLine("\t{}_VISIT_{}_METHODS(visitor);", options.MacroPrefix, klass.FullName());
	output.WriteLine("}}");
	/// - StaticVisitFields
	output.WriteLine("template <typename VISITOR> static void StaticVisitFields(VISITOR&& visitor) {{");
	output.WriteLine("\t{}_VISIT_{}_FIELDS(visitor);", options.MacroPrefix, klass.FullName());
	output.WriteLine("}}");
	/// - StaticVisitProperties
	output.WriteLine("template <typename VISITOR> static void StaticVisitProperties(VISITOR&& visitor) {{");
	output.WriteLine("\t{}_VISIT_{}_PROPERTIES(visitor);", options.MacroPrefix, klass.FullName());
	output.WriteLine("}}");

	/// ///////////////////////////////////// ///
	/// All methods
	/// ///////////////////////////////////// ///

	auto PrintPreFlags = [](enum_flags<Reflector::MethodFlags> flags) {
		std::vector<std::string_view> prefixes;
		if (flags.is_set(MethodFlags::Inline)) prefixes.push_back("inline");
		if (flags.is_set(MethodFlags::Static)) prefixes.push_back("static");
		if (flags.is_set(MethodFlags::Virtual)) prefixes.push_back("virtual");
		if (flags.is_set(MethodFlags::Explicit)) prefixes.push_back("explicit");
		return join(prefixes, " ");
	};

	auto PrintPostFlags = [](enum_flags<Reflector::MethodFlags> flags) {
		std::vector<std::string_view> suffixes;
		if (flags.is_set(MethodFlags::Const)) suffixes.push_back("const");
		if (flags.is_set(MethodFlags::Final)) suffixes.push_back("final");
		if (flags.is_set(MethodFlags::Noexcept)) suffixes.push_back("noexcept");
		return join(suffixes, " ");
	};

	for (auto& func : klass.Methods)
	{
		/// Callables for all methods
		if (options.GenerateLuaFunctionBindings && !func.Flags.is_set(Reflector::MethodFlags::NoCallable))
			output.WriteLine("{}_CALLABLE(({}), {}, ({}))", options.MacroPrefix, func.Type, func.GeneratedUniqueName(), func.GetParameters());
		if (func.Flags.is_set(Reflector::MethodFlags::Artificial))
		{
			output.WriteLine("{} auto {}({}){} -> {} {{ {} }}", PrintPreFlags(func.Flags), func.Name, func.GetParameters(), PrintPostFlags(func.Flags), func.Type, func.Body);
		}
	}

	/// Back to public
	output.EndDefine("public:");

	/// ///////////////////////////////////// ///
	/// Proxy class
	/// ///////////////////////////////////// ///

	output.WriteLine("#undef {}_GENERATED_CLASS_{}", options.MacroPrefix, klass.DeclarationLine);
	if (klass.Flags.is_set(ClassFlags::HasProxy))
	{
		output.StartDefine("#define {}_GENERATED_CLASS_{} template <typename T, typename PROXY_OBJ> struct {}_Proxy : T {{", options.MacroPrefix, klass.DeclarationLine, klass.Name);
		output.CurrentIndent++;
		output.WriteLine("mutable PROXY_OBJ ReflectionProxyObject;");
		for (auto& func : klass.Methods)
		{
			if (!func.Flags.is_set(Reflector::MethodFlags::Virtual))
				continue;

			auto base = std::format("virtual auto {0}({1})", func.Name, func.GetParameters());
			if (func.Flags.is_set(Reflector::MethodFlags::Const))
				base += " const";
			if (func.Flags.is_set(Reflector::MethodFlags::Noexcept))
				base += " noexcept";

			output.WriteLine("{} -> decltype(T::{}({})) override {{", base, func.Name, func.ParametersNamesOnly);
			output.CurrentIndent++;
			/// TODO: Change {1} to std::forward<>({1})
			output.WriteLine("using return_type = decltype(T::{0}({1}));", func.Name, func.ParametersNamesOnly);
			if (func.Flags.is_set(MethodFlags::Abstract))
				output.WriteLine("if (ReflectionProxyObject.Contains(\"{0}\")) return ReflectionProxyObject.CallOverload<return_type>(\"{0}\"{2}{1}); else ReflectionProxyObject.AbstractCall(\"{0}\");", func.Name, func.ParametersNamesOnly, (func.ParametersSplit.size() ? ", " : ""));
			else
				output.WriteLine("return ReflectionProxyObject.Contains(\"{0}\") ? ReflectionProxyObject.CallOverload<return_type>(\"{0}\"{2}{1}) : T::{0}({1});", func.Name, func.ParametersNamesOnly, (func.ParametersSplit.size() ? ", " : ""));
			output.CurrentIndent--;
			output.WriteLine("}}");
		}
		output.CurrentIndent--;
		output.WriteLine("}};");
		output.EndDefine("namespace Reflector {{ template <typename PROXY_OBJ> struct ProxyFor<{0}, PROXY_OBJ> {{ using Type = {0}_Proxy<{0}, PROXY_OBJ>; }}; }}", klass.FullType());
	}
	else
		output.WriteLine("#define {}_GENERATED_CLASS_{}", options.MacroPrefix, klass.DeclarationLine);
	
	return true;
}

bool BuildEnumEntry(FileWriter& output, const FileMirror& mirror, const Enum& henum, const Options& options)
{
	auto has_any_enumerators = (henum.Enumerators.size() > 0);
	output.WriteLine("/// From enum: {}", henum.FullType());

	WriteForwardDeclaration(output, henum, options);
	output.WriteLine("namespace Reflector {{ template <> inline constexpr bool IsReflectedEnum<{}>() {{ return true; }} }}", henum.FullType());

	output.WriteLine("#undef {}_ENUM_{}", options.MacroPrefix, henum.DeclarationLine);
	output.StartDefine("#define {}_ENUM_{}", options.MacroPrefix, henum.DeclarationLine);

	/// ///////////////////////////////////// ///
	/// Declaration and Artificials
	/// ///////////////////////////////////// ///

	output.WriteLine("enum class {};", henum.Name); /// forward decl;
	output.WriteLine("static constexpr inline size_t {}Count = {};", henum.Name, henum.Enumerators.size());
	if (has_any_enumerators)
	{
		output.WriteLine("static constexpr inline const char* {}Names[] = {{ {} }};", henum.Name, ghassanpl::string_ops::join(henum.Enumerators, ", ", [](Enumerator const& enumerator) { return std::format("\"{}\"", enumerator.Name); }));
		output.WriteLine("static constexpr inline {0} {0}Values[] = {{ {1} }} ;", henum.Name, ghassanpl::string_ops::join(henum.Enumerators, ", ", [&](Enumerator const& enumerator) {
			return std::format("{}{{{}}}", henum.Name, enumerator.Value);
		}));

		output.WriteLine("static constexpr inline {0} First{0} = {0}{{{1}}};", henum.Name, henum.Enumerators.front().Value);
		output.WriteLine("static constexpr inline {0} Last{0} = {0}{{{1}}};", henum.Name, henum.Enumerators.back().Value);
	}

	output.WriteLine("inline constexpr auto operator==(std::underlying_type_t<{0}> left, {0} right) noexcept {{ return left == std::to_underlying(right); }}", henum.Name);
	output.WriteLine("inline constexpr auto operator<=>(std::underlying_type_t<{0}> left, {0} right) noexcept {{ return left <=> std::to_underlying(right); }}", henum.Name);

	output.WriteLine("inline constexpr const char* GetEnumName({0}) {{ return \"{0}\"; }}", henum.Name);
	output.WriteLine("inline constexpr size_t GetEnumCount({}) {{ return {}; }}", henum.Name, henum.Enumerators.size());
	output.WriteLine("inline constexpr const char* GetEnumeratorName({} v) {{", henum.Name);
	{
		auto indent = output.Indent();

		if (has_any_enumerators)
		{
			if (henum.IsConsecutive())
			{
				output.WriteLine("if (int64_t(v) >= {0} && int64_t(v) <= {1}) return {2}Names[int64_t(v)-{0}];", henum.Enumerators.front().Value, henum.Enumerators.back().Value, henum.Name);
			}
			else
			{
				output.WriteLine("switch (int64_t(v)) {{");
				for (auto& enumerator : henum.Enumerators)
				{
					output.WriteLine("case {}: return \"{}\";", enumerator.Value, enumerator.Name);
				}
				output.WriteLine("}}");
			}
		}
		output.WriteLine("return \"<Unknown>\";");
	}
	output.WriteLine("}}"); 
	/// TODO: Should this return std::optional?
	/// TODO: Implement the ones below and check performance using https://quick-bench.com/
	/// Cases:
	///		naive
	///		magic_enum::enum_value("str")
	///		mph
	///		switch trie
	/// TODO: EITHER: Use mph ( https://www.ibiblio.org/pub/Linux/devel/lang/c/!INDEX.short.html Q:\Code\Native\External\mph ) to generate a minimal perfect hash function for this
	/// TODO: OR, generate a TRIE and then for leafs check for equality, and for non-leafs switch against the first character:
	/*
		constexpr auto check(TT out, char const* a, char const* b) -> TT {
			return std::strcmp(a, b) ? ILLEGAL : out;
		}

		constexpr auto lookup(char const* kw) -> TT 
		{
			switch (*kw++) {
				default: return ILLEGAL;

				case 'a': return check(AND,    kw, "nd");
				case 'c': return check(CLASS,  kw, "lass");
				case 'e': return check(ELSE,   kw, "lse");
				case 'i': return check(IF,     kw, "f");
				case 'n': return check(NIL,    kw, "il");
				case 'o': return check(OR,     kw, "r");
				case 'p': return check(PRINT,  kw, "rint");
				case 'r': return check(RETURN, kw, "eturn");
				case 's': return check(SUPER,  kw, "uper");
				case 'w': return check(WHILE,  kw, "hile");

				case 'f': 
					switch (*kw++) {
						default: return ILLEGAL;
						case 'a': return check(FALSE, kw, "lse");
						case 'o': return check(FOR,   kw, "r");
						case 'u': return check(FUN,   kw, "n");
					}

				case 't': 
					switch (*kw++) {
						default: return ILLEGAL;
						case 'h': return check(THIS, kw, "is");
						case 'r': return check(TRUE, kw, "ue");
					}
			}
		}
	*/
	output.WriteLine("inline constexpr {0} GetEnumeratorFromName({0}, std::string_view name) {{", henum.Name);
	{
		auto indent = output.Indent();
		for (auto& enumerator : henum.Enumerators)
		{
			output.WriteLine("if (name == \"{}\") return ({}){};", enumerator.Name, henum.Name, enumerator.Value);
		}
		output.WriteLine("return {{}};");
	}
	output.WriteLine("}}");

	if (has_any_enumerators && atEnumList(henum.Attributes, false))
	{
		output.WriteLine("inline constexpr {0} GetNext({0} v) {{ return {0}Values[(int64_t(v) + 1) % {0}Count]; }}", henum.Name);
		output.WriteLine("inline constexpr {0} GetPrev({0} v) {{ return {0}Values[(int64_t(v) + ({0}Count - 1)) % {0}Count]; }}", henum.Name);

		/// Preincrement
		output.WriteLine("inline constexpr {0}& operator++({0}& v) {{ v = GetNext(v); return v; }}", henum.Name);
		output.WriteLine("inline constexpr {0}& operator--({0}& v) {{ v = GetPrev(v); return v; }}", henum.Name);

		/// Postincrement
		output.WriteLine("inline constexpr {0} operator++({0}& v, int) {{ auto result = v; v = GetNext(v); return result; }}", henum.Name);
		output.WriteLine("inline constexpr {0} operator--({0}& v, int) {{ auto result = v; v = GetPrev(v); return result; }}", henum.Name);
	}

	output.WriteLine("inline std::ostream& operator<<(std::ostream& strm, {} v) {{ strm << GetEnumeratorName(v); return strm; }}", henum.Name);

	/// TODO: Move the body of the below function to the DB
	output.WriteLine("template <typename T>");
	output.WriteLine("void OutputFlagsFor(std::ostream& strm, {}, T flags, std::string_view separator = \", \") {{ ", henum.Name);
	output.CurrentIndent++;
	output.WriteLine("std::string_view sep = \"\";");
	for (auto& enumerator : henum.Enumerators)
	{
		output.WriteLine("if (flags & (T({}))) {{ strm << sep << \"{}\"; sep = separator; }}", 1ULL << enumerator.Value, enumerator.Name);
	}
	output.CurrentIndent--;
	output.WriteLine("}}");

	/*
	/// TODO: This isn't strictly necessary if we can use the ADL serializer (https://json.nlohmann.me/features/arbitrary_types/#how-do-i-convert-third-party-types)
	if (options.UseJSON)
	{
		output.WriteLine("inline void to_json({1}& j, {0} const & p) {{ j = std::underlying_type_t<{0}>(p); }}", henum.Name, options.JSONType);
		/// TODO: this depends on nlohmann::json::is_string
		output.WriteLine("inline void from_json({1} const& j, {0}& p) {{ if (j.is_string()) p = GetEnumeratorFromName({0}{{}}, j); else p = ({0})(std::underlying_type_t<{0}>)j; }}", henum.Name, options.JSONType);
	}
	*/

	output.EndDefine();


	output.StartDefine("#define {0}_VISIT_{1}_ENUMERATORS({0}_VISITOR)", options.MacroPrefix, henum.FullName());
	for (size_t i = 0; i < henum.Enumerators.size(); i++)
	{
		const auto& enumerator = henum.Enumerators[i];
		output.WriteLine("{0}_VISITOR(&StaticGetReflectionData({1}{{}}).Enumerators[{2}], {1}::{3}, \"{3}\");", options.MacroPrefix, henum.FullName(), i, enumerator.Name);
	}
	output.EndDefine("");

	return true;
}

void FileWriter::WriteLine()
{
	*mOutFile << '\n';
}

void FileWriter::Close()
{
	mOutFile->flush();
	mOutFile = nullptr;
}

FileWriter::~FileWriter()
{
	if (mOutFile)
	{
		std::cerr << std::format("file '{}' not closed due to an error, deleting", mPath.string());
		mOutFile = nullptr;
		std::filesystem::remove(mPath);
	}
}

bool BuildDatabaseEntriesForMirror(FileWriter& output, const Options& options, FileMirror const& file)
{
	for (auto& klass : file.Classes)
	{
		BuildStaticReflectionData(output, klass, options);
		output.WriteLine();
	}
	for (auto& henum : file.Enums)
	{
		BuildStaticReflectionData(output, henum, options);
		output.WriteLine();
	}
	return true;
}

bool BuildMirrorFile(path const& file_path, const Options& options, FileMirror const& file, uint64_t file_change_time, path const& final_path)
{
	FileWriter f(file_path);
	f.WriteLine("{}{}", TIMESTAMP_TEXT, file_change_time);
	f.WriteLine("/// Source file: {}", file.SourceFilePath.string());
	f.WriteLine("#pragma once");

	auto rel = final_path.parent_path().lexically_relative(options.ArtifactPath);
	f.WriteLine("#include \"{}/Reflector.h\"", rel.string());

	for (auto& klass : file.Classes)
	{
		if (!BuildClassEntry(f, file, klass, options))
			continue;
		f.WriteLine();
	}

	for (auto& henum : file.Enums)
	{
		if (!BuildEnumEntry(f, file, henum, options))
			continue;
		f.WriteLine();
	}

	f.Close();

	return true;
}