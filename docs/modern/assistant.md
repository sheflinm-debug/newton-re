# The Assistant with a language model behind it

**Status: a first version built (2026-11-12): the engine seam, an engine
for any OpenAI-style chat service, and a fake for the tests; no lookups.**

## What Newton has

The Assist button takes a written sentence and matches it against task
templates: schedule, find, remember, mail, fax, print, call, time on the US
ROM, and whatever applications add with `RegTaskTemplate`
(`docs/assist/README.md`). A match fills a slip the user confirms. The
matching is by lexicon: the first word must be one it knows, and the rest
is parsed by fixed heuristics.

## The aim

The same button, the same confirm slip, the same task templates, but a
sentence the lexicon cannot parse is understood. The model chooses a task
and fills its slots; it never acts by itself.

## How it works

1. The user writes a sentence and taps Assist, as now.
2. The ROM's matcher runs first. If it matches, nothing else happens: no
   model is called and the behaviour is the ROM's.
3. Otherwise the sentence goes to the **assist engine** with:
   - the list of tasks, generated from the live task templates (the name,
     what it acts on, its slots), so a task an application registered is
     offered to the model without further work;
   - a small set of read-only tools the model may call: look up a person in
     Names, list meetings in a date range, find notes containing words. Each
     is an ordinary soup query, limited in the number of entries returned.
4. The engine answers one task and its slot values, or "none".
5. The Assistant opens that task's own slip, filled in. The user confirms or
   corrects it exactly as for a ROM match.

A model's answer that names no registered task, or a slot of the wrong
type, is discarded and the ROM's "I don't understand" slip shown.

## The engine seam

An interface `TAssistEngine` registered by name, as the word recognisers
are (`docs/recognition/engines.md`), with the ROM's matcher as the default.
The host supplies engines; the request is asynchronous and the slip shows
the Newton's busy indicator.

| Engine | Where the text goes | Needs |
|---|---|---|
| none (default) | nowhere | nothing |
| local | a model server on the device or the user's network | a capable device or a machine at home |
| cloud | a model provider over TLS | a network, an API key kept in the host's settings |

## Privacy

- Nothing is sent unless the user tapped Assist and the ROM's matcher
  failed. There is no background watching.
- What is sent: the sentence, the task list, and the entries the model's
  lookups return. The slip has a "What was sent" line that shows it.
- The engine is a permission (`permissions.md`): a package cannot call it
  unless granted.
- The Host panel names the engine in use and where it sends.

## Tests

A fake engine that answers from a table, so ctests need no model: a
sentence the ROM matches (the engine is not called), one it does not (the
right slip, filled), a wrong task name (discarded), a lookup that returns
entries (only those are in the request).

## Later

Offers inside an ordinary note ("call Bob Friday" underlined, tap to make
the to-do) belong to `structure.md`, "Live details", and use the ROM's
matcher only, so nothing leaves the device for them.

## State (2026-11-12, branch `modern`)

Built, behind the Host panel's "Assistant model" (`host/Features.h`'s
`kFeatureAssistant`, `NEWTON_ASSISTANT`):
- **The seam.** `assist/ParseUtter.cpp`'s `parseUtter` asks
  `AskAssistEngine(sentence)` where the ROM would say "I don't understand"
  (`duh`) - so a sentence the ROM's parse understands never reaches an
  engine. The engines are NewtonScript, `src/host/HostAssist.ns` (the
  global `HostAssist:host`, `Understand(sentence)`), rather than a C++
  `TAssistEngine` registry: an engine's work is building a request and
  reading an answer, and the natives it needs are few.
- **How the model fills the slots.** The engine is asked for one command
  in the words the ROM's parse knows ("call Daniel", "schedule lunch with
  Anna friday 1pm"), and that command is handed to `ParseUtter`, which
  opens the task's own slip filled in by the ROM's own parse. A command
  whose first word is not one of the live tasks (`GenFullCommands()`: the
  ROM's and every `RegTaskTemplate` one), or that the ROM still cannot
  parse, is discarded and "I don't understand" shown. The instructions sent
  are made from the same live list, so an application's task is offered
  without further work.
- **The engines.** `NEWTON_ASSIST_ENGINE`: `none` (the default - nothing is
  ever sent) or `http`, an OpenAI-style chat completions service at
  `NEWTON_ASSIST_URL` (by default `http://localhost:11434/v1/chat/completions`,
  a model server on the same machine as Ollama and llama.cpp's server
  answer), model `NEWTON_ASSIST_MODEL` (`llama3.2`), with
  `NEWTON_ASSIST_KEY` as a bearer token when set - which makes a cloud
  provider possible too, by the user's own configuration. The request goes
  over the host's sockets (TLS for https, the IPP printers' connection,
  `print/host/HostIPPSocket.h`) from `host/HostAssist.cpp`, polled, never
  waited on; the slip shows it is thinking meanwhile; 90 seconds without an
  answer is a failure.
- **What is sent:** the sentence and the instructions with the task list,
  nothing else; `HostAssist:host.lastSent` keeps it and newton's log says
  where it went.
- Tests: `tools/host/fakemodel.py` answers from a table; demo
  `src/host/demo/assist-model.ns`, ctests `host.NewtonAssistModel` and
  `.off`.

Not built:
- the read-only lookups (Names, meetings, notes): decision 2 below is
  open, and without them nothing personal leaves the device;
- the slip's "What was sent" line (the record is kept, not yet shown);
- the Host panel's naming of the engine and where it sends (the panel has
  the switch only; the engine is chosen by environment variables);
- the engine as a permission for packages (`permissions.md` records
  grants, enforces none);
- a native engine for Ollama's own API or another provider's format.

## Owner decisions

1. Which engines to build first: local, cloud, or both.
2. Whether the lookups are allowed at all with a cloud engine, since they
   are what sends personal entries off the device.
