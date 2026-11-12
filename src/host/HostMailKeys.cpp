/*
	File:		host/HostMailKeys.cpp

	Contains:	DEVIATION (host addition; docs/modern/transports.md: "passwords
				in the host's key store, never in a soup"): mail accounts'
				passwords kept by the host, by account id.  See
				HostMailKeys.h.  Plain C++: no Newton headers (<windows.h>
				and <wincred.h> here).
*/

#include "HostMailKeys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#endif

namespace {

const int kMostKeys = 32;

struct Key
{
	char	id[96];
	char*	password;
};

Key		gKeys[kMostKeys];
int		gKeyCount = 0;

bool
MemoryOnly(void)
{
	static int sMemory = -1;
	if (sMemory < 0)
	{
		const char* asked = getenv("NEWTON_MAIL_KEYSTORE");
#if defined(_WIN32)
		sMemory = asked != NULL && strcmp(asked, "memory") == 0;
#else
		sMemory = 1;		// NOT YET: the Secret Service (libsecret), the Keychain
		(void) asked;
#endif
	}
	return sMemory != 0;
}

Key*
Find(const char* id)
{
	for (int i = 0; i < gKeyCount; i++)
		if (strcmp(gKeys[i].id, id) == 0)
			return &gKeys[i];
	return NULL;
}

#if defined(_WIN32)
void
Target(const char* id, wchar_t* target, size_t size)
{
	char narrow[160];
	snprintf(narrow, sizeof(narrow), "Newton mail/%s", id);
	MultiByteToWideChar(CP_UTF8, 0, narrow, -1, target, (int) size);
}
#endif

} // namespace


const char*
HostMailKeyStoreName(void)
{
	return MemoryOnly() ? "memory" : "Windows Credential Manager";
}

bool
HostMailKeySet(const char* id, const char* password)
{
	if (!MemoryOnly())
	{
#if defined(_WIN32)
		wchar_t target[160];
		Target(id, target, 160);
		CREDENTIALW cred;
		memset(&cred, 0, sizeof(cred));
		cred.Type = CRED_TYPE_GENERIC;
		cred.TargetName = target;
		cred.CredentialBlobSize = (DWORD) strlen(password);
		cred.CredentialBlob = (LPBYTE) password;
		cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
		cred.UserName = (LPWSTR) L"Newton";
		return CredWriteW(&cred, 0) != FALSE;
#endif
	}
	Key* key = Find(id);
	if (key == NULL)
	{
		if (gKeyCount == kMostKeys)
			return false;
		key = &gKeys[gKeyCount++];
		snprintf(key->id, sizeof(key->id), "%s", id);
		key->password = NULL;
	}
	free(key->password);
	key->password = (char*) malloc(strlen(password) + 1);
	strcpy(key->password, password);
	return true;
}

char*
HostMailKeyGet(const char* id)
{
	if (!MemoryOnly())
	{
#if defined(_WIN32)
		wchar_t target[160];
		Target(id, target, 160);
		PCREDENTIALW cred = NULL;
		if (CredReadW(target, CRED_TYPE_GENERIC, 0, &cred))
		{
			char* password = (char*) malloc(cred->CredentialBlobSize + 1);
			memcpy(password, cred->CredentialBlob, cred->CredentialBlobSize);
			password[cred->CredentialBlobSize] = 0;
			CredFree(cred);
			return password;
		}
#endif
	}
	else
	{
		Key* key = Find(id);
		if (key != NULL && key->password != NULL)
		{
			char* copy = (char*) malloc(strlen(key->password) + 1);
			strcpy(copy, key->password);
			return copy;
		}
	}
	// the main account's from the environment
	const char* fromEnvironment = getenv("NEWTON_MAIL_PASSWORD");
	if (strcmp(id, "main") == 0 && fromEnvironment != NULL)
	{
		char* copy = (char*) malloc(strlen(fromEnvironment) + 1);
		strcpy(copy, fromEnvironment);
		return copy;
	}
	return NULL;
}

void
HostMailKeyForget(const char* id)
{
	if (!MemoryOnly())
	{
#if defined(_WIN32)
		wchar_t target[160];
		Target(id, target, 160);
		CredDeleteW(target, CRED_TYPE_GENERIC, 0);
#endif
		return;
	}
	Key* key = Find(id);
	if (key != NULL)
	{
		free(key->password);
		*key = gKeys[--gKeyCount];
	}
}
