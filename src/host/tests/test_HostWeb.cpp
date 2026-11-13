/*
	File:		host/tests/test_HostWeb.cpp

	Contains:	A web page made Newton text (host/HostWebPage.h): URLs
				resolved; what the address line holds made an address or a
				search; HTTP responses taken apart, chunked and not; and the
				test pages of src/host/demo/web read as a reader view reads
				them - the article kept, the menus, banners, sidebars,
				advertisements, scripts and footers left out, headings,
				items, a table's rows, a quotation and pictures written,
				links numbered and made absolute, ISO 8859-1 made UTF-8.

				test_HostWeb <src/host/demo/web>  ==> "test_HostWeb: all passed"
*/

#include "HostWebPage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gFailures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAILED: "); printf(__VA_ARGS__); printf("\n"); gFailures++; } } while (0)

static char*
ReadFile(const char* folder, const char* name, size_t* size)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", folder, name);
	FILE* f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* data = (char*) malloc((size_t) n + 1);
	*size = fread(data, 1, (size_t) n, f);
	data[*size] = 0;
	fclose(f);
	return data;
}

static void
Resolves(const char* base, const char* reference, const char* want)
{
	char* got = HostWebResolve(base, reference);
	bool ok = want == NULL ? got == NULL : got != NULL && strcmp(got, want) == 0;
	CHECK(ok, "%s + %s: %s, wanted %s", base, reference, got != NULL ? got : "NULL", want != NULL ? want : "NULL");
	free(got);
}

// the page's whole text, its blocks one a line
static char*
AllText(const HostWebPage* page)
{
	size_t n = 1;
	for (int i = 0; i < page->blockCount; i++)
		n += strlen(page->blocks[i].text) + 1;
	char* all = (char*) malloc(n);
	all[0] = 0;
	for (int i = 0; i < page->blockCount; i++)
	{
		strcat(all, page->blocks[i].text);
		strcat(all, "\n");
	}
	return all;
}

static const HostWebBlock*
BlockWith(const HostWebPage* page, const char* text)
{
	for (int i = 0; i < page->blockCount; i++)
		if (strstr(page->blocks[i].text, text) != NULL)
			return &page->blocks[i];
	return NULL;
}

int
main(int argc, char** argv)
{
	const char* folder = argc > 1 ? argv[1] : "web";

	// URLs
	const char* base = "http://example.org:8080/news/2026/story.html?id=3#top";
	Resolves(base, "page2.html", "http://example.org:8080/news/2026/page2.html");
	Resolves(base, "../index.html", "http://example.org:8080/news/index.html");
	Resolves(base, "/", "http://example.org:8080/");
	Resolves(base, "?id=4", "http://example.org:8080/news/2026/story.html?id=4");
	Resolves(base, "//cdn.example.net/x.js", "http://cdn.example.net/x.js");
	Resolves(base, "https://other.example/a#b", "https://other.example/a#b");
	Resolves(base, "#comments", NULL);
	Resolves(base, "javascript:void(0)", NULL);
	Resolves(base, "mailto:ann@example.org", NULL);
	Resolves("https://example.org", "a/b/../c/./d.html", "https://example.org/a/c/d.html");
	{
		HostWebURL u;
		CHECK(HostWebParseURL("https://Example.ORG/a?b=1#c", &u) && u.tls && u.port == 443 && strcmp(u.host, "example.org") == 0
			&& strcmp(u.path, "/a?b=1") == 0, "parse https: %s %s", u.host, u.path);
		CHECK(HostWebParseURL("http://h:81?q", &u) && u.port == 81 && strcmp(u.path, "/?q") == 0, "parse a query with no path: %s", u.path);
		CHECK(!HostWebParseURL("ftp://h/", &u), "ftp taken");
		CHECK(!HostWebParseURL("http://user@h/", &u), "credentials taken");
	}

	// the address line
	{
		const char* engine = "https://search.example/html/?q=%s";
		char* a = HostWebAddress("  example.org/path ", engine);
		CHECK(strcmp(a, "http://example.org/path") == 0, "address: %s", a);
		free(a);
		a = HostWebAddress("https://x.example/", engine);
		CHECK(strcmp(a, "https://x.example/") == 0, "url: %s", a);
		free(a);
		a = HostWebAddress("lighthouse opening times & tides", engine);
		CHECK(strcmp(a, "https://search.example/html/?q=lighthouse+opening+times+%26+tides") == 0, "search: %s", a);
		free(a);
		a = HostWebAddress("127.0.0.1:8000/articles", engine);
		CHECK(strcmp(a, "http://127.0.0.1:8000/articles") == 0, "a numeric host: %s", a);
		free(a);
		a = HostWebAddress("localhost:8080", engine);
		CHECK(strcmp(a, "http://localhost:8080") == 0, "localhost: %s", a);
		free(a);
		a = HostWebAddress("version 2.1", engine);
		CHECK(strncmp(a, "https://search.example/", 23) == 0, "words with a dot searched: %s", a);
		free(a);
	}

	// HTTP
	{
		const char* chunked = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nTransfer-Encoding: chunked\r\n\r\n"
			"5\r\nHello\r\n7\r\n, world\r\n0\r\n\r\n";
		HostWebResponse r;
		CHECK(HostWebParseResponse(chunked, strlen(chunked), &r) && r.status == 200 && strcmp(r.body, "Hello, world") == 0
			&& strcmp(r.contentType, "text/html") == 0 && strcmp(r.charset, "utf-8") == 0, "chunked: [%s] %s %s", r.body, r.contentType, r.charset);
		HostWebFreeResponse(&r);
		const char* moved = "HTTP/1.0 301 Moved Permanently\r\nLocation: /articles/\r\nContent-Length: 3\r\n\r\nabcdef";
		CHECK(HostWebParseResponse(moved, strlen(moved), &r) && r.status == 301 && strcmp(r.location, "/articles/") == 0
			&& strcmp(r.body, "abc") == 0, "redirect: %d %s [%s]", r.status, r.location, r.body);
		HostWebFreeResponse(&r);
		CHECK(!HostWebParseResponse("garbage", 7, &r), "not HTTP taken");
	}

	// the article
	{
		size_t size;
		char* html = ReadFile(folder, "articles/index.html", &size);
		CHECK(html != NULL, "articles/index.html not there");
		if (html != NULL)
		{
			HostWebPage page;
			HostWebMakePage(html, size, "", false, "http://127.0.0.1:8000/articles/", &page);
			char* all = AllText(&page);
			CHECK(strcmp(page.title, "Harbour Gazette \xE2\x80\x93 The lighthouse is lit again") == 0, "title: %s", page.title);
			const HostWebBlock* h1 = BlockWith(&page, "The lighthouse is lit again");
			CHECK(h1 != NULL && h1->kind == kWebHeading && h1->level == 1, "the heading");
			CHECK(strstr(all, "\xE2\x80\x9CIt was the first thing I saw from the boat,\xE2\x80\x9D") != NULL, "the quotation marks");
			const HostWebBlock* item = BlockWith(&page, "142 of them");
			CHECK(item != NULL && item->kind == kWebItem, "a list item");
			CHECK(strstr(all, "\xC2\xA3" "120,000") != NULL, "the pound sign");
			CHECK(strstr(all, "Read how the money was raised [1]") != NULL, "the link's number:\n%s", all);
			CHECK(page.linkCount >= 1 && strcmp(page.links[0], "http://127.0.0.1:8000/articles/page2.html") == 0,
				"the link's URL: %s", page.linkCount > 0 ? page.links[0] : "none");
			CHECK(strstr(all, "[picture: The new lamp]") != NULL, "the picture");
			CHECK(strstr(all, "1986 | Lamp put out") != NULL, "a table row:\n%s", all);
			const HostWebBlock* quote = BlockWith(&page, "belongs to everyone");
			CHECK(quote != NULL && quote->kind == kWebQuote, "the quotation");
			// left out
			const char* never[] = { "Sport", "We use cookies", "Most read", "Ferry times", "Buy one", "All rights reserved", "tracker", "font-family" };
			for (size_t i = 0; i < sizeof(never) / sizeof(never[0]); i++)
				CHECK(strstr(all, never[i]) == NULL, "\"%s\" kept:\n%s", never[i], all);
			printf("the article: %d blocks, %d links\n%s", page.blockCount, page.linkCount, all);
			free(all);
			HostWebFreePage(&page);
			free(html);
		}
	}

	// ISO 8859-1 by its <meta>; text/plain
	{
		size_t size;
		char* html = ReadFile(folder, "latin1.html", &size);
		HostWebPage page;
		HostWebMakePage(html, size, "", false, "http://h/latin1.html", &page);
		char* all = AllText(&page);
		CHECK(strcmp(page.title, "Caf\xC3\xA9 news") == 0, "the Latin-1 title: %s", page.title);
		CHECK(strstr(all, "cr\xC3\xA8me br\xC3\xBBl\xC3\xA9" "e") != NULL, "the Latin-1 text: %s", all);
		free(all);
		HostWebFreePage(&page);
		free(html);
		const char* text = "Line one\n\n  indented\n";
		HostWebMakePage(text, strlen(text), "utf-8", true, "http://h/t.txt", &page);
		CHECK(page.blockCount == 1 && page.blocks[0].kind == kWebPre && strcmp(page.blocks[0].text, "Line one\n\n  indented") == 0,
			"plain text: [%s]", page.blockCount > 0 ? page.blocks[0].text : "");
		HostWebFreePage(&page);
	}

	if (gFailures == 0)
		printf("test_HostWeb: all passed\n");
	return gFailures == 0 ? 0 : 1;
}
