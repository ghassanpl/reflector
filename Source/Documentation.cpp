/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Documentation.h"
#include "FileWriter.h"
#include "Options.h"
#include "Declarations.h"
#include <ghassanpl/json_helpers.h>
#include <ghassanpl/parsing.h>
#include <ghassanpl/containers.h>

using namespace ghassanpl::parsing;

std::string HighlightTypes(std::string_view type, TypeDeclaration const* search_context);

struct HTMLFileWriter : FileWriter
{
	using FileWriter::FileWriter;

	void StartPage(std::string_view title, Declaration const* decl = nullptr)
	{
		WriteLine("<!doctype html>");
		StartTag("html", "lang='{}'", mOptions.Documentation.Language);

		StartTag("head");
		WriteLine("<title>{}{}</title>", title, mOptions.Documentation.PageTitleSuffix);
		WriteLine(R"(<link rel="stylesheet" href="style.css" />)");
		WriteLine(R"(<link rel="stylesheet" href="https://microsoft.github.io/vscode-codicons/dist/codicon.css" />)");
		WriteLine(R"(<script src="https://cdn.jsdelivr.net/gh/MarketingPipeline/Markdown-Tag/markdown-tag.js"></script>)");
		WriteLine(R"(<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.7.0/styles/vs2015.min.css"><script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.7.0/highlight.min.js"></script>)");
		WriteLine("{}", mOptions.Documentation.AdditionalHeadTags);
		EndTag();

		StartTag("body");

		if (decl)
		{
			WriteLine("<div class='breadcrumbs' id='breadcrumbs'>");
			std::vector<std::string> breadcrumbs;
			breadcrumbs.emplace_back("<a href='Types.html'>Types</a>");
			if (const auto typedecl = dynamic_cast<TypeDeclaration const*>(decl))
			{
				if (!typedecl->Namespace.empty())
					breadcrumbs.push_back(format(R"({}<a href='Namespace.{}.html'>{}</a>)", IconFor(DeclarationType::Namespace), replaced(typedecl->Namespace, "::", "."), typedecl->Namespace));
			}
			else if (const auto memdecl = dynamic_cast<BaseMemberDeclaration const*>(decl))
			{
				breadcrumbs.push_back(memdecl->ParentDecl()->MakeLink(LinkFlag::DeclarationType));
			}
			breadcrumbs.push_back(decl->MakeLink(LinkFlag::DeclarationType));
			WriteLine("{}", join(breadcrumbs, " / "));
			WriteLine("</div>");
		}
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
		StartBlock("<{} {}>", tag_name, std::vformat(fmt, std::make_format_args(args...)));
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
	Options const& mOptions;
	json styles;
	std::vector<Class const*> classes;
	std::vector<Enum const*> enums;

	explicit DocumentationGenerator(Options const& options)
		: mOptions(options)
	{
		/// Load and create styles
		const json default_styles = formats::json::try_load_file(mOptions.GetExePath().parent_path() / "documentation_default_css.json");
		styles = default_styles;
		styles.update(mOptions.Documentation.AdditionalStyles, true);

		/// Prepare the lists of types
		for (const auto& mirror : GetMirrors())
		{
			for (const auto& klass : mirror->Classes)
			{
				if (klass->Document())
					classes.push_back(klass.get());
			}
			for (const auto& henum : mirror->Enums)
			{
				if (henum->Document())
					enums.push_back(henum.get());
			}
		}

		std::ranges::sort(classes, [](Class const* a, Class const* b) { return a->FullType() < b->FullType(); });
		std::ranges::sort(enums, [](Enum const* a, Enum const* b) { return a->FullType() < b->FullType(); });
	}

	struct ParsedComments
	{
		std::string Brief;
		std::string Description;
		std::map<std::string, std::string, std::less<>> Parameters;
		std::string ReturnTypeDescription;
		std::vector<std::string> SeeAlsos;
		std::string Section;
	};

	/// @param or `param_name:`
	/// @return
	/// @see
	/// @warn - e.g @warn deprecated
	/// @group
	/// @default <value> - for when a field is initialized in the constructor, but we still want to document its default value
	/// @throws <exception> <when>

	/// TODO: Get comment lines starting with '@' (like `@param`) and turn them into nice properties

	std::string GetPrettyComments(SimpleDeclaration const& decl, bool for_inline_list = false) const
	{
		std::vector<std::string> comment_lines;

		if (for_inline_list)
		{
			std::string doc_notes_str;
			/// Add in-list docnotes
			for (const auto& [Header, Contents, ShowInMemberList, Icon] : decl.DocNotes)
			{
				if (
					(ShowInMemberList && !mOptions.Documentation.IgnoreDocNotes.contains(Header))
					|| mOptions.Documentation.InlineDocNotes.contains(Header)
					)
				{
					doc_notes_str += "<span class='docnote'>";
					//result += format("<md>{}**{}**</md>", Icon(note.Icon), note.Header);
					doc_notes_str += format("{}<b>{}</b>", ::Icon(Icon), Header);
					doc_notes_str += "</span>";
				}
			}
			if (!doc_notes_str.empty())
				comment_lines.push_back(std::move(doc_notes_str));
		}

		/// Add comments from entity comments (first paragraph only, if requested)
		if (!decl.Comments.empty())
		{
			if (for_inline_list)
			{
				const auto empty_comment_line = std::ranges::find_if(decl.Comments, [](auto& cmntline) { return trimmed_whitespace(cmntline).empty(); });
				comment_lines.insert(comment_lines.end(), decl.Comments.begin(), empty_comment_line);
			}
			else
			{
				for (auto& line: decl.NonDirectiveCommentLines())
					comment_lines.push_back(line);
			}
		}

		if (!comment_lines.empty())
			return format("<md>{}</md>", join(comment_lines, "\n"));
		return {};
	}

	bool CreateCSSFile(ArtifactArgs const& args) const
	{
		FileWriter out{ args };

		for (auto& ruleset : styles.items())
		{
			out.StartBlock("{} {{", (std::string)ruleset.key());
			for (auto& style : ruleset.value().items())
			{
				out.WriteLine("{}: {};", style.key(), (std::string)style.value());
			}
			out.EndBlock("}}");
		}

		return true;
	}

	bool CreateIndexFile(ArtifactArgs const& args) const
	{
		HTMLFileWriter index{ args };
		index.StartPage("Types");

		index.WriteLine("<h1>Types</h1>");
		index.WriteLine("<h2>Classes</h2>");

		index.StartTag("table", "class = 'decllist'");
		for (auto& klass : classes)
		{
			index.StartTag("tr");
			index.WriteLine("<td class='declnamecol'>{}</td>", klass->MakeLink(LinkFlags::all()));
			index.WriteLine("<td>{}</td>", GetPrettyComments(*klass, true));
			index.EndTag();
		}
		index.EndTag();

		index.WriteLine("<h2>Enums</h2>");

		index.StartBlock("<table class='decllist'>");
		for (auto& henum : enums)
		{
			index.StartTag("tr");
			index.WriteLine("<td class='declnamecol'>{}</td>", henum->MakeLink(LinkFlags::all()));
			index.WriteLine("<td>{}</td>", GetPrettyComments(*henum, true));
			index.EndTag();
		}
		index.EndBlock("</table>");

		index.EndPage();

		return true;
	}

	template <typename T>
	void OutDeclDesc(HTMLFileWriter& out, T const& decl, TypeDeclaration const& parent) const
	{
		out.WriteLine("<h2>Details</h2>");

		out.StartBlock("<ul class='desclist'>");

		if (!parent.Namespace.empty())
			out.WriteLine("<li><b>Namespace</b>: <a href='{}.html'>{}</a></li>", replaced(parent.Namespace, "::", "."), parent.Namespace);
		if (decl.DeclarationLine != 0)
			out.WriteLine("<li><b>Declaration</b>: <a href='file:///{2}#{1}' class='srclink'>{0} at line {1}</a></li>", parent.ParentMirror->SourceFilePath.filename().string(), decl.DeclarationLine, parent.ParentMirror->SourceFilePath.string());
		if (decl.Flags.bits)
			out.WriteLine("<li><b>Flags</b>: {}</li>", join(decl.Flags, ", ", [](auto v) { return std::format("<a href='Reflector.FieldFlags.html#{0}'>{0}</a>", magic_enum::enum_name(v)); }));
		if (!decl.Attributes.empty())
			out.WriteLine("<li><b>Attributes</b>: <code class='language-json'>{}</code></li>", decl.Attributes.dump());

		if constexpr (std::same_as<T, Class>)
		{
			if (!decl.BaseClass.empty())
			{
				if (const auto inhlist = decl.GetInheritanceList(); inhlist.empty())
					out.WriteLine("<li><b>Inheritance</b>: <pre>{} : {}</pre></li>", decl.Name, HighlightTypes(decl.BaseClass, &decl));
				else
				{
					auto hierarchy = join(inhlist, " : ", [&decl](Class const* klass) { return HighlightTypes(klass->FullType(), &decl); });
					if (!inhlist.back()->BaseClass.empty())
					{
						hierarchy += " : ";
						hierarchy += HighlightTypes(inhlist.back()->BaseClass, &decl);
					}
					out.WriteLine("<li><b>Inheritance</b>: <pre>{} : {}</pre></li>", decl.Name, hierarchy);
				}
			}
		}
		else if constexpr (std::same_as<T, Method>)
		{
			if (decl.SourceDeclaration && decl.SourceDeclaration != decl.ParentType)
			{
				out.WriteLine("<li><b>Source Declaration</b>: {} {}</li>", magic_enum::enum_name(decl.SourceDeclaration->DeclarationType()), decl.SourceDeclaration->MakeLink());
			}

			if (!decl.UniqueName.empty())
				out.WriteLine("<li><b>Unique Name</b>: {}</li>", decl.UniqueName);
		}
		else if constexpr (std::same_as<T, Field>)
		{
			if (decl.DisplayName != decl.Name)
				out.WriteLine("<li><b>Display Name</b>: '{}'</li>", decl.DisplayName);
		}

		out.EndBlock("</ul>");
	}

	static void OutputAttributeDescriptors(HTMLFileWriter& out, Declaration const& decl)
	{
		std::string note_str;

		for (auto& [name, desc, _, icon] : decl.DocNotes)
		{
			if (out.mOptions.Documentation.IgnoreDocNotes.contains(name))
				continue;

			note_str += std::format("<h3>{}{}</h3><md>{}</md>", Icon(icon), name, desc);
		}

		if (note_str.empty())
			return;

		/// TODO: Maybe make an option that determines whether we create a <details> or just a simple header
		out.WriteLine("<h2>Notes</h2>");
		out.WriteLine("{}", note_str);
	}

	bool CreateClassFile(ArtifactArgs const& args, Class const& klass) const
	{
		HTMLFileWriter out{ args };
		out.StartPage(klass.Name + " Class", &klass);
		out.WriteLine("<h1><pre class='entityname class'>{}</pre> Class</h1>", klass.Name);
		out.WriteLine("<h2>Description</h2>");

		out.WriteLine("{}", GetPrettyComments(klass));

		OutputAttributeDescriptors(out, klass);

		auto documented_fields = klass.Fields | std::views::filter([](auto& field) { 
			return field->Document() &&
				field->Access == AccessMode::Public; 
		});
		if (!documented_fields.empty())
		{
			out.WriteLine("<h2>Fields</h2>");
			out.StartBlock("<table class='decllist'>");
			for (auto& field : documented_fields)
			{
				out.StartTag("tr");
				out.WriteLine("<td class='fieldtype'>{}</td>", HighlightTypes(field->Type, field->ParentType));
				out.WriteLine("<td class='declnamecol'>{}</td>", field->MakeLink(LinkFlags::all() - LinkFlags{ LinkFlag::Parent, LinkFlag::ReturnType }));
				if (args.Options.Documentation.ShowFieldInitialValues)
					out.WriteLine("<td class='code'><code class='language-cpp'>{}</code></td>", Escaped(field->InitializingExpression));
				out.WriteLine("<td>{}</td>", GetPrettyComments(*field, true));
				out.EndTag();
			}
			out.EndBlock("</table>");
		}

		auto documented_methods = klass.Methods | std::views::filter([](auto& method) { 
			return method->Document() &&
				!method->Flags.contain(MethodFlags::ForFlag); /// Don't list flag methods by default
		});
		if (!documented_methods.empty())
		{
			out.WriteLine("<h2>Methods</h2>");
			out.StartBlock("<table class='decllist'>");
			for (const auto& method : documented_methods)
			{
				out.StartTag("tr");
				out.WriteLine("<td class='fieldtype'>{}</td>", HighlightTypes(method->Return.Name, method->ParentType));
				out.WriteLine("<td class='declnamecol'>{}</td>", method->MakeLink(LinkFlags::all() - LinkFlags{ LinkFlag::Parent, LinkFlag::ReturnType }));
				out.WriteLine("<td>{}</td>", GetPrettyComments(*method, true));
				out.EndTag();
			}
			out.EndBlock("</table>");
		}

		if (auto& documented_flags = klass.ClassDeclaredFlags; !documented_flags.empty())
		{
			out.WriteLine("<h2>Flags</h2>");
			out.StartBlock("<table class='decllist'>");

			Enum const* parent_enum = nullptr;
			for (const auto& [Name, SourceField, Represents, GeneratedArtificialMethods] : documented_flags)
			{
				/// Print a row for each parent type
				if (Represents->Parent() != parent_enum)
				{
					parent_enum = Represents->Parent();

					out.StartTag("tr", "class='parenttyperow'");
					out.WriteLine("<td colspan='2'>From {} via {} field:</td>", parent_enum->MakeLink(LinkFlags::all()), SourceField->MakeLink());
					out.EndTag();
				}

				out.StartTag("tr");
				out.WriteLine("<td class='declnamecol classflag'>{}</td>", Represents->MakeLink(LinkFlags::all() - LinkFlag::Parent));
				out.WriteLine("<td>{}</td>", GetPrettyComments(*Represents, true));
				out.EndTag();
			}
			out.EndBlock("</table>");
		}

		auto documented_private_fields = klass.Fields | std::views::filter([](auto& field) { 
			return field->Document() &&
				field->Access != AccessMode::Public; 
		});
		if (!documented_private_fields.empty())
		{
			out.WriteLine("<h2>Private Fields</h2>");
			out.StartBlock("<table class='decllist'>");
			for (auto& field : documented_private_fields)
			{
				out.StartTag("tr");
				out.WriteLine("<td class='fieldtype'>{}</td>", HighlightTypes(field->Type, field->ParentType));
				out.WriteLine("<td class='declnamecol'>{}</td>", field->MakeLink(LinkFlags::all() - LinkFlags{ LinkFlag::Parent, LinkFlag::ReturnType }));
				if (args.Options.Documentation.ShowFieldInitialValues)
					out.WriteLine("<td class='code'><code class='language-cpp'>{}</code></td>", Escaped(field->InitializingExpression));
				out.WriteLine("<td>{}</td>", GetPrettyComments(*field, true));
				out.EndTag();
			}
			out.EndBlock("</table>");
		}

		OutDeclDesc(out, klass, klass);

		out.EndPage();
		return true;
	}

	void OutputArtificialMethods(HTMLFileWriter& out, Declaration const& decl) const
	{
		auto documented_artificial_methods = decl.AssociatedArtificialMethods | std::views::filter([](auto& method) { 
			return method.second->Document();
		});
		if (!documented_artificial_methods.empty())
		{
			out.WriteLine("<h2>See Also</h2>");
			for (const auto& am : documented_artificial_methods | std::views::values)
			{
				out.WriteLine("<li>{}</li>", am->MakeLink(LinkFlags::all() - LinkFlag::DeclarationType));
			}
		}
	}

	bool CreateFieldFile(ArtifactArgs const& args, Field const& field) const
	{
		HTMLFileWriter out{ args };
		out.StartPage(field.ParentType->Name + "::" + field.Name + " Field", &field);
		out.WriteLine("<h1><pre class='entityname field'>{}::{}</pre> Field</h1>", field.ParentType->Name, field.Name);

		out.WriteLine("<code class='example language-cpp'>{}{} {}{};</code>",
			FormatPreFlags(field.Flags),
			Escaped(field.Type),
			field.Name,
			Escaped((field.InitializingExpression.empty() ? "" : format(" = {}", field.InitializingExpression)))
		);

		out.WriteLine("{}", GetPrettyComments(field));

		out.WriteLine("<h2>Type</h2>");
		out.WriteLine("<pre class='membertype'>{}</pre>", HighlightTypes(field.Type, field.ParentType));

		/// TODO: Special treatment for Flags fields
		/// ? Like what?

		OutputAttributeDescriptors(out, field);

		OutputArtificialMethods(out, field);

		OutDeclDesc(out, field, *field.ParentType);

		out.EndPage();
		return true;
	}

	bool CreateMethodFile(ArtifactArgs const& args, Method const& method) const
	{
		HTMLFileWriter out{ args };
		out.StartPage(method.ParentType->Name + "::" + method.Name + " Method", &method);
		out.WriteLine("<h1><pre class='entityname field'>{}::{}</pre> Method</h1>", method.ParentType->Name, method.Name);

		out.WriteLine("<code class='example language-cpp'>{}{} {}({}){};</code>",
			FormatPreFlags(method.Flags),
			Escaped(method.Return.Name),
			method.Name,
			Escaped(method.GetParameters()),
			FormatPostFlags(method.Flags)
		);

		out.WriteLine("{}", GetPrettyComments(method));

		std::map<std::string, std::string> param_comments;
		method.ForEachCommentDirective("param", [&](std::span<const std::string> param_paragraph) {
			if (param_paragraph.empty()) return;

			std::string_view first_line = param_paragraph[0];
			eat(first_line, "@param");
			const auto param_name = eat_identifier(first_line);
			auto& comments_for_param = param_comments[std::string{param_name}];
			comments_for_param += first_line;
			comments_for_param += '\n';
			comments_for_param += join(param_paragraph.subspan(1), "\n");
		});

		if (!method.ParametersSplit.empty())
		{
			out.WriteLine("<h2>Parameters</h2>");
			out.StartTag("dl");
			for (auto& param : method.ParametersSplit)
			{
				const auto comments = map_find(param_comments, param.Name);
				out.WriteLine("<dt><pre class='paramname'>{}</pre> : <code class='language-cpp'>{} {}</code></dt><dd><md>{}</md></dd>", param.Name, HighlightTypes(param.Type, method.ParentType), Escaped(param.Initializer), comments ? *comments : "");
			}
			out.EndTag();
		}

		if (method.Return.Name != "void")
		{
			out.WriteLine("<h2>Return Value</h2>");
			out.WriteLine("<pre class='membertype'>{}</pre>", HighlightTypes(method.Return.Name, method.ParentType));
			out.WriteLine("{}", GetPrettyComments(method.Return));
		}

		OutputAttributeDescriptors(out, method);

		OutputArtificialMethods(out, method);

		if (!method.ArtificialBody.empty())
		{
			out.WriteLine("<h2>Body</h2>");
			out.WriteLine("<code class='example language-cpp'>{}</code>", Escaped(method.ArtificialBody));

		}

		OutDeclDesc(out, method, *method.ParentType);

		out.EndPage();
		return true;
	}

	bool CreateEnumFile(ArtifactArgs const& args, Enum const& henum) const
	{
		HTMLFileWriter out{ args };

		out.StartPage(henum.Name + " Enum", &henum);
		out.WriteLine("<h1><pre class='entityname enum'>{}</pre> Enum</h1>", henum.Name);

		out.WriteLine("<h2>Description</h2>");
		out.WriteLine("{}", GetPrettyComments(henum));

		if (!henum.Enumerators.empty())
		{
			out.WriteLine("<h2>Enumerators</h2>");
			out.StartBlock("<table class='decllist'>");
			const bool enum_is_trivial = henum.IsTrivial();
			for (auto& enumerator : henum.Enumerators)
			{
				out.StartTag("tr");
				out.WriteLine("<td class='enumnamecol'>{}</td>", enumerator->Document() ? enumerator->MakeLink() : enumerator->Name);
				out.WriteLine("<td class='enumvalcol{}'>= {}</td>", (enum_is_trivial?" trivial":""), enumerator->Value);
				out.WriteLine("<td>{}</td>", GetPrettyComments(*enumerator, true));
				out.EndTag();
			}
			out.EndBlock("</table>");
		}

		OutputAttributeDescriptors(out, henum);

		OutputArtificialMethods(out, henum);

		OutDeclDesc(out, henum, henum);

		out.EndPage();
		return true;
	}

	bool CreateEnumeratorFile(ArtifactArgs const& args, Enumerator const& enumerator) const
	{
		HTMLFileWriter out{ args };

		auto name = format("{}::{}", enumerator.ParentType->Name, enumerator.Name);

		out.StartPage(name + " Enumerator", &enumerator);
		out.WriteLine("<h1><pre class='entityname enum'>{}</pre> Enumerator</h1>", name);

		out.WriteLine("<h2>Description</h2>");
		out.WriteLine("{}", GetPrettyComments(enumerator));

		OutputAttributeDescriptors(out, enumerator);

		OutputArtificialMethods(out, enumerator);

		OutDeclDesc(out, enumerator, *enumerator.ParentType);

		out.EndPage();
		return true;
	}

	static std::string FilenameFor(Declaration const& decl) { return decl.FullName(".") + ".html"; }

	void QueueArtifacts(Artifactory& factory) const
	{
		/// Create basic files
		factory.QueueArtifact(mOptions.ArtifactPath / mOptions.Documentation.TargetDirectory / "Types.html", std::bind_front(&DocumentationGenerator::CreateIndexFile, this));
		factory.QueueArtifact(mOptions.ArtifactPath / mOptions.Documentation.TargetDirectory / "style.css", std::bind_front(&DocumentationGenerator::CreateCSSFile, this));

		for (auto& klass : classes)
		{
			factory.QueueArtifact(mOptions.ArtifactPath / mOptions.Documentation.TargetDirectory / FilenameFor(*klass), std::bind_front(&DocumentationGenerator::CreateClassFile, this), std::ref(*klass));

			for (auto& field : klass->Fields)
			{
				if (!field->Document())
					continue;

				factory.QueueArtifact(mOptions.ArtifactPath / mOptions.Documentation.TargetDirectory / FilenameFor(*field), std::bind_front(&DocumentationGenerator::CreateFieldFile, this), std::ref(*field)); /// 
			}

			for (auto& method : klass->Methods)
			{
				if (!method->Document())
					continue;

				factory.QueueArtifact(mOptions.ArtifactPath / mOptions.Documentation.TargetDirectory / FilenameFor(*method), std::bind_front(&DocumentationGenerator::CreateMethodFile, this), std::ref(*method)); /// 
			}
		}

		for (auto& henum : enums)
		{
			factory.QueueArtifact(mOptions.ArtifactPath / mOptions.Documentation.TargetDirectory / FilenameFor(*henum), std::bind_front(&DocumentationGenerator::CreateEnumFile, this), std::ref(*henum)); ///
			
			for (auto& enumerator : henum->Enumerators)
			{
				if (!enumerator->Document())
					continue;

				factory.QueueArtifact(mOptions.ArtifactPath / mOptions.Documentation.TargetDirectory / FilenameFor(*enumerator), std::bind_front(&DocumentationGenerator::CreateEnumeratorFile, this), std::ref(*enumerator)); /// 
			}
		}
	}
};

size_t GenerateDocumentation(Artifactory& factory, Options const& options)
{
	const DocumentationGenerator gen{options};
	gen.QueueArtifacts(factory);
	return factory.Wait();
}
