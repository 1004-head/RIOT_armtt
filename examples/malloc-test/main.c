/*
 * Copyright (C) 2014 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       Hello World application
 *
 * @author      Kaspar Schleiser <kaspar@schleiser.de>
 * @author      Ludwig Knüpfer <ludwig.knuepfer@fu-berlin.de>
 *
 * @}
 */

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    puts("Hello malloc!");

    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s MCU.\n", RIOT_MCU);

    char *data = (char *)malloc(32);
    char *data1 = (char *)malloc(64);
    char *data2 = (char *)malloc(128);
    char *data3 = (char *)malloc(256);
    char *data4 = (char *)malloc(20);
    char *data5 = (char *)malloc(50);
    char *data6 = (char *)malloc(100);
    char *data7 = (char *)malloc(200);
    printf("data ptr : %p\n", data);
    printf("data1 ptr : %p\n", data1);
    printf("data2 ptr : %p\n", data2);
    printf("data3 ptr : %p\n", data3);
    printf("data4 ptr : %p\n", data4);
    printf("data5 ptr : %p\n", data5);
    printf("data6 ptr : %p\n", data6);
    printf("data7 ptr : %p\n", data7);

    if(data || data1 || data2 || data3)
    {
    snprintf(data, 32, "Hello\n");
    snprintf(data1, 64, "hello\n");
    snprintf(data2, 128, "HI\n");
    snprintf(data3, 256, "hi\n");
    printf("%s", data);
    printf("%s", data1);
    printf("%s", data2);
    printf("%s", data3);
    free(data);
    free(data1);
    free(data2);
    free(data3);
    }

    char *data8 = (char *)malloc(17);
    char *data9 = (char *)malloc(44);
    char *data10 = (char *)malloc(90);
    char *data11 = (char *)malloc(230);
    printf("data8 ptr : %p\n", data8);
    printf("data9 ptr : %p\n", data9);
    printf("data10 ptr : %p\n", data10);
    printf("data11 ptr : %p\n", data11);

    free(data4);
    free(data5);
    free(data6);
    free(data7);
    free(data8);
    free(data9);
    free(data10);
    free(data11);

    return 0;
}
