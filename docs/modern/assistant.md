# The Assistant with a language model behind it

**Status: a proposal.**

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

## Owner decisions

1. Which engines to build first: local, cloud, or both.
2. Whether the lookups are allowed at all with a cloud engine, since they
   are what sends personal entries off the device.
