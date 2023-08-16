#include "server.h"

//==================================================================================

static int subnets_compare(const void *_a, const void *_b) {
	netadr_t *a, *b;
	int diff;

	a = (netadr_t *) _a;
	b = (netadr_t *) _b;

	diff = a->type - b->type;
	if (diff) return diff;

	switch (a->type) {
		case NA_IP:
			diff = memcmp(&a->ipv._4, &b->ipv._4, sizeof(a->ipv._4));
			if (diff) return diff;
			break;
		case NA_IP6: 
			diff = memcmp(&a->ipv._6, &b->ipv._6, sizeof(a->ipv._6));
			if (diff) return diff;
			break;
		default:
			break;
	}

	diff = a->port - b->port;
	if (diff) return diff;

	return 0;
}

static int subnet_adr_diff(const void *_adr, const void *_subnet) {
	netadr_t *adr, *subnet;
	int diff, cmpbytes, cmpbits;
	unsigned char adrtail, subnettail;
	char cmpmask;

	adr = (netadr_t *) _adr;
	subnet = (netadr_t *) _subnet;

	diff = adr->type - subnet->type;
	if (diff) return diff;

	cmpbytes = subnet->port / CHAR_BIT;
	cmpbits = subnet->port & (CHAR_BIT - 1);

	switch (adr->type) {
		case NA_IP:
			if (cmpbytes > sizeof(adr->ipv._4)) cmpbytes = sizeof(adr->ipv._4);
			if (cmpbytes >= 0) {
				diff = memcmp(&adr->ipv._4, &subnet->ipv._4, cmpbytes);
				if (diff) return diff;
			}
			if (cmpbits) {
				adrtail = adr->ipv._4[cmpbytes];
				subnettail = subnet->ipv._4[cmpbytes];
			}
			break;
		case NA_IP6:
			if (cmpbytes > sizeof(adr->ipv._6)) cmpbytes = sizeof(adr->ipv._6);
			if (cmpbytes >= 0) {
				diff = memcmp(&adr->ipv._6, &subnet->ipv._6, cmpbytes);
				if (diff) return diff;
			}
			if (cmpbits) {
				adrtail = adr->ipv._6[cmpbytes];
				subnettail = subnet->ipv._6[cmpbytes];
			}
			break;
		default:
			return 0;
	}

	if (cmpbits) {
		cmpmask = (char) -1 << (CHAR_BIT - cmpbits);
		diff = (int)(adrtail & cmpmask) - (int)(subnettail & cmpmask);
		if (diff) return diff;
	}

	return 0;
}

void SVM_Subnets_Init(svm_subnets_t *subnets) {
	subnets->cap = SUBNETS_CHUNK_SIZE;
	subnets->start = subnets->cur = (netadr_t *) malloc(subnets->cap * sizeof(netadr_t));
	subnets->count = 0;
	if (!subnets->start) { Com_DPrintf("ERROR: Subnet blocker: Can't allocate memory!\n"); }
}

netadr_t *SVM_Subnets_Add(svm_subnets_t *subnets) {
	netadr_t *new_adr;

	if (subnets->count >= subnets->cap) {
		subnets->cap += SUBNETS_CHUNK_SIZE;
		new_adr = (netadr_t *) realloc(subnets->start, subnets->cap * sizeof(netadr_t));
		if (!new_adr) { subnets->cap -= SUBNETS_CHUNK_SIZE; return 0; }
		subnets->cur = new_adr + subnets->count;
		subnets->start = new_adr;
	}

	++subnets->count;
	return subnets->cur++;
}

size_t SVM_Subnets_Remove(svm_subnets_t *subnets, netadr_t *adr) {  // adr must point inside subnets (found inside it)
	netadr_t *last = subnets->start + subnets->count - 1;
	*adr = *last;
	--subnets->cur;
	return --subnets->count;
}

int SVM_Subnet_SetFromString(netadr_t *adr, char* string) {
	int mask;
	if (SV_ParseCIDRNotation(adr, &mask, string)) return 1;
	adr->port = (uint16_t) mask;
	return 0;
}

int SVM_Subnets_AddFromString(svm_subnets_t *subnets, char* string) {
	netadr_t adr, *p;
	if (SVM_Subnet_SetFromString(&adr, string)) return ERR_SVM_Subnets_SetFromString;
	p = SVM_Subnets_Add(subnets);
	if (!p) return ERR_SVM_Subnets_Add;
	*p = adr;
  return 0;
}

void SVM_Subnets_AddFromFile(svm_subnets_t *subnets, char *filename) {
	FILE *f;
	char filepath[MAX_OSPATH], subnet[MAX_INFO_VALUE];

	Com_sprintf(filepath, sizeof(filepath), "%s/%s/%s", FS_GetBasePath(), FS_GetBaseGameDir(), filename);
	f = fopen(filepath, "r");
	if (!f) {
		Com_DPrintf("ERROR: Subnet blocker: Can't open file \"%s\"!\n", filepath);
		return;
	}

	while (fgets(subnet, MAX_INFO_VALUE, f)) {
		switch (SVM_Subnets_AddFromString(subnets, subnet)) {
			case ERR_SVM_Subnets_SetFromString: continue;
			case ERR_SVM_Subnets_Add: fclose(f); return;
			default: break;
		}
	}

	fclose(f);
}

void SVM_Subnets_Commit(svm_subnets_t *subnets) {
	if (subnets->count) qsort(subnets->start, subnets->count, sizeof(netadr_t), subnets_compare);
}

netadr_t *SVM_Subnets_FindByAdr(svm_subnets_t *subnets, netadr_t *adr) {
	if (!subnets->count) return 0;
	return bsearch(adr, subnets->start, subnets->count, sizeof(netadr_t), subnet_adr_diff);
}

netadr_t *SVM_Subnets_FindByAdrString(svm_subnets_t *subnets, char* string) {
	netadr_t adr;
	if (SVM_Subnet_SetFromString(&adr, string)) return 0;
	return SVM_Subnets_FindByAdr(subnets, &adr);
}

netadr_t *SVM_Subnets_FindByAdrUC(svm_subnets_t *subnets, netadr_t *adr) {  // find in uncommitted array
	size_t c = subnets->count;
	netadr_t *p = subnets->start + c - 1;
	for (; c; --c, --p) {
		if (!subnet_adr_diff(adr, p)) return p;
	}
	return 0;
}

netadr_t *SVM_Subnets_FindByAdrStringUC(svm_subnets_t *subnets, char* string) {
	netadr_t adr;
	if (SVM_Subnet_SetFromString(&adr, string)) return 0;
	return SVM_Subnets_FindByAdrUC(subnets, &adr);
}

void SVM_Subnets_Free(svm_subnets_t *subnets) {
	if (!subnets->start) return;
	free(subnets->start);
	subnets->start = 0;
}
