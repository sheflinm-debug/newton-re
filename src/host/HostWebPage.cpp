/*
	File:		host/HostWebPage.cpp

	Contains:	A web page made Newton text.  See HostWebPage.h.  Written for
				this project from RFC 3986 (URLs), RFC 9112 (HTTP/1.1
				messages, chunked transfer) and the HTML specification's
				parsing rules as far as a reader view needs them.
*/

#include "HostWebPage.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

// ---- a growing string ----------------------------------------------------

struct Text
{
	char*	s;
	size_t	n;
	size_t	room;
};

void
Init(Text* t)
{
	t->room = 128;
	t->s = (char*) malloc(t->room);
	t->n = 0;
	t->s[0] = 0;
}

void
AddBytes(Text* t, const char* p, size_t n)
{
	if (t->n + n + 1 > t->room)
	{
		while (t->n + n + 1 > t->room)
			t->room *= 2;
		t->s = (char*) realloc(t->s, t->room);
	}
	memcpy(t->s + t->n, p, n);
	t->n += n;
	t->s[t->n] = 0;
}

void
Add(Text* t, const char* p)
{
	AddBytes(t, p, strlen(p));
}

void
AddChar(Text* t, char c)
{
	AddBytes(t, &c, 1);
}

void
AddUTF8(Text* t, unsigned long c)
{
	char b[4];
	if (c < 0x80)
		AddChar(t, (char) c);
	else if (c < 0x800)
	{
		b[0] = (char) (0xC0 | (c >> 6)); b[1] = (char) (0x80 | (c & 0x3F));
		AddBytes(t, b, 2);
	}
	else if (c < 0x10000)
	{
		b[0] = (char) (0xE0 | (c >> 12)); b[1] = (char) (0x80 | ((c >> 6) & 0x3F)); b[2] = (char) (0x80 | (c & 0x3F));
		AddBytes(t, b, 3);
	}
	else
	{
		b[0] = (char) (0xF0 | (c >> 18)); b[1] = (char) (0x80 | ((c >> 12) & 0x3F));
		b[2] = (char) (0x80 | ((c >> 6) & 0x3F)); b[3] = (char) (0x80 | (c & 0x3F));
		AddBytes(t, b, 4);
	}
}

char*
Copy(const char* s, size_t n)
{
	char* c = (char*) malloc(n + 1);
	memcpy(c, s, n);
	c[n] = 0;
	return c;
}

bool
SameNoCase(const char* a, const char* b, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i]) || a[i] == 0)
			return false;
	return true;
}

bool
ContainsNoCase(const char* hay, const char* needle)
{
	size_t n = strlen(needle);
	for (const char* p = hay; *p; p++)
		if (SameNoCase(p, needle, n))
			return true;
	return false;
}

// Windows-1252's 0x80-0x9F
const unsigned short kCP1252[32] = {
	0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
	0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };

// ---- the tree ------------------------------------------------------------

struct Node
{
	char	tag[16];		// lower case; "" a text node
	char*	text;			// a text node's (entities decoded), else NULL
	char*	href;
	char*	src;
	char*	alt;
	char*	classes;		// class and id together, for the unlikely test
	Node*	parent;
	Node**	children;
	int		childCount;
	int		childRoom;
	double	score;
	bool	scored;
	bool	skip;			// left out of the article
};

Node*
NewNode(const char* tag, Node* parent)
{
	Node* n = (Node*) calloc(1, sizeof(Node));
	snprintf(n->tag, sizeof(n->tag), "%s", tag);
	n->parent = parent;
	if (parent != NULL)
	{
		if (parent->childCount == parent->childRoom)
		{
			parent->childRoom = parent->childRoom * 2 + 4;
			parent->children = (Node**) realloc(parent->children, sizeof(Node*) * (size_t) parent->childRoom);
		}
		parent->children[parent->childCount++] = n;
	}
	return n;
}

void
FreeNode(Node* n)
{
	for (int i = 0; i < n->childCount; i++)
		FreeNode(n->children[i]);
	free(n->children);
	free(n->text);
	free(n->href);
	free(n->src);
	free(n->alt);
	free(n->classes);
	free(n);
}

bool
Is(const Node* n, const char* tag)
{
	return strcmp(n->tag, tag) == 0;
}

bool
OneOf(const char* tag, const char* const* list)
{
	for (; *list; list++)
		if (strcmp(tag, *list) == 0)
			return true;
	return false;
}

const char* const kVoid[] = { "br", "img", "hr", "meta", "link", "input", "wbr", "source", "area", "base", "col", "embed", "param", "track", NULL };
const char* const kRaw[] = { "script", "style", "textarea", "title", "noscript", "template", "svg", "math", NULL };
const char* const kBlock[] = { "p", "div", "section", "article", "main", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li",
	"pre", "blockquote", "table", "tr", "td", "th", "dl", "dt", "dd", "figure", "figcaption", "header", "footer", "nav", "aside",
	"form", "hr", "br", "body", "html", "center", "address", "details", "summary", NULL };
const char* const kLeftOut[] = { "script", "style", "nav", "header", "footer", "aside", "form", "noscript", "svg", "iframe",
	"button", "select", "textarea", "template", "math", "head", "input", "label", "menu", "dialog", NULL };
const char* const kUnlikely[] = { "comment", "sidebar", "footer", "masthead", "menu", "nav", "share", "social", "promo",
	"advert", "sponsor", "cookie", "banner", "related", "popup", "subscribe", "newsletter", "breadcrumb", "pagination", NULL };
const char* const kLikely[] = { "article", "content", "main", "body", "post", "story", "entry", "text", NULL };

// entities: the common named ones, and numbers
struct Entity { const char* name; unsigned short c; };
const Entity kEntities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
	{ "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 },
	{ "rdquo", 0x201D }, { "ldquo", 0x201C }, { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 }, { "middot", 0xB7 },
	{ "bull", 0x2022 }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "eacute", 0xE9 }, { "egrave", 0xE8 }, { "agrave", 0xE0 },
	{ "aacute", 0xE1 }, { "ccedil", 0xE7 }, { "uuml", 0xFC }, { "ouml", 0xF6 }, { "auml", 0xE4 }, { "szlig", 0xDF },
	{ "euro", 0x20AC }, { "pound", 0xA3 }, { "deg", 0xB0 }, { "times", 0xD7 }, { "shy", 0xAD }, { "zwj", 0x200D } };

void
DecodeEntities(Text* out, const char* p, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		if (p[i] == '&')
		{
			size_t j = i + 1;
			while (j < n && j - i < 12 && (isalnum((unsigned char) p[j]) || p[j] == '#'))
				j++;
			size_t len = j - i - 1;
			unsigned long c = 0;
			bool found = false;
			if (len > 1 && p[i + 1] == '#')
			{
				c = (p[i + 2] == 'x' || p[i + 2] == 'X') ? strtoul(p + i + 3, NULL, 16) : strtoul(p + i + 2, NULL, 10);
				found = c > 0;
			}
			else
				for (size_t k = 0; k < sizeof(kEntities) / sizeof(kEntities[0]) && !found; k++)
					if (strlen(kEntities[k].name) == len && strncmp(p + i + 1, kEntities[k].name, len) == 0)
					{
						c = kEntities[k].c;
						found = true;
					}
			if (found)
			{
				if (c != 0xAD && c != 0x200D)		// (a soft hyphen, a joiner: nothing to show)
					AddUTF8(out, c == 0xA0 ? ' ' : c);
				i = (j < n && p[j] == ';') ? j : j - 1;
				continue;
			}
		}
		AddChar(out, p[i]);
	}
}

// an attribute's value in a start tag (malloc'd, entities decoded), NULL when absent
char*
Attribute(const char* tag, size_t n, const char* name)
{
	size_t nl = strlen(name);
	for (size_t i = 1; i + nl < n; i++)
	{
		if (!isspace((unsigned char) tag[i - 1]))
			continue;
		if (!SameNoCase(tag + i, name, nl))
			continue;
		size_t j = i + nl;
		while (j < n && isspace((unsigned char) tag[j]))
			j++;
		if (j >= n || tag[j] != '=')
			continue;
		j++;
		while (j < n && isspace((unsigned char) tag[j]))
			j++;
		size_t start, end;
		if (j < n && (tag[j] == '"' || tag[j] == '\''))
		{
			char q = tag[j];
			start = j + 1;
			end = start;
			while (end < n && tag[end] != q)
				end++;
		}
		else
		{
			start = j;
			end = j;
			while (end < n && !isspace((unsigned char) tag[end]) && tag[end] != '>')
				end++;
		}
		Text v;
		Init(&v);
		DecodeEntities(&v, tag + start, end - start);
		return v.s;
	}
	return NULL;
}

// the open element of a tag nearest the top, NULL when none
Node*
Open(Node* current, const char* tag)
{
	for (Node* n = current; n != NULL; n = n->parent)
		if (Is(n, tag))
			return n;
	return NULL;
}

Node*
Parse(const char* html, size_t size)
{
	Node* root = NewNode("#root", NULL);
	Node* current = root;
	size_t i = 0;
	while (i < size)
	{
		if (html[i] == '<')
		{
			if (i + 3 < size && strncmp(html + i, "<!--", 4) == 0)
			{
				const char* end = strstr(html + i + 4, "-->");
				i = end != NULL ? (size_t) (end - html) + 3 : size;
				continue;
			}
			if (i + 1 < size && (html[i + 1] == '!' || html[i + 1] == '?'))
			{
				const char* end = (const char*) memchr(html + i, '>', size - i);
				i = end != NULL ? (size_t) (end - html) + 1 : size;
				continue;
			}
			bool closing = i + 1 < size && html[i + 1] == '/';
			size_t nameStart = i + (closing ? 2 : 1);
			if (nameStart < size && isalpha((unsigned char) html[nameStart]))
			{
				size_t nameEnd = nameStart;
				while (nameEnd < size && (isalnum((unsigned char) html[nameEnd]) || html[nameEnd] == '-'))
					nameEnd++;
				char tag[16];
				size_t tl = nameEnd - nameStart < sizeof(tag) - 1 ? nameEnd - nameStart : sizeof(tag) - 1;
				for (size_t k = 0; k < tl; k++)
					tag[k] = (char) tolower((unsigned char) html[nameStart + k]);
				tag[tl] = 0;
				// the tag's end (quotes may hold a '>')
				size_t end = nameEnd;
				char quote = 0;
				while (end < size && (quote != 0 || html[end] != '>'))
				{
					if (quote == 0 && (html[end] == '"' || html[end] == '\''))
						quote = html[end];
					else if (quote != 0 && html[end] == quote)
						quote = 0;
					end++;
				}
				const char* whole = html + i;
				size_t wholeSize = end - i;
				i = end < size ? end + 1 : size;
				if (closing)
				{
					Node* open = Open(current, tag);
					if (open != NULL && open != root)
						current = open->parent;
					continue;
				}
				// implied ends: a block closes an open paragraph; an item the
				// open item; a row the open row; a cell the open cell
				if (OneOf(tag, kBlock) && !Is(current, "li") && Open(current, "p") != NULL && strcmp(tag, "br") != 0)
				{
					Node* p = Open(current, "p");
					current = p->parent;
				}
				if (strcmp(tag, "li") == 0)
				{
					Node* li = Open(current, "li");
					if (li != NULL && (li->parent == current || li == current))
						current = li->parent;
				}
				if (strcmp(tag, "tr") == 0 || strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0)
				{
					const char* same = strcmp(tag, "tr") == 0 ? "tr" : (Open(current, "td") != NULL ? "td" : "th");
					Node* open = Open(current, same);
					if (open != NULL && Open(current, "table") != NULL)
						current = open->parent;
				}
				Node* node = NewNode(tag, current);
				node->href = Attribute(whole, wholeSize, "href");
				node->src = Attribute(whole, wholeSize, "src");
				node->alt = Attribute(whole, wholeSize, "alt");
				char* cls = Attribute(whole, wholeSize, "class");
				char* id = Attribute(whole, wholeSize, "id");
				char* role = Attribute(whole, wholeSize, "role");
				Text both;
				Init(&both);
				if (cls) Add(&both, cls);
				AddChar(&both, ' ');
				if (id) Add(&both, id);
				AddChar(&both, ' ');
				if (role) Add(&both, role);
				node->classes = both.s;
				free(cls);
				free(id);
				free(role);
				bool selfClosing = wholeSize > 0 && whole[wholeSize - 1] == '/';
				if (OneOf(tag, kRaw))
				{
					// its text up to its end tag, as it is
					char endTag[24];
					snprintf(endTag, sizeof(endTag), "</%s", tag);
					size_t j = i;
					while (j < size && !(html[j] == '<' && SameNoCase(html + j, endTag, strlen(endTag))))
						j++;
					if (strcmp(tag, "title") == 0)
					{
						Node* t = NewNode("", node);
						Text decoded;
						Init(&decoded);
						DecodeEntities(&decoded, html + i, j - i);
						t->text = decoded.s;
					}
					const char* close = j < size ? (const char*) memchr(html + j, '>', size - j) : NULL;
					i = close != NULL ? (size_t) (close - html) + 1 : size;
				}
				else if (!OneOf(tag, kVoid) && !selfClosing)
					current = node;
				continue;
			}
		}
		// text up to the next tag
		size_t j = i + 1;
		while (j < size && html[j] != '<')
			j++;
		Node* t = NewNode("", current);
		Text decoded;
		Init(&decoded);
		DecodeEntities(&decoded, html + i, j - i);
		t->text = decoded.s;
		i = j;
	}
	return root;
}

Node*
Find(Node* n, const char* tag)
{
	if (Is(n, tag))
		return n;
	for (int i = 0; i < n->childCount; i++)
	{
		Node* found = Find(n->children[i], tag);
		if (found != NULL)
			return found;
	}
	return NULL;
}

void
InnerText(const Node* n, Text* out)
{
	if (n->text != NULL)
		Add(out, n->text);
	for (int i = 0; i < n->childCount; i++)
		if (!n->children[i]->skip)
			InnerText(n->children[i], out);
}

// what a reader view leaves out
void
MarkLeftOut(Node* n)
{
	if (n->tag[0] != 0)
	{
		bool unlikely = false;
		if (n->classes != NULL)
			for (const char* const* u = kUnlikely; *u && !unlikely; u++)
				if (ContainsNoCase(n->classes, *u))
				{
					unlikely = true;
					for (const char* const* l = kLikely; *l; l++)
						if (ContainsNoCase(n->classes, *l))
							unlikely = false;
				}
		if (OneOf(n->tag, kLeftOut) || (unlikely && !Is(n, "body") && !Is(n, "html") && !Is(n, "article") && !Is(n, "main")))
		{
			n->skip = true;
			return;
		}
	}
	for (int i = 0; i < n->childCount; i++)
		MarkLeftOut(n->children[i]);
}

// each paragraph's score given to its parent, half to its grandparent
void
Score(Node* n)
{
	if (n->skip)
		return;
	if (Is(n, "p") || Is(n, "pre") || Is(n, "cell") || Is(n, "blockquote") || Is(n, "li"))
	{
		Text t;
		Init(&t);
		InnerText(n, &t);
		size_t length = 0;
		int commas = 0;
		for (size_t i = 0; i < t.n; i++)
		{
			if (!isspace((unsigned char) t.s[i]))
				length++;
			if (t.s[i] == ',')
				commas++;
		}
		free(t.s);
		if (length >= 25)
		{
			double score = 1 + commas + (length / 100 < 3 ? (double) (length / 100) : 3.0);
			if (n->parent != NULL)
			{
				n->parent->score += score;
				n->parent->scored = true;
				if (n->parent->parent != NULL)
				{
					n->parent->parent->score += score / 2;
					n->parent->parent->scored = true;
				}
			}
		}
	}
	for (int i = 0; i < n->childCount; i++)
		Score(n->children[i]);
}

Node*
Best(Node* n, Node* best)
{
	if (n->skip)
		return best;
	if (n->scored && n->tag[0] != 0)
	{
		double bonus = Is(n, "article") || Is(n, "main") ? 1.25 : 1.0;
		if (best == NULL || n->score * bonus > best->score * (Is(best, "article") || Is(best, "main") ? 1.25 : 1.0))
			best = n;
	}
	for (int i = 0; i < n->childCount; i++)
		best = Best(n->children[i], best);
	return best;
}

// ---- writing it out -----------------------------------------------------

struct Writer
{
	HostWebPage*	page;
	int				blockRoom;
	int				linkRoom;
	Text			current;
	HostWebBlockKind	kind;
	int				level;
	const char*		base;
	bool			pre;
};

void
Flush(Writer* w)
{
	// trimmed; nothing for an empty block
	size_t a = 0, b = w->current.n;
	while (a < b && (w->current.s[a] == ' ' || w->current.s[a] == '\n'))
		a++;
	while (b > a && (w->current.s[b - 1] == ' ' || w->current.s[b - 1] == '\n'))
		b--;
	if (b > a && w->page->blockCount < 4000)
	{
		if (w->page->blockCount == w->blockRoom)
		{
			w->blockRoom = w->blockRoom * 2 + 16;
			w->page->blocks = (HostWebBlock*) realloc(w->page->blocks, sizeof(HostWebBlock) * (size_t) w->blockRoom);
		}
		HostWebBlock* block = &w->page->blocks[w->page->blockCount++];
		block->kind = w->kind;
		block->level = w->level;
		block->text = Copy(w->current.s + a, b - a);
	}
	w->current.n = 0;
	w->current.s[0] = 0;
	w->kind = kWebParagraph;
	w->level = 0;
}

void
AddInline(Writer* w, const char* text)
{
	if (w->pre)
	{
		Add(&w->current, text);
		return;
	}
	// white space collapsed
	for (const char* p = text; *p; p++)
	{
		bool space = *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '\f';
		if (space)
		{
			if (w->current.n > 0 && w->current.s[w->current.n - 1] != ' ' && w->current.s[w->current.n - 1] != '\n')
				AddChar(&w->current, ' ');
		}
		else
			AddChar(&w->current, *p);
	}
}

int
AddLink(Writer* w, const char* href)
{
	char* url = HostWebResolve(w->base, href);
	if (url == NULL)
		return 0;
	for (int i = 0; i < w->page->linkCount; i++)
		if (strcmp(w->page->links[i], url) == 0)
		{
			free(url);
			return i + 1;
		}
	if (w->page->linkCount == w->linkRoom)
	{
		w->linkRoom = w->linkRoom * 2 + 16;
		w->page->links = (char**) realloc(w->page->links, sizeof(char*) * (size_t) w->linkRoom);
	}
	w->page->links[w->page->linkCount++] = url;
	return w->page->linkCount;
}

void
Write(Writer* w, const Node* n)
{
	if (n->skip)
		return;
	if (n->text != NULL)
	{
		AddInline(w, n->text);
		return;
	}
	const char* tag = n->tag;
	if (Is(n, "title") || Is(n, "head"))
		return;
	if (Is(n, "br"))
	{
		AddChar(&w->current, '\n');
		return;
	}
	if (Is(n, "img"))
	{
		AddInline(w, " [picture");
		if (n->alt != NULL && n->alt[0] != 0)
		{
			AddInline(w, ": ");
			AddInline(w, n->alt);
		}
		AddInline(w, "] ");
		return;
	}
	bool block = OneOf(tag, kBlock);
	if (block)
	{
		Flush(w);
		if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == 0)
		{
			w->kind = kWebHeading;
			w->level = tag[1] - '0';
		}
		else if (Is(n, "li") || Is(n, "dt"))
			w->kind = kWebItem;
		else if (Is(n, "blockquote"))
			w->kind = kWebQuote;
		else if (Is(n, "pre"))
		{
			w->kind = kWebPre;
			w->pre = true;
		}
	}
	for (int i = 0; i < n->childCount; i++)
	{
		Write(w, n->children[i]);
		// a table's cells side by side
		if (Is(n->children[i], "cell") && i + 1 < n->childCount)
			AddInline(w, " | ");
	}
	if (Is(n, "a") && n->href != NULL)
	{
		int number = AddLink(w, n->href);
		if (number > 0)
		{
			char mark[16];
			snprintf(mark, sizeof(mark), " [%d]", number);
			Add(&w->current, mark);
		}
	}
	if (block)
	{
		HostWebBlockKind kind = w->kind;
		Flush(w);
		if (kind == kWebPre)
			w->pre = false;
	}
}

// (a row's cells kept together on one line, " | " between them: a cell is
// made an inline "cell", which Score counts as a paragraph)
void
JoinCells(Node* n)
{
	for (int i = 0; i < n->childCount; i++)
		JoinCells(n->children[i]);
	if (Is(n, "td") || Is(n, "th"))
		strcpy(n->tag, "cell");
}

} // namespace


// ---- URLs ---------------------------------------------------------------

bool
HostWebParseURL(const char* url, HostWebURL* parts)
{
	const char* rest;
	if (SameNoCase(url, "http://", 7))
	{
		parts->tls = false;
		parts->port = 80;
		rest = url + 7;
	}
	else if (SameNoCase(url, "https://", 8))
	{
		parts->tls = true;
		parts->port = 443;
		rest = url + 8;
	}
	else
		return false;
	const char* hostEnd = rest;
	while (*hostEnd && *hostEnd != ':' && *hostEnd != '/' && *hostEnd != '?' && *hostEnd != '#')
		hostEnd++;
	// (credentials "user@" are not taken)
	if (hostEnd == rest || (size_t) (hostEnd - rest) >= sizeof(parts->host) || memchr(rest, '@', (size_t) (hostEnd - rest)) != NULL)
		return false;
	for (size_t i = 0; i < (size_t) (hostEnd - rest); i++)
		parts->host[i] = (char) tolower((unsigned char) rest[i]);
	parts->host[hostEnd - rest] = 0;
	const char* p = hostEnd;
	if (*p == ':')
	{
		char* end;
		long port = strtol(p + 1, &end, 10);
		if (end == p + 1 || port <= 0 || port > 65535)
			return false;
		parts->port = (int) port;
		p = end;
	}
	const char* fragment = strchr(p, '#');
	size_t pathLength = fragment != NULL ? (size_t) (fragment - p) : strlen(p);
	if (pathLength + 2 >= sizeof(parts->path))
		return false;
	size_t o = 0;
	if (pathLength == 0 || p[0] != '/')
		parts->path[o++] = '/';
	memcpy(parts->path + o, p, pathLength);
	parts->path[o + pathLength] = 0;
	return true;
}

char*
HostWebResolve(const char* base, const char* reference)
{
	while (*reference == ' ' || *reference == '\t' || *reference == '\n' || *reference == '\r')
		reference++;
	if (*reference == 0 || *reference == '#')
		return NULL;
	if (SameNoCase(reference, "http://", 7) || SameNoCase(reference, "https://", 8))
	{
		HostWebURL check;
		return HostWebParseURL(reference, &check) ? Copy(reference, strlen(reference)) : NULL;
	}
	// another scheme (javascript:, mailto:, tel:, data:): not a page
	for (const char* p = reference; *p && *p != '/' && *p != '?' && *p != '#'; p++)
		if (*p == ':')
			return NULL;
	HostWebURL b;
	if (base == NULL || !HostWebParseURL(base, &b))
		return NULL;
	Text out;
	Init(&out);
	Add(&out, b.tls ? "https://" : "http://");
	Add(&out, b.host);
	if (b.port != (b.tls ? 443 : 80))
	{
		char port[16];
		snprintf(port, sizeof(port), ":%d", b.port);
		Add(&out, port);
	}
	if (reference[0] == '/' && reference[1] == '/')
	{
		free(out.s);
		Text whole;
		Init(&whole);
		Add(&whole, b.tls ? "https:" : "http:");
		Add(&whole, reference);
		return whole.s;
	}
	// the path: the reference's, the base's with a query, or merged
	Text path;
	Init(&path);
	if (reference[0] == '/')
		Add(&path, reference);
	else if (reference[0] == '?')
	{
		const char* q = strchr(b.path, '?');
		AddBytes(&path, b.path, q != NULL ? (size_t) (q - b.path) : strlen(b.path));
		Add(&path, reference);
	}
	else
	{
		const char* q = strchr(b.path, '?');
		size_t bl = q != NULL ? (size_t) (q - b.path) : strlen(b.path);
		const char* slash = NULL;
		for (size_t i = 0; i < bl; i++)
			if (b.path[i] == '/')
				slash = b.path + i;
		AddBytes(&path, b.path, slash != NULL ? (size_t) (slash - b.path) + 1 : 1);
		Add(&path, reference);
	}
	// its fragment off; "." and ".." taken out
	char* hash = strchr(path.s, '#');
	if (hash != NULL)
		*hash = 0;
	char* query = strchr(path.s, '?');
	Text clean;
	Init(&clean);
	size_t segmentsEnd = query != NULL ? (size_t) (query - path.s) : strlen(path.s);
	// (clean is "/seg/seg": ".." takes the last "/seg" off, "." nothing;
	// a path ending in "/", ".", or ".." keeps its slash)
	size_t i = 0;
	bool slashAtEnd = false;
	while (i < segmentsEnd)
	{
		size_t j = i + 1;
		while (j < segmentsEnd && path.s[j] != '/')
			j++;
		size_t len = j - i - 1;
		const char* seg = path.s + i + 1;
		bool last = j >= segmentsEnd;
		if (len == 1 && seg[0] == '.')
			slashAtEnd = last;
		else if (len == 2 && seg[0] == '.' && seg[1] == '.')
		{
			while (clean.n > 0 && clean.s[clean.n - 1] != '/')
				clean.n--;
			if (clean.n > 0)
				clean.n--;
			clean.s[clean.n] = 0;
			slashAtEnd = last;
		}
		else
		{
			AddChar(&clean, '/');
			AddBytes(&clean, seg, len);
			slashAtEnd = false;
		}
		i = j;
	}
	if (slashAtEnd)
		AddChar(&clean, '/');
	if (clean.n == 0)
		AddChar(&clean, '/');
	if (query != NULL)
		Add(&clean, query);
	Add(&out, clean.s);
	free(clean.s);
	free(path.s);
	return out.s;
}

char*
HostWebAddress(const char* typed, const char* searchURL)
{
	while (*typed == ' ')
		typed++;
	size_t n = strlen(typed);
	while (n > 0 && typed[n - 1] == ' ')
		n--;
	char* t = Copy(typed, n);
	HostWebURL check;
	if (HostWebParseURL(t, &check))
		return t;
	// "example.org/x": a dot, no space, a letter after the last dot of the host
	bool spaced = strchr(t, ' ') != NULL;
	const char* hostEnd = t;
	while (*hostEnd && *hostEnd != '/' && *hostEnd != ':' && *hostEnd != '?')
		hostEnd++;
	const char* dot = NULL;
	for (const char* p = t; p < hostEnd; p++)
		if (*p == '.')
			dot = p;
	// (or a host that is a number - 127.0.0.1 - or localhost, or one with a port)
	bool numeric = dot != NULL;
	for (const char* p = t; p < hostEnd && numeric; p++)
		if (!isdigit((unsigned char) *p) && *p != '.')
			numeric = false;
	bool local = (size_t) (hostEnd - t) == 9 && strncmp(t, "localhost", 9) == 0;
	bool port = *hostEnd == ':' && isdigit((unsigned char) hostEnd[1]) && hostEnd > t;
	if (!spaced && ((dot != NULL && dot + 1 < hostEnd && isalpha((unsigned char) dot[1])) || numeric || local || port))
	{
		Text u;
		Init(&u);
		Add(&u, "http://");
		Add(&u, t);
		free(t);
		return u.s;
	}
	// a search: the words escaped into the engine's URL
	Text words;
	Init(&words);
	for (const char* p = t; *p; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			AddChar(&words, (char) c);
		else if (c == ' ')
			AddChar(&words, '+');
		else
		{
			char hex[4];
			snprintf(hex, sizeof(hex), "%%%02X", c);
			Add(&words, hex);
		}
	}
	free(t);
	Text u;
	Init(&u);
	const char* s = strstr(searchURL, "%s");
	if (s != NULL)
	{
		AddBytes(&u, searchURL, (size_t) (s - searchURL));
		Add(&u, words.s);
		Add(&u, s + 2);
	}
	else
	{
		Add(&u, searchURL);
		Add(&u, words.s);
	}
	free(words.s);
	return u.s;
}

// ---- HTTP ---------------------------------------------------------------

bool
HostWebParseResponse(const char* data, size_t size, HostWebResponse* out)
{
	memset(out, 0, sizeof(*out));
	if (size < 12 || strncmp(data, "HTTP/", 5) != 0)
		return false;
	const char* sp = (const char*) memchr(data, ' ', size);
	if (sp == NULL)
		return false;
	out->status = atoi(sp + 1);
	// the head's end
	const char* headEnd = NULL;
	size_t bodyStart = size;
	for (size_t i = 0; i + 1 < size; i++)
	{
		if (data[i] == '\n' && data[i + 1] == '\n')
		{
			headEnd = data + i;
			bodyStart = i + 2;
			break;
		}
		if (data[i] == '\n' && i + 2 < size && data[i + 1] == '\r' && data[i + 2] == '\n')
		{
			headEnd = data + i;
			bodyStart = i + 3;
			break;
		}
	}
	if (headEnd == NULL)
		headEnd = data + size;
	bool chunked = false;
	long contentLength = -1;
	for (const char* line = data; line < headEnd; )
	{
		const char* lineEnd = (const char*) memchr(line, '\n', (size_t) (headEnd - line));
		if (lineEnd == NULL)
			lineEnd = headEnd;
		const char* colon = (const char*) memchr(line, ':', (size_t) (lineEnd - line));
		if (colon != NULL)
		{
			size_t nameLength = (size_t) (colon - line);
			const char* v = colon + 1;
			while (v < lineEnd && *v == ' ')
				v++;
			size_t vl = (size_t) (lineEnd - v);
			while (vl > 0 && (v[vl - 1] == '\r' || v[vl - 1] == ' '))
				vl--;
			if (nameLength == 12 && SameNoCase(line, "Content-Type", 12))
			{
				size_t k = 0;
				while (k < vl && v[k] != ';' && v[k] != ' ' && k + 1 < sizeof(out->contentType))
				{
					out->contentType[k] = (char) tolower((unsigned char) v[k]);
					k++;
				}
				out->contentType[k] = 0;
				for (size_t m = 0; m + 8 < vl; m++)
					if (SameNoCase(v + m, "charset=", 8))
					{
						size_t c = 0;
						const char* cs = v + m + 8;
						if (*cs == '"')
							cs++;
						while (cs < v + vl && *cs != ';' && *cs != '"' && *cs != ' ' && c + 1 < sizeof(out->charset))
							out->charset[c++] = (char) tolower((unsigned char) *cs++);
						out->charset[c] = 0;
						break;
					}
			}
			else if (nameLength == 8 && SameNoCase(line, "Location", 8))
			{
				size_t c = vl < sizeof(out->location) - 1 ? vl : sizeof(out->location) - 1;
				memcpy(out->location, v, c);
				out->location[c] = 0;
			}
			else if (nameLength == 17 && SameNoCase(line, "Transfer-Encoding", 17))
			{
				for (size_t m = 0; m + 7 <= vl; m++)
					if (SameNoCase(v + m, "chunked", 7))
						chunked = true;
			}
			else if (nameLength == 14 && SameNoCase(line, "Content-Length", 14))
				contentLength = atol(v);
		}
		line = lineEnd + 1;
	}
	const char* body = data + bodyStart;
	size_t bodySize = bodyStart <= size ? size - bodyStart : 0;
	Text b;
	Init(&b);
	if (chunked)
	{
		size_t i = 0;
		while (i < bodySize)
		{
			char* end;
			unsigned long chunk = strtoul(body + i, &end, 16);
			const char* lineEnd = (const char*) memchr(body + i, '\n', bodySize - i);
			if (lineEnd == NULL || end == body + i)
				break;
			i = (size_t) (lineEnd - body) + 1;
			if (chunk == 0)
				break;
			if (i + chunk > bodySize)
				chunk = bodySize - i;
			AddBytes(&b, body + i, chunk);
			i += chunk;
			if (i < bodySize && body[i] == '\r')
				i++;
			if (i < bodySize && body[i] == '\n')
				i++;
		}
	}
	else
		AddBytes(&b, body, contentLength >= 0 && (size_t) contentLength < bodySize ? (size_t) contentLength : bodySize);
	out->body = b.s;
	out->bodySize = b.n;
	return true;
}

void
HostWebFreeResponse(HostWebResponse* response)
{
	free(response->body);
	response->body = NULL;
}

// ---- pages --------------------------------------------------------------

int
HostWebMakePage(const char* body, size_t size, const char* charset, bool plainText, const char* url, HostWebPage* out)
{
	memset(out, 0, sizeof(*out));
	// the charset: the header's, else the page's <meta>, else UTF-8
	char cs[48] = "";
	if (charset != NULL)
		snprintf(cs, sizeof(cs), "%s", charset);
	if (cs[0] == 0 && !plainText)
	{
		size_t look = size < 4096 ? size : 4096;
		for (size_t i = 0; i + 8 < look; i++)
			if (SameNoCase(body + i, "charset=", 8))
			{
				size_t c = 0;
				const char* p = body + i + 8;
				while (*p == '"' || *p == '\'')
					p++;
				while (p < body + look && (isalnum((unsigned char) *p) || *p == '-' || *p == '_') && c + 1 < sizeof(cs))
					cs[c++] = (char) tolower((unsigned char) *p++);
				cs[c] = 0;
				break;
			}
	}
	bool latin = strncmp(cs, "iso-8859", 8) == 0 || strcmp(cs, "windows-1252") == 0 || strcmp(cs, "latin1") == 0
		|| strcmp(cs, "us-ascii") == 0;
	Text utf8;
	Init(&utf8);
	if (latin)
		for (size_t i = 0; i < size; i++)
		{
			unsigned c = (unsigned char) body[i];
			AddUTF8(&utf8, c >= 0x80 && c < 0xA0 ? kCP1252[c - 0x80] : c);
		}
	else
		AddBytes(&utf8, body, size);

	Writer w;
	memset(&w, 0, sizeof(w));
	w.page = out;
	Init(&w.current);
	w.kind = kWebParagraph;
	w.base = url;
	if (plainText)
	{
		w.kind = kWebPre;
		w.pre = true;
		AddInline(&w, utf8.s);
		Flush(&w);
		out->title = Copy("", 0);
	}
	else
	{
		Node* root = Parse(utf8.s, utf8.n);
		// <base href> moves the base
		Node* baseNode = Find(root, "base");
		char* baseURL = baseNode != NULL && baseNode->href != NULL ? HostWebResolve(url, baseNode->href) : NULL;
		if (baseURL != NULL)
			w.base = baseURL;
		Node* title = Find(root, "title");
		Text t;
		Init(&t);
		if (title != NULL)
			InnerText(title, &t);
		JoinCells(root);
		MarkLeftOut(root);
		Score(root);
		Node* best = Best(root, NULL);
		Node* body = Find(root, "body");
		if (body == NULL)
			body = root;
		// too little scored: the whole body
		if (best != NULL)
		{
			Text bt;
			Init(&bt);
			InnerText(best, &bt);
			if (bt.n < 140)
				best = NULL;
			free(bt.s);
		}
		if (best == NULL)
			Write(&w, body);
		else
		{
			// the best, and its siblings that score near it
			Node* parent = best->parent;
			for (int i = 0; parent != NULL && i < parent->childCount; i++)
			{
				Node* sibling = parent->children[i];
				if (sibling == best || (sibling->scored && sibling->score >= best->score * 0.2 && !sibling->skip))
					Write(&w, sibling);
			}
			if (parent == NULL)
				Write(&w, best);
		}
		Flush(&w);
		// (the title: <title>, else the first heading)
		size_t a = 0, b = t.n;
		while (a < b && isspace((unsigned char) t.s[a])) a++;
		while (b > a && isspace((unsigned char) t.s[b - 1])) b--;
		if (b == a)
			for (int i = 0; i < out->blockCount; i++)
				if (out->blocks[i].kind == kWebHeading)
				{
					free(t.s);
					Init(&t);
					Add(&t, out->blocks[i].text);
					a = 0;
					b = t.n;
					break;
				}
		out->title = Copy(t.s + a, b - a);
		free(t.s);
		free(baseURL);
		FreeNode(root);
	}
	free(w.current.s);
	free(utf8.s);
	return 0;
}

void
HostWebFreePage(HostWebPage* page)
{
	for (int i = 0; i < page->blockCount; i++)
		free(page->blocks[i].text);
	free(page->blocks);
	for (int i = 0; i < page->linkCount; i++)
		free(page->links[i]);
	free(page->links);
	free(page->title);
	memset(page, 0, sizeof(*page));
}
