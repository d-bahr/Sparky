#include "Sort.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Selection sort
void StableSort(void * start, void * end, size_t objLen, SortFunc f)
{
    assert(start != NULL);
    assert(end != NULL);
    assert(end >= start);

    char * startC = (char *) start;
    char * endC = (char *) end;
    size_t len = endC - startC;
    assert(len % objLen == 0);

    if (len / objLen < 2)
        return;

    void * tmp = malloc(objLen);

    // Iterate from (start+1) to end
    char * s = startC + objLen;
    while (s < endC)
    {
        // Iterate backward to find insertion point.
        char * z = s - objLen;
        while (z >= startC)
        {
            if (!f(s, z)) // z <= s ?
                break;
            z -= objLen;
        }

        // z now points to the item just before the swap point.
        // Increment to get to the actual swap point.
        z += objLen;

        assert(z >= startC);
        assert(s >= z);
        assert((s - z) % objLen == 0);

        // Don't swap if not needed.
        if (z < s)
        {
            // Shuffle everything down by one.
            memcpy(tmp, s, objLen);
            memmove(z + objLen, z, s - z);
            memcpy(z, tmp, objLen);
        }

        s += objLen;
    }

    free(tmp);
}
