/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "ReflectionDataBuilding.h"
#include "Attributes.h"
#include "Declarations.h"
#include <charconv>
#include <fstream>

struct OutputContext
{
	FileWriter& output;
	const Options& options;

	void WriteForwardDeclaration(const Class& klass);
	void WriteForwardDeclaration(const Enum& henum);

	void BuildStaticReflectionData(const Enum& henum);
	void BuildStaticReflectionData(const Class& klass);
};

struct FileMirrorOutputContext : OutputContext
{
	const FileMirror& mirror;

	FileMirrorOutputContext(const FileMirror& mirror_, FileWriter& output_, const Options& options_)
		: OutputContext{ output_, options_ }
		, mirror(mirror_)
	{

	}

	bool BuildDatabaseEntriesForMirror();

	bool BuildClassEntry(const Class& klass);
	bool BuildEnumEntry(const Enum& henum);
};


static path RelativePath(path const& writing_file, path const& referenced_file)
{
	return referenced_file.lexically_relative(writing_file.parent_path());
}

/// Compares the generation time of the target artifact (acquired from its first line), with
/// the last write time of the source file. If they differ, the artifact needs to be regenerated.
/// The executable change time, if it is newer, always takes precedence over the source file's last write time.
uint64_t ArtifactNeedsRegenerating(const path& target_path, const path& source_path, const Options& opts)
{
	const auto stat = status(target_path);
	const uint64_t file_change_time = std::max(ExecutableChangeTime, static_cast<uint64_t>(last_write_time(source_path).time_since_epoch().count()));
	if (stat.type() != std::filesystem::file_type::not_found)
	{
		/// Open file and get first line
		std::ifstream f(target_path);
		std::string line;
		std::getline(f, line);
		if (line.empty() || line.size() < TIMESTAMP_TEXT.size())
			return 1; /// corrupted file, regenerate

		uint64_t stored_change_time = 0;
		const auto time = string_view{ string_view{ line }.substr(TIMESTAMP_TEXT.size() - 1) };
		const auto [ptr, ec] = std::from_chars(to_address(time.begin()), to_address(time.end()), stored_change_time);
		if (!opts.Force && ec == std::errc{} && file_change_time == stored_change_time)
			return 0;
	}

	return file_change_time;
}

bool CreateJSONDBArtifact(ArtifactArgs args)
{
	json db;

	for (auto&& mirror : GetMirrors())
	{
		db[mirror->SourceFilePath.string()] = mirror->ToJSON();
	}

	*args.Output << db.dump(1, '\t');

	return true;
}

bool CreateReflectorDatabaseArtifact(ArtifactArgs args)
{
	auto const& [_, final_path, opts, factory] = args;
	FileWriter database_file{args};
	
	database_file.EnsurePCH();
	database_file.WriteLine("#include <iostream>");
	database_file.WriteLine("#include \"Reflector.h\"");
	database_file.WriteLine("#include \"ReflectorUtils.h\"");

	database_file.WriteLine("#include \"Includes.reflect.h\"");

	database_file.WriteLine("template <typename T, typename U = T> bool Compare_(T&& t, U&& u) {{ return t == u; }}");
	database_file.WriteLine("static constexpr std::string_view empty_json_object_str = \"{{}}\";");
	
	for (const auto& mirror : GetMirrors())
	{
		FileMirrorOutputContext context{ *mirror, database_file, opts };
		if (!context.BuildDatabaseEntriesForMirror())
			return false;
	}

	database_file.StartBlock("namespace Reflector {{");
	database_file.StartBlock("::Reflector::Class const* Classes[] = {{");
	for (const auto& mirror : GetMirrors())
	{
		for (auto& klass : mirror->Classes)
		{
			database_file.WriteLine("&StaticGetReflectionData_For_{}(),", klass->GeneratedUniqueName());
		}
	}
	database_file.WriteLine("nullptr");
	database_file.EndBlock("}};");
	database_file.StartBlock("::Reflector::Enum const* Enums[] = {{");
	for (const auto& mirror : GetMirrors())
	{
		for (auto& henum : mirror->Enums)
		{
			database_file.WriteLine("&StaticGetReflectionData_For_{}(),", henum->GeneratedUniqueName());
		}
	}
	database_file.WriteLine("nullptr");
	database_file.EndBlock("}};");
	database_file.EndBlock("}};");

	return true;
}

bool CreateReflectorHeaderArtifact(ArtifactArgs args)
{
	auto const& [_, final_path, options, factory] = args;

	const auto reflector_classes_relative_path = RelativePath(final_path, options.ArtifactPath / "ReflectorClasses.h");
	const auto reflector_gc_relative_path = RelativePath(final_path, options.ArtifactPath / "ReflectorGC.h");

	FileWriter reflect_file{args};
	reflect_file.WriteLine("#pragma once");
	if (options.JSON.Use)
	{
		reflect_file.WriteLine("#include {}", options.JSON.HeaderPath);
		reflect_file.WriteLine("#define REFLECTOR_USES_JSON 1", options.MacroPrefix);
		reflect_file.WriteLine("#define REFLECTOR_JSON_TYPE {}", options.JSON.Type);
		reflect_file.WriteLine("#define REFLECTOR_JSON_HEADER {}", options.JSON.HeaderPath);
		reflect_file.WriteLine("#define REFLECTOR_JSON_PARSE_FUNC {}", options.JSON.ParseFunction);
	}
	if (options.AddGCFunctionality)
		reflect_file.WriteLine("#define REFLECTOR_USES_GC 1", options.MacroPrefix);
	reflect_file.WriteLine("#include \"{}\"", reflector_classes_relative_path.string());
	if (options.AddGCFunctionality)
		reflect_file.WriteLine("#include \"{}\"", reflector_gc_relative_path.string());
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE3_IMPL(x, y, z) x ## y ## z");
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE3(x, y, z) REFLECTOR_TOKENPASTE3_IMPL(x, y, z)");
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE2_IMPL(x, y) x ## y");
	reflect_file.WriteLine("#define REFLECTOR_TOKENPASTE2(x, y) REFLECTOR_TOKENPASTE2_IMPL(x, y)");
	reflect_file.WriteLine("");
	reflect_file.WriteLine("#define {}(...) REFLECTOR_TOKENPASTE2({}_GENERATED_CLASS_, __LINE__)", options.ClassAnnotationName, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...)", options.FieldAnnotationName);
	reflect_file.WriteLine("#define {}(...)", options.MethodAnnotationName);
	reflect_file.WriteLine("#define {}(...) REFLECTOR_TOKENPASTE2({}_GENERATED_CLASS_BODY_, __LINE__)", options.BodyAnnotationName, options.MacroPrefix);
	reflect_file.WriteLine("#define {}(...) ", options.EnumAnnotationName);
	reflect_file.WriteLine("#define {}(...)", options.EnumeratorAnnotationName);
	reflect_file.WriteLine("#define {}(...)", options.NamespaceAnnotationName);
	reflect_file.WriteLine("");
	return true;
}

bool CreateIncludeListArtifact(ArtifactArgs args)
{
	FileWriter out{ args };
	for (auto&& mirror : GetMirrors())
	{
		/// TODO: Make these relative
		auto relative = mirror->SourceFilePath.lexically_relative(args.TargetPath.parent_path());
		//out.WriteLine("#include \"{}\"", mirror->SourceFilePath.string());
		out.WriteLine("#include \"{}\"", relative.string());
	}
	return true;
}

bool CreateTypeListArtifact(ArtifactArgs args)
{
	FileWriter out{ args };

	for (auto&& mirror : GetMirrors())
	{
		for (auto& klass : mirror->Classes)
			out.WriteLine("ReflectClass({}, {})", klass->Name, klass->FullType());
		for (auto& henum : mirror->Enums)
			out.WriteLine("ReflectEnum({}, {})", henum->Name, henum->FullType());
	}
	return true;
}

std::string BuildCompileTimeLiteral(std::string_view str)
{
	return std::format("\"{}\"",escaped(str));
}


std::string DebuggingComment(const Options& options, std::string_view content)
{
	return options.DebuggingComments ? std::format("/* {} */ ", content) : std::string{};
}

bool FileMirrorOutputContext::BuildClassEntry(const Class& klass)
{
	output.WriteLine("/// From class: {}", klass.FullType());

	/// ///////////////////////////////////// ///
	/// Forward declare all classes
	/// ///////////////////////////////////// ///

	if (options.ForwardDeclare)
	{
		WriteForwardDeclaration(klass);
	}
	/// TODO: Make the name of this function configurable?
	output.WriteLine("::Reflector::Class const& StaticGetReflectionData_For_{}();", klass.GeneratedUniqueName());
	//output.WriteLine("constexpr ::std::string_view GetClassName(::std::type_identity<{}>) noexcept {{ return \"{}\"; }}", klass.FullType(), klass.Name);
	//output.WriteLine("constexpr ::std::string_view GetFullClassName(::std::type_identity<{}>) noexcept {{ return \"{}\"; }}", klass.FullType(), klass.FullName());

	/// ///////////////////////////////////// ///
	/// Visitor macros
	/// ///////////////////////////////////// ///

	/// Field visitor
	output.StartDefine("#define {}_VISIT_{}_FIELDS({}_VISITOR)", options.MacroPrefix, klass.FullName(), options.MacroPrefix);
	for (size_t i = 0; i < klass.Fields.size(); i++)
	{
		const auto& field = klass.Fields[i];
		const auto ptr_str = "&" + klass.FullType() + "::" + field->Name;
		output.WriteLine("{0}{1}_VISITOR(::Reflector::FieldVisitorData<::Reflector::CompileTimeFieldData<{2}, {3}, {4}, {5}, decltype({6}), {6}>>{{ &{3}::StaticGetReflectionData().Fields[{7}] }});",
			DebuggingComment(options, field->Name),
			options.MacroPrefix, 
			field->Type, /// 2
			klass.FullType(), /// 3 
			field->Flags.bits, /// 4
			BuildCompileTimeLiteral(field->Name), /// 5
			ptr_str, /// 6
			i /// 7
		);
	}
	output.EndDefine("");

	/// Method visitor
	output.StartDefine("#define {0}_VISIT_{1}_METHODS({0}_VISITOR)", options.MacroPrefix, klass.FullName());
	auto klass_full_type = klass.FullType();
	for (size_t i = 0; i < klass.Methods.size(); i++)
	{
		const auto& method = klass.Methods[i];
		
		const auto method_pointer = std::format("({})&{}::{}", method->GetSignature(klass), klass_full_type, method->Name);
		const auto parameter_tuple = std::format("::std::tuple<{}>", method->ParametersTypesOnly);
		const auto compile_time_method_data = std::format("::Reflector::CompileTimeMethodData<{0}, {1}, {2}, {3}, {4}, decltype({5}), {5}, ::Reflector::AccessMode::{6}>", 
			method->Return.Name, parameter_tuple, klass_full_type, method->Flags.bits, BuildCompileTimeLiteral(method->Name), method_pointer, magic_enum::enum_name(method->Access)
		);

		const auto debugging_comment_prefix = options.DebuggingComments ? std::format("/* {} */ ", method->Name) : std::string{};

		output.WriteLine("{}{}_VISITOR(::Reflector::MethodVisitorData<{}>{{ &{}::StaticGetReflectionData().Methods[{}] }});",
			debugging_comment_prefix, options.MacroPrefix, compile_time_method_data, klass_full_type, i);

		/*
		if (options.GenerateLuaFunctionBindings && !method->Flags.is_set(Reflector::MethodFlags::NoCallable)) /// if callable
		{
			
		}
		else
		{
			output.WriteLine("{}{}_VISITOR(&{}::StaticGetReflectionData().Methods[{}], {}, {});",
				debugging_comment_prefix, options.MacroPrefix, klass_full_type, i, method_pointer, compile_time_method_data);
		}*/
	}
	output.EndDefine("");

	/// Property visitor
	output.StartDefine("#define {0}_VISIT_{1}_PROPERTIES({0}_VISITOR)", options.MacroPrefix, klass.FullName());
	size_t property_index = 0;
	for (const auto& property : klass.Properties | std::views::values)
	{
#if 0
		std::string getter_name = "nullptr";
		if (snd.Getter)
			getter_name = std::format("&{}::{}", klass_full_type, snd.Getter->Name);
		std::string setter_name = "nullptr";
		if (snd.Setter)
			setter_name = std::format("&{}::{}", klass_full_type, snd.Setter->Name);
		const auto debugging_comment_prefix = options.DebuggingComments ? std::format("/* {} */ ", snd.Name) : std::string{};
		output.WriteLine("{7}{0}_VISITOR(::Reflector::CompileTimePropertyData<{5}, {1}, 0ULL, {6}>{{ &{1}::StaticGetReflectionData() }});",
			options.MacroPrefix, klass_full_type, snd.Name, getter_name, setter_name, snd.Type, BuildCompileTimeLiteral(snd.Name), debugging_comment_prefix);
#endif

		/*
		const auto& method = klass.Pre[i];

		const auto method_pointer = std::format("({})&{}::{}", method->GetSignature(klass), klass_full_type, method->Name);
		const auto parameter_tuple = std::format("::std::tuple<{}>", method->ParametersTypesOnly);
		const auto compile_time_method_data = std::format("::Reflector::CompileTimeMethodData<{0}, {1}, {2}, {3}, {4}, decltype({5}), {5}, ::Reflector::AccessMode::{6}>",
			method->Return.Name, parameter_tuple, klass_full_type, method->Flags.bits, BuildCompileTimeLiteral(method->Name), method_pointer, magic_enum::enum_name(method->Access)
		);*/
		const auto compile_time_property_data = std::format("::Reflector::CompileTimeCommonData<{0}, {1}, {2}, {3}>",
			property.Type, klass.FullType(), property.Flags.bits, BuildCompileTimeLiteral(property.Name));

		const auto debugging_comment_prefix = options.DebuggingComments ? std::format("/* {} */ ", property.Name) : std::string{};
		output.WriteLine("{}{}_VISITOR(::Reflector::PropertyVisitorData<{}>{{ &{}::StaticGetReflectionData().Properties[{}] }});",
			debugging_comment_prefix, options.MacroPrefix, compile_time_property_data, klass_full_type, property_index);

		++property_index;
	}
	output.EndDefine("");

	/// ///////////////////////////////////// ///
	/// Class body
	/// ///////////////////////////////////// ///

	/// TODO: Make the name of ALL these methods configurable

	output.WriteLine("#undef {}_GENERATED_CLASS_BODY_{}", options.MacroPrefix, klass.BodyLine);
	output.StartDefine("#define {}_GENERATED_CLASS_BODY_{}", options.MacroPrefix, klass.BodyLine);

	/// All these are public
	output.CurrentIndent--;
	output.WriteLine("public:");
	output.CurrentIndent++;

	/// Typedefs
	output.WriteLine("using self_type = {};", klass.Name);
	output.WriteLine("static constexpr ::std::string_view self_type_name = \"{}\";", klass.Name);
	output.WriteLine("static constexpr ::std::string_view self_type_full_name = \"{}\";", klass.FullName());
	if (!klass.BaseClass.empty())
	{
		output.WriteLine("using parent_type = {};", klass.BaseClass);
		output.WriteLine("using parent_type::parent_type;");
	}
	else
	{
		output.WriteLine("using parent_type = void;");
	}

	if (options.JSON.Use && Attribute::Serialize.GetOr(klass, true) != false)
	{
		if (!klass.BaseClass.empty())
		{
			output.WriteLine("virtual void JSONLoadFields(REFLECTOR_JSON_TYPE const& src_object) override;", options.JSON.Type);
			output.WriteLine("virtual void JSONSaveFields(REFLECTOR_JSON_TYPE& src_object) const override;", options.JSON.Type);
		}
		else
		{
			output.WriteLine("void JSONLoadFields(REFLECTOR_JSON_TYPE const& src_object);", options.JSON.Type);
			output.WriteLine("void JSONSaveFields(REFLECTOR_JSON_TYPE& src_object) const;", options.JSON.Type);
		}
	}

	if (klass.Flags.is_set(ClassFlags::HasProxy))
		output.WriteLine("template <typename PROXY_OBJ> using proxy_class = {0}{1}<{0}, PROXY_OBJ>;", klass.FullType(), options.Names.ProxyClassSuffix);
	
	/// Flags
	/// TODO: Should these be exposed as well as a virtual method? I don't think so, these are in Class, right? (CHECK)
	output.WriteLine("static constexpr unsigned long long StaticClassFlags() {{ return {}; }}", klass.Flags.bits);

	/// Other body lines
	for (auto& line : klass.AdditionalBodyLines)
		output.WriteLine(line);

	/// ///////////////////////////////////// ///
	/// Reflection Data Method
	/// ///////////////////////////////////// ///

	output.WriteLine("static ::Reflector::Class const& StaticGetReflectionData() {{ return StaticGetReflectionData_For_{}(); }}", klass.GeneratedUniqueName());

	if (!klass.BaseClass.empty())
	{
		output.WriteLine("virtual ::Reflector::Class const& GetReflectionData() const {{ return StaticGetReflectionData_For_{}(); }}", klass.GeneratedUniqueName());
	}

	/// ///////////////////////////////////// ///
	/// Visitor methods
	/// ///////////////////////////////////// ///
	
	if (options.ScriptBinding.SplitTypeListIntoHookupFiles)
		output.WriteLine("friend void Reflect_{}();", klass.FullName());

	if (klass.BaseClass.empty())
	{
		output.WriteLine("template <typename VISITOR> static void ForEachMethod(VISITOR&& visitor, bool own_only = false) {{ {}_VISIT_{}_METHODS(visitor); }}", options.MacroPrefix, klass.FullName());
		output.WriteLine("template <typename VISITOR> static void ForEachField(VISITOR&& visitor, bool own_only = false) {{ {}_VISIT_{}_FIELDS(visitor); }}", options.MacroPrefix, klass.FullName());
		output.WriteLine("template <typename VISITOR> static void ForEachProperty(VISITOR&& visitor, bool own_only = false) {{ {}_VISIT_{}_PROPERTIES(visitor); }}", options.MacroPrefix, klass.FullName());
	}
	else
	{
		output.WriteLine("template <typename VISITOR> static void ForEachMethod(VISITOR&& visitor, bool own_only = false) {{ if (!own_only) parent_type::ForEachMethod(visitor); {}_VISIT_{}_METHODS(visitor); }}", options.MacroPrefix, klass.FullName());
		output.WriteLine("template <typename VISITOR> static void ForEachField(VISITOR&& visitor, bool own_only = false) {{ if (!own_only) parent_type::ForEachField(visitor); {}_VISIT_{}_FIELDS(visitor); }}", options.MacroPrefix, klass.FullName());
		output.WriteLine("template <typename VISITOR> static void ForEachProperty(VISITOR&& visitor, bool own_only = false) {{ if (!own_only) parent_type::ForEachProperty(visitor); {}_VISIT_{}_PROPERTIES(visitor); }}", options.MacroPrefix, klass.FullName());
	}
	if (options.AddGCFunctionality)
	{
		/// - Mark
		if (!klass.Flags.contain(ClassFlags::Struct))
		{
			output.WriteLine("virtual void GCMark() const override {{");
		}
		else
			output.WriteLine("void GCMark() const {{");
		if (!klass.BaseClass.empty())
			output.WriteLine("\tparent_type::GCMark();");
		output.WriteLine("\tForEachField([this](auto&& visitor_data) {{ ::Reflector::GCMark(visitor_data.Getter(this)); }});");
		output.WriteLine("}}");
	}


	/// ///////////////////////////////////// ///
	/// All methods
	/// ///////////////////////////////////// ///

	/// Callables for all methods
	for (auto& func : klass.Methods)
	{
		const auto debugging_comment_prefix = options.DebuggingComments ? std::format("/* {} */ ", func->Name) : std::string{};
		/*
		if (options.GenerateLuaFunctionBindings && !func->Flags.is_set(Reflector::MethodFlags::NoCallable))
			output.WriteLine("{}{}_CALLABLE(({}), {}, ({}))", debugging_comment_prefix, options.MacroPrefix, func->Type, func->GeneratedUniqueName(), func->GetParameters());
			*/
	}

	/// Output artificial methods
	for (auto& func : klass.Methods)
	{
		if (func->Flags.is_set(MethodFlags::Artificial))
			output.WriteLine("{}{}auto {}({}){} -> {} {{ {} }}", FormatAccess(func->Access), FormatPreFlags(func->Flags), func->Name, func->GetParameters(), FormatPostFlags(func->Flags), func->Return.Name, func->ArtificialBody);
	}

	/// ///////////////////////////////////// ///
	/// Struct for field reflections
	/// ///////////////////////////////////// ///

	if (options.GenerateClassMirrors)
	{
		output.WriteLine("template <typename T = {}>", klass.FullType());
		output.StartBlock("struct ClassMirror {{");

		for (size_t i = 0; i < klass.Fields.size(); i++)
		{
			const auto& field = klass.Fields[i];
			const auto ptr_str = "&T::" + field->Name;
			output.WriteLine("static inline ::Reflector::FieldVisitorData<::Reflector::CompileTimeFieldData<{2}, {3}, {4}, {5}, decltype({6}), {6}>> {8} {{ &{3}::StaticGetReflectionData().Fields[{7}] }};",
				0, 0,
				field->Type, /// 2
				"T", /// 3 
				field->Flags.bits, /// 4
				BuildCompileTimeLiteral(field->Name), /// 5
				ptr_str, /// 6
				i, /// 7
				field->Name /// 8
			);
		}

		output.EndBlock("}};");
	}


	/// Back to public
	output.EndDefine("public:");

	/// ///////////////////////////////////// ///
	/// Proxy class
	/// ///////////////////////////////////// ///

	output.WriteLine("#undef {}_GENERATED_CLASS_{}", options.MacroPrefix, klass.DeclarationLine);
	if (klass.Flags.is_set(ClassFlags::HasProxy))
	{
		output.StartDefine("#define {}_GENERATED_CLASS_{} template <typename T, typename PROXY_OBJ> struct {}{} : T {{", options.MacroPrefix, klass.DeclarationLine, klass.Name, options.Names.ProxyClassSuffix);
		output.CurrentIndent++;
		output.WriteLine("mutable PROXY_OBJ ReflectionProxyObject;");
		for (auto& func : klass.Methods)
		{
			if (!func->Flags.is_set(MethodFlags::Virtual))
				continue;

			auto base = std::format("virtual auto {}({})", func->Name, func->GetParameters());
			if (func->Flags.is_set(MethodFlags::Const))
				base += " const";
			if (func->Flags.is_set(MethodFlags::Noexcept))
				base += " noexcept";

			output.StartBlock("{} -> decltype(T::{}({})) override {{", base, func->Name, func->ParametersNamesOnly);
			/// TODO: Change {1} to std::forward<>({1})
			output.WriteLine("using return_type = decltype(T::{0}({1}));", func->Name, func->ParametersNamesOnly);
			if (func->Flags.is_set(MethodFlags::Abstract))
				output.WriteLine(R"(if (ReflectionProxyObject.Contains("{0}")) return ReflectionProxyObject.template CallOverload<return_type>("{0}"{2}{1}); else ReflectionProxyObject.AbstractCall("{0}");)", func->Name, func->ParametersNamesOnly, (func->ParametersSplit.empty() ? "" : ", "));
			else
				output.WriteLine(R"(return ReflectionProxyObject.Contains("{0}") ? ReflectionProxyObject.template CallOverload<return_type>("{0}"{2}{1}) : T::{0}({1});)", func->Name, func->ParametersNamesOnly, (func->ParametersSplit.empty() ? "" : ", "));
			output.EndBlock("}}");
		}
		output.CurrentIndent--;
		output.EndDefine("}};");
	}
	else
		output.WriteLine("#define {}_GENERATED_CLASS_{}", options.MacroPrefix, klass.DeclarationLine);
	
	return true;
}

bool FileMirrorOutputContext::BuildEnumEntry(const Enum& henum)
{
	const auto has_any_enumerators = !henum.Enumerators.empty();
	output.WriteLine("/// From enum: {}", henum.FullType());

	WriteForwardDeclaration(henum);

	/// TODO: Make the name of this function configurable
	output.WriteLine("extern ::Reflector::Enum const& StaticGetReflectionData_For_{}();", henum.GeneratedUniqueName());
	output.StartBlock("namespace Reflector {{");
	output.WriteLine("template <> inline ::Reflector::Enum const& GetEnumReflectionData<{}>() {{ return StaticGetReflectionData_For_{}(); }}", henum.FullType(), henum.GeneratedUniqueName());
	output.WriteLine("template <> constexpr bool IsReflectedEnum<{}>() {{ return true; }}", henum.FullType());
	output.EndBlock("}}");

	/// ///////////////////////////////////// ///
	/// Declaration and Artificials
	/// ///////////////////////////////////// ///

	if (!henum.Namespace.empty())
	{
		output.StartBlock("namespace {} {{", henum.Namespace);
	}


	/// TODO: Make sure these work when the enums are in namespaces
	/// 
	/// TODO: Make the name of ALL these functions and values in the global namespace configurable
	/// TODO: Instead, we could do something like:
	/// 
	/// constexpr inline ::Reflector::EnumProperties EnumDataFor_ENUM_FULL_NAME = {
	///		.Count = ...,
	///		.Names = ...,
	/// };
	/// constexpr ::Reflector::EnumProperties const& EnumDataFor(ENUM_TYPE) { return EnumDataFor_ENUM_FULL_NAME; }
	/// 
	/// Basically do something to avoid declaring any functions/variables in the enum's namespace
	{
		output.WriteLine("constexpr inline size_t {}Count = {};", henum.Name, henum.Enumerators.size());
		if (has_any_enumerators)
		{
			output.WriteLine("constexpr inline std::pair<{0}, std::string_view> {0}Entries[] = {{ {1} }};", henum.Name, 
				join(henum.Enumerators, ", ", [&](auto const& enumerator) { 
					return std::format("std::pair<{0}, std::string_view>{{ {0}{{{1}}}, \"{2}\" }}", henum.Name, enumerator->Value, enumerator->Name); 
					}));
			output.WriteLine("constexpr inline std::string_view {}NamesByIndex[] = {{ {} }};", henum.Name, join(henum.Enumerators, ", ", [](auto const& enumerator) { return std::format("\"{}\"", enumerator->Name); }));
			output.WriteLine("constexpr inline std::string_view {}DisplayNamesByIndex[] = {{ {} }};", henum.Name, join(henum.Enumerators, ", ", [](auto const& enumerator) { return std::format("\"{}\"", enumerator->DisplayName); }));
			output.WriteLine("constexpr inline {0} {0}ValuesByIndex[] = {{ {1} }} ;", henum.Name, join(henum.Enumerators, ", ", [&](auto const& enumerator) {
				return std::format("{}{{{}}}", henum.Name, enumerator->Value);
			}));

			output.WriteLine("constexpr inline {0} First{0} = {0}{{{1}}};", henum.Name, henum.Enumerators.front()->Value);
			output.WriteLine("constexpr inline {0} Last{0} = {0}{{{1}}};", henum.Name, henum.Enumerators.back()->Value);
		}
	}

	output.WriteLine("constexpr std::string_view GetEnumName({0}) {{ return \"{0}\"; }}", henum.Name);
	if (has_any_enumerators)
	{
		output.WriteLine("constexpr auto const& GetEnumerators({0}) {{ return {0}Entries; }}", henum.Name);
		output.WriteLine("constexpr auto const& GetEnumeratorNames({0}) {{ return {0}NamesByIndex; }}", henum.Name);
		output.WriteLine("constexpr auto const& GetEnumeratorValuesByIndex({0}) {{ return {0}ValuesByIndex; }}", henum.Name);
	}
	output.WriteLine("constexpr size_t GetEnumCount({}) {{ return {}; }}", henum.Name, henum.Enumerators.size());
	if (has_any_enumerators)
		output.WriteLine("constexpr {0} GetEnumeratorValue({0}, size_t index) {{ return {0}ValuesByIndex[index]; }}", henum.Name);
	else
		output.WriteLine("constexpr {0} GetEnumeratorValue({0}, size_t index) {{ return {{}}; }}", henum.Name);


	output.WriteLine("constexpr size_t GetEnumeratorIndex({} v) {{", henum.Name);
	{
		auto indent = output.Indent();

		if (has_any_enumerators)
		{
			if (henum.IsConsecutive())
			{
				output.WriteLine("if (int64_t(v) >= {0} && int64_t(v) <= {1}) return size_t(v) - size_t({0});", henum.Enumerators.front()->Value, henum.Enumerators.back()->Value, henum.Name);
			}
			else
			{
				output.WriteLine("switch (int64_t(v)) {{");
				for (auto& enumerator : henum.Enumerators)
				{
					output.WriteLine("case {}: return {};", enumerator->Value, enumerator->Value);
				}
				output.WriteLine("}}");
			}
		}
		output.WriteLine("return 0;");
	}
	output.WriteLine("}}");

	output.WriteLine("constexpr std::string_view GetEnumeratorName({} v, bool display_name = false) {{", henum.Name);
	{
		auto indent = output.Indent();

		if (has_any_enumerators)
		{
			if (henum.IsConsecutive())
			{
				output.WriteLine("if (int64_t(v) >= {0} && int64_t(v) <= {1}) return display_name ? {2}DisplayNamesByIndex[int64_t(v)-({0})] : {2}NamesByIndex[int64_t(v)-({0})];", henum.Enumerators.front()->Value, henum.Enumerators.back()->Value, henum.Name);
			}
			else
			{
				output.WriteLine("switch (int64_t(v)) {{");
				for (auto& enumerator : henum.Enumerators)
				{
					if (enumerator->DisplayName == enumerator->Name)
						output.WriteLine("case {}: return \"{}\";", enumerator->Value, enumerator->Name);
					else
						output.WriteLine(R"(case {}: return display_name ? "{}" : "{}";)", enumerator->Value, enumerator->DisplayName, enumerator->Name);
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
	/// TODO: EITHER: Use mph ( https://www.ibiblio.org/pub/Linux/devel/lang/c/!INDEX.short.html Q:\Code\Native\External\mph https://github.com/boost-ext/mph ) to generate a minimal perfect hash function for this
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
	/// TODO: Make the name of this function configurable
	output.WriteLine("constexpr {0} GetEnumeratorFromName({0}, std::string_view name) {{", henum.Name);
	{
		auto indent = output.Indent();
		for (auto& enumerator : henum.Enumerators)
		{
			output.WriteLine("if (name == \"{}\") return ({}){};", enumerator->Name, henum.Name, enumerator->Value);
		}
		output.WriteLine("return {{}};");
	}
	output.WriteLine("}}");

	if (has_any_enumerators && Attribute::List(henum))
	{
		/// TODO: Make the name of these functions configurable
		output.WriteLine("constexpr {0} GetNext({0} v) {{ return {0}ValuesByIndex[(int64_t(v) + 1) % {0}Count]; }}", henum.Name);
		output.WriteLine("constexpr {0} GetPrev({0} v) {{ return {0}ValuesByIndex[(int64_t(v) + ({0}Count - 1)) % {0}Count]; }}", henum.Name);

		/// Preincrement
		output.WriteLine("constexpr {0}& operator++({0}& v) {{ v = GetNext(v); return v; }}", henum.Name);
		output.WriteLine("constexpr {0}& operator--({0}& v) {{ v = GetPrev(v); return v; }}", henum.Name);

		/// Postincrement
		output.WriteLine("constexpr {0} operator++({0}& v, int) {{ auto result = v; v = GetNext(v); return result; }}", henum.Name);
		output.WriteLine("constexpr {0} operator--({0}& v, int) {{ auto result = v; v = GetPrev(v); return result; }}", henum.Name);
	}

	output.WriteLine("constexpr bool operator==(std::underlying_type_t<{0}> left, {0} right) noexcept {{ return left == static_cast<std::underlying_type_t<{0}>>(right); }}", henum.Name);
	output.WriteLine("constexpr auto operator<=>(std::underlying_type_t<{0}> left, {0} right) noexcept {{ return left <=> static_cast<std::underlying_type_t<{0}>>(right); }}", henum.Name);

	output.WriteLine("std::ostream& operator<<(std::ostream& strm, {} v);", henum.Name);
	
	/// TODO: Make the name of this function configurable
	/// TODO: Move the body of the below function to the reflector header
	/*
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
	*/

	if (!henum.Namespace.empty())
	{
		output.EndBlock("}}"); /// end namespace
	}


	output.StartDefine("#define {0}_VISIT_{1}_ENUMERATORS({0}_VISITOR)", options.MacroPrefix, henum.FullName());
	for (size_t i = 0; i < henum.Enumerators.size(); i++)
	{
		const auto& enumerator = henum.Enumerators[i];
		const auto debugging_comment_prefix = options.DebuggingComments ? std::format("/* {} */ ", enumerator->Name) : std::string{};
		output.WriteLine("{4}{0}_VISITOR(&StaticGetReflectionData({1}{{}}).Enumerators[{2}], {1}::{3}, \"{3}\");", 
			options.MacroPrefix, henum.FullName(), i, enumerator->Name, debugging_comment_prefix);
	}
	output.EndDefine("");

	return true;
}

bool FileMirrorOutputContext::BuildDatabaseEntriesForMirror()
{
	for (auto& klass : mirror.Classes)
	{
		BuildStaticReflectionData(*klass);
		output.WriteLine();
	}
	for (auto& henum : mirror.Enums)
	{
		BuildStaticReflectionData(*henum);
		output.WriteLine();
	}

	return true;
}

bool BuildMirrorHookupFile(ArtifactArgs args, FileMirror const& mirror)
{
	auto const& [_, final_path, options, factory] = args;
	FileWriter f{ args };

	f.EnsurePCH();
	f.WriteLine("#include \"{}\"", mirror.SourceFilePath.string());
	f.WriteLine("#include \"Hookup.h\"");
	for (auto& klass : mirror.Classes)
		f.WriteLine("void Reflect_{}() {{ ReflectClass({}, {}); }}", klass->FullName(), klass->Name, klass->FullType());
	for (auto& henum : mirror.Enums)
		f.WriteLine("void Reflect_{}() {{ ReflectEnum({}, {}); }}", henum->FullName(), henum->Name, henum->FullType());

	return true;
}

bool BuildMirrorFile(ArtifactArgs args, FileMirror const& mirror, uint64_t file_change_time)
{
	auto const& [_, final_path, options, factory] = args;
	FileWriter f{ args };
	f.WriteLine("{}{}", TIMESTAMP_TEXT, file_change_time);
	f.WriteLine("/// Source file: {}", mirror.SourceFilePath.string());
	f.WriteLine("#pragma once");

	const auto rel = RelativePath(final_path, options.ArtifactPath / "Reflector.h");
	f.WriteLine("#include \"{}\"", rel.string());

	FileMirrorOutputContext context{ 
		mirror,
		f,
		options, 
	};

	for (auto& klass : mirror.Classes)
	{
		if (!context.BuildClassEntry(*klass))
			continue;
		f.WriteLine();
	}

	for (auto& henum : mirror.Enums)
	{
		if (!context.BuildEnumEntry(*henum))
			continue;
		f.WriteLine();
	}

	return true;
}

void OutputContext::WriteForwardDeclaration(const Class& klass)
{
	if (klass.Namespace.empty())
		output.WriteLine("{} {};", (klass.Flags.is_set(ClassFlags::DeclaredStruct) ? "struct" : "class"), klass.FullType());
	else
		output.WriteLine("namespace {} {{ {} {}; }}", klass.Namespace, klass.Flags.is_set(ClassFlags::DeclaredStruct) ? "struct" : "class", klass.Name);
}

void OutputContext::WriteForwardDeclaration(const Enum& henum)
{
	const auto base = henum.BaseType.empty() ? std::string{} : std::format(" : {}", henum.BaseType);
	if (henum.Namespace.empty())
		output.WriteLine("enum class {}{};", henum.FullType(), base);
	else
		output.WriteLine("namespace {} {{ enum class {}{}; }}", henum.Namespace, henum.Name, base);
}

void OutputContext::BuildStaticReflectionData(const Enum& henum)
{
	output.StartBlock("::Reflector::Enum const& StaticGetReflectionData_For_{}() {{", henum.GeneratedUniqueName());
	if (!henum.Namespace.empty())
		output.WriteLine("using namespace {};", henum.Namespace);
	output.StartBlock("static const ::Reflector::Enum _data = {{");

	output.WriteLine(".Name = \"{}\",", henum.Name);
	if (!henum.DisplayName.empty())
		output.WriteLine(".DisplayName = \"{}\",", henum.DisplayName);
	output.WriteLine(".FullType = \"{}\",", henum.FullType());
	if (!henum.Attributes.empty())
	{
		output.WriteLine(".Attributes = {},", EscapeJSON(henum.Attributes));
		if (options.JSON.Use)
			output.WriteLine(".AttributesJSON = {}({}),", options.JSON.ParseFunction, EscapeJSON(henum.Attributes));
	}
	output.StartBlock(".Enumerators = {{");
	for (auto& enumerator : henum.Enumerators)
	{
		if (enumerator->Attributes.empty())
			output.WriteLine(R"({{ "{}", "{}", {}, {}, }},)", enumerator->Name, enumerator->DisplayName, enumerator->Value, enumerator->Flags.bits);
		else if (options.JSON.Use)
			output.WriteLine(R"({{ "{}", "{}", {}, {}, {}, {}({}) }},)", enumerator->Name, enumerator->DisplayName, enumerator->Value, enumerator->Flags.bits, EscapeJSON(enumerator->Attributes), options.JSON.ParseFunction, EscapeJSON(enumerator->Attributes));
		else
			output.WriteLine(R"({{ "{}", "{}", {}, {}, {} }},)", enumerator->Name, enumerator->DisplayName, enumerator->Value, enumerator->Flags.bits, EscapeJSON(enumerator->Attributes));
	}
	output.EndBlock("}},");
	output.WriteLine(".TypeIndex = typeid({}),", henum.FullType());
	if (!henum.Flags.empty())
		output.WriteLine(".Flags = {},", henum.Flags.bits);
	output.EndBlock("}}; return _data;");
	output.EndBlock("}}");

	output.WriteLine("std::ostream& operator<<(std::ostream& strm, {} v) {{ strm << GetEnumeratorName(v); return strm; }}", henum.FullType());
}

void OutputContext::BuildStaticReflectionData(const Class& klass)
{
	output.WriteLine("static_assert(!::Reflector::derives_from_reflectable<{0}> || ::Reflector::derives_from_reflectable<{0}::parent_type>, \"Base class of {0} ({1}) must also be reflectable (marked with RClass+RBody)\");", klass.FullType(), klass.BaseClass);

	if (options.AddGCFunctionality)
	{
		output.WriteLine("template <>");
		output.WriteLine("void ::Reflector::GCMark<{0}>({0} const* r) {{ ::Reflector::GCMark((::Reflector::Reflectable const*)r); }}", klass.FullType());
	}

	/// Ensure fields do not shadow parent class fields
	if (!klass.BaseClass.empty() && !klass.Fields.empty())
	{
		output.StartBlock("template <typename T> concept {}_FieldsNotShadowedCheck = requires ( T value ) {{", klass.FullName());
		for (auto const& field : klass.Fields)
		{
			output.WriteLine("{{ value.{} }};", field->Name);
		}
		output.EndBlock("}};");
		output.WriteLine("static_assert(!{}_FieldsNotShadowedCheck<typename {}::parent_type>, \"A field in class '{}' shadows a field in its base class '{}'; Reflector currently does not support this\");", klass.FullName(), klass.FullType(), klass.FullType(), klass.BaseClass);
	}

	if (options.JSON.Use && options.JSON.GenerateSerializationMethods && Attribute::Serialize.GetOr(klass, true) != false)
	{
		output.StartBlock("void {}::JSONLoadFields({} const& src_object) {{", klass.FullType(), options.JSON.Type);
		if (!klass.BaseClass.empty())
			output.WriteLine("{}::parent_type::JSONLoadFields(src_object);", klass.FullType());

		/// TODO: call this->BeforeSerialize(src_object);, etc

		output.WriteLine();
		for (auto& field : klass.Fields)
		{
			if (field->Flags.contain(FieldFlags::NoLoad))
				continue;

			std::string reset_line;
			if (!field->InitializingExpression.empty())
			{
				if (field->Flags.contain(FieldFlags::BraceInitialized))
					reset_line = format("this->{} = {}{};", field->Name, field->Type, field->InitializingExpression);
				else
					reset_line = format("this->{} = {};", field->Name, field->InitializingExpression);
			}
			else
				reset_line = format("this->{0} = decltype(this->{0}){{}};", field->Name);

			output.StartBlock("if (auto it = src_object.find(\"{}\"); it == src_object.end())", field->LoadName);

			if (field->Flags.contain(FieldFlags::Required))
				output.WriteLine("throw ::Reflector::DataError{{ \"Missing field '{}'\" }};", field->LoadName);
			else
				output.WriteLine("{}", reset_line);

			output.EndBlock();
			output.StartBlock("else try {{");
			output.WriteLine("using field_type = std::remove_cvref_t<decltype({})>;", field->FullName("::"));
			output.WriteLine("static_assert(::nlohmann::detail::is_basic_json<field_type>::value || ::nlohmann::detail::has_from_json<::nlohmann::json, field_type>::value, \"cannot serialize type '{0}' of field {1}\");", field->Type, field->FullName("::"));
			output.WriteLine("it->get_to<field_type>(this->{});", field->Name);
			output.EndBlock("}}");
			if (options.JSON.IgnoreInvalidObjectFields)
			{
				output.StartBlock("catch (...) {{");
				output.WriteLine("{}", reset_line);
				/// TODO: Probably should report this error somehow
				output.EndBlock("}}");
			}
			else
			{
				output.StartBlock("catch (::Reflector::DataError& e) {{");
				output.WriteLine("e.File += \"/{}\";", field->LoadName);
				output.WriteLine("throw;");
				output.EndBlock("}}");
			}

			output.WriteLine();
		}
		output.EndBlock("}}");


		output.StartBlock("void {}::JSONSaveFields({}& dest_object) const {{", klass.FullType(), options.JSON.Type);
		if (!klass.BaseClass.empty())
			output.WriteLine("{}::parent_type::JSONSaveFields(dest_object);", klass.FullType());

		for (auto& field : klass.Fields)
		{
			if (field->Flags.contain(FieldFlags::NoSave))
				continue;

			const auto check_for_init_value = !field->InitializingExpression.empty() 
				&& !options.JSON.AlwaysSaveAllFields 
				&& !field->Flags.contain(FieldFlags::Required);

			/// TODO: AlwaysSave field attribute

			if (check_for_init_value)
			{
				output.StartBlock("do {{");
				output.StartBlock("if constexpr (std::equality_comparable<{}>)", field->Type);
				output.WriteLine("if (::Compare_(this->{}, {})) break;", field->Name, field->InitializingExpression.empty() ? "{}" : field->InitializingExpression);
				output.EndBlock();
			}

			output.WriteLine("dest_object[\"{}\"] = this->{};", field->SaveName, field->Name);

			if (check_for_init_value)
				output.EndBlock("}} while (false);");

			//output.WriteLine();
		}
		if (!klass.BaseClass.empty())
		{
			output.WriteLine(R"(dest_object["{}"] = "{}";)", options.JSON.ObjectTypeFieldName, klass.FullType());
			if (!klass.GUID.empty())
				output.WriteLine(R"(dest_object["{}"] = "{}";)", options.JSON.ObjectGUIDFieldName, klass.GUID);
		}
		output.EndBlock("}}");
	}

	output.StartBlock("::Reflector::Class const& StaticGetReflectionData_For_{}() {{", klass.GeneratedUniqueName());
	if (!klass.Namespace.empty())
		output.WriteLine("using namespace {};", klass.Namespace);
	output.StartBlock("static const ::Reflector::Class _data = {{");
	output.WriteLine(".Name = \"{}\",", klass.Name);
	if (!klass.DisplayName.empty())
		output.WriteLine(R"(.DisplayName = "{}",)", klass.DisplayName);
	output.WriteLine(R"(.FullType = "{}",)", klass.FullType());

	/// TODO: Comment and describe here why we should only give the type as the parent class name, since we support full namespaced names?
	///output.WriteLine(".BaseClassName = \"{}\",", OnlyType(klass.BaseClass));
	output.WriteLine(R"(.BaseClassName = "{}",)", klass.BaseClass);

	if (!klass.Attributes.empty())
	{
		output.WriteLine(".Attributes = {},", EscapeJSON(klass.Attributes));
		if (options.JSON.Use)
			output.WriteLine(".AttributesJSON = {}({}),", options.JSON.ParseFunction, EscapeJSON(klass.Attributes));
	}
	output.WriteLine(".ReflectionUID = {}ULL,", klass.ReflectionUID);
	if (!klass.GUID.empty())
		output.WriteLine(".GUID = {},", EscapeString(klass.GUID));
	output.WriteLine(".Alignment = alignof({0}),", klass.FullType());
	output.WriteLine(".Size = sizeof({0}),", klass.FullType());
	if (!klass.Flags.is_set(ClassFlags::NoConstructors))
	{
		output.WriteLine(".DefaultPlacementConstructor = +[](void* ptr){{ new (ptr) {0}({0}::StaticGetReflectionData()); }},", klass.FullType());
		output.WriteLine(".DefaultConstructor = +[]() -> void* {{ return new {0}({0}::StaticGetReflectionData()); }},", klass.FullType());
	}
	output.WriteLine(".Destructor = +[](void* obj){{ auto _tobj = ({}*)obj; _tobj->~{}(); }},", klass.FullType(), klass.Name);

	/// Fields
	output.StartBlock(".Fields = {{");
	for (auto& field : klass.Fields)
	{
		output.StartBlock("{{");
		output.WriteLine(".Name = \"{}\",", field->Name);
		if (!field->DisplayName.empty())
			output.WriteLine(".DisplayName = \"{}\",", field->DisplayName);
		output.WriteLine(".FieldType = \"{}\",", field->Type);
		if (field->InitializingExpression == "{}")
			output.WriteLine(".Initializer = empty_json_object_str,");
		else if (!field->InitializingExpression.empty())
			output.WriteLine(".Initializer = {},", EscapeString(field->InitializingExpression));
		if (!field->Attributes.empty())
		{
			output.WriteLine(".Attributes = {},", EscapeJSON(field->Attributes));
			if (options.JSON.Use)
				output.WriteLine(".AttributesJSON = {}({}),", options.JSON.ParseFunction, EscapeJSON(field->Attributes));
		}
		output.WriteLine(".FieldTypeIndex = typeid({}),", field->Type);
		if (!field->Flags.empty())
			output.WriteLine(".Flags = {},", field->Flags.bits);
		output.WriteLine(".ParentClass = &_data");
		output.EndBlock("}},");
	}
	output.EndBlock("}},");

	/// Methods
	output.StartBlock(".Methods = {{");
	for (auto& method : klass.Methods)
	{
		output.StartBlock("::Reflector::Method {{");
		output.WriteLine(".Name = \"{}\",", method->Name);
		if (!method->DisplayName.empty())
			output.WriteLine(".DisplayName = \"{}\",", method->DisplayName);
		if (method->Return.Name != "void")
			output.WriteLine(".ReturnType = \"{}\",", method->Return.Name);
		if (!method->GetParameters().empty())
		{
			output.WriteLine(".Parameters = {},", EscapeString(method->GetParameters()));
			output.WriteLine(".ParametersSplit = {{ {} }},", join(method->ParametersSplit, ", ", [](MethodParameter const& param) {
				return format("{{ {}, {}, {} }}", EscapeString(param.Name), EscapeString(param.Type), EscapeString(param.Initializer));
			}));
		}
		if (!method->Attributes.empty())
		{
			output.WriteLine(".Attributes = {},", EscapeJSON(method->Attributes));
			if (options.JSON.Use)
				output.WriteLine(".AttributesJSON = {}({}),", options.JSON.ParseFunction, EscapeJSON(method->Attributes));
		}
		if (!method->UniqueName.empty())
			output.WriteLine(".UniqueName = \"{}\",", method->UniqueName);
		if (options.ReflectBodiesOfArtificialFunctions)
		{
			if (!method->ArtificialBody.empty())
				output.WriteLine(".ArtificialBody = {},", EscapeString(method->ArtificialBody));
		}
		if (method->Return.Name != "void")
			output.WriteLine(".ReturnTypeIndex = typeid({}),", method->Return.Name);
		if (!method->GetParameters().empty())
			output.WriteLine(".ParameterTypeIndices = {{ {} }},", join(method->ParametersSplit, ", ", [](MethodParameter const& param) { return format("typeid({})", param.Type); }));
		if (!method->Flags.empty())
			output.WriteLine(".Flags = {},", method->Flags.bits);
		output.WriteLine(".ParentClass = &_data");
		output.EndBlock("}},");
	}
	output.EndBlock("}},");

	/// Properties
	output.StartBlock(".Properties = {{");
	for (auto& [name, property] : klass.Properties)
	{
		output.StartBlock("::Reflector::Property {{");
		output.WriteLine(".Name = \"{}\",", name);
		if (!property.DisplayName.empty())
			output.WriteLine(".DisplayName = \"{}\",", property.DisplayName);
		output.WriteLine(".Type = \"{}\",", property.Type);

		if (!property.Attributes.empty())
		{
			output.WriteLine(".Attributes = {},", EscapeJSON(property.Attributes));
			if (options.JSON.Use)
				output.WriteLine(".AttributesJSON = {}({}),", options.JSON.ParseFunction, EscapeJSON(property.Attributes));
		}

		if (property.Getter)
		{
			output.WriteLine(".Getter = [](void const* self, void* out_value) {{ *reinterpret_cast<std::add_pointer_t<{}>>(out_value) = reinterpret_cast<std::add_pointer_t<{} const>>(self)->{}(); }},",
				property.Type,
				klass.Name,
				property.Getter->Name
			);
		}
		if (property.Setter)
		{
			output.WriteLine(".Setter = [](void* self, void const* in_value) {{ reinterpret_cast<std::add_pointer_t<{}>>(self)->{}(*reinterpret_cast<std::add_pointer_t<{} const>>(in_value)); }},",
				klass.Name,
				property.Setter->Name,
				property.Type
			);
		}
		output.WriteLine(".PropertyTypeIndex = typeid({}),", klass.Name, property.Type);
		if (!property.Flags.empty())
			output.WriteLine(".Flags = {},", property.Flags.bits);
		output.WriteLine(".ParentClass = &_data");
		output.EndBlock("}},");
	}
	output.EndBlock("}},");

	if (options.JSON.Use && Attribute::Serialize.GetOr(klass, true) != false)
	{
		output.StartBlock(".JSONLoadFieldsFunc = [](void* dest_object, {} const& src_object){{", options.JSON.Type);
		output.WriteLine("(({0}*)dest_object)->JSONLoadFields(src_object);", klass.FullType());
		output.EndBlock("}},");
		output.StartBlock(".JSONSaveFieldsFunc = [](void const* src_object, {}& dest_object){{", options.JSON.Type);
		output.WriteLine("(({0} const*)src_object)->JSONSaveFields(dest_object);", klass.FullType());
		output.EndBlock("}},");
	}

	output.WriteLine(".TypeIndex = typeid({}),", klass.FullType());
	output.WriteLine(".Flags = {}", klass.Flags.bits);
	output.EndBlock("}}; return _data;");
	output.EndBlock("}}");

}
