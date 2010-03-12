#ifdef TEST
#include <stdio.h>
#define OHM_ERROR  printf
#endif
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
	    if (subtoken == NULL) {
#ifndef TEST
		OHM_ERROR("context provider: Invalid configuration file!");
#endif
		break;
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
		OHM_ERROR("context provider: Invalid configuration file!");
		exit(1);
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

#ifdef TEST
int main(int argc, char *argv[])
{
    int i;
    for (i=1; i < argc; i++) {
	context_info_t *values;
	
	for(values = parse_value(argv[i]); values != NULL; values = values->next) {
	    context_key_value_pair_t *kvp;
	    printf("%s: { ", values->fact);
	    printf("/%c:%s/ ", values->field_type, values->field);

	    for(kvp = values->kvp; kvp != NULL; kvp = kvp->next) {
		switch(kvp->type) {
		case 's':
		    printf("%s: '%s' ",kvp->key, kvp->value.s);
		    break;
		case 'i':
		    printf("%s: %d ",kvp->key, kvp->value.i);
		    break;
		default:
		    printf("%s: 0x%x ",kvp->key, kvp->value.i);
		    break;
		}
		
	    }
	    printf("} => %s\n", values->cf_key);
	}
    }
    return 0;
}
#endif
