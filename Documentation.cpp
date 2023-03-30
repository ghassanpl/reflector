#include "Documentation.h"
#include "FileWriter.h"
#include "Options.h"
#include "Attributes.h"
#include <ghassanpl/json_helpers.h>
#include <set>
#include <magic_enum_format.hpp>

/// TODO: Documentation options

struct HTMLFileWriter : public FileWriter
{
	Options const& options;

	HTMLFileWriter(Options const& _options) : options(_options) {}

	void StartPage(std::string_view title)
	{
		WriteLine("<!doctype html>");
		StartTag("html");

		StartTag("head");
		WriteLine("<title>{}{}</title>", title, options.Documentation.PageTitleSuffix);
		WriteLine(R"(<link rel="stylesheet" href="style.css" />)");
		WriteLine(R"(<script src="https://cdn.jsdelivr.net/gh/MarketingPipeline/Markdown-Tag/markdown-tag.js"></script>)");
		WriteLine(R"(<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.7.0/styles/vs2015.min.css"><script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.7.0/highlight.min.js"></script>)");
		EndTag();

		StartTag("body");
	}

	void EndPage()
	{
		WriteLine("{}", R"(<script>document.addEventListener('DOMContentLoaded', (event) => {
	let lang = hljs.getLanguage('cpp');
	lang.keywords.keyword = lang.keywords.keyword.concat(lang.keywords.type);
	lang.keywords.type = lang.keywords._type_hints;
	lang.keywords._type_hints = [];
	document.querySelectorAll('code').forEach((el) => {
		hljs.highlightElement(el);
	});
});</script>)");
		EndTag();
		EndTag();
	}

	template <typename... ARGS>
	void StartTag(std::string tag_name, std::string_view fmt, ARGS&&... args)
	{
		TagStack.push_back(tag_name);
		StartBlock("<{} {}>", tag_name, std::vformat(fmt, std::make_format_args(std::forward<ARGS>(args)...)));
	}

	void StartTag(std::string tag_name)
	{
		TagStack.push_back(tag_name);
		StartBlock("<{}>", tag_name);
	}
	void EndTag()
	{
		EndBlock("</{}>", TagStack.back());
		TagStack.pop_back();
	}

	std::vector<std::string> TagStack;
};

struct DocumentationGenerator
{
	Options const& options;
	json styles;
	//std::map<void const*, path> file_for_mirror;
	std::vector<Class const*> classes;
	std::vector<Enum const*> enums;

	struct ParsedComments
	{
		std::string Brief;
		std::string Description;
		std::map<std::string, std::string> Parameters;
		std::string ReturnTypeDescription;
		std::vector<std::string> SeeAlsos;
		std::string Section;
	};

	/// @param
	/// @return
	/// @see
	/// @warn - e.g @warn deprecated
	/// @group
	/// @default <value> - for when a field is initialized in the constructor, but we still want to document its default value
	/// @throws <exception> <when>

	/// TODO: Get comment lines starting with '@' (like `@param`) and turn them into nice properties

	std::string GetPrettyComments(std::vector<std::string> const& comments, bool first_paragraph_only = false)
	{
		if (comments.size() > 0)
		{
			auto joined = join(comments, "\n");
			if (first_paragraph_only)
			{
				if (auto end_of_par = joined.find("\n\n"); end_of_par != std::string::npos)
					joined.erase(joined.begin() + end_of_par, joined.end());
			}
			return format("<md>{}</md>", joined);
		}
		return {};
	}

	std::string GetPrettyComments(Declaration const& decl, bool first_paragraph_only = false)
	{
		return GetPrettyComments(decl.Comments, first_paragraph_only);
	}

	std::string GetPrettyComments(std::string comment, bool first_paragraph_only = false)
	{
		return GetPrettyComments(std::vector<std::string>{comment}, first_paragraph_only);
	}

	static std::string Escaped(std::string_view str)
	{
		return string_ops::replaced(std::string{str}, "<", "&lt;");
	}

	DocFile CreateCSSFile()
	{
		FileWriter out{};

		for (auto& ruleset : styles.items())
		{
			out.StartBlock("{} {{", (std::string)ruleset.key());
			for (auto& style : ruleset.value().items())
			{
				out.WriteLine("{}: {};", style.key(), (std::string)style.value());
			}
			out.EndBlock("}}");
		}

		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / "style.css", .Contents = out.Finish() };
	}

	DocFile CreateIndexFile()
	{
		HTMLFileWriter index{options};
		index.StartPage("Types");

		index.WriteLine("<h1>Types</h1>");
		index.WriteLine("<h2>Classes</h2>");

		index.StartTag("table", "class = 'decllist'");
		for (auto& klass : classes)
		{
			index.StartTag("tr");
			index.WriteLine("<td class='declnamecol'><a href='{0}.html' class='entitylink'><small class='namespace'>{1}</small>{2}</a></td>", klass->FullName("."), (klass->Namespace.empty() ? "" : (klass->Namespace + "::")), klass->Name);
			index.WriteLine("<td>{}</td>", GetPrettyComments(*klass, true));
			index.EndTag();
		}
		index.EndTag();

		index.WriteLine("<h2>Enums</h2>");

		index.StartBlock("<table class='decllist'>");
		for (auto& henum : enums)
		{
			index.StartTag("tr");
			index.WriteLine("<td class='declnamecol'><a href='{0}.html' class='entitylink'><small class='namespace'>{1}</small>{2}</a></td>", henum->FullName("."), (henum->Namespace.empty() ? "" : (henum->Namespace + "::")), henum->Name);
			index.WriteLine("<td>{}</td>", GetPrettyComments(*henum, true));
			index.EndTag();
		}
		index.EndBlock("</table>");

		index.EndPage();

		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / "Types.html", .Contents = index.Finish() };
	}

	template <typename T>
	void OutDeclDesc(HTMLFileWriter& out, T const& decl, TypeDeclaration const& parent)
	{
		if (!parent.Namespace.empty())
			out.WriteLine("<li><b>Namespace</b>: <a href='{}.html'>{}</a></li>", replaced(parent.Namespace, "::", "."), parent.Namespace);
		if (decl.DeclarationLine != 0)
			out.WriteLine("<li><b>Declaration</b>: <a href='file:///{2}#{1}' class='srclink'>{0} at line {1}</a></li>", parent.ParentMirror->SourceFilePath.filename().string(), decl.DeclarationLine, parent.ParentMirror->SourceFilePath.string());
		if (decl.Flags.bits)
			out.WriteLine("<li><b>Flags</b>: {}</li>", join(decl.Flags, ", ", [](auto v) { return std::format("<a href='Reflector.FieldFlags.html#{0}'>{0}</a>", magic_enum::enum_name(v)); }));
		if (!decl.Attributes.empty())
			out.WriteLine("<li><b>Attributes</b>: <code class='language-json'>{}</code></li>", decl.Attributes.dump());
	}

	void OutputAttributeDescriptors(HTMLFileWriter& out, Declaration const& decl)
	{
		std::map<std::string, std::string> attributes;
		for (auto attr : AttributeProperties::AllAttributes)
		{
			if (auto attr_name = attr->ExistsIn(decl); attr_name && attr->DocumentationDescriptionGenerator)
			{
				auto desc = attr->DocumentationDescriptionGenerator(attr->TryGet(decl), decl);
				if (!desc.empty())
					attributes[*attr_name] = move(desc);
			}
		}
		if (attributes.empty())
			return;
		out.WriteLine("<h2>Attributes</h2>");
		for (auto& [name, desc] : attributes)
		{
			out.WriteLine("<h3>{}</h3>", name);
			out.WriteLine("<md>{}</md>", desc);
		}
	}

	DocFile CreateClassFile(Class const& klass)
	{
		HTMLFileWriter out{options};
		out.StartPage(klass.Name + " Class");
		out.WriteLine("<h1><pre class='entityname class'>{}</pre> Class</h1>", klass.Name);
		out.WriteLine("<h2>Description</h2>");
		out.StartBlock("<ul class='desclist'>");
		OutDeclDesc(out, klass, klass);
		if (!klass.BaseClass.empty())
		{
			auto inhlist = klass.GetInheritanceList();// | std::views::reverse | std::ranges::to<std::vector>();
			if (inhlist.empty())
				out.WriteLine("<li><b>Inheritance</b>: <pre>{} : {}</pre></li>", klass.Name, Escaped(klass.BaseClass));
			else
				out.WriteLine("<li><b>Inheritance</b>: <pre>{} : {}</pre></li>", klass.Name, Escaped(join(inhlist, " -> ", [](Class const* klass) { return format("{}", klass->FullType()); })));
		}
		out.EndBlock("</ul>");

		out.WriteLine(GetPrettyComments(klass));

		auto documented_fields = klass.Fields | std::views::filter([](auto& field) { return !field->Flags.contain(FieldFlags::DeclaredPrivate); });
		if (!documented_fields.empty())
		{
			out.WriteLine("<h2>Fields</h2>");
			out.StartBlock("<table class='decllist'>");
			for (auto& field : documented_fields)
			{
				out.StartTag("tr");
				out.WriteLine("<td class='declnamecol'><a href='{}' class='entitylink'>{}</a> <small class='membertype'>{}</small></td>", FilenameFor(*field), field->CleanName, Escaped(field->Type));
				out.WriteLine("<td>{}</td>", GetPrettyComments(*field, true));
				out.EndTag();
			}
			out.EndBlock("</table>");
		}

		auto documented_methods = klass.Methods | std::views::filter([](auto& method) { return !method->Flags.contain(MethodFlags::Proxy); });
		if (!documented_fields.empty())
		{
			out.WriteLine("<h2>Methods</h2>");
			out.StartBlock("<table class='decllist'>");
			for (auto& method : documented_methods)
			{
				out.StartTag("tr");
				out.WriteLine("<td class='declnamecol'><small class='specifiers'>{}</small><a href='{}' class='entitylink'>{}({})<small class='specifiers'>{}</small></a> <small class='membertype'>-> {}</small></td>",
					FormatPreFlags(method->Flags),
					FilenameFor(*method),
					method->Name,
					Escaped(method->ParametersTypesOnly),
					FormatPostFlags(method->Flags),
					Escaped(method->ReturnType));
				out.WriteLine("<td>{}</td>", GetPrettyComments(*method, true));
				out.EndTag();
			}
			out.EndBlock("</table>");
		}

		OutputAttributeDescriptors(out, klass);

		out.EndPage();
		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / FilenameFor(klass), .Contents = out.Finish() };
	}

	DocFile CreateFieldFile(Field const& field)
	{
		HTMLFileWriter out{options};
		out.StartPage(field.ParentType->Name + "::" + field.Name + " Field");
		out.WriteLine("<h1><pre class='entityname field'>{}::{}</pre> Field</h1>", field.ParentType->Name, field.Name);

		auto& klass = *field.ParentType;
		out.WriteLine("<h2>Description</h2>");
		out.StartBlock("<ul class='desclist'>");
		OutDeclDesc(out, field, klass);
		if (field.DisplayName != field.Name)
			out.WriteLine("<li><b>Display Name</b>: '{}'</li>", field.DisplayName);
		out.EndBlock("</ul>");

		out.WriteLine("<code class='example language-cpp'>{}{} {}{};</code>",
			FormatPreFlags(field.Flags),
			Escaped(field.Type),
			field.Name,
			Escaped((field.InitializingExpression.empty() ? "" : format(" = {}", field.InitializingExpression)))
		);

		/// TODO: Special treatment for Flags fields

		out.WriteLine(GetPrettyComments(field));

		OutputAttributeDescriptors(out, field);

		if (field.AssociatedArtificialMethods.size() > 0)
		{
			out.WriteLine("<h2>See Also</h2>");
			for (auto& am : field.AssociatedArtificialMethods)
			{
				out.WriteLine("<li><a href='{}'><small>{}</small>::{}({})</a></li>", FilenameFor(*am), am->ParentType->FullType(), am->Name, Escaped(am->ParametersTypesOnly));
			}
		}

		out.EndPage();
		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / FilenameFor(field), .Contents = out.Finish() };
	}

	DocFile CreateMethodFile(Method const& method)
	{
		HTMLFileWriter out{options};
		out.StartPage(method.ParentType->Name + "::" + method.Name + " Method");
		out.WriteLine("<h1><pre class='entityname field'>{}::{}</pre> Method</h1>", method.ParentType->Name, method.Name);

		auto& klass = *method.ParentType;
		out.WriteLine("<h2>Description</h2>");
		out.StartBlock("<ul class='desclist'>");
		OutDeclDesc(out, method, klass);
		if (method.SourceDeclaration && method.SourceDeclaration != method.ParentType)
		{
			out.WriteLine("<li><b>Source Declaration</b>: {} <a href='{}'>{}</a></li>", method.SourceDeclaration->DeclarationType(), FilenameFor(*method.SourceDeclaration), method.SourceDeclaration->Name);
		}
		if (!method.UniqueName.empty())
			out.WriteLine("<li><b>Unique Name</b>: {}</li>", method.UniqueName);
		out.EndBlock("</ul>");

		out.WriteLine("<code class='example language-cpp'>{}{} {}({}){};</code>",
			FormatPreFlags(method.Flags),
			Escaped(method.ReturnType),
			method.Name,
			Escaped(method.GetParameters()),
			FormatPostFlags(method.Flags)
		);

		out.WriteLine(GetPrettyComments(method));

		if (method.ParametersSplit.size() > 0)
		{
			out.WriteLine("<h3>Parameters</h3>");
			out.StartTag("dl");
			for (auto& param : method.ParametersSplit)
			{
				out.WriteLine("<dt><pre class='paramname'>{}</pre> : <code class='language-cpp'>{}</code></dt><dd>{}</dd>", param.Name, param.Type, GetPrettyComments("*Undocumented*"));
			}
			out.EndTag();
		}

		if (method.ReturnType != "void")
		{
			out.WriteLine("<h3>Return Value</h3>");
			out.WriteLine("<code class='language-cpp'>{}</code>", method.ReturnType);
			out.WriteLine(GetPrettyComments("*Undocumented*"));
		}

		OutputAttributeDescriptors(out, method);

		out.EndPage();
		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / FilenameFor(method), .Contents = out.Finish() };
	}

	DocFile CreateEnumFile(Enum const& henum)
	{
		HTMLFileWriter out{options};

		out.StartPage(henum.Name + " Enum");
		out.WriteLine("<h1><pre class='entityname enum'>{}</pre> Enum</h1>", henum.Name);
		out.WriteLine("<h2>Description</h2>");
		out.StartBlock("<ul class='desclist'>");
		OutDeclDesc(out, henum, henum);
		out.EndBlock("</ul>");

		out.WriteLine(GetPrettyComments(henum));

		OutputAttributeDescriptors(out, henum);

		out.EndPage();
		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / FilenameFor(henum), .Contents = out.Finish()};
	}

	std::string FilenameFor(Declaration const& decl) { return decl.FullName(".") + ".html"; }

	std::vector<DocFile> Generate()
	{
		std::vector<DocFile> result;

		/// Load and create styles
		const json default_styles = ghassanpl::formats::json::load_file(options.GetExePath().parent_path() / "documentation_default_css.json");
		styles = default_styles;
		/// styles.update(options.Documentation.CustomStyles);

		/// Prepare the lists of types
		for (auto& mirror : GetMirrors())
		{
			for (auto& klass : mirror->Classes)
				classes.push_back(klass.get());
			for (auto& henum : mirror->Enums)
				enums.push_back(henum.get());
		}

		std::ranges::sort(classes, [](Class const* a, Class const* b) { return a->FullType() < b->FullType(); });
		std::ranges::sort(enums, [](Enum const* a, Enum const* b) { return a->FullType() < b->FullType(); });

		/// Create basic files
		result.push_back(CreateIndexFile());
		result.push_back(CreateCSSFile());

		for (auto& klass : classes)
		{
			result.push_back(CreateClassFile(*klass));

			for (auto& field : klass->Fields)
			{
				if (field->Flags.contain(FieldFlags::DeclaredPrivate))
					continue;

				result.push_back(CreateFieldFile(*field));
			}

			for (auto& method : klass->Methods)
			{
				if (method->Flags.contain(MethodFlags::Proxy))
					continue;

				result.push_back(CreateMethodFile(*method));
			}
		}
		
		for (auto& henum : enums)
		{
			result.push_back(CreateEnumFile(*henum));
		}

		return result;
	}
};

std::vector<DocFile> GenerateDocumentation(Options const& options)
{
	DocumentationGenerator gen{ options };
	return gen.Generate();
}

bool CreateDocFileArtifact(path const& target_path, path const& final_path, Options const& options, DocFile const& doc_file)
{
	FileWriter f(target_path);
	f.mOutFile->write(doc_file.Contents.data(), doc_file.Contents.size());
	f.Close();
	return true;
}
