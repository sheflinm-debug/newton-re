/*
	File:		host/HostMailFormat.cpp

	Contains:	Mail messages made and read.  See HostMailFormat.h.
				Written for this project from RFC 5322 (messages), RFC 2045
				(MIME, quoted-printable, base64), RFC 2046 (multipart) and
				RFC 2047 (encoded words).
*/

#include "HostMailFormat.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

// a growing string
struct Text
{
	char*	s;
	size_t	n;
	size_t	room;
};

void
Init(Text* t)
{
	t->room = 256;
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

// a Unicode character as UTF-8
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

bool
IsASCII(const char* s)
{
	for (; *s; s++)
		if ((unsigned char) *s >= 0x80)
			return false;
	return true;
}

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int
Base64Value(char c)
{
	const char* p = strchr(kBase64, c);
	return c != 0 && p != NULL ? (int) (p - kBase64) : -1;
}

bool
SameNoCase(const char* a, const char* b, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i]) || a[i] == 0)
			return false;
	return true;
}

// Windows-1252's 0x80-0x9F (the rest of the high half is ISO 8859-1)
const unsigned short kCP1252[32] = {
	0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
	0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };

// bytes in a character set made UTF-8
void
AddInCharset(Text* t, const char* p, size_t n, const char* charset)
{
	bool latin = charset != NULL && (SameNoCase(charset, "iso-8859-1", 10) || SameNoCase(charset, "latin1", 6)
		|| SameNoCase(charset, "windows-1252", 12) || SameNoCase(charset, "cp1252", 6) || SameNoCase(charset, "iso-8859-15", 11));
	if (!latin)
	{
		AddBytes(t, p, n);
		return;
	}
	for (size_t i = 0; i < n; i++)
	{
		unsigned c = (unsigned char) p[i];
		AddUTF8(t, c >= 0x80 && c < 0xA0 ? kCP1252[c - 0x80] : c);
	}
}

void
QPDecode(Text* out, const char* p, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		if (p[i] == '=' && i + 1 < n && (p[i + 1] == '\r' || p[i + 1] == '\n'))
		{
			// a soft line break
			i++;
			if (p[i] == '\r' && i + 1 < n && p[i + 1] == '\n')
				i++;
		}
		else if (p[i] == '=' && i + 2 < n && isxdigit((unsigned char) p[i + 1]) && isxdigit((unsigned char) p[i + 2]))
		{
			char hex[3] = { p[i + 1], p[i + 2], 0 };
			AddChar(out, (char) strtol(hex, NULL, 16));
			i += 2;
		}
		else
			AddChar(out, p[i]);
	}
}

// RFC 2047's encoded words in a header made UTF-8
void
DecodeWords(Text* out, const char* p, size_t n)
{
	size_t i = 0;
	bool lastWasWord = false;
	while (i < n)
	{
		if (p[i] == '=' && i + 1 < n && p[i + 1] == '?')
		{
			// =?charset?B|Q?text?=
			const char* end = p + n;
			const char* cs = p + i + 2;
			const char* q1 = (const char*) memchr(cs, '?', (size_t) (end - cs));
			const char* q2 = q1 != NULL && q1 + 2 < end ? (q1[2] == '?' ? q1 + 2 : NULL) : NULL;
			const char* close = NULL;
			if (q2 != NULL)
				for (const char* s = q2 + 1; s + 1 < end; s++)
					if (s[0] == '?' && s[1] == '=')
					{
						close = s;
						break;
					}
			if (close != NULL)
			{
				char charset[40];
				size_t cn = (size_t) (q1 - cs) < sizeof(charset) - 1 ? (size_t) (q1 - cs) : sizeof(charset) - 1;
				memcpy(charset, cs, cn);
				charset[cn] = 0;
				char how = (char) toupper((unsigned char) q1[1]);
				const char* body = q2 + 1;
				size_t bn = (size_t) (close - body);
				Text raw;
				Init(&raw);
				if (how == 'B')
				{
					size_t size;
					unsigned char* bytes = HostMailBase64Decode(body, bn, &size);
					AddBytes(&raw, (const char*) bytes, size);
					free(bytes);
				}
				else
				{
					for (size_t k = 0; k < bn; k++)
					{
						if (body[k] == '_')
							AddChar(&raw, ' ');
						else if (body[k] == '=' && k + 2 < bn)
						{
							char hex[3] = { body[k + 1], body[k + 2], 0 };
							AddChar(&raw, (char) strtol(hex, NULL, 16));
							k += 2;
						}
						else
							AddChar(&raw, body[k]);
					}
				}
				AddInCharset(out, raw.s, raw.n, charset);
				free(raw.s);
				i = (size_t) (close + 2 - p);
				lastWasWord = true;
				continue;
			}
		}
		// (white space between two encoded words is dropped)
		if (lastWasWord && (p[i] == ' ' || p[i] == '\t'))
		{
			size_t j = i;
			while (j < n && (p[j] == ' ' || p[j] == '\t'))
				j++;
			if (j + 1 < n && p[j] == '=' && p[j + 1] == '?')
			{
				i = j;
				continue;
			}
		}
		lastWasWord = false;
		AddChar(out, p[i]);
		i++;
	}
}

// the headers of a part: [start, end) of the unfolded block, the body after
struct Part
{
	const char*	headers;
	size_t		headerSize;
	const char*	body;
	size_t		bodySize;
};

void
SplitPart(const char* p, size_t n, Part* part)
{
	part->headers = p;
	part->headerSize = n;
	part->body = p + n;
	part->bodySize = 0;
	for (size_t i = 0; i < n; i++)
	{
		// (a blank line: CRLF CRLF or LF LF)
		if (p[i] == '\n' && i + 1 < n && (p[i + 1] == '\n' || (p[i + 1] == '\r' && i + 2 < n && p[i + 2] == '\n')))
		{
			part->headerSize = i + 1;
			size_t start = i + 1 + (p[i + 1] == '\r' ? 2 : 1);
			part->body = p + start;
			part->bodySize = n - start;
			return;
		}
		if (i == 0 && (p[0] == '\n' || (p[0] == '\r' && n > 1 && p[1] == '\n')))
		{
			part->headerSize = 0;
			size_t start = p[0] == '\r' ? 2 : 1;
			part->body = p + start;
			part->bodySize = n - start;
			return;
		}
	}
}

// a header's value, unfolded (malloc'd), NULL when the part has none
char*
Header(const Part* part, const char* name)
{
	size_t nameLength = strlen(name);
	const char* p = part->headers;
	const char* end = p + part->headerSize;
	while (p < end)
	{
		const char* lineEnd = p;
		while (lineEnd < end && *lineEnd != '\n')
			lineEnd++;
		if ((size_t) (lineEnd - p) > nameLength && p[nameLength] == ':' && SameNoCase(p, name, nameLength))
		{
			Text value;
			Init(&value);
			const char* v = p + nameLength + 1;
			for (;;)
			{
				const char* e = lineEnd;
				if (e > v && e[-1] == '\r')
					e--;
				AddBytes(&value, v, (size_t) (e - v));
				// a continuation line starts with white space
				if (lineEnd + 1 < end && (lineEnd[1] == ' ' || lineEnd[1] == '\t'))
				{
					v = lineEnd + 1;
					lineEnd = v;
					while (lineEnd < end && *lineEnd != '\n')
						lineEnd++;
					continue;
				}
				break;
			}
			// trimmed
			size_t a = 0;
			while (value.s[a] == ' ' || value.s[a] == '\t')
				a++;
			size_t b = value.n;
			while (b > a && (value.s[b - 1] == ' ' || value.s[b - 1] == '\t'))
				b--;
			memmove(value.s, value.s + a, b - a);
			value.s[b - a] = 0;
			return value.s;
		}
		p = lineEnd + 1;
	}
	return NULL;
}

// a parameter of a header value (`boundary`, `charset`): malloc'd or NULL
char*
Parameter(const char* value, const char* name)
{
	if (value == NULL)
		return NULL;
	size_t nameLength = strlen(name);
	for (const char* p = value; *p; p++)
		if (SameNoCase(p, name, nameLength) && p[nameLength] == '=' && (p == value || p[-1] == ';' || p[-1] == ' ' || p[-1] == '\t'))
		{
			const char* v = p + nameLength + 1;
			const char* e;
			if (*v == '"')
			{
				v++;
				e = strchr(v, '"');
				if (e == NULL)
					e = v + strlen(v);
			}
			else
			{
				e = v;
				while (*e && *e != ';' && *e != ' ' && *e != '\t')
					e++;
			}
			char* out = (char*) malloc((size_t) (e - v) + 1);
			memcpy(out, v, (size_t) (e - v));
			out[e - v] = 0;
			return out;
		}
	return NULL;
}

bool
TypeIs(const char* contentType, const char* type)
{
	if (contentType == NULL)
		return strcmp(type, "text/plain") == 0;
	return SameNoCase(contentType, type, strlen(type));
}

// a part's body decoded and made UTF-8 (with LF line ends)
void
AddPartText(Text* out, const Part* part, bool html)
{
	char* encoding = Header(part, "Content-Transfer-Encoding");
	char* type = Header(part, "Content-Type");
	char* charset = Parameter(type, "charset");
	Text raw;
	Init(&raw);
	if (encoding != NULL && SameNoCase(encoding, "quoted-printable", 16))
		QPDecode(&raw, part->body, part->bodySize);
	else if (encoding != NULL && SameNoCase(encoding, "base64", 6))
	{
		size_t size;
		unsigned char* bytes = HostMailBase64Decode(part->body, part->bodySize, &size);
		AddBytes(&raw, (const char*) bytes, size);
		free(bytes);
	}
	else
		AddBytes(&raw, part->body, part->bodySize);
	Text text;
	Init(&text);
	AddInCharset(&text, raw.s, raw.n, charset);
	free(raw.s);
	// line ends as LF; HTML's tags out, its common entities in
	bool inTag = false;
	for (size_t i = 0; i < text.n; i++)
	{
		char c = text.s[i];
		if (c == '\r')
			continue;
		if (html)
		{
			if (c == '<')
			{
				// (a break or a paragraph's end a new line)
				if (SameNoCase(text.s + i, "<br", 3) || SameNoCase(text.s + i, "</p", 3) || SameNoCase(text.s + i, "</div", 5))
					AddChar(out, '\n');
				inTag = true;
				continue;
			}
			if (c == '>' && inTag)
			{
				inTag = false;
				continue;
			}
			if (inTag || c == '\n')
				continue;
			if (c == '&')
			{
				static const struct { const char* name; const char* value; } kEntities[] = {
					{ "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&#39;", "'" }, { "&nbsp;", " " } };
				bool done = false;
				for (size_t k = 0; k < sizeof(kEntities) / sizeof(kEntities[0]) && !done; k++)
					if (strncmp(text.s + i, kEntities[k].name, strlen(kEntities[k].name)) == 0)
					{
						Add(out, kEntities[k].value);
						i += strlen(kEntities[k].name) - 1;
						done = true;
					}
				if (done)
					continue;
			}
		}
		AddChar(out, c);
	}
	free(text.s);
	free(encoding);
	free(type);
	free(charset);
}

// the text of a part: itself, or of its multipart's parts.  ==> whether
// text was found (`html`: take text/html when there is no plain text)
bool
FindText(Text* out, const char* p, size_t n, bool html, int depth)
{
	Part part;
	SplitPart(p, n, &part);
	char* type = Header(&part, "Content-Type");
	bool found = false;
	if (type != NULL && SameNoCase(type, "multipart/", 10) && depth < 8)
	{
		char* boundary = Parameter(type, "boundary");
		if (boundary != NULL)
		{
			size_t bl = strlen(boundary);
			// the parts between "--boundary" lines
			const char* end = part.body + part.bodySize;
			const char* start = NULL;
			for (const char* s = part.body; s + 2 + bl <= end && !found; )
			{
				const char* lineEnd = s;
				while (lineEnd < end && *lineEnd != '\n')
					lineEnd++;
				if (s[0] == '-' && s[1] == '-' && strncmp(s + 2, boundary, bl) == 0)
				{
					if (start != NULL)
					{
						const char* partEnd = s;
						if (partEnd > start && partEnd[-1] == '\n') partEnd--;
						if (partEnd > start && partEnd[-1] == '\r') partEnd--;
						found = FindText(out, start, (size_t) (partEnd - start), html, depth + 1);
					}
					bool last = s + 4 + bl <= end && s[2 + bl] == '-' && s[3 + bl] == '-';
					start = last ? NULL : (lineEnd < end ? lineEnd + 1 : end);
					if (last)
						break;
				}
				s = lineEnd < end ? lineEnd + 1 : end;
			}
			free(boundary);
		}
	}
	else
	{
		char* disposition = Header(&part, "Content-Disposition");
		bool attachment = disposition != NULL && SameNoCase(disposition, "attachment", 10);
		if (!attachment && (TypeIs(type, "text/plain") || (html && TypeIs(type, "text/html"))))
		{
			AddPartText(out, &part, TypeIs(type, "text/html"));
			found = true;
		}
		free(disposition);
	}
	free(type);
	return found;
}

// the attachments' names, as lines
void
ListAttachments(Text* out, const char* p, size_t n, int depth)
{
	Part part;
	SplitPart(p, n, &part);
	char* type = Header(&part, "Content-Type");
	if (type != NULL && SameNoCase(type, "multipart/", 10) && depth < 8)
	{
		char* boundary = Parameter(type, "boundary");
		if (boundary != NULL)
		{
			size_t bl = strlen(boundary);
			const char* end = part.body + part.bodySize;
			const char* start = NULL;
			for (const char* s = part.body; s + 2 + bl <= end; )
			{
				const char* lineEnd = s;
				while (lineEnd < end && *lineEnd != '\n')
					lineEnd++;
				if (s[0] == '-' && s[1] == '-' && strncmp(s + 2, boundary, bl) == 0)
				{
					if (start != NULL)
						ListAttachments(out, start, (size_t) (s - start), depth + 1);
					bool last = s + 4 + bl <= end && s[2 + bl] == '-' && s[3 + bl] == '-';
					start = last ? NULL : (lineEnd < end ? lineEnd + 1 : end);
					if (last)
						break;
				}
				s = lineEnd < end ? lineEnd + 1 : end;
			}
			free(boundary);
		}
	}
	else
	{
		char* disposition = Header(&part, "Content-Disposition");
		if (disposition != NULL && SameNoCase(disposition, "attachment", 10))
		{
			char* name = Parameter(disposition, "filename");
			Add(out, "\n[attachment: ");
			Add(out, name != NULL ? name : "unnamed");
			Add(out, "]");
			free(name);
		}
		free(disposition);
	}
	free(type);
}

char*
Copy(const char* s)
{
	char* c = (char*) malloc(strlen(s) + 1);
	strcpy(c, s);
	return c;
}

// a header's words made UTF-8 (malloc'd)
char*
Decoded(const char* value)
{
	Text t;
	Init(&t);
	if (value != NULL)
		DecodeWords(&t, value, strlen(value));
	return t.s;
}

// an encoded word when the text is not ASCII
void
AddHeaderText(Text* out, const char* text)
{
	if (IsASCII(text))
	{
		Add(out, text);
		return;
	}
	char* b = HostMailBase64Encode((const unsigned char*) text, strlen(text));
	Add(out, "=?UTF-8?B?");
	Add(out, b);
	Add(out, "?=");
	free(b);
}

} // namespace


char*
HostMailBase64Encode(const unsigned char* data, size_t size)
{
	char* out = (char*) malloc((size + 2) / 3 * 4 + 1);
	size_t o = 0;
	for (size_t i = 0; i < size; i += 3)
	{
		unsigned long v = (unsigned long) data[i] << 16;
		if (i + 1 < size) v |= (unsigned long) data[i + 1] << 8;
		if (i + 2 < size) v |= data[i + 2];
		out[o++] = kBase64[(v >> 18) & 63];
		out[o++] = kBase64[(v >> 12) & 63];
		out[o++] = i + 1 < size ? kBase64[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < size ? kBase64[v & 63] : '=';
	}
	out[o] = 0;
	return out;
}

unsigned char*
HostMailBase64Decode(const char* text, size_t length, size_t* size)
{
	unsigned char* out = (unsigned char*) malloc(length * 3 / 4 + 3);
	size_t o = 0;
	unsigned long v = 0;
	int bits = 0;
	for (size_t i = 0; i < length; i++)
	{
		int d = Base64Value(text[i]);
		if (d < 0)
			continue;		// (line ends, padding)
		v = (v << 6) | (unsigned long) d;
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out[o++] = (unsigned char) ((v >> bits) & 0xFF);
		}
	}
	*size = o;
	return out;
}

char*
HostMailCompose(const char* fromName, const char* fromAddress, const char* to,
				const char* subject, const char* text, const char* date, const char* messageId)
{
	Text m;
	Init(&m);
	Add(&m, "From: ");
	if (fromName != NULL && fromName[0] != 0)
	{
		if (IsASCII(fromName))
		{
			AddChar(&m, '"');
			for (const char* p = fromName; *p; p++)
				if (*p != '"' && *p != '\\')
					AddChar(&m, *p);
			AddChar(&m, '"');
		}
		else
			AddHeaderText(&m, fromName);
		Add(&m, " <");
		Add(&m, fromAddress);
		Add(&m, ">");
	}
	else
		Add(&m, fromAddress);
	Add(&m, "\r\nTo: ");
	Add(&m, to);
	Add(&m, "\r\nSubject: ");
	AddHeaderText(&m, subject != NULL ? subject : "");
	Add(&m, "\r\nDate: ");
	Add(&m, date);
	Add(&m, "\r\nMessage-ID: ");
	Add(&m, messageId);
	Add(&m, "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\n"
		"Content-Transfer-Encoding: quoted-printable\r\nX-Mailer: Newton (newton-re)\r\n\r\n");
	// the text quoted-printable, lines of at most 76
	int column = 0;
	for (const char* p = text; *p; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (c == '\r')
			continue;
		if (c == '\n')
		{
			Add(&m, "\r\n");
			column = 0;
			continue;
		}
		char piece[4];
		bool plain = c >= 32 && c < 127 && c != '=' && !((c == ' ' || c == '\t') && (p[1] == '\n' || p[1] == 0));
		if (plain)
			piece[0] = (char) c, piece[1] = 0;
		else
			snprintf(piece, sizeof(piece), "=%02X", c);
		int width = (int) strlen(piece);
		if (column + width > 75)
		{
			Add(&m, "=\r\n");
			column = 0;
		}
		Add(&m, piece);
		column += width;
	}
	Add(&m, "\r\n");
	return m.s;
}

char*
HostMailDotStuff(const char* message)
{
	Text out;
	Init(&out);
	bool lineStart = true;
	for (const char* p = message; *p; p++)
	{
		if (lineStart && *p == '.')
			AddChar(&out, '.');
		AddChar(&out, *p);
		lineStart = *p == '\n';
	}
	if (!lineStart)
		Add(&out, "\r\n");
	Add(&out, ".\r\n");
	return out.s;
}

int
HostMailParse(const char* raw, size_t size, HostMailMessage* out)
{
	Part top;
	SplitPart(raw, size, &top);
	char* from = Header(&top, "From");
	char* subject = Header(&top, "Subject");
	char* date = Header(&top, "Date");
	char* fromText = Decoded(from);
	// "Name <address>", "address (Name)" or an address
	char* lt = strchr(fromText, '<');
	char* gt = lt != NULL ? strchr(lt, '>') : NULL;
	if (lt != NULL && gt != NULL)
	{
		*gt = 0;
		out->fromAddress = Copy(lt + 1);
		*lt = 0;
		char* name = fromText;
		while (*name == ' ' || *name == '"')
			name++;
		size_t n = strlen(name);
		while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '"'))
			name[--n] = 0;
		out->fromName = Copy(name);
	}
	else
	{
		out->fromAddress = Copy(fromText);
		out->fromName = Copy("");
	}
	free(fromText);
	out->subject = Decoded(subject);
	out->date = Copy(date != NULL ? date : "");
	Text text;
	Init(&text);
	if (!FindText(&text, raw, size, false, 0))
		FindText(&text, raw, size, true, 0);
	ListAttachments(&text, raw, size, 0);
	// (trailing blank lines off)
	while (text.n > 0 && (text.s[text.n - 1] == '\n' || text.s[text.n - 1] == ' '))
		text.s[--text.n] = 0;
	out->text = text.s;
	free(from);
	free(subject);
	free(date);
	return 0;
}

void
HostMailFree(HostMailMessage* message)
{
	free(message->fromName);
	free(message->fromAddress);
	free(message->subject);
	free(message->date);
	free(message->text);
	message->fromName = message->fromAddress = message->subject = message->date = message->text = NULL;
}

void
HostMailDate(char* buffer, size_t size)
{
	static const char* const kDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char* const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	time_t now = time(NULL);
	struct tm local, utc;
#if defined(_WIN32)
	localtime_s(&local, &now);
	gmtime_s(&utc, &now);
#else
	localtime_r(&now, &local);
	gmtime_r(&now, &utc);
#endif
	// the zone: local less UTC, in minutes
	long offset = (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
	int dayDiff = local.tm_yday - utc.tm_yday;
	if (dayDiff == 1 || dayDiff < -1) offset += 1440;
	else if (dayDiff == -1 || dayDiff > 1) offset -= 1440;
	char sign = offset < 0 ? '-' : '+';
	if (offset < 0) offset = -offset;
	snprintf(buffer, size, "%s, %d %s %04d %02d:%02d:%02d %c%02ld%02ld", kDays[local.tm_wday], local.tm_mday,
		kMonths[local.tm_mon], local.tm_year + 1900, local.tm_hour, local.tm_min, local.tm_sec, sign, offset / 60, offset % 60);
}


bool
HostMailParseServer(const char* url, bool imap, char* host, size_t hostSize, int* port, bool* tls)
{
	const char* scheme = imap ? "imap" : "smtp";
	size_t sl = strlen(scheme);
	if (strncmp(url, scheme, sl) != 0)
		return false;
	const char* p = url + sl;
	*tls = false;
	if (*p == 's')
	{
		*tls = true;
		p++;
	}
	if (strncmp(p, "://", 3) != 0)
		return false;
	p += 3;
	const char* end = p;
	while (*end && *end != ':' && *end != '/')
		end++;
	if (end == p || (size_t) (end - p) >= hostSize)
		return false;
	memcpy(host, p, (size_t) (end - p));
	host[end - p] = 0;
	*port = imap ? (*tls ? 993 : 143) : (*tls ? 465 : 25);
	if (*end == ':')
		*port = atoi(end + 1);
	return *port > 0 && *port < 65536;
}
