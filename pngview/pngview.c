//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2013 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#define _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "backgroundLayer.h"
#include "imageLayer.h"
#include "key.h"
#include "loadpng.h"
#include "font.h"

#include "bcm_host.h"

//-------------------------------------------------------------------------

#define NDEBUG

//-------------------------------------------------------------------------

const char *program = NULL;

//-------------------------------------------------------------------------

volatile bool run = true;

//-------------------------------------------------------------------------

static void
signalHandler(
    int signalNumber)
{
    switch (signalNumber)
    {
    case SIGINT:
    case SIGTERM:

        run = false;
        break;
    };
}

//-------------------------------------------------------------------------

void usage(void)
{
    fprintf(stderr, "Usage: %s ", program);
    fprintf(stderr, "[-b <RGBA>] [-d <number>] [-l <layer>] ");
    fprintf(stderr, "[-x <offset>] [-y <offset>] [-s <text>] <file.png>\n");
    fprintf(stderr, "    -b - set background colour 16 bit RGBA\n");
    fprintf(stderr, "         e.g. 0x000F is opaque black\n");
    fprintf(stderr, "    -d - Raspberry Pi display number\n");
    fprintf(stderr, "    -l - DispmanX layer number\n");
    fprintf(stderr, "    -x - offset (pixels from the left)\n");
    fprintf(stderr, "    -y - offset (pixels from the top)\n");
    fprintf(stderr, "    -t - timeout in ms\n");
    fprintf(stderr, "    -n - non-interactive mode\n");
    fprintf(stderr, "    -s - string to display below image\n");

    exit(EXIT_FAILURE);
}

//-------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    uint16_t background = 0x000F;
    int32_t layer = 1;
    uint32_t displayNumber = 0;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
    uint32_t timeout = 0;
    bool xOffsetSet = false;
    bool yOffsetSet = false;
    bool interactive = true;
    char* text = NULL;

    program = basename(argv[0]);

    //---------------------------------------------------------------------

    int opt = 0;

    while ((opt = getopt(argc, argv, "b:d:l:x:y:ns:")) != -1)
    {
        switch(opt)
        {
        case 'b':

            background = strtol(optarg, NULL, 16);
            break;

        case 'd':

            displayNumber = strtol(optarg, NULL, 10);
            break;

        case 'l':

            layer = strtol(optarg, NULL, 10);
            break;

        case 'x':

            xOffset = strtol(optarg, NULL, 10);
            xOffsetSet = true;
            break;

        case 'y':

            yOffset = strtol(optarg, NULL, 10);
            yOffsetSet = true;
            break;
        
        case 't':

            timeout = atoi(optarg);
            break;

        case 'n':

            interactive = false;
            break;

        case 's':

            text= strdup(optarg);
            break;

        default:

            usage();
            break;
        }
    }

    //---------------------------------------------------------------------

    if (optind >= argc)
    {
        usage();
    }

    //---------------------------------------------------------------------

    IMAGE_LAYER_T imageLayer;

    const char *imagePath = argv[optind];

    if(strcmp(imagePath, "-") == 0)
    {
        // Use stdin
        if (loadPngFile(&(imageLayer.image), stdin) == false)
        {
            fprintf(stderr, "unable to load %s\n", imagePath);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        // Load image from path
        if (loadPng(&(imageLayer.image), imagePath) == false)
        {
            fprintf(stderr, "unable to load %s\n", imagePath);
            exit(EXIT_FAILURE);
        }
    }

    //---------------------------------------------------------------------

    if (signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perror("installing SIGINT signal handler");
        exit(EXIT_FAILURE);
    }

    //---------------------------------------------------------------------

    if (signal(SIGTERM, signalHandler) == SIG_ERR)
    {
        perror("installing SIGTERM signal handler");
        exit(EXIT_FAILURE);
    }

    //---------------------------------------------------------------------

    bcm_host_init();

    //---------------------------------------------------------------------

    DISPMANX_DISPLAY_HANDLE_T display
        = vc_dispmanx_display_open(displayNumber);
    assert(display != 0);

    //---------------------------------------------------------------------

    DISPMANX_MODEINFO_T info;
    int result = vc_dispmanx_display_get_info(display, &info);
    assert(result == 0);

    //---------------------------------------------------------------------

    BACKGROUND_LAYER_T backgroundLayer;

    if (background > 0)
    {
        initBackgroundLayer(&backgroundLayer, background, 0);
    }

    if (loadPng(&(imageLayer.image), argv[optind]) == false)
    {
        fprintf(stderr, "unable to load %s\n", argv[optind]);
    }

    createResourceImageLayer(&imageLayer, layer);

    //---------------------------------------------------------------------

    IMAGE_LAYER_T textLayer;

    if (text)
    {
        initImageLayer(&textLayer, strlen(text)*8, 16, VC_IMAGE_RGBA16);

        const RGBA8_T color = { 255, 255, 255, 255 };

        drawStringRGB(0, 0, text, &color, &(textLayer.image));

        createResourceImageLayer(&textLayer, layer+1);
    }

    //---------------------------------------------------------------------

    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    assert(update != 0);

    if (background > 0)
    {
        addElementBackgroundLayer(&backgroundLayer, display, update);
    }

    if (xOffsetSet == false)
    {
        xOffset = (info.width - imageLayer.image.width) / 2;
    }

    if (yOffsetSet == false)
    {
        yOffset = (info.height - imageLayer.image.height) / 2;
    }

    addElementImageLayerOffset(&imageLayer,
                               xOffset,
                               yOffset,
                               display,
                               update);

    if (text)
    {
        int x = 0;

        if (8 * strlen(text) < imageLayer.image.width)
        {
            x = imageLayer.image.width / 2 - 4 * strlen(text);
        }

        addElementImageLayerOffset(&textLayer,
                                   xOffset + x,
                                   yOffset + imageLayer.image.height,
                                   display,
                                   update);
    }

    result = vc_dispmanx_update_submit_sync(update);
    assert(result == 0);

    //---------------------------------------------------------------------

    int32_t step = 1;
    uint32_t currentTime = 0;

    // Sleep for 10 milliseconds every run-loop
    const int sleepMilliseconds = 10;

    while (run)
    {
        int c = 0;
        if (interactive && keyPressed(&c))
        {
            c = tolower(c);

            bool moveLayer = false;

            switch (c)
            {
            case 27:

                run = false;
                break;

            case 'a':

                xOffset -= step;
                moveLayer = true;
                break;

            case 'd':

                xOffset += step;
                moveLayer = true;
                break;

            case 'w':

                yOffset -= step;
                moveLayer = true;
                break;

            case 's':

                yOffset += step;
                moveLayer = true;
                break;

            case '+':

                if (step == 1)
                {
                    step = 5;
                }
                else if (step == 5)
                {
                    step = 10;
                }
                else if (step == 10)
                {
                    step = 20;
                }
                break;

            case '-':

                if (step == 20)
                {
                    step = 10;
                }
                else if (step == 10)
                {
                    step = 5;
                }
                else if (step == 5)
                {
                    step = 1;
                }
                break;
            }

            if (moveLayer)
            {
                update = vc_dispmanx_update_start(0);
                assert(update != 0);

                moveImageLayer(&imageLayer, xOffset, yOffset, update);

                result = vc_dispmanx_update_submit_sync(update);
                assert(result == 0);
            }
        }

        //---------------------------------------------------------------------

        usleep(sleepMilliseconds * 1000);

        currentTime += sleepMilliseconds;
        if (timeout != 0 && currentTime >= timeout)
        {
            run = false;
        }
    }

    //---------------------------------------------------------------------

    keyboardReset();

    //---------------------------------------------------------------------

    if (background > 0)
    {
        destroyBackgroundLayer(&backgroundLayer);
    }

    destroyImageLayer(&imageLayer);
    if (text)
    {
        destroyImageLayer(&textLayer);
    }

    //---------------------------------------------------------------------

    result = vc_dispmanx_display_close(display);
    assert(result == 0);

    //---------------------------------------------------------------------

    return 0;
}

