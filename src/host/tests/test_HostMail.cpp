/*
	File:		host/tests/test_HostMail.cpp

	Contains:	Mail messages made and read (host/HostMailFormat.h): a
				message made and read back as it was written - its UTF-8
				subject and sender's name, a long line, a line that is only
				a full stop, trailing spaces; dot-stuffing; messages as other
				mail programs write them - multipart/alternative with
				base64 and quoted-printable parts, HTML only, ISO 8859-1,
				encoded words in Q and B, an attachment named; base64;
				server URLs.  (Messages from Python's email package are read
				by ctest host.NewtonMail, through tools/host/fakemail.py.)

				test_HostMail  ==> "test_HostMail: all passed"
*/

#include "HostMailFormat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gFailures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAILED: "); printf(__VA_ARGS__); printf("\n"); gFailures++; } } while (0)

static void
Read(const char* raw, HostMailMessage* m)
{
	HostMailParse(raw, strlen(raw), m);
}

int
main()
{
	// made, then read back
	{
		const char* text = "Dear Ann,\nCaf\xC3\xA9 at eight?\n.\nA long line: "
			"one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen\n"
			"Trailing spaces   \nBye";
		char* m = HostMailCompose("J\xC3\xBCrgen", "jurgen@example.org", "ann@example.org",
			"Caf\xC3\xA9 at eight", text, "Thu, 12 Nov 2026 14:05:00 +1100", "<1@newton>");
		CHECK(strstr(m, "Subject: =?UTF-8?B?") != NULL, "the subject not encoded:\n%s", m);
		CHECK(strstr(m, "Content-Transfer-Encoding: quoted-printable") != NULL, "not quoted-printable");
		// no line longer than 78
		int longest = 0, run = 0;
		for (const char* p = m; *p; p++)
		{
			run = *p == '\n' ? 0 : run + 1;
			if (run > longest) longest = run;
		}
		CHECK(longest <= 78, "a line of %d", longest);
		char* data = HostMailDotStuff(m);
		CHECK(strstr(data, "\r\n..\r\n") != NULL, "the full-stop line not doubled");
		CHECK(strlen(data) >= 5 && strcmp(data + strlen(data) - 5, "\r\n.\r\n") == 0, "no ending");
		HostMailMessage back;
		HostMailParse(m, strlen(m), &back);
		CHECK(strcmp(back.subject, "Caf\xC3\xA9 at eight") == 0, "subject back: %s", back.subject);
		CHECK(strcmp(back.fromName, "J\xC3\xBCrgen") == 0, "name back: %s", back.fromName);
		CHECK(strcmp(back.fromAddress, "jurgen@example.org") == 0, "address back: %s", back.fromAddress);
		CHECK(strcmp(back.text, text) == 0, "text back:\n[%s]\nwanted\n[%s]", back.text, text);
		HostMailFree(&back);
		free(data);
		free(m);
	}

	// multipart/alternative: base64 plain, quoted-printable HTML
	{
		const char* raw =
			"From: \"Ann Ng\" <ann@example.org>\r\n"
			"To: tester@newton.test\r\n"
			"Subject: =?iso-8859-1?Q?Caf=E9_at?=\r\n =?utf-8?B?IGVpZ2h0?=\r\n"
			"MIME-Version: 1.0\r\n"
			"Content-Type: multipart/alternative;\r\n boundary=\"XYZ\"\r\n"
			"\r\n"
			"preamble\r\n"
			"--XYZ\r\n"
			"Content-Type: text/plain; charset=utf-8\r\n"
			"Content-Transfer-Encoding: base64\r\n"
			"\r\n"
			"U2VlIHlvdSBhdCB0aGUgY2Fmw6kuCkJyaW5nIHRoZSBtYXAu\r\n"
			"--XYZ\r\n"
			"Content-Type: text/html; charset=utf-8\r\n"
			"Content-Transfer-Encoding: quoted-printable\r\n"
			"\r\n"
			"<p>See you at the caf=C3=A9.</p>\r\n"
			"--XYZ--\r\n";
		HostMailMessage m;
		Read(raw, &m);
		CHECK(strcmp(m.subject, "Caf\xC3\xA9 at eight") == 0, "subject: %s", m.subject);
		CHECK(strcmp(m.fromName, "Ann Ng") == 0 && strcmp(m.fromAddress, "ann@example.org") == 0, "from: %s %s", m.fromName, m.fromAddress);
		CHECK(strcmp(m.text, "See you at the caf\xC3\xA9.\nBring the map.") == 0, "text: [%s]", m.text);
		HostMailFree(&m);
	}

	// HTML only, ISO 8859-1 quoted-printable, an attachment
	{
		const char* raw =
			"From: shop@example.org\n"
			"Subject: Order\n"
			"Content-Type: multipart/mixed; boundary=b1\n"
			"\n"
			"--b1\n"
			"Content-Type: text/html; charset=iso-8859-1\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n"
			"<html><body><p>Your cr=E8me br=FBl=E9e &amp; tea</p><p>are on their way.</p></body></html>\n"
			"--b1\n"
			"Content-Type: application/pdf; name=\"receipt.pdf\"\n"
			"Content-Disposition: attachment; filename=\"receipt.pdf\"\n"
			"Content-Transfer-Encoding: base64\n"
			"\n"
			"JVBERi0xLjQK\n"
			"--b1--\n";
		HostMailMessage m;
		Read(raw, &m);
		CHECK(strcmp(m.fromName, "") == 0 && strcmp(m.fromAddress, "shop@example.org") == 0, "from: [%s] %s", m.fromName, m.fromAddress);
		CHECK(strstr(m.text, "Your cr\xC3\xA8me br\xC3\xBBl\xC3\xA9" "e & tea\nare on their way.") != NULL, "html text: [%s]", m.text);
		CHECK(strstr(m.text, "[attachment: receipt.pdf]") != NULL, "the attachment not named: [%s]", m.text);
		HostMailFree(&m);
	}

	// plain, no MIME at all
	{
		HostMailMessage m;
		Read("From: Bob <bob@example.org>\nSubject: Hi\n\nJust text.\n", &m);
		CHECK(strcmp(m.text, "Just text.") == 0 && strcmp(m.subject, "Hi") == 0, "plain: [%s] [%s]", m.subject, m.text);
		HostMailFree(&m);
	}

	// base64 both ways
	{
		const unsigned char bytes[] = { 0, 1, 2, 250, 251, 252, 'N', 'e', 'w' };
		for (size_t n = 0; n <= sizeof(bytes); n++)
		{
			char* e = HostMailBase64Encode(bytes, n);
			size_t size;
			unsigned char* d = HostMailBase64Decode(e, strlen(e), &size);
			CHECK(size == n && memcmp(d, bytes, n) == 0, "base64 of %u bytes", (unsigned) n);
			free(e);
			free(d);
		}
		char* hello = HostMailBase64Encode((const unsigned char*) "hello", 5);
		CHECK(strcmp(hello, "aGVsbG8=") == 0, "base64 of hello: %s", hello);
		free(hello);
	}

	// server URLs
	{
		char host[64];
		int port;
		bool tls;
		CHECK(HostMailParseServer("smtps://mail.example.org", false, host, sizeof(host), &port, &tls) && port == 465 && tls
			&& strcmp(host, "mail.example.org") == 0, "smtps");
		CHECK(HostMailParseServer("imap://127.0.0.1:1143", true, host, sizeof(host), &port, &tls) && port == 1143 && !tls, "imap port");
		CHECK(HostMailParseServer("imaps://x", true, host, sizeof(host), &port, &tls) && port == 993 && tls, "imaps");
		CHECK(HostMailParseServer("smtp://x", false, host, sizeof(host), &port, &tls) && port == 25 && !tls, "smtp");
		CHECK(!HostMailParseServer("http://x", false, host, sizeof(host), &port, &tls), "http taken");
		CHECK(!HostMailParseServer("imap://x", false, host, sizeof(host), &port, &tls), "imap as smtp taken");
	}

	if (gFailures == 0)
		printf("test_HostMail: all passed\n");
	return gFailures == 0 ? 0 : 1;
}
