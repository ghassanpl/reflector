/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "ReflectionDataBuilding.h"
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

static const std::string_view ReflectorClassesFile = R"blergh(#pragma once

#include <typeindex>
#include <vector>

template <typename T> concept is_reflected_class = requires { T::StaticClassFlags(); };

namespace Reflector
{
	struct ClassReflectionData;
	struct FieldReflectionData;
	struct MethodReflectionData;

	struct ClassReflectionData
	{
		const char* Name = "";
		const char* ParentClassName = "";
		const char* Attributes = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json AttributesJSON;
#endif
		void* (*Constructor)(const ::Reflector::ClassReflectionData&) = {};

		/// These are vectors and not e.g. initializer_list's because you might want to create your own classes
		std::vector<FieldReflectionData> Fields; 
		std::vector<MethodReflectionData> Methods;

		std::type_index TypeIndex;
	};

	enum class ClassFlags
	{
		Struct,
		DeclaredStruct,
		NoConstructors,
		HasProxy
	};

	enum class FieldFlags
	{
		NoSetter,
		NoGetter,
		NoEdit,
		NoScript,
		NoSave,
		NoLoad,
		NoDebug
	};

	enum class MethodFlags
	{
		Inline,
		Virtual,
		Abstract,
		Static,
		Const,
		Noexcept,
		Final,
		Explicit,
		Artificial,
		HasBody,
		NoCallable
	};

	template <char... CHARS>
	struct CompileTimeLiteral
	{
		static constexpr const char value[] = { CHARS... };
	};
	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, typename NAME_CTL>
	struct CompileTimePropertyData
	{
		using type = std::remove_cvref_t<FIELD_TYPE>;
		using parent_type = PARENT_TYPE;
		static constexpr uint64_t flags = FLAGS;
		static constexpr const char* name = NAME_CTL::value;

		template <typename FLAG_TYPE>
		static inline constexpr bool HasFlag(FLAG_TYPE flag_val) noexcept { return (flags & (1ULL << uint64_t(flag_val))) != 0; }
	};
	template <typename FIELD_TYPE, typename PARENT_TYPE, uint64_t FLAGS, typename NAME_CTL, typename PTR_TYPE, PTR_TYPE POINTER>
	struct CompileTimeFieldData : CompileTimePropertyData<FIELD_TYPE, PARENT_TYPE, FLAGS, NAME_CTL>
	{
		static constexpr PTR_TYPE pointer = POINTER;

		static auto Getter(PARENT_TYPE const* obj) -> FIELD_TYPE const& { return (obj->*(pointer)); }
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericGetter(PARENT const* obj) -> FIELD const& { return (obj->*(pointer)); }

		template <typename PARENT = PARENT_TYPE, typename VALUE>
		static auto GenericSetter(PARENT* obj, VALUE&& value) -> void { (obj->*(pointer)) = std::forward<VALUE>(value); };

		static auto CopySetter(PARENT_TYPE* obj, FIELD_TYPE const& value) -> void { (obj->*(pointer)) = value; };
		static auto MoveSetter(PARENT_TYPE* obj, FIELD_TYPE&& value) -> void { (obj->*(pointer)) = std::move(value); };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericCopySetter(PARENT* obj, FIELD const& value) -> void { (obj->*(pointer)) = value; };
		template <typename PARENT = PARENT_TYPE, typename FIELD = FIELD_TYPE>
		static auto GenericMoveSetter(PARENT* obj, FIELD&& value) -> void { (obj->*(pointer)) = std::move(value); };

		static auto VoidGetter(void const* obj) -> void const* { return &(obj->*(pointer)); }
		static auto VoidSetter(void* obj, void const* value) -> void { (obj->*(pointer)) = reinterpret_cast<FIELD_TYPE const*>(value); };
	};
	template <uint64_t FLAGS, typename NAME_CTL>
	struct CompileTimeMethodData
	{
		static constexpr uint64_t flags = FLAGS;
		static constexpr const char* name = NAME_CTL::value;

		static inline constexpr bool HasFlag(MethodFlags flag_val) noexcept { return (flags & (1ULL << uint64_t(flag_val))) != 0; }
	};

	struct FieldReflectionData
	{
		const char* Name = "";
		const char* FieldType = "";
		const char* Initializer = "";
		const char* Attributes = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json AttributesJSON;
#endif
		std::type_index FieldTypeIndex;

		ClassReflectionData const* ParentClass = nullptr;
	};

	struct MethodReflectionData
	{
		const char* Name = "";
		const char* ReturnType = "";
		const char* Parameters = "";
		const char* Attributes = "{}";
#ifdef NLOHMANN_JSON_VERSION_MAJOR
		nlohmann::json AttributesJSON;
#endif
		const char* UniqueName = "";
		const char* Body = "";
		std::type_index ReturnTypeIndex;

		ClassReflectionData const* ParentClass = nullptr;
	};

	struct EnumeratorReflectionData
	{
		const char* Name = "";
		int64_t Value;
	};

	struct EnumReflectionData
	{
		const char* Name = "";
		const char* Attributes = "{}";
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
				.Attributes = "",
#ifdef NLOHMANN_JSON_VERSION_MAJOR
				.AttributesJSON = ::nlohmann::json::object(),
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

	template <typename T, typename PROXY_OBJ> 
	struct ProxyFor
	{
		using Type = void;
	};
}
)blergh";

bool CreateReflectorClassesHeaderArtifact(path const& path, const Options& options)
{
	FileWriter reflect_classes_file{ path };
	reflect_classes_file.mOutFile << ReflectorClassesFile;
	reflect_classes_file.Close();
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
		output.WriteLine(".FieldTypeIndex = typeid({})", field.Type);
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
		output.WriteLine(".ParentClass = &_data");
		output.CurrentIndent--;
		output.WriteLine("}},");
	}
	output.CurrentIndent--;
	output.WriteLine("}},");

	output.WriteLine(".TypeIndex = typeid(self_type)");
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
	output.WriteLine("/// From enum: {}", henum.Name);
	output.WriteLine("#undef {}_ENUM_{}", options.MacroPrefix, henum.DeclarationLine);
	output.StartDefine("#define {}_ENUM_{}", options.MacroPrefix, henum.DeclarationLine);

	output.WriteLine("enum class {};", henum.Name); /// forward decl;


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
		output.WriteLine(".Value = {}", enumerator.Value);
		output.CurrentIndent--;
		output.WriteLine("}},");
	}
	output.CurrentIndent--;
	output.WriteLine("}},");
	output.WriteLine(".TypeIndex = typeid({})", henum.Name);
	output.CurrentIndent--;
	output.WriteLine("}}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}}");

	output.WriteLine("inline constexpr const char* GetEnumName({0}) {{ return \"{0}\"; }}", henum.Name);
	output.WriteLine("inline constexpr const char* GetEnumeratorName({} v) {{", henum.Name);
	{
		auto indent = output.Indent();

		if (henum.Enumerators.size())
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