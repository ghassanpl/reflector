/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "ReflectionDataBuilding.h"
#include <charconv>

uint64_t FileNeedsUpdating(const std::filesystem::path& target_path, const std::filesystem::path& source_path, const Options& opts)
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
		const auto result = std::from_chars(time.begin(), time.end(), stored_change_time);
		if (!opts.Force && result.ec == std::errc{} && file_change_time == stored_change_time)
			return 0;
	}

	return file_change_time;
}

void CreateJSONDBArtifact(std::filesystem::path const& cwd, Options const& options)
{
	json db;

	for (auto& mirror : GetMirrors())
	{
		db[mirror.SourceFilePath.string()] = mirror.ToJSON();
	}

	std::ofstream jsondb{ cwd / "ReflectDatabase.json", std::ios_base::openmode{ std::ios_base::trunc } };
	jsondb << db.dump(1, '\t');
	jsondb.close();

	if (options.Verbose)
		PrintLine("Created ", (cwd / "ReflectDatabase.json").string());
}

void CreateReflectorHeaderArtifact(std::filesystem::path const& cwd, const Options& options)
{
	FileWriter reflect_file{ cwd / "Reflector.h" };
	reflect_file.WriteLine("#pragma once");
	reflect_file.WriteLine("#include <ReflectorClasses.h>");
	reflect_file.WriteLine("#define TOKENPASTE3_IMPL(x, y, z) x ## y ## z");
	reflect_file.WriteLine("#define TOKENPASTE3(x, y, z) TOKENPASTE3_IMPL(x, y, z)");
	reflect_file.WriteLine("#define TOKENPASTE2_IMPL(x, y) x ## y");
	reflect_file.WriteLine("#define TOKENPASTE2(x, y) TOKENPASTE2_IMPL(x, y)");
	reflect_file.WriteLine("");
	reflect_file.WriteLine("#define ", options.ClassPrefix, "(...)");
	reflect_file.WriteLine("#define ", options.FieldPrefix, "(...)");
	reflect_file.WriteLine("#define ", options.MethodPrefix, "(...)");
	reflect_file.WriteLine("#define ", options.BodyPrefix, "(...) TOKENPASTE2(", options.MacroPrefix, "_GENERATED_CLASS_BODY_, __LINE__)");
	reflect_file.WriteLine("#define ", options.EnumPrefix, "(...) TOKENPASTE2(", options.MacroPrefix, "_ENUM_, __LINE__)");
	reflect_file.WriteLine("#define ", options.EnumeratorPrefix, "(...)");
	reflect_file.WriteLine("");
	reflect_file.WriteLine("#define ", options.MacroPrefix, "_CALLABLE(ret, name, args, id) static int TOKENPASTE3(ScriptFunction_, name, id)(struct lua_State* thread) { return 0; }");
	reflect_file.Close();
	
	if (options.Verbose)
		PrintLine("Created ", (cwd / "Reflector.h").string());
}

void CreateIncludeListArtifact(std::filesystem::path const& cwd, Options const& options)
{
	std::ofstream includes_file(cwd / "Includes.reflect.h", std::ios_base::openmode{ std::ios_base::trunc });
	for (auto& mirror : GetMirrors())
	{
		includes_file << "#include " << mirror.SourceFilePath << "" << std::endl;
	}
	if (options.Verbose)
		PrintLine("Created ", (cwd / "Includes.reflect.h").string());
}

void CreateTypeListArtifact(std::filesystem::path const& cwd, Options const& options)
{
	std::ofstream classes_file(cwd / "Classes.reflect.h", std::ios_base::openmode{ std::ios_base::trunc });

	for (auto& mirror : GetMirrors())
	{
		for (auto& klass : mirror.Classes)
		{
			if (!klass.Flags.is_set(ClassFlags::Struct))
				classes_file << "ReflectClass(" << klass.Name << ")" << std::endl;
		}
		for (auto& henum : mirror.Enums)
			classes_file << "ReflectEnum(" << henum.Name << ")" << std::endl;
	}

	if (options.Verbose)
		PrintLine("Created ", (cwd / "Classes.reflect.h").string());
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
	bool should_build_proxy = false;

	output.WriteLine("/// From class: ", klass.Name);

	/// ///////////////////////////////////// ///
	/// Forward declare all classes
	/// ///////////////////////////////////// ///

	if (options.ForwardDeclare)
	{
		output.WriteLine((klass.Flags.is_set(ClassFlags::DeclaredStruct) ? "struct" : "class"), " ", klass.Name, ";");
	}

	/// ///////////////////////////////////// ///
	/// Visitor macros
	/// ///////////////////////////////////// ///

	/// Field visitor
	output.StartDefine("#define ", options.MacroPrefix, "_VISIT_", klass.Name, "_FIELDS(", options.MacroPrefix, "_VISITOR)");
	//for (auto& field : klass.Fields)
	for (size_t i = 0; i < klass.Fields.size(); i++)
	{
		const auto& field = klass.Fields[i];
		const auto ptr_str = "&" + klass.Name + "::" + field.Name;
		output.WriteLine("", options.MacroPrefix, "_VISITOR(&", klass.Name, "::StaticGetReflectionData().Fields[", i, "], ", ptr_str, 
			", ::Reflector::CompileTimeFieldData<", field.Type, ", ", klass.Name, ", ", field.Flags.bits, 
			", ::Reflector::CompileTimeLiteral<", BuildCompileTimeLiteral(field.Name),">, decltype(", ptr_str,"), ", ptr_str,">{});");
	}
	output.EndDefine("");

	/// Method visitor
	output.StartDefine("#define ", options.MacroPrefix, "_VISIT_", klass.Name, "_METHODS(", options.MacroPrefix, "_VISITOR)");
	for (size_t i = 0; i < klass.Methods.size(); i++)
	{
		const auto& method = klass.Methods[i];
		if (!method.Flags.is_set(Reflector::MethodFlags::NoCallable))
		{
			output.WriteLine("", options.MacroPrefix, "_VISITOR(&", klass.Name, "::StaticGetReflectionData().Methods[", i, "], (", method.GetSignature(klass), ")&", klass.Name, "::", method.Name, ", ", "&", klass.Name, "::ScriptFunction_", method.Name, method.ActualDeclarationLine(), ", ::Reflector::CompileTimeMethodData<", method.Flags.bits, ", ::Reflector::CompileTimeLiteral<", BuildCompileTimeLiteral(method.Name), ">>{});");
		}
	}
	output.EndDefine("");

	/// Property visitor
	output.StartDefine("#define ", options.MacroPrefix, "_VISIT_", klass.Name, "_PROPERTIES(", options.MacroPrefix, "_VISITOR)");
	for (auto& prop : klass.Properties)
	{
		auto& property = prop.second;
		std::string getter_name = "nullptr";
		if (!property.GetterName.empty())
			getter_name = baselib::Stringify("&", klass.Name, "::", property.GetterName);
		std::string setter_name = "nullptr";
		if (!property.SetterName.empty())
			setter_name = baselib::Stringify("&", klass.Name, "::", property.SetterName);
		output.WriteLine("", options.MacroPrefix, "_VISITOR(&", klass.Name, "::StaticGetReflectionData(), \"", property.Name, "\", ", getter_name, ", ", setter_name, 
			", ::Reflector::CompileTimePropertyData<", property.Type, ", ", klass.Name, ", 0ULL, ::Reflector::CompileTimeLiteral<", BuildCompileTimeLiteral(property.Name), ">>{});");
	}
	output.EndDefine("");

	/// ///////////////////////////////////// ///
	/// Class body
	/// ///////////////////////////////////// ///

	output.WriteLine("#undef ", options.MacroPrefix, "_GENERATED_CLASS_BODY_", klass.BodyLine);
	output.StartDefine("#define ", options.MacroPrefix, "_GENERATED_CLASS_BODY_", klass.BodyLine);

	/// All these are public
	output.CurrentIndent--;
	output.WriteLine("public:");
	output.CurrentIndent++;

	/// Typedefs
	output.WriteLine("typedef ", klass.Name, " self_type;");
	if (!klass.ParentClass.empty())
	{
		output.WriteLine("typedef ", klass.ParentClass, " parent_type;");
		output.WriteLine("using parent_type::parent_type;");

		/// Constructors
		if (!klass.Flags.is_set(ClassFlags::NoConstructors))
		{
			output.WriteLine("static self_type* Construct(){ return new self_type{StaticGetReflectionData()}; }");
			//output.WriteLine("static self_type* Construct(const ::Reflector::ClassReflectionData& klass){ return new self_type{klass}; }");
			output.WriteLine(klass.Name, "(::Reflector::ClassReflectionData const& klass) : ", klass.ParentClass, "(klass) {}");
		}
	}

	/// ///////////////////////////////////// ///
	/// Reflection Data Method
	/// ///////////////////////////////////// ///

	output.WriteLine("static ::Reflector::ClassReflectionData const& StaticGetReflectionData() {");
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::ClassReflectionData _data = {");
	output.CurrentIndent++;
	output.WriteLine(".Name = \"", klass.Name, "\",");
	output.WriteLine(".ParentClassName = \"", OnlyType(klass.ParentClass), "\",");

	if (!klass.Attributes.empty())
	{
		output.WriteLine(".Attributes = R\"_REFLECT_(", klass.Attributes.dump(), ")_REFLECT_\",");
		if (options.UseJSON)
			output.WriteLine(".AttributesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", klass.Attributes.dump(), ")_REFLECT_\"),");
	}
	if (!klass.Flags.is_set(ClassFlags::NoConstructors))
		output.WriteLine(".Constructor = +[](const ::Reflector::ClassReflectionData& klass){ return (void*)new self_type{klass}; },");

	/// Fields
	output.WriteLine(".Fields = {");
	output.CurrentIndent++;
	for (auto& field : klass.Fields)
	{
		output.WriteLine("::Reflector::FieldReflectionData {");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"", field.Name, "\",");
		output.WriteLine(".FieldType = \"", field.Type, "\",");
		if (!field.InitializingExpression.empty())
			output.WriteLine(".Initializer = ", json(field.InitializingExpression).dump(), ",");
		if (!field.Attributes.empty())
		{
			output.WriteLine(".Attributes = R\"_REFLECT_(", field.Attributes.dump(), ")_REFLECT_\",");
			if (options.UseJSON)
				output.WriteLine(".AttributesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", field.Attributes.dump(), ")_REFLECT_\"),");
		}
		output.WriteLine(".FieldTypeIndex = typeid(", field.Type, ")");
		output.CurrentIndent--;
		output.WriteLine("},");
	}
	output.CurrentIndent--;
	output.WriteLine("},");

	/// Methods
	output.WriteLine(".Methods = {");
	output.CurrentIndent++;
	for (auto& method : klass.Methods)
	{
		output.WriteLine("::Reflector::MethodReflectionData {");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"", method.Name, "\",");
		output.WriteLine(".ReturnType = \"", method.Type, "\",");
		if (!method.GetParameters().empty())
			output.WriteLine(".Parameters = ", json(method.GetParameters()).dump(), ",");
		if (!method.Attributes.empty())
		{
			output.WriteLine(".Attributes = R\"_REFLECT_(", method.Attributes.dump(), ")_REFLECT_\",");
			if (options.UseJSON)
				output.WriteLine(".AttributesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", method.Attributes.dump(), ")_REFLECT_\"),");
		}
		if (!method.UniqueName.empty())
			output.WriteLine(".UniqueName = \"", method.UniqueName, "\",");
		if (!method.Body.empty())
			output.WriteLine(".Body = \"", method.Body, "\",");
		output.WriteLine(".ReturnTypeIndex = typeid(", method.Type, "),");
		output.WriteLine(".ParentClass = &_data");
		output.CurrentIndent--;
		output.WriteLine("},");
	}
	output.CurrentIndent--;
	output.WriteLine("},");

	output.WriteLine(".TypeIndex = typeid(self_type)");
	output.CurrentIndent--;
	output.WriteLine("}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}");

	if (!klass.ParentClass.empty())
	{
		output.WriteLine("virtual ::Reflector::ClassReflectionData const& GetReflectionData() const override { return StaticGetReflectionData(); }");
	}

	/// ///////////////////////////////////// ///
	/// Visitor methods
	/// ///////////////////////////////////// ///

	/// - StaticVisitMethods
	output.WriteLine("template <typename VISITOR> static void StaticVisitMethods(VISITOR&& visitor) {");
	output.WriteLine("\t", options.MacroPrefix, "_VISIT_", klass.Name, "_METHODS(visitor);");
	output.WriteLine("}");
	/// - StaticVisitFields
	output.WriteLine("template <typename VISITOR> static void StaticVisitFields(VISITOR&& visitor) {");
	output.WriteLine("\t", options.MacroPrefix, "_VISIT_", klass.Name, "_FIELDS(visitor);");
	output.WriteLine("}");
	/// - StaticVisitProperties
	output.WriteLine("template <typename VISITOR> static void StaticVisitProperties(VISITOR&& visitor) {");
	output.WriteLine("\t", options.MacroPrefix, "_VISIT_", klass.Name, "_PROPERTIES(visitor);");
	output.WriteLine("}");

	/// ///////////////////////////////////// ///
	/// All methods
	/// ///////////////////////////////////// ///

	for (auto& func : klass.Methods)
	{
		if (func.Flags.is_set(Reflector::MethodFlags::Virtual) && !func.Flags.is_set(Reflector::MethodFlags::Final))
			should_build_proxy = true;

		/// Callables for all methods
		if (!func.Flags.is_set(Reflector::MethodFlags::NoCallable))
			output.WriteLine("", options.MacroPrefix, "_CALLABLE((", func.Type, "), ", func.Name, ", (", func.GetParameters(), "), ", func.ActualDeclarationLine(), ")");
		if (func.Flags.is_set(Reflector::MethodFlags::Artificial))
		{
			output.WriteLine(func.Type, " ", func.Name, "(", func.GetParameters(), ")", (func.Flags.is_set(Reflector::MethodFlags::Const) ? " const" : ""), " { ", func.Body, " }");
		}
	}

	/// Back to public
	output.EndDefine("public:");

	/// ///////////////////////////////////// ///
	/// Proxy class
	/// ///////////////////////////////////// ///

	should_build_proxy = should_build_proxy && klass.Attributes.value("CreateProxy", true);
	
	return true;
}

bool BuildEnumEntry(FileWriter& output, const FileMirror& mirror, const Enum& henum, const Options& options)
{
	output.WriteLine("/// From enum: ", henum.Name);
	output.WriteLine("#undef ", options.MacroPrefix, "_ENUM_", henum.DeclarationLine);
	output.StartDefine("#define ", options.MacroPrefix, "_ENUM_", henum.DeclarationLine);

	output.WriteLine("enum class ", henum.Name, ";"); /// forward decl;


	output.WriteLine("inline ::Reflector::EnumReflectionData const& StaticGetReflectionData(", henum.Name, ") {");
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::EnumReflectionData _data = {");
	output.CurrentIndent++;

	output.WriteLine(".Name = \"", henum.Name, "\",");
	if (!henum.Attributes.empty())
	{
		output.WriteLine(".Attributes = R\"_REFLECT_(", henum.Attributes.dump(), ")_REFLECT_\",");
		if (options.UseJSON)
			output.WriteLine(".AttributesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", henum.Attributes.dump(), ")_REFLECT_\"),");
	}
	output.WriteLine(".Enumerators = {");
	output.CurrentIndent++;
	for (auto& enumerator : henum.Enumerators)
	{
		output.WriteLine("::Reflector::EnumeratorReflectionData {");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"", enumerator.Name, "\",");
		output.WriteLine(".Value = ", enumerator.Value);
		output.CurrentIndent--;
		output.WriteLine("},");
	}
	output.CurrentIndent--;
	output.WriteLine("},");
	output.WriteLine(".TypeIndex = typeid(", henum.Name, ")");
	output.CurrentIndent--;
	output.WriteLine("}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}");

	output.WriteLine("inline constexpr const char* GetEnumName(", henum.Name, ") { return \"", henum.Name, "\"; }");
	output.WriteLine("inline constexpr const char* GetEnumeratorName(", henum.Name, " v) {");
	{
		auto indent = output.Indent();

		if (henum.Enumerators.size())
		{
			output.WriteLine("switch (int64_t(v)) {");
			for (auto& enumerator : henum.Enumerators)
			{
				output.WriteLine("case ", enumerator.Value, ": return \"", enumerator.Name, "\";");
			}
			output.WriteLine("}");
		}
		output.WriteLine("return \"<Unknown>\";");	
	}
	output.WriteLine("}");
	output.WriteLine("inline constexpr ", henum.Name, " GetEnumeratorFromName(", henum.Name, " v, std::string_view name) {");
	{
		auto indent = output.Indent();
		for (auto& enumerator : henum.Enumerators)
		{
			output.WriteLine("if (name == \"", enumerator.Name, "\") return (", henum.Name, ")", enumerator.Value, ";");
		}
		output.WriteLine("return {};");
	}
	output.WriteLine("}");
	output.WriteLine("inline std::ostream& operator<<(std::ostream& strm, ", henum.Name, " v) { strm << GetEnumeratorName(v); return strm; }");
	output.WriteLine("template <typename T>");
	output.WriteLine("void OutputFlagsFor(std::ostream& strm, ", henum.Name, ", T flags, std::string_view separator = \", \") { ");
	output.CurrentIndent++;
	output.WriteLine("std::string_view sep = \"\";");
	for (auto& enumerator : henum.Enumerators)
	{
		output.WriteLine("if (flags & (T(", 1ULL<<enumerator.Value, "))) { strm << sep << \"", enumerator.Name, "\"; sep = separator; }");
	}
	output.CurrentIndent--;
	output.WriteLine("}");
	
	if (options.UseJSON)
	{
		output.WriteLine("inline void to_json(json& j, ", henum.Name, " const & p) { j = std::underlying_type_t<", henum.Name, ">(p); }");

		output.WriteLine("inline void from_json(json const& j, ", henum.Name,"& p) { if (j.is_string()) p = GetEnumeratorFromName(", henum.Name, "{}, j); else p = (", henum.Name, ")(std::underlying_type_t<", henum.Name, ">)j; }");
	}

	output.EndDefine();


	output.StartDefine("#define ", options.MacroPrefix, "_VISIT_", henum.Name, "_ENUMERATORS(", options.MacroPrefix, "_VISITOR)");
	for (size_t i = 0; i < henum.Enumerators.size(); i++)
	{
		const auto& enumerator = henum.Enumerators[i];
		output.WriteLine("", options.MacroPrefix, "_VISITOR(&StaticGetReflectionData(", henum.Name, "{}).Enumerators[", i, "], ", henum.Name, "::", enumerator.Name, ", \"", enumerator.Name, "\");");
	}
	output.EndDefine("");

	return true;
}

void FileWriter::WriteJSON(json const& value)
{
	CurrentIndent++;
	WriteLine("return R\"_REFLECT_(", value.dump(), ")_REFLECT_\";");
	CurrentIndent--;
}

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


void BuildMirrorFile(FileMirror const& file, size_t& modified_files, const Options& options)
{
	auto file_path = file.SourceFilePath;
	file_path.concat(".mirror");

	/// TOOD: Check if we actually need to update the file
	auto file_change_time = FileNeedsUpdating(file_path, file.SourceFilePath, options);
	if (file_change_time == 0) return;

	modified_files++;

	if (!options.Quiet)
		PrintLine("Building class file ", file_path.string());

	FileWriter f(file_path);
	f.WriteLine(TIMESTAMP_TEXT, file_change_time);
	f.WriteLine("/// Source file: ", file.SourceFilePath);
	f.WriteLine("#pragma once");
	//f.WriteLine("#include <Reflector.h>");

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
}