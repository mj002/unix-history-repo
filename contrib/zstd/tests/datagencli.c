/**
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */


/*-************************************
*  Dependencies
**************************************/
#include "util.h"      /* Compiler options */
#include <stdio.h>     /* fprintf, stderr */
#include "datagen.h"   /* RDG_generate */


/*-************************************
*  Constants
**************************************/
#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define SIZE_DEFAULT ((64 KB) + 1)
#define SEED_DEFAULT 0
#define COMPRESSIBILITY_DEFAULT 50


/*-************************************
*  Macros
**************************************/
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static unsigned displayLevel = 2;


/*-*******************************************************
*  Command line
*********************************************************/
static int usage(const char* programName)
{
    DISPLAY( "Compressible data generator\n");
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [args]\n", programName);
    DISPLAY( "\n");
    DISPLAY( "Arguments :\n");
    DISPLAY( " -g#    : generate # data (default:%i)\n", SIZE_DEFAULT);
    DISPLAY( " -s#    : Select seed (default:%i)\n", SEED_DEFAULT);
    DISPLAY( " -P#    : Select compressibility in %% (default:%i%%)\n", COMPRESSIBILITY_DEFAULT);
    DISPLAY( " -h     : display help and exit\n");
    return 0;
}


int main(int argc, const char** argv)
{
    double proba = (double)COMPRESSIBILITY_DEFAULT / 100;
    double litProba = 0.0;
    U64 size = SIZE_DEFAULT;
    U32 seed = SEED_DEFAULT;
    const char* const programName = argv[0];

    int argNb;
    for(argNb=1; argNb<argc; argNb++) {
        const char* argument = argv[argNb];

        if(!argument) continue;   /* Protection if argument empty */

        /* Handle commands. Aggregated commands are allowed */
        if (*argument=='-') {
            argument++;
            while (*argument!=0) {
                switch(*argument)
                {
                case 'h':
                    return usage(programName);
                case 'g':
                    argument++;
                    size=0;
                    while ((*argument>='0') && (*argument<='9'))
                        size *= 10, size += *argument++ - '0';
                    if (*argument=='K') { size <<= 10; argument++; }
                    if (*argument=='M') { size <<= 20; argument++; }
                    if (*argument=='G') { size <<= 30; argument++; }
                    if (*argument=='B') { argument++; }
                    break;
                case 's':
                    argument++;
                    seed=0;
                    while ((*argument>='0') && (*argument<='9'))
                        seed *= 10, seed += *argument++ - '0';
                    break;
                case 'P':
                    argument++;
                    proba=0.0;
                    while ((*argument>='0') && (*argument<='9'))
                        proba *= 10, proba += *argument++ - '0';
                    if (proba>100.) proba=100.;
                    proba /= 100.;
                    break;
                case 'L':   /* hidden argument : Literal distribution probability */
                    argument++;
                    litProba=0.;
                    while ((*argument>='0') && (*argument<='9'))
                        litProba *= 10, litProba += *argument++ - '0';
                    if (litProba>100.) litProba=100.;
                    litProba /= 100.;
                    break;
                case 'v':
                    displayLevel = 4;
                    argument++;
                    break;
                default:
                    return usage(programName);
                }
    }   }   }   /* for(argNb=1; argNb<argc; argNb++) */

    DISPLAYLEVEL(4, "Data Generator \n");
    DISPLAYLEVEL(3, "Seed = %u \n", seed);
    if (proba!=COMPRESSIBILITY_DEFAULT) DISPLAYLEVEL(3, "Compressibility : %i%%\n", (U32)(proba*100));

    RDG_genStdout(size, proba, litProba, seed);
    DISPLAYLEVEL(1, "\n");

    return 0;
}
