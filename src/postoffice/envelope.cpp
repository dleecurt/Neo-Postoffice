#include "envelope.h"
#include <stdlib.h>

#define enuke(a) if(a) {TOKEN.close_token(a); a = NULL;}

MailEnvelope::MailEnvelope (void)
{
	ENV_MAILFROM = ENV_PARENTID = ENV_NAMEFROM = ENV_SUBJECT = ENV_DATE = ENV_GROUPS = NULL;
	ENV_I_READ = 0;
	ENV_I_FLAGS = 0;
}

MailEnvelope::~MailEnvelope (void)
{
	enuke(ENV_MAILFROM);
	enuke(ENV_NAMEFROM);
	enuke(ENV_SUBJECT);
	enuke(ENV_DATE);
	enuke(ENV_GROUPS);
	enuke(ENV_PARENTID);
}

void MailEnvelope::Set (int id, const char *value)
{
	switch (id)
	{
		case ENVELOPE_I_READ:
			ENV_I_READ = atoi (value);
			break;
		
		case ENVELOPE_I_PARENTID:
			enuke(ENV_PARENTID);
			ENV_PARENTID = TOKEN.open_token(value);
			break;
		
		case ENVELOPE_MAILFROM:
			enuke(ENV_MAILFROM);
			ENV_MAILFROM = TOKEN.open_token(value);
			break;
		
		case ENVELOPE_NAMEFROM:
			enuke(ENV_NAMEFROM);
			ENV_NAMEFROM = TOKEN.open_token(value);
			break;
		
		case ENVELOPE_SUBJECT:
			enuke(ENV_SUBJECT);
			ENV_SUBJECT = TOKEN.open_token(value);
			break;
		
		case ENVELOPE_DATE:
			enuke(ENV_DATE);
			ENV_DATE = TOKEN.open_token(value);
			break;
		
		case ENVELOPE_GROUPS:
			enuke(ENV_GROUPS);
			ENV_GROUPS = TOKEN.open_token(value);
			break;
		
		default:
			break;
	}
}

void MailEnvelope::SetInt (int id, int value)
{
	switch (id)
	{
		case ENVELOPE_I_READ:
			ENV_I_READ = value;
			break;
		
		case ENVELOPE_I_FLAGS:
			ENV_I_FLAGS = value;
			break;
		
		default:
			break;
	}
}

const char *MailEnvelope::Get (int id)
{
	const char *result = "";
	
	switch (id)
	{
		case ENVELOPE_I_READ:
			break;
		
		case ENVELOPE_I_PARENTID:
			if (ENV_PARENTID) result = ENV_PARENTID;
			break;
		
		case ENVELOPE_MAILFROM:
			if (ENV_MAILFROM) result = ENV_MAILFROM;
			break;
		
		case ENVELOPE_NAMEFROM:
			if (ENV_NAMEFROM) result = ENV_NAMEFROM;
			break;
		
		case ENVELOPE_SUBJECT:
			if (ENV_SUBJECT) result = ENV_SUBJECT;
			break;
		
		case ENVELOPE_DATE:
			if (ENV_DATE) result = ENV_DATE;
			break;
		
		case ENVELOPE_GROUPS:
			if (ENV_GROUPS) result = ENV_GROUPS;
			break;
		
		default:
			break;
	}
	
	return (result);
}

int MailEnvelope::GetInt (int id)
{
	switch (id)
	{
		case ENVELOPE_I_READ:
			return ENV_I_READ;
		case ENVELOPE_I_FLAGS:
			return ENV_I_FLAGS;
		default:
			return 0;
	}
}
