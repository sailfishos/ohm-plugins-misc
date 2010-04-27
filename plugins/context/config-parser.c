#include <stdlib.h>
#include <string.h>
#include "context-provider.h"

static context_key_value_pair_t * parse_key_values(const char *s);

context_info_t * parse_value(const char *s)
{
    context_info_t *ret = NULL;
    char *fkc=NULL, *p=NULL, *q=NULL, *copy=NULL;
    if (s == NULL)
	return NULL;

    copy = strdup(s);
    fkc = copy;

    while (1) {
	char *token, *part;
	int j=0;
	context_info_t *info = NULL;
	
	token = strtok_r(fkc, ",", &p);
	if (token == NULL)
	    break;
	OHM_INFO("found token %s", token);
	part = token;
	info = calloc(1, sizeof(struct context_info_s));
	if(info == NULL) {
	    /* leaks memory, but since we are out anyway we won't be able to do much.... */
	    return NULL;
	}
	info->next = ret;
	ret = info;

	for (j=0; j < 4; j++) {
	    char *subtoken;

	    subtoken = strtok_r(part, ";", &q);
	    OHM_INFO("\tfound subtoken %d %s", j+1, subtoken);
	    if (subtoken == NULL) {
		OHM_ERROR("context provider: Invalid configuration file!\n");
		return NULL;
	    }

	    switch(j) {
	    case 0:
		info->fact = strdup(subtoken);
		break;
	    case 1:
		info->kvp = parse_key_values(subtoken);
		break;
	    case 2:
		if (subtoken[1] == ':') {
		    info->field_type = subtoken[0];
		    subtoken = subtoken+2;
		}
		else {
		    info->field_type = 's';
		}
		info->field = strdup(subtoken);
		break;
	    case 3:
		info->cf_key = strdup(subtoken);
		break;
	    }

	    part = NULL;
	}
	fkc = NULL;
    }
    free(copy);
    return ret;
}

static context_key_value_pair_t * parse_key_values(const char *s)
{
    context_key_value_pair_t *ret = NULL;
    char *pairs=NULL, *p=NULL, *q=NULL, *copy=NULL;
    if (s == NULL)
	return NULL;
    if (strlen(s) < 3) /* Ignore kvp strings of length < 3 */
	return NULL;

    copy = strdup(s);
    pairs = copy;

    while (1) {
	char *kvp;
	int j;
	context_key_value_pair_t *ckvp = NULL;
	
	kvp = strtok_r(pairs, "|", &p);
	if (kvp == NULL)
	    break;

	ckvp = calloc(1, sizeof(struct context_key_value_pair_s));
	if(ckvp == NULL) {
	    /* leaks memory, but since we are out anyway we won't be able to do much.... */
	    return NULL;
	}
	ckvp->next = ret;
	ret = ckvp;

	for (j=0; j < 3; j++) {
	    char *subtoken;
	    subtoken = strtok_r(kvp, "=:", &q);
	    if (subtoken == NULL) {
		OHM_ERROR("context provider: Invalid configuration file!!");
		return NULL;
	    }
	    switch (j) {
	    case 0:
		ckvp->key = strdup(subtoken);
		break;
	    case 1:
		ckvp->type = subtoken[0];
		break;
	    case 2:
		switch(ckvp->type) {
		case 's':
		    ckvp->value.s = strdup(subtoken);
		    break;
		case 'i':
		    ckvp->value.i = atoi(subtoken);
		    break;
		default:
		    OHM_ERROR("context provider: [%s] Unsupported data type '%c' when parsing '%s' in config file",
			      __FUNCTION__, ckvp->type, subtoken);
		    exit(1);		    
		}
		break;
	    }
	    kvp = NULL;
	}
	pairs = NULL;
    }
    free(copy);
    return ret;
}
