/* GTEx (Genotype Tissue Expression) tracks  */

/* Copyright (C) 2015 The Regents of the University of California 
 * See README in this or parent directory for licensing information. */

#include "common.h"
#include "hgTracks.h"
#include "hvGfx.h"
#include "rainbow.h"
#include "gtexInfo.h"
#include "gtexGeneBed.h"
#include "gtexTissue.h"
#include "gtexTissueData.h"
#include "gtexUi.h"
// TODO: move spaceSaver code to simpleTracks
#include "spaceSaver.h"

// NOTE: Sections to change for multi-region (vertical slice) display 
//       are marked with #ifdef MULTI_REGION.  WARNING: These sections
//       are a bit out-of-date (refer to #ifndef MULTI code when integrating)

struct gtexGeneExtras 
/* Track info */
    {
    double maxMedian;           /* Maximum median rpkm for all tissues */
    boolean isComparison;       /* Comparison of two sample sets (e.g. male/female). */
    boolean isDifference;       /* True if comparison is shown as a single difference graph. 
                                   False if displayed as two graphs, one oriented downward */
    char *graphType;            /* Additional info about graph (e.g. type of comparison graph */
    struct rgbColor *colors;    /* Color palette for tissues */
    };

struct gtexGeneInfo
/* GTEx gene model, names, and expression medians */
    {
    struct gtexGeneInfo *next;  /* Next in singly linked list */
    struct gtexGeneBed *geneBed;/* Gene name, id, canonical transcript, exp count and medians 
                                        from BED table */
    struct genePred *geneModel; /* Gene structure from model table */
    double *medians1;            /* Computed medians */
    double *medians2;            /* Computed medians for comparison (inverse) graph */
    int height;                  /* Item height in pixels */
    };

/***********************************************/
/* Color gene models using GENCODE conventions */

// TODO: reuse GENCODE code for some/all of this ??
// MAKE_COLOR_32 ?
struct rgbColor codingColor = {12, 12, 120}; // #0C0C78
struct rgbColor noncodingColor = {0, 100, 0}; // #006400
struct rgbColor problemColor = {254, 0, 0}; // #FE0000
struct rgbColor unknownColor = {1, 1, 1};

static struct statusColors
/* Color values for gene models */
    {
    Color coding;
    Color noncoding;
    Color problem;
    Color unknown;
    } statusColors = {0,0,0,0};

static void initGeneColors(struct hvGfx *hvg)
/* Get and cache indexes for color values */
{
if (statusColors.coding != 0)
    return;
statusColors.coding = hvGfxFindColorIx(hvg, codingColor.r, codingColor.g, codingColor.b);
statusColors.noncoding = hvGfxFindColorIx(hvg, noncodingColor.r, noncodingColor.g, noncodingColor.b);
statusColors.problem = hvGfxFindColorIx(hvg, problemColor.r, problemColor.g, problemColor.b);
statusColors.unknown = hvGfxFindColorIx(hvg, unknownColor.r, unknownColor.g, unknownColor.b);
}

static Color getTranscriptStatusColor(struct hvGfx *hvg, struct gtexGeneBed *geneBed)
/* Find GENCODE color for transcriptClass  of canonical transcript */
{
initGeneColors(hvg);
if (geneBed->transcriptClass == NULL)
    return statusColors.unknown;
if (sameString(geneBed->transcriptClass, "coding"))
    return statusColors.coding;
if (sameString(geneBed->transcriptClass, "nonCoding"))
    return statusColors.noncoding;
if (sameString(geneBed->transcriptClass, "problem"))
    return statusColors.problem;
return statusColors.unknown;
}

/***********************************************/
/* Cache tissue info */

struct gtexTissue *getTissues()
/* Get and cache tissue metadata from database */
{
static struct gtexTissue *gtexTissues = NULL;

if (!gtexTissues)
    gtexTissues = gtexGetTissues();
return gtexTissues;
}

int getTissueCount()
/* Get and cache the number of tissues in GTEx tissue table */
{
static int tissueCount = 0;

if (!tissueCount)
    tissueCount = slCount(getTissues());
return tissueCount;
}

char *getTissueName(int id)
/* Get tissue name from id, cacheing */
{
static char **tissueNames = NULL;

struct gtexTissue *tissue;
int count = getTissueCount();
if (!tissueNames)
    {
    struct gtexTissue *tissues = getTissues();
    AllocArray(tissueNames, count);
    for (tissue = tissues; tissue != NULL; tissue = tissue->next)
        tissueNames[tissue->id] = cloneString(tissue->name);
    }
if (id >= count)
    errAbort("GTEx tissue table problem: can't find id %d\n", id);
return tissueNames[id];
}

struct rgbColor *getGtexTissueColors()
/* Get RGB colors from tissue table */
{
struct gtexTissue *tissues = getTissues();
struct gtexTissue *tissue = NULL;
int count = slCount(tissues);
struct rgbColor *colors;
AllocArray(colors, count);
int i = 0;
for (tissue = tissues; tissue != NULL; tissue = tissue->next)
    {
    // TODO: reconcile 
    colors[i] = (struct rgbColor){.r=COLOR_32_BLUE(tissue->color), .g=COLOR_32_GREEN(tissue->color), .b=COLOR_32_RED(tissue->color)};
    //colors[i] = mgColorIxToRgb(NULL, tissue->color);
    i++;
    }
return colors;
}

/*****************************************************************/
/* Load sample data, gene info, and anything else needed to draw */

static struct hash *loadGeneModels(char *table)
/* Load gene models from table */
{
struct sqlConnection *conn = hAllocConn(database);
struct sqlResult *sr;
char **row;
int rowOffset;
sr = hRangeQuery(conn, table, chromName, winStart, winEnd, NULL, &rowOffset);

struct hash *modelHash = newHash(0);
struct genePred *model = NULL;
while ((row = sqlNextRow(sr)) != NULL)
    {
    model = genePredLoad(row+rowOffset);
    hashAdd(modelHash, model->name, model);
    }
sqlFreeResult(&sr);
hFreeConn(&conn);
return modelHash;
}

static void loadComputedMedians(struct gtexGeneInfo *geneInfo, struct gtexGeneExtras *extras)
/* Compute medians based on graph type.  Returns a list of 2 for comparison graph types */
{
struct gtexGeneBed *geneBed = geneInfo->geneBed;
int expCount = geneBed->expCount;
if (extras->isComparison)
    {
    // create two score hashes, one for each sample subset
    struct hash *scoreHash1 = hashNew(0), *scoreHash2 = hashNew(0);
    struct sqlConnection *conn = hAllocConn("hgFixed");
    char query[1024];
    char **row;
    sqlSafef(query, sizeof(query), "select gtexSampleData.sample, gtexDonor.gender, gtexSampleData.tissue, gtexSampleData.score from gtexSampleData, gtexSample, gtexDonor where gtexSampleData.geneId='%s' and gtexSampleData.sample=gtexSample.sampleId and gtexSample.donor=gtexDonor.name", geneBed->geneId);
    struct sqlResult *sr = sqlGetResult(conn, query);
    while ((row = sqlNextRow(sr)) != NULL)
        {
        char gender = *row[1];
        // TODO: generalize for other comparison graphs (this code just for M/F comparison)
        struct hash *scoreHash = ((gender == 'F') ? scoreHash1 : scoreHash2);
        char *tissue = cloneString(row[2]);
        struct slDouble *score = slDoubleNew(sqlDouble(row[3]));

        // create hash of lists of scores, keyed by tissue name
        double *tissueScores = hashFindVal(scoreHash, tissue);
        if (tissueScores)
            slAddHead(tissueScores, score);
        else
            hashAdd(scoreHash, tissue, score);
        }
    sqlFreeResult(&sr);
    hFreeConn(&conn);

    // get tissue medians for each sample subset
    double *medians1;
    double *medians2;
    AllocArray(medians1, expCount);
    AllocArray(medians2, expCount);
    int i;
    for (i=0; i<geneBed->expCount; i++)
        {
        //medians1[i] = -1, medians2[i] = -1;       // mark missing tissues ?
        struct slDouble *scores;
        scores = hashFindVal(scoreHash1, getTissueName(i));
        if (scores)
            medians1[i] = slDoubleMedian(scores);
        scores = hashFindVal(scoreHash2, getTissueName(i));
        if (scores)
            medians2[i] = slDoubleMedian(scores);
        }
    if (extras->isDifference)
        {
        for (i=0; i<geneBed->expCount; i++)
            {
            if (medians1[i] >= medians2[i])
                {
                medians1[i] -= medians2[i];
                medians2[i] = 0;
                }
            else
                {
                medians2[i] -= medians1[i];
                medians1[i] = 0;
                }
            }
        }
    geneInfo->medians1 = medians1;
    geneInfo->medians2 = medians2;

    }
else
    {
    // TODO: compute median for single graph based on filtering of sample set
    }
}

static int gtexGeneItemHeight(struct track *tg, void *item);

static void gtexGeneLoadItems(struct track *tg)
/* Load method for track items */
{
/* Get track UI info */
struct gtexGeneExtras *extras;
AllocVar(extras);
tg->extraUiData = extras;
char *samples = cartUsualStringClosestToHome(cart, tg->tdb, FALSE, 
                                                GTEX_SAMPLES, GTEX_SAMPLES_DEFAULT);
extras->graphType = cloneString(samples);
if (sameString(samples, GTEX_SAMPLES_COMPARE_SEX))
    extras->isComparison = TRUE;
char *comparison = cartUsualStringClosestToHome(cart, tg->tdb, FALSE, GTEX_COMPARISON_DISPLAY,
                        GTEX_COMPARISON_DEFAULT);
extras->isDifference = sameString(comparison, GTEX_COMPARISON_DIFF) ? TRUE : FALSE;
extras->maxMedian = gtexMaxMedianScore(NULL);

/* Get geneModels in range */
//TODO: version the table name, move to lib
char *modelTable = "gtexGeneModel";
struct hash *modelHash = loadGeneModels(modelTable);

/* Get geneBeds (names and all-sample tissue median scores) in range */
bedLoadItem(tg, tg->table, (ItemLoader)gtexGeneBedLoad);

/* Create geneInfo items with BED and geneModels */
struct gtexGeneInfo *geneInfo = NULL, *list = NULL;
struct gtexGeneBed *geneBed = (struct gtexGeneBed *)tg->items;

/* Load tissue colors: GTEx or rainbow */
char *colorScheme = cartUsualStringClosestToHome(cart, tg->tdb, FALSE, GTEX_COLORS, 
                        GTEX_COLORS_DEFAULT);
if (sameString(colorScheme, GTEX_COLORS_GTEX))
    {
    extras->colors = getGtexTissueColors();
    }
else
    {
    int expCount = geneBed->expCount;
    extras->colors = getRainbow(&saturatedRainbowAtPos, expCount);
    }
while (geneBed != NULL)
    {
    AllocVar(geneInfo);
    geneInfo->geneBed = geneBed;
    geneInfo->geneModel = hashFindVal(modelHash, geneBed->geneId);
    slAddHead(&list, geneInfo);
    geneBed = geneBed->next;
    geneInfo->geneBed->next = NULL;
    if (extras->isComparison && (tg->visibility == tvFull || tg->visibility == tvPack))
        // compute medians based on configuration (comparisons, and later, filters)
        loadComputedMedians(geneInfo, extras);
    geneInfo->height = gtexGeneItemHeight(tg, geneInfo);
    }
slReverse(&list);
tg->items = list;
}

/***********************************************/
/* Draw */

/* Bargraph layouts for three window sizes */
#define WIN_MAX_GRAPH 20000
#define MAX_GRAPH_HEIGHT 100
#define MAX_BAR_WIDTH 5
#define MAX_GRAPH_PADDING 2

#define WIN_MED_GRAPH 500000
#define MED_GRAPH_HEIGHT 60
#define MED_BAR_WIDTH 3
#define MED_GRAPH_PADDING 1

#define MIN_GRAPH_HEIGHT 20
#define MIN_BAR_WIDTH 1
#define MIN_GRAPH_PADDING 0

#define MARGIN_WIDTH 1

static int gtexBarWidth()
{
#ifdef MULTI_REGION
int winSize = virtWinBaseCount; // GALT CHANGED OLD winEnd - winStart;
#else
int winSize = winEnd - winStart;
#endif
if (winSize < WIN_MAX_GRAPH)
    return MAX_BAR_WIDTH;
else if (winSize < WIN_MED_GRAPH)
    return MED_BAR_WIDTH;
else
    return MIN_BAR_WIDTH;
}

static int gtexGraphPadding()
{
#ifdef MULTI_REGION
int winSize = virtWinBaseCount; // GALT CHANGED OLD winEnd - winStart;
#else
int winSize = winEnd - winStart;
#endif

if (winSize < WIN_MAX_GRAPH)
    return MAX_GRAPH_PADDING;
else if (winSize < WIN_MED_GRAPH)
    return MED_GRAPH_PADDING;
else
    return MIN_GRAPH_PADDING;
}

static int gtexMaxGraphHeight()
{
#ifdef MULTI_REGION
int winSize = virtWinBaseCount; // GALT CHANGED OLD winEnd - winStart;
#else
int winSize = winEnd - winStart;
#endif
if (winSize < WIN_MAX_GRAPH)
    return MAX_GRAPH_HEIGHT;
else if (winSize < WIN_MED_GRAPH)
    return MED_GRAPH_HEIGHT;
else
    return MIN_GRAPH_HEIGHT;
}

static int gtexGraphWidth(struct gtexGeneInfo *geneInfo)
/* Width of GTEx graph in pixels */
{
int barWidth = gtexBarWidth();
int padding = gtexGraphPadding();
struct gtexGeneBed *geneBed = geneInfo->geneBed;
int count = geneBed->expCount;
int labelWidth = geneInfo->medians2 ? tl.mWidth : 0;
return (barWidth * count) + (padding * (count-1)) + labelWidth + 2;
}

static int gtexGraphX(struct gtexGeneBed *gtex)
/* Locate graph on X, relative to viewport. Return -1 if it won't fit */
{
int start = max(gtex->chromStart, winStart);
double scale = scaleForWindow(insideWidth, winStart, winEnd);
int x1 = round((start - winStart) * scale);
return x1;
}

static int gtexGeneModelHeight()
{
    return 8; 
}

static int gtexGeneMargin()
{
    return 1;
}

static int valToHeight(double val, double maxVal, int maxHeight, boolean doLogTransform)
/* Log-scale and convert a value from 0 to maxVal to 0 to maxHeight-1 */
{
if (val == 0.0)
    return 0;
double scaled = 0.0;
if (doLogTransform)
    scaled = log10(val+1.0) / log10(maxVal+1.0);
else
    scaled = val/maxVal;
if (scaled < 0)
    warn("scaled=%f\n", scaled);
return (scaled * (maxHeight-1));
}

static int valToClippedHeight(double val, double maxVal, int maxView, int maxHeight, 
                                        boolean doLogTransform)
/* Convert a value from 0 to maxVal to 0 to maxHeight-1, with clipping, or log transform the value */
{
double useVal = val;
double useMax = maxVal;
if (!doLogTransform)
    {
    useMax = maxView;
    if (val > maxView)
        useVal = maxView;
    }
return valToHeight(useVal, useMax, gtexMaxGraphHeight(), doLogTransform);
}

static void drawGraphBase(struct hvGfx *hvg, int x, int y, struct gtexGeneInfo *geneInfo)
/* Draw faint line under graph to delineate extent when bars are missing (tissue w/ 0 expression) */
{
Color lightGray = MAKECOLOR_32(0xD1, 0xD1, 0xD1);
int graphWidth = gtexGraphWidth(geneInfo);
hvGfxBox(hvg, x, y, graphWidth, 1, lightGray);
}

static int gtexGeneGraphHeight(struct track *tg, struct gtexGeneInfo *geneInfo, 
                                boolean doLogTransform, boolean doTop)
/* Determine height in pixels of graph.  This will be the box for tissue with highest expression
   If doTop is false, compute height of bottom graph of comparison */
{
struct gtexGeneBed *geneBed = geneInfo->geneBed;
int i;
double maxExp = 0.0;
int expCount = geneBed->expCount;
double expScore;
for (i=0; i<expCount; i++)
    {
    if (doTop)
        expScore = (geneInfo->medians1 ? geneInfo->medians1[i] : geneBed->expScores[i]);
    else
        expScore = geneInfo->medians2[i];
    maxExp = max(maxExp, expScore);
    }
double viewMax = (double)cartUsualIntClosestToHome(cart, tg->tdb, FALSE, 
                                GTEX_MAX_LIMIT, GTEX_MAX_LIMIT_DEFAULT);
double maxMedian = ((struct gtexGeneExtras *)tg->extraUiData)->maxMedian;
return valToClippedHeight(maxExp, maxMedian, viewMax, gtexMaxGraphHeight(), doLogTransform);
}

static void gtexGeneDrawAt(struct track *tg, void *item, struct hvGfx *hvg, int xOff, int y, 
                double scale, MgFont *font, Color color, enum trackVisibility vis)
/* Draw tissue expression bar graph over gene model. 
   Optionally, draw a second graph under gene, to compare sample sets */
{
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
struct gtexGeneBed *geneBed = geneInfo->geneBed;
boolean doLogTransform = cartUsualBooleanClosestToHome(cart, tg->tdb, FALSE, GTEX_LOG_TRANSFORM, 
                                                GTEX_LOG_TRANSFORM_DEFAULT);
// Color in dense mode using transcriptClass
Color statusColor = getTranscriptStatusColor(hvg, geneBed);
if (vis != tvFull && vis != tvPack)
    {
    bedDrawSimpleAt(tg, geneBed, hvg, xOff, y, scale, font, statusColor, vis);
    return;
    }

int heightPer = tg->heightPer;
int graphX = gtexGraphX(geneBed);
if (graphX < 0)
    return;

int topGraphHeight = gtexGeneGraphHeight(tg, geneInfo, doLogTransform, TRUE);
topGraphHeight = max(topGraphHeight, tl.fontHeight);
int yZero = topGraphHeight + y - 1;  // yZero is bottom of graph

#ifndef MULTI_REGION
int x1 = xOff + graphX;         // x1 is at left of graph
int keepX = x1;                 // FIXME:  Too many X's!
drawGraphBase(hvg, keepX, yZero+1, geneInfo);

int startX = x1;
struct rgbColor lineColor = {.r=0};
int lineColorIx = hvGfxFindColorIx(hvg, lineColor.r, lineColor.g, lineColor.b);
int barWidth = gtexBarWidth();
int graphPadding = gtexGraphPadding();
char *colorScheme = cartUsualStringClosestToHome(cart, tg->tdb, FALSE, GTEX_COLORS, 
                        GTEX_COLORS_DEFAULT);
Color labelColor = MG_GRAY;
Color clipColor = MG_MAGENTA;

// add labels to comparison graphs
// TODO: generalize
if (geneInfo->medians2)
    {
    hvGfxText(hvg, x1, yZero - tl.fontHeight + 1, labelColor, font, "F");
    hvGfxText(hvg, x1, yZero + gtexGeneModelHeight() + gtexGeneMargin() + 1, labelColor, font, "M");
    startX = startX + tl.mWidth+2;
    x1 = startX;
    }

// draw bar graph
// TODO: share this code with other graph
double viewMax = (double)cartUsualIntClosestToHome(cart, tg->tdb, FALSE, 
                                GTEX_MAX_LIMIT, GTEX_MAX_LIMIT_DEFAULT);
double maxMedian = ((struct gtexGeneExtras *)tg->extraUiData)->maxMedian;
int i;
int expCount = geneBed->expCount;
struct gtexGeneExtras *extras = (struct gtexGeneExtras *)tg->extraUiData;
for (i=0; i<expCount; i++)
    {
    struct rgbColor fillColor = extras->colors[i];
    if (barWidth == 1 && sameString(colorScheme, GTEX_COLORS_GTEX))
        {
        // brighten colors a bit so they'll be more visible at this scale
        fillColor = gtexTissueBrightenColor(fillColor);
        }
    int fillColorIx = hvGfxFindColorIx(hvg, fillColor.r, fillColor.g, fillColor.b);
    double expScore = (geneInfo->medians1 ? geneInfo->medians1[i] : geneBed->expScores[i]);
    int height = valToClippedHeight(expScore, maxMedian, viewMax, 
                                        gtexMaxGraphHeight(), doLogTransform);
    if (graphPadding == 0 || sameString(colorScheme, GTEX_COLORS_GTEX))
        hvGfxBox(hvg, x1, yZero-height+1, barWidth, height, fillColorIx);
    else
        hvGfxOutlinedBox(hvg, x1, yZero-height+1, barWidth, height, fillColorIx, lineColorIx);
    // mark clipped bar with magenta tip
    if (!doLogTransform && expScore > viewMax)
        hvGfxBox(hvg, x1, yZero-height+1, barWidth, 1, clipColor);
    x1 = x1 + barWidth + graphPadding;
    }
#endif

// draw gene model
int yGene = yZero + gtexGeneMargin() - 1;
tg->heightPer = gtexGeneModelHeight() + 1;
struct linkedFeatures *lf = linkedFeaturesFromGenePred(tg, geneInfo->geneModel, FALSE);
lf->filterColor = statusColor;
linkedFeaturesDrawAt(tg, lf, hvg, xOff, yGene, scale, font, color, tvSquish);
tg->heightPer = heightPer;

if (!geneInfo->medians2)
    return;

#ifndef MULTI_REGION
// draw comparison bar graph (upside down)
x1 = startX;
yZero = yGene + gtexGeneModelHeight() + 1; // yZero is at top of graph
drawGraphBase(hvg, keepX, yZero-1, geneInfo);

for (i=0; i<expCount; i++)
    {
    struct rgbColor fillColor = extras->colors[i];
    if (barWidth == 1 && sameString(colorScheme, GTEX_COLORS_GTEX))
        {
        // brighten colors a bit so they'll be more visible at this scale
        struct hslColor hsl = mgRgbToHsl(fillColor);
        hsl.s = min(1000, hsl.s + 300);
        fillColor = mgHslToRgb(hsl);
        }
    int fillColorIx = hvGfxFindColorIx(hvg, fillColor.r, fillColor.g, fillColor.b);
    double expScore = geneInfo->medians2[i];
    int height = valToClippedHeight(expScore, maxMedian, viewMax, gtexMaxGraphHeight(), 
                                        doLogTransform);
    if (graphPadding == 0 || sameString(colorScheme, GTEX_COLORS_GTEX))
        hvGfxBox(hvg, x1, yZero, barWidth, height, fillColorIx);
    else
        hvGfxOutlinedBox(hvg, x1, yZero, barWidth, height, fillColorIx, lineColorIx);
    if (!doLogTransform && expScore > viewMax)
        hvGfxBox(hvg, x1, yZero + height, barWidth, 1, clipColor);
    x1 = x1 + barWidth + graphPadding;
    }
#endif
}

#ifdef MULTI_REGION
static int gtexGeneNonPropPixelWidth(struct track *tg, void *item)
/* Return end chromosome coordinate of item, including graph */
{
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
int graphWidth = gtexGraphWidth(geneInfo);
return graphWidth;
}
#endif

#ifdef MULTI_REGION
static void gtexGeneNonPropDrawAt(struct track *tg, void *item, struct hvGfx *hvg, int xOff, int y,
                double scale, MgFont *font, Color color, enum trackVisibility vis)
{
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
struct gtexGeneBed *geneBed = geneInfo->geneBed;

// Color in dense mode using transcriptClass
// GALT REMOVE Color statusColor = getTranscriptStatusColor(hvg, geneBed);
if (vis != tvFull && vis != tvPack)
    {
    //GALT bedDrawSimpleAt(tg, geneBed, hvg, xOff, y, scale, font, statusColor, vis);
    return;
    }
int i;
int expCount = geneBed->expCount;
double maxMedian = ((struct gtexGeneExtras *)tg->extraUiData)->maxMedian;
struct rgbColor lineColor = {.r=0};
int lineColorIx = hvGfxFindColorIx(hvg, lineColor.r, lineColor.g, lineColor.b);
// GALT REMOVE int heightPer = tg->heightPer;

int graphX = gtexGraphX(geneBed);
if (graphX < 0)
    return;
int x1 = xOff + graphX; // x1 is at left of graph
int startX = x1;
int yZero = gtexMaxGraphHeight() + y - 1; // yZero is at bottom of graph

// draw faint line under graph to delineate extent when bars are missing (tissue w/ 0 expression)
// TODO: skip missing bars
Color lightGray = MAKECOLOR_32(0xD1, 0xD1, 0xD1);
int graphWidth = gtexGraphWidth(geneInfo);
hvGfxBox(hvg, x1, yZero+1, graphWidth, 1, lightGray);

int barWidth = gtexBarWidth();
int graphPadding = gtexGraphPadding();

char *colorScheme = cartUsualStringClosestToHome(cart, tg->tdb, FALSE, GTEX_COLORS,
                        GTEX_COLORS_DEFAULT);
Color labelColor = MG_GRAY;

if (geneInfo->medians2)
    {
    // add labels to comparison graphs
    // TODO: generalize
    hvGfxText(hvg, x1, yZero-tl.fontHeight, labelColor, font, "F");
    hvGfxText(hvg, x1, yZero + gtexGeneModelHeight() + gtexGeneMargin(), labelColor, font, "M");
    startX = startX + tl.mWidth + 2;
    x1 = startX;
    }
for (i=0; i<expCount; i++)
    {
    struct rgbColor fillColor = extras->colors[i];
    if (barWidth == 1 && sameString(colorScheme, GTEX_COLORS_GTEX))
        {
        // brighten colors a bit so they'll be more visible at this scale
        fillColor = gtexTissueBrightenColor(fillColor);
        }
    int fillColorIx = hvGfxFindColorIx(hvg, fillColor.r, fillColor.g, fillColor.b);

    double expScore = (geneInfo->medians1 ? geneInfo->medians1[i] : geneBed->expScores[i]);
    int height = valToHeight(expScore, maxMedian, gtexMaxGraphHeight(), doLogTransform);
    if (graphPadding == 0 || sameString(colorScheme, GTEX_COLORS_GTEX))
        hvGfxBox(hvg, x1, yZero-height, barWidth, height, fillColorIx);
    else
        hvGfxOutlinedBox(hvg, x1, yZero-height, barWidth, height, fillColorIx, lineColorIx);
    x1 = x1 + barWidth + graphPadding;
    }

// mark gene extent
int yGene = yZero + gtexGeneMargin() - 1;

/* GALT NOT DONE HERE NOW
// draw gene model
tg->heightPer = gtexGeneModelHeight()+1;
struct linkedFeatures *lf = linkedFeaturesFromGenePred(tg, geneInfo->geneModel, FALSE);
lf->filterColor = statusColor;
// GALT linkedFeaturesDrawAt(tg, lf, hvg, xOff, yGene, scale, font, color, tvSquish);
tg->heightPer = heightPer;
*/
if (!geneInfo->medians2)
    return;
// draw comparison graph (upside down)
x1 = startX;
yZero = yGene + gtexGeneModelHeight(); // yZero is at top of graph
for (i=0; i<expCount; i++)
    {
    struct rgbColor fillColor = extras->colors[i];
    if (barWidth == 1 && sameString(colorScheme, GTEX_COLORS_GTEX))
        {
        // brighten colors a bit so they'll be more visible at this scale
        struct hslColor hsl = mgRgbToHsl(fillColor);
        hsl.s = min(1000, hsl.s + 300);
        fillColor = mgHslToRgb(hsl);
        }
    int fillColorIx = hvGfxFindColorIx(hvg, fillColor.r, fillColor.g, fillColor.b);
    double expScore = geneInfo->medians2[i];
    int height = valToHeight(expScore, maxMedian, gtexMaxGraphHeight(), doLogTransform);
    if (graphPadding == 0 || sameString(colorScheme, GTEX_COLORS_GTEX))
        hvGfxBox(hvg, x1, yZero, barWidth, height, fillColorIx);
    else
        hvGfxOutlinedBox(hvg, x1, yZero, barWidth, height, fillColorIx, lineColorIx);
    x1 = x1 + barWidth + graphPadding;
    }
}
#endif

static int gtexGeneItemHeightOptionalMax(struct track *tg, void *item, boolean isMax)
{
if (tg->visibility == tvSquish || tg->visibility == tvDense)
    return 0;
if (isMax)
    {
    int extra = 0;
    if (((struct gtexGeneExtras *)tg->extraUiData)->isComparison)
        extra = gtexMaxGraphHeight() + 2;
    return gtexMaxGraphHeight() + gtexGeneMargin() + gtexGeneModelHeight() + extra;
    }
if (item == NULL)
    return 0;
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
if (geneInfo->height != 0)
    return geneInfo->height;
boolean doLogTransform = cartUsualBooleanClosestToHome(cart, tg->tdb, FALSE, GTEX_LOG_TRANSFORM, 
                                                GTEX_LOG_TRANSFORM_DEFAULT);
int topGraphHeight = gtexGeneGraphHeight(tg, geneInfo, doLogTransform, TRUE);
topGraphHeight = max(topGraphHeight, tl.fontHeight);
int bottomGraphHeight = 0;
boolean isComparison = ((struct gtexGeneExtras *)tg->extraUiData)->isComparison;
if (isComparison)
    {
    bottomGraphHeight = max(gtexGeneGraphHeight(tg, geneInfo, doLogTransform, FALSE),
                                tl.fontHeight) + gtexGeneMargin();
    }
int height = topGraphHeight + bottomGraphHeight + gtexGeneMargin() + gtexGeneModelHeight();
return height;
}

static int gtexGeneMaxHeight(struct track *tg)
/* Maximum height in pixels of a gene graph */
{
return gtexGeneItemHeightOptionalMax(tg, NULL, TRUE);
}

static int gtexGeneItemHeight(struct track *tg, void *item)
{
return gtexGeneItemHeightOptionalMax(tg, item, FALSE);
}

static void gtexGeneDrawItemsFull(struct track *tg, int seqStart, int seqEnd,
                                      struct hvGfx *hvg, int xOff, int yOff, int width,
                                      MgFont *font, Color color, enum trackVisibility vis)
/* Draw GTEx gene graphs in full mode.  Special handling as they are variable height */
{
double scale = scaleForWindow(width, seqStart, seqEnd);
struct slList *item;
int y = yOff + 1;
for (item = tg->items; item != NULL; item = item->next)
    {
    tg->drawItemAt(tg, item, hvg, xOff, y, scale, font, color, vis);
    genericDrawNextItem(tg, item, hvg, xOff, y, scale, color, vis);
    int height = gtexGeneItemHeight(tg, item);
    y += height;
    }
}

void gtexGeneDrawItems(struct track *tg, int seqStart, int seqEnd, 
                        struct hvGfx *hvg, int xOff, int yOff, int width, 
                        MgFont *font, Color color, enum trackVisibility vis)
/* Draw GTEx gene graphs, which are of variable height so require custom layout in full
 * and pack modes */
{
if (vis == tvDense || vis == tvSquish)
    genericDrawItems(tg, seqStart, seqEnd, hvg, xOff, yOff, width, font, color, vis);
else if (vis == tvFull)
    gtexGeneDrawItemsFull(tg, seqStart, seqEnd, hvg, xOff, yOff, width, font, color, vis);
else if (vis == tvPack)
    genericDrawItems(tg, seqStart, seqEnd, hvg, xOff, yOff, width, font, color, vis);
}

static char *tissueExpressionText(struct gtexTissue *tissue, double expScore, 
                                        boolean doLogTransform, char *qualifier)
/* Construct mouseover text for tissue graph */
{
static char buf[128];
safef(buf, sizeof(buf), "%s (%.1f %s%s%sRPKM)", tissue->description, 
                                doLogTransform ? log10(expScore+1.0) : expScore,
                                qualifier != NULL ? qualifier : "",
                                qualifier != NULL ? " " : "",
                                doLogTransform ? "log " : "");
return buf;
}

static void gtexGeneMapItem(struct track *tg, struct hvGfx *hvg, void *item, char *itemName, 
                        char *mapItemName, int start, int end, int x, int y, int width, int height)
/* Create a map box for each tissue (bar in the graph) or a single map for squish/dense modes */
{
if (tg->visibility == tvDense || tg->visibility == tvSquish)
    {
    genericMapItem(tg, hvg, item, itemName, itemName, start, end, x, y, width, height);
    return;
    }
struct gtexGeneExtras *extras = (struct gtexGeneExtras *)tg->extraUiData;
struct gtexTissue *tissues = getTissues();
struct gtexTissue *tissue = NULL;
struct gtexGeneInfo *geneInfo = item;
struct gtexGeneBed *geneBed = geneInfo->geneBed;
int barWidth = gtexBarWidth();
int padding = gtexGraphPadding();
double maxMedian = ((struct gtexGeneExtras *)tg->extraUiData)->maxMedian;

int graphX = gtexGraphX(geneBed);
if (graphX < 0)
    return;
// x1 is at left of graph
int x1 = insideX + graphX;

if (geneInfo->medians2)
    {
    // skip over labels in comparison graphs
    x1 = x1 + tl.mWidth+ 2;
    }
int i = 0;

boolean doLogTransform = cartUsualBooleanClosestToHome(cart, tg->tdb, FALSE, GTEX_LOG_TRANSFORM, 
                                                GTEX_LOG_TRANSFORM_DEFAULT);
int topGraphHeight = gtexGeneGraphHeight(tg, geneInfo, doLogTransform, TRUE);
topGraphHeight = max(topGraphHeight, tl.fontHeight);        // label
int yZero = topGraphHeight + y - 1;  // yZero is bottom of (top) graph

double viewMax = (double)cartUsualIntClosestToHome(cart, tg->tdb, FALSE, 
                                GTEX_MAX_LIMIT, GTEX_MAX_LIMIT_DEFAULT);
for (tissue = tissues; tissue != NULL; tissue = tissue->next, i++)
    {
    double expScore =  (geneInfo->medians1 ? geneInfo->medians1[i] : geneBed->expScores[i]);
    int height = valToClippedHeight(expScore, maxMedian, viewMax, 
                                        gtexMaxGraphHeight(), doLogTransform);
    char *qualifier = NULL;
    if (extras->isComparison && extras->isDifference)
        qualifier = "F-M";
    mapBoxHc(hvg, start, end, x1, yZero-height, barWidth, height, tg->track, mapItemName,  
                tissueExpressionText(tissue, expScore, doLogTransform, qualifier));
    // add map box to comparison graph
    if (geneInfo->medians2)
        {
        double expScore = geneInfo->medians2[i];
        int height = valToClippedHeight(expScore, maxMedian, viewMax, 
                                        gtexMaxGraphHeight(), doLogTransform);
        int y = yZero + gtexGeneModelHeight() + gtexGeneMargin();  // y is top of bottom graph
        if (extras->isComparison && extras->isDifference)
            qualifier = "M-F";
        mapBoxHc(hvg, start, end, x1, y, barWidth, height, tg->track, mapItemName,
                tissueExpressionText(tissue, expScore, doLogTransform, qualifier));
        }
    x1 = x1 + barWidth + padding;
    }
}

static char *gtexGeneItemName(struct track *tg, void *item)
/* Return gene name */
{
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
struct gtexGeneBed *geneBed = geneInfo->geneBed;
return geneBed->name;
}

static int gtexGeneHeight(void *item)
{
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
assert(geneInfo->height != 0);
return geneInfo->height;
}

static int gtexGeneTotalHeight(struct track *tg, enum trackVisibility vis)
/* Figure out total height of track. Set in track and also return it */
{
int height = 0;
struct gtexGeneInfo *item;
if (tg->visibility == tvSquish || tg->visibility == tvDense)
    {
    height = tgFixedTotalHeightOptionalOverflow(tg, vis, tl.fontHeight+1, tl.fontHeight, FALSE);
    }
else if (tg->visibility == tvFull)
    {
    for (item = tg->items; item != NULL; item = item->next)
        height += gtexGeneItemHeight(tg, item);
    }
else if (tg->visibility == tvPack)
    {
    if (!tg->ss)
        {
        // layout -- initially as fixed height
        int height = gtexGeneMaxHeight(tg);
        tgFixedTotalHeightOptionalOverflow(tg, vis, height, height, FALSE); // TODO: allow oflow ?
        }
    // set variable height rows
    if (!tg->ss->rowSizes)
        height = spaceSaverSetRowHeights(tg->ss, gtexGeneHeight);
    else
        height = spaceSaverGetRowHeightsTotal(tg->ss);
    }
tg->height = height;
return height;
}

static int gtexGeneItemStart(struct track *tg, void *item)
/* Return end chromosome coordinate of item, including graph */
{
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
struct gtexGeneBed *geneBed = geneInfo->geneBed;
return geneBed->chromStart;
}

static int gtexGeneItemEnd(struct track *tg, void *item)
/* Return end chromosome coordinate of item, including graph */
{
struct gtexGeneInfo *geneInfo = (struct gtexGeneInfo *)item;
struct gtexGeneBed *geneBed = geneInfo->geneBed;
double scale = scaleForWindow(insideWidth, winStart, winEnd);
int graphWidth = gtexGraphWidth(geneInfo);
return max(geneBed->chromEnd, max(winStart, geneBed->chromStart) + graphWidth/scale);
}

void gtexGeneMethods(struct track *tg)
{
tg->drawItems = gtexGeneDrawItems;
tg->drawItemAt = gtexGeneDrawAt;
tg->loadItems = gtexGeneLoadItems;
//tg->freeItems = gtexGeneFreeItems;
tg->mapItem = gtexGeneMapItem;
tg->itemName = gtexGeneItemName;
tg->mapItemName = gtexGeneItemName;
tg->itemHeight = gtexGeneItemHeight;
tg->itemStart = gtexGeneItemStart;
tg->itemEnd = gtexGeneItemEnd;
tg->totalHeight = gtexGeneTotalHeight;
#ifdef MULTI_REGION
tg->nonPropDrawItemAt = gtexGeneNonPropDrawAt;
tg->nonPropPixelWidth = gtexGeneNonPropPixelWidth;
#endif
}


