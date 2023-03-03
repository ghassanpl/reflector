/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "ReflectionDataBuilding.h"
#include "Attributes.h"
#include <charconv>

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

bool CreateReflectorHeaderArtifact(path const& target_path, const Options& options, path const& final_path)
{
	auto rel = final_path.parent_path().lexically_relative(options.ArtifactPath);

	FileWriter reflect_file{ target_path };
	reflect_file.WriteLine("#pragma once");
	reflect_file.WriteLine("#include \"{}/ReflectorClasses.h\"", rel.string());
	reflect_file.WriteLine("#define TOKENPASTE3_IMPL(x, y, z) x ## y ## z");
	reflect_file.WriteLine("#define TOKENPASTE3(x, y, z) TOKENPASTE3_IMPL(x, y, z)");
	reflect_file.WriteLine("#define TOKENPASTE2_IMPL(x, y) x ## y");
	reflect_file.WriteLine("#define TOKENPASTE2(x, y) TOKENPASTE2_IMPL(x, y)");
	reflect_file.WriteLine("");
	reflect_file.WriteLine("#define {}(...) TOKENPASTE2({}_GENERATED_CLASS_, __LINE__)", options.ClassPrefix, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...)", options.FieldPrefix);
	reflect_file.WriteLine("#define {}(...)", options.MethodPrefix);
	reflect_file.WriteLine("#define {}(...) TOKENPASTE2({}_GENERATED_CLASS_BODY_, __LINE__)", options.BodyPrefix, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...) TOKENPASTE2({}_ENUM_, __LINE__)", options.EnumPrefix, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...)", options.EnumeratorPrefix);
	reflect_file.WriteLine("");
	reflect_file.WriteLine("#define {}_CALLABLE(ret, name, args, id) static int TOKENPASTE3(ScriptFunction_, name, id)(struct lua_State* thread) {{ return 0; }}", options.MacroPrefix);
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
			classes_file << "ReflectClass(" << klass.Name << ")" << std::endl;
		}
		for (auto& henum : mirror.Enums)
			classes_file << "ReflectEnum(" << henum.Name << ")" << std::endl;
	}
	return true;
}

std::string BuildCompileTimeLiteral(std::string_view str)
{
	std::string result;
	for (auto c : str)
	{
		result += "'";
		result += c;
		result += "',";
	}
	result += "0";
	return result;
}

bool BuildClassEntry(FileWriter& output, const FileMirror& mirror, const Class& klass, const Options& options)
{
	output.WriteLine("/// From class: {}", klass.Name);

	/// ///////////////////////////////////// ///
	/// Forward declare all classes
	/// ///////////////////////////////////// ///

	if (options.ForwardDeclare)
	{
		output.WriteLine("{} {};", (klass.Flags.is_set(ClassFlags::DeclaredStruct) ? "struct" : "class"), klass.Name);
	}

	/// ///////////////////////////////////// ///
	/// Visitor macros
	/// ///////////////////////////////////// ///

	/// Field visitor
	output.StartDefine("#define {}_VISIT_{}_FIELDS({}_VISITOR)", options.MacroPrefix, klass.Name, options.MacroPrefix);
	//for (auto& field : klass.Fields)
	for (size_t i = 0; i < klass.Fields.size(); i++)
	{
		const auto& field = klass.Fields[i];
		const auto ptr_str = "&" + klass.Name + "::" + field.Name;
		output.WriteLine("{}_VISITOR(&{}::StaticGetReflectionData().Fields[{}], {}, ::Reflector::CompileTimeFieldData<{}, {}, {}, ::Reflector::CompileTimeLiteral<{}>, decltype({}), {}>{{}});"
			, options.MacroPrefix, klass.Name, i, ptr_str, field.Type, klass.Name, field.Flags.bits, BuildCompileTimeLiteral(field.Name), ptr_str, ptr_str);
	}
	output.EndDefine("");

	/// Method visitor
	output.StartDefine("#define {0}_VISIT_{1}_METHODS({0}_VISITOR)", options.MacroPrefix, klass.Name);
	for (size_t i = 0; i < klass.Methods.size(); i++)
	{
		const auto& method = klass.Methods[i];
		if (!method.Flags.is_set(Reflector::MethodFlags::NoCallable))
		{
			output.WriteLine("{0}_VISITOR(&{1}::StaticGetReflectionData().Methods[{2}], ({3})&{1}::{4}, &{1}::ScriptFunction_{4}{5}, ::Reflector::CompileTimeMethodData<{6}, ::Reflector::CompileTimeLiteral<{7}>>{{}});",
				options.MacroPrefix, klass.Name, i, method.GetSignature(klass), method.Name, method.ActualDeclarationLine(), method.Flags.bits, BuildCompileTimeLiteral(method.Name));
		}
	}
	output.EndDefine("");

	/// Property visitor
	output.StartDefine("#define {0}_VISIT_{1}_PROPERTIES({0}_VISITOR)", options.MacroPrefix, klass.Name);
	for (auto& prop : klass.Properties)
	{
		auto& property = prop.second;
		std::string getter_name = "nullptr";
		if (!property.GetterName.empty())
			getter_name = std::format("&{}::{}", klass.Name, property.GetterName);
		std::string setter_name = "nullptr";
		if (!property.SetterName.empty())
			setter_name = std::format("&{}::{}", klass.Name, property.SetterName);
		output.WriteLine("{0}_VISITOR(&{1}::StaticGetReflectionData(), \"{2}\", {3}, {4}, ::Reflector::CompileTimePropertyData<{5}, {1}, 0ULL, ::Reflector::CompileTimeLiteral<{6}>>{{}});",
			options.MacroPrefix, klass.Name, property.Name, getter_name, setter_name, property.Type, BuildCompileTimeLiteral(property.Name));
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

	output.WriteLine("static ::Reflector::ClassReflectionData const& StaticGetReflectionData() {{");
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::ClassReflectionData _data = {{");
	output.CurrentIndent++;
	output.WriteLine(".Name = \"{}\",", klass.Name);
	output.WriteLine(".ParentClassName = \"{}\",", OnlyType(klass.ParentClass));

	if (!klass.Attributes.empty())
	{
		output.WriteLine(".Attributes = {},", EscapeJSON(klass.Attributes));
		if (options.UseJSON)
			output.WriteLine(".AttributesJSON = ::nlohmann::json::parse({}),", EscapeJSON(klass.Attributes));
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
				output.WriteLine(".AttributesJSON = ::nlohmann::json::parse({}),", EscapeJSON(field.Attributes));
		}
		output.WriteLine(".FieldTypeIndex = typeid({}),", field.Type);
		output.WriteLine(".Flags = {},", field.Flags.bits);
		output.WriteLine(".ParentClass = &_data");
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
				output.WriteLine(".AttributesJSON = ::nlohmann::json::parse({}),", EscapeJSON(method.Attributes));
		}
		if (!method.UniqueName.empty())
			output.WriteLine(".UniqueName = \"{}\",", method.UniqueName);
		if (!method.Body.empty())
			output.WriteLine(".Body = {},", EscapeJSON(method.Body));
		output.WriteLine(".ReturnTypeIndex = typeid({}),", method.Type);
		output.WriteLine(".Flags = {},", method.Flags.bits);
		output.WriteLine(".ParentClass = &_data");
		output.CurrentIndent--;
		output.WriteLine("}},");
	}
	output.CurrentIndent--;
	output.WriteLine("}},");

	output.WriteLine(".TypeIndex = typeid(self_type),");
	output.WriteLine(".Flags = {}", klass.Flags.bits);
	output.CurrentIndent--;
	output.WriteLine("}}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}}");

	if (!klass.ParentClass.empty())
	{
		output.WriteLine("virtual ::Reflector::ClassReflectionData const& GetReflectionData() const {{ return StaticGetReflectionData(); }}");
	}

	/// ///////////////////////////////////// ///
	/// Visitor methods
	/// ///////////////////////////////////// ///

	/// - StaticVisitMethods
	output.WriteLine("template <typename VISITOR> static void StaticVisitMethods(VISITOR&& visitor) {{");
	output.WriteLine("\t{}_VISIT_{}_METHODS(visitor);", options.MacroPrefix, klass.Name);
	output.WriteLine("}}");
	/// - StaticVisitFields
	output.WriteLine("template <typename VISITOR> static void StaticVisitFields(VISITOR&& visitor) {{");
	output.WriteLine("\t{}_VISIT_{}_FIELDS(visitor);", options.MacroPrefix, klass.Name);
	output.WriteLine("}}");
	/// - StaticVisitProperties
	output.WriteLine("template <typename VISITOR> static void StaticVisitProperties(VISITOR&& visitor) {{");
	output.WriteLine("\t{}_VISIT_{}_PROPERTIES(visitor);", options.MacroPrefix, klass.Name);
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
		if (!func.Flags.is_set(Reflector::MethodFlags::NoCallable))
			output.WriteLine("{}_CALLABLE(({}), {}, ({}), {})", options.MacroPrefix, func.Type, func.Name, func.GetParameters(), func.ActualDeclarationLine());
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
		output.EndDefine("namespace Reflector {{ template <typename PROXY_OBJ> struct ProxyFor<{0}, PROXY_OBJ> {{ using Type = {0}_Proxy<{0}, PROXY_OBJ>; }}; }}", klass.Name);
	}
	else
		output.WriteLine("#define {}_GENERATED_CLASS_{}", options.MacroPrefix, klass.DeclarationLine);
	
	return true;
}

bool BuildEnumEntry(FileWriter& output, const FileMirror& mirror, const Enum& henum, const Options& options)
{
	auto has_any_enumerators = (henum.Enumerators.size() > 0);
	output.WriteLine("/// From enum: {}", henum.Name);
	output.WriteLine("#undef {}_ENUM_{}", options.MacroPrefix, henum.DeclarationLine);
	output.StartDefine("#define {}_ENUM_{}", options.MacroPrefix, henum.DeclarationLine);

	output.WriteLine("enum class {};", henum.Name); /// forward decl;
	output.WriteLine("static constexpr inline size_t {}Count = {};", henum.Name, henum.Enumerators.size());
	output.WriteLine("static constexpr inline std::string_view {}Names[] = {{ {} }};", henum.Name, ghassanpl::string_ops::join(henum.Enumerators, ", ", [](Enumerator const& enumerator) { return std::format("\"{}\"", enumerator.Name); }));
	output.WriteLine("static constexpr inline {0} {0}Values[] = {{ {1} }} ;", henum.Name, ghassanpl::string_ops::join(henum.Enumerators, ", ", [&](Enumerator const& enumerator) { 
		return std::format("{}{{{}}}", henum.Name, enumerator.Value); 
	}));
	
	if (has_any_enumerators)
	{
		output.WriteLine("static constexpr inline {0} First{0} = {0}{{{1}}};", henum.Name, henum.Enumerators.front().Value);
		output.WriteLine("static constexpr inline {0} Last{0} = {0}{{{1}}};", henum.Name, henum.Enumerators.back().Value);
	}

	output.WriteLine("inline constexpr auto operator==(std::underlying_type_t<{0}> left, {0} right) noexcept {{ return left == std::to_underlying(right); }}", henum.Name);
	output.WriteLine("inline constexpr auto operator<=>(std::underlying_type_t<{0}> left, {0} right) noexcept {{ return left <=> std::to_underlying(right); }}", henum.Name);

	output.WriteLine("inline ::Reflector::EnumReflectionData const& StaticGetReflectionData({}) {{", henum.Name);
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::EnumReflectionData _data = {{");
	output.CurrentIndent++;

	output.WriteLine(".Name = \"{}\",", henum.Name);
	if (!henum.Attributes.empty())
	{
		output.WriteLine(".Attributes = {},", EscapeJSON(henum.Attributes));
		if (options.UseJSON)
			output.WriteLine(".AttributesJSON = ::nlohmann::json::parse({}),", EscapeJSON(henum.Attributes));
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
	output.WriteLine(".TypeIndex = typeid({}),", henum.Name);
	output.WriteLine(".Flags = {},", henum.Flags.bits);
	output.CurrentIndent--;
	output.WriteLine("}}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}}");

	output.WriteLine("inline constexpr const char* GetEnumName({0}) {{ return \"{0}\"; }}", henum.Name);
	output.WriteLine("inline constexpr size_t GetEnumCount({}) {{ return {}; }}", henum.Name, henum.Enumerators.size());
	output.WriteLine("inline constexpr const char* GetEnumeratorName({} v) {{", henum.Name);
	{
		auto indent = output.Indent();

		if (has_any_enumerators)
		{
			output.WriteLine("switch (int64_t(v)) {{");
			for (auto& enumerator : henum.Enumerators)
			{
				output.WriteLine("case {}: return \"{}\";", enumerator.Value, enumerator.Name);
			}
			output.WriteLine("}}");
		}
		output.WriteLine("return \"<Unknown>\";");	
	}
	output.WriteLine("}}");
	output.WriteLine("inline constexpr {0} GetEnumeratorFromName({0} v, std::string_view name) {{", henum.Name);
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
	
	if (options.UseJSON)
	{
		output.WriteLine("inline void to_json(json& j, {0} const & p) {{ j = std::underlying_type_t<{0}>(p); }}", henum.Name);

		output.WriteLine("inline void from_json(json const& j, {0}& p) {{ if (j.is_string()) p = GetEnumeratorFromName({0}{{}}, j); else p = ({0})(std::underlying_type_t<{0}>)j; }}", henum.Name);
	}

	output.EndDefine();


	output.StartDefine("#define {0}_VISIT_{1}_ENUMERATORS({0}_VISITOR)", options.MacroPrefix, henum.Name);
	for (size_t i = 0; i < henum.Enumerators.size(); i++)
	{
		const auto& enumerator = henum.Enumerators[i];
		output.WriteLine("{0}_VISITOR(&StaticGetReflectionData({1}{{}}).Enumerators[{2}], {1}::{3}, \"{3}\");", options.MacroPrefix, henum.Name, i, enumerator.Name);
	}
	output.EndDefine("");

	return true;
}

/*
void FileWriter::WriteJSON(json const& value)
{
	CurrentIndent++;
	_WriteLine("return R\"_REFLECT_({})_REFLECT_\";", value.dump());
	CurrentIndent--;
}
*/

void FileWriter::WriteLine()
{
	mOutFile << '\n';
}

void FileWriter::Close()
{
	mOutFile.flush();
	mOutFile.close();
}

FileWriter::~FileWriter()
{
	if (mOutFile.is_open())
	{
		mOutFile.close();
		std::filesystem::remove(mPath);
	}
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