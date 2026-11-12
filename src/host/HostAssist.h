/*
	File:		host/HostAssist.h

	Contains:	DEVIATION (host addition; docs/modern/assistant.md): the
				Assistant's engines - HostAssist.ns, evaluated at boot and kept
				in the global HostAssist:host, which assist/ParseUtter.cpp asks
				when the ROM's parse of a sentence finds no task - and the
				natives it talks to a model with:

				  HostAssistConfig() - {engine, url, model}: the engine
					  NEWTON_ASSIST_ENGINE names ('http, 'table, or 'none, the
					  default), the address of an OpenAI-style chat completions
					  service (NEWTON_ASSIST_URL, by default a model server on
					  this machine: http://localhost:11434/v1/chat/completions,
					  as Ollama and llama.cpp's server answer) and the model
					  (NEWTON_ASSIST_MODEL).
				  HostAssistRequest(instructions, sentence) - the request
					  begun: a POST of {model, messages: [system, user]} over
					  the host's own sockets (TLS for https://, as the IPP
					  printers' connection, print/host/HostIPPSocket.h), with
					  NEWTON_ASSIST_KEY as a bearer token when it is set.
					  ==> true, or nil when it could not be begun.
				  HostAssistPoll() - nil while the answer is coming, the
					  reply's text (choices[0].message.content) when it has
					  come, or an error's description as a symbol-less frame
					  {error: "..."}.

				One request at a time; nothing waits - each poll does what the
				socket allows, so the NewtonScript side asks again from a
				delayed call while the slip shows that it is thinking.
*/

#ifndef __HOSTASSIST_H
#define __HOSTASSIST_H

#include <stddef.h>

void	HostInstallAssist(void);		// at boot: the natives, HostAssist.ns evaluated

// (for tests: plain C++, no Newton types)
// The JSON string literal of UTF-8 text, quotes and all, into out.  ==> its
// length, nought if it does not fit.
size_t	HostAssistJSONString(const char* text, char* out, size_t size);
// The text of the first "content" string after "choices" in a chat
// completions reply, as UTF-8 into out.  ==> false when there is none.
bool	HostAssistReplyContent(const char* json, char* out, size_t size);
// An HTTP response's body found, chunks joined: ==> its status, or -1 when
// it is not HTTP; body/bodySize what follows the head.
int		HostAssistHTTPBody(char* response, size_t size, char** body, size_t* bodySize);

#endif	/* __HOSTASSIST_H */
