#ifndef TEST_RECORD_GRAPH_H
#define TEST_RECORD_GRAPH_H

#include <stdio.h>
#include "../src/TwGraph.h"

/* The test executable supplies the existing renderer factories at link time. */
void TestGraph_RecordTo(FILE *output);
int TestGraph_LiveTextObjects(void);

#endif
