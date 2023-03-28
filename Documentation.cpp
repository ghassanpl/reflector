#include "Documentation.h"
#include "FileWriter.h"
#include "Options.h"
#include <ghassanpl/json_helpers.h>
#include <set>
#include <magic_enum_format.hpp>

/// TODO: Documentation options
/// - FileLinkResolve - a structure that somehow turns filenames into github (or other) links
/// - PageTitleSuffix
/// - AdditionalStyles - updated into default_styles
/// - AdditionalHeadTags - added to StartPage <head> tag
/// - Language - language tag (e.g. "en") to add to StartPage <html>
/// - DocumentationDirectory - relative to artifact directory or options.json file?
/// - TypeAliases - e.g "ImageResolvable" -> "Resolvable<Image>", or smth
/// - AdditionalTypes - enables highlighting of additional types

struct HTMLFileWriter : public FileWriter
{
	using FileWriter::FileWriter;

	void StartPage(std::string_view title)
	{
		WriteLine("<!doctype html>");
		StartTag("html");

		StartTag("head");
		WriteLine("<title>{}</title>", title);
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
	document.querySelectorAll('code.example').forEach((el) => {
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
	std::map<void const*, path> file_for_mirror;
	std::vector<Class const*> classes;
	std::vector<Enum const*> enums;

	std::string GetPrettyComments(Declaration const& decl, bool first_paragraph_only = false)
	{
		if (decl.Comments.size() > 0)
		{
			auto joined = join(decl.Comments, "\n");
			if (first_paragraph_only)
			{
				if (auto end_of_par = joined.find("\n\n"); end_of_par != std::string::npos)
					joined.erase(joined.begin() + end_of_par, joined.end());
			}
			return format("<md>{}</md>", joined);
		}
		return {};
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
		HTMLFileWriter index{};
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
	void OutDeclDesc(HTMLFileWriter& out, T const& decl, Class const& parent)
	{
		if (!parent.Namespace.empty())
			out.WriteLine("<li><b>Namespace</b>: <a href='{}.html'>{}</a></li>", replaced(parent.Namespace, "::", "."), parent.Namespace);
		if (decl.DeclarationLine != 0)
			out.WriteLine("<li><b>Declaration</b>: <a href='{2}@{1}' class='srclink'>{0}@{1}</a></li>", file_for_mirror[&parent].filename().string(), decl.DeclarationLine, file_for_mirror[&parent].string());
		if (decl.Flags.bits)
			out.WriteLine("<li><b>Flags</b>: {}</li>", join(decl.Flags, ", ", [](auto v) { return std::format("<a href='Reflector.FieldFlags.html#{0}'>{0}</a>", magic_enum::enum_name(v)); }));
		if (!decl.Attributes.empty())
			out.WriteLine("<li><b>Attributes</b>: <code>{}</code></li>", decl.Attributes.dump());
	}

	DocFile CreateClassFile(Class const& klass)
	{
		HTMLFileWriter out{};
		out.StartPage(klass.Name + " Class");
		out.WriteLine("<h1><code class='entityname class'>{}</code> Class</h1>", klass.Name);
		out.WriteLine("<h2>Description</h2>");
		out.StartBlock("<ul class='desclist'>");
		OutDeclDesc(out, klass, klass);
		if (!klass.BaseClass.empty())
		{
			auto inhlist = klass.GetInheritanceList();// | std::views::reverse | std::ranges::to<std::vector>();
			if (inhlist.empty())
				out.WriteLine("<li><b>Inheritance</b>: <code>{} : {}</code></li>", klass.Name, Escaped(klass.BaseClass));
			else
				out.WriteLine("<li><b>Inheritance</b>: <code>{} : {}</code></li>", klass.Name, Escaped(join(inhlist, " -> ", [](Class const* klass) { return format("{}", klass->FullType()); })));
		}
		out.EndBlock("</ul>");

		out.WriteLine(GetPrettyComments(klass));

		out.WriteLine("<h2>Fields</h2>");
		out.StartBlock("<table class='decllist'>");
		for (auto& field : klass.Fields)
		{
			if (field->Flags.contain(FieldFlags::DeclaredPrivate))
				continue;

			out.StartTag("tr");
			out.WriteLine("<td class='declnamecol'><a href='{}' class='entitylink'>{}</a> <small class='membertype'>{}</small></td>", FilenameFor(*field), field->CleanName, Escaped(field->Type));
			out.WriteLine("<td>{}</td>", GetPrettyComments(*field, true));
			out.EndTag();
		}
		out.EndBlock("</table>");

		out.WriteLine("<h2>Methods</h2>");
		out.StartBlock("<table class='decllist'>");
		for (auto& method : klass.Methods)
		{
			if (method->Flags.contain(MethodFlags::Proxy))
				continue;

			out.StartTag("tr");
			out.WriteLine("<td class='declnamecol'><a href='{}' class='entitylink'>{}({})</a> <small class='membertype'>-> {}</small></td>", FilenameFor(*method), method->Name, Escaped(method->ParametersTypesOnly), Escaped(method->ReturnType));
			out.WriteLine("<td>{}</td>", GetPrettyComments(*method, true));
			out.EndTag();
		}
		out.EndBlock("</table>");

		out.EndPage();
		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / FilenameFor(klass), .Contents = out.Finish() };
	}

	DocFile CreateFieldFile(Field const& field)
	{
		HTMLFileWriter out{};
		out.StartPage(field.ParentClass->Name + "::" + field.Name + " Field");
		out.WriteLine("<h1><code class='entityname field'>{}::{}</code> Field</h1>", field.ParentClass->Name, field.Name);

		auto& klass = *field.ParentClass;
		out.WriteLine("<h2>Description</h2>");
		out.StartBlock("<ul class='desclist'>");
		OutDeclDesc(out, field, klass);
		if (field.DisplayName != field.Name)
			out.WriteLine("<li><b>Display Name</b>: '{}'</li>", field.DisplayName);
		out.EndBlock("</ul>");

		out.WriteLine("<pre><code class='example language-cpp'>{}{} {}{};</code></pre>",
			FormatPreFlags(field.Flags),
			Escaped(field.Type),
			field.Name,
			Escaped((field.InitializingExpression.empty() ? "" : format(" = {}", field.InitializingExpression)))
		);

		/// TODO: Special treatment for Flags fields

		out.WriteLine(GetPrettyComments(field));

		out.WriteLine("<h2>See Also</h2>");
		for (auto& am : field.AssociatedArtificialMethods)
		{
			out.WriteLine("<li><a href='{}'><small>{}</small>::{}({})</a></li>", FilenameFor(*am), am->ParentClass->FullType(), am->Name, Escaped(am->ParametersTypesOnly));
		}

		out.EndPage();
		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / FilenameFor(field), .Contents = out.Finish() };
	}

	DocFile CreateMethodFile(Method const& method)
	{
		HTMLFileWriter out{};
		out.StartPage(method.ParentClass->Name + "::" + method.Name + " Method");
		out.WriteLine("<h1><code class='entityname field'>{}::{}</code> Method</h1>", method.ParentClass->Name, method.Name);

		auto& klass = *method.ParentClass;
		out.WriteLine("<h2>Description</h2>");
		out.StartBlock("<ul class='desclist'>");
		OutDeclDesc(out, method, klass);
		if (method.SourceDeclaration && method.SourceDeclaration != method.ParentClass)
		{
			out.WriteLine("<li><b>Source Declaration</b>: {} <a href='{}'>{}</a></li>", method.SourceDeclaration->DeclarationType(), FilenameFor(*method.SourceDeclaration), method.SourceDeclaration->Name);
		}
		if (!method.UniqueName.empty())
			out.WriteLine("<li><b>Unique Name</b>: {}</li>", method.UniqueName);
		out.EndBlock("</ul>");

		out.WriteLine("<pre><code class='example language-cpp'>{}{} {}({}){};</code></pre>",
			FormatPreFlags(method.Flags),
			Escaped(method.ReturnType),
			method.Name,
			Escaped(method.GetParameters()),
			FormatPostFlags(method.Flags)
		);

		out.WriteLine(GetPrettyComments(method));

		for (auto& param : method.ParametersSplit)
		{

		}

		out.EndPage();
		return DocFile{ .TargetPath = options.ArtifactPath / "Documentation" / FilenameFor(method), .Contents = out.Finish() };
	}

	DocFile CreateEnumFile(Enum const& henum)
	{
		HTMLFileWriter out{};
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
		for (auto& [SourceFilePath, Classes, Enums] : GetMirrors())
		{
			for (auto& klass : Classes)
			{
				classes.push_back(klass.get());
				file_for_mirror[klass.get()] = SourceFilePath;
			}
			for (auto& henum : Enums)
			{
				enums.push_back(henum.get());
				file_for_mirror[henum.get()] = SourceFilePath;
			}
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
