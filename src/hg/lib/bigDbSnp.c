/* bigDbSnp.c was originally generated by the autoSql program, which also 
 * generated bigDbSnp.h and bigDbSnp.sql.  This module links the database and
 * the RAM representation of objects. */

#include "common.h"
#include "linefile.h"
#include "dystring.h"
#include "jksql.h"
#include "bigDbSnp.h"



char *bigDbSnpCommaSepFieldNames = "chrom,chromStart,chromEnd,name,ref,altCount,alts,shiftBases,freqSourceCount,minorAlleleFreq,majorAllele,minorAllele,maxFuncImpact,class,ucscNotes,_dataOffset,_dataLen";

/* definitions for class column */
static char *values_class[] = {"snv", "mnv", "ins", "del", "delins", "identity", NULL};
static struct hash *valhash_class = NULL;

struct bigDbSnp *bigDbSnpLoad(char **row)
/* Load a bigDbSnp from row fetched with select * from bigDbSnp
 * from database.  Dispose of this with bigDbSnpFree(). */
{
struct bigDbSnp *ret;

AllocVar(ret);
ret->altCount = sqlSigned(row[5]);
ret->freqSourceCount = sqlSigned(row[8]);
ret->chrom = cloneString(row[0]);
ret->chromStart = sqlUnsigned(row[1]);
ret->chromEnd = sqlUnsigned(row[2]);
ret->name = cloneString(row[3]);
ret->ref = cloneString(row[4]);
{
int sizeOne;
sqlStringDynamicArray(row[6], &ret->alts, &sizeOne);
assert(sizeOne == ret->altCount);
}
ret->shiftBases = sqlUnsigned(row[7]);
{
int sizeOne;
sqlDoubleDynamicArray(row[9], &ret->minorAlleleFreq, &sizeOne);
assert(sizeOne == ret->freqSourceCount);
}
{
int sizeOne;
sqlStringDynamicArray(row[10], &ret->majorAllele, &sizeOne);
assert(sizeOne == ret->freqSourceCount);
}
{
int sizeOne;
sqlStringDynamicArray(row[11], &ret->minorAllele, &sizeOne);
assert(sizeOne == ret->freqSourceCount);
}
ret->maxFuncImpact = sqlUnsigned(row[12]);
ret->class = sqlEnumParse(row[13], values_class, &valhash_class);
ret->ucscNotes = cloneString(row[14]);
ret->_dataOffset = sqlLongLong(row[15]);
ret->_dataLen = sqlSigned(row[16]);
return ret;
}

struct bigDbSnp *bigDbSnpLoadAll(char *fileName) 
/* Load all bigDbSnp from a whitespace-separated file.
 * Dispose of this with bigDbSnpFreeList(). */
{
struct bigDbSnp *list = NULL, *el;
struct lineFile *lf = lineFileOpen(fileName, TRUE);
char *row[17];

while (lineFileRow(lf, row))
    {
    el = bigDbSnpLoad(row);
    slAddHead(&list, el);
    }
lineFileClose(&lf);
slReverse(&list);
return list;
}

struct bigDbSnp *bigDbSnpLoadAllByChar(char *fileName, char chopper) 
/* Load all bigDbSnp from a chopper separated file.
 * Dispose of this with bigDbSnpFreeList(). */
{
struct bigDbSnp *list = NULL, *el;
struct lineFile *lf = lineFileOpen(fileName, TRUE);
char *row[17];

while (lineFileNextCharRow(lf, chopper, row, ArraySize(row)))
    {
    el = bigDbSnpLoad(row);
    slAddHead(&list, el);
    }
lineFileClose(&lf);
slReverse(&list);
return list;
}

struct bigDbSnp *bigDbSnpCommaIn(char **pS, struct bigDbSnp *ret)
/* Create a bigDbSnp out of a comma separated string. 
 * This will fill in ret if non-null, otherwise will
 * return a new bigDbSnp */
{
char *s = *pS;

if (ret == NULL)
    AllocVar(ret);
ret->chrom = sqlStringComma(&s);
ret->chromStart = sqlUnsignedComma(&s);
ret->chromEnd = sqlUnsignedComma(&s);
ret->name = sqlStringComma(&s);
ret->ref = sqlStringComma(&s);
ret->altCount = sqlSignedComma(&s);
{
int i;
s = sqlEatChar(s, '{');
AllocArray(ret->alts, ret->altCount);
for (i=0; i<ret->altCount; ++i)
    {
    ret->alts[i] = sqlStringComma(&s);
    }
s = sqlEatChar(s, '}');
s = sqlEatChar(s, ',');
}
ret->shiftBases = sqlUnsignedComma(&s);
ret->freqSourceCount = sqlSignedComma(&s);
{
int i;
s = sqlEatChar(s, '{');
AllocArray(ret->minorAlleleFreq, ret->freqSourceCount);
for (i=0; i<ret->freqSourceCount; ++i)
    {
    ret->minorAlleleFreq[i] = sqlDoubleComma(&s);
    }
s = sqlEatChar(s, '}');
s = sqlEatChar(s, ',');
}
{
int i;
s = sqlEatChar(s, '{');
AllocArray(ret->majorAllele, ret->freqSourceCount);
for (i=0; i<ret->freqSourceCount; ++i)
    {
    ret->majorAllele[i] = sqlStringComma(&s);
    }
s = sqlEatChar(s, '}');
s = sqlEatChar(s, ',');
}
{
int i;
s = sqlEatChar(s, '{');
AllocArray(ret->minorAllele, ret->freqSourceCount);
for (i=0; i<ret->freqSourceCount; ++i)
    {
    ret->minorAllele[i] = sqlStringComma(&s);
    }
s = sqlEatChar(s, '}');
s = sqlEatChar(s, ',');
}
ret->maxFuncImpact = sqlUnsignedComma(&s);
ret->class = sqlEnumComma(&s, values_class, &valhash_class);
ret->ucscNotes = sqlStringComma(&s);
ret->_dataOffset = sqlLongLongComma(&s);
ret->_dataLen = sqlSignedComma(&s);
*pS = s;
return ret;
}

void bigDbSnpFree(struct bigDbSnp **pEl)
/* Free a single dynamically allocated bigDbSnp such as created
 * with bigDbSnpLoad(). */
{
struct bigDbSnp *el;

if ((el = *pEl) == NULL) return;
freeMem(el->chrom);
freeMem(el->name);
freeMem(el->ref);
/* All strings in alts are allocated at once, so only need to free first. */
if (el->alts != NULL)
    freeMem(el->alts[0]);
freeMem(el->alts);
freeMem(el->minorAlleleFreq);
/* All strings in majorAllele are allocated at once, so only need to free first. */
if (el->majorAllele != NULL)
    freeMem(el->majorAllele[0]);
freeMem(el->majorAllele);
/* All strings in minorAllele are allocated at once, so only need to free first. */
if (el->minorAllele != NULL)
    freeMem(el->minorAllele[0]);
freeMem(el->minorAllele);
freeMem(el->ucscNotes);
freez(pEl);
}

void bigDbSnpFreeList(struct bigDbSnp **pList)
/* Free a list of dynamically allocated bigDbSnp's */
{
struct bigDbSnp *el, *next;

for (el = *pList; el != NULL; el = next)
    {
    next = el->next;
    bigDbSnpFree(&el);
    }
*pList = NULL;
}

void bigDbSnpOutput(struct bigDbSnp *el, FILE *f, char sep, char lastSep) 
/* Print out bigDbSnp.  Separate fields with sep. Follow last field with lastSep. */
{
if (sep == ',') fputc('"',f);
fprintf(f, "%s", el->chrom);
if (sep == ',') fputc('"',f);
fputc(sep,f);
fprintf(f, "%u", el->chromStart);
fputc(sep,f);
fprintf(f, "%u", el->chromEnd);
fputc(sep,f);
if (sep == ',') fputc('"',f);
fprintf(f, "%s", el->name);
if (sep == ',') fputc('"',f);
fputc(sep,f);
if (sep == ',') fputc('"',f);
fprintf(f, "%s", el->ref);
if (sep == ',') fputc('"',f);
fputc(sep,f);
fprintf(f, "%d", el->altCount);
fputc(sep,f);
{
int i;
if (sep == ',') fputc('{',f);
for (i=0; i<el->altCount; ++i)
    {
    if (sep == ',') fputc('"',f);
    fprintf(f, "%s", el->alts[i]);
    if (sep == ',') fputc('"',f);
    fputc(',', f);
    }
if (sep == ',') fputc('}',f);
}
fputc(sep,f);
fprintf(f, "%u", el->shiftBases);
fputc(sep,f);
fprintf(f, "%d", el->freqSourceCount);
fputc(sep,f);
{
int i;
if (sep == ',') fputc('{',f);
for (i=0; i<el->freqSourceCount; ++i)
    {
    fprintf(f, "%g", el->minorAlleleFreq[i]);
    fputc(',', f);
    }
if (sep == ',') fputc('}',f);
}
fputc(sep,f);
{
int i;
if (sep == ',') fputc('{',f);
for (i=0; i<el->freqSourceCount; ++i)
    {
    if (sep == ',') fputc('"',f);
    fprintf(f, "%s", el->majorAllele[i]);
    if (sep == ',') fputc('"',f);
    fputc(',', f);
    }
if (sep == ',') fputc('}',f);
}
fputc(sep,f);
{
int i;
if (sep == ',') fputc('{',f);
for (i=0; i<el->freqSourceCount; ++i)
    {
    if (sep == ',') fputc('"',f);
    fprintf(f, "%s", el->minorAllele[i]);
    if (sep == ',') fputc('"',f);
    fputc(',', f);
    }
if (sep == ',') fputc('}',f);
}
fputc(sep,f);
fprintf(f, "%u", el->maxFuncImpact);
fputc(sep,f);
if (sep == ',') fputc('"',f);
sqlEnumPrint(f, el->class, values_class);
if (sep == ',') fputc('"',f);
fputc(sep,f);
if (sep == ',') fputc('"',f);
fprintf(f, "%s", el->ucscNotes);
if (sep == ',') fputc('"',f);
fputc(sep,f);
fprintf(f, "%lld", el->_dataOffset);
fputc(sep,f);
fprintf(f, "%d", el->_dataLen);
fputc(lastSep,f);
}

/* -------------------------------- End autoSql Generated Code -------------------------------- */