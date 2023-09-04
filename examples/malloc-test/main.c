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
    puts("\nHello malloc!\n");

    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s MCU.\n\n", RIOT_MCU);

    char *data = (char *)malloc(32);
    char *data1 = (char *)malloc(64);
    char *data2 = (char *)malloc(128);
    char *data3 = (char *)malloc(256);
    snprintf(data3, 15, "hithisisdata\n");
    char *data4 = (char *)malloc(20);
    char *data5 = (char *)malloc(50);
    char *data6 = (char *)malloc(100);
    char *data7 = (char *)malloc(300);
    printf("%s", data3);

    printf("data : %p\n", data);
    printf("data1 : %p\n", data1);
    printf("data2 : %p\n", data2);
    printf("data3 : %p\n", data3);
    printf("data4 : %p\n", data4);
    printf("data5 : %p\n", data5);
    printf("data6 : %p\n", data6);
    printf("data7 : %p\n", data7);

    free(data);
    free(data1);
    free(data2);
    free(data3);
    free(data4);
    free(data5);
    free(data6);
    free(data7);

    printf("done!!\n");

    return 0;
}
