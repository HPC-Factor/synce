/* $Id$ */
#define _BSD_SOURCE 1
#define _SVID_SOURCE 1
#include "matchmaker.h"
#include "rapi.h"
#include <synce_log.h>
#include <synce_ini.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* PARTNERS =
	"Software\\Microsoft\\Windows CE Services\\Partners";

static const char* CURRENT_PARTNER  = "PCur";
static const char* PARTNER_ID       = "PId";
static const char* PARTNER_NAME     = "PName";

static const char* RRA_DIRECTORY    = "rra";

static const char* PARTERSHIP_FILENAME = "partner";
static const char* PARTERSHIP_SECTION  = "partnership";

struct _RRA_Matchmaker
{
  HKEY keys[3];
};
 
#define KEY_PARTNERS    0
#define KEY_PARTNER_1   1
#define KEY_PARTNER_2   2
#define KEY_COUNT       3

RRA_Matchmaker* rra_matchmaker_new()
{
  HKEY partnersKey;
  
  if (rapi_reg_create_key(
				HKEY_LOCAL_MACHINE, 
				PARTNERS, 
				&partnersKey))
  {
    RRA_Matchmaker* result = calloc(1, sizeof(RRA_Matchmaker));
    
    if (result)
      result->keys[KEY_PARTNERS] = partnersKey;
    
    return result;
  }
  else
    return NULL;
}

void rra_matchmaker_destroy(RRA_Matchmaker* matchmaker)
{
  if (matchmaker)
  {
    int i;
    
    for (i = 0; i < KEY_COUNT; i++)
      if (matchmaker->keys[i])
        CeRegCloseKey(matchmaker->keys[i]);

    free(matchmaker);
  }
}

static bool rra_matchmaker_create_key(RRA_Matchmaker* matchmaker, uint32_t index)
{
  if (index == 1 || index == 2) 
  {
    if (matchmaker->keys[index])
    {
      /* Already have this key */
      return true;
    }
    else
    {
      char name[MAX_PATH];
      snprintf(name, sizeof(name), "%s\\P%i", PARTNERS, index);
      return rapi_reg_create_key(HKEY_LOCAL_MACHINE, name, &matchmaker->keys[index]);
    }
  }
  else
    return false;
}

static bool rra_matchmaker_open_key(RRA_Matchmaker* matchmaker, uint32_t index)
{
  if (index == 1 || index == 2) 
  {
    if (matchmaker->keys[index])
    {
      /* Already have this key */
      return true;
    }
    else
    {
      char name[MAX_PATH];
      snprintf(name, sizeof(name), "%s\\P%i", PARTNERS, index);
      return rapi_reg_open_key(HKEY_LOCAL_MACHINE, name, &matchmaker->keys[index]);
    }
  }
  else
    return false;
}

bool rra_matchmaker_set_current_partner(RRA_Matchmaker* matchmaker, uint32_t index)
{
	return 
		(index == 1 || index == 2) &&
		rapi_reg_set_dword(matchmaker->keys[KEY_PARTNERS], CURRENT_PARTNER, index);
}

bool rra_matchmaker_get_current_partner(RRA_Matchmaker* matchmaker, uint32_t* index)
{
	return 
		rapi_reg_query_dword(matchmaker->keys[KEY_PARTNERS], CURRENT_PARTNER, index);
}

bool rra_matchmaker_set_partner_id(RRA_Matchmaker* matchmaker, uint32_t index, uint32_t id)
{
	HKEY partner_key = 0;

	bool success = 
		rra_matchmaker_create_key(matchmaker, index) &&
		rapi_reg_set_dword(matchmaker->keys[index], PARTNER_ID, id);

	if (partner_key)
		CeRegCloseKey(partner_key);

	return success;
}

bool rra_matchmaker_get_partner_id(RRA_Matchmaker* matchmaker, uint32_t index, uint32_t* id)
{
	HKEY partner_key = 0;

	bool success = 
		rra_matchmaker_open_key(matchmaker, index) &&
		rapi_reg_query_dword(matchmaker->keys[index], PARTNER_ID, id);

	if (partner_key)
		CeRegCloseKey(partner_key);

	return success;
}

bool rra_matchmaker_set_partner_name(RRA_Matchmaker* matchmaker, uint32_t index, const char* name)
{
	HKEY partner_key = 0;
	
	bool success =
		rra_matchmaker_open_key(matchmaker, index) &&
		rapi_reg_set_string(matchmaker->keys[index], PARTNER_NAME, name);
	
	if (partner_key)
		CeRegCloseKey(partner_key);
	
	return success;
}

bool rra_matchmaker_get_partner_name(RRA_Matchmaker* matchmaker, uint32_t index, char** name)
{
	HKEY partner_key = 0;

	bool success = 
		(index == 1 || index == 2) &&
		rra_matchmaker_open_key(matchmaker, index) &&
		rapi_reg_query_string(matchmaker->keys[index], PARTNER_NAME, name);

	if (partner_key)
		CeRegCloseKey(partner_key);

	return success;	
}

static char* rra_matchmaker_get_filename(uint32_t id)
{
  char* path = NULL;
  char filename[256];

  if (!synce_get_subdirectory(RRA_DIRECTORY, &path))
    return NULL;

  snprintf(filename, sizeof(filename), "%s/%s-%08x", path, PARTERSHIP_FILENAME, id);

  free(path);

  return strdup(filename);
}

bool rra_matchmaker_replace_partnership(RRA_Matchmaker* matchmaker, uint32_t index)
{
  bool success = false;
  char hostname[256];
  uint32_t other_id = 0;
  uint32_t id = 0;
  SynceInfo* info = synce_info_new(NULL);
  char* filename = NULL;
  char* p;

  if (!info)
  {
    goto exit;
  }
  
  if (index != 1 && index != 2)
  {
    synce_error("Bad index: %i", index);
    goto exit;
  }

  if (0 != gethostname(hostname, sizeof(hostname)))
  {
    synce_error("Failed to get hostname");
    goto exit;
  }

  /* only use the part of the hostname before the first '.' char */
  for (p = hostname; *p != '\0'; p++)
    if ('.' == *p) 
    {
      *p = '\0';
      break;
    }

  if (!rra_matchmaker_get_partner_id(matchmaker, 3-index, &other_id))
    other_id = 0;

  srandom(time(NULL));

  for (;;)
  {
    char* filename = NULL;
    struct stat dummy;
    
    /* better id generation? */
    id = (uint32_t)random();

    filename = rra_matchmaker_get_filename(id);
    
    if (0 == stat(filename, &dummy))
      id = 0;       /* file exists, try another id */

    free(filename);

    if (0 == id || 0xffffffff == id || other_id == id)
      continue;

    break;
  }

  success = 
    rra_matchmaker_set_partner_id(matchmaker, index, id) &&
    rra_matchmaker_set_partner_name(matchmaker, index, hostname);

  if (success)
  {
    FILE* file = NULL;

    if (!(filename = rra_matchmaker_get_filename(id)))
    {
      synce_error("Failed to get filename for partner id %08x", id);
      goto exit;
    }
    
    if ((file = fopen(filename, "w")) == NULL)
    {
      synce_error("Failed to open file for writing: %s", filename);
      goto exit;
    }

    fprintf(file,
        "[device]\n"
        "name=%s\n"
        "\n"
        "[%s]\n"
        "%s=%i\n"
        "%s=%i\n"
        "%s=%s\n"
        ,
        info->name,
        PARTERSHIP_SECTION,
        CURRENT_PARTNER,  index,
        PARTNER_ID,       id,
        PARTNER_NAME,     hostname);

    fclose(file);
  }

exit:
  synce_info_destroy(info);
  if (filename)
    free(filename);
  return success;
}

bool rra_matchmaker_create_partnership(RRA_Matchmaker* matchmaker, uint32_t* index)
{
  bool success = false;
  int i;
  uint32_t ids[2];
  SynceIni* ini = NULL;

  for (i = 0; i < 2; i++)
  {
    if (!rra_matchmaker_get_partner_id(matchmaker, i+1, &ids[i]))
      ids[i] = 0;

    /* see if we have a partnership file */
    if (ids[i])
    {
      char* filename = rra_matchmaker_get_filename(ids[i]);

      if (!filename)
      {
        synce_error("Failed to get filename for partner id %08x", ids[i]);
        goto exit;
      }

      ini = synce_ini_new(filename);
      free(filename);

      if (ini)
      {
        const char* local_name = synce_ini_get_string(ini, PARTERSHIP_SECTION, PARTNER_NAME);
        char* remote_name = NULL;

        /* verify that the hostnames match */
        if (local_name &&
            rra_matchmaker_get_partner_name(matchmaker, i+1, &remote_name) &&
            remote_name &&
            0 == strcmp(local_name, remote_name))
        {
          free(remote_name);
          *index = i+1;
          success = true;
          goto exit;
        }
        else
        {
          synce_trace("Local host name '%s' and remote host name '%s' do not match", 
              local_name, remote_name);
        }
      }
      else
      {
        synce_trace("Partnership file not found for ID %08x", ids[i]);
      }
    }
    else
    {
      synce_trace("Partnership slot %i is empty on device", i+1);
    }
  
    /* prepare for next ini file */
    synce_ini_destroy(ini);
    ini = NULL;
  }

  /* If we get here, we have no partnership with the device.
     We try to create a partnership on an empty slot */

  for (i = 0; i < 2; i++)
  {
    if (0 == ids[i])
    {
      if (rra_matchmaker_replace_partnership(matchmaker, i+1))
      {
        *index = i+1;
        success = true;
        goto exit;
      }
    }
  }

  synce_error("Partnership not found and there are no empty partner slots on device.");

exit:
  synce_ini_destroy(ini);
  if (success)
    success = rra_matchmaker_set_current_partner(matchmaker, *index);
  return success;
}

